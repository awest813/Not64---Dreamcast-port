/* Same-SH4 execution tests for generated code, independent of game data.
 * The scalar oracle models the legacy pure core's integer/register semantics.
 */
#include <kos.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../../platform/dc_interp_block.h"
#include "../../platform/dc_memory.h"
#include "../../gc_memory/memory.h"
#include "../../r4300/r4300.h"
KOS_INIT_FLAGS(INIT_DEFAULT);

long long int reg[34];
unsigned long op,address,word;
unsigned long long dword;
unsigned char byte;
unsigned short hword;
unsigned long rdram[0x400000/4] __attribute__((aligned(32)));
unsigned char *const rdramb=(unsigned char*)rdram;
void (**rwmem[0x10000])();
static volatile unsigned ordinary_calls;
void read_rdram(void){ordinary_calls=1;} void read_rdramb(void){ordinary_calls=2;}
void read_rdramh(void){ordinary_calls=3;} void read_rdramd(void){ordinary_calls=4;}
void write_rdram(void){ordinary_calls=5;} void write_rdramb(void){ordinary_calls=6;}
void write_rdramh(void){ordinary_calls=7;} void write_rdramd(void){ordinary_calls=8;}
static unsigned hook_calls,failures;
static void hook(void){++hook_calls;}
static void (*ram_handlers[8])()={read_rdram,read_rdramb,read_rdramh,read_rdramd,
    write_rdram,write_rdramb,write_rdramh,write_rdramd};
