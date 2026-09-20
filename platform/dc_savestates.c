/**
 * Dreamcast savestates (Phase 8d).
 *
 * Wii64 gzwrite()'d native structs (host-endian, zlib). That is not a
 * portable SH4/host format. This file is uncompressed little-endian with
 * explicit field widths so ILP32 HOST tests and KOS share one dump.
 *
 * NOT64ST v3: ROM goodname + cart CRC1 in the header, CRC32 of the body
 * so a truncated file is rejected before RDRAM is touched. Slots are
 * `saves/<sanitized-goodname>.stN`. EEPROM/SRAM/Flash/mempak blobs sit in
 * the body after the 24-byte flashram info block. v2 dumps will not load.
 */

#include "../main/winlnxdefs.h"
#include "../main/savestates.h"
#include "../fileBrowser/fileBrowser.h"
#include "../gc_memory/memory.h"
#include "../gc_memory/flashram.h"
#include "../gc_memory/Saves.h"
#include "../gc_memory/TLB-Cache.h"
#include "../gc_memory/tlb.h"
#include "../r4300/r4300.h"
#include "../r4300/interupt.h"
#include "../r4300/macros.h"
#include "../main/rom.h"
#include "dc_debug.h"
#include "dc_memory.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifdef DC_HOST_STUB
#include <unistd.h>
#include "../gc_memory/pif.h"
#endif

extern BOOL eepromWritten, sramWritten, flashramWritten, mempakWritten;
extern char *get_savespath(void);

#define SS_MAGIC "NOT64ST\n"
#define SS_VERSION 3u
#define SS_NAME_LEN 32
#define SS_STEM_LEN 24

int savestates_job;

static unsigned slot;
static int last_ok;
static char last_err[40];
static uint32_t crc_state;
static int crc_on;

static uint32_t crc32_update(uint32_t crc, const void *p, size_t n)
{
	const unsigned char *b = p;
	size_t i;
	unsigned k;

	for (i = 0; i < n; ++i) {
		crc ^= b[i];
		for (k = 0; k < 8; ++k)
			crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)-(int)(crc & 1u));
	}
	return crc;
}

static void cart_name(char *dst)
{
	memset(dst, 0, SS_NAME_LEN);
	strncpy(dst, ROM_SETTINGS.goodname, SS_NAME_LEN - 1);
}

static void file_stem(char *dst, size_t n)
{
	const char *s = ROM_SETTINGS.goodname;
	size_t i = 0;

	if (n == 0)
		return;
	while (*s && i + 1 < n) {
		unsigned char c = (unsigned char)*s++;
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
		    (c >= '0' && c <= '9'))
			dst[i++] = (char)c;
		else if (i > 0 && dst[i - 1] != '_')
			dst[i++] = '_';
	}
	while (i > 0 && dst[i - 1] == '_')
		i--;
	if (i == 0) {
		strncpy(dst, "cart", n - 1);
		dst[n - 1] = 0;
		return;
	}
	dst[i] = 0;
}

static const char *slot_path(void)
{
	static char path[FILE_BROWSER_MAX_PATH_LEN];
	char stem[SS_STEM_LEN];
	const char *dir = get_savespath();

	if (!dir)
		dir = "./saves";
	file_stem(stem, sizeof(stem));
	snprintf(path, sizeof(path), "%s/%s.st%u", dir, stem, slot);
	return path;
}

unsigned savestates_get_slot(void)
{
	return slot;
}

const char *savestates_filename(void)
{
	return slot_path();
}

int savestates_ok(void)
{
	return last_ok;
}

const char *savestates_error(void)
{
	return last_err;
}

static int buf_not_fill(const unsigned char *p, size_t n, unsigned char v)
{
	size_t i;

	for (i = 0; i < n; ++i) {
		if (p[i] != v)
			return 1;
	}
	return 0;
}

static int wr_mem(FILE *f, const void *p, size_t n)
{
	if (crc_on)
		crc_state = crc32_update(crc_state, p, n);
	return fwrite(p, 1, n, f) == n ? 0 : -1;
}

static int rd_mem(FILE *f, void *p, size_t n)
{
	if (crc_on)
		crc_state = crc32_update(crc_state, p, n);
	return fread(p, 1, n, f) == n ? 0 : -1;
}

static int wr_u32(FILE *f, unsigned long v)
{
	uint32_t x = (uint32_t)v;
	return wr_mem(f, &x, 4);
}

static int rd_u32(FILE *f, unsigned long *v)
{
	uint32_t x;
	if (rd_mem(f, &x, 4))
		return -1;
	*v = x;
	return 0;
}

