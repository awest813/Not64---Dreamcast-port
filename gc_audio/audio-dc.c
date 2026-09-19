/**
 * Dreamcast audio plugin stub.
 * Buffers AI DMA locally. AICA / snd_stream output is Phase 3.
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

static void reset_buffer(void)
{
	write_off = 0;
	read_off = 0;
	buffered = 0;
}

EXPORT void CALL AiDacrateChanged(int SystemType)
{
	switch (SystemType) {
	case SYSTEM_NTSC:
		freq = 48681812 / (*AudioInfo.AI_DACRATE_REG + 1);
		break;
	case SYSTEM_PAL:
		freq = 49656530 / (*AudioInfo.AI_DACRATE_REG + 1);
		break;
	case SYSTEM_MPAL:
		freq = 48628316 / (*AudioInfo.AI_DACRATE_REG + 1);
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

	if (!audioEnabled)
		return;

	stream = (char *)(AudioInfo.RDRAM + (*AudioInfo.AI_DRAM_ADDR_REG & 0xFFFFFF));
	length = (int)*AudioInfo.AI_LEN_REG;

	while (length > 0 && buffered < DC_AUDIO_RING_SIZE) {
		int chunk = (int)(DC_AUDIO_RING_SIZE - write_off);
		if (chunk > length)
			chunk = length;
		if (chunk > (int)(DC_AUDIO_RING_SIZE - buffered))
			chunk = (int)(DC_AUDIO_RING_SIZE - buffered);
		if (chunk <= 0)
			break;
		memcpy(ring + write_off, stream, (size_t)chunk);
		stream += chunk;
		length -= chunk;
		write_off = (write_off + (unsigned int)chunk) % DC_AUDIO_RING_SIZE;
		buffered += (unsigned int)chunk;
	}
	(void)read_off;
	(void)freq;
}

EXPORT DWORD CALL AiReadLength(void)
{
	return 0;
}

EXPORT void CALL AiUpdate(BOOL Wait)
{
	(void)Wait;
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
