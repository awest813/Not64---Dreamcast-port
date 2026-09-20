/**
 * Dreamcast / host entry: diagnostics, interpreter, CPU test, I/O smoke.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef DC_HOST_STUB
#include <kos.h>
#endif

#include "../platform/dc_memory.h"
#include "../platform/dc_menu/dc_menu.h"
#include "../platform/dc_settings.h"
#include "../platform/dc_pvr.h"
#include "../platform/dc_debug.h"
#include "../fileBrowser/fileBrowser.h"
#include "../fileBrowser/fileBrowser-kos.h"
#include "../main/winlnxdefs.h"
#include "../main/plugin.h"
#include "../gc_input/controller.h"
#include "../main/wii64config.h"
#include "../main/rom.h"
#include "../main/ROM-Cache.h"
#include "../r4300/r4300.h"
#include "../r4300/interupt.h"
#include "../gc_memory/memory.h"
#include "../gc_memory/TLB-Cache.h"
#include "../gc_memory/pif.h"
#include "../gc_memory/flashram.h"

#ifndef DC_HOST_STUB
KOS_INIT_FLAGS(INIT_DEFAULT);
#endif

extern unsigned long dc_interp_step_limit;
extern unsigned long dc_interp_steps;
extern BOOL hasLoadedROM;
extern void init_controller_ts(void);
extern void auto_assign_controllers(void);
extern unsigned int audio_dc_buffered(void);
extern void native_ReadController(int Control, unsigned char *Command);

/* Interpreter budget for one bring-up run. */
#define DC_BRINGUP_STEPS 10000UL

static GFX_INFO gfx_info;
static AUDIO_INFO audio_info;
static CONTROL_INFO control_info;
static RSP_INFO rsp_info;

static void print_budget(void)
{
	dc_log(DC_LOG_INFO, "Not64 Dreamcast bring-up");
	dc_log(DC_LOG_INFO, "  main RAM          %d MiB", DC_MAIN_RAM_SIZE / DC_MB);
	dc_log(DC_LOG_INFO, "  OS/code reserve   %d MiB", DC_OS_AND_CODE_RESERVE / DC_MB);
	dc_log(DC_LOG_INFO, "  N64 RDRAM         %d MiB", DC_N64_RDRAM_SIZE / DC_MB);
	dc_log(DC_LOG_INFO, "  ROM stream        %d MiB", DC_ROM_STREAM_SIZE / DC_MB);
	dc_log(DC_LOG_INFO, "  TLB/misc          %d KiB", DC_TLB_MISC_SIZE / DC_KB);
	dc_log(DC_LOG_INFO, "  tex cache         %d", DC_TEXCACHE_SIZE);
	dc_log(DC_LOG_INFO, "  audio ring        %d KiB", DC_AUDIO_RING_SIZE / DC_KB);
	dc_log(DC_LOG_INFO, "  heap remainder    %d bytes", DC_HEAP_REMAINDER);
}

static void list_rom_dir(void)
{
	fileBrowser_file *entries = NULL;
	int n, i;

	fileBrowser_kos_bind();
	if (romFile_init(romFile_topLevel) != 0) {
		dc_log(DC_LOG_ERROR, "rom dir init failed: %s",
		       romFile_topLevel->name);
		return;
	}

	n = romFile_readDir(romFile_topLevel, &entries);
	dc_log(DC_LOG_INFO, "ROM dir %s (%d entries)", romFile_topLevel->name, n);
	if (n < 0) {
		dc_log(DC_LOG_INFO,
		       "  (create this folder and add .z64/.n64 dumps later)");
		romFile_deinit(romFile_topLevel);
		return;
	}
	for (i = 0; i < n && i < 16; ++i) {
		const char *base = strrchr(entries[i].name, '/');
		base = base ? base + 1 : entries[i].name;
		if (base[0] == '.')
			continue;
		{
			const char *ext = strrchr(base, '.');
			if (ext && strcmp(ext, ".py") == 0)
				continue;
		}
		dc_log(DC_LOG_INFO, "  %s%s", entries[i].name,
		       (entries[i].attr & FILE_BROWSER_ATTR_DIR) ? "/" : "");
	}
	if (n > 16)
		dc_log(DC_LOG_INFO, "  ... %d more", n - 16);
	free(entries);
	romFile_deinit(romFile_topLevel);
}

static void probe_controllers(void)
{
	int i;
	controller_DC.refreshAvailable();
	for (i = 0; i < 4; ++i)
		dc_log(DC_LOG_INFO, "Maple %d: %s", i,
		       controller_DC.available[i] ? "present" : "empty");
}

