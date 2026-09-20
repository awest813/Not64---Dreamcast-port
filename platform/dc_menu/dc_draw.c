/**
 * Immediate-mode RGB565 surface for the Dreamcast menu.
 *
 * KOS presents into vram; the host stub keeps the buffer so tests can
 * snapshot a PPM. The same 8x8 font is used on both so layout is identical.
 */

#include "dc_draw.h"

#include <stdlib.h>
#include <string.h>

#ifndef DC_HOST_STUB
#include <kos.h>
#endif

#include "dc_font8.inc"

static uint16_t *fb;
static int fb_w, fb_h;

int dc_draw_init(void)
{
	if (fb)
		return 0;
	fb_w = DC_FB_W;
	fb_h = DC_FB_H;
	fb = (uint16_t *)malloc((size_t)fb_w * (size_t)fb_h * sizeof(uint16_t));
	return fb ? 0 : -1;
}

void dc_draw_shutdown(void)
{
	free(fb);
	fb = NULL;
	fb_w = fb_h = 0;
}

int dc_draw_ready(void)
{
	return fb != NULL;
}

const uint16_t *dc_draw_pixels(void)
{
	return fb;
}

void dc_draw_clear(uint16_t rgb565)
{
	int i, n;

	if (!fb)
		return;
	n = fb_w * fb_h;
	for (i = 0; i < n; ++i)
		fb[i] = rgb565;
}

void dc_draw_fill_rect(int x, int y, int w, int h, uint16_t rgb565)
{
	int ix, iy, x0, y0, x1, y1;

	if (!fb || w <= 0 || h <= 0)
		return;
	x0 = x < 0 ? 0 : x;
	y0 = y < 0 ? 0 : y;
	x1 = x + w;
	y1 = y + h;
	if (x1 > fb_w)
		x1 = fb_w;
	if (y1 > fb_h)
		y1 = fb_h;
	for (iy = y0; iy < y1; ++iy) {
		uint16_t *row = fb + iy * fb_w + x0;
		for (ix = x0; ix < x1; ++ix)
			*row++ = rgb565;
	}
}

void dc_draw_rect(int x, int y, int w, int h, uint16_t rgb565)
{
	if (w <= 0 || h <= 0)
		return;
	dc_draw_fill_rect(x, y, w, 1, rgb565);
	dc_draw_fill_rect(x, y + h - 1, w, 1, rgb565);
	dc_draw_fill_rect(x, y, 1, h, rgb565);
	dc_draw_fill_rect(x + w - 1, y, 1, h, rgb565);
}

void dc_draw_blit(int x, int y, int w, int h, const uint16_t *src)
{
	int row, col, x0, y0, x1, y1, sx, sy;

	if (!fb || !src || w <= 0 || h <= 0)
		return;
	x0 = x < 0 ? 0 : x;
	y0 = y < 0 ? 0 : y;
	x1 = x + w;
	y1 = y + h;
	if (x1 > fb_w)
		x1 = fb_w;
	if (y1 > fb_h)
		y1 = fb_h;
	sy = y0 - y;
	for (row = y0; row < y1; ++row, ++sy) {
		sx = x0 - x;
		for (col = x0; col < x1; ++col, ++sx)
			fb[row * fb_w + col] = src[sy * w + sx];
	}
}

static void draw_glyph(int x, int y, unsigned char ch, uint16_t rgb565, int scale)
{
	const unsigned char *g;
	int row, col, px, py, sx, sy;

	if (!fb || scale < 1)
		return;
	if (ch < 32 || ch > 126)
		ch = '?';
	g = dc_font8[ch - 32];
	for (row = 0; row < 8; ++row) {
		unsigned char bits = g[row];
		for (col = 0; col < 8; ++col) {
			if (!(bits & (0x80 >> col)))
				continue;
			px = x + col * scale;
			py = y + row * scale;
			for (sy = 0; sy < scale; ++sy) {
				int yy = py + sy;
				if (yy < 0 || yy >= fb_h)
					continue;
				for (sx = 0; sx < scale; ++sx) {
					int xx = px + sx;
					if (xx < 0 || xx >= fb_w)
						continue;
					fb[yy * fb_w + xx] = rgb565;
				}
			}
		}
	}
}

void dc_draw_text_n(int x, int y, uint16_t rgb565, const char *s, int n)
{
	int i;

	if (!s)
		return;
	for (i = 0; i < n && s[i]; ++i)
		draw_glyph(x + i * DC_FONT_W, y, (unsigned char)s[i], rgb565, 1);
}

void dc_draw_text(int x, int y, uint16_t rgb565, const char *s)
{
	if (!s)
		return;
	dc_draw_text_n(x, y, rgb565, s, 0x7fff);
}

void dc_draw_text_scaled(int x, int y, uint16_t rgb565, const char *s, int scale)
{
	int i;

	if (!s || scale < 1)
		return;
	for (i = 0; s[i]; ++i)
		draw_glyph(x + i * DC_FONT_W * scale, y, (unsigned char)s[i],
			   rgb565, scale);
}

int dc_draw_text_width_n(const char *s, int n)
{
	int i = 0;

	if (!s)
		return 0;
	while (i < n && s[i])
		++i;
	return i * DC_FONT_W;
}

int dc_draw_text_width(const char *s)
{
	return dc_draw_text_width_n(s, 0x7fff);
}

void dc_draw_present(void)
{
	if (!fb)
		return;
#ifndef DC_HOST_STUB
	{
		uint16_t *dst = (uint16_t *)vram_s;
		memcpy(dst, fb, (size_t)fb_w * (size_t)fb_h * sizeof(uint16_t));
	}
#endif
}

int dc_draw_write_ppm(const char *path)
{
	FILE *fp;
	int x, y;

	if (!fb || !path)
		return -1;
	fp = fopen(path, "wb");
	if (!fp)
		return -1;
	fprintf(fp, "P6\n%d %d\n255\n", fb_w, fb_h);
	for (y = 0; y < fb_h; ++y) {
		for (x = 0; x < fb_w; ++x) {
			uint16_t p = fb[y * fb_w + x];
			unsigned char rgb[3];
			rgb[0] = (unsigned char)(((p >> 11) & 0x1F) << 3);
			rgb[1] = (unsigned char)(((p >> 5) & 0x3F) << 2);
			rgb[2] = (unsigned char)((p & 0x1F) << 3);
			if (fwrite(rgb, 1, 3, fp) != 3) {
				fclose(fp);
				return -1;
			}
		}
	}
	fclose(fp);
	return 0;
}

int dc_draw_count_colour(uint16_t rgb565)
{
	int i, n, c = 0;

	if (!fb)
		return 0;
	n = fb_w * fb_h;
	for (i = 0; i < n; ++i)
		if (fb[i] == rgb565)
			++c;
	return c;
}
