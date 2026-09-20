/**
 * In-game pause overlay (Phase 8b).
 *
 * Wii64: GetKeys exit combo sets stop and returns to libgui.
 * mupen64plus: CurrentStateSlot 0–9, pause from the front-end.
 * Flycast: Start opens a small in-game menu over the current frame.
 *
 * Save/Load write and restore `saves/<goodname>.stN` (Phase 8d/P4). Reset
 * and Return-to-menu stop the interpreter so main() can reload or show the
 * ROM browser. Load failures surface savestates_error() (wrong ROM,
 * truncated, missing).
 */

#include "dc_overlay.h"
#include "dc_menu/dc_draw.h"
#include "dc_menu/dc_menu.h"
#include "dc_debug.h"
#include "../main/savestates.h"
#include <stdio.h>
#include <string.h>

extern int stop;

#define COL_BG    DC_RGB(0x10, 0x14, 0x20)
#define COL_BAR   DC_RGB(0x28, 0x50, 0x90)
#define COL_TITLE DC_RGB(0xFF, 0xFF, 0xFF)
#define COL_TEXT  DC_RGB(0xC8, 0xC8, 0xC8)
#define COL_PICK  DC_RGB(0xFF, 0xFF, 0x80)
#define COL_HINT  DC_RGB(0x80, 0x88, 0x98)

enum {
	OV_CONTINUE = 0,
	OV_RESET,
	OV_MENU,
	OV_SAVE,
	OV_LOAD,
	OV_COUNT
};

static const char *const labels[OV_COUNT] = {
	"Continue",
	"Reset ROM",
	"Return to menu",
	"Save state",
	"Load state"
};

static int cursor, prev, action;
static char status[96];

static void refresh_status(void)
{
	snprintf(status, sizeof(status), "Slot %u %s",
		 savestates_get_slot(),
		 savestates_exists(SAVESTATE) ? "(file present)" : "(empty)");
}

void dc_overlay_enter(void)
{
	cursor = 0;
	prev = ~0; /* swallow the Start+A+B hold that opened this screen */
	action = DC_OVERLAY_NONE;
	refresh_status();
}

int dc_overlay_step(const BUTTONS *keys)
{
	unsigned now = 0, pressed;

	if (!keys)
		return 1;
	if (keys->B_BUTTON) now |= 1u;
	if (keys->A_BUTTON) now |= 2u;
	if (keys->START_BUTTON) now |= 2u;
	if (keys->U_DPAD) now |= 4u;
	if (keys->D_DPAD) now |= 8u;
	if (keys->L_DPAD) now |= 16u;
	if (keys->R_DPAD) now |= 32u;
	pressed = now & ~(unsigned)prev;
	prev = (int)now;

	if (pressed & 1u) {
		action = DC_OVERLAY_CONTINUE;
		return 0;
	}
	if (pressed & 4u)
		cursor--;
	if (pressed & 8u)
		cursor++;
	if (cursor < 0)
		cursor = OV_COUNT - 1;
	if (cursor >= OV_COUNT)
		cursor = 0;
	if (pressed & 16u)
		savestates_select_slot((savestates_get_slot() + 9u) % 10u);
	if (pressed & 32u)
		savestates_select_slot((savestates_get_slot() + 1u) % 10u);
	if (pressed & (16u | 32u))
		refresh_status();
	if (pressed & 2u) {
		switch (cursor) {
		case OV_CONTINUE:
			action = DC_OVERLAY_CONTINUE;
			return 0;
		case OV_RESET:
			action = DC_OVERLAY_RESET;
			stop = 1;
			return 0;
		case OV_MENU:
			action = DC_OVERLAY_MENU;
			stop = 1;
			return 0;
		case OV_SAVE:
			savestates_job = SAVESTATE;
			savestates_save();
			{
				const char *why = savestates_error();

				if (savestates_ok())
					snprintf(status, sizeof(status),
						 "Slot %u saved",
						 savestates_get_slot());
				else if (why && why[0])
					snprintf(status, sizeof(status),
						 "Save slot %u: %s",
						 savestates_get_slot(), why);
				else
					snprintf(status, sizeof(status),
						 "Save slot %u failed",
						 savestates_get_slot());
			}
			dc_log(DC_LOG_INFO, "%s", status);
			break;
		case OV_LOAD:
			if (!savestates_exists(LOADSTATE))
				snprintf(status, sizeof(status),
					 "Load slot %u: no file",
					 savestates_get_slot());
			else {
				const char *why;

				savestates_job = LOADSTATE;
				savestates_load();
				why = savestates_error();
				if (savestates_ok())
					snprintf(status, sizeof(status),
						 "Slot %u restored",
						 savestates_get_slot());
				else if (why && why[0])
					snprintf(status, sizeof(status),
						 "Load slot %u: %s",
						 savestates_get_slot(), why);
				else
					snprintf(status, sizeof(status),
						 "Load slot %u failed",
						 savestates_get_slot());
			}
			dc_log(DC_LOG_INFO, "%s", status);
			break;
		}
	}
	return 1;
}

