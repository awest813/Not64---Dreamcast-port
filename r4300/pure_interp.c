/**
 * Mupen64 - pure_interp.c
 * Copyright (C) 2002 Hacktarux
 *
 * Mupen64 homepage: http://mupen64.emulation64.com
 * email address: hacktarux@yahoo.fr
 *
 * If you want to contribute to the project please contact
 * me first (maybe someone is already making what you are
 * planning to do).
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
 * You should have received a copy of the GNU General Public
 * Licence along with this program; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139,
 * USA.
 *
**/

#ifdef DC_INTERP_COMPACT
// Keep this isolated from renderer/audio compiler choices. SH4 hot-code size
// and call layout are measured against O2; correctness and speed are separate.
#pragma GCC optimize ("Os")
#endif

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "r4300.h"
#include "exception.h"
#include "../gc_memory/memory.h"
#include "../gc_memory/TLB-Cache.h"
#include "macros.h"
#include "interupt.h"
#ifdef DC_INTERP_DIRECT
#include "../platform/dc_interp_decode.h"
#endif
#ifdef DC_INTERP_MEMORY
#include "../platform/dc_interp_memory.h"
#endif

#ifdef DC_INTERP_BLOCK
#include "../platform/dc_interp_block.h"
#endif

#ifdef DC_INTERP_BLOCK_EPOCH
#if defined(DBG) || defined(COMPARE_CORE)
#error "BLOCK_EPOCH is not compatible with per-instruction observers"
#endif
static void dc_pure_observed_interrupt(void)
{
   dc_interp_block_observer();
   gen_interupt();
   dc_interp_block_observer();
}
#define gen_interupt() dc_pure_observed_interrupt()
#endif

#ifdef __PPC__
#include "../main/ROM-Cache.h"
#endif
#include "../gui/DEBUG.h"
#if defined(DC_INTERP_PC_LAZY) && (defined(DBG) || defined(COMPARE_CORE))
#error "PC_LAZY is not compatible with per-instruction debugger observers"
#endif

#ifdef DBG
extern int debugger_mode;
extern void update_debugger();
#endif

#ifdef PPC_DYNAREC
#include "Invalid_Code.h"
#include "ARAM-blocks.h"

#define check_memory() \
	if(dynacore) invalidate_func(address)
#else
#define check_memory()
#endif

unsigned long interp_addr;
unsigned long op;
static long skip;
#ifdef DC_INTERP_ORACLE
// Differential-test routing only; absent from game/performance builds.
unsigned dc_interp_oracle;
#endif
#ifdef DC_INTERP_COUNT_INLINE
#if defined(DBG) || defined(COMPARE_CORE)
#error "COUNT_INLINE is not compatible with Count debugger observers"
#endif
static inline __attribute__((always_inline)) void dc_pure_update_count(void)
{
#ifdef DC_INTERP_ORACLE
   if(dc_interp_oracle){update_count();return;}
#endif
   if(interpcore) {
      Count += ((interp_addr-last_addr)>>2)*count_per_op;
      last_addr=interp_addr;
   } else update_count();
}
// Preserve every original update boundary and non-pure-core fallback.
#define update_count() dc_pure_update_count()
#endif
#ifdef __DREAMCAST__
unsigned long dc_interp_step_limit;
unsigned long dc_interp_steps;   /* retired this run; read by the bring-up */
#endif

void prefetch();

#if defined(DC_INTERP_ALU) && defined(DC_INTERP_MEMORY) && defined(DC_INTERP_DIRECT)
static inline void dc_interp_prefetch(void)
{
   /* Delay slots read RAM afresh with the same alias/alignment qualification
    * as the main loop. Translated code and devices retain prefetch checks. */
   if ((interp_addr & 0xdfc00000ul) == 0x80000000ul) {
      op = *(unsigned long *)(rdramb + (interp_addr & 0x3ffffcul));
      PC->addr = interp_addr;
   } else prefetch();
}
#define DC_INTERP_PREFETCH() dc_interp_prefetch()
#else
#define DC_INTERP_PREFETCH() prefetch()
#endif

/*static*/ void (*interp_ops[64])(void);
#ifdef DC_INTERP_ALU
static inline void dc_interp_execute(void);
#define DC_INTERP_DISPATCH() dc_interp_execute()
#else
#define DC_INTERP_DISPATCH() interp_ops[(op >> 26) & 63]()
#endif

#ifdef DC_CPU_PROFILE
static unsigned long profile_ops[64], profile_special[64], profile_fp[64];
static unsigned long profile_total;
#ifdef DC_CPU_TRACE_HOST
/* Exact execution counts in host diagnostics only. Four MiB cannot be
 * reserved casually on the target, and this work must never qualify FPS. */
static unsigned long profile_exact[0x100000],profile_other;
#endif
static struct dc_profile_sample { unsigned long page, samples; } profile_pages[64], profile_pcs[64];
static void profile_sample(struct dc_profile_sample *samples, unsigned long address)
{
   unsigned least = 0;
   for (unsigned i = 0; i < 64; ++i) {
      if (samples[i].page == address) { ++samples[i].samples; return; }
      if (samples[i].samples < samples[least].samples) least = i;
   }
   samples[least].page = address;
   ++samples[least].samples;
}
static void profile_instruction(void)
{
   unsigned primary = (op >> 26) & 63;
#ifdef DC_CPU_TRACE_HOST
   if(!(interp_addr&3) && (interp_addr&0xdfc00000u)==0x80000000u)
       ++profile_exact[(interp_addr&0x3ffffcu)/4];
   else ++profile_other;
#endif
   ++profile_ops[primary];
   if (!primary) ++profile_special[op & 63];
   if (primary == 17) ++profile_fp[(op >> 21) & 31];
   if ((++profile_total & 1023) == 0) {
      profile_sample(profile_pages, interp_addr & ~4095ul);
      profile_sample(profile_pcs, interp_addr);
   }
}
void dc_interp_profile_report(void)
{
   printf("CPU profile: instructions=%lu (includes delay slots)\n", profile_total);
#ifdef DC_CPU_TRACE_HOST
   unsigned top[64]={0},counts[64]={0};
   unsigned long total=profile_other;
   for(unsigned i=0;i<0x100000;i++) {
       unsigned count=profile_exact[i];total+=count;
       if(count<=counts[63])continue;
       unsigned at=63;
       while(at && count>counts[at-1]){counts[at]=counts[at-1];top[at]=top[at-1];--at;}
       counts[at]=count;top[at]=i;
   }
   printf("CPU exact: total=%lu other=%lu\n",total,profile_other);
   for(unsigned i=0;i<64 && counts[i];i++) {
       printf("CPU exact PC %08x count=%u code=",0x80000000u+top[i]*4,counts[i]);
       for(int j=-4;j<=4;j++) {
           int at=(int)top[i]+j;
           printf(" %08lx",at>=0 && at<0x100000?rdram[at]:0ul);
       }
       printf("\n");
   }
#endif
   for (unsigned i = 0; i < 64; ++i) if (profile_ops[i])
      printf("CPU opcode %02x count=%lu\n", i, profile_ops[i]);
   for (unsigned i = 0; i < 64; ++i) if (profile_special[i])
      printf("CPU special %02x count=%lu\n", i, profile_special[i]);
   for (unsigned i = 0; i < 32; ++i) if (profile_fp[i])
      printf("CPU cop1 %02x count=%lu\n", i, profile_fp[i]);
   for (unsigned i = 0; i < 64; ++i) if (profile_pages[i].samples)
      printf("CPU sampled page %08lx weight=%lu\n", profile_pages[i].page, profile_pages[i].samples);
   for (unsigned i = 0; i < 64; ++i) if (profile_pcs[i].samples)
      printf("CPU sampled PC %08lx weight=%lu\n", profile_pcs[i].page, profile_pcs[i].samples);
}
#define INTERP_EXECUTE() do { profile_instruction(); DC_INTERP_DISPATCH(); } while (0)
#else
#define INTERP_EXECUTE() DC_INTERP_DISPATCH()
#endif

extern unsigned long next_vi;

static void NI()
{
   printf("NI:%x\n", (unsigned int)op);
   stop=1; 
#ifdef DEBUGON
  _break();
#endif     
}

static void SLL()
{
   rrd32 = (unsigned int)(rrt32) << rsa;
   sign_extended(rrd);
   interp_addr+=4;
}

#if defined(DC_INTERP_ALU) && !defined(DC_INTERP_DELAY_ALU)
static inline void dc_interp_execute(void)
{
   /* Delay slots commonly contain NOP. Preserve SLL's register-zero
    * assignment and sign extension, avoiding two indirect opcode calls. */
   if (!op) SLL();
   else interp_ops[(op >> 26) & 63]();
}
#endif

static void SRL()
{
   rrd32 = (unsigned int)rrt32 >> rsa;
   sign_extended(rrd);
   interp_addr+=4;
}

static void SRA()
{
   rrd32 = (signed int)rrt32 >> rsa;
   sign_extended(rrd);
   interp_addr+=4;
}

static void SLLV()
{
   rrd32 = (unsigned int)(rrt32) << (rrs32&0x1F);
   sign_extended(rrd);
   interp_addr+=4;
}

static void SRLV()
{
   rrd32 = (unsigned int)rrt32 >> (rrs32 & 0x1F);
   sign_extended(rrd);
   interp_addr+=4;
}

static void SRAV()
{
   rrd32 = (signed int)rrt32 >> (rrs32 & 0x1F);
   sign_extended(rrd);
   interp_addr+=4;
}

static void JR()
{
   local_rs32 = irs32;
   interp_addr+=4;
   delay_slot=1;
   DC_INTERP_PREFETCH();
   INTERP_EXECUTE();
   update_count();
   delay_slot=0;
   interp_addr = local_rs32;
   last_addr = interp_addr;
   if (next_interupt <= Count) gen_interupt();
}

static void JALR()
{
   long long int *dest = &rrd;
   local_rs32 = rrs32;
   interp_addr+=4;
   delay_slot=1;
   DC_INTERP_PREFETCH();
   INTERP_EXECUTE();
   update_count();
   delay_slot=0;
   if (!skip_jump)
     {
	*dest = interp_addr;
	sign_extended(*dest);

	interp_addr = local_rs32;
     }
   last_addr = interp_addr;
   if (next_interupt <= Count) gen_interupt();
}

static void SYSCALL()
{
   Cause = 8 << 2;
   exception_general();
}

static void SYNC()
{
   interp_addr+=4;
}

#define DUMP_ON_BREAK
#ifdef DUMP_ON_BREAK
#ifndef __DREAMCAST__
#include <ogc/pad.h>
#endif
#endif
static void BREAK(){
#ifdef DUMP_ON_BREAK
#ifdef DEBUGON
	_break(); return;
#endif
	printf("-- BREAK @ %08x: DUMPING N64 REGISTERS --\n", interp_addr);
	int i;
	for(i=0; i<32; i+=4)
		printf("r%2d: %08x  r%2d: %08x  r%2d: %08x  r%2d: %08x\n",
		       i, (unsigned int)reg[i], i+1, (unsigned int)reg[i+1],
		       i+2, (unsigned int)reg[i+2], i+3, (unsigned int)reg[i+3]);
	printf("Press A to continue execution\n");
#ifndef __DREAMCAST__
	while(!(PAD_ButtonsHeld(0) & PAD_BUTTON_A));
	while( (PAD_ButtonsHeld(0) & PAD_BUTTON_A));
#endif
#endif
}

static void MFHI()
{
   rrd = hi;
   interp_addr+=4;
}

static void MTHI()
{
   hi = rrs;
   interp_addr+=4;
}

static void MFLO()
{
   rrd = lo;
   interp_addr+=4;
}

static void MTLO()
{
   lo = rrs;
   interp_addr+=4;
}

static void DSLLV()
{
   rrd = rrt << (rrs32&0x3F);
   interp_addr+=4;
}

static void DSRLV()
{
   rrd = (unsigned long long)rrt >> (rrs32 & 0x3F);
   interp_addr+=4;
}

static void DSRAV()
{
   rrd = (long long)rrt >> (rrs32 & 0x3F);
   interp_addr+=4;
}

static void MULT()
{
   long long int temp;
   temp = rrs * rrt;
   hi = temp >> 32;
   lo = temp;
   sign_extended(lo);
   interp_addr+=4;
}

static void MULTU()
{
   unsigned long long int temp;
   temp = (unsigned long)rrs * (unsigned long long)((unsigned long)rrt);
   hi = (long long)temp >> 32;
   lo = temp;
   sign_extended(lo);
   interp_addr+=4;
}

static void DIV()
{
   if (rrt32)
     {
	lo = rrs32 / rrt32;
	hi = rrs32 % rrt32;
	sign_extended(lo);
	sign_extended(hi);
     }
   else printf("div\n");
   interp_addr+=4;
}

static void DIVU()
{
   if (rrt32)
     {
	lo = (unsigned int)rrs32 / (unsigned int)rrt32;
	hi = (unsigned int)rrs32 % (unsigned int)rrt32;
	sign_extended(lo);
	sign_extended(hi);
     }
   else printf("divu\n");
   interp_addr+=4;
}

static void DMULT()
{
   unsigned long long int op1, op2, op3, op4;
   unsigned long long int result1, result2, result3, result4;
   unsigned long long int temp1, temp2, temp3, temp4;
   int sign = 0;

   if (rrs < 0)
     {
	op2 = -rrs;
	sign = 1 - sign;
     }
   else op2 = rrs;
   if (rrt < 0)
     {
	op4 = -rrt;
	sign = 1 - sign;
     }
   else op4 = rrt;

   op1 = op2 & 0xFFFFFFFF;
   op2 = (op2 >> 32) & 0xFFFFFFFF;
   op3 = op4 & 0xFFFFFFFF;
   op4 = (op4 >> 32) & 0xFFFFFFFF;

   temp1 = op1 * op3;
   temp2 = (temp1 >> 32) + op1 * op4;
   temp3 = op2 * op3;
   temp4 = (temp3 >> 32) + op2 * op4;

   result1 = temp1 & 0xFFFFFFFF;
   result2 = temp2 + (temp3 & 0xFFFFFFFF);
   result3 = (result2 >> 32) + temp4;
   result4 = (result3 >> 32);

   lo = result1 | (result2 << 32);
   hi = (result3 & 0xFFFFFFFF) | (result4 << 32);
   if (sign)
     {
	hi = ~hi;
	if (!lo) hi++;
	else lo = ~lo + 1;
     }
   interp_addr+=4;
}

