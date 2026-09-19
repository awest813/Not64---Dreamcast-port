#include "../r4300/r4300.h"
#include "../r4300/recomp.h"
#include "../r4300/recomph.h"

int fast_memory;

void dyna_jump(void) {}
void dyna_start(void) {}
void dyna_stop(void) {}
void stop_it(void) {}
void invalidate_func(unsigned int addr) { (void)addr; }

/*
 * Fill the interpreter decode union for one instruction.
 * The union members overlay each other: I-type immediate sits on the
 * R-type rd pointer, and COP1 byte fields sit on rs. Write only the
 * format the opcode actually uses.
 */
void prefetch_opcode(unsigned long instr)
{
	unsigned int op = (instr >> 26) & 63;
	unsigned int rs = (instr >> 21) & 31;
	unsigned int rt = (instr >> 16) & 31;
	unsigned int rd = (instr >> 11) & 31;
	unsigned int sa = (instr >> 6) & 31;
	short imm = (short)(instr & 0xFFFF);

	PC->addr = interp_addr;

	if (op == 0) {
		/* SPECIAL: R-type */
		PC->f.r.rs = &reg[rs];
		PC->f.r.rt = &reg[rt];
		PC->f.r.rd = &reg[rd];
		PC->f.r.nrd = (unsigned char)rd;
		PC->f.r.sa = (unsigned char)sa;
		return;
	}

	if (op == 2 || op == 3) {
		PC->f.j.inst_index = instr & 0x3FFFFFF;
		return;
	}

	if (op == 16) {
		/* COP0: GPR in rt, CP0 sel in rd (nrd) */
		PC->f.r.rs = &reg[rs];
		PC->f.r.rt = &reg[rt];
		PC->f.r.rd = &reg[rd];
		PC->f.r.nrd = (unsigned char)rd;
		PC->f.r.sa = (unsigned char)sa;
		return;
	}

	if (op == 17) {
		/* COP1 */
		if (rs == 8) {
			PC->f.i.rs = &reg[rs];
			PC->f.i.rt = &reg[rt];
			PC->f.i.immediate = imm;
			return;
		}
		if (rs < 16) {
			/* MxC1 / CxC1 */
			PC->f.r.rs = &reg[rs];
			PC->f.r.rt = &reg[rt];
			PC->f.r.rd = &reg[rd];
			PC->f.r.nrd = (unsigned char)rd;
			PC->f.r.sa = (unsigned char)sa;
			return;
		}
		PC->f.cf.ft = (unsigned char)rt;
		PC->f.cf.fs = (unsigned char)rd;
		PC->f.cf.fd = (unsigned char)sa;
		return;
	}

	if (op == 0x31 || op == 0x35 || op == 0x39 || op == 0x3D) {
		/* LWC1 / LDC1 / SWC1 / SDC1 */
		PC->f.lf.base = (unsigned char)rs;
		PC->f.lf.ft = (unsigned char)rt;
		PC->f.lf.offset = imm;
		return;
	}

	/* I-type (branches, ALU immediate, integer load/store, REGIMM) */
	PC->f.i.rs = &reg[rs];
	PC->f.i.rt = &reg[rt];
	PC->f.i.immediate = imm;
}