static int wr_u64(FILE *f, unsigned long long v)
{
	uint64_t x = (uint64_t)v;
	return wr_mem(f, &x, 8);
}

static int rd_u64(FILE *f, unsigned long long *v)
{
	uint64_t x;
	if (rd_mem(f, &x, 8))
		return -1;
	*v = x;
	return 0;
}

static int wr_i8(FILE *f, int v)
{
	int8_t x = (int8_t)v;
	return wr_mem(f, &x, 1);
}

static int rd_i8(FILE *f, char *v)
{
	int8_t x;
	if (rd_mem(f, &x, 1))
		return -1;
	*v = (char)x;
	return 0;
}

static int wr_i16(FILE *f, int v)
{
	int16_t x = (int16_t)v;
	return wr_mem(f, &x, 2);
}

static int rd_i16(FILE *f, short *v)
{
	int16_t x;
	if (rd_mem(f, &x, 2))
		return -1;
	*v = (short)x;
	return 0;
}

static int wr_ulongs(FILE *f, const unsigned long *p, int n)
{
	int i;
	for (i = 0; i < n; ++i) {
		if (wr_u32(f, p[i]))
			return -1;
	}
	return 0;
}

static int rd_ulongs(FILE *f, unsigned long *p, int n)
{
	int i;
	for (i = 0; i < n; ++i) {
		if (rd_u32(f, &p[i]))
			return -1;
	}
	return 0;
}

static int wr_tlb_e(FILE *f, const tlb *t)
{
	if (wr_i16(f, t->mask))
		return -1;
	if (wr_u32(f, (unsigned long)t->vpn2))
		return -1;
	if (wr_i8(f, t->g) || wr_i8(f, (int)t->asid))
		return -1;
	if (wr_u32(f, (unsigned long)t->pfn_even))
		return -1;
	if (wr_i8(f, t->c_even) || wr_i8(f, t->d_even) || wr_i8(f, t->v_even))
		return -1;
	if (wr_u32(f, (unsigned long)t->pfn_odd))
		return -1;
	if (wr_i8(f, t->c_odd) || wr_i8(f, t->d_odd) || wr_i8(f, t->v_odd) ||
	    wr_i8(f, t->r))
		return -1;
	if (wr_u32(f, t->start_even) || wr_u32(f, t->end_even) ||
	    wr_u32(f, t->phys_even) || wr_u32(f, t->start_odd) ||
	    wr_u32(f, t->end_odd) || wr_u32(f, t->phys_odd))
		return -1;
	return 0;
}

static int rd_tlb_e(FILE *f, tlb *t)
{
	unsigned long u;
	char c;

	memset(t, 0, sizeof(*t));
	if (rd_i16(f, &t->mask))
		return -1;
	if (rd_u32(f, &u))
		return -1;
	t->vpn2 = (long)u;
	if (rd_i8(f, &t->g) || rd_i8(f, &c))
		return -1;
	t->asid = (unsigned char)c;
	if (rd_u32(f, &u))
		return -1;
	t->pfn_even = (long)u;
	if (rd_i8(f, &t->c_even) || rd_i8(f, &t->d_even) || rd_i8(f, &t->v_even))
		return -1;
	if (rd_u32(f, &u))
		return -1;
	t->pfn_odd = (long)u;
	if (rd_i8(f, &t->c_odd) || rd_i8(f, &t->d_odd) || rd_i8(f, &t->v_odd) ||
	    rd_i8(f, &t->r))
		return -1;
	if (rd_u32(f, &t->start_even) || rd_u32(f, &t->end_even) ||
	    rd_u32(f, &t->phys_even) || rd_u32(f, &t->start_odd) ||
	    rd_u32(f, &t->end_odd) || rd_u32(f, &t->phys_odd))
		return -1;
	return 0;
}

