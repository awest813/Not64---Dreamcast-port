/**
 * Wii64 - TLB-Cache-hash.c (Deprecated)
 * Copyright (C) 2007, 2008, 2009 Mike Slegeir
 * Copyright (C) 2007, 2008, 2009 emu_kidid
 * 
 * This is how the TLB LUT should be accessed, using a hash map
 *
 * Wii64 homepage: http://www.emulatemii.com
 * email address: tehpola@gmail.com
 *                emukidid@gmail.com
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

/* ----------------------------------------------------
   The table is keyed on the LOW bits of the page number. The original
   version shifted the page RIGHT by (20 - log2(NUM_SLOTS) + 1), so the
   bucket came from the TOP bits and 16,384 *consecutive* pages shared one
   slot -- hence the old FIXME measuring lists up to ~16,000 nodes. Pages
   inside a mapping are consecutive, so that is the worst possible key.
   ----------------------------------------------------
   MEMORY USAGE:
     STATIC:
     	TLB LUT r: NUM_SLOTS * 4 (currently 4 KiB)
     	TLB LUT w: NUM_SLOTS * 4 (currently 4 KiB)
     HEAP:
     	TLB hash nodes: one per *mapped* page, recycled through a free list.
     	Storing 0 unlinks the node instead of leaving a tombstone behind,
     	so the heap tracks the live mapping rather than accumulating one
     	node per page ever touched. A node is 12 bytes on SH4, and the page
     	number is 20 bits, so that worst case was 12 MiB per table and 24
     	MiB across both -- against 16 MiB of Dreamcast main RAM.
 */

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "TLB-Cache.h"
#include "../gui/DEBUG.h"
#include <stdio.h>
#ifndef __DREAMCAST__
#include <zlib.h>
#endif

#ifdef USE_TLB_CACHE

static TLB_hash_node* TLB_LUT_r[TLB_NUM_SLOTS];
static TLB_hash_node* TLB_LUT_w[TLB_NUM_SLOTS];

/* Nodes unlinked by an invalidation are kept here and handed back out by the
 * next insert, so a game that re-maps the same pages every frame stops calling
 * malloc() after it has touched its working set once. */
static TLB_hash_node* TLB_free_list;

void TLBCache_init(void){
  TLBCache_deinit();
}

static void TLB_free_chain(TLB_hash_node* node){
	TLB_hash_node* next;
	for(; node != NULL; node = next){
		next = node->next;
		free(node);
	}
}

void TLBCache_deinit(void){
	int i;
	for(i=0; i<TLB_NUM_SLOTS; ++i){
		TLB_free_chain(TLB_LUT_r[i]);
		TLB_LUT_r[i] = NULL;
		TLB_free_chain(TLB_LUT_w[i]);
		TLB_LUT_w[i] = NULL;
	}
	TLB_free_chain(TLB_free_list);
	TLB_free_list = NULL;
}

static unsigned int inline TLB_hash(unsigned int page){
	/* Pages within one mapping are consecutive, so the low bits hold the
	 * entropy. Fold in bit 10 and up as well, so a mapping laid out on a
	 * TLB_NUM_SLOTS stride still spreads across the table. */
	return (page ^ (page >> 10)) & (TLB_NUM_SLOTS - 1);
}

static TLB_hash_node* TLB_node_alloc(void){
	TLB_hash_node* node = TLB_free_list;
	if(node != NULL){
		TLB_free_list = node->next;
		return node;
	}
	return (TLB_hash_node*)malloc( sizeof(TLB_hash_node) );
}

static void inline TLB_set(TLB_hash_node** lut, unsigned int page, unsigned int val){
	unsigned int slot = TLB_hash(page);
	TLB_hash_node** link = &lut[slot];
	TLB_hash_node* node;

	for(node = *link; node != NULL; link = &node->next, node = node->next){
		if(node->page != page) continue;
		if(val){
			/* Re-mapping a live page: no allocation, no list surgery. */
			node->value = val;
		} else {
			/* 0 means "unmapped", which is what a lookup miss already
			 * reports, so drop the node rather than parking a 0 in the
			 * list where it would be walked for the rest of the run. */
			*link = node->next;
			node->next = TLB_free_list;
			TLB_free_list = node;
		}
		return;
	}

	if(!val) return;	/* already absent; a miss reads back as 0 */

	node = TLB_node_alloc();
	if(node == NULL) return;	/* leave the page unmapped; the caller
					 * takes a TLB refill rather than a crash */
	node->page  = page;
	node->value = val;
	node->next  = lut[slot];
	lut[slot] = node;
}

