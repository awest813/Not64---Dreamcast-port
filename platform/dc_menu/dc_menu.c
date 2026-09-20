/**
 * Phase 8a — immediate-mode ROM browser.
 *
 * Not a libgui port: four screens do not need a FocusManager. Draw through
 * dc_draw_* so a later PVR backend can replace the RGB565 fill without
 * touching list logic. Input is the *unshifted* Maple word — the in-game
 * dual-trigger C-button shift must not steal D-pad navigation here.
 */

#include "dc_menu.h"
#include "dc_draw.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#ifdef DC_HOST_STUB
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#endif

#ifndef DC_HOST_STUB
#include <kos.h>
#endif

#include "../../fileBrowser/fileBrowser.h"
#include "../../fileBrowser/fileBrowser-kos.h"
#include "../../gc_input/controller.h"
#include "../../main/wii64config.h"

#define ROW_H 28
#define LIST_X 28
#define LIST_Y 72
#define LIST_W 584
#define HEADER_H 56
#define FOOTER_Y 424

struct dc_menu_entry {
	char path[FILE_BROWSER_MAX_PATH_LEN];
	char label[56];
	unsigned int size;
	int is_dir;
	int is_parent;
};

struct dc_menu_browser {
	char root[FILE_BROWSER_MAX_PATH_LEN];
	char path[FILE_BROWSER_MAX_PATH_LEN];
	char chosen[FILE_BROWSER_MAX_PATH_LEN];
	struct dc_menu_entry *entries;
	int count;
	int cursor;
	int scroll;
	int status;
};

int dc_menu_is_rom_name(const char *name)
{
	const char *dot;
	char ext[8];
	int i, n;

	if (!name)
		return 0;
	dot = strrchr(name, '.');
	if (!dot || !dot[1] || dot == name)
		return 0;
	n = 0;
	for (i = 1; dot[i] && n < (int)sizeof(ext) - 1; ++i)
		ext[n++] = (char)tolower((unsigned char)dot[i]);
	ext[n] = '\0';
	return !strcmp(ext, "z64") || !strcmp(ext, "n64") || !strcmp(ext, "v64");
}

void dc_menu_ellipsize(char *dst, size_t dst_len, const char *src, int max_chars)
{
	int n;

	if (!dst || dst_len == 0)
		return;
	dst[0] = '\0';
	if (!src || max_chars <= 0)
		return;
	n = (int)strlen(src);
	if (n <= max_chars) {
		strncpy(dst, src, dst_len - 1);
		dst[dst_len - 1] = '\0';
		return;
	}
	if (max_chars <= 3) {
		strncpy(dst, "...", dst_len - 1);
		dst[dst_len - 1] = '\0';
		return;
	}
	if ((size_t)(max_chars - 3) >= dst_len)
		max_chars = (int)dst_len - 1;
	memcpy(dst, src, (size_t)(max_chars - 3));
	dst[max_chars - 3] = '.';
	dst[max_chars - 2] = '.';
	dst[max_chars - 1] = '.';
	if ((size_t)max_chars < dst_len)
		dst[max_chars] = '\0';
	else
		dst[dst_len - 1] = '\0';
}

void dc_menu_format_size(char *dst, size_t dst_len, unsigned int bytes)
{
	if (!dst || dst_len == 0)
		return;
	if (bytes < 1024)
		snprintf(dst, dst_len, "%u B", bytes);
	else if (bytes < 1024u * 1024u)
		snprintf(dst, dst_len, "%u.%u K", bytes / 1024,
			 (bytes % 1024) / 103);
	else
		snprintf(dst, dst_len, "%u.%u M", bytes / (1024u * 1024u),
			 (bytes % (1024u * 1024u)) / 104858);
}

static const char *base_name(const char *path)
{
	const char *s;

	if (!path || !*path)
		return path;
	s = strrchr(path, '/');
	return s ? s + 1 : path;
}

static int at_root(const dc_menu_browser *b)
{
	return strcmp(b->path, b->root) == 0;
}

static void parent_path(const char *path, char *out, size_t out_len)
{
	char tmp[FILE_BROWSER_MAX_PATH_LEN];
	char *slash;

	strncpy(tmp, path, sizeof(tmp) - 1);
	tmp[sizeof(tmp) - 1] = '\0';
	slash = strrchr(tmp, '/');
	if (!slash || slash == tmp) {
		strncpy(out, tmp[0] ? "/" : ".", out_len - 1);
		out[out_len - 1] = '\0';
		return;
	}
	*slash = '\0';
	if (!tmp[0])
		strcpy(tmp, "/");
	strncpy(out, tmp, out_len - 1);
	out[out_len - 1] = '\0';
}