static int wr_mmio(FILE *f)
{
	int i;

	if (wr_ulongs(f, (unsigned long *)&rdram_register, 10))
		return -1;

	if (wr_u32(f, MI_register.w_mi_init_mode_reg) ||
	    wr_u32(f, MI_register.mi_init_mode_reg) ||
	    wr_i8(f, MI_register.init_length) ||
	    wr_i8(f, MI_register.init_mode) ||
	    wr_i8(f, MI_register.ebus_test_mode) ||
	    wr_i8(f, MI_register.RDRAM_reg_mode) ||
	    wr_u32(f, MI_register.mi_version_reg) ||
	    wr_u32(f, MI_register.mi_intr_reg) ||
	    wr_u32(f, MI_register.mi_intr_mask_reg) ||
	    wr_u32(f, MI_register.w_mi_intr_mask_reg) ||
	    wr_i8(f, MI_register.SP_intr_mask) ||
	    wr_i8(f, MI_register.SI_intr_mask) ||
	    wr_i8(f, MI_register.AI_intr_mask) ||
	    wr_i8(f, MI_register.VI_intr_mask) ||
	    wr_i8(f, MI_register.PI_intr_mask) ||
	    wr_i8(f, MI_register.DP_intr_mask))
		return -1;

	if (wr_ulongs(f, (unsigned long *)&pi_register, 13))
		return -1;

	if (wr_u32(f, sp_register.sp_mem_addr_reg) ||
	    wr_u32(f, sp_register.sp_dram_addr_reg) ||
	    wr_u32(f, sp_register.sp_rd_len_reg) ||
	    wr_u32(f, sp_register.sp_wr_len_reg) ||
	    wr_u32(f, sp_register.w_sp_status_reg) ||
	    wr_u32(f, sp_register.sp_status_reg) ||
	    wr_i8(f, sp_register.halt) || wr_i8(f, sp_register.broke) ||
	    wr_i8(f, sp_register.dma_busy) || wr_i8(f, sp_register.dma_full) ||
	    wr_i8(f, sp_register.io_full) || wr_i8(f, sp_register.single_step) ||
	    wr_i8(f, sp_register.intr_break) || wr_i8(f, sp_register.signal0) ||
	    wr_i8(f, sp_register.signal1) || wr_i8(f, sp_register.signal2) ||
	    wr_i8(f, sp_register.signal3) || wr_i8(f, sp_register.signal4) ||
	    wr_i8(f, sp_register.signal5) || wr_i8(f, sp_register.signal6) ||
	    wr_i8(f, sp_register.signal7) ||
	    wr_u32(f, sp_register.sp_dma_full_reg) ||
	    wr_u32(f, sp_register.sp_dma_busy_reg) ||
	    wr_u32(f, sp_register.sp_semaphore_reg))
		return -1;

	if (wr_u32(f, rsp_register.rsp_pc) || wr_u32(f, rsp_register.rsp_ibist))
		return -1;
	if (wr_ulongs(f, (unsigned long *)&si_register, 4))
		return -1;
	if (wr_ulongs(f, (unsigned long *)&vi_register, 15))
		return -1;
	if (wr_ulongs(f, (unsigned long *)&ri_register, 8))
		return -1;
	if (wr_ulongs(f, (unsigned long *)&ai_register, 10))
		return -1;

	if (wr_u32(f, dpc_register.dpc_start) ||
	    wr_u32(f, dpc_register.dpc_end) ||
	    wr_u32(f, dpc_register.dpc_current) ||
	    wr_u32(f, dpc_register.w_dpc_status) ||
	    wr_u32(f, dpc_register.dpc_status) ||
	    wr_i8(f, dpc_register.xbus_dmem_dma) ||
	    wr_i8(f, dpc_register.freeze) || wr_i8(f, dpc_register.flush) ||
	    wr_i8(f, dpc_register.start_glck) ||
	    wr_i8(f, dpc_register.tmem_busy) ||
	    wr_i8(f, dpc_register.pipe_busy) ||
	    wr_i8(f, dpc_register.cmd_busy) ||
	    wr_i8(f, dpc_register.cbuf_busy) ||
	    wr_i8(f, dpc_register.dma_busy) ||
	    wr_i8(f, dpc_register.end_valid) ||
	    wr_i8(f, dpc_register.start_valid) ||
	    wr_u32(f, dpc_register.dpc_clock) ||
	    wr_u32(f, dpc_register.dpc_bufbusy) ||
	    wr_u32(f, dpc_register.dpc_pipebusy) ||
	    wr_u32(f, dpc_register.dpc_tmem))
		return -1;

	if (wr_ulongs(f, (unsigned long *)&dps_register, 4))
		return -1;

	for (i = 0; i < 32; ++i) {
		if (wr_tlb_e(f, &tlb_e[i]))
			return -1;
	}
	return 0;
}

