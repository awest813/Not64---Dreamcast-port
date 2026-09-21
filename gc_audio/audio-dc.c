/**
 * Dreamcast audio plugin.
 * Buffers AI DMA locally. AiReadLength reports unplayed bytes in the last
 * DMA. KOS polls AICA on a worker so slow emulated frames cannot stall audio.
 */

#include "../main/winlnxdefs.h"
#include <string.h>
#include "../platform/dc_memory.h"
#include "../platform/dc_audio.h"
#include "AudioPlugin.h"
#include "Audio_#1.1.h"
#ifndef DC_HOST_STUB
#include <kos.h>
#include <dc/sound/stream.h>
static mutex_t audio_lock = MUTEX_INITIALIZER;
#define LOCK() mutex_lock(&audio_lock)
#define UNLOCK() mutex_unlock(&audio_lock)
static snd_stream_hnd_t output = SND_STREAM_INVALID;
static kthread_t *worker;
static int quitting, paused, started, stream_ready;
static unsigned int playing_freq;
#define STREAM_BYTES DC_AUDIO_STREAM_BYTES
static unsigned char output_buffer[STREAM_BYTES] __attribute__((aligned(32)));
#else
#define LOCK() ((void)0)
#define UNLOCK() ((void)0)
#endif

AUDIO_INFO AudioInfo;

char audioEnabled = 1;
char scalePitch;

static unsigned char ring[DC_AUDIO_RING_SIZE];
static unsigned int write_off;
static unsigned int read_off;
static unsigned int buffered;
static unsigned int freq = 33600;

static unsigned int pending_len;

static unsigned int drain_bytes(unsigned int n)
{
	if (n > buffered)
		n = buffered;
	if (n == 0)
		return 0;
	read_off = (read_off + n) % DC_AUDIO_RING_SIZE;
	buffered -= n;
	/* The latest DMA occupies the tail: older queued data drains first. */
	if (pending_len > buffered) pending_len = buffered;
	return n;
}

unsigned int audio_dc_drain(unsigned int n)
{
	LOCK();
	n = drain_bytes(n);
	UNLOCK();
	return n;
}

/* RDRAM is word-swapped: each little-endian word contains R then L.
 * Convert to interleaved native-endian L/R, padding underruns with silence.
 * The caller owns audio_lock, including KOS's synchronous fill callback. */
static unsigned int read_pcm(unsigned char *dst, unsigned int bytes)
{
	unsigned int i, available = buffered & ~3u;
	bytes &= ~3u;
	if (available > bytes) available = bytes;
	for (i = 0; i < available; i += 4) {
		unsigned int pos = (read_off + i) % DC_AUDIO_RING_SIZE;
		dst[i] = ring[(pos + 2) % DC_AUDIO_RING_SIZE];
		dst[i + 1] = ring[(pos + 3) % DC_AUDIO_RING_SIZE];
		dst[i + 2] = ring[pos];
		dst[i + 3] = ring[(pos + 1) % DC_AUDIO_RING_SIZE];
	}
	memset(dst + available, 0, bytes - available);
	drain_bytes(available);
	return available;
}

#ifdef DC_HOST_STUB
unsigned int audio_dc_read_pcm(unsigned char *dst, unsigned int bytes)
{
	return read_pcm(dst, bytes);
}
#else
static void *stream_callback(snd_stream_hnd_t handle, int requested, int *received)
{
	(void)handle;
	/* Despite older header wording, this SDK passes bytes, not samples. */
	if (requested < 0 || requested > STREAM_BYTES) {
		*received = 0;
		return NULL;
	}
	read_pcm(output_buffer, (unsigned int)requested);
	*received = requested;
	return output_buffer;
}

static void *audio_worker(void *unused)
{
	(void)unused;
	for (;;) {
		int done;
		LOCK();
		done = quitting;
		UNLOCK();
		if (done) break;
		AiUpdate(FALSE);
		thd_sleep(5);
	}
	return NULL;
}

static void stop_output(void)
{
	LOCK();
	quitting = 1;
	UNLOCK();
	if (worker) thd_join(worker, NULL);
	worker = NULL;
	if (output != SND_STREAM_INVALID) snd_stream_destroy(output);
	output = SND_STREAM_INVALID;
	if (stream_ready) snd_stream_shutdown();
	stream_ready = started = 0;
}
#endif

static void reset_buffer(void)
{
	write_off = 0;
	read_off = 0;
	buffered = 0;
	pending_len = 0;
}

void audio_dc_set_enabled(int enabled)
{
	LOCK();
	audioEnabled = !!enabled;
	if (!audioEnabled) {
		reset_buffer();
#ifndef DC_HOST_STUB
		if (started) snd_stream_stop(output);
		started = 0;
#endif
	}
	UNLOCK();
}