void dc_overlay_draw(void)
{
	const int cw = dc_draw_char_w();
	const int ch = dc_draw_char_h();
	char line[128];
	int i;

	dc_draw_begin(COL_BG);
	dc_draw_fill_rect(0, 0, dc_draw_width(), ch, COL_BAR);
	dc_draw_text(cw, 0, COL_TITLE, "Paused");
	for (i = 0; i < OV_COUNT; ++i) {
		int y = ch * (i + 2);
		if (i == cursor)
			dc_draw_fill_rect(0, y, dc_draw_width(), ch, COL_BAR);
		snprintf(line, sizeof(line), "%c %s", i == cursor ? '>' : ' ',
			 labels[i]);
		dc_draw_text(cw, y, i == cursor ? COL_PICK : COL_TEXT, line);
	}
	dc_draw_text(cw, dc_draw_height() - ch * 2, COL_HINT, status);
	dc_draw_text(cw, dc_draw_height() - ch, COL_HINT,
		     "A select  L/R slot  B continue  Start+A+B opens this");
	dc_draw_end();
}

int dc_overlay_run(void)
{
	BUTTONS keys;
	unsigned raw = 0;
	int jx = 0, jy = 0, lt = 0, rt = 0;
	int frames = 0;
	int started = 0;

	dc_overlay_enter();
	refresh_status();
	if (dc_draw_init() == 0)
		started = 1;
	for (;;) {
		if (started)
			dc_overlay_draw();
		memset(&keys, 0, sizeof(keys));
		if (controller_DC_poll_raw(0, &raw, &jx, &jy, &lt, &rt)) {
			keys.A_BUTTON = !!(raw & DC_CONT_A);
			keys.B_BUTTON = !!(raw & DC_CONT_B);
			keys.START_BUTTON = !!(raw & DC_CONT_START);
			keys.U_DPAD = !!(raw & DC_CONT_DPAD_UP);
			keys.D_DPAD = !!(raw & DC_CONT_DPAD_DOWN);
			keys.L_DPAD = !!(raw & DC_CONT_DPAD_LEFT);
			keys.R_DPAD = !!(raw & DC_CONT_DPAD_RIGHT);
		}
		if (!dc_overlay_step(&keys))
			break;
#ifdef DC_HOST_STUB
		if (++frames > DC_MENU_HOST_FRAME_CAP) {
			action = DC_OVERLAY_CONTINUE;
			break;
		}
#else
		(void)frames;
#endif
	}
	if (started)
		dc_draw_shutdown();
	return action;
}

int dc_overlay_take_action(void)
{
	int a = action;
	action = DC_OVERLAY_NONE;
	return a;
}

#ifdef DC_HOST_STUB
static void overlay_release(void)
{
	BUTTONS k;

	memset(&k, 0, sizeof(k));
	dc_overlay_step(&k);
}