static void DMULTU()
{
   unsigned long long int op1, op2, op3, op4;
   unsigned long long int result1, result2, result3, result4;
   unsigned long long int temp1, temp2, temp3, temp4;

   op1 = rrs & 0xFFFFFFFF;
   op2 = (rrs >> 32) & 0xFFFFFFFF;
   op3 = rrt & 0xFFFFFFFF;
   op4 = (rrt >> 32) & 0xFFFFFFFF;

   temp1 = op1 * op3;
   temp2 = (temp1 >> 32) + op1 * op4;
   temp3 = op2 * op3;
   temp4 = (temp3 >> 32) + op2 * op4;

   result1 = temp1 & 0xFFFFFFFF;
   result2 = temp2 + (temp3 & 0xFFFFFFFF);
   result3 = (result2 >> 32) + temp4;
   result4 = (result3 >> 32);

   lo = result1 | (result2 << 32);
   hi = (result3 & 0xFFFFFFFF) | (result4 << 32);

   interp_addr+=4;
}

static void DDIV()
{
   if (rrt)
     {
	lo = (long long int)rrs / (long long int)rrt;
	hi = (long long int)rrs % (long long int)rrt;
     }
   else printf("ddiv\n");
   interp_addr+=4;
}

static void DDIVU()
{
   if (rrt)
     {
	lo = (unsigned long long int)rrs / (unsigned long long int)rrt;
	hi = (unsigned long long int)rrs % (unsigned long long int)rrt;
     }
   else printf("ddivu\n");
   interp_addr+=4;
}

static void ADD()
{
   rrd32 = rrs32 + rrt32;
   sign_extended(rrd);
   interp_addr+=4;
}

static void ADDU()
{
   rrd32 = rrs32 + rrt32;
   sign_extended(rrd);
   interp_addr+=4;
}

static void SUB()
{
   rrd32 = rrs32 - rrt32;
   sign_extended(rrd);
   interp_addr+=4;
}

static void SUBU()
{
   rrd32 = rrs32 - rrt32;
   sign_extended(rrd);
   interp_addr+=4;
}

static void AND()
{
   rrd = rrs & rrt;
   interp_addr+=4;
}

static void OR()
{
   rrd = rrs | rrt;
   interp_addr+=4;
}

static void XOR()
{
   rrd = rrs ^ rrt;
   interp_addr+=4;
}

static void NOR()
{
   rrd = ~(rrs | rrt);
   interp_addr+=4;
}

static void SLT()
{
   if (rrs < rrt) rrd = 1;
   else rrd = 0;
   interp_addr+=4;
}

static void SLTU()
{
   if ((unsigned long long)rrs < (unsigned long long)rrt)
     rrd = 1;
   else rrd = 0;
   interp_addr+=4;
}

static void DADD()
{
   rrd = rrs + rrt;
   interp_addr+=4;
}

static void DADDU()
{
   rrd = rrs + rrt;
   interp_addr+=4;
}

static void DSUB()
{
   rrd = rrs - rrt;
   interp_addr+=4;
}

static void DSUBU()
{
   rrd = rrs - rrt;
   interp_addr+=4;
}

static void TEQ()
{
   if (rrs == rrt)
     {
	printf("trap exception in teq\n");
	stop=1;
#ifdef DEBUGON
  _break();
#endif     
     }
   interp_addr+=4;
}

static void DSLL()
{
   rrd = rrt << rsa;
   interp_addr+=4;
}

static void DSRL()
{
   rrd = (unsigned long long)rrt >> rsa;
   interp_addr+=4;
}

static void DSRA()
{
   rrd = rrt >> rsa;
   interp_addr+=4;
}

static void DSLL32()
{
   rrd = rrt << (32+rsa);
   interp_addr+=4;
}

static void DSRL32()
{
   rrd = (unsigned long long int)rrt >> (32+rsa);
   interp_addr+=4;
}

static void DSRA32()
{
   rrd = (signed long long int)rrt >> (32+rsa);
   interp_addr+=4;
}

static void (*interp_special[64])(void) =
{
   SLL , NI   , SRL , SRA , SLLV   , NI    , SRLV  , SRAV  ,
   JR  , JALR , NI  , NI  , SYSCALL, BREAK , NI    , SYNC  ,
   MFHI, MTHI , MFLO, MTLO, DSLLV  , NI    , DSRLV , DSRAV ,
   MULT, MULTU, DIV , DIVU, DMULT  , DMULTU, DDIV  , DDIVU ,
   ADD , ADDU , SUB , SUBU, AND    , OR    , XOR   , NOR   ,
   NI  , NI   , SLT , SLTU, DADD   , DADDU , DSUB  , DSUBU ,
   NI  , NI   , NI  , NI  , TEQ    , NI    , NI    , NI    ,
   DSLL, NI   , DSRL, DSRA, DSLL32 , NI    , DSRL32, DSRA32
};

static void BLTZ()
{
   short local_immediate = iimmediate;
   local_rs = irs;
   if ((interp_addr + (local_immediate+1)*4) == interp_addr)
     if (local_rs < 0)
       {
	  if (probe_nop(interp_addr+4))
	    {
	       update_count();
	       skip = next_interupt - Count;
	       if (skip > 3)
		 {
		    Count += (skip & 0xFFFFFFFC);
		    return;
		 }
	    }
       }
   interp_addr+=4;
   delay_slot=1;
   DC_INTERP_PREFETCH();
   INTERP_EXECUTE();
   update_count();
   delay_slot=0;
   if (local_rs < 0)
     interp_addr += (local_immediate-1)*4;
   last_addr = interp_addr;
   if (next_interupt <= Count) gen_interupt();
}

static void BGEZ()
{
   short local_immediate = iimmediate;
   local_rs = irs;
   if ((interp_addr + (local_immediate+1)*4) == interp_addr)
     if (local_rs >= 0)
       {
	  if (probe_nop(interp_addr+4))
	    {
	       update_count();
	       skip = next_interupt - Count;
	       if (skip > 3)
		 {
		    Count += (skip & 0xFFFFFFFC);
		    return;
		 }
	    }
       }
   interp_addr+=4;
   delay_slot=1;
   DC_INTERP_PREFETCH();
   INTERP_EXECUTE();
   update_count();
   delay_slot=0;
   if (local_rs >= 0)
     interp_addr += (local_immediate-1)*4;
   last_addr = interp_addr;
   if (next_interupt <= Count) gen_interupt();
}

static void BLTZL()
{
   short local_immediate = iimmediate;
   local_rs = irs;
   if ((interp_addr + (local_immediate+1)*4) == interp_addr)
     if (irs < 0)
       {
	  if (probe_nop(interp_addr+4))
	    {
	       update_count();
	       skip = next_interupt - Count;
	       if (skip > 3)
		 {
		    Count += (skip & 0xFFFFFFFC);
		    return;
		 }
	    }
       }
   if (irs < 0)
     {
	interp_addr+=4;
	delay_slot=1;
	DC_INTERP_PREFETCH();
	INTERP_EXECUTE();
	update_count();
	delay_slot=0;
	interp_addr += (local_immediate-1)*4;
     }
   else interp_addr+=8;
   last_addr = interp_addr;
   if (next_interupt <= Count) gen_interupt();
}

static void BGEZL()
{
   short local_immediate = iimmediate;
   local_rs = irs;
   if ((interp_addr + (local_immediate+1)*4) == interp_addr)
     if (irs >= 0)
       {
	  if (probe_nop(interp_addr+4))
	    {
	       update_count();
	       skip = next_interupt - Count;
	       if (skip > 3)
		 {
		    Count += (skip & 0xFFFFFFFC);
		    return;
		 }
	    }
       }
   if (irs >= 0)
     {
	interp_addr+=4;
	delay_slot=1;
	DC_INTERP_PREFETCH();
	INTERP_EXECUTE();
	update_count();
	delay_slot=0;
	interp_addr += (local_immediate-1)*4;
     }
   else interp_addr+=8;
   last_addr = interp_addr;
   if (next_interupt <= Count) gen_interupt();
}

static void BLTZAL()
{
   short local_immediate = iimmediate;
   local_rs = irs;
   reg[31]=interp_addr+8;
   if((&irs)!=(reg+31))
     {
	if ((interp_addr + (local_immediate+1)*4) == interp_addr)
	  if (local_rs < 0)
	    {
	       if (probe_nop(interp_addr+4))
		 {
		    update_count();
		    skip = next_interupt - Count;
		    if (skip > 3)
		      {
			 Count += (skip & 0xFFFFFFFC);
			 return;
		      }
		 }
	    }
	interp_addr+=4;
	delay_slot=1;
	DC_INTERP_PREFETCH();
	INTERP_EXECUTE();
	update_count();
	delay_slot=0;
	if(local_rs < 0)
	  interp_addr += (local_immediate-1)*4;
     }
   else printf("erreur dans bltzal\n");
   last_addr = interp_addr;
   if (next_interupt <= Count) gen_interupt();
}

static void BGEZAL()
{
   short local_immediate = iimmediate;
   local_rs = irs;
   reg[31]=interp_addr+8;
   if((&irs)!=(reg+31))
     {
	if ((interp_addr + (local_immediate+1)*4) == interp_addr)
	  if (local_rs >= 0)
	    {
	       if (probe_nop(interp_addr+4))
		 {
		    update_count();
		    skip = next_interupt - Count;
		    if (skip > 3)
		      {
			 Count += (skip & 0xFFFFFFFC);
			 return;
		      }
		 }
	    }
	interp_addr+=4;
	delay_slot=1;
	DC_INTERP_PREFETCH();
	INTERP_EXECUTE();
	update_count();
	delay_slot=0;
	if(local_rs >= 0)
	  interp_addr += (local_immediate-1)*4;
     }
   else printf("erreur dans bgezal\n");
   last_addr = interp_addr;
   if (next_interupt <= Count) gen_interupt();
}

static void BLTZALL()
{
   short local_immediate = iimmediate;
   local_rs = irs;
   reg[31]=interp_addr+8;
   if((&irs)!=(reg+31))
     {
	if ((interp_addr + (local_immediate+1)*4) == interp_addr)
	  if (local_rs < 0)
	    {
	       if (probe_nop(interp_addr+4))
		 {
		    update_count();
		    skip = next_interupt - Count;
		    if (skip > 3)
		      {
			 Count += (skip & 0xFFFFFFFC);
			 return;
		      }
		 }
	    }
	if (local_rs < 0)
	  {
	     interp_addr+=4;
	     delay_slot=1;
	     DC_INTERP_PREFETCH();
	     INTERP_EXECUTE();
	     update_count();
	     delay_slot=0;
	     interp_addr += (local_immediate-1)*4;
	  }
	else interp_addr+=8;
     }
   else printf("erreur dans bltzall\n");
   last_addr = interp_addr;
   if (next_interupt <= Count) gen_interupt();
}

static void BGEZALL()
{
   short local_immediate = iimmediate;
   local_rs = irs;
   reg[31]=interp_addr+8;
   if((&irs)!=(reg+31))
     {
	if ((interp_addr + (local_immediate+1)*4) == interp_addr)
	  if (local_rs >= 0)
	    {
	       if (probe_nop(interp_addr+4))
		 {
		    update_count();
		    skip = next_interupt - Count;
		    if (skip > 3)
		      {
			 Count += (skip & 0xFFFFFFFC);
			 return;
		      }
		 }
	    }
	if (local_rs >= 0)
	  {
	     interp_addr+=4;
	     delay_slot=1;
	     DC_INTERP_PREFETCH();
	     INTERP_EXECUTE();
	     update_count();
	     delay_slot=0;
	     interp_addr += (local_immediate-1)*4;
	  }
	else interp_addr+=8;
     }
   else printf("erreur dans bgezall\n");
   last_addr = interp_addr;
   if (next_interupt <= Count) gen_interupt();
}

static void (*interp_regimm[32])(void) =
{
   BLTZ  , BGEZ  , BLTZL  , BGEZL  , NI, NI, NI, NI,
   NI    , NI    , NI     , NI     , NI, NI, NI, NI,
   BLTZAL, BGEZAL, BLTZALL, BGEZALL, NI, NI, NI, NI,
   NI    , NI    , NI     , NI     , NI, NI, NI, NI
};

static void TLBR()
{
   int index;
   index = Index & 0x1F;
   PageMask = tlb_e[index].mask << 13;
   EntryHi = ((tlb_e[index].vpn2 << 13) | tlb_e[index].asid);
   EntryLo0 = (tlb_e[index].pfn_even << 6) | (tlb_e[index].c_even << 3)
     | (tlb_e[index].d_even << 2) | (tlb_e[index].v_even << 1)
       | tlb_e[index].g;
   EntryLo1 = (tlb_e[index].pfn_odd << 6) | (tlb_e[index].c_odd << 3)
     | (tlb_e[index].d_odd << 2) | (tlb_e[index].v_odd << 1)
       | tlb_e[index].g;
   interp_addr+=4;
}

