/**
 * Dreamcast / host entry: diagnostics, interpreter, CPU test, I/O smoke.
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#ifndef DC_HOST_STUB
#include <kos.h>
#include <dc/fb_console.h>
#endif

#include "../platform/dc_memory.h"
#include "../platform/dc_gfx.h"
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
#include "../platform/dc_menu/dc_menu.h"
#include "../platform/dc_menu/dc_draw.h"
#include "../gc_memory/pif.h"
#include "../gc_memory/flashram.h"

#ifndef DC_HOST_STUB
KOS_INIT_FLAGS(INIT_DEFAULT);
#ifdef DC_EMBED_VITEST
extern unsigned char romdisk[];
KOS_INIT_ROMDISK(romdisk);
#endif
#endif

extern unsigned long dc_interp_step_limit;
extern unsigned long dc_interp_steps;
extern BOOL hasLoadedROM;
extern void init_controller_ts(void);
extern void controller_DC_set_start_pulse(unsigned vi);
extern void auto_assign_controllers(void);
extern unsigned int audio_dc_buffered(void);
extern void native_ReadController(int Control, unsigned char *Command);

static const char *capture_path;
static GFX_INFO gfx_info;
static AUDIO_INFO audio_info;
static CONTROL_INFO control_info;
static RSP_INFO rsp_info;

static void print_budget(void)
{
	printf("Not64 Dreamcast bring-up\n");
	printf("  main RAM          %d MiB\n", DC_MAIN_RAM_SIZE / DC_MB);
	printf("  OS/code reserve   %d MiB\n", DC_OS_AND_CODE_RESERVE / DC_MB);
	printf("  N64 RDRAM         %d MiB\n", DC_N64_RDRAM_SIZE / DC_MB);
	printf("  ROM stream        %d MiB\n", DC_ROM_STREAM_SIZE / DC_MB);
	printf("  TLB/misc          %d KiB\n", DC_TLB_MISC_SIZE / DC_KB);
	printf("  tex cache         %d\n", DC_TEXCACHE_SIZE);
	printf("  audio ring        %d KiB\n", DC_AUDIO_RING_SIZE / DC_KB);
	printf("  heap remainder    %d bytes\n", DC_HEAP_REMAINDER);
}

static void list_rom_dir(void)
{
	fileBrowser_file *entries = NULL;
	int n, i;

	fileBrowser_kos_bind();
	if (romFile_init(romFile_topLevel) != 0) {
		printf("rom dir init failed: %s\n", romFile_topLevel->name);
		return;
	}

	n = romFile_readDir(romFile_topLevel, &entries);
	printf("ROM dir %s (%d entries)\n", romFile_topLevel->name, n);
	if (n < 0) {
		printf("  (create this folder and add .z64/.n64 dumps later)\n");
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
		printf("  %s%s\n", entries[i].name,
		       (entries[i].attr & FILE_BROWSER_ATTR_DIR) ? "/" : "");
	}
	if (n > 16)
		printf("  ... %d more\n", n - 16);
	free(entries);
	romFile_deinit(romFile_topLevel);
}

static void probe_controllers(void)
{
	int i;
	controller_DC.refreshAvailable();
	for (i = 0; i < 4; ++i)
		printf("Maple %d: %s\n", i,
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
		printf("save dir init failed: %s\n", saveFile_dir->name);
		return -1;
	}

	memset(&marker, 0, sizeof(marker));
	strncpy(marker.name, saveFile_dir->name, FILE_BROWSER_MAX_PATH_LEN - 16);
	marker.name[FILE_BROWSER_MAX_PATH_LEN - 16] = '\0';
	strcat(marker.name, "/dc_host.txt");
	n = saveFile_writeFile(&marker, (void *)msg, (unsigned int)strlen(msg));
	if (n < (int)strlen(msg)) {
		printf("save write failed (%d)\n", n);
		return -1;
	}
	marker.offset = 0;
	memset(buf, 0, sizeof(buf));
	n = saveFile_readFile(&marker, buf, sizeof(buf) - 1);
	printf("save %s (%d bytes): %s", marker.name, n, buf);
	saveFile_deinit(saveFile_dir);
	return n > 0 ? 0 : -1;
}

static int smoke_io(void)
{
	BUTTONS keys;
	int fail = 0;

#ifdef DC_HOST_STUB
	controller_DC_host_set(0, DC_CONT_A, 72, -48); /* A + analog */
#endif
	memset(&keys, 0, sizeof(keys));
	getKeys(0, &keys);
	printf("input0 A=%u X=%d Y=%d\n",
	       (unsigned)keys.A_BUTTON, (int)keys.X_AXIS, (int)keys.Y_AXIS);
#ifdef DC_HOST_STUB
	if (!keys.A_BUTTON) {
		printf("input smoke: expected A held on host inject\n");
		fail = 1;
	}
#endif

	ai_register.ai_dram_addr = 0;
	ai_register.ai_len = 64;
	memset(rdram, 0x5A, 64);
	aiLenChanged();
	printf("audio ring buffered %u bytes\n", audio_dc_buffered());
	if (audio_dc_buffered() < 64) {
		printf("audio smoke: ring did not accept AI DMA\n");
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
};

/* Stick scaling: raw Maple 0-255 -> N64 -80..+80 with a 10-count deadzone.
 * Full scale is asymmetric (-80 / +79) because 128 is centre in a 0-255
 * range: there are 128 counts below it and 127 above. */
struct dc_axis_case {
	const char *what;
	int jx, jy;
	int want_x, want_y;
};

/* Stick inputs are CENTRED, the way KOS's cont_state_t reports them: 0 at
 * rest, -128..+127 at the extremes. These used to be written as 0-255 with
 * 128 for centre, which is the raw Maple byte, not what the driver receives --
 * so the test agreed with the code and both were wrong. */
