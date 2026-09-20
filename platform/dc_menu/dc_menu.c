/**
 * Not64 Dreamcast menu — Phase 8a ROM browser.
 *
 * See dc_menu.h for scope. Drawing goes through dc_draw.h. The ROM list
 * reads unshifted Maple (controller_DC_poll_raw) so C-shift cannot steal
 * the D-pad; getKeys() is the in-game map. Y opens Settings, X opens
 * Controls (Flycast/RetroArch extra screens, mupen64plus.cfg on disk).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dc_menu.h"
#include "dc_draw.h"
#include "../dc_settings.h"
#include "../../gc_input/controller.h"

extern void getKeys(int Control, BUTTONS *Keys);

/* Edge-detection mask. Built by hand rather than from BUTTONS.Value: the bit
 * order inside that bitfield is implementation-defined, and DWORD is 8 bytes
 * on an LP64 host and 4 on SH4, so the raw word is not a portable key. */
enum {
	DC_MENU_EDGE_A     = 1u << 0,
	DC_MENU_EDGE_B     = 1u << 1,
	DC_MENU_EDGE_START = 1u << 2,
	DC_MENU_EDGE_Z     = 1u << 3,
	DC_MENU_EDGE_R     = 1u << 4
};

static unsigned int decode_edges(const BUTTONS *k)
{
	unsigned int m = 0;

	if (k->A_BUTTON)     m |= DC_MENU_EDGE_A;
	if (k->B_BUTTON)     m |= DC_MENU_EDGE_B;
	if (k->START_BUTTON) m |= DC_MENU_EDGE_START;
	if (k->Z_TRIG)       m |= DC_MENU_EDGE_Z;
	if (k->R_TRIG)       m |= DC_MENU_EDGE_R;
	return m;
}

/* ---- list ------------------------------------------------------------- */

static const char *basename_of(const char *path)
{
	const char *slash = strrchr(path, '/');
	return slash ? slash + 1 : path;
}

static int ext_is_rom(const char *path)
{
	static const char *exts[] = { ".z64", ".n64", ".v64" };
	size_t len = strlen(path);
	unsigned int i;

	for (i = 0; i < sizeof(exts) / sizeof(exts[0]); ++i) {
		size_t elen = strlen(exts[i]);
		const char *tail;
		unsigned int c;

		if (len < elen)
			continue;
		tail = path + len - elen;
		for (c = 0; c < elen; ++c) {
			char a = tail[c], b = exts[i][c];
			if (a >= 'A' && a <= 'Z')
				a = (char)(a - 'A' + 'a');
			if (a != b)
				break;
		}
		if (c == elen)
			return 1;
	}
	return 0;
}

static int name_cmp(const void *a, const void *b)
{
	const dc_menu_entry *ea = (const dc_menu_entry *)a;
	const dc_menu_entry *eb = (const dc_menu_entry *)b;
	const char *x = ea->label, *y = eb->label;

	for (; *x && *y; ++x, ++y) {
		char cx = *x, cy = *y;
		if (cx >= 'A' && cx <= 'Z') cx = (char)(cx - 'A' + 'a');
		if (cy >= 'A' && cy <= 'Z') cy = (char)(cy - 'A' + 'a');
		if (cx != cy)
			return (cx < cy) ? -1 : 1;
	}
	if (*x == *y)
		return 0;
	return *x ? 1 : -1;
}

