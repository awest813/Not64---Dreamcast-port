#ifndef DC_AUDIO_H
#define DC_AUDIO_H

void audio_dc_set_enabled(int enabled);
/* Discard pre-load output and use the restored AI clock, preserving pause. */
void audio_dc_restore(unsigned int country);
void pauseAudio(void);
void resumeAudio(void);
unsigned int audio_dc_buffered(void);
unsigned int audio_dc_drain(unsigned int bytes);
#ifdef DC_HOST_STUB
unsigned int audio_dc_read_pcm(unsigned char *dst, unsigned int bytes);
unsigned int audio_dc_rate(void);
#endif
#ifdef DC_EMBED_VITEST
int audio_dc_stream_test(void);
#endif
#endif