static void TLBWI()
{
   unsigned int i;

   if (tlb_e[Index&0x3F].v_even)
     {
	for (i=tlb_e[Index&0x3F].start_even; i<tlb_e[Index&0x3F].end_even; i+=0x1000)
#ifdef USE_TLB_CACHE
	  TLBCache_set_r(i>>12, 0);
#else
	  tlb_LUT_r[i>>12] = 0;
#endif
	if (tlb_e[Index&0x3F].d_even)
	  for (i=tlb_e[Index&0x3F].start_even; i<tlb_e[Index&0x3F].end_even; i+=0x1000)
#ifdef USE_TLB_CACHE
	    TLBCache_set_w(i>>12, 0);
#else
	    tlb_LUT_w[i>>12] = 0;
#endif
     }
   if (tlb_e[Index&0x3F].v_odd)
     {
	for (i=tlb_e[Index&0x3F].start_odd; i<tlb_e[Index&0x3F].end_odd; i+=0x1000)
#ifdef USE_TLB_CACHE
	  TLBCache_set_r(i>>12, 0);
#else
	  tlb_LUT_r[i>>12] = 0;
#endif
	if (tlb_e[Index&0x3F].d_odd)
	  for (i=tlb_e[Index&0x3F].start_odd; i<tlb_e[Index&0x3F].end_odd; i+=0x1000)
#ifdef USE_TLB_CACHE
	    TLBCache_set_w(i>>12, 0);
#else
	    tlb_LUT_w[i>>12] = 0;
#endif
     }
   tlb_e[Index&0x3F].g = (EntryLo0 & EntryLo1 & 1);
   tlb_e[Index&0x3F].pfn_even = (EntryLo0 & 0x3FFFFFC0) >> 6;
   tlb_e[Index&0x3F].pfn_odd = (EntryLo1 & 0x3FFFFFC0) >> 6;
   tlb_e[Index&0x3F].c_even = (EntryLo0 & 0x38) >> 3;
   tlb_e[Index&0x3F].c_odd = (EntryLo1 & 0x38) >> 3;
   tlb_e[Index&0x3F].d_even = (EntryLo0 & 0x4) >> 2;
   tlb_e[Index&0x3F].d_odd = (EntryLo1 & 0x4) >> 2;
   tlb_e[Index&0x3F].v_even = (EntryLo0 & 0x2) >> 1;
   tlb_e[Index&0x3F].v_odd = (EntryLo1 & 0x2) >> 1;
   tlb_e[Index&0x3F].asid = (EntryHi & 0xFF);
   tlb_e[Index&0x3F].vpn2 = (EntryHi & 0xFFFFE000) >> 13;
   //tlb_e[Index&0x3F].r = (EntryHi & 0xC000000000000000LL) >> 62;
   tlb_e[Index&0x3F].mask = (PageMask & 0x1FFE000) >> 13;

   tlb_e[Index&0x3F].start_even = tlb_e[Index&0x3F].vpn2 << 13;
   tlb_e[Index&0x3F].end_even = tlb_e[Index&0x3F].start_even+
     (tlb_e[Index&0x3F].mask << 12) + 0xFFF;
   tlb_e[Index&0x3F].phys_even = tlb_e[Index&0x3F].pfn_even << 12;

   if (tlb_e[Index&0x3F].v_even)
     {
	if (tlb_e[Index&0x3F].start_even < tlb_e[Index&0x3F].end_even &&
	    !(tlb_e[Index&0x3F].start_even >= 0x80000000 &&
	    tlb_e[Index&0x3F].end_even < 0xC0000000) &&
	    tlb_e[Index&0x3F].phys_even < 0x20000000)
	  {
	     for (i=tlb_e[Index&0x3F].start_even;i<tlb_e[Index&0x3F].end_even;i+=0x1000)
#ifdef USE_TLB_CACHE
		TLBCache_set_r(i>>12, 0x80000000 |
	       (tlb_e[Index&0x3F].phys_even + (i - tlb_e[Index&0x3F].start_even + 0xFFF)));
#else
	       tlb_LUT_r[i>>12] = 0x80000000 |
	       (tlb_e[Index&0x3F].phys_even + (i - tlb_e[Index&0x3F].start_even + 0xFFF));
#endif
	     if (tlb_e[Index&0x3F].d_even)
	       for (i=tlb_e[Index&0x3F].start_even;i<tlb_e[Index&0x3F].end_even;i+=0x1000)
#ifdef USE_TLB_CACHE
		TLBCache_set_w(i>>12, 0x80000000 |
	       (tlb_e[Index&0x3F].phys_even + (i - tlb_e[Index&0x3F].start_even + 0xFFF)));
#else
		 tlb_LUT_w[i>>12] = 0x80000000 |
	       (tlb_e[Index&0x3F].phys_even + (i - tlb_e[Index&0x3F].start_even + 0xFFF));
#endif
	  }
     }

   tlb_e[Index&0x3F].start_odd = tlb_e[Index&0x3F].end_even+1;
   tlb_e[Index&0x3F].end_odd = tlb_e[Index&0x3F].start_odd+
     (tlb_e[Index&0x3F].mask << 12) + 0xFFF;
   tlb_e[Index&0x3F].phys_odd = tlb_e[Index&0x3F].pfn_odd << 12;

   if (tlb_e[Index&0x3F].v_odd)
     {
	if (tlb_e[Index&0x3F].start_odd < tlb_e[Index&0x3F].end_odd &&
	    !(tlb_e[Index&0x3F].start_odd >= 0x80000000 &&
	    tlb_e[Index&0x3F].end_odd < 0xC0000000) &&
	    tlb_e[Index&0x3F].phys_odd < 0x20000000)
	  {
	     for (i=tlb_e[Index&0x3F].start_odd;i<tlb_e[Index&0x3F].end_odd;i+=0x1000)
#ifdef USE_TLB_CACHE
		TLBCache_set_r(i>>12, 0x80000000 |
	       (tlb_e[Index&0x3F].phys_odd + (i - tlb_e[Index&0x3F].start_odd + 0xFFF)));
#else
	       tlb_LUT_r[i>>12] = 0x80000000 |
	       (tlb_e[Index&0x3F].phys_odd + (i - tlb_e[Index&0x3F].start_odd + 0xFFF));
#endif
	     if (tlb_e[Index&0x3F].d_odd)
	       for (i=tlb_e[Index&0x3F].start_odd;i<tlb_e[Index&0x3F].end_odd;i+=0x1000)
#ifdef USE_TLB_CACHE
		TLBCache_set_w(i>>12, 0x80000000 |
	       (tlb_e[Index&0x3F].phys_odd + (i - tlb_e[Index&0x3F].start_odd + 0xFFF)));
#else
		 tlb_LUT_w[i>>12] = 0x80000000 |
	       (tlb_e[Index&0x3F].phys_odd + (i - tlb_e[Index&0x3F].start_odd + 0xFFF));
#endif
	  }
     }
   interp_addr+=4;
}

static void TLBWR()
{
	unsigned int i;
	update_count();
	Random = (Count / count_per_op % (32 - Wired)) + Wired;

	if (tlb_e[Random].v_even){
		for (i=tlb_e[Random].start_even; i<tlb_e[Random].end_even; i+=0x1000)
#ifdef USE_TLB_CACHE
			TLBCache_set_r(i>>12, 0);
#else
			tlb_LUT_r[i>>12] = 0;
#endif
		if (tlb_e[Random].d_even)
			for (i=tlb_e[Random].start_even; i<tlb_e[Random].end_even; i+=0x1000)
#ifdef USE_TLB_CACHE
				TLBCache_set_w(i>>12, 0);
#else
				tlb_LUT_w[i>>12] = 0;
#endif
	}
	if (tlb_e[Random].v_odd){
		for (i=tlb_e[Random].start_odd; i<tlb_e[Random].end_odd; i+=0x1000)
#ifdef USE_TLB_CACHE
			TLBCache_set_r(i>>12, 0);
#else
			tlb_LUT_r[i>>12] = 0;
#endif
	if (tlb_e[Random].d_odd)
		for (i=tlb_e[Random].start_odd; i<tlb_e[Random].end_odd; i+=0x1000)
#ifdef USE_TLB_CACHE
			TLBCache_set_w(i>>12, 0);
#else
			tlb_LUT_w[i>>12] = 0;
#endif
	}

	tlb_e[Random].g = (EntryLo0 & EntryLo1 & 1);
	tlb_e[Random].pfn_even = (EntryLo0 & 0x3FFFFFC0) >> 6;
	tlb_e[Random].pfn_odd = (EntryLo1 & 0x3FFFFFC0) >> 6;
	tlb_e[Random].c_even = (EntryLo0 & 0x38) >> 3;
	tlb_e[Random].c_odd = (EntryLo1 & 0x38) >> 3;
	tlb_e[Random].d_even = (EntryLo0 & 0x4) >> 2;
	tlb_e[Random].d_odd = (EntryLo1 & 0x4) >> 2;
	tlb_e[Random].v_even = (EntryLo0 & 0x2) >> 1;
	tlb_e[Random].v_odd = (EntryLo1 & 0x2) >> 1;
	tlb_e[Random].asid = (EntryHi & 0xFF);
	tlb_e[Random].vpn2 = (EntryHi & 0xFFFFE000) >> 13;
	//tlb_e[Random].r = (EntryHi & 0xC000000000000000LL) >> 62;
	tlb_e[Random].mask = (PageMask & 0x1FFE000) >> 13;

	tlb_e[Random].start_even = tlb_e[Random].vpn2 << 13;
	tlb_e[Random].end_even = tlb_e[Random].start_even+
		(tlb_e[Random].mask << 12) + 0xFFF;
	tlb_e[Random].phys_even = tlb_e[Random].pfn_even << 12;

	if (tlb_e[Random].v_even){
		if(tlb_e[Random].start_even < tlb_e[Random].end_even &&
		   !(tlb_e[Random].start_even >= 0x80000000 &&
		   tlb_e[Random].end_even < 0xC0000000) &&
		   tlb_e[Random].phys_even < 0x20000000){
			for (i=tlb_e[Random].start_even;i<tlb_e[Random].end_even;i+=0x1000)
#ifdef USE_TLB_CACHE
				TLBCache_set_r(i>>12, 0x80000000 |
					(tlb_e[Random].phys_even + (i - tlb_e[Random].start_even + 0xFFF)));
#else
				tlb_LUT_r[i>>12] = 0x80000000 |
					(tlb_e[Random].phys_even + (i - tlb_e[Random].start_even + 0xFFF));
#endif
			if (tlb_e[Random].d_even)
				for (i=tlb_e[Random].start_even;i<tlb_e[Random].end_even;i+=0x1000)
#ifdef USE_TLB_CACHE
					TLBCache_set_w(i>>12, 0x80000000 |
						(tlb_e[Random].phys_even + (i - tlb_e[Random].start_even + 0xFFF)));
#else
					tlb_LUT_w[i>>12] = 0x80000000 |
						(tlb_e[Random].phys_even + (i - tlb_e[Random].start_even + 0xFFF));
#endif
		}
	}
	tlb_e[Random].start_odd = tlb_e[Random].end_even+1;
	tlb_e[Random].end_odd = tlb_e[Random].start_odd+
		(tlb_e[Random].mask << 12) + 0xFFF;
	tlb_e[Random].phys_odd = tlb_e[Random].pfn_odd << 12;

	if (tlb_e[Random].v_odd){
		if(tlb_e[Random].start_odd < tlb_e[Random].end_odd &&
		   !(tlb_e[Random].start_odd >= 0x80000000 &&
		   tlb_e[Random].end_odd < 0xC0000000) &&
		   tlb_e[Random].phys_odd < 0x20000000){
			for (i=tlb_e[Random].start_odd;i<tlb_e[Random].end_odd;i+=0x1000)
#ifdef USE_TLB_CACHE
				TLBCache_set_r(i>>12, 0x80000000 |
					(tlb_e[Random].phys_odd + (i - tlb_e[Random].start_odd + 0xFFF)));
#else
				tlb_LUT_r[i>>12] = 0x80000000 |
					(tlb_e[Random].phys_odd + (i - tlb_e[Random].start_odd + 0xFFF));
#endif
			if (tlb_e[Random].d_odd)
				for (i=tlb_e[Random].start_odd;i<tlb_e[Random].end_odd;i+=0x1000)
#ifdef USE_TLB_CACHE
					TLBCache_set_w(i>>2, 0x80000000 |
						(tlb_e[Random].phys_odd + (i - tlb_e[Random].start_odd + 0xFFF)));
#else
					tlb_LUT_w[i>>12] = 0x80000000 |
						(tlb_e[Random].phys_odd + (i - tlb_e[Random].start_odd + 0xFFF));
#endif
		}
	}
	interp_addr+=4;
}

static void TLBP()
{
   int i;
   Index |= 0x80000000;
   for (i=0; i<32; i++)
     {
	if (((tlb_e[i].vpn2 & (~tlb_e[i].mask)) ==
	     (((EntryHi & 0xFFFFE000) >> 13) & (~tlb_e[i].mask))) &&
	    ((tlb_e[i].g) ||
	     (tlb_e[i].asid == (EntryHi & 0xFF))))
	  {
	     Index = i;
	     break;
	  }
     }
   interp_addr+=4;
}

static void ERET()
{
//	DEBUG_print("ERET\n", DBG_USBGECKO);
   update_count();
   if (Status & 0x4)
     {
	printf("erreur dans ERET\n");
	stop=1;
#ifdef DEBUGON
  _break();
#endif     
     }
   else
     {
	Status &= 0xFFFFFFFD;
	interp_addr = EPC;
     }
   llbit = 0;
   check_interupt();
   last_addr = interp_addr;
   if (next_interupt <= Count) gen_interupt();
}

static void (*interp_tlb[64])(void) =
{
   NI  , TLBR, TLBWI, NI, NI, NI, TLBWR, NI,
   TLBP, NI  , NI   , NI, NI, NI, NI   , NI,
   NI  , NI  , NI   , NI, NI, NI, NI   , NI,
   ERET, NI  , NI   , NI, NI, NI, NI   , NI,
   NI  , NI  , NI   , NI, NI, NI, NI   , NI,
   NI  , NI  , NI   , NI, NI, NI, NI   , NI,
   NI  , NI  , NI   , NI, NI, NI, NI   , NI,
   NI  , NI  , NI   , NI, NI, NI, NI   , NI
};

static void MFC0()
{
   switch(rfs)
     {
      case 1:
	printf("lecture de Random\n");
	stop=1;
#ifdef DEBUGON
  _break();
#endif     
      default:
	rrt32 = reg_cop0[rfs];
	sign_extended(rrt);
     }
   interp_addr+=4;
}

static void MTC0()
{
   switch(rfs)
     {
      case 0:    // Index
	Index = rrt & 0x8000003F;
	if ((Index & 0x3F) > 31)
	  {
	     printf ("il y a plus de 32 TLB\n");
	     stop=1;
#ifdef DEBUGON
       _break();
#endif     
	  }
	break;
      case 1:    // Random
	break;
      case 2:    // EntryLo0
	EntryLo0 = rrt & 0x3FFFFFFF;
	break;
      case 3:    // EntryLo1
 	EntryLo1 = rrt & 0x3FFFFFFF;
	break;
      case 4:    // Context
	Context = (rrt & 0xFF800000) | (Context & 0x007FFFF0);
	break;
      case 5:    // PageMask
	PageMask = rrt & 0x01FFE000;
	break;
      case 6:    // Wired
	Wired = rrt;
	Random = 31;
	break;
      case 8:    // BadVAddr
	break;
      case 9:    // Count
	update_count();
	if (next_interupt <= Count) gen_interupt();
	translate_event_queue(rrt & 0xFFFFFFFF);
	Count = rrt & 0xFFFFFFFF;
	break;
      case 10:   // EntryHi
	EntryHi = rrt & 0xFFFFE0FF;
	break;
      case 11:   // Compare
	update_count();
	remove_event(COMPARE_INT);
	add_interupt_event_count(COMPARE_INT, (unsigned long)rrt);
	Compare = rrt;
	Cause = Cause & 0xFFFF7FFF; //Timer interupt is clear
	break;
      case 12:   // Status
	if ((rrt & 0x04000000) != (Status & 0x04000000))
	  set_fpr_pointers(rrt);
	Status = rrt;
	update_count();
	interp_addr+=4;
	check_interupt();
	if (next_interupt <= Count) gen_interupt();
	interp_addr-=4;
	break;
      case 13:   // Cause
	if (rrt!=0)
	  {
	     printf("ï¿½criture dans Cause\n");
	     stop = 1;
#ifdef DEBUGON
       _break();
#endif     
	  }
	else Cause = rrt;
	break;
      case 14:   // EPC
	EPC = rrt;
	break;
      case 15:  // PRevID
	break;
      case 16:  // Config
	Config = rrt;
	break;
      case 18:  // WatchLo
	WatchLo = rrt & 0xFFFFFFFF;
	break;
      case 19:  // WatchHi
	WatchHi = rrt & 0xFFFFFFFF;
	break;
      case 27: // CacheErr
	break;
      case 28: // TagLo
	TagLo = rrt & 0x0FFFFFC0;
	break;
      case 29: // TagHi
	TagHi =0;
	break;
      case 30: // ErrorEPC
	ErrorEPC = rrt;
	break;
      default:
	printf("unknown mtc0 write : %u\n", (unsigned)rfs);
	stop=1;
#ifdef DEBUGON
  _break();
#endif     
     }
   interp_addr+=4;
}