static const struct dc_axis_case dc_axis_cases[] = {
	{ "centre",              0,   0,   0,   0 },
	{ "inside deadzone",     7,  -7,   0,   0 },
	{ "just past deadzone", 11,   0,   1,   0 },
	{ "full left",        -128,   0, -80,   0 },
	{ "full right",        127,   0,  79,   0 },
	{ "full up",             0, -128,  0,  80 },
	{ "full down",           0, 127,   0, -79 },
	{ "host inject",        72, -48,  42,  26 },
};

static int smoke_map(void)
{
	unsigned int i;
	int fail = 0;

	for (i = 0; i < sizeof(dc_map_cases) / sizeof(dc_map_cases[0]); ++i) {
		const struct dc_map_case *t = &dc_map_cases[i];
		BUTTONS k;

		controller_DC_host_set(0, t->buttons, 0, 0);
		controller_DC_host_set_triggers(0, t->ltrig, t->rtrig);
		memset(&k, 0, sizeof(k));
		getKeys(0, &k);

		if (k.Z_TRIG != t->want_z || k.L_TRIG != t->want_l ||
		    k.R_TRIG != t->want_r ||
		    k.U_CBUTTON != t->want_cu || k.D_CBUTTON != t->want_cd ||
		    k.L_CBUTTON != t->want_cl || k.R_CBUTTON != t->want_cr ||
		    k.U_DPAD != t->want_du) {
			printf("map smoke: %s -> Z=%u L=%u R=%u C(u%u d%u l%u r%u) DU=%u\n",
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
			printf("map smoke: %s raw(%d,%d) -> X=%d Y=%d want %d,%d\n",
			       t->what, t->jx, t->jy,
			       (int)(signed char)k.X_AXIS,
			       (int)(signed char)k.Y_AXIS,
			       t->want_x, t->want_y);
			fail = 1;
		}
	}

	/* Leave pad 0 as the other smokes expect to find it. */
	controller_DC_host_set_triggers(0, 0, 0);
	controller_DC_host_set(0, DC_CONT_A, 72, -48);
	printf("map smoke %s (%u button + %u analog cases)\n",
	       fail ? "FAIL" : "PASS",
	       (unsigned)(sizeof(dc_map_cases) / sizeof(dc_map_cases[0])),
	       (unsigned)(sizeof(dc_axis_cases) / sizeof(dc_axis_cases[0])));
	return fail;
}
#endif /* DC_HOST_STUB */

static int smoke_tlbcache(void)
{
	/* A game maps a run of CONSECUTIVE pages, which was the worst case for
	 * the old high-bit hash: every page in the run landed in one bucket. */
	const unsigned int base = 0x00048;	/* the KUSEG page Phase 3.5 faults on */
	const unsigned int count = 512;
	unsigned int i, longest;
	int fail = 0;

	TLBCache_init();

	for (i = 0; i < count; ++i) {
		if (TLBCache_get_r(base + i) || TLBCache_get_w(base + i)) {
			printf("tlb cache smoke: page %x not empty after init\n", base + i);
			fail = 1;
			break;
		}
	}

	for (i = 0; i < count; ++i)
		TLBCache_set_r(base + i, 0x80000000u | ((0x100u + i) << 12));

	for (i = 0; i < count; ++i) {
		unsigned int want = 0x80000000u | ((0x100u + i) << 12);
		if (TLBCache_get_r(base + i) != want) {
			printf("tlb cache smoke: r page %x = %08x want %08x\n",
			       base + i, TLBCache_get_r(base + i), want);
			fail = 1;
			break;
		}
		/* r and w are independent tables. */
		if (TLBCache_get_w(base + i) != 0) {
			printf("tlb cache smoke: w page %x leaked from r\n", base + i);
			fail = 1;
			break;
		}
	}

	/* This is the property the hash exists for. With 512 consecutive pages
	 * in 1024 slots a chain over 8 means the key is clumping again. */
	longest = TLBCache_longest_chain();
	if (longest > 8) {
		printf("tlb cache smoke: %u consecutive pages -> longest chain %u\n",
		       count, longest);
		fail = 1;
	}

	/* Re-mapping a live page must update it, not shadow it. */
	TLBCache_set_r(base, 0x80042000u);
	if (TLBCache_get_r(base) != 0x80042000u) {
		printf("tlb cache smoke: re-map of page %x did not take\n", base);
		fail = 1;
	}

	/* Invalidation must unlink, not park a 0 in the chain forever. */
	for (i = 0; i < count; ++i)
		TLBCache_set_r(base + i, 0);
	for (i = 0; i < count; ++i) {
		if (TLBCache_get_r(base + i) != 0) {
			printf("tlb cache smoke: page %x still mapped after clear\n",
			       base + i);
			fail = 1;
			break;
		}
	}
	if (TLBCache_longest_chain() != 0) {
		/* longest_chain is a bucket depth, not a total: one tombstone per
		 * bucket still reports 1, which is the point -- it should be 0. */
		printf("tlb cache smoke: cleared table still has a chain of %u\n",
		       TLBCache_longest_chain());
		fail = 1;
	}

	/* Recycled nodes must still be usable. */
	for (i = 0; i < count; ++i)
		TLBCache_set_w(base + i, 0x80000000u | ((0x200u + i) << 12));
	for (i = 0; i < count; ++i) {
		unsigned int want = 0x80000000u | ((0x200u + i) << 12);
		if (TLBCache_get_w(base + i) != want) {
			printf("tlb cache smoke: w page %x = %08x want %08x after reuse\n",
			       base + i, TLBCache_get_w(base + i), want);
			fail = 1;
			break;
		}
	}

	TLBCache_init();
	if (TLBCache_longest_chain() != 0) {
		printf("tlb cache smoke: init left %u nodes\n",
		       TLBCache_longest_chain());
		fail = 1;
	}

	printf("tlb cache smoke %s (%u pages, longest chain %u of %u slots)\n",
	       fail ? "FAIL" : "PASS", count, longest, (unsigned)TLB_NUM_SLOTS);
	return fail;
}

/* The ROM cache pages 64 KiB blocks in and out of a 1 MiB window for the whole
 * run, so its eviction has to hand back the right bytes and has to keep the
 * blocks the game is actually using. Only a ROM bigger than the window
 * exercises it; the 4 KiB bring-up images do not.
 *
 * Host stub only, deliberately. The sweep below reads the whole cart, which
 * costs nothing from a host filesystem and would mean pulling 32 MiB off the
 * card before every single boot on hardware. */
#ifdef DC_HOST_STUB
static int smoke_romcache(void)
{
	enum { BLOCK = 64 * 1024, PROBE = 32 };
	const unsigned int win = DC_ROM_STREAM_SIZE / BLOCK;
	unsigned char first[PROBE], again[PROBE];
	unsigned long before, sweep_pageins;
	unsigned int i, total, probes = 0;
	int fail = 0;

	if (!ROMCache_streaming()) {
		printf("rom cache smoke SKIP (ROM fits the stream window)\n");
		return 0;
	}
	total = (unsigned int)rom_length / BLOCK;
	if (total < win + 16) {
		printf("rom cache smoke SKIP (ROM only %u blocks)\n", total);
		return 0;
	}

	/* 1. Bytes must survive a round trip through eviction. */
	ROMCache_read(first, 0, PROBE);
	for (i = 0; i * BLOCK + PROBE <= (unsigned int)rom_length; ++i) {
		ROMCache_read(again, i * BLOCK, PROBE);
		probes++;
	}
	sweep_pageins = ROMCache_pagein_count();
	ROMCache_read(again, 0, PROBE);
	if (memcmp(first, again, PROBE) != 0) {
		printf("rom cache smoke: offset 0 differs after eviction\n");
		fail = 1;
	}

	/* A sweep of N blocks through a smaller window pages in about N blocks. */
	if (sweep_pageins < probes / 2 || sweep_pageins > (unsigned long)probes * 2) {
		printf("rom cache smoke: %lu page-ins for a %u block sweep\n",
		       sweep_pageins, probes);
		fail = 1;
	}

	/* 2. The eviction POLICY. Fill the window oldest-first... */
	for (i = 0; i < win; ++i)
		ROMCache_read(again, i * BLOCK, PROBE);

	/* ...then touch one block outside it. That must cost exactly one
	 * page-in, and the victim must be block 0, the least recently used. */
	before = ROMCache_pagein_count();
	ROMCache_read(again, (win + 8) * BLOCK, PROBE);
	if (ROMCache_pagein_count() != before + 1) {
		printf("rom cache smoke: one miss caused %lu page-ins\n",
		       ROMCache_pagein_count() - before);
		fail = 1;
	}

	/* Blocks 1..win-1 were all used more recently than block 0, so every one
	 * of them must still be resident. This is what fails if eviction picks
	 * the newest block, or any block that is not the oldest. */
	before = ROMCache_pagein_count();
	for (i = 1; i < win; ++i)
		ROMCache_read(again, i * BLOCK, PROBE);
	if (ROMCache_pagein_count() != before) {
		printf("rom cache smoke: evicting one block cost %lu of the %u still in use\n",
		       ROMCache_pagein_count() - before, win - 1);
		fail = 1;
	}

	printf("rom cache smoke %s (%u blocks swept, %lu page-ins, %lu file opens)\n",
	       fail ? "FAIL" : "PASS", probes, sweep_pageins,
	       fileBrowser_kos_open_count());
	return fail;
}
#endif /* DC_HOST_STUB */

#ifdef DC_HOST_STUB
/* ---- menu (Phase 8a) ----------------------------------------------------
 * The host stub has no framebuffer, so the browser is tested by driving
 * dc_menu_step() with synthetic pad words and reading the character grid the
 * host draw backend fills in. That covers everything except the pixels: the
 * list, the filter, the sort, edge detection, auto-repeat, wrapping, the
 * scroll window, paging and the pick/cancel result.
 */

static dc_menu_action menu_frame(dc_menu_state *st, int u, int d,
				 int a, int b, int z, int r, int y)
{
	BUTTONS k;

	memset(&k, 0, sizeof(k));
	k.U_DPAD = u;
	k.D_DPAD = d;
	k.A_BUTTON = a;
	k.B_BUTTON = b;
	k.Z_TRIG = z;
	k.R_TRIG = r;
	k.Y_AXIS = y;
	return dc_menu_step(st, &k);
}

#define MENU_IDLE(st)  menu_frame((st), 0, 0, 0, 0, 0, 0, 0)
#define MENU_DOWN(st)  menu_frame((st), 0, 1, 0, 0, 0, 0, 0)
#define MENU_UP(st)    menu_frame((st), 1, 0, 0, 0, 0, 0, 0)

/* One press: a frame held, then a frame released, so the next press is an
 * edge again. */
static dc_menu_action menu_tap(dc_menu_state *st, int u, int d,
			       int a, int b, int z, int r)
{
	dc_menu_action act = menu_frame(st, u, d, a, b, z, r, 0);
	MENU_IDLE(st);
	return act;
}

static int smoke_menu(void)
{
	dc_menu_list  synth;
	dc_menu_entry items[20];
	dc_menu_state st;
	dc_menu_list  real;
	int fail = 0, i, n;

	/* --- a synthetic list, so movement does not depend on ./roms --- */
	for (i = 0; i < 20; ++i) {
		memset(&items[i], 0, sizeof(items[i]));
		snprintf(items[i].label, sizeof(items[i].label), "rom%02d", i);
		snprintf(items[i].path, sizeof(items[i].path), "./roms/rom%02d.z64", i);
		items[i].size = (unsigned int)(i + 1) * 1024u * 1024u;
	}
	memset(&synth, 0, sizeof(synth));
	synth.items = items;
	synth.count = 20;
	dc_menu_state_init(&st, &synth, 5);

	/* A held direction must move once, not once per polled frame. */
	MENU_DOWN(&st);
	if (st.cursor != 1) {
		printf("menu smoke: first press moved to %d, want 1\n", st.cursor);
		fail = 1;
	}
	for (i = 0; i < DC_MENU_REPEAT_FIRST; ++i)
		MENU_DOWN(&st);
	if (st.cursor != 2) {
		printf("menu smoke: %d frames held moved to %d, want 2\n",
		       DC_MENU_REPEAT_FIRST + 1, st.cursor);
		fail = 1;
	}
	MENU_IDLE(&st);

	/* The window follows the cursor: 20 entries, 5 rows. */
	dc_menu_state_init(&st, &synth, 5);
	for (i = 0; i < 5; ++i)
		menu_tap(&st, 0, 1, 0, 0, 0, 0);
	if (st.cursor != 5 || st.top != 1) {
		printf("menu smoke: after 5 downs cursor=%d top=%d, want 5/1\n",
		       st.cursor, st.top);
		fail = 1;
	}

	/* Up from the top wraps to the end, and the window follows. */
	dc_menu_state_init(&st, &synth, 5);
	menu_tap(&st, 1, 0, 0, 0, 0, 0);
	if (st.cursor != 19 || st.top != 15) {
		printf("menu smoke: wrap up gave cursor=%d top=%d, want 19/15\n",
		       st.cursor, st.top);
		fail = 1;
	}

	/* Triggers page by a screenful and clamp at the ends. */
	dc_menu_state_init(&st, &synth, 5);
	menu_tap(&st, 0, 0, 0, 0, 0, 1);
	if (st.cursor != 5) {
		printf("menu smoke: page down gave %d, want 5\n", st.cursor);
		fail = 1;
	}
	for (i = 0; i < 10; ++i)
		menu_tap(&st, 0, 0, 0, 0, 0, 1);
	if (st.cursor != 19) {
		printf("menu smoke: paging past the end gave %d, want 19\n", st.cursor);
		fail = 1;
	}
	for (i = 0; i < 10; ++i)
		menu_tap(&st, 0, 0, 0, 0, 1, 0);
	if (st.cursor != 0 || st.top != 0) {
		printf("menu smoke: paging past the start gave %d/%d, want 0/0\n",
		       st.cursor, st.top);
		fail = 1;
	}

	/* The analog stick drives the cursor too, past its threshold only. */
	dc_menu_state_init(&st, &synth, 5);
	menu_frame(&st, 0, 0, 0, 0, 0, 0, -(DC_MENU_STICK_ON - 1));
	if (st.cursor != 0) {
		printf("menu smoke: stick under threshold moved to %d\n", st.cursor);
		fail = 1;
	}
	menu_frame(&st, 0, 0, 0, 0, 0, 0, -DC_MENU_STICK_ON);
	if (st.cursor != 1) {
		printf("menu smoke: stick at threshold gave %d, want 1\n", st.cursor);
		fail = 1;
	}

	/* A picks once per press, B cancels, and a held A does not re-pick. */
	dc_menu_state_init(&st, &synth, 5);
	if (menu_frame(&st, 0, 0, 1, 0, 0, 0, 0) != DC_MENU_PICK) {
		printf("menu smoke: A did not pick\n");
		fail = 1;
	}
	if (menu_frame(&st, 0, 0, 1, 0, 0, 0, 0) != DC_MENU_NONE) {
		printf("menu smoke: held A picked twice\n");
		fail = 1;
	}
	MENU_IDLE(&st);
	if (menu_frame(&st, 0, 0, 0, 1, 0, 0, 0) != DC_MENU_CANCEL) {
		printf("menu smoke: B did not cancel\n");
		fail = 1;
	}

	/* An empty list must not pick anything or move. */
	{
		dc_menu_list empty;
		memset(&empty, 0, sizeof(empty));
		dc_menu_state_init(&st, &empty, 5);
		if (menu_frame(&st, 0, 0, 1, 0, 0, 0, 0) != DC_MENU_NONE) {
			printf("menu smoke: empty list picked\n");
			fail = 1;
		}
		menu_tap(&st, 0, 1, 0, 0, 0, 0);
		if (st.cursor != 0 || st.top != 0) {
			printf("menu smoke: empty list moved to %d/%d\n",
			       st.cursor, st.top);
			fail = 1;
		}
	}

	/* What the browser actually draws: the marker on the selected row, the
	 * label, and the size. */
	dc_menu_state_init(&st, &synth, 5);
	menu_tap(&st, 0, 1, 0, 0, 0, 0);
	dc_draw_init();
	dc_menu_draw(&st, "Not64 test");
	{
		const char *row = dc_draw_host_row(3);	/* title, blank, 0, 1 */
		if (!row || row[1] != '>' || !strstr(row, "rom01") ||
		    !strstr(row, "2 MiB")) {
			printf("menu smoke: selected row drew as '%s'\n",
			       row ? row : "(null)");
			fail = 1;
		}
		row = dc_draw_host_row(2);
		if (!row || row[1] == '>') {
			printf("menu smoke: marker also on the unselected row\n");
			fail = 1;
		}
	}
	dc_draw_shutdown();

	/* --- the real directory: filter and sort --- */
	n = dc_menu_list_load(&real, romFile_topLevel);
	if (n < 0) {
		printf("menu smoke: list load failed (%d)\n", n);
		fail = 1;
	} else {
		for (i = 0; i < n; ++i) {
			const char *lbl = real.items[i].label;
			size_t len = strlen(lbl);
			if (len < 4 || (strcmp(lbl + len - 4, ".z64") &&
					strcmp(lbl + len - 4, ".n64") &&
					strcmp(lbl + len - 4, ".v64"))) {
				printf("menu smoke: kept a non-ROM entry '%s'\n", lbl);
				fail = 1;
				break;
			}
			if (i > 0 && strcmp(real.items[i - 1].label, lbl) > 0) {
				printf("menu smoke: '%s' sorts after '%s'\n",
				       real.items[i - 1].label, lbl);
				fail = 1;
				break;
			}
		}
		if (n < 2) {
			printf("menu smoke: expected the two bring-up ROMs, saw %d\n", n);
			fail = 1;
		}
	}
	dc_menu_list_free(&real);
	if (real.items != NULL || real.count != 0) {
		printf("menu smoke: free left the list populated\n");
		fail = 1;
	}

	/* --- the whole browser, through the real getKeys() path --- */
	if (n > 0) {
		dc_menu_entry choice;
		int picked;

		controller_DC_host_set_triggers(0, 0, 0);
		controller_DC_host_set(0, DC_CONT_A, 0, 0);
		memset(&choice, 0, sizeof(choice));
		picked = dc_menu_run(romFile_topLevel, &choice);
		if (picked != 1) {
			printf("menu smoke: dc_menu_run returned %d, want 1\n", picked);
			fail = 1;
		} else if (choice.path[0] == '\0') {
			printf("menu smoke: dc_menu_run picked an empty path\n");
			fail = 1;
		} else {
			FILE *fp = fopen(choice.path, "rb");
			if (!fp) {
				printf("menu smoke: picked '%s', which does not open\n",
				       choice.path);
				fail = 1;
			} else {
				fclose(fp);
			}
		}
	}

	/* Leave pad 0 where smoke_map left it: the I/O and PIF smokes read it. */
	controller_DC_host_set_triggers(0, 0, 0);
	controller_DC_host_set(0, DC_CONT_A, 72, -48);

	printf("menu smoke %s (%d ROMs listed)\n", fail ? "FAIL" : "PASS", n);
	return fail;
}
#endif /* DC_HOST_STUB */

static int smoke_pif(void)
{
	int fail = 0;
	unsigned char native_cmd[8];
	unsigned char status_type = 0;

#ifdef DC_HOST_STUB
	controller_DC_host_set(0, DC_CONT_A, 72, -48);
#endif

	if (!Controls[0].Present) {
		printf("pif smoke: pad 0 not Present\n");
		return 1;
	}

	memset(PIF_RAMb, 0, 0x40);
	PIF_RAMb[0] = 0x01;
	PIF_RAMb[1] = 0x03;
	PIF_RAMb[2] = 0x00;
	update_pif_write();
	status_type = PIF_RAMb[3];
	if (status_type != 0x05) {
		printf("pif smoke: status type=0x%02x want 0x05\n", status_type);
		fail = 1;
	}

	memset(PIF_RAMb, 0, 0x40);
	PIF_RAMb[0] = 0x01;
	PIF_RAMb[1] = 0x04;
	PIF_RAMb[2] = 0x01;
	update_pif_read();
	if (!(PIF_RAMb[3] & 0x80u)) {
		printf("pif smoke: buttons %02x %02x %02x %02x missing A\n",
		       PIF_RAMb[3], PIF_RAMb[4], PIF_RAMb[5], PIF_RAMb[6]);
		fail = 1;
	}
	/* Centered (72,-48) scaled to the N64 range by controller-DC.c. */
	if ((signed char)PIF_RAMb[5] != 42 || (signed char)PIF_RAMb[6] != 26) {
		printf("pif smoke: analog X=%d Y=%d want 42,26\n",
		       (int)(signed char)PIF_RAMb[5],
		       (int)(signed char)PIF_RAMb[6]);
		fail = 1;
	}

	memset(native_cmd, 0, sizeof(native_cmd));
	native_cmd[0] = 0x01;
	native_cmd[1] = 0x04;
	native_cmd[2] = 0x01;
	native_ReadController(0, native_cmd);
	if (!(native_cmd[3] & 0x80u)) {
		printf("pif smoke: native_ReadController missing A\n");
		fail = 1;
	}

#ifdef DC_HOST_STUB
    /* Check the wire packet, not the compiler's native bitfield layout. */
    controller_DC_host_set(0, DC_CONT_START, 0, 0);
    native_ReadController(0, native_cmd);
    if (native_cmd[3] != 0x10 || native_cmd[4] || native_cmd[5] || native_cmd[6]) {
        printf("pif smoke: Start packet has wrong Joybus bit order\n");
        fail = 1;
    }
    controller_DC_host_set(0, 0, 0, 0);
#endif

	printf("pif smoke %s (status=0x%02x A=%u X=%d Y=%d)\n",
	       fail ? "FAIL" : "PASS",
	       status_type,
	       (unsigned)!!(PIF_RAMb[3] & 0x80u),
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
			printf("CPUTEST FAIL: %s decoded as '%s', "
			       "expected 'DC CPUTEST'\n",
			       base, ROM_SETTINGS.goodname);
			return 1;
		}
		return 0;
	}

#define DC_EXPECT_REG(n, v) \
	do { \
		got = (unsigned long)(reg[(n)] & 0xffffffffu); \
		if (got != (unsigned long)(v)) { \
			printf("CPUTEST r%d=0x%lx want 0x%x\n", (n), got, (v)); \
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
		printf("CPUTEST r22=0x%016llx want 0xFFFFFFFFF208B700 "
		       "(32-bit result not sign-extended)\n",
		       (unsigned long long)reg[22]);
		fail = 1;
	}

	got = (unsigned long)(rdram[0] & 0xffffffffu);
	if (got != 0x1333u) {
		printf("CPUTEST rdram[0]=0x%lx want 0x1333\n", got);
		fail = 1;
	}
	/* IPL spin; move these if gen_dc_roms.py changes the instruction count. */
	if (interp_addr != 0xa40000a8 && interp_addr != 0xa40000ac) {
		printf("CPUTEST interp_addr=0x%08lx (expected IPL BEQ spin)\n",
		       interp_addr);
		fail = 1;
	}

	/* The header's byte- and halfword-addressed fields only decode if the
	 * DC loader un-swapped them (rom_dc.c). Without that, isEEPROM16k(),
	 * saveregionstr() and GetVILimit() all read scrambled bytes. */
	if (ROM_HEADER.Cartridge_ID != 'DO') {
		printf("CPUTEST Cartridge_ID=0x%04x want 0x%04x\n",
		       (unsigned)ROM_HEADER.Cartridge_ID, (unsigned)'DO');
		fail = 1;
	}
	if (ROM_HEADER.Country_code != 0x45) {
		printf("CPUTEST Country_code=0x%02x want 0x45\n",
		       (unsigned)ROM_HEADER.Country_code);
		fail = 1;
	}
	if (ROM_HEADER.Version != 0x01) {
		printf("CPUTEST Version=0x%02x want 0x01\n",
		       (unsigned)ROM_HEADER.Version);
		fail = 1;
	}
	if (!ROM_SETTINGS.isEEPROM16k) {
		printf("CPUTEST isEEPROM16k=0 want 1 ('DO'/'E' is in ROM_TABLE)\n");
		fail = 1;
	}

	printf("CPUTEST %s\n", fail ? "FAIL" : "PASS");
	return fail;
}

/* Validate CPU-produced pixels through the same bounded accessor intended for
 * VI scanout. This is a framebuffer fixture, not a renderer test yet. */
static int check_vitest(void)
{
    static const uint16_t colors[8] = {
        0xffff, 0xffc1, 0x07ff, 0x07c1, 0xf83f, 0xf801, 0x003f, 0x0001
    };
    uint32_t complete;
    unsigned x, y;
    if (strcmp(ROM_SETTINGS.goodname, "DC VITEST") != 0) return 0;
    if (!dc_gfx_read_u32(0x200, &complete) || complete != 0x56495445 ||
        vi_register.vi_origin != 0x10000 || vi_register.vi_width != 320 ||
        vi_register.vi_status != 2 || vi_register.vi_v_sync != 525 ||
        vi_register.vi_h_start != ((108u << 16) | 748u) ||
        vi_register.vi_v_start != ((37u << 16) | 517u) ||
        vi_register.vi_x_scale != 512 || vi_register.vi_y_scale != 1024) {
        fprintf(stderr, "VITEST FAIL: incomplete frame or incorrect VI registers\n");
        return 1;
    }
    for (y = 0; y < 240; ++y) for (x = 0; x < 320; ++x) {
        uint16_t actual, expected = colors[x / 40];
        if (y == 0 && x == 0) expected = 0xf801;
        if (y == 0 && x == 319) expected = 0x07c1;
        if (y == 239 && x == 0) expected = 0x003f;
        if (y == 239 && x == 319) expected = 0xffff;
        if (!dc_gfx_read_u16(0x10000 + (y * 320 + x) * 2, &actual) || actual != expected) {
            fprintf(stderr, "VITEST FAIL: pixel %u,%u\n", x, y);
            return 1;
        }
    }
    if (!dc_gfx_get_stats().presented) {
        fprintf(stderr, "VITEST FAIL: no converted frame presented\n");
        return 1;
    }
    puts("VITEST PASS (76800 CPU-written pixels + VI scanout)");
    return 0;
}

#ifdef DC_EMBED_VITEST
/* Exercise the real target presenters without rerunning the emulated CPU.
 * Alternate ROM close/reopen with complete plugin destruction/reinitialization. */
static int stress_video(void)
{
    unsigned cycle, n;
    const unsigned cycles = 8;
    uint64_t total_us = 0;
#ifdef DC_VIDEO_PVR
    size_t free_vram = pvr_mem_available();
#endif
    dc_gfx_set_frame_limit(0);
    for (cycle = 0; cycle < cycles; ++cycle) {
        dc_gfx_stats stats;
        romClosed_gfx();
        romClosed_gfx(); /* Idempotent release must not free twice. */
        if (dc_gfx_is_open() || dc_gfx_get_frame()) return 1;
        if (cycle & 1) {
            closeDLL_gfx();
            if (!initiateGFX(gfx_info)) return 1;
        }
        romOpen_gfx();
        if (!dc_gfx_is_open()) return 1;
#ifdef DC_VIDEO_PVR
        if (pvr_mem_available() != free_vram) {
            puts("VIDEO STRESS FAIL: PVR allocation changed across reopen");
            return 1;
        }
#endif
        stop = 0;
        vi_register.vi_status = 0;
        updateScreen();
        vi_register.vi_status = 2;
        updateScreen();
        vi_register.vi_origin = DC_N64_RDRAM_SIZE;
        updateScreen();
        vi_register.vi_origin = 0x10000;
        updateScreen();
        vi_register.vi_status = 1;
        updateScreen();
        vi_register.vi_status = 2;
        updateScreen();
        for (n = 0; n < 16; ++n) {
            uint64_t begin = timer_us_gettime64();
            updateScreen();
            total_us += timer_us_gettime64() - begin;
        }
        stats = dc_gfx_get_stats();
        if (stop || stats.present_failures || stats.presented != 19 ||
            stats.vi_updates != 22 || stats.blanked != 1 ||
            stats.invalid_vi != 1 || stats.unsupported_vi != 1 || check_vitest()) {
            printf("VIDEO STRESS FAIL: cycle=%u\n", cycle + 1);
            return 1;
        }
    }
    printf("VIDEO STRESS PASS: backend=%s cycles=%u valid-frames=%u scanout-present-avg-us=%lu\n",
           dc_gfx_backend_name(), cycles, cycles * 19,
           (unsigned long)(total_us / (cycles * 16)));
    return 0;
}
#endif

static BOOL gfx_info_init(void)
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
	return initiateGFX(gfx_info);
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
	printf("  COP0   Count=0x%08lx Compare=0x%08lx Status=0x%08lx Cause=0x%08lx EPC=0x%08lx\n",
	       (unsigned long)(unsigned int)reg_cop0[9],
	       (unsigned long)(unsigned int)reg_cop0[11],
	       (unsigned long)(unsigned int)reg_cop0[12],
	       (unsigned long)(unsigned int)reg_cop0[13],
	       (unsigned long)(unsigned int)reg_cop0[14]);
	printf("  COP0   BadVAddr=0x%08lx EntryHi=0x%08lx Index=0x%08lx Wired=0x%08lx\n",
	       (unsigned long)(unsigned int)reg_cop0[8],
	       (unsigned long)(unsigned int)reg_cop0[10],
	       (unsigned long)(unsigned int)reg_cop0[0],
	       (unsigned long)(unsigned int)reg_cop0[6]);
	printf("  MI     intr=0x%08lx mask=0x%08lx\n",
	       (unsigned long)MI_register.mi_intr_reg,
	       (unsigned long)MI_register.mi_intr_mask_reg);
	printf("  VI     origin=0x%08lx width=%lu status=0x%08lx current=%lu\n",
	       (unsigned long)vi_register.vi_origin,
	       (unsigned long)vi_register.vi_width,
	       (unsigned long)vi_register.vi_status,
	       (unsigned long)vi_register.vi_current);
	printf("  SP     status=0x%08lx  DPC start=0x%08lx end=0x%08lx current=0x%08lx\n",
	       (unsigned long)sp_register.sp_status_reg,
	       (unsigned long)dpc_register.dpc_start,
	       (unsigned long)dpc_register.dpc_end,
	       (unsigned long)dpc_register.dpc_current);
	printf("  VERDICT: %s\n",
	       vi_register.vi_origin
		       ? "VI framebuffer set - ROM reached video (Phase 4 gate)"
		       : "VI origin still 0 - no framebuffer handed over yet");
}