static int cmp_entries(const void *va, const void *vb)
{
	const struct dc_menu_entry *a = va, *b = vb;
	int da, db;

	if (a->is_parent != b->is_parent)
		return b->is_parent - a->is_parent;
	da = a->is_dir && !a->is_parent;
	db = b->is_dir && !b->is_parent;
	if (da != db)
		return db - da;
	return strcasecmp(a->label, b->label);
}

static void free_entries(dc_menu_browser *b)
{
	free(b->entries);
	b->entries = NULL;
	b->count = 0;
	b->cursor = 0;
	b->scroll = 0;
}

static int load_dir(dc_menu_browser *b)
{
	fileBrowser_file dir;
	fileBrowser_file *raw = NULL;
	int n, i, keep = 0;
	struct dc_menu_entry *out = NULL;

	free_entries(b);
	b->status = DC_MENU_ST_OK;
	b->chosen[0] = '\0';

	memset(&dir, 0, sizeof(dir));
	strncpy(dir.name, b->path, FILE_BROWSER_MAX_PATH_LEN - 1);
	dir.attr = FILE_BROWSER_ATTR_DIR;

	fileBrowser_kos_bind();
	/* Do not mkdir here: creating /sd/not64/roms is the browser's
	 * bind/startup job. Navigating into a missing path should show
	 * the error state, not silently invent folders. */
	n = romFile_readDir(&dir, &raw);
	if (n < 0) {
		free(raw);
		b->status = DC_MENU_ST_MISSING;
		return -1;
	}

	out = (struct dc_menu_entry *)calloc(DC_MENU_MAX_ENTRIES,
					     sizeof(*out));
	if (!out) {
		free(raw);
		b->status = DC_MENU_ST_MISSING;
		return -1;
	}

	if (!at_root(b)) {
		out[keep].is_dir = 1;
		out[keep].is_parent = 1;
		strcpy(out[keep].label, "..");
		parent_path(b->path, out[keep].path, sizeof(out[keep].path));
		keep++;
	}

	for (i = 0; i < n && keep < DC_MENU_MAX_ENTRIES; ++i) {
		const char *base = base_name(raw[i].name);
		int is_dir = (raw[i].attr & FILE_BROWSER_ATTR_DIR) != 0;

		if (!base[0] || base[0] == '.')
			continue;
		if (!is_dir && !dc_menu_is_rom_name(base))
			continue;
		strncpy(out[keep].path, raw[i].name, sizeof(out[keep].path) - 1);
		dc_menu_ellipsize(out[keep].label, sizeof(out[keep].label),
				  base, 48);
		out[keep].size = raw[i].size;
		out[keep].is_dir = is_dir;
		out[keep].is_parent = 0;
		keep++;
	}
	free(raw);

	qsort(out, (size_t)keep, sizeof(*out), cmp_entries);
	b->entries = out;
	b->count = keep;
	if (keep == 0 || (keep == 1 && out[0].is_parent))
		b->status = DC_MENU_ST_EMPTY;
	return 0;
}

static void clamp_scroll(dc_menu_browser *b)
{
	if (b->count <= 0) {
		b->cursor = 0;
		b->scroll = 0;
		return;
	}
	if (b->cursor < 0)
		b->cursor = 0;
	if (b->cursor >= b->count)
		b->cursor = b->count - 1;
	if (b->cursor < b->scroll)
		b->scroll = b->cursor;
	if (b->cursor >= b->scroll + DC_MENU_VISIBLE)
		b->scroll = b->cursor - DC_MENU_VISIBLE + 1;
	if (b->scroll < 0)
		b->scroll = 0;
}

dc_menu_browser *dc_menu_browser_create(const char *root)
{
	dc_menu_browser *b = (dc_menu_browser *)calloc(1, sizeof(*b));

	if (!b)
		return NULL;
	if (!root || !*root)
		root = "./roms";
	strncpy(b->root, root, sizeof(b->root) - 1);
	strncpy(b->path, root, sizeof(b->path) - 1);
	load_dir(b);
	clamp_scroll(b);
	return b;
}

void dc_menu_browser_destroy(dc_menu_browser *b)
{
	if (!b)
		return;
	free_entries(b);
	free(b);
}

int dc_menu_browser_count(const dc_menu_browser *b)
{
	return b ? b->count : 0;
}

int dc_menu_browser_cursor(const dc_menu_browser *b)
{
	return b ? b->cursor : 0;
}

int dc_menu_browser_scroll(const dc_menu_browser *b)
{
	return b ? b->scroll : 0;
}

int dc_menu_browser_status(const dc_menu_browser *b)
{
	return b ? b->status : DC_MENU_ST_MISSING;
}

int dc_menu_browser_entry_is_dir(const dc_menu_browser *b, int i)
{
	if (!b || i < 0 || i >= b->count)
		return 0;
	return b->entries[i].is_dir;
}

int dc_menu_browser_entry_is_parent(const dc_menu_browser *b, int i)
{
	if (!b || i < 0 || i >= b->count)
		return 0;
	return b->entries[i].is_parent;
}

