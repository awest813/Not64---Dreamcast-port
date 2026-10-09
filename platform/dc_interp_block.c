/* Bounded experimental SH4 integer blocks. Generated functions use only the
 * caller-saved r0-r7, leave PR/FPSCR alone and return a completed prefix in r0.
 * Optional word-RAM proof reuse saves/restores r8 on every generated exit.
 * Native encodings follow Renesas SH-4 Software Manual REJ09B0318-0600.
 * Cached words are checked before execution, or an opt-in scoped page proof
 * establishes that no instruction writer has intervened. Ordinary blocks end
 * at stores; optional longer blocks guard every write against future code.
 * RAM handlers are checked at each access; devices/hooks exit before effects.
 */
#include "dc_interp_block.h"
#include "dc_memory.h"
#include "../gc_memory/memory.h"
#include "../r4300/r4300.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#ifndef DC_HOST_STUB
#include <arch/cache.h>
#endif
#ifdef DC_INTERP_BLOCK_PROFILE
#ifdef DC_HOST_STUB
#include <time.h>
static uint64_t block_profile_now(void) {
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);
    return (uint64_t)t.tv_sec*1000000000u+t.tv_nsec;
}
#else
#include <dc/perfctr.h>
#include <arch/timer.h>
static int block_profile_counter;
static uint64_t block_profile_now(void) {
    // Read the existing KOS clock; never configure a timer/counter here.
    return block_profile_counter?perf_cntr_count(PRFC0)*5u:timer_ns_gettime64();
}
#endif
enum { PROFILE_QUALIFY,PROFILE_LOOKUP,PROFILE_VALIDATE,PROFILE_COMPILE,PROFILE_NATIVE,PROFILE_STAGES };
static uint64_t profile_ns[PROFILE_STAGES],profile_last,profile_clock_ns;
static unsigned profile_samples[PROFILE_STAGES],profile_attempts,profile_hist[17];
static unsigned profile_rng,profile_phase;
static int profile_active;
static void block_profile_phase(unsigned next) {
    if(!profile_active)return;
    uint64_t now=block_profile_now();
    profile_ns[profile_phase]+=now-profile_last;
    ++profile_samples[profile_phase];profile_last=now;profile_phase=next;
}
#define BLOCK_PROFILE_PHASE(next) block_profile_phase(next)
#else
#define BLOCK_PROFILE_PHASE(next) do {} while(0)
#endif

extern unsigned long op;
#ifdef DC_INTERP_BLOCK_HOT_ENTRY
DcInterpHotEntry dc_interp_hot_entries[DC_INTERP_HOT_SLOTS] __attribute__((aligned(32)));
#define hits dc_interp_hot_hits
#define executed dc_interp_hot_executed
#define exits dc_interp_hot_exits
#define clock_age dc_interp_hot_clock_age
#define epoch_hits dc_interp_hot_epoch_hits
#define epoch_live dc_interp_hot_epoch_live
#define BLOCK_HOT_BYTES sizeof(dc_interp_hot_entries)
#else
#define BLOCK_HOT_BYTES 0
#endif

#ifdef DC_INTERP_BLOCK_ARENA
enum { SLOTS=DC_INTERP_BLOCK_KB*2, CODE_WORDS=512 };
#elif defined(DC_INTERP_BLOCK_STORES)
enum { SLOTS=DC_INTERP_BLOCK_KB, CODE_WORDS=384 };
#else
enum { SLOTS=DC_INTERP_BLOCK_KB*2, CODE_WORDS=192 };
#endif
enum { WAYS=4, OPS=16, LITERALS=64, FIXUPS=160 };
typedef struct {
#ifdef DC_INTERP_BLOCK_ARENA
    uint16_t *code;
#else
    uint16_t code[CODE_WORDS];
#endif
    uint32_t pc, raw[OPS];
    unsigned count,age;
#ifdef DC_INTERP_BLOCK_PROFILE
    unsigned usedWords;
#endif
#ifdef DC_INTERP_BLOCK_EPOCH
    unsigned observerEpoch,pageEpoch[2];
#endif
} Block;
static Block blocks[SLOTS] __attribute__((aligned(32)));
#ifdef DC_INTERP_BLOCK_EPOCH
unsigned dc_interp_block_page_epoch[1024];
unsigned dc_interp_block_epoch_valid;
unsigned dc_interp_block_observer_epoch;
#ifdef DC_INTERP_BLOCK_HOT_ENTRY
unsigned epoch_live,epoch_hits;
#else
static unsigned epoch_live,epoch_hits;
#endif
_Static_assert(sizeof(blocks)+sizeof(dc_interp_block_page_epoch)+32<=DC_INTERP_BLOCK_SIZE,
               "integer block cache and page proofs exceed budget");
