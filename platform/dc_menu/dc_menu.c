/**
 * Not64 Dreamcast menu — Phase 8a ROM browser.
 *
 * See dc_menu.h for scope. Drawing goes through dc_draw.h, input comes from
 * the same getKeys() path the emulator uses, so the Maple map (triggers carry
 * Z/R, Y+left trigger is L, both triggers shift the D-pad to the C-buttons)
 * already applies here.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dc_menu.h"
#include "dc_draw.h"

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
		dc_draw_text(cw, ch * 3, COL_HINT, "Put .z64/.n64/.v64 files in the roms folder.");
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

	snprintf(line, sizeof(line), "%d/%d   A start   B exit   Z/R page",
		 st->cursor + 1, st->list->count);
	dc_draw_text(cw, dc_draw_height() - ch, COL_HINT, line);
	dc_draw_end();
}

/* ---- the browser ------------------------------------------------------ */

int dc_menu_run(fileBrowser_file *dir, dc_menu_entry *out)
{
	dc_menu_list  list;
	dc_menu_state st;
	int found, rows, result = 0;

	if (!out)
		return FILE_BROWSER_ERROR;

	found = dc_menu_list_load(&list, dir);
	if (found < 0)
		return found;

	if (dc_draw_init() != 0) {
		dc_menu_list_free(&list);
		return FILE_BROWSER_ERROR;
	}

	/* Two rows of chrome at the top, one hint row at the bottom. */
	rows = dc_draw_height() / dc_draw_char_h() - 3;
	dc_menu_state_init(&st, &list, rows);

#ifdef DC_HOST_STUB
	/* The host stub has no pad: only controller_DC_host_set() can move this
	 * loop, and dc_draw_end() does not wait for a vblank. Cap the spin so an
	 * un-driven `--menu` reports instead of hanging a terminal. Hardware
	 * blocks on vblank and waits for the player, as it should. */
	{
	int frames = 0;
#endif
	for (;;) {
		BUTTONS keys;
		dc_menu_action act;

		dc_menu_draw(&st, "Not64  -  choose a ROM");

		memset(&keys, 0, sizeof(keys));
		getKeys(0, &keys);
		act = dc_menu_step(&st, &keys);

		if (act == DC_MENU_PICK) {
			*out = list.items[st.cursor];
			result = 1;
			break;
		}
		if (act == DC_MENU_CANCEL) {
			result = 0;
			break;
		}
#ifdef DC_HOST_STUB
		if (++frames > 3600) {
			printf("menu: no host input after %d frames, cancelling\n",
			       frames);
			result = 0;
			break;
		}
#endif
	}
#ifdef DC_HOST_STUB
	}
#endif

	/* Phase 8 constraint: the menu frees everything before go() runs. */
	dc_draw_shutdown();
	dc_menu_list_free(&list);
	return result;
}
