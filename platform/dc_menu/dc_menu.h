/**
 * Not64 Dreamcast menu — Phase 8a ROM browser.
 *
 * Lists the ROM directory, lets the pad pick one, and hands the choice back.
 * That is the whole scope: no settings, no in-game overlay, no renderer (see
 * Phase 8 in PORTING.md for 8b/8c and for why libgui/ is not ported).
 *
 * The menu is optional and never load-bearing. skipMenu plus the argv path
 * stays the regression harness, and `make -f Makefile.dc HOST=1 test` passes
 * without the menu ever running.
 *
 * The list and the input state are separated from drawing on purpose: the
 * host stub has no framebuffer, so smoke_menu() drives dc_menu_step() with
 * synthetic pad words and asserts the cursor, the scroll window and the
 * result. Only the pixels are platform-specific.
 */

#ifndef DC_MENU_H
#define DC_MENU_H

#include "../../fileBrowser/fileBrowser.h"
#include "../../gc_input/controller.h"

#define DC_MENU_MAX_NAME 64

typedef struct {
	char         path[FILE_BROWSER_MAX_PATH_LEN];	/* what rom_read wants */
	char         label[DC_MENU_MAX_NAME];		/* basename, for the row */
	unsigned int size;
} dc_menu_entry;

typedef struct {
	dc_menu_entry *items;
	int            count;
	/* Where the entries came from. Worth showing: when the list is empty,
	 * the path is the one thing the player needs to know. */
	char           dir[FILE_BROWSER_MAX_PATH_LEN];
} dc_menu_list;

/* What one polled frame of input decided. */
typedef enum {
	DC_MENU_NONE = 0,
	DC_MENU_PICK,
	DC_MENU_CANCEL
} dc_menu_action;

typedef struct {
	const dc_menu_list *list;
	int cursor;		/* highlighted entry */
	int top;		/* first visible entry */
	int rows;		/* visible entries */

	/* Edge detection and auto-repeat. The pad is polled, so acting on the
	 * level would run the cursor off the end of the list in one press. */
	unsigned int prev;	/* previous frame's decoded button mask */
	int  repeat_in;		/* frames until a held direction repeats */
	int  repeat_dir;	/* -1 up, +1 down, 0 idle */
} dc_menu_state;

/* Frames (at ~60 Hz) before a held direction starts repeating, and between
 * repeats after that. */
#define DC_MENU_REPEAT_FIRST 20
#define DC_MENU_REPEAT_NEXT   4

/* Analog deflection, out of the N64 range (+/-80), that counts as a push. */
#define DC_MENU_STICK_ON  40
#define DC_MENU_STICK_OFF 20

/* Host stub only: nothing but controller_DC_host_set() can drive the browser
 * there and dc_draw_end() does not wait for a vblank, so an un-driven menu
 * would spin. Give up after this many iterations and report instead. */
#define DC_MENU_HOST_FRAME_CAP 3600

/* Reads dir, keeps .z64/.n64/.v64, sorts by name. Returns the entry count,
 * 0 for an empty directory, or a negative fileBrowser error. */
int  dc_menu_list_load(dc_menu_list *list, fileBrowser_file *dir);
void dc_menu_list_free(dc_menu_list *list);

void dc_menu_state_init(dc_menu_state *st, const dc_menu_list *list, int rows);

/* One polled frame. Moves the cursor, scrolls, and reports a pick or cancel. */
dc_menu_action dc_menu_step(dc_menu_state *st, const BUTTONS *keys);

/* Draws the current state through dc_draw.h. */
void dc_menu_draw(const dc_menu_state *st, const char *title);

/* A single status line on an otherwise empty screen, same chrome as the
 * browser. Used for "Loading ..." while a cart streams off the disc. */
void dc_menu_message(const char *title, const char *message);

/* The whole browser: open the surface, loop until the pad picks or cancels,
 * free everything. Returns 1 on a pick (out is filled), 0 on cancel, negative
 * on error. `dir` is normally romFile_topLevel. */
int dc_menu_run(fileBrowser_file *dir, dc_menu_entry *out);

#endif /* DC_MENU_H */