static void (*other_handlers[8])()={hook,hook,hook,hook,hook,hook,hook,hook};
#ifdef DC_INTERP_BLOCK_EPOCH
#include "../../platform/dc_interp_memory.h"
#endif
static uint32_t random_state=0x92030c17;
static uint32_t random_word(void) {
    random_state^=random_state<<13;random_state^=random_state>>17;
    random_state^=random_state<<5;return random_state;
}
static uint32_t insn(unsigned primary,unsigned rs,unsigned rt,unsigned immediate) {
    return (primary<<26)|(rs<<21)|(rt<<16)|(immediate&65535);
}
#ifdef DC_INTERP_BLOCK_EPOCH
static void dma_patch(void) {rdram[0x6000/4]=insn(9,3,3,9);}
#endif
static uint32_t special(unsigned rs,unsigned rt,unsigned rd,unsigned shift,unsigned function) {
    return (rs<<21)|(rt<<16)|(rd<<11)|(shift<<6)|function;
}
static uint64_t sign32(uint32_t value){return (uint64_t)(int64_t)(int32_t)value;}
static void oracle(uint64_t *r,uint32_t instruction) {
    unsigned primary=instruction>>26,rs=(instruction>>21)&31,rt=(instruction>>16)&31;
    unsigned rd=(instruction>>11)&31,sa=(instruction>>6)&31,func=instruction&63;
    uint32_t a=(uint32_t)r[rs],b=(uint32_t)r[rt];
    if(!primary) {
        switch(func) {
        case 0:r[rd]=sign32(b<<sa);break;
        case 2:r[rd]=sign32(b>>sa);break;
        case 3:r[rd]=sign32((uint32_t)((int32_t)b>>sa));break;
        case 4:r[rd]=sign32(b<<(a&31));break;
        case 6:r[rd]=sign32(b>>(a&31));break;
        case 7:r[rd]=sign32((uint32_t)((int32_t)b>>(a&31)));break;
        case 33:r[rd]=sign32(a+b);break;
        case 35:r[rd]=sign32(a-b);break;
        case 36:r[rd]=r[rs]&r[rt];break;
        case 37:r[rd]=r[rs]|r[rt];break;
        case 38:r[rd]=r[rs]^r[rt];break;
        case 39:r[rd]=~(r[rs]|r[rt]);break;
        }
    } else switch(primary) {
    case 9:r[rt]=sign32(a+(uint32_t)(int32_t)(int16_t)instruction);break;
    case 12:r[rt]=r[rs]&(instruction&65535);break;
    case 13:r[rt]=r[rs]|(instruction&65535);break;
    case 14:r[rt]=r[rs]^(instruction&65535);break;
    case 15:r[rt]=sign32(instruction<<16);break;
    }
}
static void check(int condition,const char *name,unsigned test) {
    if(!condition){++failures;printf("BLOCK FAIL %s case=%u op=%08lx\n",name,test,op);}
}
static void arithmetic(void) {
    static const unsigned funcs[]={0,2,3,4,6,7,33,35,36,37,38,39};
    static const unsigned primaries[]={9,12,13,14,15};
    for(unsigned test=0;test<1024;test++) {
        uint64_t expected[34];
        for(unsigned i=0;i<34;i++)expected[i]=(uint64_t)random_word()<<32|random_word();
        memcpy(reg,expected,sizeof(reg));
        unsigned offset=0x1000+(test%32)*128;
        for(unsigned i=0;i<16;i++) {
            unsigned rs=random_word()&31,rt=random_word()&31,rd=random_word()&31;
            rdram[offset/4+i]=(test+i)%3?special(rs,rt,rd,random_word()&31,funcs[(test+i)%12]):
                insn(primaries[(test+i)%5],rs,rt,random_word());
        }
        unsigned n=dc_interp_block_execute(0x80000000+offset,16);
        check(n>=2 && n<=16,"native-prefix",test);
        for(unsigned i=0;i<n;i++)oracle(expected,rdram[offset/4+i]);
        check(!memcmp(reg,expected,sizeof(reg)),"arithmetic/high-halves/aliases",test);
        check(op==rdram[offset/4+n-1],"final-opcode",test);
        if(failures>10)return;
    }
}
static void memory_cases(void) {
    const unsigned reads[]={32,33,35,36,37},stores[]={40,41,43};
    for(unsigned test=0;test<80;test++) {
        unsigned primary=test<50?reads[test%5]:stores[test%3];
        unsigned size=(primary==32 || primary==36 || primary==40)?0:
                      (primary==33 || primary==37 || primary==41)?1:2;
        uint32_t guest=(test&1?0xa0004000:0x80004000)+(test&12);
        uint32_t native=guest&(sizeof(rdram)-1),value=random_word();
        memset(reg,0,sizeof(reg));reg[1]=guest;reg[2]=value;rdram[0x4000/4+(test&12)/4]=primary>=40?~value:value;
        rdram[0x1000/4]=insn(9,5,5,1);rdram[0x1000/4+1]=insn(primary,1,2,0);
        rdram[0x1000/4+2]=insn(9,5,5,1);rdram[0x1000/4+3]=0x08000000;
        address=0x123;word=0x456;byte=0x78;hword=0x9abc;
        unsigned done=dc_interp_block_execute(0x80001000,16);
        check(done==(
#ifdef DC_INTERP_BLOCK_STORES
            3u
#else
            primary>=40?2u:3u
#endif
        ),"memory-prefix/store-boundary",test);
        check((uint64_t)reg[5]==(
#ifdef DC_INTERP_BLOCK_STORES
            2u
#else
            primary>=40?1u:2u
#endif
        ),"memory-prefix-effects",test);
        check(address==guest,"bus-address",test);
        if(primary<40) {
            uint64_t expected;
            if(size==0){unsigned v=rdramb[native^3];expected=primary==36?v:(uint64_t)(int64_t)(int8_t)v;check(byte==v,"byte-global",test);}
            else if(size==1){unsigned v=*(uint16_t*)(rdramb+(native^2));expected=primary==37?v:(uint64_t)(int64_t)(int16_t)v;check(hword==v,"half-global",test);}
            else {uint32_t v=*(uint32_t*)(rdramb+native);expected=sign32(v);check(word==v,"word-global",test);}
            check((uint64_t)reg[2]==expected,"load-extension",test);
        } else {
            if(size==0){check(rdramb[native^3]==(uint8_t)value && byte==(uint8_t)value,"byte-store",test);}
            else if(size==1){check(*(uint16_t*)(rdramb+(native^2))==(uint16_t)value && hword==(uint16_t)value,"half-store",test);}
            else check(*(uint32_t*)(rdramb+native)==value && word==value,"word-store",test);
        }
    }
    // Replaced handlers on an ordinary RAM page, MMIO and unaligned accesses
    // return an executed prefix before calling a handler or changing bus globals.
    const unsigned ops[]={32,33,35,36,37,40,41,43};
    for(unsigned i=0;i<8;i++) {
        uint32_t guest=i&1?0xbf000000:0x80004000;
        void (**saved)()=rwmem[guest>>16];rwmem[guest>>16]=other_handlers;
        memset(reg,0,sizeof(reg));reg[1]=guest;
        rdram[0x1000/4]=insn(9,5,5,1);rdram[0x1000/4+1]=insn(ops[i],1,2,0);
        rdram[0x1000/4+2]=insn(9,5,5,1);address=0x123;
        check(dc_interp_block_execute(0x80001000,16)==1,"handler-side-exit",i);
        check(reg[5]==1 && !hook_calls && address==0x123,"no-device-side-effects",i);
        rwmem[guest>>16]=saved;
    }
    reg[1]=0x80004001;rdram[0x1000/4+1]=insn(35,1,2,0);
    check(dc_interp_block_execute(0x80001000,16)==1,"unaligned-side-exit",0);
    reg[1]=0; address=0x123;
    check(dc_interp_block_execute(0x80001000,16)==1 && address==0x123,"zero-address-side-exit",0);
}
static unsigned collision_address(unsigned j) {
#ifdef DC_INTERP_BLOCK_STORES
    unsigned sets=DC_INTERP_BLOCK_KB/4;
#else
    unsigned sets=DC_INTERP_BLOCK_KB/2;
#endif
    return (sets<=512?((j<<9)|j):(j*sets+j*(sets/512)))*4;
}
static void cache_reuse(void) {
    memset(reg,0,sizeof(reg));uint64_t expected=0;
    // Eight distinct PCs deliberately collide in a four-way set. Revisit
    // them in both orders and repeat a hot entry before changing code.
    for(unsigned j=1;j<=8;j++) {
        unsigned at=collision_address(j);
        rdram[at/4]=insn(9,3,3,j);rdram[at/4+1]=insn(9,3,3,1);
        rdram[at/4+2]=0x08000000;
    }
    for(unsigned pass=0;pass<16;pass++)for(unsigned n=1;n<=8;n++) {
        unsigned j=pass&1?9-n:n,at=collision_address(j);
        check(dc_interp_block_execute(0x80000000+at,16)==2,"set-eviction-prefix",pass*8+n);
        expected+=j+1;check((uint64_t)reg[3]==expected,"set-eviction-value",pass*8+n);
    }
    unsigned at=collision_address(8);
    for(unsigned pass=0;pass<8;pass++) {
        check(dc_interp_block_execute(0x80000000+at,16)==2,"hot-cache-prefix",pass);
        expected+=9;check((uint64_t)reg[3]==expected,"hot-cache-value",pass);
    }
    dc_interp_block_report();
}
static void mutation_and_limits(void) {
    memset(reg,0,sizeof(reg));
    rdram[0x1000/4]=insn(9,3,3,1);rdram[0x1000/4+1]=insn(9,3,3,1);
    rdram[0x1000/4+2]=insn(9,3,3,1);rdram[0x1000/4+3]=0x08000000;
    check(dc_interp_block_execute(0x80001000,2)==2 && reg[3]==2,"bounded-prefix",0);
    check(dc_interp_block_execute(0xa0001000,3)==3 && reg[3]==5,"fetch-alias",0);
    uint32_t replacement=insn(9,3,3,7);
    memcpy(rdramb+0x1004,&replacement,4); // A DMA-style writer, without an invalidate hook.
    check(dc_interp_block_execute(0x80001000,3)==3 && reg[3]==14,"fresh-DMA-words",0);
    reg[1]=0x80001008;reg[2]=insn(9,3,3,11);
    rdram[0x1000/4]=insn(9,3,3,1);rdram[0x1000/4+1]=insn(43,1,2,0);
    check(dc_interp_block_execute(0x80001000,16)==2 && reg[3]==15,"self-modifying-store-exit",0);
    rdram[0x1000/4+3]=insn(9,3,3,1);rdram[0x1000/4+4]=0x08000000;
    check(dc_interp_block_execute(0x80001008,16)==2 && reg[3]==27,"modified-next-block",0);
    dc_interp_block_reset();
    check(dc_interp_block_execute(0x80001008,16)==2 && reg[3]==39,"reset/reentry",0);
    check(!dc_interp_block_execute(0x80001008,0),"zero-budget",0);
    check(!dc_interp_block_execute(0x80001009,16),"unaligned-PC",0);
    check(!dc_interp_block_execute(0xa4000040,16),"device-PC",0);
    rdram[sizeof(rdram)/4-2]=insn(9,3,3,1);rdram[sizeof(rdram)/4-1]=insn(9,3,3,1);
    check(dc_interp_block_execute(0x803ffff8,16)==2 && reg[3]==41,"fetch-RDRAM-boundary",0);
}
static void store_windows(void) {
#ifdef DC_INTERP_BLOCK_STORES
#ifdef DC_INTERP_BLOCK_EPOCH
    dc_interp_block_enter();
#endif
    const int offsets[]={-4,0,4,8,12,60,64};
    unsigned cases=0;
    for(unsigned size=0;size<3;size++)for(unsigned alias=0;alias<2;alias++)
    for(unsigned n=0;n<sizeof(offsets)/sizeof(offsets[0]);n++) {
#ifdef DC_INTERP_BLOCK_EPOCH
        dc_interp_block_observer(); // Announce fixture's raw code writes below.
#endif
        unsigned at=0x2000,physical=at+offsets[n];
        unsigned guest=(alias?0xa0000000u:0x80000000u)+physical;
        memset(reg,0,sizeof(reg));reg[1]=guest;reg[2]=0;
        rdram[at/4]=insn(9,5,5,1);
        rdram[at/4+1]=insn(size==0?40:size==1?41:43,1,2,0);
        rdram[at/4+2]=insn(9,5,5,2);rdram[at/4+3]=insn(9,5,5,4);
        rdram[at/4+4]=0x08000000;
        unsigned done=dc_interp_block_execute(0x80000000u+at,16);
        int future=offsets[n]>=8 && offsets[n]<64;
        check(done==(future?2u:4u),"store-future-window",cases);
        check(reg[5]==(future?1:7),"store-before-future-effects",cases);
        check(address==guest,"store-window-bus-alias",cases);
        if(size==0)check(!rdramb[physical^3],"store-window-byte-effect",cases);
        if(size==1)check(!*(uint16_t*)(rdramb+(physical^2)),"store-window-half-effect",cases);
        if(size==2)check(!rdram[physical/4],"store-window-word-effect",cases);
        ++cases;
    }
    printf("BLOCK store-windows cases=%u\n",cases);
#ifdef DC_INTERP_BLOCK_EPOCH
    dc_interp_block_leave();
#endif
#endif
}
static void ram_proof_cases(void) {
#ifdef DC_INTERP_BLOCK_RAM_PROOF
    unsigned cases=0;
    for(unsigned scenario=0;scenario<12;scenario++)for(unsigned alias=0;alias<2;alias++) {
        uint32_t first=(alias?0xa0000000u:0x80000000u)+0x4000;
        uint32_t second=first+(scenario&1?0x10000:4);
        void (*changed[8])();memcpy(changed,ram_handlers,sizeof(changed));
        unsigned page=second>>16;void (**saved)()=rwmem[page];
        if(scenario>=2 && scenario<6)changed[scenario&2?MEM_READ_WORD:MEM_WRITE_WORD]=hook;
        if(scenario>=6 && scenario<8)second=0;
        if(scenario>=8 && scenario<10)second|=1;
        if(scenario>=10)second=(alias?0xa0000000u:0x80000000u)+0x300c;
        // Hooks on other widths do not invalidate a word-only proof.
        if(scenario==0)changed[MEM_READ_BYTE]=hook;
        rwmem[page]=changed;
        memset(reg,0,sizeof(reg));reg[1]=first;reg[2]=second;reg[3]=0;
        uint32_t a=0x12345678,b=0x87654321;
        rdram[0x4000/4]=a;rdram[0x4004/4]=b;rdram[0x14000/4]=b;
        rdram[0x3000/4]=insn(9,5,5,1);
        rdram[0x3000/4+1]=insn(35,1,4,0);
#ifdef DC_INTERP_BLOCK_STORES
        rdram[0x3000/4+2]=insn(43,2,3,0);
#else
        rdram[0x3000/4+2]=insn(35,2,3,0);
#endif
        rdram[0x3000/4+3]=insn(9,5,5,2);rdram[0x3000/4+4]=0x08000000;
        address=0x123;word=0x456;
        unsigned done=dc_interp_block_execute(0x80003000,16);
        unsigned expected=scenario>=2 && scenario<6?(scenario&1?2:1):
                          scenario>=6 && scenario<10?2:
#ifdef DC_INTERP_BLOCK_STORES
                          scenario>=10?3:4;
#else
                          4;
#endif
        check(done==expected,"word-proof-prefix/all-side-exits",cases);
        check(reg[5]==(expected==4?3:1),"word-proof-register-effects",cases);
        check(!hook_calls,"word-proof-no-callback",cases);
        if(expected>=2)check((uint64_t)reg[4]==sign32(a),"word-proof-first-load",cases);
        if(expected==1)check(address==0x123 && word==0x456,"word-proof-exit-before-bus",cases);
        if(expected==2)check(address==first && word==a,"word-proof-second-exit-before-bus",cases);
#ifdef DC_INTERP_BLOCK_STORES
        if(expected>=3)check(word==0 && !rdram[(second&0x3fffff)/4],"word-proof-store-effect",cases);
#endif
        rwmem[page]=saved;++cases;
    }
    printf("BLOCK word-proof cases=%u\n",cases);
#endif
}
static void epoch_cases(void) {
#ifdef DC_INTERP_BLOCK_EPOCH
    unsigned cases=0;
    // Real width wrappers qualify ordinary handlers, then notify physical pages.
    for(unsigned width=0;width<4;width++)for(unsigned alias=0;alias<2;alias++) {
        dc_interp_block_reset();dc_interp_block_enter();memset(reg,0,sizeof(reg));
        rdram[0x6000/4]=insn(9,3,3,1);rdram[0x6004/4]=insn(9,3,3,2);
        rdram[0x6008/4]=0x08000000;
        check(dc_interp_block_execute(0x80006000,16)==2 && reg[3]==3,"epoch-cold",cases);
        check(dc_interp_block_execute(0x80006000,16)==2 && reg[3]==6,"epoch-hot",cases);
        address=(alias?0xa0000000u:0x80000000u)+0x6000;
        switch(width) {
        case 0:address+=3;byte=9;dc_write_byte();break;
        case 1:address+=2;hword=9;dc_write_half();break;
        case 2:word=insn(9,3,3,9);dc_write_word();break;
        default:dword=((uint64_t)insn(9,3,3,9)<<32)|insn(9,3,3,2);dc_write_long();break;
        }
        check(dc_interp_block_execute(0x80006000,16)==2 && reg[3]==17,"epoch-width-alias-patch",cases++);
        dc_interp_block_leave();
    }
    for(unsigned scenario=0;scenario<8;scenario++) {
        dc_interp_block_reset();dc_interp_block_enter();memset(reg,0,sizeof(reg));
        rdram[0x6ffc/4]=insn(9,3,3,1);rdram[0x7000/4]=insn(9,3,3,2);
        rdram[0x7004/4]=0x08000000;
        check(dc_interp_block_execute(0x80006ffc,16)==2 && reg[3]==3,"epoch-two-page-cold",cases);
        unsigned patched=scenario&1?0x7000:0x6ffc;
        rdram[patched/4]=insn(9,3,3,9);
        if(scenario<2)dc_interp_block_written(0xa0000000u+patched,4);
        else if(scenario<4)dc_interp_block_observer(); // DMA/RSP/readback-style raw patch.
        else if(scenario<6)dc_interp_block_leave(); // Unannounced outside-context writes.
        else {dc_interp_block_leave();dc_interp_block_enter();} // Re-entry invalidates.
        check(dc_interp_block_execute(0x80006ffc,16)==2 && reg[3]==(patched==0x7000?13:14),
              "epoch-two-page-writer",cases++);
        dc_interp_block_leave();
    }
    // Every read/write callback can mutate RAM, even an apparent device read.
    for(unsigned slot=0;slot<8;slot++) {
        dc_interp_block_reset();dc_interp_block_enter();memset(reg,0,sizeof(reg));
        rdram[0x6000/4]=insn(9,3,3,1);rdram[0x6004/4]=insn(9,3,3,2);
        rdram[0x6008/4]=0x08000000;
        dc_interp_block_execute(0x80006000,16);
        void (*callbacks[8])();memcpy(callbacks,ram_handlers,sizeof(callbacks));
        callbacks[slot]=dma_patch;void (**saved)()=rwmem[0x8404];rwmem[0x8404]=callbacks;
        address=0x84040000;
        switch(slot) {
        case 0:dc_read_word();break;case 1:dc_read_byte();break;
        case 2:dc_read_half();break;case 3:dc_read_long();break;
        case 4:dc_write_word();break;case 5:dc_write_byte();break;
        case 6:dc_write_half();break;case 7:dc_write_long();break;
        }
        rwmem[0x8404]=saved;
        check(dc_interp_block_execute(0x80006000,16)==2 && reg[3]==14,"epoch-callback-patch",cases++);
        dc_interp_block_leave();
    }
    // A native store to an earlier cached block must invalidate its proof.
    for(unsigned width=0;width<3;width++)for(unsigned alias=0;alias<2;alias++) {
        dc_interp_block_reset();dc_interp_block_enter();memset(reg,0,sizeof(reg));
        rdram[0x6000/4]=insn(9,3,3,1);rdram[0x6004/4]=insn(9,3,3,2);
        rdram[0x6008/4]=0x08000000;dc_interp_block_execute(0x80006000,16);
        reg[1]=(alias?0xa0000000u:0x80000000u)+0x6000+(width==0?3:width==1?2:0);
        reg[2]=width<2?9:insn(9,3,3,9);
        rdram[0x8000/4]=insn(9,5,5,1);rdram[0x8004/4]=insn(width==0?40:width==1?41:43,1,2,0);
        rdram[0x8008/4]=0x08000000;
        check(dc_interp_block_execute(0x80008000,16)==2,"epoch-native-store",cases);
        check(dc_interp_block_execute(0x80006000,16)==2 && reg[3]==14,"epoch-native-store-patch",cases++);
        // Counter wrap during generated execution switches to fresh words.
        dc_interp_block_page_epoch[6]=~0u;
        dc_interp_block_execute(0x80008000,16);
        check(!dc_interp_block_epoch_valid,"epoch-native-wrap-fallback",cases++);
        dc_interp_block_leave();
    }
    dc_interp_block_reset();dc_interp_block_enter();
    dc_interp_block_page_epoch[6]=~0u;dc_interp_block_written(0xa0006000,1);
    check(!dc_interp_block_epoch_valid,"epoch-CPU-wrap-fallback",cases++);
    dc_interp_block_reset();dc_interp_block_enter();
    dc_interp_block_observer_epoch=~0u;dc_interp_block_observer();
    check(!dc_interp_block_epoch_valid,"epoch-observer-wrap-fallback",cases++);
    dc_interp_block_reset();dc_interp_block_enter();
    dc_interp_block_page_epoch[0]=dc_interp_block_page_epoch[1]=dc_interp_block_page_epoch[1023]=1;
    dc_interp_block_written(0x80000fff,2);
    check(dc_interp_block_page_epoch[0]==2 && dc_interp_block_page_epoch[1]==2,"epoch-cross-page-width",cases++);
    dc_interp_block_written(0x803fffff,2);
    check(dc_interp_block_page_epoch[1023]==2 && dc_interp_block_page_epoch[0]==3,"epoch-RAM-wrap-width",cases++);
    dc_interp_block_written(0x80009000,4);
    check(!dc_interp_block_page_epoch[9],"epoch-unwatched-data-page",cases++);
    dc_interp_block_leave();
    printf("BLOCK code-epoch cases=%u\n",cases);
#endif
}
static void arena_cases(void) {
#ifdef DC_INTERP_BLOCK_ARENA
    dc_interp_block_reset();memset(reg,0,sizeof(reg));
#ifdef DC_INTERP_BLOCK_HOT_ENTRY
    dc_interp_block_enter();unsigned previousWraps=0;
#endif
    for(unsigned n=0;n<8192;n++) {
        unsigned at=0x10000+n*128;
        for(unsigned i=0;i<16;i++)rdram[at/4+i]=0x24630001; // ADDIU r3,r3,1.
        rdram[at/4+16]=0x08000000;
#ifdef DC_INTERP_BLOCK_HOT_ENTRY
        dc_interp_block_written(0x80000000+at,68);
#endif
        unsigned done=dc_interp_block_execute(0x80000000+at,16);
        check(done==16 && reg[3]==(n+1)*16,"arena-relocated-execution",n);
        check(dc_interp_block_arena_used()<=dc_interp_block_arena_capacity(),"arena-bound",n);
#ifdef DC_INTERP_BLOCK_HOT_ENTRY
        unsigned wrapsNow=dc_interp_block_arena_wraps();
        if(wrapsNow!=previousWraps) {
            unsigned live=0;
            for(unsigned i=0;i<DC_INTERP_HOT_SLOTS;i++)live+=dc_interp_hot_entries[i].count!=0;
            check(live==1,"arena-wrap-clears-hot-entries",n);previousWraps=wrapsNow;
        }
#endif
    }
    check(dc_interp_block_arena_wraps()>0,"arena-wrap-exercised",0);
    unsigned wraps=dc_interp_block_arena_wraps();
#ifdef DC_INTERP_BLOCK_HOT_ENTRY
    dc_interp_block_leave();
#endif
    rdram[0x10000/4+1]=0x24630009;
    check(dc_interp_block_execute(0xa0010000,16)==16 && reg[3]==8192*16+24,
          "arena-old-address-alias-patch",0);
    dc_interp_block_reset();
    check(!dc_interp_block_arena_used() && !dc_interp_block_arena_wraps(),"arena-reset",0);
    check(dc_interp_block_execute(0x80010000,2)==2 && reg[3]==8192*16+34,
          "arena-reset-reentry",0);
    printf("BLOCK arena-wrap cases=8192 wraps=%u capacity=%u\n",wraps,dc_interp_block_arena_capacity());
#endif
}

