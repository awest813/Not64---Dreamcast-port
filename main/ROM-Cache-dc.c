/**
 * Streaming ROM cache for the Dreamcast port (1 MiB window).
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "../fileBrowser/fileBrowser.h"
#include "../r4300/r4300.h"
#include "ROM-Cache.h"
#include "../platform/dc_memory.h"
#include "../platform/dc_types.h"

#ifndef MIN
#define MIN(a,b) ((a)<(b)?(a):(b))
#endif

#define BLOCK_SIZE  (64*1024)
#define BLOCK_MASK  (BLOCK_SIZE-1)
#define BLOCK_SHIFT (16)
#define MAX_ROMSIZE (64*1024*1024)
#define NUM_BLOCKS  (MAX_ROMSIZE/BLOCK_SIZE)

static char *ROMCACHE_LO;
static u32   ROMCACHE_BYTES;
static u32   ROMSize;
static int   ROMTooBig;
static char* ROMBlocks[NUM_BLOCKS];
static int   ROMBlocksLRU[NUM_BLOCKS];
static fileBrowser_file* ROMFile;
static char readBefore = 0;

extern void pauseAudio(void);
extern void resumeAudio(void);
BOOL hasLoadedROM = FALSE;

void showLoadIcon(void) { }

static void ensure_block(u32 block);

void ROMCache_init(fileBrowser_file* f){
	readBefore = 0;
	ROMFile = f;
	ROMSize = f ? f->size : 0;
	ROMCACHE_BYTES = DC_ROM_STREAM_SIZE;
	if (!ROMCACHE_LO)
		ROMCACHE_LO = malloc(ROMCACHE_BYTES);
	ROMTooBig = ROMSize > ROMCACHE_BYTES;
	rom_length = (int)ROMSize;
	if (f)
		romFile_seekFile(f, 0, FILE_BROWSER_SEEK_SET);
}

void ROMCache_deinit(){
	free(ROMCACHE_LO);
	ROMCACHE_LO = NULL;
	memset(ROMBlocks, 0, sizeof(ROMBlocks));
	memset(ROMBlocksLRU, 0, sizeof(ROMBlocksLRU));
	ROMFile = NULL;
	ROMSize = 0;
	ROMTooBig = 0;
	readBefore = 0;
}

void* ROMCache_pointer(u32 rom_offset){
	if (!ROMCACHE_LO || ROMSize == 0)
		return NULL;
	if (rom_offset >= ROMSize)
		rom_offset = ROMSize - 1;
	if(ROMTooBig){
		u32 block = rom_offset >> BLOCK_SHIFT;
		u32 block_offset = rom_offset & BLOCK_MASK;
		ensure_block(block);
		if (!ROMBlocks[block])
			return NULL;
		return ROMBlocks[block] + block_offset;
	}
	return ROMCACHE_LO + rom_offset;
}

static void ROMCache_load_block(char* dst, u32 rom_offset){
	u32 want;

	if (!dst || !ROMFile)
		return;
	want = (rom_offset + BLOCK_SIZE > ROMSize) ? (ROMSize-rom_offset) : BLOCK_SIZE;
	romFile_seekFile(ROMFile, rom_offset, FILE_BROWSER_SEEK_SET);
	{
		u32 bytes_read = (u32)romFile_readFile(ROMFile, dst, want);
		byte_swap(dst, bytes_read);
	}
}

static void ensure_block(u32 block){
	if(ROMBlocks[block])
		return;
	{
		int i, max_i = -1, max_lru = -1;
		for(i=0; i<NUM_BLOCKS; ++i) {
			if(ROMBlocks[i] && ROMBlocksLRU[i] > max_lru) {
				max_i = i;
				max_lru = ROMBlocksLRU[i];
			}
		}
		if (max_i < 0 || !ROMBlocks[max_i])
			return;
		ROMBlocks[block] = ROMBlocks[max_i];
		ROMCache_load_block(ROMBlocks[block], block << BLOCK_SHIFT);
		ROMBlocks[max_i] = 0;
	}
}

static u32 clip_rom_len(u32 offset, u32 length)
{
	if (offset >= ROMSize)
		return 0;
	if (offset + length > ROMSize)
		return ROMSize - offset;
	return length;
}

void ROMCache_read(u8* dest, u32 offset, u32 length){
	if (!dest || !ROMCACHE_LO)
		return;
	length = clip_rom_len(offset, length);
	if (!length)
		return;
	if(ROMTooBig){
		u32 block = offset>>BLOCK_SHIFT;
		u32 length2 = length;
		u32 offset2 = offset&BLOCK_MASK;

		while(length2){
			ensure_block(block);
			if(length2 > BLOCK_SIZE - offset2)
				length = BLOCK_SIZE - offset2;
			else
				length = length2;
			{
				int i;
				for(i=0; i<NUM_BLOCKS; ++i)
					++ROMBlocksLRU[i];
			}
			ROMBlocksLRU[block] = 0;
			if (!ROMBlocks[block])
				return;
			memcpy(dest, ROMBlocks[block] + offset2, length);
			++block; length2 -= length; offset2 = 0; dest += length; offset += length;
		}
	} else {
		memcpy(dest, ROMCACHE_LO + offset, length);
	}
}

void ROMCache_write(u8* src, u32 offset, u32 length){
	if (!src || !ROMCACHE_LO)
		return;
	length = clip_rom_len(offset, length);
	if (!length)
		return;
	if(ROMTooBig){
		u32 block = offset>>BLOCK_SHIFT;
		u32 length2 = length;
		u32 offset2 = offset&BLOCK_MASK;
		while(length2){
			ensure_block(block);
			if(length2 > BLOCK_SIZE - offset2)
				length = BLOCK_SIZE - offset2;
			else
				length = length2;
			{
				int i;
				for(i=0; i<NUM_BLOCKS; ++i)
					++ROMBlocksLRU[i];
			}
			ROMBlocksLRU[block] = 0;
			memcpy(ROMBlocks[block] + offset2, src, length);
			++block; length2 -= length; offset2 = 0; src += length; offset += length;
		}
	} else {
		memcpy(ROMCACHE_LO + offset, src, length);
	}
}

int ROMCache_load(fileBrowser_file* f){
	u32 offset = 0;
	int bytes_read;
	u32 sizeToLoad;

	(void)f;
	if (!ROMCACHE_LO)
		return -1;
	memset(ROMBlocks, 0, sizeof(ROMBlocks));
	memset(ROMBlocksLRU, 0, sizeof(ROMBlocksLRU));

	romFile_seekFile(ROMFile, 0, FILE_BROWSER_SEEK_SET);
	sizeToLoad = MIN(ROMCACHE_BYTES, ROMSize);
	while(offset < sizeToLoad){
		u32 chunk = (offset + BLOCK_SIZE > ROMSize) ? (ROMSize-offset) : BLOCK_SIZE;
		bytes_read = romFile_readFile(ROMFile, ROMCACHE_LO + offset, chunk);
		if(bytes_read <= 0)
			return -1;
		if(!readBefore) {
			unsigned char *p = (unsigned char *)ROMCACHE_LO;
			unsigned int magic = ((unsigned int)p[0] << 24) |
				((unsigned int)p[1] << 16) |
				((unsigned int)p[2] << 8) |
				(unsigned int)p[3];
			if(init_byte_swap(magic) == BYTE_SWAP_BAD)
				return -2;
			readBefore = 1;
		}
		byte_swap(ROMCACHE_LO + offset, (unsigned int)bytes_read);
		offset += (u32)bytes_read;
	}

	if(ROMTooBig){
		int i;
		for(i=0; i<(int)(ROMCACHE_BYTES/BLOCK_SIZE); ++i)
			ROMBlocks[i] = ROMCACHE_LO + i*BLOCK_SIZE;
		for(; i<(int)(ROMSize/BLOCK_SIZE); ++i)
			ROMBlocks[i] = 0;
		for(i=0; i<(int)(ROMSize/BLOCK_SIZE); ++i)
			ROMBlocksLRU[i] = i;
	}
	return 0;
}