/* Bring the controller layer up. assign_controller() writes through
 * control_info.Controls, so that pointer has to be set before anything is
 * assigned -- otherwise the first getKeys() dereferences NULL. The Phase 8
 * menu polls the pad before a ROM exists, so this runs once with no header
 * for the browser and again with the header once the ROM is loaded. */
static void init_controllers(void *header)
{
	int i;

	init_controller_ts();
	control_info.MemoryBswaped = TRUE;
	control_info.HEADER = (BYTE *)header;
	control_info.Controls = Controls;
	for (i = 0; i < 4; i++) {
		Controls[i].Present = FALSE;
		Controls[i].RawData = FALSE;
		Controls[i].Plugin = PLUGIN_NONE;
	}
	initiateControllers(control_info);
	auto_assign_controllers();
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
			printf("ROM open failed: %s\n", path);
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
		printf("rom_read failed (%d)\n", ret);
		TLBCache_deinit();
		ROMCache_deinit();
		return ret;
	}

	hasLoadedROM = TRUE;
	printf("Loaded '%s' (%d bytes)\n",
	       ROM_SETTINGS.goodname, rom_length);

	init_memory();
	if (!gfx_info_init()) {
		fprintf(stderr, "Graphics initialization failed\n");
		closeDLL_gfx();
		TLBCache_deinit();
		ROMCache_deinit();
		hasLoadedROM = FALSE;
		return 1;
	}
	audio_info_init();
	init_controllers(&ROM_HEADER);
	rsp_info_init();
	romOpen_gfx();
	if (!dc_gfx_is_open()) {
		fprintf(stderr, "Graphics open failed\n");
		closeDLL_gfx(); TLBCache_deinit(); ROMCache_deinit();
		hasLoadedROM = FALSE;
		return 1;
	}
	romOpen_audio();
	romOpen_input();

	dynacore = 2;
	cpu_init();
	printf("Header name: '%s'  country=0x%02x  CIC_Chip=%lu  PC=0x%08x\n",
	       ROM_SETTINGS.goodname, ROM_HEADER.Country_code, CIC_Chip, ROM_HEADER.PC);