const char *dc_menu_browser_entry_name(const dc_menu_browser *b, int i)
{
	if (!b || i < 0 || i >= b->count)
		return "";
	return b->entries[i].label;
}

const char *dc_menu_browser_path(const dc_menu_browser *b)
{
	return b ? b->path : "";
}

const char *dc_menu_browser_chosen(const dc_menu_browser *b)
{
	return b ? b->chosen : "";
}

int dc_menu_browser_apply(dc_menu_browser *b, int action)
{
	struct dc_menu_entry *e;

	if (!b)
		return DC_MENU_ERROR;

	switch (action) {
	case DC_MENU_ACT_UP:
		if (b->count <= 0)
			break;
		b->cursor = (b->cursor == 0) ? b->count - 1 : b->cursor - 1;
		clamp_scroll(b);
		break;
	case DC_MENU_ACT_DOWN:
		if (b->count <= 0)
			break;
		b->cursor = (b->cursor + 1) % b->count;
		clamp_scroll(b);
		break;
	case DC_MENU_ACT_PAGE_UP:
		if (b->count <= 0)
			break;
		b->cursor -= DC_MENU_VISIBLE;
		if (b->cursor < 0)
			b->cursor = 0;
		clamp_scroll(b);
		break;
	case DC_MENU_ACT_PAGE_DOWN:
		if (b->count <= 0)
			break;
		b->cursor += DC_MENU_VISIBLE;
		if (b->cursor >= b->count)
			b->cursor = b->count - 1;
		clamp_scroll(b);
		break;
	case DC_MENU_ACT_BACK:
		if (at_root(b))
			return DC_MENU_QUIT;
		parent_path(b->path, b->path, sizeof(b->path));
		load_dir(b);
		clamp_scroll(b);
		break;
	case DC_MENU_ACT_QUIT:
		return DC_MENU_QUIT;
	case DC_MENU_ACT_CONFIRM:
		if (b->count <= 0 || b->status == DC_MENU_ST_MISSING)
			return DC_MENU_ERROR;
		e = &b->entries[b->cursor];
		if (e->is_dir) {
			strncpy(b->path, e->path, sizeof(b->path) - 1);
			load_dir(b);
			clamp_scroll(b);
			break;
		}
		strncpy(b->chosen, e->path, sizeof(b->chosen) - 1);
		return DC_MENU_OK;
	default:
		break;
	}
	return -1; /* still browsing */
}

static void draw_help_chip(int x, int y, const char *key, const char *label)
{
	int kw = dc_draw_text_width(key) + 10;

	dc_draw_fill_rect(x, y, kw, 16, DC_COL_ACCENT);
	dc_draw_text(x + 5, y + 4, DC_COL_BG, key);
	dc_draw_text(x + kw + 8, y + 4, DC_COL_DIM, label);
}

