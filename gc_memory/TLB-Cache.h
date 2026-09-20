/**
 * Wii64 - TLB-Cache.h
 * Copyright (C) 2007, 2008, 2009 Mike Slegeir
 * 
 * This is how the TLB LUT should be accessed, this way it won't waste RAM
 *
 * Wii64 homepage: http://www.emulatemii.com
 * email address: tehpola@gmail.com
 *
 *
 * This program is free software; you can redistribute it and/
 * or modify it under the terms of the GNU General Public Li-
 * cence as published by the Free Software Foundation; either
 * version 2 of the Licence, or any later version.
 *
 * This program is distributed in the hope that it will be use-
 * ful, but WITHOUT ANY WARRANTY; without even the implied war-
 * ranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public Licence for more details.
 *
**/


#ifndef TLB_CACHE_H
#define TLB_CACHE_H

#ifdef USE_TLB_CACHE

#ifdef __DREAMCAST__
/* Savestate dump helpers are unused on the DC bring-up. */
typedef void *gzFile;
#else
#include <zlib.h>
#endif

// Num Slots must be a power of 2!
// 1024 slots is 4 KiB per table (r and w), and keeps a normally mapped game
// at roughly one node per bucket. 64 slots only worked out to 256 bytes, but
// the lookup that saved is on the path of every TLB-mapped load and store.
#define TLB_NUM_SLOTS 1024

typedef struct node {
	unsigned int value;
	unsigned int page;
	struct node* next;
} TLB_hash_node;

void TLBCache_init(void);
void TLBCache_deinit(void);

unsigned int inline TLBCache_get_r(unsigned int page);
unsigned int inline TLBCache_get_w(unsigned int page);

void inline TLBCache_set_r(unsigned int page, unsigned int val);
void inline TLBCache_set_w(unsigned int page, unsigned int val);

// Longest bucket across both tables; 0 when the cache is empty.
unsigned int TLBCache_longest_chain(void);

// for savestates
void TLBCache_dump_r(gzFile *f);
void TLBCache_dump_w(gzFile *f);

void ARAM_ReadTLBBlock(unsigned int addr, int type);
void ARAM_WriteTLBBlock(unsigned int addr, int type);

#endif

#endif