#ifdef DC_HOST_STUB
	if (!strncmp(ROM_SETTINGS.goodname, "DC ", 3) && (smoke_map() || smoke_menu() || smoke_tlbcache() || smoke_romcache() || smoke_io() || smoke_pif())) {
		romClosed_gfx();
		closeDLL_gfx();
		cpu_deinit();
		TLBCache_deinit();
		ROMCache_deinit();
		hasLoadedROM = FALSE;
		return 1;
	}

#endif
	dc_interp_step_limit = steps;
	go();
	printf("Interpreter stopped (budget=%lu interp_addr=0x%08lx stop=%d)\n",
	       steps, interp_addr, stop);
	ret = check_cputest(path) | check_vitest();
	{
		dc_gfx_stats stats = dc_gfx_get_stats();
		printf("Graphics %s: VI=%lu presented=%lu DList=%lu RDP=%lu invalid=%lu unsupported=%lu failures=%lu\n",
               dc_gfx_backend_name(), (unsigned long)stats.vi_updates,
               (unsigned long)stats.presented, (unsigned long)stats.display_lists,
               (unsigned long)stats.rdp_lists, (unsigned long)stats.invalid_vi,
               (unsigned long)stats.unsupported_vi, (unsigned long)stats.present_failures);
#ifdef DC_SOFT_GFX
        printf("Software graphics: decode-failures=%lu\n", (unsigned long)stats.decode_failures);
#endif
        if (stats.present_failures || stats.decode_failures) ret = 1;
	}
    if (capture_path && !dc_gfx_capture_ppm(capture_path)) {
        fprintf(stderr, "Framebuffer capture failed: %s\n", capture_path);
        ret = 1;
    }