static int probe_saves(void)
{
	fileBrowser_file marker;
	char buf[32];
	const char *msg = "not64-dc-host\n";
	int n;

	fileBrowser_kos_bind();
	if (saveFile_init(saveFile_dir) != 0) {
		dc_log(DC_LOG_ERROR, "save dir init failed: %s",
		       saveFile_dir->name);
		return -1;
	}

	memset(&marker, 0, sizeof(marker));
	strncpy(marker.name, saveFile_dir->name, FILE_BROWSER_MAX_PATH_LEN - 16);
	marker.name[FILE_BROWSER_MAX_PATH_LEN - 16] = '\0';
	strcat(marker.name, "/dc_host.txt");
	n = saveFile_writeFile(&marker, (void *)msg, (unsigned int)strlen(msg));
	if (n < (int)strlen(msg)) {
		dc_log(DC_LOG_ERROR, "save write failed (%d)", n);
		return -1;
	}
	marker.offset = 0;
	memset(buf, 0, sizeof(buf));
	n = saveFile_readFile(&marker, buf, sizeof(buf) - 1);
	dc_log(DC_LOG_INFO, "save %s (%d bytes): %s", marker.name, n, buf);
	saveFile_deinit(saveFile_dir);
	return n > 0 ? 0 : -1;
}

static int smoke_io(void)
{
	BUTTONS keys;
	int fail = 0;

#ifdef DC_HOST_STUB
	controller_DC_host_set(0, DC_CONT_A, 200, 80); /* A + analog */
#endif
	memset(&keys, 0, sizeof(keys));
	getKeys(0, &keys);
	dc_log(DC_LOG_INFO, "input0 A=%u X=%d Y=%d",
	       (unsigned)keys.A_BUTTON, (int)keys.X_AXIS, (int)keys.Y_AXIS);
#ifdef DC_HOST_STUB
	if (!keys.A_BUTTON) {
		dc_log(DC_LOG_ERROR, "input smoke: expected A held on host inject");
		fail = 1;
	}
#endif

	ai_register.ai_dram_addr = 0;
	ai_register.ai_len = 64;
	memset(rdram, 0x5A, 64);
	aiLenChanged();
	dc_log(DC_LOG_INFO, "audio ring buffered %u bytes", audio_dc_buffered());
	if (audio_dc_buffered() < 64) {
		dc_log(DC_LOG_ERROR, "audio smoke: ring did not accept AI DMA");
		fail = 1;
	}
	/* The ROM runs next: do not leave the smoke pattern in RDRAM. */
	memset(rdram, 0, 64);
	return fail;
}

#ifdef DC_HOST_STUB
/*
 * The Dreamcast pad cannot reach N64 Z, L, R or the C-buttons directly, so
 * controller-DC.c shifts them onto the analog triggers. Exercise every branch:
 * a mis-shift is silent in-game but obvious here.
 */
struct dc_map_case {
	const char *what;
	unsigned int buttons;
	int ltrig, rtrig;
	unsigned int want_z, want_l, want_r;
	unsigned int want_cu, want_cd, want_cl, want_cr;
	unsigned int want_du;
};

static const struct dc_map_case dc_map_cases[] = {
	/*                        btns                 lt  rt   Z  L  R  CU CD CL CR DU */
	{ "idle",                 0,                    0,  0,  0, 0, 0,  0, 0, 0, 0, 0 },
	{ "L-trigger -> Z",       0,                  200,  0,  1, 0, 0,  0, 0, 0, 0, 0 },
	{ "R-trigger -> R",       0,                    0,200,  0, 0, 1,  0, 0, 0, 0, 0 },
	{ "Y+L-trigger -> L",     DC_CONT_Y,          200,  0,  0, 1, 0,  0, 0, 0, 0, 0 },
	{ "Y alone idle",         DC_CONT_Y,            0,  0,  0, 0, 0,  0, 0, 0, 0, 0 },
	{ "X -> L",               DC_CONT_X,            0,  0,  0, 1, 0,  0, 0, 0, 0, 0 },
	{ "X+L-trigger -> L+Z",   DC_CONT_X,          200,  0,  1, 1, 0,  0, 0, 0, 0, 0 },
	{ "D-pad unshifted",      DC_CONT_DPAD_UP,      0,  0,  0, 0, 0,  0, 0, 0, 0, 1 },
	{ "both+Up -> C-Up",      DC_CONT_DPAD_UP,    200,200,  0, 0, 0,  1, 0, 0, 0, 0 },
	{ "both+X+Up -> L+C-Up",  DC_CONT_X | DC_CONT_DPAD_UP,
	                                              200,200,  0, 1, 0,  1, 0, 0, 0, 0 },
	{ "both+Down -> C-Down",  DC_CONT_DPAD_DOWN,  200,200,  0, 0, 0,  0, 1, 0, 0, 0 },
	{ "both+Left -> C-Left",  DC_CONT_DPAD_LEFT,  200,200,  0, 0, 0,  0, 0, 1, 0, 0 },
	{ "both+Right -> C-Right",DC_CONT_DPAD_RIGHT, 200,200,  0, 0, 0,  0, 0, 0, 1, 0 },
	/* A resting finger must not latch a shift. */
	{ "below threshold",      0,                   20, 20,  0, 0, 0,  0, 0, 0, 0, 0 },
	{ "threshold-1 idle",     0,                   47,  0,  0, 0, 0,  0, 0, 0, 0, 0 },
	{ "threshold Z",          0,                   48,  0,  1, 0, 0,  0, 0, 0, 0, 0 },
};