void dc_interp_block_observer(void) {
    if(!++dc_interp_block_observer_epoch)dc_interp_block_epoch_valid=0;
}
void dc_interp_block_enter(void) {
    dc_interp_block_observer();epoch_live=1;
}
void dc_interp_block_leave(void) {epoch_live=0;}
void dc_interp_block_written(unsigned address,unsigned size) {
    unsigned first=(address&(sizeof(rdram)-1))>>12;
    unsigned last=((address+size-1)&(sizeof(rdram)-1))>>12;
    dc_interp_block_page_written(first);
    if(last!=first)dc_interp_block_page_written(last);
}
#else
_Static_assert(sizeof(blocks)<=DC_INTERP_BLOCK_SIZE,"integer block cache exceeds budget");
#endif
#ifdef DC_INTERP_BLOCK_HOT_ENTRY
unsigned hits,executed,exits,clock_age;
static unsigned compiles;
#else
static unsigned hits,compiles,executed,exits,clock_age;
#endif
#ifdef DC_INTERP_BLOCK_ARENA
static uint16_t compile_code[CODE_WORDS] __attribute__((aligned(32)));
#ifdef DC_INTERP_BLOCK_EPOCH
#define BLOCK_PROOF_BYTES sizeof(dc_interp_block_page_epoch)
#else
#define BLOCK_PROOF_BYTES 0
#endif
// Code and metadata share the existing budget. Compilation is synchronous;
// arena wrap clears every cache reference before reusing any executable byte.
static uint16_t code_arena[(DC_INTERP_BLOCK_SIZE-sizeof(blocks)-sizeof(compile_code)-BLOCK_PROOF_BYTES-BLOCK_HOT_BYTES-128)/2]
    __attribute__((aligned(32)));
static unsigned arena_used,arena_wraps;
_Static_assert(sizeof(blocks)+sizeof(compile_code)+sizeof(code_arena)+BLOCK_PROOF_BYTES+BLOCK_HOT_BYTES+128<=DC_INTERP_BLOCK_SIZE,
               "native arena exceeds the block-cache budget");
unsigned dc_interp_block_arena_wraps(void){return arena_wraps;}
unsigned dc_interp_block_arena_used(void){return arena_used;}
unsigned dc_interp_block_arena_capacity(void){return sizeof(code_arena);}
#endif
typedef struct {
    Block *block;
    unsigned words, literals, fixups;
#ifdef DC_INTERP_BLOCK_RAM_PROOF
    int ramProof;
#endif
#ifdef DC_INTERP_BLOCK_REUSE
    unsigned cachedGuest;
    int cachedLow;
#endif
    uint32_t values[LITERALS];
    struct { unsigned word,literal; } fix[FIXUPS];
} Builder;