EXPORT void CALL AiDacrateChanged(int SystemType)
{
	unsigned int rate = 1;
	unsigned int old_freq;
	LOCK();
	old_freq = freq;

	if (AudioInfo.AI_DACRATE_REG)
		rate = *AudioInfo.AI_DACRATE_REG + 1;
	if (rate == 0)
		rate = 1;

	switch (SystemType) {
	case SYSTEM_NTSC:
		freq = 48681812 / rate;
		break;
	case SYSTEM_PAL:
		freq = 49656530 / rate;
		break;
	case SYSTEM_MPAL:
		freq = 48628316 / rate;
		break;
	default:
		freq = 33600;
		break;
	}
	if (!freq) freq = 1;
	if (freq > 48000) freq = 48000;
	if (freq != old_freq) {
		/* Queued PCM belongs to the previous clock rate. Do not replay it
		 * at the new pitch, or leave it ahead of the next DMA. */
		reset_buffer();
#ifndef DC_HOST_STUB
		if (started) snd_stream_stop(output);
		started = 0;
#endif
	}
	UNLOCK();
}

void audio_dc_restore(unsigned int country)
{
	int system;
	/* Match the core's country-to-AI-clock mapping. */
	switch (country & 0xff) {
	case 0x44: case 0x46: case 0x49: case 0x50:
	case 0x53: case 0x55: case 0x58: case 0x59:
		system = SYSTEM_PAL; break;
	default:
		system = SYSTEM_NTSC; break;
	}
	LOCK();
	reset_buffer();
#ifndef DC_HOST_STUB
	if (started) snd_stream_stop(output);
	started = 0;
#endif
	UNLOCK();
	AiDacrateChanged(system);
}

#ifdef DC_HOST_STUB
unsigned int audio_dc_rate(void) { return freq; }
#endif

EXPORT void CALL AiLenChanged(void)
{
	char *stream;
	unsigned int length, addr, first;

	if (!AudioInfo.RDRAM || !AudioInfo.AI_DRAM_ADDR_REG || !AudioInfo.AI_LEN_REG)
		return;

	/* The GC/Wii plugin masks to 16 MiB; DC RDRAM is 4 MiB with nothing
	 * mapped behind it, so clamp the source to the region that exists. */
	LOCK();
	if (!audioEnabled) {
		UNLOCK();
		return;
	}
	addr = (unsigned int)(*AudioInfo.AI_DRAM_ADDR_REG) & (DC_N64_RDRAM_SIZE - 1) & ~3u;
	stream = (char *)(AudioInfo.RDRAM + addr);
	length = (unsigned int)*AudioInfo.AI_LEN_REG;
	if (length > DC_N64_RDRAM_SIZE - addr)
		length = DC_N64_RDRAM_SIZE - addr;
	length &= ~3u;
	/* Retain the newest PCM, copying at most one ring under the lock.
	 * Skip a DMA's discarded prefix rather than copying it just to evict it. */
	if (length > DC_AUDIO_RING_SIZE) {
		stream += length - DC_AUDIO_RING_SIZE;
		length = DC_AUDIO_RING_SIZE;
	}
	if (length > DC_AUDIO_RING_SIZE - buffered)
		drain_bytes(length - (DC_AUDIO_RING_SIZE - buffered));
	first = DC_AUDIO_RING_SIZE - write_off;
	if (first > length) first = length;
	memcpy(ring + write_off, stream, first);
	memcpy(ring, stream + first, length - first);
	write_off = (write_off + length) % DC_AUDIO_RING_SIZE;
	buffered += length;
	pending_len = length;
	UNLOCK();
}

EXPORT DWORD CALL AiReadLength(void)
{
	unsigned int result;
	LOCK();
	result = pending_len;
	UNLOCK();
	return result;
}

EXPORT void CALL AiUpdate(BOOL Wait)
{
	(void)Wait;
#ifdef DC_HOST_STUB
	drain_bytes(buffered);
#else
	LOCK();
	if (output != SND_STREAM_INVALID && !paused && !quitting) {
		if (started && (!audioEnabled || playing_freq != freq)) {
			snd_stream_stop(output);
			started = 0;
		}
		if (!started && buffered && audioEnabled) {
			playing_freq = freq;
			snd_stream_start(output, freq, 1);
			started = 1;
			printf("AICA stream started: rate=%u stereo PCM16\n", freq);
		}
		if (started && snd_stream_poll(output) < 0) {
			printf("AICA stream FAIL: poll\n");
			snd_stream_stop(output);
			started = 0;
		}
	}
	UNLOCK();
#endif
}

EXPORT void CALL CloseDLL(void)
{
#ifndef DC_HOST_STUB
	stop_output();
#endif
	reset_buffer();
}

EXPORT void CALL DllAbout(HWND hParent)
{
	(void)hParent;
}

EXPORT void CALL DllConfig(HWND hParent)
{
	(void)hParent;
}

EXPORT void CALL DllTest(HWND hParent)
{
	(void)hParent;
}

EXPORT void CALL GetDllInfo(PLUGIN_INFO *PluginInfo)
{
	memset(PluginInfo, 0, sizeof(*PluginInfo));
	PluginInfo->Version = 0x0101;
	PluginInfo->Type = PLUGIN_TYPE_AUDIO;
	strcpy(PluginInfo->Name, "Not64 Dreamcast Audio");
}