/* Stick scaling: raw Maple 0-255 -> N64 -80..+80 with a 10-count deadzone.
 * Full scale is asymmetric (-80 / +79) because 128 is centre in a 0-255
 * range: there are 128 counts below it and 127 above. */
struct dc_axis_case {
	const char *what;
	int jx, jy;
	int want_x, want_y;
};

static const struct dc_axis_case dc_axis_cases[] = {
	{ "centre",            128, 128,   0,   0 },
	{ "inside deadzone",   135, 121,   0,   0 },
	{ "just past deadzone",139, 128,   1,   0 },
	{ "full left",           0, 128, -80,   0 },
	{ "full right",        255, 128,  79,   0 },
	{ "full up",           128,   0,   0,  80 },
	{ "full down",         128, 255,   0, -79 },
	{ "host inject",       200,  80,  42,  26 },
};

static int smoke_map(void)
{
	unsigned int i;
	int fail = 0;

	for (i = 0; i < sizeof(dc_map_cases) / sizeof(dc_map_cases[0]); ++i) {
		const struct dc_map_case *t = &dc_map_cases[i];
		BUTTONS k;

		controller_DC_host_set(0, t->buttons, 128, 128);
		controller_DC_host_set_triggers(0, t->ltrig, t->rtrig);
		memset(&k, 0, sizeof(k));
		getKeys(0, &k);

		if (k.Z_TRIG != t->want_z || k.L_TRIG != t->want_l ||
		    k.R_TRIG != t->want_r ||
		    k.U_CBUTTON != t->want_cu || k.D_CBUTTON != t->want_cd ||
		    k.L_CBUTTON != t->want_cl || k.R_CBUTTON != t->want_cr ||
		    k.U_DPAD != t->want_du) {
			dc_log(DC_LOG_ERROR,
			       "map smoke: %s -> Z=%u L=%u R=%u C(u%u d%u l%u r%u) DU=%u",
			       t->what, (unsigned)k.Z_TRIG, (unsigned)k.L_TRIG,
			       (unsigned)k.R_TRIG, (unsigned)k.U_CBUTTON,
			       (unsigned)k.D_CBUTTON, (unsigned)k.L_CBUTTON,
			       (unsigned)k.R_CBUTTON, (unsigned)k.U_DPAD);
			fail = 1;
		}
	}

	for (i = 0; i < sizeof(dc_axis_cases) / sizeof(dc_axis_cases[0]); ++i) {
		const struct dc_axis_case *t = &dc_axis_cases[i];
		BUTTONS k;

		controller_DC_host_set(0, 0, t->jx, t->jy);
		controller_DC_host_set_triggers(0, 0, 0);
		memset(&k, 0, sizeof(k));
		getKeys(0, &k);

		if ((int)(signed char)k.X_AXIS != t->want_x ||
		    (int)(signed char)k.Y_AXIS != t->want_y) {
			dc_log(DC_LOG_ERROR,
			       "map smoke: %s raw(%d,%d) -> X=%d Y=%d want %d,%d",
			       t->what, t->jx, t->jy,
			       (int)(signed char)k.X_AXIS,
			       (int)(signed char)k.Y_AXIS,
			       t->want_x, t->want_y);
			fail = 1;
		}
	}

	/* Leave pad 0 as the other smokes expect to find it. */
	controller_DC_host_set_triggers(0, 0, 0);
	controller_DC_host_set(0, DC_CONT_A, 200, 80);
	{
		BUTTONS k;
		int lx = 0, ly = 0;

		memset(&k, 0, sizeof(k));
		getKeys(0, &k);
		if (!controller_DC_lastStick(0, &lx, &ly) || lx != 200 || ly != 80) {
			dc_log(DC_LOG_ERROR, "map smoke: last stick %d,%d want 200,80",
			       lx, ly);
			fail = 1;
		}
	}
	dc_log(fail ? DC_LOG_ERROR : DC_LOG_INFO,
	       "map smoke %s (%u button + %u analog cases)",
	       fail ? "FAIL" : "PASS",
	       (unsigned)(sizeof(dc_map_cases) / sizeof(dc_map_cases[0])),
	       (unsigned)(sizeof(dc_axis_cases) / sizeof(dc_axis_cases[0])));
	return fail;
}
#endif /* DC_HOST_STUB */

