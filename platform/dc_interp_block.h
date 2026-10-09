#ifndef DC_INTERP_BLOCK_H
#define DC_INTERP_BLOCK_H

/* Optional straight-line SH4 executor. It never handles guest branches,
 * devices, exceptions or FPU instructions. Returns the executed prefix length;
 * a zero/partial prefix resumes the ordinary fresh-fetch interpreter. */
unsigned dc_interp_block_execute(unsigned pc, unsigned limit);
void dc_interp_block_reset(void);
void dc_interp_block_report(void);
#ifdef DC_INTERP_BLOCK_ARENA
unsigned dc_interp_block_arena_wraps(void);
unsigned dc_interp_block_arena_used(void);
unsigned dc_interp_block_arena_capacity(void);
#endif

#ifdef DC_INTERP_BLOCK_DISPATCH
static inline __attribute__((always_inline)) int dc_interp_block_candidate(unsigned raw) {
    // The executor's supported() remains authoritative. Reject unsupported
    // starts at the existing fresh fetch, before paying its external ABI call.
    unsigned primary=raw>>26;
    if(!primary) {
        unsigned function=raw&63;
        if(function<8)return (0xddu>>function)&1;
        return function>=32 && function<40 && ((0xfau>>(function-32))&1);
    }
    if(primary<32)return (0xf200u>>primary)&1;
    return primary<44 && ((0xb3bu>>(primary-32))&1);
}
#endif

#ifdef DC_INTERP_BLOCK_EPOCH
/* Proofs are trusted only in the call-closed pure interpreter. External users
 * keep fresh-word validation. See STADIUM2_8FPS_PLAN.md for writer ownership. */
extern unsigned dc_interp_block_page_epoch[1024];
extern unsigned dc_interp_block_epoch_valid;
extern unsigned dc_interp_block_observer_epoch;
static inline void dc_interp_block_page_written(unsigned page) {
    // Zero means this page has never contained cached code in this lifetime.
    // Wrapped generations also become zero, with the shortcut disabled.
    unsigned value=dc_interp_block_page_epoch[page];
    if(value && !(dc_interp_block_page_epoch[page]=value+1))dc_interp_block_epoch_valid=0;
}
void dc_interp_block_enter(void);
void dc_interp_block_leave(void);
void dc_interp_block_observer(void);
void dc_interp_block_written(unsigned address,unsigned size);
#endif

#ifdef DC_INTERP_BLOCK_HOT_ENTRY
#include <stdint.h>
enum { DC_INTERP_HOT_SLOTS=DC_INTERP_BLOCK_KB*2 };
typedef struct {
    unsigned pc,count,observerEpoch,pageEpoch[2];
    uint32_t *raw;
    unsigned *age;
    unsigned short *code;
} DcInterpHotEntry;
extern DcInterpHotEntry dc_interp_hot_entries[DC_INTERP_HOT_SLOTS];
extern unsigned dc_interp_hot_hits,dc_interp_hot_executed,dc_interp_hot_exits;
extern unsigned dc_interp_hot_clock_age,dc_interp_hot_epoch_hits,dc_interp_hot_epoch_live;
static inline unsigned dc_interp_hot_index(unsigned pc) {
    return ((pc>>2)^(pc>>11))&(DC_INTERP_HOT_SLOTS-1);
}
static inline __attribute__((always_inline)) unsigned
dc_interp_block_hot_execute(unsigned pc,unsigned limit,unsigned *base,unsigned long *opcode) {
#ifdef DC_HOST_STUB
    return ~0u; // ARM cannot execute the generated SH4 function.
#else
    if(!dc_interp_hot_epoch_live || !dc_interp_block_epoch_valid ||
       dc_interp_hot_clock_age==~0u)return ~0u;
    DcInterpHotEntry *entry=&dc_interp_hot_entries[dc_interp_hot_index(pc)];
    unsigned count=entry->count;
    if(entry->pc!=pc || count<2 || count>limit ||
       entry->observerEpoch!=dc_interp_block_observer_epoch)return ~0u;
    unsigned offset=pc&0x3ffffc;
    if(entry->pageEpoch[0]!=dc_interp_block_page_epoch[offset>>12] ||
       entry->pageEpoch[1]!=dc_interp_block_page_epoch[(offset+(count-1)*4)>>12])return ~0u;
    ++dc_interp_hot_hits;++dc_interp_hot_epoch_hits;
    *entry->age=++dc_interp_hot_clock_age;
    typedef unsigned (*Native)(unsigned*,unsigned*,unsigned*,unsigned*);
    unsigned done=((Native)entry->code)(base,base+16,base+32,base+48);
    dc_interp_hot_executed+=done;if(done<count)++dc_interp_hot_exits;
    if(done)*opcode=entry->raw[done-1];
    return done; // Zero is an attempted guarded side exit; ~0 is a cache miss.
#endif
}
#endif

#endif
