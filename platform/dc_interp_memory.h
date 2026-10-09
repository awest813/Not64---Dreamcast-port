#ifndef DC_INTERP_MEMORY_H
#define DC_INTERP_MEMORY_H

/* Optional pure-interpreter fast path. Qualify the installed handler rather
 * than just the address: framebuffer callbacks and all translated/device
 * handlers retain their original dispatch and bus globals. */
#define DC_RAM_OFFSET (address & (sizeof(rdram)-1))
#ifdef DC_INTERP_BLOCK_EPOCH
#include "dc_interp_block.h"
#define DC_RAM_OBSERVER() dc_interp_block_observer()
static inline void dc_ram_written(unsigned size) {
    unsigned first=(address&(sizeof(rdram)-1))>>12;
    unsigned last=((address+size-1)&(sizeof(rdram)-1))>>12;
    dc_interp_block_page_written(first);
    if(last!=first)dc_interp_block_page_written(last);
}
#define DC_RAM_WRITTEN(size) dc_ram_written(size)
#else
#define DC_RAM_OBSERVER() do {} while(0)
#define DC_RAM_WRITTEN(size) do {} while(0)
#endif
#ifdef DC_INTERP_PC_LAZY
#include "../r4300/r4300.h"
// Memory handlers observe the fetched PC, even though their caller has already
// advanced interp_addr. Width-qualified ordinary RAM does not call observers.
#define DC_RAM_OBSERVER_PC() do { if(PC)PC->addr=interp_addr-4; } while(0)
#else
#define DC_RAM_OBSERVER_PC() do {} while(0)
#endif
#define DC_RAM_DISPATCH(name, slot, ordinary, operation) \
static inline void name(void) { \
    void (*handler)(void) = rwmem[n64_addr(address)>>16][slot]; \
    if (handler == ordinary) { operation; } else { DC_RAM_OBSERVER_PC(); DC_RAM_OBSERVER(); handler(); DC_RAM_OBSERVER(); } \
}
DC_RAM_DISPATCH(dc_read_word, MEM_READ_WORD, read_rdram,
    word=*(unsigned long *)(rdramb+DC_RAM_OFFSET))
DC_RAM_DISPATCH(dc_read_byte, MEM_READ_BYTE, read_rdramb,
    byte=*(rdramb+(DC_RAM_OFFSET^S8)))
DC_RAM_DISPATCH(dc_read_half, MEM_READ_HALF, read_rdramh,
    hword=*(unsigned short *)(rdramb+(DC_RAM_OFFSET^S16)))
DC_RAM_DISPATCH(dc_read_long, MEM_READ_LONG, read_rdramd,
    dword=((unsigned long long)*(unsigned long *)(rdramb+DC_RAM_OFFSET)<<32) |
          *(unsigned long *)(rdramb+DC_RAM_OFFSET+4))
DC_RAM_DISPATCH(dc_write_word, MEM_WRITE_WORD, write_rdram,
    *(unsigned long *)(rdramb+DC_RAM_OFFSET)=word; DC_RAM_WRITTEN(4))
DC_RAM_DISPATCH(dc_write_byte, MEM_WRITE_BYTE, write_rdramb,
    *(rdramb+(DC_RAM_OFFSET^S8))=byte; DC_RAM_WRITTEN(1))
DC_RAM_DISPATCH(dc_write_half, MEM_WRITE_HALF, write_rdramh,
    *(unsigned short *)(rdramb+(DC_RAM_OFFSET^S16))=hword; DC_RAM_WRITTEN(2))
DC_RAM_DISPATCH(dc_write_long, MEM_WRITE_LONG, write_rdramd,
    *(unsigned long *)(rdramb+DC_RAM_OFFSET)=dword>>32;
    *(unsigned long *)(rdramb+DC_RAM_OFFSET+4)=(unsigned long)dword; DC_RAM_WRITTEN(8))
#undef DC_RAM_DISPATCH
#undef DC_RAM_OBSERVER_PC
#undef DC_RAM_OFFSET
#undef DC_RAM_OBSERVER
#undef DC_RAM_WRITTEN
#undef read_word_in_memory
#undef read_byte_in_memory
#undef read_hword_in_memory
#undef read_dword_in_memory
#undef write_word_in_memory
#undef write_byte_in_memory
#undef write_hword_in_memory
#undef write_dword_in_memory
#define read_word_in_memory() dc_read_word()
#define read_byte_in_memory() dc_read_byte()
#define read_hword_in_memory() dc_read_half()
#define read_dword_in_memory() dc_read_long()
#define write_word_in_memory() dc_write_word()
#define write_byte_in_memory() dc_write_byte()
#define write_hword_in_memory() dc_write_half()
#define write_dword_in_memory() dc_write_long()
#endif