static int smoke_pif(void)
{
	int fail = 0;
	unsigned char native_cmd[8];
	unsigned char status_type = 0;

#ifdef DC_HOST_STUB
	controller_DC_host_set(0, DC_CONT_A, 200, 80);
#endif

	if (!Controls[0].Present) {
		dc_log(DC_LOG_ERROR, "pif smoke: pad 0 not Present");
		return 1;
	}

	memset(PIF_RAMb, 0, 0x40);
	PIF_RAMb[0] = 0x01;
	PIF_RAMb[1] = 0x03;
	PIF_RAMb[2] = 0x00;
	update_pif_write();
	status_type = PIF_RAMb[3];
	if (status_type != 0x05) {
		dc_log(DC_LOG_ERROR, "pif smoke: status type=0x%02x want 0x05",
		       status_type);
		fail = 1;
	}

	memset(PIF_RAMb, 0, 0x40);
	PIF_RAMb[0] = 0x01;
	PIF_RAMb[1] = 0x04;
	PIF_RAMb[2] = 0x01;
	update_pif_read();
	if (!(PIF_RAMb[3] & 1u)) {
		dc_log(DC_LOG_ERROR,
		       "pif smoke: buttons %02x %02x %02x %02x missing A",
		       PIF_RAMb[3], PIF_RAMb[4], PIF_RAMb[5], PIF_RAMb[6]);
		fail = 1;
	}
	/* Raw (200,80) scaled to the N64 range by controller-DC.c. */
	if ((signed char)PIF_RAMb[5] != 42 || (signed char)PIF_RAMb[6] != 26) {
		dc_log(DC_LOG_ERROR, "pif smoke: analog X=%d Y=%d want 42,26",
		       (int)(signed char)PIF_RAMb[5],
		       (int)(signed char)PIF_RAMb[6]);
		fail = 1;
	}

	memset(native_cmd, 0, sizeof(native_cmd));
	native_cmd[0] = 0x01;
	native_cmd[1] = 0x04;
	native_cmd[2] = 0x01;
	native_ReadController(0, native_cmd);
	if (!(native_cmd[3] & 1u)) {
		dc_log(DC_LOG_ERROR, "pif smoke: native_ReadController missing A");
		fail = 1;
	}

	dc_log(fail ? DC_LOG_ERROR : DC_LOG_INFO,
	       "pif smoke %s (status=0x%02x A=%u X=%d Y=%d)",
	       fail ? "FAIL" : "PASS",
	       status_type,
	       (unsigned)(PIF_RAMb[3] & 1u),
	       (int)(signed char)PIF_RAMb[5],
	       (int)(signed char)PIF_RAMb[6]);
	return fail;
}

/* The CPUTEST checks key off the ROM's decoded name, so a loader regression
 * used to skip them silently. When the caller asked for the CPUTEST image,
 * a name that does not decode is itself a failure. */
