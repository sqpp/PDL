#ifndef SOUND_IN_H
#define SOUND_IN_H

#define DEFAULT_HI_AUDIO	1
#define DEFAULT_LO_AUDIO	0

#define INDEX512	0
#define INDEX1200	1
#define INDEX2400	2
#define INDEX1600	3
#define INDEX3200	4

extern bool bCapturing;
extern char high_audio;
extern char low_audio;
extern long BaudRate;
extern int  cross_over;

void CALLBACK Callback_Function(HWAVEIN hwi, UINT uMsg, DWORD dwInstance, DWORD dwParam1, DWORD dwParam2);
void Process_ReadyBuffers(HWND hwnd);
void free_audio_buffers(void);
BOOL Stop_Capturing(void);
BOOL Start_Capturing(void);

void Audio_To_Bits  (char *lpAudioBuffer, long LenAudioBuffer);
void MOBITEX_To_Bits(char *lpAudioBuffer, long LenAudioBuffer);
void ACARS_To_Bits  (char *lpAudioBuffer, long LenAudioBuffer);
void ERMES_To_Bits  (char *lpAudioBuffer, long LenAudioBuffer); // PH: test

void Reset_ATB(void);
void SetAudioConfig(int sac_type);
int Get_Percent(int x,int percent);

#ifdef __linux__
void pdl_linux_feed_audio(char *lpAudioBuffer, long LenAudioBuffer);
void pdl_linux_feed_audio_from_pagercast(char *lpAudioBuffer, long LenAudioBuffer);
void pdl_linux_set_pagercast_audio_exclusive(int enable);
int pdl_linux_alsa_open(const char *device, unsigned int sample_rate);
void pdl_linux_alsa_stop(void);
void pdl_linux_alsa_enumerate_capture(void (*cb)(const char *name, const char *desc, void *ctx), void *ctx);
void pdl_linux_alsa_enumerate_playback(void (*cb)(const char *name, const char *desc, void *ctx), void *ctx);
int pdl_linux_play_wav(const char *wav_path, const char *alsa_device);
double pdl_linux_get_input_level(void);  /* 0..100, raw input level for UI indicator */
/* PulseAudio: list all devices (no state filter), capture from named source */
void pdl_linux_pulse_enumerate_capture(void (*cb)(const char *name, const char *desc, void *ctx), void *ctx);
int pdl_linux_pulse_open(const char *device, unsigned int sample_rate);
void pdl_linux_pulse_close(void);
void pdl_linux_pulse_stop(void);
int pdl_linux_pulse_run(void);
double pdl_linux_pulse_get_input_level(void);
int pdl_linux_pulse_is_open(void);
#endif

#endif


