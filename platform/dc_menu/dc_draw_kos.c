/**
 * KallistiOS backend for the menu drawing surface: BIOS font into a back
 * buffer, blitted to vram once a frame.
 *
 * Built and run: sh-elf-gcc 9.3.0 / KOS 2.x, booted from a CD image in
 * Flycast. Three things only showed up once it was on screen:
 *
 *  - The first version drew straight into the visible framebuffer, so every
 *    frame was cleared and repainted in full view of the scanout. It tore
 *    badly: captures routinely caught half a menu, rows missing and a name
 *    cut off mid-word.
 *  - Page flipping fixes the tearing, but vid_flip() always leaves vram_s
 *    pointing at the buffer that is NOT on screen, and KOS's framebuffer
 *    console writes through vram_s. Everything the bring-up printed after
 *    the menu went into the hidden buffer. Collapsing back to one buffer
 *    does not help either, because vid_set_mode_ex() clears vram, which
 *    throws away whatever was last drawn.
 *  - So: own back buffer, one blit per frame. vram_s stays the visible
 *    framebuffer throughout, the console keeps working, and the last frame
 *    drawn (the "Loading ..." message) survives shutdown because nothing
 *    clears it. The buffer is freed in dc_draw_shutdown(), which is what
 *    Phase 8 means by the menu releasing everything before go().
 *  - bfont_draw_str() takes a non-const char *, so the string has to be cast.
 *
 * Phase 7 can put a PVR surface behind the same three calls.
 */

#include <stdlib.h>
#include <string.h>

#include <kos.h>
#include <dc/video.h>
#include <dc/biosfont.h>

#include "dc_draw.h"

#define KOS_W      640
#define KOS_H      480
#define KOS_CHAR_W BFONT_THIN_WIDTH
#define KOS_CHAR_H BFONT_HEIGHT

static uint16_t *back;
static int       started;

int dc_draw_init(void)
{
	if (started)
		return 0;

	back = (uint16_t *)malloc((size_t)KOS_W * KOS_H * sizeof(uint16_t));
	if (!back)
		return -1;
	memset(back, 0, (size_t)KOS_W * KOS_H * sizeof(uint16_t));

	vid_set_mode(DM_640x480, PM_RGB565);
	started = 1;
	return 0;
}

void dc_draw_shutdown(void)
{
	if (!started)
		return;
	started = 0;
	/* Leave the last frame on screen: it is the "Loading ..." message, and
	 * the console prints on top of it from here. */
	free(back);
	back = NULL;
}

void dc_draw_begin(uint16_t clear_rgb565)
{
	if (!started)
		return;
	dc_draw_fill_rect(0, 0, KOS_W, KOS_H, clear_rgb565);
}

void dc_draw_end(void)
{
	if (!started)
		return;
	/* One blit on the blanking interval, so a frame never appears partly
	 * drawn. */
	vid_waitvbl();
	memcpy(vram_s, back, (size_t)KOS_W * KOS_H * sizeof(uint16_t));
}

void dc_draw_wait(void)
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
		uint16_t *dst = back + (y + row) * KOS_W + x;
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
	 * selection bar without punching a box through it.
	 * KOS declares the string parameter as char *, not const char *. */
	bfont_draw_str(back + y * KOS_W + x, KOS_W, 0, (char *)str);
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
		memcpy(back + (y + row) * KOS_W + x,
		       src + (size_t)row * w,
		       (size_t)w * sizeof(uint16_t));
}

int dc_draw_width(void)  { return KOS_W; }
int dc_draw_height(void) { return KOS_H; }
int dc_draw_char_w(void) { return KOS_CHAR_W; }
int dc_draw_char_h(void) { return KOS_CHAR_H; }