int dc_menu_list_load(dc_menu_list *list, fileBrowser_file *dir)
{
	fileBrowser_file *raw = NULL;
	int found, i, kept = 0;

	if (!list || !dir)
		return FILE_BROWSER_ERROR;

	list->items = NULL;
	list->count = 0;
	list->dir[0] = '\0';
	strncpy(list->dir, dir->name, sizeof(list->dir) - 1);

	if (romFile_init)
		romFile_init(dir);
	if (!romFile_readDir)
		return FILE_BROWSER_ERROR;

	found = romFile_readDir(dir, &raw);
	if (found < 0)
		return found;
	if (found == 0) {
		free(raw);
		return 0;
	}

	list->items = (dc_menu_entry *)malloc(sizeof(dc_menu_entry) * (size_t)found);
	if (!list->items) {
		free(raw);
		return FILE_BROWSER_ERROR;
	}

	for (i = 0; i < found; ++i) {
		dc_menu_entry *e;

		if (raw[i].attr & FILE_BROWSER_ATTR_DIR)
			continue;
		if (!ext_is_rom(raw[i].name))
			continue;

		e = &list->items[kept];
		memset(e, 0, sizeof(*e));
		strncpy(e->path, raw[i].name, FILE_BROWSER_MAX_PATH_LEN - 1);
		strncpy(e->label, basename_of(raw[i].name), DC_MENU_MAX_NAME - 1);
		e->size = raw[i].size;
		kept++;
	}
	free(raw);

	if (kept == 0) {
		free(list->items);
		list->items = NULL;
		return 0;
	}

	qsort(list->items, (size_t)kept, sizeof(dc_menu_entry), name_cmp);
	list->count = kept;
	return kept;
}

void dc_menu_list_free(dc_menu_list *list)
{
	if (!list)
		return;
	free(list->items);
	list->items = NULL;
	list->count = 0;
}

#ifndef DC_HOST_STUB
/* Where a Dreamcast keeps ROMs, in the order worth trying. Burning a CD-R is
 * how most people run homebrew, so the disc comes first; /sd/not64/roms is the
 * SD-adapter layout; /pc is dcload over serial or BBA while developing.
 * A console with no SD adapter simply has no /sd, which is why a directory
 * that will not open has to mean "nothing here", not "give up". */
static const char *const dc_rom_dirs[] = {
	"/cd/roms",
	"/cd",
	"/sd/not64/roms",
	"/pc/roms"
};
#define DC_ROM_DIR_COUNT (sizeof(dc_rom_dirs) / sizeof(dc_rom_dirs[0]))
#endif

/* Try the configured directory, then the usual Dreamcast ones. Returns the
 * entry count, and on 0 leaves list->dir holding what was searched so the
 * empty screen can say where it looked. */
static int dc_menu_find_roms(dc_menu_list *list, fileBrowser_file *dir)
{
	int n = dc_menu_list_load(list, dir);
	char tried[FILE_BROWSER_MAX_PATH_LEN];

	if (n > 0)
		return n;

	tried[0] = '\0';
	strncpy(tried, dir->name, sizeof(tried) - 1);

#ifndef DC_HOST_STUB
	{
		unsigned int i;

		for (i = 0; i < DC_ROM_DIR_COUNT; ++i) {
			fileBrowser_file probe;

			if (strcmp(dir->name, dc_rom_dirs[i]) == 0)
				continue;

			memset(&probe, 0, sizeof(probe));
			strncpy(probe.name, dc_rom_dirs[i],
				FILE_BROWSER_MAX_PATH_LEN - 1);
			/* No DIR attr on purpose: romFile_init() would try to
			 * create it, and probing must not mkdir its way around
			 * the filesystem. */
			n = dc_menu_list_load(list, &probe);
			if (n > 0)
				return n;

			if (strlen(tried) + strlen(dc_rom_dirs[i]) + 3 < sizeof(tried)) {
				strcat(tried, ", ");
				strcat(tried, dc_rom_dirs[i]);
			}
		}
	}
#endif

	list->items = NULL;
	list->count = 0;
	strncpy(list->dir, tried, sizeof(list->dir) - 1);
	list->dir[sizeof(list->dir) - 1] = '\0';
	return 0;
}

/* ---- input ------------------------------------------------------------ */

void dc_menu_state_init(dc_menu_state *st, const dc_menu_list *list, int rows)
{
	if (!st)
		return;
	memset(st, 0, sizeof(*st));
	st->list = list;
	st->rows = rows > 0 ? rows : 1;
}

/* Keep the cursor inside the list and the window around the cursor. */
static void clamp_view(dc_menu_state *st)
{
	int count = st->list ? st->list->count : 0;

	if (count <= 0) {
		st->cursor = 0;
		st->top = 0;
		return;
	}
	if (st->cursor < 0)
		st->cursor = count - 1;		/* wrap: few entries is the norm */
	if (st->cursor >= count)
		st->cursor = 0;

	if (st->cursor < st->top)
		st->top = st->cursor;
	if (st->cursor >= st->top + st->rows)
		st->top = st->cursor - st->rows + 1;

	if (st->top > count - st->rows)
		st->top = count - st->rows;
	if (st->top < 0)
		st->top = 0;
}