static int rd_mmio(FILE *f)
{
	int i;

	if (rd_ulongs(f, (unsigned long *)&rdram_register, 10))
		return -1;

	if (rd_u32(f, &MI_register.w_mi_init_mode_reg) ||
	    rd_u32(f, &MI_register.mi_init_mode_reg) ||
	    rd_i8(f, &MI_register.init_length) ||
	    rd_i8(f, &MI_register.init_mode) ||
	    rd_i8(f, &MI_register.ebus_test_mode) ||
	    rd_i8(f, &MI_register.RDRAM_reg_mode) ||
	    rd_u32(f, &MI_register.mi_version_reg) ||
	    rd_u32(f, &MI_register.mi_intr_reg) ||
	    rd_u32(f, &MI_register.mi_intr_mask_reg) ||
	    rd_u32(f, &MI_register.w_mi_intr_mask_reg) ||
	    rd_i8(f, &MI_register.SP_intr_mask) ||
	    rd_i8(f, &MI_register.SI_intr_mask) ||
	    rd_i8(f, &MI_register.AI_intr_mask) ||
	    rd_i8(f, &MI_register.VI_intr_mask) ||
	    rd_i8(f, &MI_register.PI_intr_mask) ||
	    rd_i8(f, &MI_register.DP_intr_mask))
		return -1;

	if (rd_ulongs(f, (unsigned long *)&pi_register, 13))
		return -1;

	if (rd_u32(f, &sp_register.sp_mem_addr_reg) ||
	    rd_u32(f, &sp_register.sp_dram_addr_reg) ||
	    rd_u32(f, &sp_register.sp_rd_len_reg) ||
	    rd_u32(f, &sp_register.sp_wr_len_reg) ||
	    rd_u32(f, &sp_register.w_sp_status_reg) ||
	    rd_u32(f, &sp_register.sp_status_reg) ||
	    rd_i8(f, &sp_register.halt) || rd_i8(f, &sp_register.broke) ||
	    rd_i8(f, &sp_register.dma_busy) || rd_i8(f, &sp_register.dma_full) ||
	    rd_i8(f, &sp_register.io_full) || rd_i8(f, &sp_register.single_step) ||
	    rd_i8(f, &sp_register.intr_break) || rd_i8(f, &sp_register.signal0) ||
	    rd_i8(f, &sp_register.signal1) || rd_i8(f, &sp_register.signal2) ||
	    rd_i8(f, &sp_register.signal3) || rd_i8(f, &sp_register.signal4) ||
	    rd_i8(f, &sp_register.signal5) || rd_i8(f, &sp_register.signal6) ||
	    rd_i8(f, &sp_register.signal7) ||
	    rd_u32(f, &sp_register.sp_dma_full_reg) ||
	    rd_u32(f, &sp_register.sp_dma_busy_reg) ||
	    rd_u32(f, &sp_register.sp_semaphore_reg))
		return -1;

	if (rd_u32(f, &rsp_register.rsp_pc) || rd_u32(f, &rsp_register.rsp_ibist))
		return -1;
	if (rd_ulongs(f, (unsigned long *)&si_register, 4))
		return -1;
	if (rd_ulongs(f, (unsigned long *)&vi_register, 15))
		return -1;
	if (rd_ulongs(f, (unsigned long *)&ri_register, 8))
		return -1;
	if (rd_ulongs(f, (unsigned long *)&ai_register, 10))
		return -1;

	if (rd_u32(f, &dpc_register.dpc_start) ||
	    rd_u32(f, &dpc_register.dpc_end) ||
	    rd_u32(f, &dpc_register.dpc_current) ||
	    rd_u32(f, &dpc_register.w_dpc_status) ||
	    rd_u32(f, &dpc_register.dpc_status) ||
	    rd_i8(f, &dpc_register.xbus_dmem_dma) ||
	    rd_i8(f, &dpc_register.freeze) || rd_i8(f, &dpc_register.flush) ||
	    rd_i8(f, &dpc_register.start_glck) ||
	    rd_i8(f, &dpc_register.tmem_busy) ||
	    rd_i8(f, &dpc_register.pipe_busy) ||
	    rd_i8(f, &dpc_register.cmd_busy) ||
	    rd_i8(f, &dpc_register.cbuf_busy) ||
	    rd_i8(f, &dpc_register.dma_busy) ||
	    rd_i8(f, &dpc_register.end_valid) ||
	    rd_i8(f, &dpc_register.start_valid) ||
	    rd_u32(f, &dpc_register.dpc_clock) ||
	    rd_u32(f, &dpc_register.dpc_bufbusy) ||
	    rd_u32(f, &dpc_register.dpc_pipebusy) ||
	    rd_u32(f, &dpc_register.dpc_tmem))
		return -1;

	if (rd_ulongs(f, (unsigned long *)&dps_register, 4))
		return -1;

	for (i = 0; i < 32; ++i) {
		if (rd_tlb_e(f, &tlb_e[i]))
			return -1;
	}
	return 0;
}

