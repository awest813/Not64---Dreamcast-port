/**
 * Dreamcast audio plugin stub.
 * Buffers AI DMA locally. AiReadLength reports unplayed bytes in the last
 * DMA. AiUpdate drains the ring (stand-in until AICA/snd_stream).
 */

#include "../main/winlnxdefs.h"
#include <string.h>
#include "../platform/dc_memory.h"
#include "AudioPlugin.h"
#include "Audio_#1.1.h"

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
	if (n > pending_len)
		pending_len = 0;
	else
		pending_len -= n;
	return n;
}

unsigned int audio_dc_drain(unsigned int n)
{
	return drain_bytes(n);
}

static void reset_buffer(void)
{
	write_off = 0;
	read_off = 0;
	buffered = 0;
	pending_len = 0;
}

EXPORT void CALL AiDacrateChanged(int SystemType)
{
	unsigned int rate = 1;

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
}

EXPORT void CALL AiLenChanged(void)
{
	char *stream;
	int length;
	unsigned int addr, copied;

	if (!audioEnabled)
		return;

	if (!AudioInfo.RDRAM || !AudioInfo.AI_DRAM_ADDR_REG || !AudioInfo.AI_LEN_REG)
		return;

	/* The GC/Wii plugin masks to 16 MiB; DC RDRAM is 4 MiB with nothing
	 * mapped behind it, so clamp the source to the region that exists. */
	addr = (unsigned int)(*AudioInfo.AI_DRAM_ADDR_REG) & (DC_N64_RDRAM_SIZE - 1);
	stream = (char *)(AudioInfo.RDRAM + addr);
	length = (int)*AudioInfo.AI_LEN_REG;
	if (length < 0 || (unsigned int)length > DC_N64_RDRAM_SIZE - addr)
		length = (int)(DC_N64_RDRAM_SIZE - addr);

	copied = 0;
	while (length > 0) {
		int chunk = (int)(DC_AUDIO_RING_SIZE - write_off);
		if (chunk > length)
			chunk = length;
		if (chunk > (int)(DC_AUDIO_RING_SIZE - buffered))
			chunk = (int)(DC_AUDIO_RING_SIZE - buffered);
		if (chunk <= 0) {
			/* Ring is full: drop the oldest samples so this DMA is
			 * not silently discarded (games keep feeding AI). */
			if (!drain_bytes(DC_AUDIO_RING_SIZE / 8))
				break;
			continue;
		}
		memcpy(ring + write_off, stream, (size_t)chunk);
		stream += chunk;
		length -= chunk;
		copied += (unsigned int)chunk;
		write_off = (write_off + (unsigned int)chunk) % DC_AUDIO_RING_SIZE;
		buffered += (unsigned int)chunk;
	}
	pending_len = copied;
	(void)freq;
}

EXPORT DWORD CALL AiReadLength(void)
{
	return pending_len;
}

EXPORT void CALL AiUpdate(BOOL Wait)
{
	(void)Wait;
	/* HOST/KOS without AICA: consume the ring so a game that keeps
	 * submitting DMA does not stall on a full buffer. Hardware will
	 * replace this with snd_stream. */
	drain_bytes(buffered);
}

EXPORT void CALL CloseDLL(void)
{
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
	strcpy(PluginInfo->Name, "Not64 Dreamcast Audio (stub)");
}

EXPORT BOOL CALL InitiateAudio(AUDIO_INFO Audio_Info)
{
	AudioInfo = Audio_Info;
	reset_buffer();
	return TRUE;
}

EXPORT void CALL ProcessAlist(void)
{
}

EXPORT void CALL RomOpen(void)
{
	reset_buffer();
}

EXPORT void CALL RomClosed(void)
{
	reset_buffer();
}

void pauseAudio(void)
{
}

void resumeAudio(void)
{
}

unsigned int audio_dc_buffered(void)
{
	return buffered;
}