void dc_menu_browser_draw(const dc_menu_browser *b)
{
	char line[96];
	char shown[80];
	int i, y, last;
	int total;

	dc_draw_clear(DC_COL_BG);
	dc_draw_fill_rect(0, 0, DC_FB_W, HEADER_H, DC_COL_BG2);
	dc_draw_fill_rect(0, 0, 8, HEADER_H, DC_COL_ACCENT);
	dc_draw_text_scaled(24, 10, DC_COL_TEXT, "Not64", 2);
	dc_draw_text(24 + dc_draw_text_width("Not64") * 2 + 16, 18,
		     DC_COL_ACCENT2, "Dreamcast");
	dc_menu_ellipsize(shown, sizeof(shown), b->path, 52);
	dc_draw_text(24, 40, DC_COL_DIM, shown);
	dc_draw_fill_rect(0, HEADER_H, DC_FB_W, 2, DC_COL_LINE);

	total = b->count;
	if (total > 0) {
		snprintf(line, sizeof(line), "%d of %d", b->cursor + 1, total);
		dc_draw_text(DC_FB_W - 8 - dc_draw_text_width(line), 40,
			     DC_COL_DIM, line);
	}

	dc_draw_fill_rect(LIST_X - 12, LIST_Y - 12, LIST_W + 24,
			  DC_MENU_VISIBLE * ROW_H + 16, DC_COL_PANEL);
	dc_draw_rect(LIST_X - 12, LIST_Y - 12, LIST_W + 24,
		     DC_MENU_VISIBLE * ROW_H + 16, DC_COL_LINE);

	if (b->status == DC_MENU_ST_MISSING) {
		dc_draw_text(LIST_X, LIST_Y + 80, DC_COL_WARN,
			     "Could not open this folder.");
		dc_draw_text(LIST_X, LIST_Y + 104, DC_COL_DIM,
			     "Create /sd/not64/roms and add .z64 / .n64 / .v64 dumps.");
	} else if (b->status == DC_MENU_ST_EMPTY) {
		dc_draw_text(LIST_X, LIST_Y + 80, DC_COL_WARN,
			     "No ROMs in this folder.");
		dc_draw_text(LIST_X, LIST_Y + 104, DC_COL_DIM,
			     "Drop a .z64, .n64 or .v64 here, or open a subfolder.");
		if (!at_root(b) && b->count > 0)
			dc_draw_text(LIST_X, LIST_Y + 128, DC_COL_DIR,
				     "A on [..] or B goes up one folder.");
	} else {
		int track_h = DC_MENU_VISIBLE * ROW_H;

		last = b->scroll + DC_MENU_VISIBLE;
		if (last > b->count)
			last = b->count;
		for (i = b->scroll; i < last; ++i) {
			const struct dc_menu_entry *e = &b->entries[i];
			int selected = (i == b->cursor);
			char sizebuf[16];
			uint16_t namecol;

			y = LIST_Y + (i - b->scroll) * ROW_H;
			if (selected) {
				dc_draw_fill_rect(LIST_X - 8, y - 4, LIST_W - 12, ROW_H,
						  DC_COL_SEL_BG);
				dc_draw_fill_rect(LIST_X - 8, y - 4, 4, ROW_H,
						  DC_COL_ACCENT);
			}
			if (selected)
				namecol = DC_COL_TEXT;
			else if (e->is_dir)
				namecol = DC_COL_DIR;
			else
				namecol = DC_COL_FILE;
			if (e->is_parent)
				dc_draw_text(LIST_X, y + 6, namecol, "[..]");
			else if (e->is_dir) {
				snprintf(line, sizeof(line), "[%s]", e->label);
				dc_draw_text(LIST_X, y + 6, namecol, line);
			} else
				dc_draw_text(LIST_X, y + 6, namecol, e->label);
			if (!e->is_dir) {
				dc_menu_format_size(sizebuf, sizeof(sizebuf), e->size);
				dc_draw_text(LIST_X + LIST_W - 28 -
						     dc_draw_text_width(sizebuf),
					     y + 6, DC_COL_DIM, sizebuf);
			}
		}
		if (b->count > DC_MENU_VISIBLE) {
			int thumb_h = track_h * DC_MENU_VISIBLE / b->count;
			int thumb_y;

			if (thumb_h < 12)
				thumb_h = 12;
			thumb_y = LIST_Y +
				  (track_h - thumb_h) * b->scroll /
					  (b->count - DC_MENU_VISIBLE);
			dc_draw_fill_rect(LIST_X + LIST_W - 6, LIST_Y, 4, track_h,
					  DC_COL_BG);
			dc_draw_fill_rect(LIST_X + LIST_W - 6, thumb_y, 4, thumb_h,
					  DC_COL_ACCENT2);
		}
	}

	dc_draw_fill_rect(0, FOOTER_Y, DC_FB_W, DC_FB_H - FOOTER_Y, DC_COL_BG2);
	dc_draw_fill_rect(0, FOOTER_Y, DC_FB_W, 2, DC_COL_LINE);
	draw_help_chip(24, FOOTER_Y + 12, "A", "Open");
	draw_help_chip(140, FOOTER_Y + 12, "B", "Back");
	draw_help_chip(256, FOOTER_Y + 12, "Start", "Boot");
	draw_help_chip(420, FOOTER_Y + 12, "Y", "Quit");
	dc_draw_text(24, FOOTER_Y + 36, DC_COL_DIM,
		     "In-game: LT=Z  RT=R  Y+LT=L  both+D-pad=C");
}

int dc_menu_decode_pad(unsigned int buttons, unsigned int *prev_buttons)
{
	unsigned int prev = prev_buttons ? *prev_buttons : 0;
	unsigned int edge = buttons & ~prev;
	int act = DC_MENU_ACT_NONE;

	if (prev_buttons)
		*prev_buttons = buttons;

#ifdef DC_HOST_STUB
	if (edge & DC_CONT_DPAD_UP)
		act = DC_MENU_ACT_UP;
	else if (edge & DC_CONT_DPAD_DOWN)
		act = DC_MENU_ACT_DOWN;
	else if (edge & DC_CONT_DPAD_LEFT)
		act = DC_MENU_ACT_PAGE_UP;
	else if (edge & DC_CONT_DPAD_RIGHT)
		act = DC_MENU_ACT_PAGE_DOWN;
	else if (edge & (DC_CONT_A | DC_CONT_START))
		act = DC_MENU_ACT_CONFIRM;
	else if (edge & DC_CONT_B)
		act = DC_MENU_ACT_BACK;
	else if (edge & DC_CONT_Y)
		act = DC_MENU_ACT_QUIT;
#else
	if (edge & CONT_DPAD_UP)
		act = DC_MENU_ACT_UP;
	else if (edge & CONT_DPAD_DOWN)
		act = DC_MENU_ACT_DOWN;
	else if (edge & CONT_DPAD_LEFT)
		act = DC_MENU_ACT_PAGE_UP;
	else if (edge & CONT_DPAD_RIGHT)
		act = DC_MENU_ACT_PAGE_DOWN;
	else if (edge & (CONT_A | CONT_START))
		act = DC_MENU_ACT_CONFIRM;
	else if (edge & CONT_B)
		act = DC_MENU_ACT_BACK;
	else if (edge & CONT_Y)
		act = DC_MENU_ACT_QUIT;
#endif
	return act;
}