static int check_cputest(const char *path)
{
	int fail = 0;
	unsigned long got;
	const char *base = strrchr(path, '/');
	int want_cputest;

	base = base ? base + 1 : path;
	want_cputest = (strncmp(base, "dc_cputest", 10) == 0);

	if (strcmp(ROM_SETTINGS.goodname, "DC CPUTEST") != 0) {
		if (want_cputest) {
			dc_log(DC_LOG_ERROR,
			       "CPUTEST FAIL: %s decoded as '%s', expected 'DC CPUTEST'",
			       base, ROM_SETTINGS.goodname);
			return 1;
		}
		return 0;
	}

#define DC_EXPECT_REG(n, v) \
	do { \
		got = (unsigned long)(reg[(n)] & 0xffffffffu); \
		if (got != (unsigned long)(v)) { \
			dc_log(DC_LOG_ERROR, "CPUTEST r%d=0x%lx want 0x%x", (n), got, (v)); \
			fail = 1; \
		} \
	} while (0)

	DC_EXPECT_REG(1, 0x1234);
	DC_EXPECT_REG(2, 0x00FF);
	DC_EXPECT_REG(3, 0x1333);
	DC_EXPECT_REG(4, 0x12CB);
	DC_EXPECT_REG(5, 0x80000000u);
	DC_EXPECT_REG(6, 0x1333);
	DC_EXPECT_REG(7, 0xFF00);
	DC_EXPECT_REG(9, 0x0030);
	DC_EXPECT_REG(10, 0x1334);
	DC_EXPECT_REG(11, 0x0001);
	/* 32-bit shift/divide on a NEGATIVE operand. `long` is 32-bit on SH4 and
	 * PPC but 64-bit on an LP64 host, where SRL/SRLV/DIVU used to shift or
	 * divide a sign-extended 64-bit value and drag the high bits down. A
	 * positive operand passes either way, which is why this went unnoticed
	 * until a real ROM's boot checksum failed. */
	DC_EXPECT_REG(12, 0x95F208B7u);   /* lui+ori                       */
	DC_EXPECT_REG(13, 0x00000004);    /* shift amount                  */
	DC_EXPECT_REG(14, 0x095F208Bu);   /* srl  - logical, zero-filled   */
	DC_EXPECT_REG(15, 0x095F208Bu);   /* srlv - same, variable amount  */
	DC_EXPECT_REG(16, 0xF95F208Bu);   /* sra  - arithmetic, sign-filled*/
	DC_EXPECT_REG(17, 0xF95F208Bu);   /* srav                          */
	DC_EXPECT_REG(18, 0x00012340u);   /* sllv                          */
	DC_EXPECT_REG(19, 0xFFFFEDCCu);   /* subu r0 - 0x1234              */
	DC_EXPECT_REG(20, 0x257C822Du);   /* mflo: 0x95F208B7 / 4          */
	DC_EXPECT_REG(21, 0x00000003);    /* mfhi: remainder               */
#undef DC_EXPECT_REG

	/* Full 64-bit check. A 32-bit op whose result has bit 31 set must leave
	 * the register sign-extended; the low half alone looks right even when
	 * the high half is garbage, which is how the r4300/macros.h LP64 bug
	 * survived every low-32 check here. IPL3's checksum compares 64-bit
	 * registers with SLTU, so the high half is load-bearing. */
	if ((unsigned long long)reg[22] != 0xFFFFFFFFF208B700ull) {
		dc_log(DC_LOG_ERROR,
		       "CPUTEST r22=0x%016llx want 0xFFFFFFFFF208B700 "
		       "(32-bit result not sign-extended)",
		       (unsigned long long)reg[22]);
		fail = 1;
	}

	got = (unsigned long)(rdram[0] & 0xffffffffu);
	if (got != 0x1333u) {
		dc_log(DC_LOG_ERROR, "CPUTEST rdram[0]=0x%lx want 0x1333", got);
		fail = 1;
	}
	/* IPL spin; move these if gen_dc_roms.py changes the instruction count. */
	if (interp_addr != 0xa40000a8 && interp_addr != 0xa40000ac) {
		dc_log(DC_LOG_ERROR,
		       "CPUTEST interp_addr=0x%08lx (expected IPL BEQ spin)",
		       interp_addr);
		fail = 1;
	}

	/* The header's byte- and halfword-addressed fields only decode if the
	 * DC loader un-swapped them (rom_dc.c). Without that, isEEPROM16k(),
	 * saveregionstr() and GetVILimit() all read scrambled bytes. */
	if (ROM_HEADER.Cartridge_ID != 'DO') {
		dc_log(DC_LOG_ERROR, "CPUTEST Cartridge_ID=0x%04x want 0x%04x",
		       (unsigned)ROM_HEADER.Cartridge_ID, (unsigned)'DO');
		fail = 1;
	}
	if (ROM_HEADER.Country_code != 0x45) {
		dc_log(DC_LOG_ERROR, "CPUTEST Country_code=0x%02x want 0x45",
		       (unsigned)ROM_HEADER.Country_code);
		fail = 1;
	}
	if (ROM_HEADER.Version != 0x01) {
		dc_log(DC_LOG_ERROR, "CPUTEST Version=0x%02x want 0x01",
		       (unsigned)ROM_HEADER.Version);
		fail = 1;
	}
	if (!ROM_SETTINGS.isEEPROM16k) {
		dc_log(DC_LOG_ERROR,
		       "CPUTEST isEEPROM16k=0 want 1 ('DO'/'E' is in ROM_TABLE)");
		fail = 1;
	}

	dc_log(fail ? DC_LOG_ERROR : DC_LOG_INFO, "CPUTEST %s",
	       fail ? "FAIL" : "PASS");
	return fail;
}