/* Direction the pad is pushing this frame: -1 up, +1 down, 0 none. The D-pad
 * and the analog stick both drive it, with separate on/off thresholds on the
 * stick so a resting hand does not chatter. */
static int wanted_dir(const dc_menu_state *st, const BUTTONS *k)
{
	int y = (int)(signed char)k->Y_AXIS;
	int on = (st->repeat_dir != 0) ? DC_MENU_STICK_OFF : DC_MENU_STICK_ON;

	if (k->U_DPAD || y >= on)
		return -1;
	if (k->D_DPAD || y <= -on)
		return 1;
	return 0;
}

dc_menu_action dc_menu_step(dc_menu_state *st, const BUTTONS *keys)
{
	unsigned int now, pressed;
	int count, dir, step = 0;

	if (!st || !keys)
		return DC_MENU_NONE;

	count = st->list ? st->list->count : 0;
	now = decode_edges(keys);
	pressed = now & ~st->prev;	/* act on the press, not the hold */
	st->prev = now;

	/* A picks, B backs out. Start also picks: it is the button a player
	 * reaches for, and there is nothing else it could mean here. */
	if (count > 0 && (pressed & (DC_MENU_EDGE_A | DC_MENU_EDGE_START)))
		return DC_MENU_PICK;
	if (pressed & DC_MENU_EDGE_B)
		return DC_MENU_CANCEL;

	if (count == 0) {
		st->repeat_dir = 0;
		st->repeat_in = 0;
		return DC_MENU_NONE;
	}

	/* Triggers page. On the Dreamcast map the left trigger is Z and the
	 * right trigger is R, so both are a single reachable press. */
	if (pressed & DC_MENU_EDGE_Z)
		step = -st->rows;
	else if (pressed & DC_MENU_EDGE_R)
		step = st->rows;

	if (step) {
		/* Paging clamps rather than wraps: holding a trigger to walk a
		 * long list should stop at the end, not cycle past it. */
		st->cursor += step;
		if (st->cursor < 0)
			st->cursor = 0;
		if (st->cursor >= count)
			st->cursor = count - 1;
		clamp_view(st);
		return DC_MENU_NONE;
	}

	dir = wanted_dir(st, keys);
	if (dir == 0) {
		st->repeat_dir = 0;
		st->repeat_in = 0;
		return DC_MENU_NONE;
	}

	if (dir != st->repeat_dir) {
		/* Fresh push: move once, then wait before repeating. */
		st->repeat_dir = dir;
		st->repeat_in = DC_MENU_REPEAT_FIRST;
		st->cursor += dir;
		clamp_view(st);
		return DC_MENU_NONE;
	}

	if (--st->repeat_in <= 0) {
		st->repeat_in = DC_MENU_REPEAT_NEXT;
		st->cursor += dir;
		clamp_view(st);
	}
	return DC_MENU_NONE;
}

/* ---- drawing ---------------------------------------------------------- */

#define COL_BG    DC_RGB(0x10, 0x14, 0x20)
#define COL_BAR   DC_RGB(0x28, 0x50, 0x90)
#define COL_TITLE DC_RGB(0xFF, 0xFF, 0xFF)
#define COL_TEXT  DC_RGB(0xC8, 0xC8, 0xC8)
#define COL_PICK  DC_RGB(0xFF, 0xFF, 0x80)
#define COL_HINT  DC_RGB(0x80, 0x88, 0x98)

static void size_text(char *buf, size_t n, unsigned int bytes)
{
	if (bytes >= 1024u * 1024u)
		snprintf(buf, n, "%u MiB", bytes / (1024u * 1024u));
	else if (bytes >= 1024u)
		snprintf(buf, n, "%u KiB", bytes / 1024u);
	else
		snprintf(buf, n, "%u B", bytes);
}