static void TLB()
{
   start_section(TLB_SECTION);
   interp_tlb[(op & 0x3F)]();
   end_section(TLB_SECTION);
}

static void (*interp_cop0[32])(void) =
{
   MFC0, NI, NI, NI, MTC0, NI, NI, NI,
   NI  , NI, NI, NI, NI  , NI, NI, NI,
   TLB , NI, NI, NI, NI  , NI, NI, NI,
   NI  , NI, NI, NI, NI  , NI, NI, NI
};

static void BC1F()
{
   short local_immediate = iimmediate;
   if ((interp_addr + (local_immediate+1)*4) == interp_addr)
     if ((FCR31 & 0x800000)==0)
       {
	  if (probe_nop(interp_addr+4))
	    {
	       update_count();
	       skip = next_interupt - Count;
	       if (skip > 3)
		 {
		    Count += (skip & 0xFFFFFFFC);
		    return;
		 }
	    }
       }
   interp_addr+=4;
   delay_slot=1;
   DC_INTERP_PREFETCH();
   INTERP_EXECUTE();
   update_count();
   delay_slot=0;
   if ((FCR31 & 0x800000)==0)
     interp_addr += (local_immediate-1)*4;
   last_addr = interp_addr;
   if (next_interupt <= Count) gen_interupt();
}

static void BC1T()
{
   short local_immediate = iimmediate;
   if ((interp_addr + (local_immediate+1)*4) == interp_addr)
     if ((FCR31 & 0x800000)!=0)
       {
	  if (probe_nop(interp_addr+4))
	    {
	       update_count();
	       skip = next_interupt - Count;
	       if (skip > 3)
		 {
		    Count += (skip & 0xFFFFFFFC);
		    return;
		 }
	    }
       }
   interp_addr+=4;
   delay_slot=1;
   DC_INTERP_PREFETCH();
   INTERP_EXECUTE();
   update_count();
   delay_slot=0;
   if ((FCR31 & 0x800000)!=0)
     interp_addr += (local_immediate-1)*4;
   last_addr = interp_addr;
   if (next_interupt <= Count) gen_interupt();
}

static void BC1FL()
{
   short local_immediate = iimmediate;
   if ((interp_addr + (local_immediate+1)*4) == interp_addr)
     if ((FCR31 & 0x800000)==0)
       {
	  if (probe_nop(interp_addr+4))
	    {
	       update_count();
	       skip = next_interupt - Count;
	       if (skip > 3)
		 {
		    Count += (skip & 0xFFFFFFFC);
		    return;
		 }
	    }
       }
   if ((FCR31 & 0x800000)==0)
     {
	interp_addr+=4;
	delay_slot=1;
	DC_INTERP_PREFETCH();
	INTERP_EXECUTE();
	update_count();
	delay_slot=0;
	interp_addr += (local_immediate-1)*4;
     }
   else
     interp_addr+=8;
   last_addr = interp_addr;
   if (next_interupt <= Count) gen_interupt();
}

static void BC1TL()
{
   short local_immediate = iimmediate;
   if ((interp_addr + (local_immediate+1)*4) == interp_addr)
     if ((FCR31 & 0x800000)!=0)
       {
	  if (probe_nop(interp_addr+4))
	    {
	       update_count();
	       skip = next_interupt - Count;
	       if (skip > 3)
		 {
		    Count += (skip & 0xFFFFFFFC);
		    return;
		 }
	    }
       }
   if ((FCR31 & 0x800000)!=0)
     {
	interp_addr+=4;
	delay_slot=1;
	DC_INTERP_PREFETCH();
	INTERP_EXECUTE();
	update_count();
	delay_slot=0;
	interp_addr += (local_immediate-1)*4;
     }
   else
     interp_addr+=8;
   last_addr = interp_addr;
   if (next_interupt <= Count) gen_interupt();
}

static void (*interp_cop1_bc[4])(void) =
{
   BC1F , BC1T,
   BC1FL, BC1TL
};

static void ADD_S()
{
   set_rounding();
   *reg_cop1_simple[cffd] = *reg_cop1_simple[cffs] +
     *reg_cop1_simple[cfft];
   interp_addr+=4;
}

static void SUB_S()
{
   set_rounding();
   *reg_cop1_simple[cffd] = *reg_cop1_simple[cffs] -
     *reg_cop1_simple[cfft];
   interp_addr+=4;
}

static void MUL_S()
{
   set_rounding();
   *reg_cop1_simple[cffd] = *reg_cop1_simple[cffs] *
     *reg_cop1_simple[cfft];
   interp_addr+=4;
}

static void DIV_S()
{
   if((FCR31 & 0x400) && *reg_cop1_simple[cfft] == 0)
     {
	printf("div_s by 0\n");
     }
   set_rounding();
   *reg_cop1_simple[cffd] = *reg_cop1_simple[cffs] /
     *reg_cop1_simple[cfft];
   interp_addr+=4;
}

static void SQRT_S()
{
   set_rounding();
   *reg_cop1_simple[cffd] = sqrt(*reg_cop1_simple[cffs]);
   interp_addr+=4;
}

static void ABS_S()
{
   set_rounding();
   *reg_cop1_simple[cffd] = fabs(*reg_cop1_simple[cffs]);
   interp_addr+=4;
}

static void MOV_S()
{
   set_rounding();
   *reg_cop1_simple[cffd] = *reg_cop1_simple[cffs];
   interp_addr+=4;
}

static void NEG_S()
{
   set_rounding();
   *reg_cop1_simple[cffd] = -(*reg_cop1_simple[cffs]);
   interp_addr+=4;
}

static void ROUND_L_S()
{
   set_round();
   *((long long*)(reg_cop1_double[cffd])) = *reg_cop1_simple[cffs];
   interp_addr+=4;
}

static void TRUNC_L_S()
{
   set_trunc();
   *((long long*)(reg_cop1_double[cffd])) = *reg_cop1_simple[cffs];
   interp_addr+=4;
}

static void CEIL_L_S()
{
   set_ceil();
   *((long long*)(reg_cop1_double[cffd])) = *reg_cop1_simple[cffs];
   interp_addr+=4;
}

static void FLOOR_L_S()
{
   set_floor();
   *((long long*)(reg_cop1_double[cffd])) = *reg_cop1_simple[cffs];
   interp_addr+=4;
}

static void ROUND_W_S()
{
   set_round();
   *((long*)reg_cop1_simple[cffd]) = *reg_cop1_simple[cffs];
   interp_addr+=4;
}

static void TRUNC_W_S()
{
   set_trunc();
   *((long*)reg_cop1_simple[cffd]) = *reg_cop1_simple[cffs];
   interp_addr+=4;
}

static void CEIL_W_S()
{
   set_ceil();
   *((long*)reg_cop1_simple[cffd]) = *reg_cop1_simple[cffs];
   interp_addr+=4;
}

static void FLOOR_W_S()
{
   set_floor();
   *((long*)reg_cop1_simple[cffd]) = *reg_cop1_simple[cffs];
   interp_addr+=4;
}

static void CVT_D_S()
{
   set_rounding();
   *reg_cop1_double[cffd] = *reg_cop1_simple[cffs];
   interp_addr+=4;
}

static void CVT_W_S()
{
   set_rounding();
   *((long*)reg_cop1_simple[cffd]) = *reg_cop1_simple[cffs];
   interp_addr+=4;
}

static void CVT_L_S()
{
   set_rounding();
   *((long long*)(reg_cop1_double[cffd])) = *reg_cop1_simple[cffs];
   interp_addr+=4;
}

static void C_F_S()
{
   FCR31 &= ~0x800000;
   interp_addr+=4;
}

static void C_UN_S()
{
   if (isnan(*reg_cop1_simple[cffs]) || isnan(*reg_cop1_simple[cfft]))
     FCR31 |= 0x800000;
   else FCR31 &= ~0x800000;
   interp_addr+=4;
}

static void C_EQ_S()
{
   if (!isnan(*reg_cop1_simple[cffs]) && !isnan(*reg_cop1_simple[cfft]) &&
       *reg_cop1_simple[cffs] == *reg_cop1_simple[cfft])
     FCR31 |= 0x800000;
   else FCR31 &= ~0x800000;
   interp_addr+=4;
}

static void C_UEQ_S()
{
   if (isnan(*reg_cop1_simple[cffs]) || isnan(*reg_cop1_simple[cfft]) ||
       *reg_cop1_simple[cffs] == *reg_cop1_simple[cfft])
     FCR31 |= 0x800000;
   else FCR31 &= ~0x800000;
   interp_addr+=4;
}

static void C_OLT_S()
{
   if (!isnan(*reg_cop1_simple[cffs]) && !isnan(*reg_cop1_simple[cfft]) &&
       *reg_cop1_simple[cffs] < *reg_cop1_simple[cfft])
     FCR31 |= 0x800000;
   else FCR31 &= ~0x800000;
   interp_addr+=4;
}

static void C_ULT_S()
{
   if (isnan(*reg_cop1_simple[cffs]) || isnan(*reg_cop1_simple[cfft]) ||
       *reg_cop1_simple[cffs] < *reg_cop1_simple[cfft])
     FCR31 |= 0x800000;
   else FCR31 &= ~0x800000;
   interp_addr+=4;
}

static void C_OLE_S()
{
   if (!isnan(*reg_cop1_simple[cffs]) && !isnan(*reg_cop1_simple[cfft]) &&
       *reg_cop1_simple[cffs] <= *reg_cop1_simple[cfft])
     FCR31 |= 0x800000;
   else FCR31 &= ~0x800000;
   interp_addr+=4;
}

static void C_ULE_S()
{
   if (isnan(*reg_cop1_simple[cffs]) || isnan(*reg_cop1_simple[cfft]) ||
       *reg_cop1_simple[cffs] <= *reg_cop1_simple[cfft])
     FCR31 |= 0x800000;
   else FCR31 &= ~0x800000;
   interp_addr+=4;
}

static void C_SF_S()
{
   if (isnan(*reg_cop1_simple[cffs]) || isnan(*reg_cop1_simple[cfft]))
     {
	printf("Invalid operation exception in C opcode\n");
	stop=1;
#ifdef DEBUGON
  _break();
#endif     
     }
   FCR31 &= ~0x800000;
   interp_addr+=4;
}

static void C_NGLE_S()
{
   if (isnan(*reg_cop1_simple[cffs]) || isnan(*reg_cop1_simple[cfft]))
     {
	printf("Invalid operation exception in C opcode\n");
	stop=1;
#ifdef DEBUGON
  _break();
#endif     
     }
   FCR31 &= ~0x800000;
   interp_addr+=4;
}

static void C_SEQ_S()
{
   if (isnan(*reg_cop1_simple[cffs]) || isnan(*reg_cop1_simple[cfft]))
     {
	printf("Invalid operation exception in C opcode\n");
	stop=1;
#ifdef DEBUGON
  _break();
#endif     
     }
   if (*reg_cop1_simple[cffs] == *reg_cop1_simple[cfft])
     FCR31 |= 0x800000;
   else FCR31 &= ~0x800000;
   interp_addr+=4;
}

static void C_NGL_S()
{
   if (isnan(*reg_cop1_simple[cffs]) || isnan(*reg_cop1_simple[cfft]))
     {
	printf("Invalid operation exception in C opcode\n");
	stop=1;
#ifdef DEBUGON
  _break();
#endif     
     }
   if (*reg_cop1_simple[cffs] == *reg_cop1_simple[cfft])
     FCR31 |= 0x800000;
   else FCR31 &= ~0x800000;
   interp_addr+=4;
}

static void C_LT_S()
{
   if (isnan(*reg_cop1_simple[cffs]) || isnan(*reg_cop1_simple[cfft]))
     {
	printf("Invalid operation exception in C opcode\n");
	stop=1;
#ifdef DEBUGON
  _break();
#endif     
     }
   if (*reg_cop1_simple[cffs] < *reg_cop1_simple[cfft])
     FCR31 |= 0x800000;
   else FCR31 &= ~0x800000;
   interp_addr+=4;
}

static void C_NGE_S()
{
   if (isnan(*reg_cop1_simple[cffs]) || isnan(*reg_cop1_simple[cfft]))
     {
	printf("Invalid operation exception in C opcode\n");
	stop=1;
#ifdef DEBUGON
  _break();
#endif     
     }
   if (*reg_cop1_simple[cffs] < *reg_cop1_simple[cfft])
     FCR31 |= 0x800000;
   else FCR31 &= ~0x800000;
   interp_addr+=4;
}

static void C_LE_S()
{
   if (isnan(*reg_cop1_simple[cffs]) || isnan(*reg_cop1_simple[cfft]))
     {
	printf("Invalid operation exception in C opcode\n");
	stop=1;
#ifdef DEBUGON
  _break();
#endif     
     }
   if (*reg_cop1_simple[cffs] <= *reg_cop1_simple[cfft])
     FCR31 |= 0x800000;
   else FCR31 &= ~0x800000;
   interp_addr+=4;
}

static void C_NGT_S()
{
   if (isnan(*reg_cop1_simple[cffs]) || isnan(*reg_cop1_simple[cfft]))
     {
	printf("Invalid operation exception in C opcode\n");
	stop=1;
#ifdef DEBUGON
  _break();
#endif     
     }
   if (*reg_cop1_simple[cffs] <= *reg_cop1_simple[cfft])
     FCR31 |= 0x800000;
   else FCR31 &= ~0x800000;
   interp_addr+=4;
}

static void (*interp_cop1_s[64])(void) =
{
ADD_S    ,SUB_S    ,MUL_S   ,DIV_S    ,SQRT_S   ,ABS_S    ,MOV_S   ,NEG_S    ,
ROUND_L_S,TRUNC_L_S,CEIL_L_S,FLOOR_L_S,ROUND_W_S,TRUNC_W_S,CEIL_W_S,FLOOR_W_S,
NI       ,NI       ,NI      ,NI       ,NI       ,NI       ,NI      ,NI       ,
NI       ,NI       ,NI      ,NI       ,NI       ,NI       ,NI      ,NI       ,
NI       ,CVT_D_S  ,NI      ,NI       ,CVT_W_S  ,CVT_L_S  ,NI      ,NI       ,
NI       ,NI       ,NI      ,NI       ,NI       ,NI       ,NI      ,NI       ,
C_F_S    ,C_UN_S   ,C_EQ_S  ,C_UEQ_S  ,C_OLT_S  ,C_ULT_S  ,C_OLE_S ,C_ULE_S  ,
C_SF_S   ,C_NGLE_S ,C_SEQ_S ,C_NGL_S  ,C_LT_S   ,C_NGE_S  ,C_LE_S  ,C_NGT_S
};