static void gfx_info_init(void)
{
	gfx_info.MemoryBswaped = TRUE;
	gfx_info.HEADER = (BYTE*)&ROM_HEADER;
	gfx_info.RDRAM = (BYTE*)rdram;
	gfx_info.DMEM = (BYTE*)SP_DMEM;
	gfx_info.IMEM = (BYTE*)SP_IMEM;
	gfx_info.MI_INTR_REG = &(MI_register.mi_intr_reg);
	gfx_info.DPC_START_REG = &(dpc_register.dpc_start);
	gfx_info.DPC_END_REG = &(dpc_register.dpc_end);
	gfx_info.DPC_CURRENT_REG = &(dpc_register.dpc_current);
	gfx_info.DPC_STATUS_REG = &(dpc_register.dpc_status);
	gfx_info.DPC_CLOCK_REG = &(dpc_register.dpc_clock);
	gfx_info.DPC_BUFBUSY_REG = &(dpc_register.dpc_bufbusy);
	gfx_info.DPC_PIPEBUSY_REG = &(dpc_register.dpc_pipebusy);
	gfx_info.DPC_TMEM_REG = &(dpc_register.dpc_tmem);
	gfx_info.VI_STATUS_REG = &(vi_register.vi_status);
	gfx_info.VI_ORIGIN_REG = &(vi_register.vi_origin);
	gfx_info.VI_WIDTH_REG = &(vi_register.vi_width);
	gfx_info.VI_INTR_REG = &(vi_register.vi_v_intr);
	gfx_info.VI_V_CURRENT_LINE_REG = &(vi_register.vi_current);
	gfx_info.VI_TIMING_REG = &(vi_register.vi_burst);
	gfx_info.VI_V_SYNC_REG = &(vi_register.vi_v_sync);
	gfx_info.VI_H_SYNC_REG = &(vi_register.vi_h_sync);
	gfx_info.VI_LEAP_REG = &(vi_register.vi_leap);
	gfx_info.VI_H_START_REG = &(vi_register.vi_h_start);
	gfx_info.VI_V_START_REG = &(vi_register.vi_v_start);
	gfx_info.VI_V_BURST_REG = &(vi_register.vi_v_burst);
	gfx_info.VI_X_SCALE_REG = &(vi_register.vi_x_scale);
	gfx_info.VI_Y_SCALE_REG = &(vi_register.vi_y_scale);
	gfx_info.CheckInterrupts = check_interupt;
	initiateGFX(gfx_info);
}

static void audio_info_init(void)
{
	audio_info.MemoryBswaped = TRUE;
	audio_info.HEADER = (BYTE*)&ROM_HEADER;
	audio_info.RDRAM = (BYTE*)rdram;
	audio_info.DMEM = (BYTE*)SP_DMEM;
	audio_info.IMEM = (BYTE*)SP_IMEM;
	audio_info.MI_INTR_REG = &(MI_register.mi_intr_reg);
	audio_info.AI_DRAM_ADDR_REG = &(ai_register.ai_dram_addr);
	audio_info.AI_LEN_REG = &(ai_register.ai_len);
	audio_info.AI_CONTROL_REG = &(ai_register.ai_control);
	audio_info.AI_STATUS_REG = &(ai_register.ai_status);
	audio_info.AI_DACRATE_REG = &(ai_register.ai_dacrate);
	audio_info.AI_BITRATE_REG = &(ai_register.ai_bitrate);
	audio_info.CheckInterrupts = check_interupt;
	initiateAudio(audio_info);
}

static void rsp_info_init(void)
{
	static int cycle_count;
	rsp_info.MemoryBswaped = TRUE;
	rsp_info.RDRAM = (BYTE*)rdram;
	rsp_info.DMEM = (BYTE*)SP_DMEM;
	rsp_info.IMEM = (BYTE*)SP_IMEM;
	rsp_info.MI_INTR_REG = &MI_register.mi_intr_reg;
	rsp_info.SP_MEM_ADDR_REG = &sp_register.sp_mem_addr_reg;
	rsp_info.SP_DRAM_ADDR_REG = &sp_register.sp_dram_addr_reg;
	rsp_info.SP_RD_LEN_REG = &sp_register.sp_rd_len_reg;
	rsp_info.SP_WR_LEN_REG = &sp_register.sp_wr_len_reg;
	rsp_info.SP_STATUS_REG = &sp_register.sp_status_reg;
	rsp_info.SP_DMA_FULL_REG = &sp_register.sp_dma_full_reg;
	rsp_info.SP_DMA_BUSY_REG = &sp_register.sp_dma_busy_reg;
	rsp_info.SP_PC_REG = &rsp_register.rsp_pc;
	rsp_info.SP_SEMAPHORE_REG = &sp_register.sp_semaphore_reg;
	rsp_info.DPC_START_REG = &dpc_register.dpc_start;
	rsp_info.DPC_END_REG = &dpc_register.dpc_end;
	rsp_info.DPC_CURRENT_REG = &dpc_register.dpc_current;
	rsp_info.DPC_STATUS_REG = &dpc_register.dpc_status;
	rsp_info.DPC_CLOCK_REG = &dpc_register.dpc_clock;
	rsp_info.DPC_BUFBUSY_REG = &dpc_register.dpc_bufbusy;
	rsp_info.DPC_PIPEBUSY_REG = &dpc_register.dpc_pipebusy;
	rsp_info.DPC_TMEM_REG = &dpc_register.dpc_tmem;
	rsp_info.CheckInterrupts = check_interupt;
	rsp_info.ProcessDlistList = processDList;
	rsp_info.ProcessAlistList = processAList;
	rsp_info.ProcessRdpList = processRDPList;
	rsp_info.ShowCFB = showCFB;
	initiateRSP(rsp_info, (DWORD*)&cycle_count);
}

