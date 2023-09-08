#include <iostream>
#include <string>
#include <thread>
#include <map>
#include <vector>
#include <csignal>
#include "alsaLib.hpp"
#include "../cpplibs/ssocket.hpp"
#include "../cpplibs/libjson.hpp"
using namespace std;

enum Mode {
    PLAY,
    CAPTURE
};

struct cl_data {
    Mode mode;
    int rate;
    int channels;
    int bitdepth;
};

_snd_pcm_format inttoformat(int i) {
    switch (i)
    {
    case 16:
        return SND_PCM_FORMAT_S16_LE;
        break;

    // case 24:
    //     return SND_PCM_FORMAT_S24_LE;
    //     break;
    
    default:
        return SND_PCM_FORMAT_S32_LE;
        break;
    }
}

// vector<cl_data> play_buff;
map<string, string> play_status;
map<string, cl_data> play_setup;
map<string, PCM*> pcms;

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
    string id = client.srecv(1024);

    while (play_setup.find(id) == play_setup.end()) continue;

    Mode mode = play_setup[id].mode;
    int rate = play_setup[id].rate;
    int format = play_setup[id].bitdepth;
    int channels = play_setup[id].channels;

    cout << "Player: connected user: " << id << endl;

    PCM pcm("default", (_snd_pcm_stream)mode, 0);
    pcm.setAccess(SND_PCM_ACCESS_RW_INTERLEAVED);
    pcm.setFormat(inttoformat(format));
    pcm.setChannels(channels);
    pcm.setRate(rate);
    pcm.paramsApply();
    pcm.prepare();

    pcms[id] = &pcm;

    int period = pcm.getPeriod();
    size_t buff_size = period * channels * (format / 8);
    client.ssend(to_string(buff_size));

    if (mode == PLAY) {
        while (true) {
            recvdata wavdata = client.srecv_char(buff_size);
            client.ssend("ok");

            // if (stoi(netparams[1]) == 16) {
            //     for(int i = 0; i < buff_size; i += sizeof(int16_t)) {
            //         intbuff[i / sizeof(int16_t)] = *((int16_t*)&wavdata.value[i]);
            //     }
                
            //     for(int i = 0; i < buff_size / sizeof(int16_t); i += 1) {
            //         intbuff[i] = ((int32_t)intbuff[i]) << 16;
            //     }
                
            // }

            if (play_status[id] == "STOP") {
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

            if (pcm.writei(wavdata.value, period) == -EPIPE) {// * (format / 8)
                cout << "xrun" << endl;
                pcm.recover(-EPIPE, 1);
            }
        }

    } else if (mode == CAPTURE) {
        char buff[buff_size];

        while (true) {
            if (play_status[id] == "STOP") {
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

            if (pcm.readi(buff, period) == -EPIPE) {// * (format / 8)
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
    sock.ssend(id);

    string mdata = sock.srecv(1024);
    JsonNode node = json.parse(mdata);

    if (node.size() < 4) {
        sock.sclose();
        return;
    }

    cl_data cldata;
    cldata.mode = (Mode)node["mode"].integer;
    cldata.rate = node["rate"].integer;
    cldata.bitdepth = node["width"].integer;
    cldata.channels = node["channels"].integer;

    play_setup[id] = cldata;
    sock.ssend("ok");

    cout << "Manager: connected user: " << id << endl;

    while (true) {
        string data = sock.srecv(1024);

        if (data == "start") {
            play_status[id] = "PREPARED";
        } else if (data == "pause") {
            play_status[id] = "PAUSED";
        } else if (data == "stop" || data.length() == 0) {
            play_status[id] = "STOP";
            break;
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

    while (true) thread(player, sock.saccept()).detach();

}


int main() {
    SSocket sock(AF_INET, SOCK_STREAM);
    sock.ssetsockopt(SOL_SOCKET, SO_REUSEADDR, 1);
    sock.sbind("", 53764);
    sock.slisten(0);

    thread(listener).detach();
    
    while (true) thread(manager, sock.saccept()).detach();
}