static void ADD_D()
{
   set_rounding();
   *reg_cop1_double[cffd] = *reg_cop1_double[cffs] +
     *reg_cop1_double[cfft];
   interp_addr+=4;
}

static void SUB_D()
{
   set_rounding();
   *reg_cop1_double[cffd] = *reg_cop1_double[cffs] -
     *reg_cop1_double[cfft];
   interp_addr+=4;
}

static void MUL_D()
{
   set_rounding();
   *reg_cop1_double[cffd] = *reg_cop1_double[cffs] *
     *reg_cop1_double[cfft];
   interp_addr+=4;
}

static void DIV_D()
{
   if((FCR31 & 0x400) && *reg_cop1_double[cfft] == 0)
     {
	//FCR31 |= 0x8020;
	/*FCR31 |= 0x8000;
	Cause = 15 << 2;
	exception_general();*/
	printf("div_d by 0\n");
	//return;
     }
   set_rounding();
   *reg_cop1_double[cffd] = *reg_cop1_double[cffs] /
     *reg_cop1_double[cfft];
   interp_addr+=4;
}

static void SQRT_D()
{
   set_rounding();
   *reg_cop1_double[cffd] = sqrt(*reg_cop1_double[cffs]);
   interp_addr+=4;
}

static void ABS_D()
{
   set_rounding();
   *reg_cop1_double[cffd] = fabs(*reg_cop1_double[cffs]);
   interp_addr+=4;
}

static void MOV_D()
{
   set_rounding();
   *reg_cop1_double[cffd] = *reg_cop1_double[cffs];
   interp_addr+=4;
}

static void NEG_D()
{
   set_rounding();
   *reg_cop1_double[cffd] = -(*reg_cop1_double[cffs]);
   interp_addr+=4;
}

static void ROUND_L_D()
{
   set_round();
   *((long long*)(reg_cop1_double[cffd])) = *reg_cop1_double[cffs];
   interp_addr+=4;
}

static void TRUNC_L_D()
{
   set_trunc();
   *((long long*)(reg_cop1_double[cffd])) = *reg_cop1_double[cffs];
   interp_addr+=4;
}

static void CEIL_L_D()
{
   set_ceil();
   *((long long*)(reg_cop1_double[cffd])) = *reg_cop1_double[cffs];
   interp_addr+=4;
}

static void FLOOR_L_D()
{
   set_floor();
   *((long long*)(reg_cop1_double[cffd])) = *reg_cop1_double[cffs];
   interp_addr+=4;
}

static void ROUND_W_D()
{
   set_round();
   *((long*)reg_cop1_simple[cffd]) = *reg_cop1_double[cffs];
   interp_addr+=4;
}

static void TRUNC_W_D()
{
   set_trunc();
   *((long*)reg_cop1_simple[cffd]) = *reg_cop1_double[cffs];
   interp_addr+=4;
}

static void CEIL_W_D()
{
   set_ceil();
   *((long*)reg_cop1_simple[cffd]) = *reg_cop1_double[cffs];
   interp_addr+=4;
}

static void FLOOR_W_D()
{
   set_floor();
   *((long*)reg_cop1_simple[cffd]) = *reg_cop1_double[cffs];
   interp_addr+=4;
}

static void CVT_S_D()
{
   set_rounding();
   *reg_cop1_simple[cffd] = *reg_cop1_double[cffs];
   interp_addr+=4;
}

static void CVT_W_D()
{
   set_rounding();
   *((long*)reg_cop1_simple[cffd]) = *reg_cop1_double[cffs];
   interp_addr+=4;
}

static void CVT_L_D()
{
   set_rounding();
   *((long long*)(reg_cop1_double[cffd])) = *reg_cop1_double[cffs];
   interp_addr+=4;
}

static void C_F_D()
{
   FCR31 &= ~0x800000;
   interp_addr+=4;
}

static void C_UN_D()
{
   if (isnan(*reg_cop1_double[cffs]) || isnan(*reg_cop1_double[cfft]))
     FCR31 |= 0x800000;
   else FCR31 &= ~0x800000;
   interp_addr+=4;
}

static void C_EQ_D()
{
   if (!isnan(*reg_cop1_double[cffs]) && !isnan(*reg_cop1_double[cfft]) &&
       *reg_cop1_double[cffs] == *reg_cop1_double[cfft])
     FCR31 |= 0x800000;
   else FCR31 &= ~0x800000;
   interp_addr+=4;
}

static void C_UEQ_D()
{
   if (isnan(*reg_cop1_double[cffs]) || isnan(*reg_cop1_double[cfft]) ||
       *reg_cop1_double[cffs] == *reg_cop1_double[cfft])
     FCR31 |= 0x800000;
   else FCR31 &= ~0x800000;
   interp_addr+=4;
}

static void C_OLT_D()
{
   if (!isnan(*reg_cop1_double[cffs]) && !isnan(*reg_cop1_double[cfft]) &&
       *reg_cop1_double[cffs] < *reg_cop1_double[cfft])
     FCR31 |= 0x800000;
   else FCR31 &= ~0x800000;
   interp_addr+=4;
}

static void C_ULT_D()
{
   if (isnan(*reg_cop1_double[cffs]) || isnan(*reg_cop1_double[cfft]) ||
       *reg_cop1_double[cffs] < *reg_cop1_double[cfft])
     FCR31 |= 0x800000;
   else FCR31 &= ~0x800000;
   interp_addr+=4;
}

static void C_OLE_D()
{
   if (!isnan(*reg_cop1_double[cffs]) && !isnan(*reg_cop1_double[cfft]) &&
       *reg_cop1_double[cffs] <= *reg_cop1_double[cfft])
     FCR31 |= 0x800000;
   else FCR31 &= ~0x800000;
   interp_addr+=4;
}

static void C_ULE_D()
{
   if (isnan(*reg_cop1_double[cffs]) || isnan(*reg_cop1_double[cfft]) ||
       *reg_cop1_double[cffs] <= *reg_cop1_double[cfft])
     FCR31 |= 0x800000;
   else FCR31 &= ~0x800000;
   interp_addr+=4;
}

static void C_SF_D()
{
   if (isnan(*reg_cop1_double[cffs]) || isnan(*reg_cop1_double[cfft]))
     {
	printf("Invalid operation exception in C opcode\n");
	stop=1;
#ifdef DEBUGON
  _break();
#endif     
     }
   FCR31 &= ~0x800000;
   interp_addr+=4;
}

static void C_NGLE_D()
{
   if (isnan(*reg_cop1_double[cffs]) || isnan(*reg_cop1_double[cfft]))
     {
	printf("Invalid operation exception in C opcode\n");
	stop=1;
#ifdef DEBUGON
  _break();
#endif     
     }
   FCR31 &= ~0x800000;
   interp_addr+=4;
}

static void C_SEQ_D()
{
   if (isnan(*reg_cop1_double[cffs]) || isnan(*reg_cop1_double[cfft]))
     {
	printf("Invalid operation exception in C opcode\n");
	stop=1;
#ifdef DEBUGON
  _break();
#endif     
     }
   if (*reg_cop1_double[cffs] == *reg_cop1_double[cfft])
     FCR31 |= 0x800000;
   else FCR31 &= ~0x800000;
   interp_addr+=4;
}

static void C_NGL_D()
{
   if (isnan(*reg_cop1_double[cffs]) || isnan(*reg_cop1_double[cfft]))
     {
	printf("Invalid operation exception in C opcode\n");
	stop=1;
#ifdef DEBUGON
  _break();
#endif     
     }
   if (*reg_cop1_double[cffs] == *reg_cop1_double[cfft])
     FCR31 |= 0x800000;
   else FCR31 &= ~0x800000;
   interp_addr+=4;
}

static void C_LT_D()
{
   if (isnan(*reg_cop1_double[cffs]) || isnan(*reg_cop1_double[cfft]))
     {
	printf("Invalid operation exception in C opcode\n");
	stop=1;
#ifdef DEBUGON
  _break();
#endif     
     }
   if (*reg_cop1_double[cffs] < *reg_cop1_double[cfft])
     FCR31 |= 0x800000;
   else FCR31 &= ~0x800000;
   interp_addr+=4;
}

static void C_NGE_D()
{
   if (isnan(*reg_cop1_double[cffs]) || isnan(*reg_cop1_double[cfft]))
     {
	printf("Invalid operation exception in C opcode\n");
	stop=1;
#ifdef DEBUGON
  _break();
#endif     
     }
   if (*reg_cop1_double[cffs] < *reg_cop1_double[cfft])
     FCR31 |= 0x800000;
   else FCR31 &= ~0x800000;
   interp_addr+=4;
}

static void C_LE_D()
{
   if (isnan(*reg_cop1_double[cffs]) || isnan(*reg_cop1_double[cfft]))
     {
	printf("Invalid operation exception in C opcode\n");
	stop=1;
#ifdef DEBUGON
  _break();
#endif     
     }
   if (*reg_cop1_double[cffs] <= *reg_cop1_double[cfft])
     FCR31 |= 0x800000;
   else FCR31 &= ~0x800000;
   interp_addr+=4;
}

static void C_NGT_D()
{
   if (isnan(*reg_cop1_double[cffs]) || isnan(*reg_cop1_double[cfft]))
     {
	printf("Invalid operation exception in C opcode\n");
	stop=1;
#ifdef DEBUGON
  _break();
#endif     
     }
   if (*reg_cop1_double[cffs] <= *reg_cop1_double[cfft])
     FCR31 |= 0x800000;
   else FCR31 &= ~0x800000;
   interp_addr+=4;
}

static void (*interp_cop1_d[64])(void) =
{
ADD_D    ,SUB_D    ,MUL_D   ,DIV_D    ,SQRT_D   ,ABS_D    ,MOV_D   ,NEG_D    ,
ROUND_L_D,TRUNC_L_D,CEIL_L_D,FLOOR_L_D,ROUND_W_D,TRUNC_W_D,CEIL_W_D,FLOOR_W_D,
NI       ,NI       ,NI      ,NI       ,NI       ,NI       ,NI      ,NI       ,
NI       ,NI       ,NI      ,NI       ,NI       ,NI       ,NI      ,NI       ,
CVT_S_D  ,NI       ,NI      ,NI       ,CVT_W_D  ,CVT_L_D  ,NI      ,NI       ,
NI       ,NI       ,NI      ,NI       ,NI       ,NI       ,NI      ,NI       ,
C_F_D    ,C_UN_D   ,C_EQ_D  ,C_UEQ_D  ,C_OLT_D  ,C_ULT_D  ,C_OLE_D ,C_ULE_D  ,
C_SF_D   ,C_NGLE_D ,C_SEQ_D ,C_NGL_D  ,C_LT_D   ,C_NGE_D  ,C_LE_D  ,C_NGT_D
};

static void CVT_S_W()
{
   set_rounding();
   *reg_cop1_simple[cffd] = *((long*)reg_cop1_simple[cffs]);
   interp_addr+=4;
}

static void CVT_D_W()
{
   set_rounding();
   *reg_cop1_double[cffd] = *((long*)reg_cop1_simple[cffs]);
   interp_addr+=4;
}

static void (*interp_cop1_w[64])(void) =
{
   NI     , NI     , NI, NI, NI, NI, NI, NI,
   NI     , NI     , NI, NI, NI, NI, NI, NI,
   NI     , NI     , NI, NI, NI, NI, NI, NI,
   NI     , NI     , NI, NI, NI, NI, NI, NI,
   CVT_S_W, CVT_D_W, NI, NI, NI, NI, NI, NI,
   NI     , NI     , NI, NI, NI, NI, NI, NI,
   NI     , NI     , NI, NI, NI, NI, NI, NI,
   NI     , NI     , NI, NI, NI, NI, NI, NI
};

static void CVT_S_L()
{
   set_rounding();
   *reg_cop1_simple[cffd] = *((long long*)(reg_cop1_double[cffs]));
   interp_addr+=4;
}

static void CVT_D_L()
{
   set_rounding();
   *reg_cop1_double[cffd] = *((long long*)(reg_cop1_double[cffs]));
   interp_addr+=4;
}

static void (*interp_cop1_l[64])(void) =
{
   NI     , NI     , NI, NI, NI, NI, NI, NI,
   NI     , NI     , NI, NI, NI, NI, NI, NI,
   NI     , NI     , NI, NI, NI, NI, NI, NI,
   NI     , NI     , NI, NI, NI, NI, NI, NI,
   CVT_S_L, CVT_D_L, NI, NI, NI, NI, NI, NI,
   NI     , NI     , NI, NI, NI, NI, NI, NI,
   NI     , NI     , NI, NI, NI, NI, NI, NI,
   NI     , NI     , NI, NI, NI, NI, NI, NI
};

static void MFC1()
{
   rrt32 = *((long*)reg_cop1_simple[rfs]);
   sign_extended(rrt);
   interp_addr+=4;
}

static void DMFC1()
{
   rrt = *((long long*)(reg_cop1_double[rfs]));
   interp_addr+=4;
}

static void CFC1()
{
   if (rfs==31)
     {
	rrt32 = FCR31;
	sign_extended(rrt);
     }
   if (rfs==0)
     {
	rrt32 = FCR0;
	sign_extended(rrt);
     }
   interp_addr+=4;
}

static void MTC1()
{
   *((long*)reg_cop1_simple[rfs]) = rrt32;
   interp_addr+=4;
}

static void DMTC1()
{
   *((long long*)reg_cop1_double[rfs]) = rrt;
   interp_addr+=4;
}

static void CTC1()
{
   if (rfs==31)
     FCR31 = rrt32;
   switch((FCR31 & 3))
     {
      case 0:
	rounding_mode = 0x33F;
	break;
      case 1:
	rounding_mode = 0xF3F;
	break;
      case 2:
	rounding_mode = 0xB3F;
	break;
      case 3:
	rounding_mode = 0x73F;
	break;
     }
   //if ((FCR31 >> 7) & 0x1F) printf("FPU Exception enabled : %x\n",
//				   (int)((FCR31 >> 7) & 0x1F));
   interp_addr+=4;
}

static void BC()
{
   interp_cop1_bc[(op >> 16) & 3]();
}

static void S()
{
   interp_cop1_s[(op & 0x3F)]();
}

static void D()
{
   interp_cop1_d[(op & 0x3F)]();
}

static void W()
{
   interp_cop1_w[(op & 0x3F)]();
}

static void L()
{
   interp_cop1_l[(op & 0x3F)]();
}

static void (*interp_cop1[32])(void) =
{
   MFC1, DMFC1, CFC1, NI, MTC1, DMTC1, CTC1, NI,
   BC  , NI   , NI  , NI, NI  , NI   , NI  , NI,
   S   , D    , NI  , NI, W   , L    , NI  , NI,
   NI  , NI   , NI  , NI, NI  , NI   , NI  , NI
};

static void SPECIAL()
{
   interp_special[(op & 0x3F)]();
}

