#ifndef DC_INTERP_LOCAL_H
#define DC_INTERP_LOCAL_H

/* Included after the original handlers in pure_interp.c. Keep fetch/PC local
 * across ordinary integer instructions. Publish both before every original
 * handler, so delay slots, exceptions, devices and Count keep their boundaries.
 * No instruction cache: stores and DMA are visible at the very next fetch. */
static void dc_interp_run_local(void)
{
    unsigned long current_pc=interp_addr, current_op=op;
    unsigned rs,rt,rd,kind;
    unsigned long ram_address,offset;
    static const void *dispatch[64]={
        &&special,&&original,&&original,&&original,&&original,&&original,&&original,&&original,
        &&original,&&addiu,&&original,&&original,&&andi,&&ori,&&xori,&&lui,
        &&original,&&original,&&original,&&original,&&original,&&original,&&original,&&original,
        &&original,&&original,&&original,&&original,&&original,&&original,&&original,&&original,
        &&load_byte,&&load_half,&&original,&&load_word,&&load_byte,&&load_half,&&original,&&original,
        &&store_byte,&&store_half,&&original,&&store_word,&&original,&&original,&&original,&&original,
        &&original,&&original,&&original,&&original,&&original,&&original,&&original,&&original,
        &&original,&&original,&&original,&&original,&&original,&&original,&&original,&&original
    };
next:
    if(stop)goto finished;
    if(dc_interp_step_limit && ++dc_interp_steps>=dc_interp_step_limit) {
        stop=1;goto finished;
    }
    if((current_pc&0xdfc00000ul)!=0x80000000ul) {
        interp_addr=current_pc;prefetch();
        current_op=op;
        if(stop)goto finished;
        goto execute_original;
    }
    current_op=*(unsigned long *)(rdramb+(current_pc&0x3ffffcul));
    rs=(current_op>>21)&31;rt=(current_op>>16)&31;
    goto *dispatch[current_op>>26];
special:
    rd=(current_op>>11)&31;
    switch(current_op&63) {
    case 0: reg[rd]=(int)((unsigned)reg[rt]<<((current_op>>6)&31));break;
    case 2: reg[rd]=(int)((unsigned)reg[rt]>>((current_op>>6)&31));break;
    case 3: reg[rd]=(int)reg[rt]>>((current_op>>6)&31);break;
    case 4: reg[rd]=(int)((unsigned)reg[rt]<<((unsigned)reg[rs]&31));break;
    case 6: reg[rd]=(int)((unsigned)reg[rt]>>((unsigned)reg[rs]&31));break;
    case 7: reg[rd]=(int)reg[rt]>>((unsigned)reg[rs]&31);break;
    case 33: reg[rd]=(int)((unsigned)reg[rs]+(unsigned)reg[rt]);break;
    case 35: reg[rd]=(int)((unsigned)reg[rs]-(unsigned)reg[rt]);break;
    case 36: reg[rd]=reg[rs]&reg[rt];break;
    case 37: reg[rd]=reg[rs]|reg[rt];break;
    case 38: reg[rd]=reg[rs]^reg[rt];break;
    case 39: reg[rd]=~(reg[rs]|reg[rt]);break;
    default: goto original;
    }
    goto retired;
addiu: reg[rt]=(int)((unsigned)reg[rs]+(unsigned)(int)(short)current_op);goto retired;
andi: reg[rt]=reg[rs]&(unsigned short)current_op;goto retired;
ori: reg[rt]=reg[rs]|(unsigned short)current_op;goto retired;
xori: reg[rt]=reg[rs]^(unsigned short)current_op;goto retired;
lui: reg[rt]=(int)((unsigned)current_op<<16);goto retired;
load_byte:
    kind=MEM_READ_BYTE;goto memory;
load_half:
    kind=MEM_READ_HALF;goto memory;
load_word:
    kind=MEM_READ_WORD;goto memory;
store_byte:
    kind=MEM_WRITE_BYTE;goto memory;
store_half:
    kind=MEM_WRITE_HALF;goto memory;
store_word:
    kind=MEM_WRITE_WORD;
memory:
    ram_address=(unsigned)reg[rs]+(unsigned)(int)(short)current_op;
    /* Do not hide installed framebuffer hooks, translated accesses or device
     * side effects. Misaligned data keeps its existing handler path. */
    if(!ram_address || (ram_address&0xdfc00000ul)!=0x80000000ul)goto original;
    if((kind==MEM_READ_WORD || kind==MEM_WRITE_WORD) && (ram_address&3))goto original;
    if((kind==MEM_READ_HALF || kind==MEM_WRITE_HALF) && (ram_address&1))goto original;
    {
        static void (*const ordinary[8])(void)={
            read_rdram,read_rdramb,read_rdramh,read_rdramd,
            write_rdram,write_rdramb,write_rdramh,write_rdramd
        };
        if(rwmem[ram_address>>16][kind]!=ordinary[kind])goto original;
    }
    address=ram_address;offset=ram_address&0x3ffffful;
    switch(kind) {
    case MEM_READ_BYTE:
        byte=rdramb[offset^S8];
        reg[rt]=(current_op>>26)==32?(long long)(signed char)byte:byte;break;
    case MEM_READ_HALF:
        hword=*(unsigned short *)(rdramb+(offset^S16));
        reg[rt]=(current_op>>26)==33?(long long)(short)hword:hword;break;
    case MEM_READ_WORD:
        word=*(unsigned long *)(rdramb+offset);reg[rt]=(int)word;break;
    case MEM_WRITE_BYTE:
        byte=(unsigned char)reg[rt];rdramb[offset^S8]=byte;break;
    case MEM_WRITE_HALF:
        hword=(unsigned short)reg[rt];*(unsigned short *)(rdramb+(offset^S16))=hword;break;
    case MEM_WRITE_WORD:
        word=(unsigned long)reg[rt];*(unsigned long *)(rdramb+offset)=word;break;
    }
retired:
    current_pc+=4;goto next;
original:
    interp_addr=current_pc;op=current_op;PC->addr=current_pc;
execute_original:
    INTERP_EXECUTE();
    current_pc=interp_addr;current_op=op;goto next;
finished:
    interp_addr=current_pc;op=current_op;
}
#endif
