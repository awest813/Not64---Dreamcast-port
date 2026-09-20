#ifndef DC_MENU_H
#define DC_MENU_H

#include <stddef.h>

enum {
	DC_MENU_ACT_NONE = 0,
	DC_MENU_ACT_UP,
	DC_MENU_ACT_DOWN,
	DC_MENU_ACT_PAGE_UP,
	DC_MENU_ACT_PAGE_DOWN,
	DC_MENU_ACT_CONFIRM,
	DC_MENU_ACT_BACK,
	DC_MENU_ACT_QUIT
};

enum {
	DC_MENU_OK = 0,     /* path filled */
	DC_MENU_QUIT = 1,   /* user backed out */
	DC_MENU_SKIP = 2,   /* skipMenu / argv path */
	DC_MENU_ERROR = 3
};

enum {
	DC_MENU_ST_OK = 0,
	DC_MENU_ST_EMPTY = 1,
	DC_MENU_ST_MISSING = 2
};

#define DC_MENU_VISIBLE 12
#define DC_MENU_MAX_ENTRIES 256

int dc_menu_is_rom_name(const char *name);
void dc_menu_ellipsize(char *dst, size_t dst_len, const char *src, int max_chars);
void dc_menu_format_size(char *dst, size_t dst_len, unsigned int bytes);

typedef struct dc_menu_browser dc_menu_browser;

dc_menu_browser *dc_menu_browser_create(const char *root);
void dc_menu_browser_destroy(dc_menu_browser *b);
int dc_menu_browser_apply(dc_menu_browser *b, int action);
void dc_menu_browser_draw(const dc_menu_browser *b);

int dc_menu_browser_count(const dc_menu_browser *b);
int dc_menu_browser_cursor(const dc_menu_browser *b);
int dc_menu_browser_scroll(const dc_menu_browser *b);
int dc_menu_browser_status(const dc_menu_browser *b);
int dc_menu_browser_entry_is_dir(const dc_menu_browser *b, int i);
int dc_menu_browser_entry_is_parent(const dc_menu_browser *b, int i);
const char *dc_menu_browser_entry_name(const dc_menu_browser *b, int i);
const char *dc_menu_browser_path(const dc_menu_browser *b);
const char *dc_menu_browser_chosen(const dc_menu_browser *b);

/* Decode a raw (unshifted) Maple word into one edge-triggered action. */
int dc_menu_decode_pad(unsigned int buttons, unsigned int *prev_buttons);

/*
 * Interactive ROM picker. Allocates the framebuffer, runs until a ROM is
 * chosen or the user quits, then frees everything. max_frames==0 means
 * until a decision; host tests pass a budget so a stuck pad cannot hang.
 */
int dc_menu_pick_rom(char *out_path, size_t out_len, unsigned int max_frames);

#ifdef DC_HOST_STUB
int dc_menu_selftest(void);
#endif

#endif