#ifdef DC_HOST_STUB
static int stdin_flags_saved;
static int stdin_raw;
static struct termios stdin_term_saved;

static void host_input_begin(void)
{
	struct termios t;

	stdin_raw = 0;
	if (!isatty(STDIN_FILENO))
		return;
	if (tcgetattr(STDIN_FILENO, &stdin_term_saved) != 0)
		return;
	t = stdin_term_saved;
	t.c_lflag &= ~(ICANON | ECHO);
	t.c_cc[VMIN] = 0;
	t.c_cc[VTIME] = 0;
	if (tcsetattr(STDIN_FILENO, TCSANOW, &t) != 0)
		return;
	stdin_flags_saved = fcntl(STDIN_FILENO, F_GETFL, 0);
	fcntl(STDIN_FILENO, F_SETFL, stdin_flags_saved | O_NONBLOCK);
	stdin_raw = 1;
}

static void host_input_end(void)
{
	if (!stdin_raw)
		return;
	tcsetattr(STDIN_FILENO, TCSANOW, &stdin_term_saved);
	fcntl(STDIN_FILENO, F_SETFL, stdin_flags_saved);
	stdin_raw = 0;
}

static int host_keyboard_action(void)
{
	unsigned char buf[8];
	int n = (int)read(STDIN_FILENO, buf, sizeof(buf));

	if (n <= 0)
		return DC_MENU_ACT_NONE;
	if (buf[0] == 0x1b && n >= 3 && buf[1] == '[') {
		if (buf[2] == 'A')
			return DC_MENU_ACT_UP;
		if (buf[2] == 'B')
			return DC_MENU_ACT_DOWN;
		if (buf[2] == 'C')
			return DC_MENU_ACT_PAGE_DOWN;
		if (buf[2] == 'D')
			return DC_MENU_ACT_PAGE_UP;
	}
	if (buf[0] == '\n' || buf[0] == '\r' || buf[0] == ' ')
		return DC_MENU_ACT_CONFIRM;
	if (buf[0] == 0x7f || buf[0] == 0x08 || buf[0] == 0x1b)
		return DC_MENU_ACT_BACK;
	if (buf[0] == 'q' || buf[0] == 'Q')
		return DC_MENU_ACT_QUIT;
	if (buf[0] == 'w' || buf[0] == 'W')
		return DC_MENU_ACT_UP;
	if (buf[0] == 's' || buf[0] == 'S')
		return DC_MENU_ACT_DOWN;
	return DC_MENU_ACT_NONE;
}
#endif

static int poll_action(unsigned int *prev, unsigned int *hold)
{
	unsigned int buttons = 0;
	int act;

	controller_DC_poll_raw(0, &buttons, NULL, NULL, NULL, NULL);
	act = dc_menu_decode_pad(buttons, prev);
#ifdef DC_HOST_STUB
	if (act == DC_MENU_ACT_NONE)
		act = host_keyboard_action();
#endif
	if (act == DC_MENU_ACT_NONE && hold) {
#ifdef DC_HOST_STUB
		unsigned int dir = buttons & (DC_CONT_DPAD_UP | DC_CONT_DPAD_DOWN |
					      DC_CONT_DPAD_LEFT | DC_CONT_DPAD_RIGHT);
#else
		unsigned int dir = buttons & (CONT_DPAD_UP | CONT_DPAD_DOWN |
					      CONT_DPAD_LEFT | CONT_DPAD_RIGHT);
#endif
		if (dir) {
			++*hold;
			if (*hold >= 14 && (*hold % 3) == 0)
				act = dc_menu_decode_pad(dir, NULL);
		} else
			*hold = 0;
	} else if (hold)
		*hold = 0;
	return act;
}