EXPORT BOOL CALL InitiateAudio(AUDIO_INFO Audio_Info)
{
	CloseDLL();
	AudioInfo = Audio_Info;
	freq = 33600;
	reset_buffer();
	return TRUE;
}

EXPORT void CALL ProcessAlist(void)
{
}

EXPORT void CALL RomOpen(void)
{
	CloseDLL();
	reset_buffer();
#ifndef DC_HOST_STUB
	quitting = paused = 0;
	if (snd_stream_init_ex(2, STREAM_BYTES) < 0) {
		printf("AICA stream FAIL: init\n");
		snd_stream_shutdown();
		return;
	}
	stream_ready = 1;
	output = snd_stream_alloc(stream_callback, STREAM_BYTES);
	if (output != SND_STREAM_INVALID) worker = thd_create(0, audio_worker, NULL);
	if (!worker) {
		printf("AICA stream FAIL: allocation/worker\n");
		stop_output();
	}
#endif
}

EXPORT void CALL RomClosed(void)
{
	CloseDLL();
}

void pauseAudio(void)
{
#ifndef DC_HOST_STUB
	LOCK();
	paused = 1;
	if (started) snd_stream_stop(output);
	started = 0;
	reset_buffer();
	UNLOCK();
#endif
}

void resumeAudio(void)
{
#ifndef DC_HOST_STUB
	LOCK();
	paused = 0;
	UNLOCK();
#endif
}

unsigned int audio_dc_buffered(void)
{
	unsigned int result;
	LOCK();
	result = buffered;
	UNLOCK();
	return result;
}

#if defined(DC_EMBED_VITEST) && !defined(DC_HOST_STUB)
/* Exercise the public DMA/rate path as well as AICA-paced consumption. */
int audio_dc_stream_test(void)
{
	AUDIO_INFO saved = AudioInfo;
	DWORD address = 0, length = 32768, dacrate = 1499;
	unsigned char *pcm = malloc(32768);
	unsigned int cycle, i;
	int failed = 0, enabled = audioEnabled;
	if (!pcm) return 1;
	RomClosed();
	AudioInfo.RDRAM = pcm;
	AudioInfo.AI_DRAM_ADDR_REG = &address;
	AudioInfo.AI_LEN_REG = &length;
	AudioInfo.AI_DACRATE_REG = &dacrate;
	audio_dc_set_enabled(1);
	for (i = 0; i < 32768; i += 4) {
		int16_t tone = ((i / 4) % 80 < 40) ? 2400 : -2400;
		memcpy(pcm + i, &tone, 2);
		memcpy(pcm + i + 2, &tone, 2);
	}
	for (cycle = 0; cycle < 3; ++cycle) {
		RomOpen();
		pauseAudio();
		dacrate = cycle == 1 ? 1099 : 1499;
		AiDacrateChanged(SYSTEM_NTSC);
		AiLenChanged();
		thd_sleep(20);
		if (audio_dc_buffered() != length || AiReadLength() != length) failed = 1;
		/* Repeated notification of the same rate must preserve queued PCM. */
		AiDacrateChanged(SYSTEM_NTSC);
		if (audio_dc_buffered() != length) failed = 1;
		resumeAudio();
		/* Prefill consumes only 16384 bytes: the rest needs actual polls. */
		for (i = 0; i < 200 && audio_dc_buffered(); ++i) thd_sleep(5);
		if (audio_dc_buffered() || AiReadLength()) failed = 1;
		thd_sleep(300);
		/* Queue more data at the old rate, then change the clock while live.
		 * Holding the lock prevents the worker consuming this test queue. */
		LOCK();
		paused = 1;
		UNLOCK();
		AiLenChanged();
		dacrate = 2199;
		AiDacrateChanged(SYSTEM_NTSC);
		if (audio_dc_buffered() || AiReadLength()) failed = 1;
		AiLenChanged();
		resumeAudio();
		for (i = 0; i < 200 && audio_dc_buffered(); ++i) thd_sleep(5);
		if (audio_dc_buffered()) failed = 1;
		/* Muting stops a live stream and discards queued/future DMA. */
		LOCK();
		paused = 1;
		UNLOCK();
		AiLenChanged();
		audio_dc_set_enabled(0);
		AiLenChanged();
		if (audio_dc_buffered() || AiReadLength()) failed = 1;
		audio_dc_set_enabled(1);
		AiLenChanged();
		if (audio_dc_buffered() != length) failed = 1;
		resumeAudio();
		for (i = 0; i < 200 && audio_dc_buffered(); ++i) thd_sleep(5);
		if (audio_dc_buffered()) failed = 1;
		RomClosed();
		RomClosed();
	}
	AudioInfo = saved;
	audio_dc_set_enabled(enabled);
	free(pcm);
	printf("AICA stream %s: cycles=3 DMA/drain/pause/rate/mute/underrun/close\n",
	       failed ? "FAIL" : "PASS");
	return failed;
}
#endif
