#include <iostream>
#include <string>
#include <thread>
#include <map>
#include <vector>
#include <chrono>
#include <csignal>
#include "alsaLib.hpp"
#include "cpplibs/ssocket.hpp"
#include "cpplibs/libjson.hpp"
#include "cpplibs/argparse.hpp"
using namespace std;

struct cl_data {
    Mode mode;
    WAVHeader header;
};

long double doubleTime() { return std::chrono::duration_cast<std::chrono::duration<long double>>(std::chrono::system_clock::now().time_since_epoch()).count(); }

map<string, string> play_status;
map<string, cl_data> play_setup;
map<string, PCM*> pcms;
string globalDevice;

void sighandler(int e) {
    for (auto i : pcms) i.second->pcm_exit();
    exit(0);
}

void player(SSocket client) {
    string id = client.srecv(1024).string;

    while (play_setup.find(id) == play_setup.end()) continue;

    Mode mode = play_setup[id].mode;
    cout << "Player: connected user: " << id << endl;

    PCM pcm;
    pcm.setup(globalDevice, play_setup[id].header, mode);
    try { pcm.setBufferSize(65536); } catch (int e) {};
    pcm.paramsApply();
    pcm.prepare();

    pcms[id] = &pcm;

    int period = pcm.getPeriod();
    size_t buff_size = period * play_setup[id].header.numChannels * (play_setup[id].header.bitsPerSample / 8);
    client.ssend(to_string(buff_size));
    client.setblocking(false);

    if (mode == PLAY) {
        while (true) {
            if (play_status[id] == "PAUSED") {
                pcm.pause();
                while (play_status[id] == "PAUSED") { this_thread::sleep_for(5ms); }
            }

            if (play_status[id] == "STOPPED") {
                pcm.drop();
                pcm.close();
                client.sclose();
                pcms.erase(id);
                cout << "Player: disconnected user: " << id << endl;
                break;
            }

            sockrecv_t wavdata = client.srecv(buff_size);
            
            if (wavdata.length > 0) {
                client.ssend("ok");

                if (pcm.writei(wavdata.buffer, period) == -EPIPE) {
                    cout << "xrun" << endl;
                    pcm.recover(-EPIPE, 1);
                }
            }
        }

    } else if (mode == CAPTURE) {
        char* buff = new char[buff_size];

        while (true) {
            if (play_status[id] == "PAUSED") {
                pcm.pause();
                while (play_status[id] == "PAUSED") { this_thread::sleep_for(5ms); }
                pcm.resume();
            }

            if (play_status[id] == "STOPPED") {
                pcm.drop();
                pcm.close();
                client.sclose();
                pcms.erase(id);
                cout << "Player: disconnected user: " << id << endl;
                break;
            }

            if (pcm.readi(buff, period) == -EPIPE) {
                cout << "xrun" << endl;
                pcm.recover(-EPIPE, 1);
            }

            client.ssend(buff, buff_size);
        }

        delete[] buff;
    }

}

void manager(SSocket sock) {
    Json json;
    string id = uuid4();
    play_status[id] = "NONE";

    cl_data cldata;
    cldata.header = *((WAVHeader*)sock.srecv(44).buffer);
    cldata.mode = (Mode)stoi(sock.srecv(1).string);

    JsonNode params = json.parse(sock.srecv(1024).string);

    play_setup[id] = cldata;
    sock.ssend(id);

    long double start = 0;
    long double duration = (long double)(cldata.header.subchunk2Size / (cldata.header.numChannels * (cldata.header.bitsPerSample / 8))) / (long double)cldata.header.sampleRate;
    long double pause_time_start = 0;
    long double pause_time = 0;

    cout << "Manager: connected user: " << id << endl;

    while (true) {
        string data = sock.srecv(1024).string;

        if (data == "start") {
            play_status[id] = "RUNNING";
            start = doubleTime();
        }
        
        else if (data == "pause") {
            // cout << "paused" << endl;
            play_status[id] = "PAUSED";
            pause_time_start = doubleTime();
        }
        
        else if (data == "resume") {
            // cout << "resume" << endl;
            play_status[id] = "RUNNING";
            pause_time += doubleTime() - pause_time_start;
            sock.ssend("0");
        }
        
        else if (data == "stop" || data.length() == 0) {
            // cout << pcms[id]->bufferAvailable() << endl;
            play_status[id] = "STOPPED";
            break;
        }
        
        else if (data == "is_running") {
            int preverrno = errno;

            if (params["check_is_running"].str == "use_buffer_experimental") {
                int frames = pcms[id]->bufferAvailable();
                sock.ssend((frames >= pcms[id]->getBufferSize() - 100 || frames < 0) ? "0" : "1");
            } else if (params["check_is_running"].str == "use_timer") sock.ssend((doubleTime() - start >= duration + pause_time) ? "0" : "1");

            errno = preverrno;
        }
    }

    sock.sclose();
    cout << "Manager: disconnected user: " << id << endl;
}

void listener() {
    SSocket sock(AF_INET, SOCK_STREAM);
    sock.ssetsockopt(SOL_SOCKET, SO_REUSEADDR, 1);
    sock.sbind("", 53765);
    sock.slisten(0);

    while (true) thread(player, sock.saccept().first).detach();

}


int main(int argc, char** argv) {
    ArgumentParser parser(argc, argv);
    parser.add_argument({.flag1 = "-D", .flag2 = "--default-device"});
    auto args = parser.parse();

    globalDevice = (args["--default-device"].type != ANYNONE) ? args["--default-device"].str : "default";

    SSocket sock(AF_INET, SOCK_STREAM);
    sock.ssetsockopt(SOL_SOCKET, SO_REUSEADDR, 1);
    sock.sbind("", 53764);
    sock.slisten(0);

    thread(listener).detach();
    
    while (true) thread(manager, sock.saccept().first).detach();
}