int dc_menu_pick_rom(char *out_path, size_t out_len, unsigned int max_frames)
{
	dc_menu_browser *b;
	unsigned int prev = 0;
	unsigned int hold = 0;
	unsigned int frame = 0;
	int result = DC_MENU_QUIT;
	const char *root;

	if (skipMenu)
		return DC_MENU_SKIP;
	if (dc_draw_init() != 0)
		return DC_MENU_ERROR;

	fileBrowser_kos_bind();
	root = romFile_topLevel ? romFile_topLevel->name : "./roms";
	b = dc_menu_browser_create(root);
	if (!b) {
		dc_draw_shutdown();
		return DC_MENU_ERROR;
	}

#ifdef DC_HOST_STUB
	host_input_begin();
#endif

	for (;;) {
		int act, applied;

		if (max_frames && frame++ >= max_frames) {
			result = DC_MENU_QUIT;
			break;
		}
		dc_menu_browser_draw(b);
		dc_draw_present();
#ifdef DC_HOST_STUB
		/* Avoid a busy spin when a human is driving --menu. */
		if (!max_frames)
			usleep(16000);
#else
		vid_waitvbl();
#endif
		act = poll_action(&prev, &hold);
		if (act == DC_MENU_ACT_NONE)
			continue;
		applied = dc_menu_browser_apply(b, act);
		if (applied == DC_MENU_OK) {
			if (out_path && out_len) {
				strncpy(out_path, dc_menu_browser_chosen(b),
					out_len - 1);
				out_path[out_len - 1] = '\0';
			}
			result = DC_MENU_OK;
			break;
		}
		if (applied == DC_MENU_QUIT) {
			result = DC_MENU_QUIT;
			break;
		}
	}

#ifdef DC_HOST_STUB
	host_input_end();
#endif
	dc_menu_browser_destroy(b);
	dc_draw_shutdown();
	return result;
}

#ifdef DC_HOST_STUB

static int failf(int *fails, const char *msg)
{
	printf("menu selftest FAIL: %s\n", msg);
	++*fails;
	return 0;
}

static int expect(int *fails, int cond, const char *msg)
{
	if (!cond)
		failf(fails, msg);
	return cond;
}

static void write_file(const char *path, const char *data)
{
	FILE *fp = fopen(path, "wb");
	if (fp) {
		fputs(data, fp);
		fclose(fp);
	}
}

