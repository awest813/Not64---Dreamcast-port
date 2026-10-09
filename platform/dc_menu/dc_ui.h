#ifndef DC_UI_H
#define DC_UI_H

#include "dc_draw.h"
#include "../../gc_input/controller.h"

/* Ten large rows, with a safe border for interlaced TVs and overscan. */
#define DC_UI_FIRST_ROW 4
#define DC_UI_ROWS 10
#define DC_UI_BG DC_RGB(12, 18, 29)
#define DC_UI_PANEL DC_RGB(24, 36, 53)
#define DC_UI_FOCUS DC_RGB(35, 64, 83)
#define DC_UI_ACCENT DC_RGB(107, 230, 203)
#define DC_UI_TEXT DC_RGB(237, 243, 249)
#define DC_UI_MUTED DC_RGB(158, 177, 196)
#define DC_UI_WARN DC_RGB(255, 202, 116)

enum {
    DC_UI_A=1, DC_UI_B=2, DC_UI_START=4,
    DC_UI_UP=8, DC_UI_DOWN=16, DC_UI_LEFT=32, DC_UI_RIGHT=64,
    DC_UI_PAGE_UP=128, DC_UI_PAGE_DOWN=256
};
typedef struct {
    unsigned prev;
    unsigned held[4];
    int release;
} dc_ui_input;

void dc_ui_input_init(dc_ui_input *input, int wait_for_release);
unsigned dc_ui_input_step(dc_ui_input *input, const BUTTONS *keys);
/* Unshifted controls. Returns zero on disconnect and supplies neutral keys. */
int dc_ui_poll(BUTTONS *keys, unsigned *raw);
void dc_ui_scroll(int *cursor, int *top, int count, int rows, unsigned input);

int dc_ui_columns(void);
void dc_ui_ellipsize(char *out, size_t size, const char *text, int columns);
void dc_ui_begin(const char *title, const char *subtitle);
void dc_ui_text(int row, uint16_t color, const char *text);
void dc_ui_paragraph(int row, int lines, uint16_t color, const char *text);
void dc_ui_row(int row, const char *label, const char *value, int selected);
void dc_ui_finish(const char *status, const char *hints);
/* A confirms; B cancels. Held input from the previous screen is consumed. */
int dc_ui_confirm(const char *title, const char *message, const char *verb);

#endif
