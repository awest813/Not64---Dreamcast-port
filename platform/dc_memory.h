/**
 * Dreamcast RAM budget for Not64.
 *
 * Dreamcast main RAM is 16 MiB. Wii MEM2.h reserves ~50 MiB of fixed
 * regions before optional ROM-cache expansion. Those sizes must not be
 * copied here.
 *
 * This header is the paper map for Phase 1–2. Allocations are not yet
 * wired into gc_memory/; do not include this from MEM2.h.
 */

#ifndef PLATFORM_DC_MEMORY_H
#define PLATFORM_DC_MEMORY_H

#define DC_MB (1024 * 1024)
#define DC_KB 1024

/* Console */
#define DC_MAIN_RAM_SIZE        (16 * DC_MB)

/* Reserved for KallistiOS, .text/.data, stack, PVR scratch */
#define DC_OS_AND_CODE_RESERVE  (3 * DC_MB)

/* Emulated N64 RDRAM (no Expansion Pak in Phase 1–2) */
#define DC_N64_RDRAM_SIZE       (4 * DC_MB)

/* Sliding ROM window instead of Wii's 16–192 MiB ROM cache */
#define DC_ROM_STREAM_SIZE      (1 * DC_MB)

/* Hash TLB only; no 8 MiB LUT */
#define DC_TLB_MISC_SIZE        (512 * DC_KB)

/* Graphics cache: 0 until a renderer exists */
#define DC_TEXCACHE_SIZE        0

/* Audio ring in gc_audio/audio-dc.c */
#define DC_AUDIO_RING_SIZE      (64 * DC_KB)

#define DC_EMU_FIXED_SIZE \
	(DC_N64_RDRAM_SIZE + DC_ROM_STREAM_SIZE + DC_TLB_MISC_SIZE + \
	 DC_TEXCACHE_SIZE + DC_AUDIO_RING_SIZE)

#define DC_HEAP_REMAINDER \
	(DC_MAIN_RAM_SIZE - DC_OS_AND_CODE_RESERVE - DC_EMU_FIXED_SIZE)

#if DC_EMU_FIXED_SIZE + DC_OS_AND_CODE_RESERVE > DC_MAIN_RAM_SIZE
#error Dreamcast memory budget exceeds 16 MiB
#endif

#endif