int dc_menu_selftest(void)
{
	int fails = 0;
	char buf[64];
	char tmpdir[] = "/tmp/not64-menu-XXXXXX";
	char sub[256], rom_a[256], rom_b[256], junk[256];
	dc_menu_browser *b;
	unsigned int prev;
	const char *ppm = "/tmp/not64-menu.ppm";

	printf("menu selftest: helpers\n");
	expect(&fails, dc_menu_is_rom_name("foo.z64"), "z64 is a ROM");
	expect(&fails, dc_menu_is_rom_name("FOO.N64"), "N64 is a ROM");
	expect(&fails, dc_menu_is_rom_name("bar.v64"), "v64 is a ROM");
	expect(&fails, !dc_menu_is_rom_name("gen_dc_roms.py"), "py is not a ROM");
	expect(&fails, !dc_menu_is_rom_name("readme.txt"), "txt is not a ROM");
	/* The helper is the extension; the loader separately skips dotfiles. */
	expect(&fails, dc_menu_is_rom_name(".hidden.z64"), "extension still matches");

	dc_menu_ellipsize(buf, sizeof(buf), "short", 10);
	expect(&fails, !strcmp(buf, "short"), "short name unchanged");
	dc_menu_ellipsize(buf, sizeof(buf), "abcdefghijklmnop", 10);
	expect(&fails, !strcmp(buf, "abcdefg..."), "ellipsize to 10");
	dc_menu_format_size(buf, sizeof(buf), 500);
	expect(&fails, !strcmp(buf, "500 B"), "bytes");
	dc_menu_format_size(buf, sizeof(buf), 4096);
	expect(&fails, strstr(buf, "K") != NULL, "kilobytes");

	prev = 0;
	expect(&fails,
	       dc_menu_decode_pad(DC_CONT_DPAD_DOWN, &prev) == DC_MENU_ACT_DOWN,
	       "down edge");
	expect(&fails,
	       dc_menu_decode_pad(DC_CONT_DPAD_DOWN, &prev) == DC_MENU_ACT_NONE,
	       "down hold is not a repeat");
	expect(&fails,
	       dc_menu_decode_pad(0, &prev) == DC_MENU_ACT_NONE, "release");
	expect(&fails,
	       dc_menu_decode_pad(DC_CONT_A, &prev) == DC_MENU_ACT_CONFIRM,
	       "A confirms");
	prev = 0;
	expect(&fails,
	       dc_menu_decode_pad(DC_CONT_START, &prev) == DC_MENU_ACT_CONFIRM,
	       "Start confirms");

	if (!mkdtemp(tmpdir)) {
		failf(&fails, "mkdtemp");
		printf("menu selftest: %d failure(s)\n", fails);
		return fails ? 1 : 0;
	}
	snprintf(sub, sizeof(sub), "%s/folder", tmpdir);
	snprintf(rom_a, sizeof(rom_a), "%s/alpha.z64", tmpdir);
	snprintf(rom_b, sizeof(rom_b), "%s/zeta.n64", tmpdir);
	snprintf(junk, sizeof(junk), "%s/notes.txt", tmpdir);
	mkdir(sub, 0755);
	write_file(rom_a, "aaaa");
	write_file(rom_b, "bbbbbbbb");
	write_file(junk, "nope");
	write_file("/tmp/not64-menu-ignore.py", "print(1)\n");

	printf("menu selftest: listing %s\n", tmpdir);
	b = dc_menu_browser_create(tmpdir);
	expect(&fails, b != NULL, "browser created");
	if (b) {
		int i, saw_txt = 0, dirs = 0;
		/* dirs first (folder), then alpha, zeta. no notes.txt */
		expect(&fails, dc_menu_browser_count(b) == 3, "3 entries (dir + 2 roms)");
		expect(&fails, dc_menu_browser_entry_is_dir(b, 0), "dir first");
		expect(&fails, !strcmp(dc_menu_browser_entry_name(b, 0), "folder") ||
				       strstr(dc_menu_browser_entry_name(b, 0), "folder") != NULL,
		       "folder label");
		expect(&fails, !dc_menu_browser_entry_is_dir(b, 1), "rom after dirs");
		expect(&fails, strstr(dc_menu_browser_entry_name(b, 1), "alpha") != NULL,
		       "alpha before zeta");
		expect(&fails, strstr(dc_menu_browser_entry_name(b, 2), "zeta") != NULL,
		       "zeta last");
		for (i = 0; i < dc_menu_browser_count(b); ++i) {
			if (strstr(dc_menu_browser_entry_name(b, i), "notes"))
				saw_txt = 1;
			if (dc_menu_browser_entry_is_dir(b, i))
				dirs++;
		}
		expect(&fails, !saw_txt, "non-ROM filtered");
		expect(&fails, dirs == 1, "one real directory");

		expect(&fails, dc_menu_browser_apply(b, DC_MENU_ACT_DOWN) == -1,
		       "down stays in browser");
		expect(&fails, dc_menu_browser_cursor(b) == 1, "cursor on alpha");
		expect(&fails, dc_menu_browser_apply(b, DC_MENU_ACT_CONFIRM) == DC_MENU_OK,
		       "A boots ROM");
		expect(&fails, strstr(dc_menu_browser_chosen(b), "alpha.z64") != NULL,
		       "chosen alpha.z64");

		/* Re-open to test folder + parent + wrap + empty. */
		dc_menu_browser_destroy(b);
		b = dc_menu_browser_create(tmpdir);
		dc_menu_browser_apply(b, DC_MENU_ACT_CONFIRM); /* enter folder */
		expect(&fails, dc_menu_browser_status(b) == DC_MENU_ST_EMPTY,
		       "empty subfolder");
		expect(&fails, dc_menu_browser_entry_is_parent(b, 0), ".. present");
		expect(&fails, dc_menu_browser_apply(b, DC_MENU_ACT_BACK) == -1,
		       "B leaves folder");
		expect(&fails, strcmp(dc_menu_browser_path(b), tmpdir) == 0,
		       "back at root");
		expect(&fails, dc_menu_browser_apply(b, DC_MENU_ACT_BACK) == DC_MENU_QUIT,
		       "B at root quits");

		dc_menu_browser_destroy(b);
		b = dc_menu_browser_create(tmpdir);
		expect(&fails, dc_menu_browser_apply(b, DC_MENU_ACT_UP) == -1, "wrap up");
		expect(&fails, dc_menu_browser_cursor(b) == dc_menu_browser_count(b) - 1,
		       "wrap to last");
		dc_menu_browser_apply(b, DC_MENU_ACT_DOWN);
		expect(&fails, dc_menu_browser_cursor(b) == 0, "wrap to first");

		/* Page keys move a full screen; with 3 items they clamp. */
		dc_menu_browser_apply(b, DC_MENU_ACT_PAGE_DOWN);
		expect(&fails, dc_menu_browser_cursor(b) == dc_menu_browser_count(b) - 1,
		       "page down clamps");

		if (dc_draw_init() == 0) {
			int accent, text;
			dc_menu_browser_draw(b);
			accent = dc_draw_count_colour(DC_COL_ACCENT);
			text = dc_draw_count_colour(DC_COL_TEXT);
			expect(&fails, accent > 50, "accent pixels drawn");
			expect(&fails, text > 50, "text pixels drawn");
			expect(&fails, dc_draw_write_ppm(ppm) == 0, "wrote PPM");
			printf("menu selftest: snapshot %s\n", ppm);
			dc_draw_shutdown();
			expect(&fails, !dc_draw_ready(), "framebuffer freed");
		} else
			failf(&fails, "draw init");

		dc_menu_browser_destroy(b);
	}

	{
		char crowded[256];
		int n, idx = -1;

		snprintf(crowded, sizeof(crowded), "%s/crowded", tmpdir);
		mkdir(crowded, 0755);
		for (n = 0; n < 20; ++n) {
			char p[320];
			snprintf(p, sizeof(p), "%s/rom%02d.z64", crowded, n);
			write_file(p, "rom");
		}
		b = dc_menu_browser_create(tmpdir);
		for (n = 0; n < dc_menu_browser_count(b); ++n)
			if (strstr(dc_menu_browser_entry_name(b, n), "crowded"))
				idx = n;
		expect(&fails, idx >= 0, "crowded dir listed");
		while (dc_menu_browser_cursor(b) != idx)
			dc_menu_browser_apply(b, DC_MENU_ACT_DOWN);
		expect(&fails, dc_menu_browser_apply(b, DC_MENU_ACT_CONFIRM) == -1,
		       "enter crowded");
		expect(&fails, dc_menu_browser_count(b) == 21, "20 roms + parent");
		{
			int k;
			for (k = 0; k < 15; ++k)
				dc_menu_browser_apply(b, DC_MENU_ACT_DOWN);
		}
		expect(&fails, dc_menu_browser_cursor(b) == 15, "cursor 15");
		expect(&fails, dc_menu_browser_scroll(b) == 15 - DC_MENU_VISIBLE + 1,
		       "scroll keeps cursor visible");
		if (dc_draw_init() == 0) {
			dc_menu_browser_draw(b);
			expect(&fails, dc_draw_count_colour(DC_COL_ACCENT2) > 0,
			       "scrollbar thumb");
			dc_draw_write_ppm("/tmp/not64-menu-scroll.ppm");
			dc_draw_shutdown();
		}
		dc_menu_browser_destroy(b);
	}

	{
		char missing[256];
		snprintf(missing, sizeof(missing), "%s-nope", tmpdir);
		b = dc_menu_browser_create(missing);
		expect(&fails, b && dc_menu_browser_status(b) == DC_MENU_ST_MISSING,
		       "missing folder");
		if (dc_draw_init() == 0) {
			dc_menu_browser_draw(b);
			expect(&fails, dc_draw_count_colour(DC_COL_WARN) > 0,
			       "missing-state warning colour");
			dc_draw_write_ppm("/tmp/not64-menu-missing.ppm");
			dc_draw_shutdown();
		}
		dc_menu_browser_destroy(b);
	}

	skipMenu = 1;
	expect(&fails, dc_menu_pick_rom(buf, sizeof(buf), 8) == DC_MENU_SKIP,
	       "skipMenu bypasses picker");
	skipMenu = 0;

	if (dc_draw_init() == 0) {
		dc_menu_browser *roms = dc_menu_browser_create("./roms");
		if (roms) {
			dc_menu_browser_draw(roms);
			dc_draw_write_ppm("/tmp/not64-menu-roms.ppm");
			dc_menu_browser_destroy(roms);
		}
		dc_draw_shutdown();
	}

	/* Scripted pad: idle, down, release, A — pick second item (a ROM,
	 * because dirs sort first: folder, alpha, zeta). After one down we
	 * are on alpha. */
	{
		char picked[FILE_BROWSER_MAX_PATH_LEN];
		extern fileBrowser_file topLevel_kos;
		fileBrowser_file saved = topLevel_kos;

		strncpy(topLevel_kos.name, tmpdir, FILE_BROWSER_MAX_PATH_LEN - 1);
		controller_DC_host_set(0, 0, 128, 128);
		/* pick_rom loops: frame 0 draw, frame 1 down, 2 release, 3 A */
		{
			/* Drive pick_rom with a tiny helper loop instead —
			 * host_set is sampled each frame, so we cannot change
			 * it from inside pick_rom. Use the browser API. */
			int guard = 0;
			b = dc_menu_browser_create(tmpdir);
			prev = 0;
			/* Dirs sort first; walk until the cursor is a ROM. */
			while (dc_menu_browser_entry_is_dir(b, dc_menu_browser_cursor(b)) &&
			       guard++ < dc_menu_browser_count(b)) {
				int r = dc_menu_browser_apply(
					b, dc_menu_decode_pad(DC_CONT_DPAD_DOWN, &prev));
				expect(&fails, r == -1, "script down");
				dc_menu_decode_pad(0, &prev);
			}
			expect(&fails, !dc_menu_browser_entry_is_dir(b, dc_menu_browser_cursor(b)),
			       "landed on a ROM");
			{
				int r = dc_menu_browser_apply(
					b, dc_menu_decode_pad(DC_CONT_A, &prev));
				expect(&fails, r == DC_MENU_OK, "script A");
			}
			strncpy(picked, dc_menu_browser_chosen(b), sizeof(picked) - 1);
			expect(&fails, strstr(picked, ".z64") != NULL ||
					       strstr(picked, ".n64") != NULL,
			       "script picked a ROM");
			dc_menu_browser_destroy(b);
		}
		topLevel_kos = saved;
	}

	printf("menu selftest: %s (%d failure(s))\n",
	       fails ? "FAIL" : "PASS", fails);
	return fails ? 1 : 0;
}

#endif /* DC_HOST_STUB */
