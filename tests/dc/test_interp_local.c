/* Differential CPU streams against the existing instruction handlers. The
 * oracle fetches each word anew and uses the original branch/delay machinery. */
#include "../../r4300/r4300.h"
#include "../../gc_memory/memory.h"
#include "../../r4300/interupt.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef DC_HOST_STUB
#include <kos.h>
KOS_INIT_FLAGS(INIT_DEFAULT);
#endif
#if !defined(DC_INTERP_LOCAL) && !defined(DC_INTERP_THREAD) && !defined(DC_INTERP_PC_LAZY)
#error "This test requires LOCAL, THREAD or PC_LAZY"
#endif
extern unsigned long op,dc_interp_steps,dc_interp_step_limit;
extern void (*interp_ops[64])(void);
extern void prefetch(void);
#ifndef DC_INTERP_ORACLE
#error "Differential tests require the original Count/delay test router"
#endif
extern unsigned dc_interp_oracle;
static unsigned random_state=0x482510ab,failures,hook_calls;
static unsigned random_word(void) {
    random_state^=random_state<<13;random_state^=random_state>>17;
    random_state^=random_state<<5;return random_state;
}
static void hook(void) {
    ++hook_calls;
    /* Observable handler entry is part of the differential snapshot. */
    reg[30]=((uint64_t)interp_addr<<32)|PC->addr;
    reg[29]=((uint64_t)op<<32)|address;
}
typedef struct {
    long long gpr[34];
    long long fpr[32];
    unsigned long cop0[32],pc,opcode,last,delay,steps,bus_word,bus_address;
    unsigned long fcr0,fcr31;
    unsigned long long bus_long;
    unsigned short bus_half;
    unsigned char bus_byte;
    unsigned hooks;
    unsigned event_size,next,next_vi,skip,field;
    char events[512];
    unsigned long memory[256];
} Snapshot;
static void snapshot(Snapshot *s) {
    memset(s,0,sizeof(*s));memcpy(s->gpr,reg,sizeof(reg));
    memcpy(s->fpr,reg_cop1_fgr_64,sizeof(s->fpr));s->fcr0=FCR0;s->fcr31=FCR31;
    memcpy(s->cop0,reg_cop0,sizeof(reg_cop0));
    s->pc=interp_addr;s->opcode=op;s->last=last_addr;s->delay=delay_slot;
    s->steps=dc_interp_steps;s->bus_word=word;s->bus_address=address;
    s->bus_half=hword;s->bus_byte=byte;s->hooks=hook_calls;
    s->bus_long=dword;
    s->event_size=save_eventqueue_infos(s->events);
    s->next=next_interupt;s->next_vi=next_vi;s->skip=skip_jump;s->field=vi_field;
    memcpy(s->memory,rdram+0x1000/4,sizeof(s->memory));
}
static void restore(const Snapshot *s) {
    memcpy(reg,s->gpr,sizeof(reg));memcpy(reg_cop0,s->cop0,sizeof(reg_cop0));
    memcpy(reg_cop1_fgr_64,s->fpr,sizeof(s->fpr));FCR0=s->fcr0;FCR31=s->fcr31;
    set_fpr_pointers(reg_cop0[12]);
    interp_addr=s->pc;op=s->opcode;last_addr=s->last;delay_slot=s->delay;
    dc_interp_steps=s->steps;word=s->bus_word;address=s->bus_address;
    hword=s->bus_half;byte=s->bus_byte;hook_calls=s->hooks;
    dword=s->bus_long;
    memcpy(rdram+0x1000/4,s->memory,sizeof(s->memory));
    load_eventqueue_infos(s->events);
    stop=0;next_interupt=s->next;next_vi=s->next_vi;skip_jump=s->skip;vi_field=s->field;
    dynacore=0;interpcore=1;
}
static void compare(unsigned test,unsigned limit) {
    Snapshot initial,expected,actual;
    snapshot(&initial);restore(&initial);PC=calloc(1,sizeof(*PC));
    // Keep pure-core exception delivery while forcing original Count and
    // delay dispatch. Changing CPU mode is unsafe for faults in delay slots.
    dc_interp_oracle=1;
    dc_interp_steps=0;last_addr=interp_addr;
    while(!stop) {
        if(++dc_interp_steps>=limit){stop=1;break;}
        prefetch();if(stop)break;
        interp_ops[op>>26]();
    }
    snapshot(&expected);free(PC);PC=NULL;
    restore(&initial);dc_interp_oracle=0;dc_interp_step_limit=limit;pure_interpreter();snapshot(&actual);
    if(memcmp(&expected,&actual,sizeof(actual))) {
        ++failures;printf("LOCAL FAIL case=%u pc=%08lx/%08lx op=%08lx/%08lx steps=%lu/%lu\n",
            test,expected.pc,actual.pc,expected.opcode,actual.opcode,expected.steps,actual.steps);
    }
}
int main(void) {
    static const unsigned functions[]={0,2,3,4,6,7,33,35,36,37,38,39};
    static const unsigned primaries[]={9,12,13,14,15};
    static const unsigned memory_ops[]={32,33,35,36,37,40,41,43};
    if(init_memory())return 1;
    init_interupt();next_interupt=0xfffffffful;skip_jump=0;
    count_per_op=2;
    for(unsigned test=0;test<1024;++test) {
        for(unsigned r=0;r<34;++r)reg[r]=(uint64_t)random_word()<<32|random_word();
        for(unsigned i=0;i<32;++i)reg_cop0[i]=0;
        interp_addr=(test&1?0xa0001000:0x80001000);op=0x2468;delay_slot=0;
        word=0x12345678;hword=0x9abc;byte=0xde;address=0x2345;hook_calls=0;
        for(unsigned i=0;i<32;++i) {
            unsigned rs=random_word()&31,rt=random_word()&31,rd=random_word()&31;
            rdram[0x1000/4+i]=(test+i)%3?
                (rs<<21)|(rt<<16)|(rd<<11)|((random_word()&31)<<6)|functions[(test+i)%12]:
                (primaries[(test+i)%5]<<26)|(rs<<21)|(rt<<16)|(random_word()&65535);
        }
        compare(test,2+test%31);
    }
    for(unsigned test=0;test<128;++test) {
        memset(reg,0,sizeof(reg));memset(reg_cop0,0,sizeof(reg_cop0));
        interp_addr=0x80001000;op=0;delay_slot=0;hook_calls=0;
        unsigned primary=memory_ops[test%8];
        reg[1]=(test&8?0xa0001300:0x80001300)+(test&12);
        reg[2]=(uint64_t)random_word()<<32|random_word();
        rdram[0x1300/4+(test&12)/4]=random_word();
        rdram[0x1000/4]=(9u<<26)|(5<<16)|1;
        rdram[0x1000/4+1]=(primary<<26)|(1<<21)|(2<<16);
        rdram[0x1000/4+2]=(9u<<26)|(5<<21)|(5<<16)|1;
        void (**saved)()=rwmem[(unsigned long)reg[1]>>16];
        void (*hooks[8])()={hook,hook,hook,hook,hook,hook,hook,hook};
        if(test&64)rwmem[(unsigned long)reg[1]>>16]=hooks;
        compare(1024+test,4);
        rwmem[(unsigned long)reg[1]>>16]=saved;
    }
    /* Store into the next instruction, including both physical aliases. */
    for(unsigned test=0;test<2;++test) {
        memset(reg,0,sizeof(reg));memset(reg_cop0,0,sizeof(reg_cop0));
        interp_addr=0x80001000;op=0;delay_slot=0;hook_calls=0;
        reg[1]=test?0xa0001004:0x80001004;reg[2]=(9u<<26)|(3<<16)|7;
        rdram[0x1000/4]=(43u<<26)|(1<<21)|(2<<16);rdram[0x1000/4+1]=0;
        compare(1152+test,3);
    }
    /* Taken/not-taken ordinary and likely branches, with non-NOP delays. */
    for(unsigned test=0;test<16;++test) {
        memset(reg,0,sizeof(reg));memset(reg_cop0,0,sizeof(reg_cop0));
        interp_addr=0x80001000;op=0;delay_slot=0;hook_calls=0;
        reg[1]=test&1;reg[2]=0;
        unsigned primary=(test&2?5:4)+(test&4?16:0);
        rdram[0x1000/4]=(primary<<26)|(1<<21)|(2<<16)|2;
        rdram[0x1000/4+1]=(9u<<26)|(3<<16)|7;
        rdram[0x1000/4+2]=(9u<<26)|(4<<16)|9;
        rdram[0x1000/4+3]=(9u<<26)|(5<<16)|11;
        compare(1154+test,3);
    }
    /* Raw FP transfers, paired/independent FPR layouts and bus callbacks.
     * No host FP arithmetic: NaN payloads and both 64-bit halves stay exact. */
    const unsigned fp_memory[]={49,53,57,61};
    for(unsigned test=0;test<64;++test) {
        memset(reg,0,sizeof(reg));memset(reg_cop0,0,sizeof(reg_cop0));
        reg_cop0[12]=0x20000000u|(test&1?0x04000000u:0);
        for(unsigned i=0;i<32;i++)reg_cop1_fgr_64[i]=(uint64_t)random_word()<<32|random_word();
        FCR0=0x1234;FCR31=0x800000;
        interp_addr=0x80001000;op=0;delay_slot=0;hook_calls=0;
        reg[1]=(test&2?0xa0001300:0x80001300);reg[2]=(uint64_t)random_word()<<32|random_word();
        rdram[0x1300/4]=random_word();rdram[0x1300/4+1]=random_word();
        word=0x12345678;dword=0x8765432112345678ULL;address=0x2345;
        unsigned primary=fp_memory[(test>>2)%4];
        rdram[0x1000/4]=(primary<<26)|(1<<21)|(3<<16);
        rdram[0x1000/4+1]=(17u<<26)|(4<<21)|(2<<16)|(5<<11); // MTC1
        rdram[0x1000/4+2]=(17u<<26)|(6<<16)|(5<<11); // MFC1
        void (**saved)()=rwmem[(unsigned long)reg[1]>>16];
        void (*hooks[8])()={hook,hook,hook,hook,hook,hook,hook,hook};
        if(test&32)rwmem[(unsigned long)reg[1]>>16]=hooks;
        compare(1170+test,4);rwmem[(unsigned long)reg[1]>>16]=saved;
    }
    for(unsigned test=0;test<8;++test) {
        memset(reg,0,sizeof(reg));memset(reg_cop0,0,sizeof(reg_cop0));
        interp_addr=0x80001000;op=0;delay_slot=0;hook_calls=0;
        reg[1]=0x80001300;
        rdram[0x1000/4]=test&4?(17u<<26)|(4<<21)|(2<<16)|(3<<11):
            (fp_memory[test%4]<<26)|(1<<21)|(3<<16);
        compare(1234+test,2); // CU1 disabled: precise fault before bus/FP writes.
    }
    for(unsigned test=0;test<8;++test) {
        memset(reg,0,sizeof(reg));memset(reg_cop0,0,sizeof(reg_cop0));
        reg_cop0[9]=0xfffffffcu;interp_addr=0x80001000;op=0;delay_slot=0;hook_calls=0;
        reg[1]=test&1;
        unsigned primary=(test&2?5:4)+(test&4?16:0);
        rdram[0x1000/4]=(primary<<26)|(1<<21)|2;
        rdram[0x1000/4+1]=(9u<<26)|(3<<16)|7;
        rdram[0x1000/4+2]=(9u<<26)|(4<<16)|9;
        rdram[0x1000/4+3]=(9u<<26)|(5<<16)|11;
        compare(1242+test,3);
    }
    /* Delay dispatch must retain captured branch operands, likely annulment,
     * handler-visible delay PCs, self/other register writes and CU1 faults. */
    const unsigned delayCodes[]={
        (3u<<16)|(1u<<11)|(5u<<6), // SLL into a branch operand.
        (1u<<21)|(3u<<16)|(1u<<11)|33, (1u<<21)|(3u<<16)|(1u<<11)|37,
        (9u<<26)|(1u<<21)|(1u<<16)|7, (12u<<26)|(1u<<21)|(1u<<16)|255,
        (13u<<26)|(1u<<21)|(1u<<16)|255, (15u<<26)|(1u<<16)|0x8000,
        (32u<<26)|(4u<<21)|(1u<<16), (33u<<26)|(4u<<21)|(1u<<16),
        (35u<<26)|(4u<<21)|(1u<<16), (36u<<26)|(4u<<21)|(1u<<16),
        (37u<<26)|(4u<<21)|(1u<<16), (40u<<26)|(4u<<21)|(3u<<16),
        (41u<<26)|(4u<<21)|(3u<<16), (43u<<26)|(4u<<21)|(3u<<16),
        (17u<<26)|(4u<<21)|(3u<<16)|(5u<<11)
    };
    for(unsigned test=0;test<256;test++) {
        memset(reg,0,sizeof(reg));memset(reg_cop0,0,sizeof(reg_cop0));
        reg_cop0[12]=test&8?0:0x20000000u;
        reg[1]=0x100000000LL;reg[2]=reg[1]+(test&4?1:0);
        reg[3]=(uint64_t)random_word()<<32|random_word();
        reg[4]=test&8?0xa0001300:0x80001300;
        interp_addr=0x80001000;op=0;delay_slot=0;hook_calls=0;
        word=0x12345678;byte=0x56;hword=0x789a;address=0x2345;
        rdram[0x1300/4]=random_word();rdram[0x1304/4]=random_word();
        unsigned primary=4+(test&1)+(test&2?16:0);
        rdram[0x1000/4]=(primary<<26)|(1u<<21)|(2u<<16)|2;
        rdram[0x1004/4]=delayCodes[test>>4];
        rdram[0x1008/4]=(9u<<26)|(5u<<16)|9;
        rdram[0x100c/4]=(9u<<26)|(6u<<16)|11;
        void (**saved)()=rwmem[(unsigned long)reg[4]>>16];
        void (*hooks[8])()={hook,hook,hook,hook,hook,hook,hook,hook};
        if(test&8)rwmem[(unsigned long)reg[4]>>16]=hooks;
        compare(1250+test,3);rwmem[(unsigned long)reg[4]>>16]=saved;
    }
    /* Negative/idle branches, high-half operand differences, the final RAM
     * word and a due Compare interrupt exercise boundaries independent of the
     * optimized pair's delay dispatch. Stop after the branch, before a target
     * outside RAM can be fetched. Annulled final-word slots must not be read. */
    for(unsigned test=0;test<128;test++) {
        memset(reg,0,sizeof(reg));memset(reg_cop0,0,sizeof(reg_cop0));
        reg_cop0[9]=100;reg_cop0[11]=104;init_interupt();skip_jump=0;
        unsigned variant=test>>5,config=(test>>2)&7;
        unsigned primary=4+(test&1)+(test&2?16:0);
        unsigned offset=variant==2?sizeof(rdram)-8:variant==3?sizeof(rdram)-4:0x1000;
        unsigned immediate=variant==0?0xffff:variant==1?0xfffe:2;
        reg[1]=config&2?0x8000000012345678LL:0x1000012345678LL;
        reg[2]=reg[1]+(config&1?0x100000000LL:0);
        if(variant==3) {
            primary=20+(test&1);
            reg[2]=reg[1]+(primary==20?0x100000000LL:0);
        }
        if(config&4) {
            reg_cop0[12]=0x8001;add_interupt_event_count(COMPARE_INT,104);
        } else next_interupt=0xfffffffful;
        interp_addr=(config&2?0xa0000000:0x80000000)+offset;
        op=0;delay_slot=0;hook_calls=0;
        rdram[offset/4]=(primary<<26)|(1u<<21)|(2u<<16)|immediate;
        if(variant!=3)rdram[offset/4+1]=variant==0?0:(9u<<26)|(1u<<21)|(1u<<16)|7;
        compare(1506+test,2);
    }
    printf("LOCAL RESULT arithmetic=1024 memory/hooks=128 self-modification=2 branches=16 fp-transfers/hooks=64 cu1-faults=8 count-wrap=8 branch-delays=256 branch-boundaries=128 failures=%u %s\n",
        failures,failures?"FAIL":"PASS");
    return failures?1:0;
}