static void emit(Builder *b,unsigned instruction) {
    b->block->code[b->words++]=(uint16_t)instruction;
}
static void constant(Builder *b,unsigned target,uint32_t value) {
#ifdef DC_INTERP_BLOCK_REUSE
    if(!target)b->cachedLow=0;
#endif
    if((int32_t)value>=-128 && (int32_t)value<=127) {
        emit(b,0xe000|(target<<8)|(value&255));return;
    }
    unsigned i;
    for(i=0;i<b->literals && b->values[i]!=value;++i){}
    if(i==b->literals)b->values[b->literals++]=value;
    b->fix[b->fixups].word=b->words;b->fix[b->fixups++].literal=i;
    emit(b,0xd000|(target<<8));
}
static void move(Builder *b,unsigned from,unsigned to) {emit(b,0x6003|(to<<8)|(from<<4));}
static void load_reg(Builder *b,unsigned guest,unsigned host,unsigned high) {
#ifdef DC_INTERP_BLOCK_REUSE
    if(!host && !high && b->cachedLow && b->cachedGuest==guest)return;
#endif
    emit(b,0x5000|(host<<8)|((4+guest/8)<<4)|((guest%8)*2+high));
#ifdef DC_INTERP_BLOCK_REUSE
    if(!host){b->cachedLow=!high;b->cachedGuest=guest;}
#endif
}
static void store_reg(Builder *b,unsigned guest,unsigned host,unsigned high) {
    emit(b,0x1000|((4+guest/8)<<8)|(host<<4)|((guest%8)*2+high));
#ifdef DC_INTERP_BLOCK_REUSE
    if(!host && !high){b->cachedLow=1;b->cachedGuest=guest;}
#endif
}
static void sign_store(Builder *b,unsigned guest) {
    move(b,0,1);constant(b,2,(uint32_t)-31);emit(b,0x412c); // SHAD r2,r1
    store_reg(b,guest,0,0);store_reg(b,guest,1,1);
}
static void return_count(Builder *b,unsigned count) {
#ifdef DC_INTERP_BLOCK_RAM_PROOF
    if(b->ramProof)emit(b,0x68f6); // MOV.L @r15+,r8
#endif
    constant(b,0,count);emit(b,0x000b);emit(b,0x0009); // RTS; NOP delay slot
}
static unsigned exit_displacement(const Builder *b) {
#ifdef DC_INTERP_BLOCK_RAM_PROOF
    if(b->ramProof)return 3; // Four-word exit, including saved r8.
#endif
    return 2;
}
static void guard(Builder *b,unsigned count,int true_success) {
    emit(b,(true_success?0x8900:0x8b00)|exit_displacement(b));
    return_count(b,count);
}
static void bus_store(Builder *b,const void *location,unsigned size) {
    constant(b,3,(uintptr_t)location);
    emit(b,0x2300|size); // MOV.B/W/L r0,@r3
}
static inline __attribute__((always_inline)) int supported(unsigned instruction) {
    unsigned primary=instruction>>26;
    if(!primary) switch(instruction&63) {
    case 0:case 2:case 3:case 4:case 6:case 7:
    case 33:case 35:case 36:case 37:case 38:case 39:return 1;
    default:return 0;
    }
    switch(primary) {
    case 9:case 12:case 13:case 14:case 15:
    case 32:case 33:case 35:case 36:case 37:
    case 40:case 41:case 43:return 1;
    default:return 0;
    }
}
static int memory_op(unsigned primary) {return primary>=32;}
static int store_op(unsigned primary) {return primary>=40;}