static void REGIMM()
{
   interp_regimm[((op >> 16) & 0x1F)]();
}

static void J()
{
   unsigned long naddr = (jinst_index<<2) | (interp_addr & 0xF0000000);
   if (naddr == interp_addr)
     {
	if (probe_nop(interp_addr+4))
	  {
	     update_count();
	     skip = next_interupt - Count;
	     if (skip > 3)
	       {
		  Count += (skip & 0xFFFFFFFC);
		  return;
	       }
	  }
     }
   interp_addr+=4;
   delay_slot=1;
   DC_INTERP_PREFETCH();
   INTERP_EXECUTE();
   update_count();
   delay_slot=0;
   interp_addr = naddr;
   last_addr = interp_addr;
   if (next_interupt <= Count) gen_interupt();
}

static void JAL()
{
   unsigned long naddr = (jinst_index<<2) | (interp_addr & 0xF0000000);
   if (naddr == interp_addr)
     {
	if (probe_nop(interp_addr+4))
	  {
	     update_count();
	     skip = next_interupt - Count;
	     if (skip > 3)
	       {
		  Count += (skip & 0xFFFFFFFC);
		  return;
	       }
	  }
     }
   interp_addr+=4;
   delay_slot=1;
   DC_INTERP_PREFETCH();
   INTERP_EXECUTE();
   update_count();
   delay_slot=0;
   if (!skip_jump)
     {
	reg[31]=interp_addr;
	sign_extended(reg[31]);

	interp_addr = naddr;
     }
   last_addr = interp_addr;
   if (next_interupt <= Count) gen_interupt();
}

static void BEQ()
{
   short local_immediate = iimmediate;
   local_rs = irs;
   local_rt = irt;
   if ((interp_addr + (local_immediate+1)*4) == interp_addr)
     if (local_rs == local_rt)
       {
	  if (probe_nop(interp_addr+4))
	    {
	       update_count();
	       skip = next_interupt - Count;
	       if (skip > 3)
		 {
		    Count += (skip & 0xFFFFFFFC);
		    return;
		 }
	    }
       }
   interp_addr+=4;
   delay_slot=1;
   DC_INTERP_PREFETCH();
   INTERP_EXECUTE();
   update_count();
   delay_slot=0;
   if (local_rs == local_rt)
     interp_addr += (local_immediate-1)*4;
   last_addr = interp_addr;
   if (next_interupt <= Count) gen_interupt();
}

static void BNE()
{
   short local_immediate = iimmediate;
   local_rs = irs;
   local_rt = irt;
   if ((interp_addr + (local_immediate+1)*4) == interp_addr)
     if (local_rs != local_rt)
       {
	  if (probe_nop(interp_addr+4))
	    {
	       update_count();
	       skip = next_interupt - Count;
	       if (skip > 3)
		 {
		    Count += (skip & 0xFFFFFFFC);
		    return;
		 }
	    }
       }
   interp_addr+=4;
   delay_slot=1;
   DC_INTERP_PREFETCH();
   INTERP_EXECUTE();
   update_count();
   delay_slot=0;
   if (local_rs != local_rt)
     interp_addr += (local_immediate-1)*4;
   last_addr = interp_addr;
   if (next_interupt <= Count) gen_interupt();
}

static void BLEZ()
{
   short local_immediate = iimmediate;
   local_rs = irs;
   if ((interp_addr + (local_immediate+1)*4) == interp_addr)
     if (local_rs <= 0)
       {
	  if (probe_nop(interp_addr+4))
	    {
	       update_count();
	       skip = next_interupt - Count;
	       if (skip > 3)
		 {
		    Count += (skip & 0xFFFFFFFC);
		    return;
		 }
	    }
       }
   interp_addr+=4;
   delay_slot=1;
   DC_INTERP_PREFETCH();
   INTERP_EXECUTE();
   update_count();
   delay_slot=0;
   if (local_rs <= 0)
     interp_addr += (local_immediate-1)*4;
   last_addr = interp_addr;
   if (next_interupt <= Count) gen_interupt();
}

static void BGTZ()
{
   short local_immediate = iimmediate;
   local_rs = irs;
   if ((interp_addr + (local_immediate+1)*4) == interp_addr)
     if (local_rs > 0)
       {
	  if (probe_nop(interp_addr+4))
	    {
	       update_count();
	       skip = next_interupt - Count;
	       if (skip > 3)
		 {
		    Count += (skip & 0xFFFFFFFC);
		    return;
		 }
	    }
       }
   interp_addr+=4;
   delay_slot=1;
   DC_INTERP_PREFETCH();
   INTERP_EXECUTE();
   update_count();
   delay_slot=0;
   if (local_rs > 0)
     interp_addr += (local_immediate-1)*4;
   last_addr = interp_addr;
   if (next_interupt <= Count) gen_interupt();
}

static void ADDI()
{
   irt32 = irs32 + iimmediate;
   sign_extended(irt);
   interp_addr+=4;
}

static void ADDIU()
{
   irt32 = irs32 + iimmediate;
   sign_extended(irt);
   interp_addr+=4;
}

static void SLTI()
{
   if (irs < iimmediate) irt = 1;
   else irt = 0;
   interp_addr+=4;
}

static void SLTIU()
{
   if ((unsigned long long)irs < (unsigned long long)((long long)iimmediate))
     irt = 1;
   else irt = 0;
   interp_addr+=4;
}

static void ANDI()
{
   irt = irs & (unsigned short)iimmediate;
   interp_addr+=4;
}

static void ORI()
{
   irt = irs | (unsigned short)iimmediate;
   interp_addr+=4;
}

static void XORI()
{
   irt = irs ^ (unsigned short)iimmediate;
   interp_addr+=4;
}

static void LUI()
{
   irt32 = iimmediate << 16;
   sign_extended(irt);
   interp_addr+=4;
}

static void COP0()
{
   interp_cop0[((op >> 21) & 0x1F)]();
}

static void COP1()
{
   if (check_cop1_unusable()) return;
   start_section(FP_SECTION);
   interp_cop1[((op >> 21) & 0x1F)]();
   end_section(FP_SECTION);
}

static void BEQL()
{
   short local_immediate = iimmediate;
   local_rs = irs;
   local_rt = irt;
   if ((interp_addr + (local_immediate+1)*4) == interp_addr)
     if (irs == irt)
       {
	  if (probe_nop(interp_addr+4))
	    {
	       update_count();
	       skip = next_interupt - Count;
	       if (skip > 3)
		 {
		    Count += (skip & 0xFFFFFFFC);
		    return;
		 }
	    }
       }
   if (local_rs == local_rt)
     {
	interp_addr+=4;
	delay_slot=1;
	DC_INTERP_PREFETCH();
	INTERP_EXECUTE();
	update_count();
	delay_slot=0;
	interp_addr += (local_immediate-1)*4;
     }
   else
     {
	interp_addr+=8;
	update_count();
     }
   last_addr = interp_addr;
   if (next_interupt <= Count) gen_interupt();
}

static void BNEL()
{
   short local_immediate = iimmediate;
   local_rs = irs;
   local_rt = irt;
   if ((interp_addr + (local_immediate+1)*4) == interp_addr)
     if (irs != irt)
       {
	  if (probe_nop(interp_addr+4))
	    {
	       update_count();
	       skip = next_interupt - Count;
	       if (skip > 3)
		 {
		    Count += (skip & 0xFFFFFFFC);
		    return;
		 }
	    }
       }
   if (local_rs != local_rt)
     {
	interp_addr+=4;
	delay_slot=1;
	DC_INTERP_PREFETCH();
	INTERP_EXECUTE();
	update_count();
	delay_slot=0;
	interp_addr += (local_immediate-1)*4;
     }
   else
     {
	interp_addr+=8;
	update_count();
     }
   last_addr = interp_addr;
   if (next_interupt <= Count) gen_interupt();
}

static void BLEZL()
{
   short local_immediate = iimmediate;
   local_rs = irs;
   if ((interp_addr + (local_immediate+1)*4) == interp_addr)
     if (irs <= 0)
       {
	  if (probe_nop(interp_addr+4))
	    {
	       update_count();
	       skip = next_interupt - Count;
	       if (skip > 3)
		 {
		    Count += (skip & 0xFFFFFFFC);
		    return;
		 }
	    }
       }
   if (local_rs <= 0)
     {
	interp_addr+=4;
	delay_slot=1;
	DC_INTERP_PREFETCH();
	INTERP_EXECUTE();
	update_count();
	delay_slot=0;
	interp_addr += (local_immediate-1)*4;
     }
   else
     {
	interp_addr+=8;
	update_count();
     }
   last_addr = interp_addr;
   if (next_interupt <= Count) gen_interupt();
}

static void BGTZL()
{
   short local_immediate = iimmediate;
   local_rs = irs;
   if ((interp_addr + (local_immediate+1)*4) == interp_addr)
     if (irs > 0)
       {
	  if (probe_nop(interp_addr+4))
	    {
	       update_count();
	       skip = next_interupt - Count;
	       if (skip > 3)
		 {
		    Count += (skip & 0xFFFFFFFC);
		    return;
		 }
	    }
       }
   if (local_rs > 0)
     {
	interp_addr+=4;
	delay_slot=1;
	DC_INTERP_PREFETCH();
	INTERP_EXECUTE();
	update_count();
	delay_slot=0;
	interp_addr += (local_immediate-1)*4;
     }
   else
     {
	interp_addr+=8;
	update_count();
     }
   last_addr = interp_addr;
   if (next_interupt <= Count) gen_interupt();
}

static void DADDI()
{
   irt = irs + iimmediate;
   interp_addr+=4;
}

static void DADDIU()
{
   irt = irs + iimmediate;
   interp_addr+=4;
}

static void LDL()
{
   interp_addr+=4;
   switch ((iimmediate + irs32) & 7)
     {
      case 0:
	address = iimmediate + irs32;
	read_dword_in_memory();
	if (!address) return;
	irt = dword;
	break;
      case 1:
	address = (iimmediate + irs32) & 0xFFFFFFF8;
	read_dword_in_memory();
	if (!address) return;
	irt = (irt & 0xFF) | (dword << 8);
	break;
      case 2:
	address = (iimmediate + irs32) & 0xFFFFFFF8;
	read_dword_in_memory();
	if (!address) return;
	irt = (irt & 0xFFFF) | (dword << 16);
	break;
      case 3:
	address = (iimmediate + irs32) & 0xFFFFFFF8;
	read_dword_in_memory();
	if (!address) return;
	irt = (irt & 0xFFFFFF) | (dword << 24);
	break;
      case 4:
	address = (iimmediate + irs32) & 0xFFFFFFF8;
	read_dword_in_memory();
	if (!address) return;
	irt = (irt & 0xFFFFFFFF) | (dword << 32);
	break;
      case 5:
	address = (iimmediate + irs32) & 0xFFFFFFF8;
	read_dword_in_memory();
	if (!address) return;
	irt = (irt & 0xFFFFFFFFFFLL) | (dword << 40);
	break;
      case 6:
	address = (iimmediate + irs32) & 0xFFFFFFF8;
	read_dword_in_memory();
	if (!address) return;
	irt = (irt & 0xFFFFFFFFFFFFLL) | (dword << 48);
	break;
      case 7:
	address = (iimmediate + irs32) & 0xFFFFFFF8;
	read_dword_in_memory();
	if (!address) return;
	irt = (irt & 0xFFFFFFFFFFFFFFLL) | (dword << 56);
	break;
     }
}

static void LDR()
{
   interp_addr+=4;
   switch ((iimmediate + irs32) & 7)
     {
      case 0:
	address = (iimmediate + irs32) & 0xFFFFFFF8;
	read_dword_in_memory();
	if (!address) return;
	irt = (irt & 0xFFFFFFFFFFFFFF00LL) | (dword >> 56);
	break;
      case 1:
	address = (iimmediate + irs32) & 0xFFFFFFF8;
	read_dword_in_memory();
	if (!address) return;
	irt = (irt & 0xFFFFFFFFFFFF0000LL) | (dword >> 48);
	break;
      case 2:
	address = (iimmediate + irs32) & 0xFFFFFFF8;
	read_dword_in_memory();
	if (!address) return;
	irt = (irt & 0xFFFFFFFFFF000000LL) | (dword >> 40);
	break;
      case 3:
	address = (iimmediate + irs32) & 0xFFFFFFF8;
	read_dword_in_memory();
	if (!address) return;
	irt = (irt & 0xFFFFFFFF00000000LL) | (dword >> 32);
	break;
      case 4:
	address = (iimmediate + irs32) & 0xFFFFFFF8;
	read_dword_in_memory();
	if (!address) return;
	irt = (irt & 0xFFFFFF0000000000LL) | (dword >> 24);
	break;
      case 5:
	address = (iimmediate + irs32) & 0xFFFFFFF8;
	read_dword_in_memory();
	if (!address) return;
	irt = (irt & 0xFFFF000000000000LL) | (dword >> 16);
	break;
      case 6:
	address = (iimmediate + irs32) & 0xFFFFFFF8;
	read_dword_in_memory();
	if (!address) return;
	irt = (irt & 0xFF00000000000000LL) | (dword >> 8);
	break;
      case 7:
	address = (iimmediate + irs32) & 0xFFFFFFF8;
	read_dword_in_memory();
	if (!address) return;
	irt = dword;
	break;
     }
}

/* Main-loop memory labels need an inline body to avoid the SH4 call and
 * register/FPSCR setup on ordinary RAM accesses. Addressable opcode-table
 * copies remain available for delay slots and generic execution. */
#ifdef DC_INTERP_ALU
#define DC_INTERP_MEMORY_OP static inline __attribute__((always_inline)) void
#else
#define DC_INTERP_MEMORY_OP static void
#endif

DC_INTERP_MEMORY_OP LB()
{
   interp_addr+=4;
   address = iimmediate + irs32;
   read_byte_in_memory();
   if (!address) return;
   irt = byte;
   sign_extendedb(irt);
}

DC_INTERP_MEMORY_OP LH()
{
   interp_addr+=4;
   address = iimmediate + irs32;
   read_hword_in_memory();
   if (!address) return;
   irt = hword;
   sign_extendedh(irt);
}

static void LWL()
{
   interp_addr+=4;
   switch ((iimmediate + irs32) & 3)
     {
      case 0:
	address = iimmediate + irs32;
	read_word_in_memory();
	if (!address) return;
	irt = word;
	break;
      case 1:
	address = (iimmediate + irs32) & 0xFFFFFFFC;
	read_word_in_memory();
	if (!address) return;
	irt = (irt & 0xFF) | (word << 8);
	break;
      case 2:
	address = (iimmediate + irs32) & 0xFFFFFFFC;
	read_word_in_memory();
	if (!address) return;
	irt = (irt & 0xFFFF) | (word << 16);
	break;
      case 3:
	address = (iimmediate + irs32) & 0xFFFFFFFC;
	read_word_in_memory();
	if (!address) return;
	irt = (irt & 0xFFFFFF) | (word << 24);
	break;
     }
   sign_extended(irt);
}