int dc_overlay_selftest(void)
{
	int fails = 0;
	BUTTONS k;
	const char *row;

	stop = 0;
	dc_overlay_enter();
	memset(&k, 0, sizeof(k));
	k.A_BUTTON = 1;
	k.B_BUTTON = 1;
	k.START_BUTTON = 1;
	if (!dc_overlay_step(&k)) {
		printf("overlay FAIL: opening combo must not select Continue\n");
		fails++;
	}
	overlay_release();
	dc_draw_init();
	dc_overlay_draw();
	row = dc_draw_host_row(0);
	if (!row || !strstr(row, "Paused")) {
		printf("overlay FAIL: title '%s'\n", row ? row : "(null)");
		fails++;
	}
	row = dc_draw_host_row(2);
	if (!row || !strstr(row, "Continue")) {
		printf("overlay FAIL: continue '%s'\n", row ? row : "(null)");
		fails++;
	}
	memset(&k, 0, sizeof(k));
	k.D_DPAD = 1;
	dc_overlay_step(&k);
	memset(&k, 0, sizeof(k));
	dc_overlay_step(&k);
	k.D_DPAD = 1;
	dc_overlay_step(&k);
	memset(&k, 0, sizeof(k));
	dc_overlay_step(&k);
	k.D_DPAD = 1;
	dc_overlay_step(&k);
	memset(&k, 0, sizeof(k));
	dc_overlay_step(&k);
	k.A_BUTTON = 1;
	dc_overlay_step(&k);
	if (!strstr(status, "saved") || strstr(status, "failed")) {
		printf("overlay FAIL: save status '%s'\n", status);
		fails++;
	}
	if (stop) {
		printf("overlay FAIL: save must not stop the interpreter\n");
		fails++;
	}
	if (!savestates_exists(SAVESTATE)) {
		printf("overlay FAIL: save did not create %s\n",
		       savestates_filename());
		fails++;
	}
	{
		FILE *patch = fopen(savestates_filename(), "r+b");
		char other[32];

		memset(other, 0, sizeof(other));
		strncpy(other, "OTHER ROM", sizeof(other) - 1);
		if (patch) {
			if (fseek(patch, 16, SEEK_SET) == 0)
				fwrite(other, 1, 32, patch);
			fclose(patch);
		}
		memset(&k, 0, sizeof(k));
		dc_overlay_step(&k);
		k.D_DPAD = 1;
		dc_overlay_step(&k);
		memset(&k, 0, sizeof(k));
		dc_overlay_step(&k);
		k.A_BUTTON = 1;
		dc_overlay_step(&k);
		if (!strstr(status, "wrong ROM")) {
			printf("overlay FAIL: wrong-ROM load status '%s'\n", status);
			fails++;
		}
	}
	remove(savestates_filename());
	dc_overlay_enter();
	overlay_release();
	memset(&k, 0, sizeof(k));
	k.A_BUTTON = 1;
	if (dc_overlay_step(&k) || dc_overlay_take_action() != DC_OVERLAY_CONTINUE) {
		printf("overlay FAIL: A on Continue\n");
		fails++;
	}
	dc_overlay_enter();
	overlay_release();
	memset(&k, 0, sizeof(k));
	k.D_DPAD = 1;
	dc_overlay_step(&k);
	memset(&k, 0, sizeof(k));
	dc_overlay_step(&k);
	k.D_DPAD = 1;
	dc_overlay_step(&k);
	memset(&k, 0, sizeof(k));
	dc_overlay_step(&k);
	k.A_BUTTON = 1;
	stop = 0;
	if (dc_overlay_step(&k) || !stop ||
	    dc_overlay_take_action() != DC_OVERLAY_MENU) {
		printf("overlay FAIL: return to menu\n");
		fails++;
	}
	stop = 0;
	dc_draw_shutdown();
	printf("overlay selftest: %s (%d failure(s))\n",
	       fails ? "FAIL" : "PASS", fails);
	return fails;
}
#endif