static void integer(Builder *b,uint32_t raw) {
    unsigned primary=raw>>26,rs=(raw>>21)&31,rt=(raw>>16)&31,rd=(raw>>11)&31;
    unsigned function=raw&63,shift=(raw>>6)&31;
    if(!primary) {
        if(function<=7) {
            load_reg(b,rt,0,0);
            if(function<4)constant(b,1,function==0?shift:(uint32_t)-(int)shift);
            else {
                load_reg(b,rs,1,0);constant(b,2,31);emit(b,0x2129); // AND r2,r1
                if(function!=4)emit(b,0x611b); // NEG r1,r1
            }
            emit(b,(function==3 || function==7?0x400c:0x400d)|0x10);
            sign_store(b,rd);return;
        }
        load_reg(b,rs,0,0);load_reg(b,rt,1,0);
        if(function==33 || function==35) {
            emit(b,function==33?0x301c:0x3018);sign_store(b,rd);return;
        }
        load_reg(b,rs,2,1);load_reg(b,rt,3,1);
        unsigned operation=function==36?0x2009:function==38?0x200a:0x200b;
        emit(b,operation|0x10);emit(b,operation|0x230);
        if(function==39){emit(b,0x6007);emit(b,0x6227);}
        store_reg(b,rd,0,0);store_reg(b,rd,2,1);return;
    }
    if(primary==15) {constant(b,0,raw<<16);sign_store(b,rt);return;}
    load_reg(b,rs,0,0);
    constant(b,1,primary==9?(uint32_t)(int32_t)(int16_t)raw:raw&65535);
    if(primary==9){emit(b,0x301c);sign_store(b,rt);return;}
    emit(b,primary==12?0x2019:primary==13?0x201b:0x201a);
    if(primary==12)constant(b,2,0);else load_reg(b,rs,2,1);
    store_reg(b,rt,0,0);store_reg(b,rt,2,1);
}
static void memory(Builder *b,uint32_t raw,unsigned prefix) {
    unsigned primary=raw>>26,rs=(raw>>21)&31,rt=(raw>>16)&31;
    unsigned size=(primary==32 || primary==36 || primary==40)?0:
                  (primary==33 || primary==37 || primary==41)?1:2;
    unsigned slot=store_op(primary)?(size==0?MEM_WRITE_BYTE:size==1?MEM_WRITE_HALF:MEM_WRITE_WORD):
                  (size==0?MEM_READ_BYTE:size==1?MEM_READ_HALF:MEM_READ_WORD);
    void (*ordinary)(void)=store_op(primary)?(size==0?write_rdramb:size==1?write_rdramh:write_rdram):
                          (size==0?read_rdramb:size==1?read_rdramh:read_rdram);
    load_reg(b,rs,0,0);constant(b,1,(uint32_t)(int32_t)(int16_t)raw);emit(b,0x301c);
#ifdef DC_INTERP_BLOCK_REUSE
    // r0 now contains the effective address, not the base register value.
    b->cachedLow=0;
#endif
    emit(b,0x8800);guard(b,prefix,0); // Address zero retains the legacy load guard.
    if(size){emit(b,0xc800|(size==1?1:3));guard(b,prefix,1);}
    move(b,0,2);emit(b,0x6229);emit(b,0x622d);emit(b,0x4208); // Page *4
#ifdef DC_INTERP_BLOCK_RAM_PROOF
    unsigned proven=0;
    if(b->ramProof && size==2) {
        move(b,2,1);emit(b,0x3810); // CMP/EQ r1,r8: same virtual page.
        proven=b->words;emit(b,0x8900); // BT to the qualified bus operation.
    }
#endif
    constant(b,3,(uintptr_t)rwmem);emit(b,0x323c);emit(b,0x6222);
#ifdef DC_INTERP_BLOCK_RAM_PROOF
    if(b->ramProof && size==2) {
        // No original handler, event or observer runs inside this prefix.
        // Prove both word handlers once; a store cannot change the host table.
        emit(b,0x5820|MEM_READ_WORD);constant(b,3,(uintptr_t)read_rdram);emit(b,0x3830);
        guard(b,prefix,1);
        emit(b,0x5820|MEM_WRITE_WORD);constant(b,3,(uintptr_t)write_rdram);emit(b,0x3830);
        guard(b,prefix,1);move(b,1,8);
        b->block->code[proven]|=b->words-(proven+2);
    } else
#endif
    {
    emit(b,0x5220|slot);constant(b,3,(uintptr_t)ordinary);emit(b,0x3230);
    guard(b,prefix,1);
    }
    bus_store(b,&address,2);
    constant(b,2,sizeof(rdram)-1);emit(b,0x2209);
    if(size!=2){constant(b,3,size==0?S8:S16);emit(b,0x223a);}
    constant(b,3,(uintptr_t)rdramb);emit(b,0x332c);
    void *value=size==0?(void*)&byte:size==1?(void*)&hword:(void*)&word;
    if(store_op(primary)) {
        move(b,3,2);load_reg(b,rt,0,0);bus_store(b,value,size);emit(b,0x2200|size);
#ifdef DC_INTERP_BLOCK_EPOCH
        // r2 keeps the actual aligned byte/half/word host address. Endian lane
        // selection stays within its physical page. No callback/PR use here.
        move(b,2,1);constant(b,0,(uintptr_t)rdramb);emit(b,0x3108);
        constant(b,0,(sizeof(rdram)-1)&~4095u);emit(b,0x2109);
        constant(b,0,(uint32_t)-10);emit(b,0x410d); // Page *4.
        constant(b,0,(uintptr_t)dc_interp_block_page_epoch);emit(b,0x310c);
        emit(b,0x6012);emit(b,0x2008);emit(b,0x8906); // Unwatched data page: skip.
        emit(b,0x7001);emit(b,0x2102); // ++physical page epoch.
        emit(b,0x2008);emit(b,0x8b02); // Nonzero: skip permanent wrap fallback.
        constant(b,1,(uintptr_t)&dc_interp_block_epoch_valid);
        constant(b,0,0);emit(b,0x2102);
#endif
#ifdef DC_INTERP_BLOCK_STORES
        // The write is complete. Continue only when it cannot overlap any
        // future guest instruction in this invocation. r2 retains the native
        // address (including the byte/half endian lane); naturally aligned
        // widths cannot straddle the aligned first future instruction.
        unsigned start=(b->block->pc&0x3ffffc)+(prefix+1)*4;
        unsigned end=(b->block->pc&0x3ffffc)+OPS*4;
        constant(b,0,(uintptr_t)rdramb+start);emit(b,0x3202); // CMP/HS r0,r2
        unsigned outside=b->words;emit(b,0x8b00); // BF to continuation
        constant(b,0,(uintptr_t)rdramb+end);emit(b,0x3202);
        emit(b,0x8900|exit_displacement(b));return_count(b,prefix+1); // >=end: continue; else exit
        b->block->code[outside]|=b->words-(outside+2);
#endif
    } else {
        emit(b,0x6030|size);bus_store(b,value,size);
        if(primary==36 || primary==37) {
            emit(b,primary==36?0x600c:0x600d);constant(b,1,0);
            store_reg(b,rt,0,0);store_reg(b,rt,1,1);
        } else sign_store(b,rt);
    }
}
static void compile(Block *block,unsigned pc,unsigned limit) {
#ifdef DC_INTERP_BLOCK_HOT_ENTRY
    DcInterpHotEntry *old=&dc_interp_hot_entries[dc_interp_hot_index(block->pc)];
    if(old->raw==block->raw)old->count=0;
#endif
    Builder b={.block=block};block->pc=pc;block->count=0;
#ifdef DC_INTERP_BLOCK_ARENA
    block->code=compile_code;
#endif
#ifdef DC_INTERP_BLOCK_PROFILE
    block->usedWords=0;
#endif
    unsigned offset=pc&0x3ffffc;
#ifdef DC_INTERP_BLOCK_RAM_PROOF
    unsigned wordAccesses=0;
    for(unsigned i=0;i<OPS && i<limit && offset+4*i<=sizeof(rdram)-4;++i) {
        uint32_t raw=rdram[offset/4+i];unsigned primary=raw>>26;
        if(!supported(raw))break;
        if(primary==35 || primary==43)++wordAccesses;
#ifndef DC_INTERP_BLOCK_STORES
        if(store_op(primary))break;
#endif
    }
    // Single word accesses retain the smaller original leaf function.
    b.ramProof=wordAccesses>=2;
    if(b.ramProof){emit(&b,0x2f86);emit(&b,0xe8ff);} // Save r8; invalid page tag.
#endif
    for(unsigned i=0;i<OPS && i<limit && offset+4*i<=sizeof(rdram)-4;++i) {
        uint32_t raw=rdram[offset/4+i];unsigned primary=raw>>26;
        if(!supported(raw))break;
        /* Reserve enough code/fixups for one worst-case memory operation,
         * all literals and the final return. A full cache is ordinary fallback. */
#ifdef DC_INTERP_BLOCK_STORES
        unsigned reserve=84,extraLiterals=10,extraFixups=16;
#else
        unsigned reserve=72,extraLiterals=8,extraFixups=12;
#endif
        unsigned finalWords=4;
#ifdef DC_INTERP_BLOCK_RAM_PROOF
        if(b.ramProof){reserve=96;extraLiterals=10;extraFixups=16;finalWords=5;}
#endif
#ifdef DC_INTERP_BLOCK_EPOCH
        reserve+=24;extraLiterals+=4;extraFixups+=6;
#endif
        if(b.words+reserve+2*(b.literals+extraLiterals)+finalWords>CODE_WORDS ||
           b.literals+extraLiterals>LITERALS || b.fixups+extraFixups>FIXUPS)break;
        block->raw[i]=raw;
        if(memory_op(primary))memory(&b,raw,i);else integer(&b,raw);
        ++block->count;
#ifndef DC_INTERP_BLOCK_STORES
        if(store_op(primary))break;
#endif
    }
    if(!block->count){block->raw[0]=rdram[offset/4];return;}
    return_count(&b,block->count);
    if(b.words&1)emit(&b,0x0009);
    unsigned pool=b.words;
    for(unsigned i=0;i<b.literals;i++) {
        emit(&b,b.values[i]&65535);emit(&b,b.values[i]>>16);
    }
    for(unsigned i=0;i<b.fixups;i++) {
        unsigned at=b.fix[i].word,to=pool+2*b.fix[i].literal;
        unsigned disp=(to-((at+2)&~1u))/2;
        if(disp>255){block->count=0;return;}
        block->code[at]|=disp;
    }
#ifdef DC_INTERP_BLOCK_PROFILE
    block->usedWords=b.words;
#endif
#ifdef DC_INTERP_BLOCK_ARENA
    unsigned bytes=(b.words*2+31u)&~31u;
    if(bytes>sizeof(code_arena)){block->count=0;return;}
    if(arena_used+bytes>sizeof(code_arena)) {
        // No generated code is active while compiling. Preserve just this
        // candidate across global eviction; older pointers are never reused.
        Block candidate=*block;
        memset(blocks,0,sizeof(blocks));*block=candidate;
#ifdef DC_INTERP_BLOCK_HOT_ENTRY
        memset(dc_interp_hot_entries,0,sizeof(dc_interp_hot_entries));
#endif
        arena_used=0;++arena_wraps;
    }
    block->code=code_arena+arena_used/2;
    memcpy(block->code,compile_code,b.words*2);arena_used+=bytes;
#endif
#ifndef DC_HOST_STUB
    arch_icache_sync_range((uintptr_t)block->code,b.words*2);
#endif
    ++compiles;
}