static void hot_entry_cases(void) {
#ifdef DC_INTERP_BLOCK_HOT_ENTRY
    unsigned cases=0;
    dc_interp_block_reset();dc_interp_block_enter();memset(reg,0,sizeof(reg));
    rdram[0x6ffc/4]=insn(9,3,3,1);rdram[0x7000/4]=insn(9,3,3,2);
    rdram[0x7004/4]=0x08000000;
    check(dc_interp_block_execute(0x80006ffc,16)==2 && reg[3]==3,"hot-cold",cases++);
    check(dc_interp_block_hot_execute(0x80006ffc,16,(unsigned*)reg,&op)==2 && reg[3]==6 &&
          op==rdram[0x7000/4],"hot-execute-opcode",cases++);
    check(dc_interp_block_hot_execute(0x80006ffc,1,(unsigned*)reg,&op)==~0u && reg[3]==6,
          "hot-budget-miss",cases++);
    check(dc_interp_block_hot_execute(0xa0006ffc,16,(unsigned*)reg,&op)==~0u,
          "hot-alias-tag-miss",cases++);
    rdram[0x7000/4]=insn(9,3,3,9);dc_interp_block_written(0xa0007000,4);
    check(dc_interp_block_hot_execute(0x80006ffc,16,(unsigned*)reg,&op)==~0u && reg[3]==6,
          "hot-second-page-writer-miss",cases++);
    check(dc_interp_block_execute(0x80006ffc,16)==2 && reg[3]==16,"hot-recompile",cases++);
    dc_interp_block_observer();
    check(dc_interp_block_hot_execute(0x80006ffc,16,(unsigned*)reg,&op)==~0u,
          "hot-observer-miss",cases++);
    dc_interp_block_execute(0x80006ffc,16);dc_interp_block_leave();
    check(dc_interp_block_hot_execute(0x80006ffc,16,(unsigned*)reg,&op)==~0u,
          "hot-outside-scope-miss",cases++);
    dc_interp_block_enter();
    check(dc_interp_block_hot_execute(0x80006ffc,16,(unsigned*)reg,&op)==~0u,
          "hot-reentry-miss",cases++);
    dc_interp_block_execute(0x80006ffc,16);dc_interp_hot_clock_age=~0u;
    check(dc_interp_block_hot_execute(0x80006ffc,16,(unsigned*)reg,&op)==~0u,
          "hot-age-wrap-miss",cases++);
    dc_interp_block_execute(0x80006ffc,16);
    dc_interp_block_page_epoch[7]=~0u;dc_interp_block_written(0x80007000,4);
    check(dc_interp_block_hot_execute(0x80006ffc,16,(unsigned*)reg,&op)==~0u,
          "hot-page-wrap-miss",cases++);
    dc_interp_block_reset();
    check(dc_interp_block_hot_execute(0x80006ffc,16,(unsigned*)reg,&op)==~0u,
          "hot-reset-miss",cases++);
    dc_interp_block_enter();dc_interp_block_execute(0x80006ffc,16);
    dc_interp_block_observer_epoch=~0u;dc_interp_block_observer();
    check(dc_interp_block_hot_execute(0x80006ffc,16,(unsigned*)reg,&op)==~0u,
          "hot-observer-wrap-miss",cases++);
    dc_interp_block_reset();dc_interp_block_enter();
    rdram[0x6000/4]=insn(9,3,3,1);rdram[0x6004/4]=insn(9,3,3,2);
    rdram[0x6008/4]=0x08000000;dc_interp_block_execute(0x80006000,16);
    unsigned targetIndex=dc_interp_hot_index(0x80006000),evictions=0;
    for(unsigned at=0x10000;at<0x200000 && evictions<4;at+=4) {
        unsigned index=dc_interp_hot_index(0x80000000+at);
        if(index==targetIndex || (index&((DC_INTERP_HOT_SLOTS/4)-1))!=
           (targetIndex&((DC_INTERP_HOT_SLOTS/4)-1)))continue;
        rdram[at/4]=insn(9,5,5,1);rdram[at/4+1]=insn(9,5,5,2);
        rdram[at/4+2]=0x08000000;
        dc_interp_block_written(0x80000000+at,12);
        dc_interp_block_execute(0x80000000+at,16);++evictions;
    }
    check(evictions==4 && dc_interp_block_hot_execute(0x80006000,16,(unsigned*)reg,&op)==~0u,
          "hot-metadata-replacement-miss",cases++);
    dc_interp_block_reset();
    // The first native memory guard can return zero. A hot hit must report an
    // attempted exit, distinct from a miss, without calling any device hook.
    dc_interp_block_enter();reg[1]=0x80001300;reg[3]=0;
    rdram[0x6000/4]=insn(35,1,3,0);rdram[0x6004/4]=insn(9,3,3,2);
    rdram[0x6008/4]=0x08000000;dc_interp_block_execute(0x80006000,16);
    void (**saved)()=rwmem[0x8000];rwmem[0x8000]=other_handlers;
    check(dc_interp_block_hot_execute(0x80006000,16,(unsigned*)reg,&op)==0,
          "hot-zero-guarded-exit",cases++);rwmem[0x8000]=saved;
    dc_interp_block_leave();
    printf("BLOCK hot-entry cases=%u\n",cases);
#endif
}

int main(void) {
    for(unsigned i=0;i<65536;i++)rwmem[i]=other_handlers;
    for(unsigned i=0;i<64;i++)rwmem[0x8000+i]=rwmem[0xa000+i]=ram_handlers;
    dc_interp_block_reset();arithmetic();memory_cases();cache_reuse();mutation_and_limits();store_windows();ram_proof_cases();epoch_cases();arena_cases();hot_entry_cases();
    check(!ordinary_calls,"native-direct-RAM",0);
    dc_interp_block_report();
    printf("BLOCK RESULT cases=1024 memory=80 hooks=8 cache=136 failures=%u %s\n",failures,failures?"FAIL":"PASS");
    for(;;)thd_sleep(1000);
}