static int wr_cpu(FILE *f)
{
	int i;
	int32_t ll = llbit;

	if (wr_mem(f, &ll, 4))
		return -1;
	for (i = 0; i < 34; ++i) {
		if (wr_u64(f, (unsigned long long)reg[i]))
			return -1;
	}
	for (i = 0; i < 32; ++i) {
		if (wr_u32(f, reg_cop0[i]))
			return -1;
	}
	for (i = 0; i < 32; ++i) {
		if (wr_u64(f, (unsigned long long)reg_cop1_fgr_64[i]))
			return -1;
	}
	if (wr_u32(f, FCR0) || wr_u32(f, FCR31))
		return -1;
	if (wr_u32(f, interp_addr) || wr_u32(f, last_addr) ||
	    wr_u32(f, next_interupt) || wr_u32(f, next_vi) ||
	    wr_u32(f, (unsigned long)vi_field) || wr_u32(f, delay_slot) ||
	    wr_u32(f, skip_jump))
		return -1;
	return 0;
}

static int rd_cpu(FILE *f)
{
	int i;
	int32_t ll;
	unsigned long u;
	unsigned long long q;

	if (rd_mem(f, &ll, 4))
		return -1;
	llbit = ll;
	for (i = 0; i < 34; ++i) {
		if (rd_u64(f, &q))
			return -1;
		reg[i] = (long long int)q;
	}
	for (i = 0; i < 32; ++i) {
		if (rd_u32(f, &reg_cop0[i]))
			return -1;
	}
	for (i = 0; i < 32; ++i) {
		if (rd_u64(f, &q))
			return -1;
		reg_cop1_fgr_64[i] = (long long int)q;
	}
	if (rd_u32(f, &FCR0) || rd_u32(f, &FCR31))
		return -1;
	if (rd_u32(f, &interp_addr) || rd_u32(f, &last_addr) ||
	    rd_u32(f, &next_interupt) || rd_u32(f, &next_vi) ||
	    rd_u32(f, &u))
		return -1;
	vi_field = (int)u;
	if (rd_u32(f, &delay_slot) || rd_u32(f, &skip_jump))
		return -1;
	set_fpr_pointers((int)Status);
	return 0;
}

static int wr_events(FILE *f)
{
	char buf[1024];
	uint32_t len;

	len = (uint32_t)save_eventqueue_infos(buf);
	if (len > sizeof(buf))
		return -1;
	if (wr_u32(f, len) || wr_mem(f, buf, len))
		return -1;
	return 0;
}

static int rd_events(FILE *f)
{
	char buf[1024];
	unsigned long len = 0;

	if (rd_u32(f, &len))
		return -1;
	if (len == 0 || len > sizeof(buf))
		return -1;
	if (rd_mem(f, buf, (size_t)len))
		return -1;
	load_eventqueue_infos(buf);
	return 0;
}

static int write_state(FILE *f)
{
	char flash[24];
	char name[SS_NAME_LEN];
	uint32_t ver = SS_VERSION;
	uint32_t rdram_bytes = DC_N64_RDRAM_SIZE;
	uint32_t crc1 = ROM_HEADER.CRC1;

	crc_on = 0;
	cart_name(name);
	if (wr_mem(f, SS_MAGIC, 8) || wr_mem(f, &ver, 4) ||
	    wr_mem(f, &rdram_bytes, 4) || wr_mem(f, name, SS_NAME_LEN) ||
	    wr_mem(f, &crc1, 4))
		return -1;
	crc_on = 1;
	crc_state = 0xffffffffu;
	if (wr_mmio(f))
		return -1;
	if (wr_mem(f, rdramb, DC_N64_RDRAM_SIZE) ||
	    wr_mem(f, SP_DMEMb, 0x1000) || wr_mem(f, SP_IMEMb, 0x1000) ||
	    wr_mem(f, PIF_RAMb, 0x40))
		return -1;
	save_flashram_infos(flash);
	if (wr_mem(f, flash, 24))
		return -1;
	if (wr_mem(f, dc_cart_eeprom(), DC_EEPROM_SIZE) ||
	    wr_mem(f, dc_cart_sram(), DC_SRAM_SIZE) ||
	    wr_mem(f, dc_cart_flashram(), DC_FLASH_SIZE) ||
	    wr_mem(f, dc_cart_mempak(), DC_MEMPAK_SIZE))
		return -1;
	{
		long tlb_at = ftell(f);

		if (tlb_at < 0 || TLBCache_fwrite(f))
			return -1;
		if (crc_on) {
			unsigned char buf[4096];
			long tlb_end = ftell(f);
			long left;

			if (tlb_end < tlb_at)
				return -1;
			if (fseek(f, tlb_at, SEEK_SET) != 0)
				return -1;
			left = tlb_end - tlb_at;
			while (left > 0) {
				size_t n = (size_t)left > sizeof(buf) ? sizeof(buf)
								     : (size_t)left;
				if (fread(buf, 1, n, f) != n)
					return -1;
				crc_state = crc32_update(crc_state, buf, n);
				left -= (long)n;
			}
		}
	}
	if (wr_cpu(f) || wr_events(f))
		return -1;
	crc_on = 0;
	if (wr_u32(f, crc_state))
		return -1;
	return 0;
}