unsigned int inline TLBCache_get_r(unsigned int page){
	TLB_hash_node* node = TLB_LUT_r[ TLB_hash(page) ];

	for(; node != NULL; node = node->next)
		if(node->page == page) return node->value;

	return 0;
}

unsigned int inline TLBCache_get_w(unsigned int page){
	TLB_hash_node* node = TLB_LUT_w[ TLB_hash(page) ];

	for(; node != NULL; node = node->next)
		if(node->page == page) return node->value;

	return 0;
}

void inline TLBCache_set_r(unsigned int page, unsigned int val){
	TLB_set(TLB_LUT_r, page, val);
}

void inline TLBCache_set_w(unsigned int page, unsigned int val){
	TLB_set(TLB_LUT_w, page, val);
}

/* Longest bucket across both tables. The point of the hash is that this stays
 * near 1 for a real mapping; smoke_tlbcache() asserts it, and TLBCache_dump()
 * is the way to see where the nodes actually landed. */
unsigned int TLBCache_longest_chain(void){
	int i;
	unsigned int longest = 0;

	for(i=0; i<TLB_NUM_SLOTS; ++i){
		unsigned int n;
		TLB_hash_node* node;

		for(n = 0, node = TLB_LUT_r[i]; node != NULL; node = node->next) ++n;
		if(n > longest) longest = n;
		for(n = 0, node = TLB_LUT_w[i]; node != NULL; node = node->next) ++n;
		if(n > longest) longest = n;
	}
	return longest;
}

char* TLBCache_dump(){
	int i;
	DEBUG_print("\n\nTLB Cache r dump:\n", DBG_USBGECKO);
	for(i=0; i<TLB_NUM_SLOTS; ++i){
		sprintf(txtbuffer, "%d\t", i);
		DEBUG_print(txtbuffer, DBG_USBGECKO);
		TLB_hash_node* node = TLB_LUT_r[i];
		for(; node != NULL; node = node->next){
			sprintf(txtbuffer, "%05x,%05x -> ", node->page, node->value);
			DEBUG_print(txtbuffer, DBG_USBGECKO);
		}
		DEBUG_print("\n", DBG_USBGECKO);
	}
	DEBUG_print("\n\nTLB Cache w dump:\n", DBG_USBGECKO);
	for(i=0; i<TLB_NUM_SLOTS; ++i){
		sprintf(txtbuffer, "%d\t", i);
		DEBUG_print(txtbuffer, DBG_USBGECKO);
		TLB_hash_node* node = TLB_LUT_w[i];
		for(; node != NULL; node = node->next){
			sprintf(txtbuffer, "%05x,%05x -> ", node->page, node->value);
			DEBUG_print(txtbuffer, DBG_USBGECKO);
		}
		DEBUG_print("\n", DBG_USBGECKO);
	}
	return "TLB Cache dumped to USB Gecko";
}

#ifndef __DREAMCAST__
void TLBCache_dump_r(gzFile *f)
{
	int i = 0,total=0;
		for(i=0; i<TLB_NUM_SLOTS; ++i){
		TLB_hash_node* node = TLB_LUT_r[i];
		for(; node != NULL; node = node->next){
			total++;	
		}
	}
	gzwrite(f, &total, sizeof(int));
	for(i=0; i<TLB_NUM_SLOTS; ++i){
		TLB_hash_node* node = TLB_LUT_r[i];
		for(; node != NULL; node = node->next){
			gzwrite(f, &node->page, 4);
			gzwrite(f, &node->value, 4);		
		}
	}
}

void TLBCache_dump_w(gzFile *f)
{
	int i = 0,total=0;
	//traverse to get total
	for(i=0; i<TLB_NUM_SLOTS; ++i){
		TLB_hash_node* node = TLB_LUT_w[i];
		for(; node != NULL; node = node->next){
			total++;
		}
	}
	gzwrite(f, &total, sizeof(int));
	for(i=0; i<TLB_NUM_SLOTS; ++i){
		TLB_hash_node* node = TLB_LUT_w[i];
		for(; node != NULL; node = node->next){
			gzwrite(f, &node->page, 4);
			gzwrite(f, &node->value, 4);		
		}
	}
}
#else
void TLBCache_dump_r(gzFile *f) { (void)f; }
void TLBCache_dump_w(gzFile *f) { (void)f; }
#endif /* !__DREAMCAST__ */
#endif