void dc_menu_draw(const dc_menu_state *st, const char *title)
{
	const int cw = dc_draw_char_w();
	const int ch = dc_draw_char_h();
	const int cols = dc_draw_width() / cw;
	char line[128];
	int i;

	if (!st)
		return;

	dc_draw_begin(COL_BG);
	dc_draw_fill_rect(0, 0, dc_draw_width(), ch, COL_BAR);
	dc_draw_text(cw, 0, COL_TITLE, title ? title : "Not64");

	if (!st->list || st->list->count == 0) {
		dc_draw_text(cw, ch * 2, COL_TEXT, "No ROMs found.");
		if (st->list && st->list->dir[0]) {
			snprintf(line, sizeof(line), "Looked in: %.*s",
				 (int)sizeof(line) - 12, st->list->dir);
			dc_draw_text(cw, ch * 3, COL_HINT, line);
		}
		dc_draw_text(cw, ch * 4, COL_HINT,
			     "Put .z64/.n64/.v64 files there, then restart.");
		/* Still say how to leave: B is the only thing that works here. */
		dc_draw_text(cw, dc_draw_height() - ch, COL_HINT,
			     "B exit   Y settings   X controls");
		dc_draw_end();
		return;
	}

	for (i = 0; i < st->rows; ++i) {
		int idx = st->top + i;
		int y = ch * (i + 2);
		char sz[16];
		int room;

		if (idx >= st->list->count)
			break;

		size_text(sz, sizeof(sz), st->list->items[idx].size);
		/* Leave room for the marker, a space, the size and a gap. */
		room = cols - 2 - (int)strlen(sz) - 2;
		if (room < 8)
			room = 8;
		if (room > (int)sizeof(line) - 1)
			room = (int)sizeof(line) - 1;

		if (idx == st->cursor)
			dc_draw_fill_rect(0, y, dc_draw_width(), ch, COL_BAR);

		snprintf(line, sizeof(line), "%c %-*.*s %s",
			 idx == st->cursor ? '>' : ' ',
			 room, room, st->list->items[idx].label, sz);
		dc_draw_text(cw, y, idx == st->cursor ? COL_PICK : COL_TEXT, line);
	}

	snprintf(line, sizeof(line), "%d/%d  A start  B exit  Y settings  X controls",
		 st->cursor + 1, st->list->count);
	dc_draw_text(cw, dc_draw_height() - ch, COL_HINT, line);
	dc_draw_end();
}

void dc_menu_message(const char *title, const char *message)
{
	const int cw = dc_draw_char_w();
	const int ch = dc_draw_char_h();

	dc_draw_begin(COL_BG);
	dc_draw_fill_rect(0, 0, dc_draw_width(), ch, COL_BAR);
	dc_draw_text(cw, 0, COL_TITLE, title ? title : "Not64");
	if (message)
		dc_draw_text(cw, ch * 2, COL_TEXT, message);
	dc_draw_end();
}

#define DC_MENU_TRIG_ON 48

static int keys_from_raw(BUTTONS *k, unsigned b, int jx, int jy, int lt, int rt)
{
	int x, y;

	if (!k)
		return 0;
	memset(k, 0, sizeof(*k));
	k->A_BUTTON = !!(b & DC_CONT_A);
	k->B_BUTTON = !!(b & DC_CONT_B);
	k->START_BUTTON = !!(b & DC_CONT_START);
	k->U_DPAD = !!(b & DC_CONT_DPAD_UP);
	k->D_DPAD = !!(b & DC_CONT_DPAD_DOWN);
	k->L_DPAD = !!(b & DC_CONT_DPAD_LEFT);
	k->R_DPAD = !!(b & DC_CONT_DPAD_RIGHT);
	k->Z_TRIG = lt >= DC_MENU_TRIG_ON;
	k->R_TRIG = rt >= DC_MENU_TRIG_ON;
	x = jx;
	y = -jy;
	if (x > 10 || x < -10) {
		if (x > 80) x = 80;
		if (x < -80) x = -80;
		k->X_AXIS = (signed char)x;
	}
	if (y > 10 || y < -10) {
		if (y > 80) y = 80;
		if (y < -80) y = -80;
		k->Y_AXIS = (signed char)y;
	}
	return 1;
}

