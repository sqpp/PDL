/*
 * ALSA WAV playback for PDL Linux. Plays WAV file to the selected output device
 * (used by sndPlaySound from stubs_linux).
 */
#ifdef __linux__
#include "platform/pdl_linux_types.h"
#include "Headers/pdl.h"
#include "Headers/sound_in.h"
#include <alsa/asoundlib.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>

static int play_wav_sync_impl(const char *wav_path, const char *alsa_device)
{
	if (!wav_path || !wav_path[0]) return 0;
	FILE *f = fopen(wav_path, "rb");
	if (!f) return -1;
	char riff[4], wave[4];
	if (fread(riff, 1, 4, f) != 4 || memcmp(riff, "RIFF", 4) != 0) { fclose(f); return -1; }
	uint32_t file_len;
	if (fread(&file_len, 1, 4, f) != 4) { fclose(f); return -1; }
	if (fread(wave, 1, 4, f) != 4 || memcmp(wave, "WAVE", 4) != 0) { fclose(f); return -1; }
	uint16_t format_tag = 0, nch = 0, bps = 0, block_align = 0;
	uint32_t sample_rate = 0, data_len = 0;
	unsigned char *data = NULL;
	for (;;) {
		char ck[4];
		uint32_t ck_size;
		if (fread(ck, 1, 4, f) != 4 || fread(&ck_size, 1, 4, f) != 4) break;
		if (memcmp(ck, "fmt ", 4) == 0 && ck_size >= 16) {
			fread(&format_tag, 1, 2, f);
			fread(&nch, 1, 2, f);
			fread(&sample_rate, 1, 4, f);
			fseek(f, 4, SEEK_CUR);
			fread(&block_align, 1, 2, f);
			fread(&bps, 1, 2, f);
			if (ck_size > 16) fseek(f, (long)(ck_size - 16), SEEK_CUR);
		} else if (memcmp(ck, "data", 4) == 0) {
			data_len = ck_size;
			data = (unsigned char *)malloc(data_len);
			if (!data || fread(data, 1, data_len, f) != data_len) { free(data); data = NULL; break; }
			break;
		} else {
			fseek(f, (long)ck_size, SEEK_CUR);
		}
	}
	fclose(f);
	if (!data || format_tag != 1 || nch == 0 || sample_rate == 0) { free(data); return -1; }
	const char *dev = alsa_device && alsa_device[0] ? alsa_device : "default";
	snd_pcm_t *pcm = NULL;
	int err = snd_pcm_open(&pcm, dev, SND_PCM_STREAM_PLAYBACK, 0);
	if (err < 0) { free(data); return -1; }
	snd_pcm_format_t fmt = (bps == 8) ? SND_PCM_FORMAT_U8 : SND_PCM_FORMAT_S16_LE;
	err = snd_pcm_set_params(pcm, fmt, SND_PCM_ACCESS_RW_INTERLEAVED, nch, sample_rate, 1, 500000);
	if (err < 0) { snd_pcm_close(pcm); free(data); return -1; }
	size_t frame_size = (size_t)((bps/8) * nch);
	size_t written = 0;
	while (written < data_len) {
		snd_pcm_sframes_t n = snd_pcm_writei(pcm, data + written, (data_len - written) / frame_size);
		if (n < 0) { n = snd_pcm_recover(pcm, (int)n, 0); if (n < 0) break; }
		else { written += (size_t)n * frame_size; }
	}
	snd_pcm_drain(pcm);
	snd_pcm_close(pcm);
	free(data);
	return 0;
}

typedef struct { char path[1024]; char device[128]; } play_args_t;

static void *play_wav_thread(void *arg)
{
	play_args_t *a = (play_args_t *)arg;
	play_wav_sync_impl(a->path, a->device[0] ? a->device : NULL);
	free(a);
	return NULL;
}

int pdl_linux_play_wav(const char *wav_path, const char *alsa_device)
{
	if (!wav_path || !wav_path[0]) return 0;
	play_args_t *a = (play_args_t *)malloc(sizeof(play_args_t));
	if (!a) return -1;
	strncpy(a->path, wav_path, sizeof(a->path) - 1);
	a->path[sizeof(a->path) - 1] = '\0';
	if (alsa_device)
		{ strncpy(a->device, alsa_device, sizeof(a->device) - 1); a->device[sizeof(a->device) - 1] = '\0'; }
	else
		a->device[0] = '\0';
	pthread_t th;
	if (pthread_create(&th, NULL, play_wav_thread, a) != 0) { free(a); return -1; }
	pthread_detach(th);
	return 0;
}

#endif