/*
 * After a run, say what the ROM actually reached. The Phase 4 gate is "a ROM
 * hits RDP/VI", so VI origin/width is the signal worth printing: non-zero
 * means the game has handed the video interface a framebuffer and a software
 * renderer now has something to draw.
 */
static void dump_run_state(void)
{
	dc_log(DC_LOG_INFO,
	       "  COP0   Count=0x%08lx Compare=0x%08lx Status=0x%08lx Cause=0x%08lx EPC=0x%08lx",
	       (unsigned long)(unsigned int)reg_cop0[9],
	       (unsigned long)(unsigned int)reg_cop0[11],
	       (unsigned long)(unsigned int)reg_cop0[12],
	       (unsigned long)(unsigned int)reg_cop0[13],
	       (unsigned long)(unsigned int)reg_cop0[14]);
	dc_log(DC_LOG_INFO,
	       "  COP0   BadVAddr=0x%08lx EntryHi=0x%08lx Index=0x%08lx Wired=0x%08lx",
	       (unsigned long)(unsigned int)reg_cop0[8],
	       (unsigned long)(unsigned int)reg_cop0[10],
	       (unsigned long)(unsigned int)reg_cop0[0],
	       (unsigned long)(unsigned int)reg_cop0[6]);
	dc_log(DC_LOG_INFO, "  MI     intr=0x%08lx mask=0x%08lx",
	       (unsigned long)MI_register.mi_intr_reg,
	       (unsigned long)MI_register.mi_intr_mask_reg);
	dc_log(DC_LOG_INFO,
	       "  VI     origin=0x%08lx width=%lu status=0x%08lx current=%lu",
	       (unsigned long)vi_register.vi_origin,
	       (unsigned long)vi_register.vi_width,
	       (unsigned long)vi_register.vi_status,
	       (unsigned long)vi_register.vi_current);
	dc_log(DC_LOG_INFO,
	       "  SP     status=0x%08lx  DPC start=0x%08lx end=0x%08lx current=0x%08lx",
	       (unsigned long)sp_register.sp_status_reg,
	       (unsigned long)dpc_register.dpc_start,
	       (unsigned long)dpc_register.dpc_end,
	       (unsigned long)dpc_register.dpc_current);
	dc_log(DC_LOG_INFO, "VERDICT: %s",
	       vi_register.vi_origin
		       ? "VI framebuffer set - ROM reached video (Phase 4 gate)"
		       : "VI origin still 0 - no framebuffer handed over yet");
}