void dc_interp_block_reset(void) {
    memset(blocks,0,sizeof(blocks));hits=compiles=executed=exits=clock_age=0;
#ifdef DC_INTERP_BLOCK_HOT_ENTRY
    memset(dc_interp_hot_entries,0,sizeof(dc_interp_hot_entries));
#endif
#ifdef DC_INTERP_BLOCK_ARENA
    arena_used=arena_wraps=0;
#endif
#ifdef DC_INTERP_BLOCK_EPOCH
    memset(dc_interp_block_page_epoch,0,sizeof(dc_interp_block_page_epoch));
    dc_interp_block_observer_epoch=1;epoch_live=epoch_hits=0;dc_interp_block_epoch_valid=1;
#endif
#ifdef DC_INTERP_BLOCK_PROFILE
    memset(profile_ns,0,sizeof(profile_ns));memset(profile_samples,0,sizeof(profile_samples));
    memset(profile_hist,0,sizeof(profile_hist));profile_attempts=0;profile_rng=0x71937ab1u;
    profile_active=0;profile_clock_ns=0;
#ifndef DC_HOST_STUB
    block_profile_counter=perf_cntr_timer_enabled();
#endif
    for(unsigned i=0;i<64;i++) {
        uint64_t before=block_profile_now(),after=block_profile_now();
        profile_clock_ns+=after-before;
    }
#endif
}
void dc_interp_block_report(void) {
    printf("CPU blocks: hits=%u compiles=%u instructions=%u exits=%u bytes=%u\n",
           hits,compiles,executed,exits,(unsigned)sizeof(blocks));
#ifdef DC_INTERP_BLOCK_ARENA
    printf("CPU native arena: used=%u capacity=%u wraps=%u slots=%u\n",
           arena_used,(unsigned)sizeof(code_arena),arena_wraps,SLOTS);
#endif
#ifdef DC_INTERP_BLOCK_EPOCH
    printf("CPU code epochs: hits=%u valid=%u live=%u bytes=%u\n",epoch_hits,
           dc_interp_block_epoch_valid,epoch_live,(unsigned)sizeof(dc_interp_block_page_epoch));
#endif
#ifdef DC_INTERP_BLOCK_PROFILE
    const char *names[]={"qualify","lookup","validate","compile","native"};
    printf("CPU block profile: attempts=%u clock-pairs=64 clock-ns=%llu diagnostic=1\n",
           profile_attempts,(unsigned long long)profile_clock_ns);
    for(unsigned i=0;i<PROFILE_STAGES;i++)
        printf("CPU block stage: kind=%s samples=%u ns=%llu\n",names[i],profile_samples[i],
               (unsigned long long)profile_ns[i]);
    for(unsigned i=0;i<17;i++)if(profile_hist[i])
        printf("CPU block prefix: instructions=%u calls=%u\n",i,profile_hist[i]);
    unsigned resident=0,used=0;
    for(unsigned i=0;i<SLOTS;i++)if(blocks[i].count){++resident;used+=blocks[i].usedWords*2;}
    printf("CPU block arena: resident=%u used-code-bytes=%u reserved-code-bytes=%u\n",
           resident,used,resident*CODE_WORDS*2);
#endif
}
static inline __attribute__((always_inline)) int fresh_words(const Block *block,unsigned offset,unsigned count) {
    // Both streams are aligned, count<=16, and only equality is needed. Keep
    // fresh instruction validation in the caller instead of general memcmp's
    // alignment/byte-order/ordering path and an external ABI call.
    const uint32_t *guest=(const uint32_t*)(rdramb+offset);
    for(unsigned i=0;i<count;i++)if(block->raw[i]!=guest[i])return 0;
    return 1;
}
static unsigned dc_interp_block_execute_impl(unsigned pc,unsigned limit) {
    if(!limit || (pc&3) || (pc&0xdfc00000u)!=0x80000000u)return 0;
    unsigned offset=pc&0x3ffffc;
    /* Unsupported and single-instruction starts must not evict useful native
     * blocks. The first prototype cached those starts and thrashed badly in
     * actual game code despite passing standalone execution tests. */
    if(limit<2 || offset>sizeof(rdram)-8)return 0;
#ifndef DC_INTERP_BLOCK_EPOCH
    if(!supported(rdram[offset/4]) ||
#ifndef DC_INTERP_BLOCK_STORES
       store_op(rdram[offset/4]>>26) ||
#endif
       !supported(rdram[offset/4+1]))return 0;
#endif
    BLOCK_PROFILE_PHASE(PROFILE_LOOKUP);
    unsigned set=((pc>>2)^(pc>>11))&((SLOTS/WAYS)-1);
    Block *first=&blocks[set*WAYS],*block=first;
    for(unsigned i=0;i<WAYS;i++) {
        if(first[i].pc==pc){block=first+i;break;}
        if(first[i].age<block->age)block=first+i;
    }
    unsigned count=block->count;
    BLOCK_PROFILE_PHASE(PROFILE_VALIDATE);
#ifdef DC_INTERP_BLOCK_EPOCH
    unsigned page=offset>>12,lastPage=((offset+(count?count-1:0)*4)>>12);
    int unchanged=epoch_live && dc_interp_block_epoch_valid && block->pc==pc && count &&
                  block->observerEpoch==dc_interp_block_observer_epoch &&
                  block->pageEpoch[0]==dc_interp_block_page_epoch[page] &&
                  block->pageEpoch[1]==dc_interp_block_page_epoch[lastPage];
    if(unchanged)++epoch_hits;
    if(!unchanged && (!supported(rdram[offset/4]) ||
#ifndef DC_INTERP_BLOCK_STORES
                     store_op(rdram[offset/4]>>26) ||
#endif
                     !supported(rdram[offset/4+1])))return 0;
#endif
    if(block->pc!=pc || !count || count>limit ||
#ifdef DC_INTERP_BLOCK_EPOCH
       (!unchanged && !fresh_words(block,offset,count))) {
#else
       !fresh_words(block,offset,count)) {
#endif
        BLOCK_PROFILE_PHASE(PROFILE_COMPILE);
        compile(block,pc,limit);count=block->count;
        BLOCK_PROFILE_PHASE(PROFILE_VALIDATE);
    } else ++hits;
#ifdef DC_INTERP_BLOCK_EPOCH
    // Capture before execution: a native self-write makes the next call stale.
    if(!unchanged && epoch_live && dc_interp_block_epoch_valid && count) {
        unsigned last=(offset+(count-1)*4)>>12;
        if(!dc_interp_block_page_epoch[page])dc_interp_block_page_epoch[page]=1;
        if(!dc_interp_block_page_epoch[last])dc_interp_block_page_epoch[last]=1;
        block->observerEpoch=dc_interp_block_observer_epoch;
        block->pageEpoch[0]=dc_interp_block_page_epoch[page];
        block->pageEpoch[1]=dc_interp_block_page_epoch[last];
    }
#endif
    if(!++clock_age) {
        for(unsigned i=0;i<SLOTS;i++)blocks[i].age=0;
        clock_age=1;
    }
    block->age=clock_age;
    if(count<2)return 0;
#ifdef DC_INTERP_BLOCK_HOT_ENTRY
    if(epoch_live && dc_interp_block_epoch_valid) {
        DcInterpHotEntry *entry=&dc_interp_hot_entries[dc_interp_hot_index(pc)];
        *entry=(DcInterpHotEntry){pc,count,block->observerEpoch,
            {block->pageEpoch[0],block->pageEpoch[1]},block->raw,&block->age,block->code};
    }
#endif
#ifdef DC_HOST_STUB
    /* Host tests inspect generated encodings; guest execution on ARM retains
     * the existing interpreter. Native execution is qualified separately. */
    return 0;
#else
    BLOCK_PROFILE_PHASE(PROFILE_NATIVE);
    typedef unsigned (*Native)(uint32_t*,uint32_t*,uint32_t*,uint32_t*);
    uint32_t *base=(uint32_t*)reg;
    unsigned done=((Native)block->code)(base,base+16,base+32,base+48);
    executed+=done;if(done<count)++exits;
    if(done)op=block->raw[done-1];
    return done;
#endif
}

unsigned dc_interp_block_execute(unsigned pc,unsigned limit) {
#ifdef DC_INTERP_BLOCK_PROFILE
    ++profile_attempts;
    // Pseudorandom sampling avoids locking a fixed interval to guest loops.
    profile_rng=profile_rng*1664525u+1013904223u;
    profile_active=(profile_rng>>22)==0;profile_phase=PROFILE_QUALIFY;
    if(profile_active)profile_last=block_profile_now();
#endif
    unsigned done=dc_interp_block_execute_impl(pc,limit);
#ifdef DC_INTERP_BLOCK_PROFILE
    block_profile_phase(PROFILE_QUALIFY);profile_active=0;
    if(done<=16)++profile_hist[done];
#endif
    return done;
}
