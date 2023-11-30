#pragma once
#pragma pack(push, 1)

struct WAVHeader {
    char chunkID[4];
    int chunkSize;
    char format[4];

    char subchunk1ID[4];
    int subchunk1Size;
    short audioFormat;
    short numChannels;
    int sampleRate;
    int byteRate;
    short blockAlign;
    short bitsPerSample;

    char subchunk2ID[4];
    int subchunk2Size;
};

#pragma pack(pop)