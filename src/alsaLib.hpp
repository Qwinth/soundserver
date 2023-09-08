#include <string>
#include <alsa/asoundlib.h>

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
			int dir;
			if (int error = snd_pcm_hw_params_set_rate_near(pcm, params, &rate, &dir) < 0) throw error;
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

		int getRate() {
			int dir;
			unsigned int tmp;
			snd_pcm_hw_params_get_rate(params, &tmp, &dir);
			return tmp;
		}

		int getPeriod() {
			int dir;
			snd_pcm_uframes_t frames;
			snd_pcm_hw_params_get_period_size(params, &frames, &dir);
			return frames;
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
			return snd_pcm_writei(pcm, buff, frames);
		}

		int readi(void * buff, snd_pcm_uframes_t frames) {
			return snd_pcm_readi(pcm, buff, frames);
		}

		int pause() {
			return snd_pcm_pause(pcm, 1);
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
};