DC_INTERP_MEMORY_OP LW()
{
   address = iimmediate + irs32;
   interp_addr+=4;
   read_word_in_memory();
   if (!address) return;
   irt = word;
   sign_extended(irt);
}

DC_INTERP_MEMORY_OP LBU()
{
   interp_addr+=4;
   address = iimmediate + irs32;
   read_byte_in_memory();
   if (!address) return;
   irt = byte;
}

DC_INTERP_MEMORY_OP LHU()
{
   interp_addr+=4;
   address = iimmediate + irs32;
   read_hword_in_memory();
   if (!address) return;
   irt = hword;
}

static void LWR()
{
   interp_addr+=4;
   switch ((iimmediate + irs32) & 3)
     {
      case 0:
	address = (iimmediate + irs32) & 0xFFFFFFFC;
	read_word_in_memory();
	if (!address) return;
	irt = (irt & 0xFFFFFFFFFFFFFF00LL) | ((word >> 24) & 0xFF);
	break;
      case 1:
	address = (iimmediate + irs32) & 0xFFFFFFFC;
	read_word_in_memory();
	if (!address) return;
	irt = (irt & 0xFFFFFFFFFFFF0000LL) | ((word >> 16) & 0xFFFF);
	break;
      case 2:
	address = (iimmediate + irs32) & 0xFFFFFFFC;
	read_word_in_memory();
	if (!address) return;
	irt = (irt & 0xFFFFFFFFFF000000LL) | ((word >> 8) & 0xFFFFFF);
	break;
      case 3:
	address = (iimmediate + irs32) & 0xFFFFFFFC;
	read_word_in_memory();
	if (!address) return;
	irt = word;
	sign_extended(irt);
     }
}

static void LWU()
{
   address = iimmediate + irs32;
   interp_addr+=4;
   read_word_in_memory();
   if (!address) return;
   irt = word;
}

DC_INTERP_MEMORY_OP SB()
{
   interp_addr+=4;
   address = iimmediate + irs32;
   byte = (unsigned char)(irt & 0xFF);
   write_byte_in_memory();
   check_memory();
}

DC_INTERP_MEMORY_OP SH()
{
   interp_addr+=4;
   address = iimmediate + irs32;
   hword = (unsigned short)(irt & 0xFFFF);
   write_hword_in_memory();
   check_memory();
}

static void SWL()
{
   interp_addr+=4;
   switch ((iimmediate + irs32) & 3)
     {
      case 0:
	address = (iimmediate + irs32) & 0xFFFFFFFC;
	word = (unsigned long)irt;
	write_word_in_memory();
	check_memory();
	break;
      case 1:
	address = (iimmediate + irs32) & 0xFFFFFFFC;
	read_word_in_memory();
	word = ((unsigned long)irt >> 8) | (word & 0xFF000000);
	write_word_in_memory();
	check_memory();
	break;
      case 2:
	address = (iimmediate + irs32) & 0xFFFFFFFC;
	read_word_in_memory();
	word = ((unsigned long)irt >> 16) | (word & 0xFFFF0000);
	write_word_in_memory();
	check_memory();
	break;
      case 3:
	address = iimmediate + irs32;
	byte = (unsigned char)(irt >> 24);
	write_byte_in_memory();
	check_memory();
	break;
     }
}

DC_INTERP_MEMORY_OP SW()
{
   interp_addr+=4;
   address = iimmediate + irs32;
   word = (unsigned long)(irt & 0xFFFFFFFF);
   write_word_in_memory();
   check_memory();
}

static void SDL()
{
   interp_addr+=4;
   switch ((iimmediate + irs32) & 7)
     {
      case 0:
	address = (iimmediate + irs32) & 0xFFFFFFF8;
	dword = irt;
	write_dword_in_memory();
	check_memory();
	break;
      case 1:
	address = (iimmediate + irs32) & 0xFFFFFFF8;
	read_dword_in_memory();
	dword = ((unsigned long long)irt >> 8)|(dword & 0xFF00000000000000LL);
	write_dword_in_memory();
	check_memory();
	break;
      case 2:
	address = (iimmediate + irs32) & 0xFFFFFFF8;
	read_dword_in_memory();
	dword = ((unsigned long long)irt >> 16)|(dword & 0xFFFF000000000000LL);
	write_dword_in_memory();
	check_memory();
	break;
      case 3:
	address = (iimmediate + irs32) & 0xFFFFFFF8;
	read_dword_in_memory();
	dword = ((unsigned long long)irt >> 24)|(dword & 0xFFFFFF0000000000LL);
	write_dword_in_memory();
	check_memory();
	break;
      case 4:
	address = (iimmediate + irs32) & 0xFFFFFFF8;
	read_dword_in_memory();
	dword = ((unsigned long long)irt >> 32)|(dword & 0xFFFFFFFF00000000LL);
	write_dword_in_memory();
	check_memory();
	break;
      case 5:
	address = (iimmediate + irs32) & 0xFFFFFFF8;
	read_dword_in_memory();
	dword = ((unsigned long long)irt >> 40)|(dword & 0xFFFFFFFFFF000000LL);
	write_dword_in_memory();
	check_memory();
	break;
      case 6:
	address = (iimmediate + irs32) & 0xFFFFFFF8;
	read_dword_in_memory();
	dword = ((unsigned long long)irt >> 48)|(dword & 0xFFFFFFFFFFFF0000LL);
	write_dword_in_memory();
	check_memory();
	break;
      case 7:
	address = (iimmediate + irs32) & 0xFFFFFFF8;
	read_dword_in_memory();
	dword = ((unsigned long long)irt >> 56)|(dword & 0xFFFFFFFFFFFFFF00LL);
	write_dword_in_memory();
	check_memory();
	break;
     }
}

static void SDR()
{
   interp_addr+=4;
   switch ((iimmediate + irs32) & 7)
     {
      case 0:
	address = iimmediate + irs32;
	read_dword_in_memory();
	dword = (irt << 56) | (dword & 0x00FFFFFFFFFFFFFFLL);
	write_dword_in_memory();
	check_memory();
	break;
      case 1:
	address = (iimmediate + irs32) & 0xFFFFFFF8;
	read_dword_in_memory();
	dword = (irt << 48) | (dword & 0x0000FFFFFFFFFFFFLL);
	write_dword_in_memory();
	check_memory();
	break;
      case 2:
	address = (iimmediate + irs32) & 0xFFFFFFF8;
	read_dword_in_memory();
	dword = (irt << 40) | (dword & 0x000000FFFFFFFFFFLL);
	write_dword_in_memory();
	check_memory();
	break;
      case 3:
	address = (iimmediate + irs32) & 0xFFFFFFF8;
	read_dword_in_memory();
	dword = (irt << 32) | (dword & 0x00000000FFFFFFFFLL);
	write_dword_in_memory();
	check_memory();
	break;
      case 4:
	address = (iimmediate + irs32) & 0xFFFFFFF8;
	read_dword_in_memory();
	dword = (irt << 24) | (dword & 0x0000000000FFFFFFLL);
	write_dword_in_memory();
	check_memory();
	break;
      case 5:
	address = (iimmediate + irs32) & 0xFFFFFFF8;
	read_dword_in_memory();
	dword = (irt << 16) | (dword & 0x000000000000FFFFLL);
	write_dword_in_memory();
	check_memory();
	break;
      case 6:
	address = (iimmediate + irs32) & 0xFFFFFFF8;
	read_dword_in_memory();
	dword = (irt << 8) | (dword & 0x00000000000000FFLL);
	write_dword_in_memory();
	check_memory();
	break;
      case 7:
	address = (iimmediate + irs32) & 0xFFFFFFF8;
	dword = irt;
	write_dword_in_memory();
	check_memory();
	break;
     }
}

static void SWR()
{
   interp_addr+=4;
   switch ((iimmediate + irs32) & 3)
     {
      case 0:
	address = iimmediate + irs32;
	read_word_in_memory();
	word = ((unsigned long)irt << 24) | (word & 0x00FFFFFF);
	write_word_in_memory();
	check_memory();
	break;
      case 1:
	address = (iimmediate + irs32) & 0xFFFFFFFC;
	read_word_in_memory();
	word = ((unsigned long)irt << 16) | (word & 0x0000FFFF);
	write_word_in_memory();
	check_memory();
	break;
      case 2:
	address = (iimmediate + irs32) & 0xFFFFFFFC;
	read_word_in_memory();
	word = ((unsigned long)irt << 8) | (word & 0x000000FF);
	write_word_in_memory();
	check_memory();
	break;
      case 3:
	address = (iimmediate + irs32) & 0xFFFFFFFC;
	word = (unsigned long)irt;
	write_word_in_memory();
	check_memory();
	break;
     }
}

static void CACHE()
{
   interp_addr+=4;
}

static void LL()
{
   address = iimmediate + irs32;
   interp_addr+=4;
   read_word_in_memory();
   if (!address) return;
   irt = word;
   sign_extended(irt);
   llbit = 1;
}

static void LWC1()
{
   if (check_cop1_unusable()) return;
   interp_addr+=4;
   address = lfoffset+reg[lfbase];
   read_word_in_memory();
   if (!address) return;
   *((long*)reg_cop1_simple[lfft]) = word;
}

static void LDC1()
{
   if (check_cop1_unusable()) return;
   interp_addr+=4;
   address = lfoffset+reg[lfbase];
   read_dword_in_memory();
   if (!address) return;
   *((long long*)reg_cop1_double[lfft]) = dword;
}

static void LD()
{
   interp_addr+=4;
   address = iimmediate + irs32;
   read_dword_in_memory();
   if (!address) return;
   irt = dword;
}

static void SC()
{
   interp_addr+=4;
   if(llbit)
     {
	address = iimmediate + irs32;
	word = (unsigned long)(irt & 0xFFFFFFFF);
	write_word_in_memory();
	check_memory();
	llbit = 0;
	irt = 1;
     }
   else
     {
	irt = 0;
     }
}

static void SWC1()
{
   if (check_cop1_unusable()) return;
   interp_addr+=4;
   address = lfoffset+reg[lfbase];
   word = *((long*)reg_cop1_simple[lfft]);
   write_word_in_memory();
   check_memory();
}

static void SDC1()
{
   if (check_cop1_unusable()) return;
   interp_addr+=4;
   address = lfoffset+reg[lfbase];
   dword = *((unsigned long long*)reg_cop1_double[lfft]);
   write_dword_in_memory();
   check_memory();
}

static void SD()
{
   interp_addr+=4;
   address = iimmediate + irs32;
   dword = irt;
   write_dword_in_memory();
   check_memory();
}

/*static*/ void (*interp_ops[64])(void) =
{
   SPECIAL, REGIMM, J   , JAL  , BEQ , BNE , BLEZ , BGTZ ,
   ADDI   , ADDIU , SLTI, SLTIU, ANDI, ORI , XORI , LUI  ,
   COP0   , COP1  , NI  , NI   , BEQL, BNEL, BLEZL, BGTZL,
   DADDI  , DADDIU, LDL , LDR  , NI  , NI  , NI   , NI   ,
   LB     , LH    , LWL , LW   , LBU , LHU , LWR  , LWU  ,
   SB     , SH    , SWL , SW   , SDL , SDR , SWR  , CACHE,
   LL     , LWC1  , NI  , NI   , NI  , LDC1, NI   , LD   ,
   SC     , SWC1  , NI  , NI   , NI  , SDC1, NI   , SD
};

void prefetch()
{
   //static FILE *f = NULL;
   //static int line=1;
   //static int tlb_used = 0;
   //unsigned int comp;
   //if (!tlb_used)
     //{
	/*if (f==NULL) f = fopen("/mnt/windows/pcdeb.txt", "rb");
	fscanf(f, "%x", &comp);
	if (comp != interp_addr)
	  {
	     printf("diff@%x, line:%d\n", interp_addr, line);
	     stop=1;
	  }*/
	//line++;
	//if ((debug_count+Count) > 0x50fe000) printf("line:%d\n", line);
	/*if ((debug_count+Count) > 0xb70000)
	  printf("count:%x, add:%x, op:%x, l%d\n", (int)(Count+debug_count),
		 interp_addr, op, line);*/
     //}
   //printf("addr:%x\n", interp_addr);

   // --- Trying to track down a bug ---
   /*static disturbed = 0;
   if(reg[31] == 0x100000 && !disturbed){
   	disturbed = 1;
   	printf("LR disturbed at last address %08x\n", (int)last_addr);
	int i;
	for(i=-8; i<4; ++i)
		printf(" %08x%s",
	   	       rdram[(last_addr&0xFFFFFF)/4+i],
	   	       ((i+8)%4 == 3) ? "\n" : "");
   }
   static pi_reg_value;
   if(pi_register.pi_dram_addr_reg != pi_reg_value){
   	pi_reg_value = pi_register.pi_dram_addr_reg;
   	printf("PI DRAM addr reg changed to %08x\n at last_addr %08x, op: %08x, count: %u\n",
   	       pi_reg_value, (int)last_addr, rdram[(last_addr&0xFFFFFF)/4], (unsigned int)debug_count+Count);
   }

   if(!((debug_count + Count) % 100000)) printf("%u instructions executed\n",
                                             (unsigned int)(debug_count + Count));*/
   // --- OK ---

   unsigned long *mem;
#ifdef DC_INTERP_MEMORY
   /* KSEG0/KSEG1 first-RDRAM fetch needs neither translation nor ROM lookup.
    * Match fast_mem_access's word alignment; other addresses use its checks. */
   if ((interp_addr & 0xdfc00000ul) == 0x80000000ul)
      mem = (unsigned long *)(rdramb + (interp_addr & 0x3ffffcul));
   else
#endif
      mem = fast_mem_access(interp_addr);
   if (mem != NULL)
     {
	op = *mem;
#ifdef DC_INTERP_DIRECT
        PC->addr = interp_addr;
#else
        prefetch_opcode(op);
#endif
     }
   else
     {
	printf("execution &#65533; l'addresse :%x\n", (int)interp_addr);
	stop=1;
#ifdef DEBUGON
	_break();
#endif
     }
}

#ifdef DC_INTERP_DELAY_ALU
static inline void dc_interp_execute(void)
{
   // Publish/fetch still happens at the original delay-slot boundary. These
   // are the same handlers as the main loop; device/fault paths stay intact.
   // The non-pure mode provides the original table dispatch for the oracle.
#ifdef DC_INTERP_ORACLE
   if(dc_interp_oracle){interp_ops[(op>>26)&63]();return;}
#endif
   if(!interpcore){interp_ops[(op>>26)&63]();return;}
   switch((op>>26)&63) {
   case 0:
      switch(op&63) {
      case 0:SLL();return;case 33:ADDU();return;case 37:OR();return;
      default:SPECIAL();return;
      }
   case 9:ADDIU();return;case 12:ANDI();return;case 13:ORI();return;case 15:LUI();return;
   case 32:LB();return;case 33:LH();return;case 35:LW();return;
   case 36:LBU();return;case 37:LHU();return;
   case 40:SB();return;case 41:SH();return;case 43:SW();return;
   default:interp_ops[(op>>26)&63]();return;
   }
}
#endif