static int file_crc_ok(FILE *f, long body_off)
{
	unsigned char buf[4096];
	long end, left;
	uint32_t got, want;
	size_t n;

	if (fseek(f, 0, SEEK_END) != 0)
		return -1;
	end = ftell(f);
	if (end < body_off + 4)
		return -1;
	left = end - body_off - 4;
	if (fseek(f, body_off, SEEK_SET) != 0)
		return -1;
	got = 0xffffffffu;
	while (left > 0) {
		n = (size_t)left > sizeof(buf) ? sizeof(buf) : (size_t)left;
		if (fread(buf, 1, n, f) != n)
			return -1;
		got = crc32_update(got, buf, n);
		left -= (long)n;
	}
	if (fread(&want, 4, 1, f) != 1)
		return -1;
	return got == want ? 0 : -1;
}

static int read_state(FILE *f)
{
	char magic[8];
	char flash[24];
	char name[SS_NAME_LEN], expect[SS_NAME_LEN];
	uint32_t ver = 0, rdram_bytes = 0, crc1 = 0;
	long body_off;

	crc_on = 0;
	if (rd_mem(f, magic, 8) || memcmp(magic, SS_MAGIC, 8) != 0) {
		snprintf(last_err, sizeof(last_err), "bad dump");
		return -1;
	}
	if (rd_mem(f, &ver, 4) || ver != SS_VERSION) {
		snprintf(last_err, sizeof(last_err), "bad dump");
		return -1;
	}
	if (rd_mem(f, &rdram_bytes, 4) || rdram_bytes != DC_N64_RDRAM_SIZE) {
		snprintf(last_err, sizeof(last_err), "bad dump");
		return -1;
	}
	if (rd_mem(f, name, SS_NAME_LEN) || rd_mem(f, &crc1, 4))
		return -1;
	cart_name(expect);
	if (memcmp(name, expect, SS_NAME_LEN) != 0 || crc1 != ROM_HEADER.CRC1) {
		snprintf(last_err, sizeof(last_err), "wrong ROM");
		return -1;
	}
	body_off = ftell(f);
	if (body_off < 0 || file_crc_ok(f, body_off) ||
	    fseek(f, body_off, SEEK_SET) != 0) {
		snprintf(last_err, sizeof(last_err), "truncated");
		return -1;
	}
	if (rd_mmio(f))
		return -1;
	if (rd_mem(f, rdramb, DC_N64_RDRAM_SIZE) ||
	    rd_mem(f, SP_DMEMb, 0x1000) || rd_mem(f, SP_IMEMb, 0x1000) ||
	    rd_mem(f, PIF_RAMb, 0x40))
		return -1;
	if (rd_mem(f, flash, 24))
		return -1;
	load_flashram_infos(flash);
	if (rd_mem(f, dc_cart_eeprom(), DC_EEPROM_SIZE) ||
	    rd_mem(f, dc_cart_sram(), DC_SRAM_SIZE) ||
	    rd_mem(f, dc_cart_flashram(), DC_FLASH_SIZE) ||
	    rd_mem(f, dc_cart_mempak(), DC_MEMPAK_SIZE))
		return -1;
	eepromWritten = buf_not_fill(dc_cart_eeprom(), DC_EEPROM_SIZE, 0xFF);
	sramWritten = buf_not_fill(dc_cart_sram(), DC_SRAM_SIZE, 0x00);
	flashramWritten = buf_not_fill(dc_cart_flashram(), DC_FLASH_SIZE, 0xFF);
	mempakWritten = TRUE;
	if (TLBCache_fread(f))
		return -1;
	if (rd_cpu(f) || rd_events(f))
		return -1;
	return 0;
}

void savestates_save(void)
{
	FILE *fp;
	const char *path = slot_path();

	savestates_job &= ~SAVESTATE;
	last_ok = 0;
	last_err[0] = 0;
	if (!rdramb) {
		snprintf(last_err, sizeof(last_err), "no RDRAM");
		dc_log(DC_LOG_ERROR, "savestate: save slot %u: no RDRAM", slot);
		return;
	}
	fp = fopen(path, "w+b");
	if (!fp) {
		snprintf(last_err, sizeof(last_err), "cannot open");
		dc_log(DC_LOG_ERROR, "savestate: save slot %u: cannot open %s",
		       slot, path);
		return;
	}
	if (write_state(fp)) {
		fclose(fp);
		remove(path);
		snprintf(last_err, sizeof(last_err), "write failed");
		dc_log(DC_LOG_ERROR, "savestate: save slot %u failed (%s)",
		       slot, path);
		return;
	}
	if (fclose(fp) != 0) {
		remove(path);
		dc_log(DC_LOG_ERROR, "savestate: save slot %u close failed", slot);
		return;
	}
	last_ok = 1;
	dc_log(DC_LOG_INFO, "savestate: saved slot %u (%s)", slot, path);
}