#ifdef DC_EMBED_VITEST
    if (!ret && stress_video()) ret = 1;
#endif
#ifndef DC_HOST_STUB
    /* Keep the last submitted image visible before restoring console video. */
#if defined(DC_EMBED_VITEST) || defined(DC_GAME_DISC)
    /* Leave the diagnostic result visible for emulator/hardware inspection. */
    thd_sleep(60000);
#else
    thd_sleep(1000);
#endif
#endif
	romClosed_gfx();
	closeDLL_gfx();
	cpu_deinit();
	TLBCache_deinit();
	ROMCache_deinit();
	hasLoadedROM = FALSE;
	return ret;
}

static int positive_number(const char *text, unsigned long *value)
{
    char *end;
    errno = 0;
    *value = strtoul(text, &end, 10);
    return !errno && text[0] && strspn(text, "0123456789") == strlen(text) && !*end && *value;
}

int main(int argc, char **argv)
{
    const char *rompath = "roms/dc_cputest.z64";
    int fail = 0, arg = 1;
    dc_menu_entry choice;
    skipMenu = 1;
#if !defined(DC_HOST_STUB) && !defined(DC_EMBED_VITEST) && !defined(DC_GAME_DISC)
    skipMenu = argc > 1;
#endif
    unsigned long steps = 10000, frames = 0;
#ifdef DC_GAME_DISC
    rompath = "/cd/game.z64";
    steps = 500000000;
    controller_DC_set_start_pulse(400);
#endif
#ifdef DC_EMBED_VITEST
    rompath = "/rd/roms/dc_vitest.z64";
    steps = 300000;
#endif
#ifndef DC_HOST_STUB
    frames = 120; /* Prevent an idle ROM presenting thousands of real frames. */
#ifdef DC_GAME_DISC
    frames = 600;
#endif
#endif
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    if (arg < argc && !strcmp(argv[arg], "--menu")) {
        skipMenu = 0;
        arg++;
    }
    if (arg < argc && argv[arg][0] != '-') {
        rompath = argv[arg++];
        skipMenu = 1;
    }
    if (arg < argc && argv[arg][0] != '-') {
        if (!positive_number(argv[arg++], &steps)) goto usage;
    }
    while (arg < argc) {
        if (!strcmp(argv[arg], "--frames") && arg + 1 < argc) {
            if (!positive_number(argv[arg + 1], &frames)) goto usage;
            arg += 2;
        } else if (!strcmp(argv[arg], "--start-at") && arg + 1 < argc) {
            unsigned long start_vi;
            if (!positive_number(argv[arg + 1], &start_vi)) goto usage;
            controller_DC_set_start_pulse((unsigned)start_vi);
            arg += 2;
        } else if (!strcmp(argv[arg], "--capture") && arg + 1 < argc) {
            capture_path = argv[arg + 1];
            arg += 2;
        } else goto usage;
    }
    dc_gfx_set_frame_limit(frames);
#ifndef DC_HOST_STUB
    /* A disc has no resident loader to return to after main exits. */
    arch_set_exit_path(ARCH_EXIT_MENU);
    vid_set_mode(DM_640x480, PM_RGB565);
#if !defined(DC_EMBED_VITEST) && !defined(DC_GAME_DISC)
    if (!skipMenu) dbgio_dev_select("fb");
#endif
#endif
    print_budget();
    list_rom_dir();
    probe_controllers();
#if !defined(DC_EMBED_VITEST) && !defined(DC_GAME_DISC)
    if (probe_saves()) fail = 1;
#endif
	if (!skipMenu) {
		int picked;

		fileBrowser_kos_bind();
		/* No ROM yet, so no header; load_and_step() redoes this with one. */
		init_controllers(NULL);

		picked = dc_menu_run(romFile_topLevel, &choice);
#ifdef DC_HOST_STUB
		/* No framebuffer here, so print the last screen the browser drew.
		 * It is the only way to see the menu without a Dreamcast. */
		{
			int row;
			printf("--- menu screen (%dx%d chars) ---\n",
			       dc_draw_width() / dc_draw_char_w(),
			       dc_draw_host_rows());
			for (row = 0; row < dc_draw_host_rows(); ++row) {
				const char *text = dc_draw_host_row(row);
				if (text)
					printf("|%s|\n", text);
			}
			printf("--- end menu screen ---\n");
		}
#endif
		if (picked < 0) {
			printf("menu failed (%d)\n", picked);
			return 1;
		}
		if (picked == 0) {
			printf("menu cancelled\n");
			return 0;
		}
		rompath = choice.path;
	}

    printf("Dreamcast interpreter: %lu steps using %s\n", steps, rompath);
    if (load_and_step(rompath, steps)) fail = 1;
#ifndef DC_HOST_STUB
#if defined(DC_EMBED_VITEST) || defined(DC_GAME_DISC)
    /* ReIOS boots a mounted disc again when asked for the BIOS menu. All
     * emulator and graphics resources are closed; keep only KOS alive so
     * a bounded diagnostic cannot reenter startup with stale disc state. */
    printf("Dreamcast run complete: status=%d; diagnostic idle (close emulator to exit)\n", fail);
    for (;;) thd_sleep(1000);
#else
    printf("Dreamcast run complete: status=%d; exit=system-menu\n", fail);
#endif
#endif
    return fail;
usage:
    fprintf(stderr, "Usage: %s [--menu] [rom.z64 [positive-step-budget]] [--frames positive-count] [--capture output.ppm] [--start-at VI]\n", argv[0]);
    return 1;
}