static int load_and_step(const char *path, unsigned long steps)
{
	fileBrowser_file romfile;
	int ret;

	memset(&romfile, 0, sizeof(romfile));
	strncpy(romfile.name, path, FILE_BROWSER_MAX_PATH_LEN - 1);
	{
		FILE *fp = fopen(path, "rb");
		if (!fp) {
			dc_log(DC_LOG_ERROR, "ROM open failed: %s", path);
			return -1;
		}
		fseek(fp, 0, SEEK_END);
		romfile.size = (unsigned int)ftell(fp);
		fclose(fp);
	}

	fileBrowser_kos_bind();

	format_mempacks();
	reset_flashram();
	init_eeprom();
	TLBCache_init();

	ret = rom_read(&romfile);
	if (ret) {
		dc_log(DC_LOG_ERROR, "rom_read failed (%d)", ret);
		TLBCache_deinit();
		ROMCache_deinit();
		return ret;
	}

	hasLoadedROM = TRUE;
	dc_log(DC_LOG_INFO, "Loaded '%s' (%d bytes)",
	       ROM_SETTINGS.goodname, rom_length);

	init_memory();
	gfx_info_init();
	audio_info_init();
	init_controller_ts();
	control_info.MemoryBswaped = TRUE;
	control_info.HEADER = (BYTE*)&ROM_HEADER;
	control_info.Controls = Controls;
	{
		int i;
		for (i = 0; i < 4; i++) {
			Controls[i].Present = FALSE;
			Controls[i].RawData = FALSE;
			Controls[i].Plugin = PLUGIN_NONE;
		}
	}
	initiateControllers(control_info);
	auto_assign_controllers();
	rsp_info_init();
	romOpen_gfx();
	romOpen_audio();
	romOpen_input();

	dynacore = 2;
	cpu_init();
	dc_log(DC_LOG_INFO,
	       "Header name: '%s'  country=0x%02x  CIC_Chip=%lu  PC=0x%08x",
	       ROM_SETTINGS.goodname, ROM_HEADER.Country_code, CIC_Chip,
	       ROM_HEADER.PC);

#ifdef DC_HOST_STUB
	if (smoke_map()) {
		cpu_deinit();
		TLBCache_deinit();
		ROMCache_deinit();
		return 1;
	}
#endif
	if (smoke_io() || smoke_pif()) {
		cpu_deinit();
		TLBCache_deinit();
		ROMCache_deinit();
		return 1;
	}

	dc_interp_step_limit = steps;
	go();
	/* Report what was actually retired: echoing the limit hides an early
	 * exit (exception, unmapped fetch, NI opcode) as a clean finish. */
	dc_log(DC_LOG_INFO,
	       "Interpreter retired %lu of %lu steps (%s), interp_addr=0x%08lx stop=%d",
	       dc_interp_steps, steps,
	       (steps && dc_interp_steps >= steps) ? "hit step limit"
						   : "stopped early",
	       (unsigned long)(unsigned int)interp_addr, stop);
	dump_run_state();
	ret = check_cputest(path);
	cpu_deinit();
	TLBCache_deinit();
	ROMCache_deinit();
	return ret;
}

int main(int argc, char **argv)
{
	const char *rompath = "roms/dc_cputest.z64";
	unsigned long steps = DC_BRINGUP_STEPS;
	int fail = 0;
	char menu_rom[FILE_BROWSER_MAX_PATH_LEN];
	int want_menu = 0;

	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);

	dc_settings_defaults();
#ifdef DC_HOST_STUB
	/* Load cfg for the selftests, but do not log a session first — that
	 * used to leave a one-line banner in not64.log before the real tests. */
	if (argc > 1 && strcmp(argv[1], "--menu-test") == 0) {
		dc_settings_load(NULL);
		return dc_menu_selftest();
	}
#endif
	if (dc_settings_load(NULL) != 0)
		dc_log(DC_LOG_INFO, "settings: defaults (no cfg yet)");
	else
		dc_log(DC_LOG_INFO, "settings: loaded");
	if (dc_pvr_available())
		dc_log(DC_LOG_ERROR, "PVR: unexpected backend present");

#ifndef DC_HOST_STUB
	vid_set_mode(DM_640x480, PM_RGB565);
#endif

	print_budget();
	list_rom_dir();
	probe_controllers();
	if (probe_saves())
		fail = 1;

	if (argc > 1 && strcmp(argv[1], "--menu") == 0)
		want_menu = 1;
#ifndef DC_HOST_STUB
	/* Hardware: no argv ROM means the browser is the product. skipMenu
	 * keeps the dcload/serial bring-up path working. */
	if (argc <= 1 && !skipMenu)
		want_menu = 1;
#endif

	if (want_menu) {
		int picked = dc_menu_pick_rom(menu_rom, sizeof(menu_rom), 0);

		if (picked == DC_MENU_OK) {
			rompath = menu_rom;
			dc_log(DC_LOG_INFO, "menu picked %s", rompath);
		} else if (picked == DC_MENU_SKIP) {
			dc_log(DC_LOG_INFO, "skipMenu: using %s", rompath);
		} else {
			dc_log(DC_LOG_INFO, "menu: no ROM selected");
			return fail;
		}
	} else if (argc > 1)
		rompath = argv[1];
	/* Optional step budget: a real ROM needs far more than the bring-up
	 * default to get through IPL3. 0 means run until the ROM stops. */
	if (argc > 2 && !want_menu)
		steps = strtoul(argv[2], NULL, 0);

	dc_log(DC_LOG_INFO, "Phase 2/3: interpreter %lu steps using %s",
	       steps, rompath);
	if (load_and_step(rompath, steps))
		fail = 1;

	dc_debug_close();

#ifndef DC_HOST_STUB
	{
		int frames;
		for (frames = 0; frames < 60; ++frames)
			thd_sleep(16);
	}
#endif
	return fail;
}
