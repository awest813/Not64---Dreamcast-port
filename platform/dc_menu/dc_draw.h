#ifndef DC_DRAW_H
#define DC_DRAW_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define DC_FB_W 640
#define DC_FB_H 480
#define DC_FONT_W 8
#define DC_FONT_H 8

#define DC_RGB565(r, g, b) \
	(uint16_t)(((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | (((b) & 0xF8) >> 3)))

/* Dreamcast-ish chrome: navy field, orange accent, paper text. */
#define DC_COL_BG       DC_RGB565(10, 18, 36)
#define DC_COL_BG2      DC_RGB565(16, 28, 52)
#define DC_COL_PANEL    DC_RGB565(22, 38, 68)
#define DC_COL_ACCENT   DC_RGB565(228, 90, 32)
#define DC_COL_ACCENT2  DC_RGB565(255, 156, 64)
#define DC_COL_TEXT     DC_RGB565(236, 240, 244)
#define DC_COL_DIM      DC_RGB565(140, 156, 176)
#define DC_COL_DIR      DC_RGB565(120, 200, 220)
#define DC_COL_FILE     DC_RGB565(210, 218, 228)
#define DC_COL_SEL_BG   DC_RGB565(40, 56, 88)
#define DC_COL_WARN     DC_RGB565(255, 196, 72)
#define DC_COL_LINE     DC_RGB565(48, 68, 104)

int dc_draw_init(void);
void dc_draw_shutdown(void);
int dc_draw_ready(void);

void dc_draw_clear(uint16_t rgb565);
void dc_draw_fill_rect(int x, int y, int w, int h, uint16_t rgb565);
void dc_draw_rect(int x, int y, int w, int h, uint16_t rgb565);
void dc_draw_blit(int x, int y, int w, int h, const uint16_t *src);
void dc_draw_text(int x, int y, uint16_t rgb565, const char *s);
void dc_draw_text_n(int x, int y, uint16_t rgb565, const char *s, int n);
void dc_draw_text_scaled(int x, int y, uint16_t rgb565, const char *s, int scale);

int dc_draw_text_width(const char *s);
int dc_draw_text_width_n(const char *s, int n);

void dc_draw_present(void);
int dc_draw_write_ppm(const char *path);
const uint16_t *dc_draw_pixels(void);

/* Count pixels matching a colour — used by the host UI self-test. */
int dc_draw_count_colour(uint16_t rgb565);

#endif
