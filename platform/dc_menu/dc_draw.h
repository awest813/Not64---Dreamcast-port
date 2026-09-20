/**
 * Not64 Dreamcast menu — drawing surface.
 *
 * Phase 8 in PORTING.md: the menu draws through exactly three calls so it
 * stays renderer-agnostic. v1 backs them with the KOS BIOS font straight into
 * the framebuffer, which needs no font asset and no renderer; Phase 7 can
 * repoint the same calls at PVR without touching menu logic.
 *
 * Two backends exist:
 *   dc_draw_kos.c    KallistiOS bfont + vram_s. Built only for not64-dc.elf.
 *   dc_draw_host.c   character grid in memory. Built only for the host stub,
 *                    so the menu logic can be tested without a Dreamcast.
 */

#ifndef DC_DRAW_H
#define DC_DRAW_H

#include <stdint.h>

/* 565 literals for the menu palette. */
#define DC_RGB(r, g, b) \
	((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

int  dc_draw_init(void);
void dc_draw_shutdown(void);

/* One frame: begin clears, end presents. */
void dc_draw_begin(uint16_t clear_rgb565);
void dc_draw_end(void);

void dc_draw_fill_rect(int x, int y, int w, int h, uint16_t rgb565);
void dc_draw_text(int x, int y, uint16_t rgb565, const char *str);
void dc_draw_blit(int x, int y, int w, int h, const uint16_t *src);

/* Screen and glyph metrics, so menu layout does not hard-code 640x480/12x24. */
int dc_draw_width(void);
int dc_draw_height(void);
int dc_draw_char_w(void);
int dc_draw_char_h(void);

#ifdef DC_HOST_STUB
/* Host backend only: read back what was drawn, so a test can assert it.
 * Returns a NUL-terminated view of one text row, or NULL if out of range. */
const char *dc_draw_host_row(int row);
int         dc_draw_host_rows(void);
#endif

#endif /* DC_DRAW_H */