static int menu_poll(BUTTONS *keys, unsigned *raw_out)
{
	unsigned raw = 0;
	int jx = 0, jy = 0, lt = 0, rt = 0;

	if (controller_DC_poll_raw(0, &raw, &jx, &jy, &lt, &rt)) {
		if (raw_out)
			*raw_out = raw;
		return keys_from_raw(keys, raw, jx, jy, lt, rt);
	}
	if (raw_out)
		*raw_out = 0;
	memset(keys, 0, sizeof(*keys));
	getKeys(0, keys);
	return 1;
}

static void run_until_back(void (*enter)(void), int (*step)(const BUTTONS *),
			   void (*draw)(void))
{
	BUTTONS keys;
	int frames = 0;

	enter();
	for (;;) {
		draw();
		menu_poll(&keys, NULL);
		if (!step(&keys))
			break;
#ifdef DC_HOST_STUB
		if (++frames > DC_MENU_HOST_FRAME_CAP)
			break;
#else
		(void)frames;
#endif
	}
}

/* ---- the browser ------------------------------------------------------ */

int dc_menu_run(fileBrowser_file *dir, dc_menu_entry *out)
{
	dc_menu_list  list;
	dc_menu_state st;
	int found, rows, result = 0;
	int frames = 0;
	int dirty = 1;
	unsigned extra_prev = 0;

	if (!out)
		return FILE_BROWSER_ERROR;

	/* A directory that will not open is not a failure -- it means no ROMs
	 * there. The browser still opens and says where it looked; refusing to
	 * draw left a Dreamcast with no SD adapter staring at a black screen. */
	found = dc_menu_find_roms(&list, dir);
	(void)found;

	if (dc_draw_init() != 0) {
		dc_menu_list_free(&list);
		return FILE_BROWSER_ERROR;
	}

	(void)frames;	/* only the host stub counts them */

	/* Two rows of chrome at the top, one hint row at the bottom. */
	rows = dc_draw_height() / dc_draw_char_h() - 3;
	dc_menu_state_init(&st, &list, rows);

	/* Repaint only when the selection actually moves. The picture is
	 * identical otherwise, and on a Dreamcast a repaint is a 600 KiB blit --
	 * no reason to spend that sixty times a second on a static list. */
	for (;;) {
		BUTTONS keys;
		dc_menu_action act;
		unsigned raw = 0;
		int was_cursor = st.cursor, was_top = st.top;

		if (dirty) {
			dc_menu_draw(&st, "Not64  -  choose a ROM");
			dirty = 0;
		} else {
			dc_draw_wait();
		}

		menu_poll(&keys, &raw);
		{
			unsigned extra = raw & (DC_CONT_X | DC_CONT_Y);
			unsigned press = extra & ~extra_prev;
			extra_prev = extra;
			if (press & DC_CONT_Y) {
				run_until_back(dc_settings_enter, dc_settings_step,
					       dc_settings_draw);
				(void)dc_settings_save(NULL);
				dirty = 1;
				continue;
			}
			if (press & DC_CONT_X) {
				run_until_back(dc_controls_enter, dc_controls_step,
					       dc_controls_draw);
				(void)dc_settings_save(NULL);
				dirty = 1;
				continue;
			}
		}
		act = dc_menu_step(&st, &keys);
		if (st.cursor != was_cursor || st.top != was_top)
			dirty = 1;

		if (act == DC_MENU_PICK) {
			char msg[128];

			*out = list.items[st.cursor];
			/* Pulling a 32 MiB cart off a CD takes seconds, and the
			 * bring-up console is not visible behind the menu. Without
			 * this the screen simply goes dark after the button press,
			 * which reads as a crash. */
			snprintf(msg, sizeof(msg), "Loading %s ...", out->label);
			dc_menu_message("Not64", msg);
			result = 1;
			break;
		}
		if (act == DC_MENU_CANCEL) {
			result = 0;
			break;
		}
#ifdef DC_HOST_STUB
		/* Hardware blocks on vblank and waits for the player, as it
		 * should; the host stub has nothing to wait for. */
		if (++frames > DC_MENU_HOST_FRAME_CAP) {
			printf("menu: no host input after %d frames, cancelling\n",
			       frames);
			result = 0;
			break;
		}
#endif
	}

	/* Phase 8 constraint: the menu frees everything before go() runs. */
	dc_draw_shutdown();
	dc_menu_list_free(&list);
	return result;
}
