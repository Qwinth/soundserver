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

// vector<cl_data> play_buff;
map<string, string> play_status;
map<string, cl_data> play_setup;
map<string, PCM*> pcms;
string globalDevice;

// void soundwriter() {
//     PCM pcm("default", SND_PCM_STREAM_PLAYBACK, 0);
//     pcm.setAccess(SND_PCM_ACCESS_RW_INTERLEAVED);
//     pcm.setFormat(SND_PCM_FORMAT_S32_LE);
//     pcm.setChannels(2);
//     pcm.setRate(48000, 0);
//     pcm.paramsApply();
//     pcm.prepare();
//     cl_data data;

//     while (true) {
//         if (!play_buff.empty()) {
//             if (pcm.getState() == "SETUP") {
//                 pcm.prepare();
//             }

//             data = play_buff[0];
//             cout << play_status[data.id] << endl;
//             if (play_status.find(data.id) != play_status.end()) {
//                 // cout << play_status[data.id] << endl;
//                 if (play_status[data.id] != "STOP") {
                
//                 // cout << "pcm writei" << endl;
//                     if (pcm.writei(data.data, data.period) == -EPIPE) {
//                         cout << "xrun" << endl;
//                         pcm.prepare();
//                     }

//                     play_buff.erase(play_buff.begin());

//                     if (play_status[data.id] != "STOP") {
//                         play_status[data.id] = "PREPARED";
//                     } //else {
//                 //     cout << "Soundwriter: disconnected user: " << data.id << endl;
//                 // }
//                 }
//             }
//         } else {
//             this_thread::sleep_for(1ms);
//             // pcm.prepare();
//         }

//         if (play_status.empty()) {
//             pcm.drop();
//         }
//     }
// }

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

    if (mode == PLAY) {
        while (true) {
            sockrecv_t wavdata = client.srecv(buff_size);
            client.ssend("ok");

            // if (stoi(netparams[1]) == 16) {
            //     for(int i = 0; i < buff_size; i += sizeof(int16_t)) {
            //         intbuff[i / sizeof(int16_t)] = *((int16_t*)&wavdata.value[i]);
            //     }
                
            //     for(int i = 0; i < buff_size / sizeof(int16_t); i += 1) {
            //         intbuff[i] = ((int32_t)intbuff[i]) << 16;
            //     }
                
            // }

            if (play_status[id] == "STOPPED") {
                pcm.drop();
                pcm.close();
                client.sclose();
                pcms.erase(id);
                cout << "Player: disconnected user: " << id << endl;
                break;
            }
            
            else if (play_status[id] == "PAUSED") {
                pcm.pause();
                while (play_status[id] == "PAUSED") { this_thread::sleep_for(1ms); }
            }

            if (pcm.writei(wavdata.buffer, period) == -EPIPE) {
                cout << "xrun" << endl;
                pcm.recover(-EPIPE, 1);
            }
        }

    } else if (mode == CAPTURE) {
        char buff[buff_size];

        while (true) {
            if (play_status[id] == "STOPPED") {
                pcm.drop();
                pcm.close();
                client.sclose();
                pcms.erase(id);
                cout << "Player: disconnected user: " << id << endl;
                break;
            }

            else if (play_status[id] == "PAUSED") {
                pcm.pause();
                while (play_status[id] == "PAUSED") { this_thread::sleep_for(1ms); }
            }

            if (pcm.readi(buff, period) == -EPIPE) {
                cout << "xrun" << endl;
                pcm.recover(-EPIPE, 1);
            }

            client.ssend(buff, buff_size);
        }
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

    cout << "Manager: connected user: " << id << endl;

    while (true) {
        string data = sock.srecv(1024).string;
    
        if (data == "start") {
            play_status[id] = "RUNNING";
            start = doubleTime();
        } else if (data == "pause") {
            play_status[id] = "PAUSED";
        } else if (data == "stop" || data.length() == 0) {
            // cout << pcms[id]->bufferAvailable() << endl;
            play_status[id] = "STOPPED";
            break;
        } else if (data == "is_running") {
            int preverrno = errno;

            if (params["check_is_running"].str == "use_buffer_experimental") {
                int frames = pcms[id]->bufferAvailable();
                sock.ssend((frames >= pcms[id]->getBufferSize() - 100 || frames < 0) ? "0" : "1");
            } else if (params["check_is_running"].str == "use_timer") sock.ssend((doubleTime() - start >= duration) ? "0" : "1");

            errno = preverrno;
        }
    }
    sock.sclose();
    cout << "Manager: disconnected user: " << id << endl;
}

// void handler(SSocket sock) {
//     int err;
//     string recv = sock.srecv(1024);
//     vector<string> netparams = split(recv, ";");
//     if (netparams.size() < 3) {
//         sock.sclose();
//         return;
//     }
//     _snd_pcm_format format;
//     switch (stoi(netparams[0]))
//     {
//     case 16:
//         format = SND_PCM_FORMAT_S16_LE;
//         break;
    
//     case 32:
//         format = SND_PCM_FORMAT_S32_LE;
//         break;
    
//     case 64:
//         format = SND_PCM_FORMAT_FLOAT64_LE;
//         break;
    
//     default:
//         format = SND_PCM_FORMAT_S16_LE;
//         break;
//     }
    
//     int rate = stoi(netparams[1]);
//     int channels = stoi(netparams[2]);
//     PCM pcm("default", SND_PCM_STREAM_PLAYBACK, 0);
//     pcm.setAccess(SND_PCM_ACCESS_RW_INTERLEAVED);
//     pcm.setFormat(format);
//     pcm.setChannels(channels);
//     pcm.setRate(rate, 0);
//     pcm.paramsApply();

//     int period = pcm.getPeriod(0);
//     int buff_size = period * channels * pcm.getFormatWidth() / 8;

//     recvdata data;
//     // data.value = (char*)alloca(buff_size);
//     // data.value = {0};
//     // vector<char*> buff = {};
//     sock.ssend(to_string(buff_size));
//     // for (int i = 0; i < 4096; i++) {
//     //     data = sock.srecv_char(buff_size);
//     //     buff.push_back(data.value);
//     //     sock.ssend(to_string(buff_size));
//     // }

//     while (true) {
//         data = sock.srecv_char(buff_size);
        
//         while (err = pcm.writei(data.value, period) == -EPIPE) {
//         pcm.prepare();
//         }

//         if (data.length == 0) {
//             pcm.drop();
//             break;
//         }
//         else if (data.length < buff_size) {
//             pcm.drain();
//             break;
//         }
        
//         if (err < 0) {
//         break;
//         }
//         sock.ssend(to_string(buff_size));
//     }
//     sock.ssend("exit");
//     pcm.close();
//     sock.sclose();
    
// }
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

    globalDevice = (args["--default-device"].str != "false") ? args["--default-device"].str : "default";

    SSocket sock(AF_INET, SOCK_STREAM);
    sock.ssetsockopt(SOL_SOCKET, SO_REUSEADDR, 1);
    sock.sbind("", 53764);
    sock.slisten(0);

    thread(listener).detach();
    
    while (true) thread(manager, sock.saccept().first).detach();
}