void savestates_load(void)
{
	FILE *fp;
	const char *path = slot_path();

	savestates_job &= ~LOADSTATE;
	last_ok = 0;
	last_err[0] = 0;
	if (!rdramb) {
		dc_log(DC_LOG_ERROR, "savestate: load slot %u: no RDRAM", slot);
		return;
	}
	fp = fopen(path, "rb");
	if (!fp) {
		snprintf(last_err, sizeof(last_err), "missing");
		dc_log(DC_LOG_ERROR, "savestate: load slot %u: missing %s",
		       slot, path);
		return;
	}
	if (read_state(fp)) {
		fclose(fp);
		dc_log(DC_LOG_ERROR, "savestate: load slot %u failed (%s)%s%s",
		       slot, path, last_err[0] ? ": " : "", last_err);
		return;
	}
	fclose(fp);
	last_ok = 1;
	dc_log(DC_LOG_INFO, "savestate: loaded slot %u (%s)", slot, path);
}

int savestates_exists(int mode)
{
	FILE *fp;
	char magic[8];

	(void)mode;
	fp = fopen(slot_path(), "rb");
	if (!fp)
		return 0;
	if (fread(magic, 1, 8, fp) != 8 || memcmp(magic, SS_MAGIC, 8) != 0) {
		fclose(fp);
		return 0;
	}
	fclose(fp);
	return 1;
}

void savestates_select_slot(unsigned int s)
{
	if (s > 9)
		s = 9;
	slot = s;
}

void savestates_select_filename(void)
{
}

