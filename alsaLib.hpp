#pragma once
#include <string>
#include <vector>
#include <alsa/asoundlib.h>
#include <iostream>

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

enum Mode {
    PLAY,
    CAPTURE
};

_snd_pcm_format inttoformat(int i) {
	switch (i)
	{
	case 16:
		return SND_PCM_FORMAT_S16_LE;
		break;
	
	default:
		return SND_PCM_FORMAT_S32_LE;
		break;
	}
}

class PCM {
	bool isopened = false;
	_snd_pcm_format _format;

	public:
	snd_pcm_hw_params_t *params;
	snd_pcm_t *pcm;
	PCM() {}
	PCM(std::string device, _snd_pcm_stream stream, int mode) {
		if (int error = snd_pcm_open(&pcm, device.c_str(), stream, mode) < 0) throw	error;
		snd_pcm_hw_params_malloc(&params);
		snd_pcm_hw_params_any(pcm, params);
		isopened = true;
	}

	~PCM() {
		if (isopened) close();
		snd_pcm_hw_params_free(params);
	}

	void setup(std::string device, WAVHeader header, Mode mode) {
		try{ open(device, (_snd_pcm_stream)mode, 0); } catch (int e) {  if (e == 1) try { open(cardlist().back(), (_snd_pcm_stream)mode, 0); } catch(int er) { std::cerr << snd_strerror(e) << std::endl; exit(er); } }
		setAccess(SND_PCM_ACCESS_RW_INTERLEAVED);
		setFormat(inttoformat(header.bitsPerSample));
		setChannels(header.numChannels);
		setRate(header.sampleRate);
	}

	void open(std::string device, _snd_pcm_stream stream, int mode) {
		if (int error = snd_pcm_open(&pcm, device.c_str(), stream, mode) < 0) throw	error;
		snd_pcm_hw_params_malloc(&params);
		snd_pcm_hw_params_any(pcm, params);
		isopened = true;
	}
	
	void setAccess(_snd_pcm_access _access) {
		if (int error = snd_pcm_hw_params_set_access(pcm, params, _access) < 0) throw error;
	}

	void setFormat(_snd_pcm_format format) {
		_format = format;
		if (int error = snd_pcm_hw_params_set_format(pcm, params, format) < 0) throw error;
	}

	void setChannels(int channels) {
		if (int error = snd_pcm_hw_params_set_channels(pcm, params, channels) < 0) throw error;
	}

	void setRate(unsigned int rate) {
		if (int error = snd_pcm_hw_params_set_rate_near(pcm, params, &rate, 0) < 0) throw error;
	}

	void setBufferSize(int size) {
		if (int error = snd_pcm_hw_params_set_buffer_size(pcm, params, size) < 0) throw error;
	}

	void paramsApply() {
		if (int error = snd_pcm_hw_params(pcm, params) < 0) throw error;
	}

	std::string getName() {
		return std::string(snd_pcm_name(pcm));
	}

	std::string getState() {
		return std::string(snd_pcm_state_name(snd_pcm_state(pcm)));
	}

	int getChannels() {
		unsigned int tmp;
		snd_pcm_hw_params_get_channels(params, &tmp);
		return tmp;
	}

	unsigned int getRate() {
		unsigned int tmp;
		snd_pcm_hw_params_get_rate(params, &tmp, 0);
		return tmp;
	}

	int getPeriod() {
		snd_pcm_uframes_t frames;
		snd_pcm_hw_params_get_period_size(params, &frames, 0);
		return frames;
	}

	int getBufferSize() {
		snd_pcm_uframes_t buf_size;
		snd_pcm_hw_params_get_buffer_size(params, &buf_size);
		return buf_size;
	}

	int getFormatWidth() {
		return snd_pcm_format_width(_format);
	}

	void start() {
		if (int error = snd_pcm_start(pcm) < 0) throw error;
	}

	void prepare() {
		if (int error = snd_pcm_prepare(pcm) < 0) throw error;
	}

	void recover(int err, int silent) {
		if (int error = snd_pcm_recover(pcm, err, silent) < 0) throw error;
	}

	int writei(const void * buff, snd_pcm_uframes_t frames) {
		if (getState() == "PAUSED") resume();
		return snd_pcm_writei(pcm, buff, frames);
	}

	int readi(void * buff, snd_pcm_uframes_t frames) {
		if (getState() == "PAUSED") resume();
		return snd_pcm_readi(pcm, buff, frames);
	}

	int pause() {
		return snd_pcm_pause(pcm, 1);
	}

	int resume() {
		return snd_pcm_pause(pcm, 0);
	}

	int bufferAvailable() {
		return snd_pcm_avail(pcm);
	}

	void drain() {
		if (int error = snd_pcm_drain(pcm) < 0) throw error;

	}

	void drop() {
		if (int error = snd_pcm_drop(pcm) < 0) throw error;
	}

	void close() {
		isopened = false;
		if (int error = snd_pcm_close(pcm) < 0) throw error;
	}

	void pcm_exit() {
		drop();
		close();
		snd_pcm_hw_params_free(params);
	}

	std::vector<std::string> cardlist() {
		std::vector<std::string> list;
		int card = -1;

		do {
			snd_card_next(&card);
			list.push_back("default:" + std::to_string(card));
		} while (card != -1);

		list.pop_back();

		return list;
	}
};
