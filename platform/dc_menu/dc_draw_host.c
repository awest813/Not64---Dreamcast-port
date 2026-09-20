/**
 * Host-stub backend for the menu drawing surface.
 *
 * There is no framebuffer here. Text lands in a character grid the same shape
 * as the Dreamcast's (640x480 through a 12x24 BIOS font = 53x20), so a test
 * can drive the menu and assert what it put on screen. Rectangles and blits
 * only clear the cells they cover; the menu uses them for the title bar and
 * the selection highlight, neither of which carries information the text does
 * not already carry.
 */

#include <stdio.h>
#include <string.h>

#include "dc_draw.h"

#define HOST_W      640
#define HOST_H      480
#define HOST_CHAR_W 12
#define HOST_CHAR_H 24
#define HOST_COLS   (HOST_W / HOST_CHAR_W)
#define HOST_ROWS   (HOST_H / HOST_CHAR_H)

static char grid[HOST_ROWS][HOST_COLS + 1];
static int  started;

int dc_draw_init(void)
{
	started = 1;
	dc_draw_begin(0);
	return 0;
}

void dc_draw_shutdown(void)
{
	started = 0;
}

void dc_draw_begin(uint16_t clear_rgb565)
{
	int r;

	(void)clear_rgb565;
	for (r = 0; r < HOST_ROWS; ++r) {
		memset(grid[r], ' ', HOST_COLS);
		grid[r][HOST_COLS] = '\0';
	}
}

void dc_draw_end(void)
{
}

void dc_draw_wait(void)
{
	/* No display to wait for. */
}

void dc_draw_fill_rect(int x, int y, int w, int h, uint16_t rgb565)
{
	(void)x; (void)y; (void)w; (void)h; (void)rgb565;
}

void dc_draw_text(int x, int y, uint16_t rgb565, const char *str)
{
	int row = y / HOST_CHAR_H;
	int col = x / HOST_CHAR_W;
	int i;

	(void)rgb565;
	if (!started || !str)
		return;
	if (row < 0 || row >= HOST_ROWS)
		return;

	for (i = 0; str[i] && col + i < HOST_COLS; ++i) {
		if (col + i < 0)
			continue;
		grid[row][col + i] = str[i];
	}
}

void dc_draw_blit(int x, int y, int w, int h, const uint16_t *src)
{
	(void)x; (void)y; (void)w; (void)h; (void)src;
}

int dc_draw_width(void)  { return HOST_W; }
int dc_draw_height(void) { return HOST_H; }
int dc_draw_char_w(void) { return HOST_CHAR_W; }
int dc_draw_char_h(void) { return HOST_CHAR_H; }

const char *dc_draw_host_row(int row)
{
	if (row < 0 || row >= HOST_ROWS)
		return NULL;
	return grid[row];
}

int dc_draw_host_rows(void)
{
	return HOST_ROWS;
}