#ifdef DC_HOST_STUB
int savestates_selftest(void)
{
	int fails = 0;
	unsigned old_slot = slot;
	unsigned char old_byte, old_eep0, old_eep1, old_sram;
	BOOL old_ew, old_sw;
	unsigned long old_pc;
	long long old_r1;
	unsigned long old_vi;
	const char *path;
	char stem[SS_STEM_LEN];

	if (!rdramb) {
		printf("savestate FAIL: RDRAM not initialised\n");
		return 1;
	}

	savestates_select_slot(9);
	path = slot_path();
	file_stem(stem, sizeof(stem));
	remove(path);

	if (!strstr(path, stem) || !strstr(path, ".st9") ||
	    strstr(path, "not64.st")) {
		printf("savestate FAIL: per-ROM path '%s' (stem '%s')\n",
		       path, stem);
		fails++;
	}

	old_byte = rdramb[0x200];
	old_pc = interp_addr;
	old_r1 = reg[1];
	old_vi = vi_register.vi_origin;
	old_eep0 = dc_eeprom_debug_get(0);
	old_eep1 = dc_eeprom_debug_get(1);
	old_sram = dc_cart_sram()[0];
	old_ew = eepromWritten;
	old_sw = sramWritten;

	rdramb[0x200] = 0xA5;
	interp_addr = 0xa4000040ul;
	reg[1] = 0x1111;
	vi_register.vi_origin = 0x00100000ul;
	dc_eeprom_debug_set(0, 0x3C);
	dc_eeprom_debug_set(1, 0xA5);
	dc_cart_sram()[0] = 0x5A;
	sramWritten = TRUE;
	savestates_save();
	if (!last_ok || !savestates_exists(SAVESTATE)) {
		printf("savestate FAIL: did not write %s\n", path);
		fails++;
	}

	rdramb[0x200] = 0;
	interp_addr = 0;
	reg[1] = 0;
	vi_register.vi_origin = 0;
	init_eeprom();
	dc_cart_sram()[0] = 0;
	savestates_load();
	if (!last_ok) {
		printf("savestate FAIL: load reported error\n");
		fails++;
	}
	if (rdramb[0x200] != 0xA5) {
		printf("savestate FAIL: RDRAM byte 0x%02x want 0xA5\n",
		       rdramb[0x200]);
		fails++;
	}
	if (interp_addr != 0xa4000040ul) {
		printf("savestate FAIL: PC=0x%lx want 0xa4000040\n", interp_addr);
		fails++;
	}
	if ((reg[1] & 0xffffffffu) != 0x1111u) {
		printf("savestate FAIL: r1=0x%llx want 0x1111\n",
		       (unsigned long long)reg[1]);
		fails++;
	}
	if (vi_register.vi_origin != 0x00100000ul) {
		printf("savestate FAIL: VI origin=0x%lx\n",
		       vi_register.vi_origin);
		fails++;
	}
	if (dc_eeprom_debug_get(0) != 0x3C || dc_eeprom_debug_get(1) != 0xA5) {
		printf("savestate FAIL: EEPROM %02x %02x want 3c a5\n",
		       dc_eeprom_debug_get(0), dc_eeprom_debug_get(1));
		fails++;
	}
	if (dc_cart_sram()[0] != 0x5A) {
		printf("savestate FAIL: SRAM 0x%02x want 0x5A\n",
		       dc_cart_sram()[0]);
		fails++;
	}
	if (!eepromWritten || !sramWritten) {
		printf("savestate FAIL: dirty cart saves not marked written\n");
		fails++;
	}

	remove(path);
	if (savestates_exists(SAVESTATE)) {
		printf("savestate FAIL: %s still present after unlink\n", path);
		fails++;
	}

	savestates_load();
	if (last_ok || strcmp(last_err, "missing") != 0) {
		printf("savestate FAIL: missing dump err='%s'\n", last_err);
		fails++;
	}

	{
		FILE *bogus = fopen(path, "wb");
		unsigned char old;

		if (bogus) {
			fwrite("NOPEFILE", 8, 1, bogus);
			fclose(bogus);
		}
		if (savestates_exists(SAVESTATE)) {
			printf("savestate FAIL: garbage file counted as a dump\n");
			fails++;
		}
		savestates_load();
		if (last_ok || strcmp(last_err, "bad dump") != 0) {
			printf("savestate FAIL: garbage err='%s'\n", last_err);
			fails++;
		}
		remove(path);

		rdramb[0x200] = 0xA5;
		interp_addr = 0xa4000040ul;
		reg[1] = 0x1111;
		vi_register.vi_origin = 0x00100000ul;
		dc_eeprom_debug_set(0, 0x3C);
		savestates_save();
		old = rdramb[0x200];
		{
			FILE *patch = fopen(path, "r+b");
			char other[SS_NAME_LEN];

			memset(other, 0, sizeof(other));
			strncpy(other, "OTHER ROM", sizeof(other) - 1);
			if (!patch || fseek(patch, 16, SEEK_SET) != 0 ||
			    fwrite(other, 1, SS_NAME_LEN, patch) != SS_NAME_LEN) {
				printf("savestate FAIL: could not patch dump header\n");
				fails++;
			}
			if (patch)
				fclose(patch);
			rdramb[0x200] = 0x11;
			savestates_load();
			if (last_ok) {
				printf("savestate FAIL: loaded dump from another ROM name\n");
				fails++;
			}
			if (strcmp(last_err, "wrong ROM") != 0) {
				printf("savestate FAIL: wrong-ROM err='%s'\n", last_err);
				fails++;
			}
			if (rdramb[0x200] != 0x11) {
				printf("savestate FAIL: rejected load still mutated RDRAM\n");
				fails++;
			}
		}
		remove(path);
		rdramb[0x200] = old;
		savestates_save();
		{
			FILE *cut = fopen(path, "r+b");

			if (cut) {
				if (ftruncate(fileno(cut), 64) != 0)
					printf("savestate WARN: ftruncate failed\n");
				fclose(cut);
			}
			rdramb[0x200] = old;
			savestates_load();
			if (last_ok) {
				printf("savestate FAIL: truncated dump loaded\n");
				fails++;
			}
			if (strcmp(last_err, "truncated") != 0) {
				printf("savestate FAIL: truncated err='%s'\n", last_err);
				fails++;
			}
			if (rdramb[0x200] != old) {
				printf("savestate FAIL: truncated load mutated RDRAM\n");
				fails++;
			}
		}
		remove(path);
		rdramb[0x200] = old;
	}

	rdramb[0x200] = old_byte;
	interp_addr = old_pc;
	reg[1] = old_r1;
	vi_register.vi_origin = old_vi;
	dc_cart_eeprom()[0] = old_eep0;
	dc_cart_eeprom()[1] = old_eep1;
	dc_cart_sram()[0] = old_sram;
	eepromWritten = old_ew;
	sramWritten = old_sw;
	savestates_select_slot(old_slot);

	printf("savestate selftest: %s (%d failure(s))\n",
	       fails ? "FAIL" : "PASS", fails);
	return fails;
}
#endif
