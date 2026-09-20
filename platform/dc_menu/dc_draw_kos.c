/**
 * KallistiOS backend for the menu drawing surface: BIOS font into vram.
 *
 * NOT YET COMPILED. The environment this was written in has no sh-elf-gcc and
 * no KallistiOS, so every other DC file here is reached through the host stub
 * and this one is not reachable at all. It is written against the conventional
 * KOS 2.x API (vram_s, vid_set_mode, bfont_draw_str, BFONT_THIN_WIDTH,
 * BFONT_HEIGHT). Expect to fix the bfont call signature on first build -- KOS
 * has changed it across versions, and bfont_draw_str_ex exists in newer trees.
 * Nothing above this file depends on those details: dc_menu.c only uses
 * dc_draw.h, and the host backend exercises the same interface.
 *
 * v1 draws straight into the visible framebuffer. There is no double buffer,
 * so dc_draw_begin() clears and the frame is built in view; at menu redraw
 * rates that is what the Wii menu's software path did too. Phase 7 can put a
 * PVR surface behind the same three calls.
 */

#include <string.h>

#include <kos.h>
#include <dc/video.h>
#include <dc/biosfont.h>

#include "dc_draw.h"

#define KOS_W      640
#define KOS_H      480
#define KOS_CHAR_W BFONT_THIN_WIDTH
#define KOS_CHAR_H BFONT_HEIGHT

static int started;

int dc_draw_init(void)
{
	if (started)
		return 0;
	vid_set_mode(DM_640x480, PM_RGB565);
	started = 1;
	return 0;
}

void dc_draw_shutdown(void)
{
	/* The core sets its own mode when a ROM starts; just stop drawing. */
	started = 0;
}

void dc_draw_begin(uint16_t clear_rgb565)
{
	if (!started)
		return;
	dc_draw_fill_rect(0, 0, KOS_W, KOS_H, clear_rgb565);
}

void dc_draw_end(void)
{
	if (started)
		vid_waitvbl();
}

void dc_draw_fill_rect(int x, int y, int w, int h, uint16_t rgb565)
{
	int row;

	if (!started)
		return;
	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (x >= KOS_W || y >= KOS_H || w <= 0 || h <= 0)
		return;
	if (x + w > KOS_W) w = KOS_W - x;
	if (y + h > KOS_H) h = KOS_H - y;

	for (row = 0; row < h; ++row) {
		uint16_t *dst = vram_s + (y + row) * KOS_W + x;
		int col;
		for (col = 0; col < w; ++col)
			dst[col] = rgb565;
	}
}

void dc_draw_text(int x, int y, uint16_t rgb565, const char *str)
{
	if (!started || !str)
		return;
	if (x < 0 || y < 0 || y + KOS_CHAR_H > KOS_H)
		return;

	bfont_set_foreground_color(rgb565);
	/* opaque = 0: leave the background alone, so text can sit on a filled
	 * selection bar without punching a box through it. */
	bfont_draw_str(vram_s + y * KOS_W + x, KOS_W, 0, str);
}

void dc_draw_blit(int x, int y, int w, int h, const uint16_t *src)
{
	int row;

	if (!started || !src)
		return;
	if (x < 0 || y < 0 || w <= 0 || h <= 0)
		return;
	if (x + w > KOS_W || y + h > KOS_H)
		return;

	for (row = 0; row < h; ++row)
		memcpy(vram_s + (y + row) * KOS_W + x,
		       src + (size_t)row * w,
		       (size_t)w * sizeof(uint16_t));
}

int dc_draw_width(void)  { return KOS_W; }
int dc_draw_height(void) { return KOS_H; }
int dc_draw_char_w(void) { return KOS_CHAR_W; }
int dc_draw_char_h(void) { return KOS_CHAR_H; }