#ifdef DC_INTERP_BRANCH_PAIR
#if defined(DBG) || defined(COMPARE_CORE)
#error "BRANCH_PAIR is not compatible with per-instruction observers"
#endif
static inline __attribute__((always_inline)) int dc_interp_branch_pair(unsigned primary)
{
#ifdef DC_INTERP_ORACLE
   if(dc_interp_oracle)return 0;
#endif
   unsigned long branch=interp_addr;
   short displacement=iimmediate;
   // Self-target branches retain the original idle-loop/count shortcut. No
   // effects precede qualification, so every other rejected case can fall back.
   if(delay_slot || displacement==-1 || (branch&3) ||
      (branch&0xdfc00000ul)!=0x80000000ul)return 0;
   long long lhs=irs,rhs=irt;
   int taken=(lhs==rhs) ^ (primary&1);
   int annul=(primary&16) && !taken;
   unsigned long slot=0;
   if(!annul) {
      unsigned offset=branch&0x3ffffc;
      if(offset>sizeof(rdram)-8)return 0;
      slot=rdram[offset/4+1];
      unsigned kind=slot>>26;
      if(!((kind==0 && ((slot&63)==0 || (slot&63)==33 || (slot&63)==37)) ||
           kind==9 || kind==12 || kind==13 || kind==15))return 0;
   }
   local_rs=lhs;local_rt=rhs;PC->addr=branch;
   if(annul) {
      interp_addr=branch+8;update_count();
   } else {
      interp_addr=branch+4;delay_slot=1;op=slot;PC->addr=interp_addr;
      // These original bodies cannot call out, fault or alter the branch
      // operands saved above. Loads/stores and other delay slots stay original.
      switch(slot>>26) {
      case 0:
         if((slot&63)==0)SLL();else if((slot&63)==33)ADDU();else OR();
         break;
      case 9: ADDIU();break;
      case 12: ANDI();break;
      case 13: ORI();break;
      case 15: LUI();break;
      }
      update_count();delay_slot=0;
      if(taken)interp_addr+=(displacement-1)*4;
   }
   last_addr=interp_addr;
   if(next_interupt<=Count)gen_interupt();
   return 1;
}
#endif

#ifdef DC_INTERP_LOCAL
#include "../platform/dc_interp_local.h"
#endif
#ifdef DC_INTERP_THREAD
extern void dc_interp_run_thread(void);
unsigned long dc_interp_thread_fallback(unsigned long current_pc)
{
   interp_addr=current_pc;
   prefetch();
   if(!stop)INTERP_EXECUTE();
   return interp_addr;
}
#endif

void pure_interpreter()
{
   //interp_addr = 0xa4000040;
   stop=0;
#ifdef __DREAMCAST__
   dc_interp_steps = 0;
#ifdef DC_INTERP_BLOCK
   dc_interp_block_reset();
#endif
#ifdef DC_CPU_PROFILE
   memset(profile_ops,0,sizeof(profile_ops));
   memset(profile_special,0,sizeof(profile_special));
   memset(profile_fp,0,sizeof(profile_fp));
   memset(profile_pages,0,sizeof(profile_pages));
   memset(profile_pcs,0,sizeof(profile_pcs));
   profile_total=0;
#ifdef DC_CPU_TRACE_HOST
   memset(profile_exact,0,sizeof(profile_exact));profile_other=0;
#endif
#endif
#endif
   PC = malloc(sizeof(precomp_instr));
   if (!PC) {
      stop = 1;
      return;
   }
   last_addr = interp_addr;
#ifdef DC_INTERP_BLOCK_EPOCH
   dc_interp_block_enter();
#endif
#ifdef DC_INTERP_BLOCK_FRONTIER
   int native_frontier=1;
#endif
#ifdef DC_INTERP_LOCAL
   dc_interp_run_local();
#elif defined(DC_INTERP_THREAD)
   dc_interp_run_thread();
#else
#ifdef DC_INTERP_ALU
   /* Keep common ALU and memory operations in the loop; branches and devices
    * retain their original handlers and delay-slot/interrupt boundaries. */
#ifdef DC_INTERP_EXPAND
#define DC_ALU_COP1 &&dc_alu_cop1
#define DC_ALU_XORI &&dc_alu_xori
#else
#define DC_ALU_COP1 &&dc_alu_generic
#define DC_ALU_XORI &&dc_alu_generic
#endif
#ifdef DC_INTERP_BRANCH_PAIR
#define DC_ALU_BRANCH &&dc_alu_branch
#else
#define DC_ALU_BRANCH &&dc_alu_generic
#endif
   static const void *alu_dispatch[64] = {
      &&dc_alu_special,&&dc_alu_generic,&&dc_alu_generic,&&dc_alu_generic,DC_ALU_BRANCH,DC_ALU_BRANCH,&&dc_alu_generic,&&dc_alu_generic,
      &&dc_alu_generic,&&dc_alu_addiu,&&dc_alu_generic,&&dc_alu_generic,&&dc_alu_andi,&&dc_alu_ori,DC_ALU_XORI,&&dc_alu_lui,
      &&dc_alu_generic,DC_ALU_COP1,&&dc_alu_generic,&&dc_alu_generic,DC_ALU_BRANCH,DC_ALU_BRANCH,&&dc_alu_generic,&&dc_alu_generic,
      &&dc_alu_generic,&&dc_alu_generic,&&dc_alu_generic,&&dc_alu_generic,&&dc_alu_generic,&&dc_alu_generic,&&dc_alu_generic,&&dc_alu_generic,
      &&dc_alu_lb,&&dc_alu_lh,&&dc_alu_generic,&&dc_alu_lw,&&dc_alu_lbu,&&dc_alu_lhu,&&dc_alu_generic,&&dc_alu_generic,
      &&dc_alu_sb,&&dc_alu_sh,&&dc_alu_generic,&&dc_alu_sw,&&dc_alu_generic,&&dc_alu_generic,&&dc_alu_generic,&&dc_alu_generic,
      &&dc_alu_generic,&&dc_alu_generic,&&dc_alu_generic,&&dc_alu_generic,&&dc_alu_generic,&&dc_alu_generic,&&dc_alu_generic,&&dc_alu_generic,
      &&dc_alu_generic,&&dc_alu_generic,&&dc_alu_generic,&&dc_alu_generic,&&dc_alu_generic,&&dc_alu_generic,&&dc_alu_generic,&&dc_alu_generic,
   };
#endif
   while (!stop)
     {
#ifdef __DREAMCAST__
	if (dc_interp_step_limit && ++dc_interp_steps >= dc_interp_step_limit)
	  {
	     stop = 1;
	     break;
	  }
#endif
#if defined(DC_INTERP_BLOCK) && !defined(DC_INTERP_BLOCK_DISPATCH)
#ifdef DC_INTERP_BLOCK_FRONTIER
        // Attempt once at an original-handler boundary. Unsupported starts
        // and short suffixes must not pay native cache lookup on every ALU.
        if(native_frontier) {
            native_frontier=0;
#endif
        unsigned block_limit=16;
        if(dc_interp_step_limit && dc_interp_step_limit-dc_interp_steps<block_limit)
            block_limit=dc_interp_step_limit-dc_interp_steps;
        unsigned block_done=dc_interp_block_execute(interp_addr,block_limit);
        if(block_done) {
#ifndef DC_INTERP_PC_LAZY
            PC->addr=interp_addr+(block_done-1)*4;
#endif
            interp_addr+=block_done*4;
            if(dc_interp_step_limit)dc_interp_steps+=block_done-1;
            continue;
        }
#ifdef DC_INTERP_BLOCK_FRONTIER
        }
#endif
#endif
#if defined(DC_INTERP_ALU) && defined(DC_INTERP_MEMORY) && defined(DC_INTERP_DIRECT)
        /* Keep the ordinary RAM fetch in this loop, avoiding a call and an
         * opcode reload. Read each instruction afresh, including modified code. */
        if ((interp_addr & 0xdfc00000ul) == 0x80000000ul) {
           op=*(unsigned long *)(rdramb+(interp_addr & 0x3ffffcul));
#ifndef DC_INTERP_PC_LAZY
           PC->addr=interp_addr;
#endif
        } else prefetch();
#else
        prefetch();
#endif
	if (stop)
	  break;
#ifdef DC_INTERP_BLOCK_DISPATCH
        if(native_frontier) {
            native_frontier=0;
            unsigned offset=interp_addr&0x3ffffc;
            unsigned block_limit=16;
            if(dc_interp_step_limit && dc_interp_step_limit-dc_interp_steps<block_limit)
                block_limit=dc_interp_step_limit-dc_interp_steps;
            unsigned block_done=~0u;
#ifdef DC_INTERP_BLOCK_HOT_ENTRY
            block_done=dc_interp_block_hot_execute(interp_addr,block_limit,(unsigned*)reg,&op);
#endif
            if(block_done==~0u) {
                if(!(interp_addr&3) && (interp_addr&0xdfc00000u)==0x80000000u &&
                   offset<=sizeof(rdram)-8 && dc_interp_block_candidate(op) &&
                   dc_interp_block_candidate(rdram[offset/4+1]))
                    block_done=dc_interp_block_execute(interp_addr,block_limit);
            }
            if(block_done!=~0u && block_done) {
#ifndef DC_INTERP_PC_LAZY
                PC->addr=interp_addr+(block_done-1)*4;
#endif
                interp_addr+=block_done*4;
                if(dc_interp_step_limit)dc_interp_steps+=block_done-1;
                continue;
            }
        }
#endif
#ifdef COMPARE_CORE
	compare_core();
#endif
	//if(interp_addr == 0x80000194) _break();
	//if (Count > 0x2000000) printf("inter:%x,%x\n", interp_addr,op);
	//if ((Count+debug_count) > 0xabaa2c) stop=1;
#ifdef DC_INTERP_ALU
#ifdef DC_CPU_PROFILE
        profile_instruction();
#endif
        goto *alu_dispatch[(op >> 26) & 63];
      dc_alu_special:
        switch(op & 63) {
          case 0: SLL(); break;
          case 33: ADDU(); break;
          case 37: OR(); break;
#ifdef DC_INTERP_EXPAND
          case 2: SRL(); break;
          case 3: SRA(); break;
          case 4: SLLV(); break;
          case 6: SRLV(); break;
          case 7: SRAV(); break;
          case 16: MFHI(); break;
          case 18: MFLO(); break;
          case 35: SUBU(); break;
          case 36: AND(); break;
          case 38: XOR(); break;
          case 39: NOR(); break;
          case 42: SLT(); break;
          case 43: SLTU(); break;
#endif
          default:
#ifdef DC_INTERP_PC_LAZY
            PC->addr=interp_addr;
#endif
            SPECIAL();
#ifdef DC_INTERP_BLOCK_FRONTIER
            native_frontier=1;goto dc_alu_after;
#else
            break;
#endif
        }
        goto dc_alu_done;
      dc_alu_addiu: ADDIU(); goto dc_alu_done;
      dc_alu_andi: ANDI(); goto dc_alu_done;
      dc_alu_ori: ORI(); goto dc_alu_done;
      dc_alu_lui: LUI(); goto dc_alu_done;
      dc_alu_lb: LB(); goto dc_alu_done;
      dc_alu_lh: LH(); goto dc_alu_done;
      dc_alu_lw: LW(); goto dc_alu_done;
      dc_alu_lbu: LBU(); goto dc_alu_done;
      dc_alu_lhu: LHU(); goto dc_alu_done;
      dc_alu_sb: SB(); goto dc_alu_done;
      dc_alu_sh: SH(); goto dc_alu_done;
      dc_alu_sw: SW(); goto dc_alu_done;
#ifdef DC_INTERP_EXPAND
      dc_alu_xori: XORI(); goto dc_alu_done;
      dc_alu_cop1:
#ifdef DC_INTERP_PC_LAZY
        PC->addr=interp_addr;
#endif
        // Preserve the CU1 exception and FP profiling boundary, but dispatch
        // straight to the original format handler rather than COP1->S/D/etc.
        if(check_cop1_unusable())goto dc_alu_done;
        start_section(FP_SECTION);
        switch((op>>21)&31) {
          case 0: MFC1(); break;
          case 2: CFC1(); break;
          case 4: MTC1(); break;
          case 6: CTC1(); break;
          case 8: interp_cop1_bc[(op>>16)&3](); break;
          case 16: interp_cop1_s[op&63](); break;
          case 17: interp_cop1_d[op&63](); break;
          case 20: interp_cop1_w[op&63](); break;
          case 21: interp_cop1_l[op&63](); break;
          default: interp_cop1[(op>>21)&31](); break;
        }
        end_section(FP_SECTION);
        goto dc_alu_done;
#endif
#ifdef DC_INTERP_BRANCH_PAIR
      dc_alu_branch:
        if(dc_interp_branch_pair(op>>26))goto dc_alu_done;
#endif
      dc_alu_generic:
#ifdef DC_INTERP_PC_LAZY
        PC->addr=interp_addr;
#endif
        interp_ops[(op >> 26) & 63]();
#ifdef DC_INTERP_BLOCK_FRONTIER
        native_frontier=1;goto dc_alu_after;
#endif
      dc_alu_done: ;
#ifdef DC_INTERP_BLOCK_FRONTIER
        native_frontier=0;
      dc_alu_after: ;
#endif
#else
        INTERP_EXECUTE();
#endif

	//Count = (unsigned long)Count + 2;
	//if (interp_addr == 0x80000180) last_addr = interp_addr;
#ifdef DBG
	PC->addr = interp_addr;
	if (debugger_mode) update_debugger();
#endif
     }
#endif /* DC_INTERP_LOCAL / DC_INTERP_THREAD */
   PC->addr = interp_addr;
#ifdef DC_INTERP_BLOCK_EPOCH
   dc_interp_block_leave();
#endif
#ifdef __DREAMCAST__
   /* go() can be re-entered per ROM; do not leak a precomp_instr each time. */
   free(PC);
   PC = NULL;
#endif
}

void interprete_section(unsigned long addr)
{
   interp_addr = addr;
   PC = malloc(sizeof(precomp_instr));
   last_addr = interp_addr;
   while (!stop && (addr >> 12) == (interp_addr >> 12))
     {
	prefetch();
#ifdef COMPARE_CORE
	compare_core();
#endif
	PC->addr = interp_addr;
	INTERP_EXECUTE();
#ifdef DBG
	PC->addr = interp_addr;
	if (debugger_mode) update_debugger();
#endif
     }
   PC->addr = interp_addr;
}
