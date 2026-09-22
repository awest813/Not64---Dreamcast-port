#ifndef DC_VI_H
#define DC_VI_H
#include <stddef.h>
#include <stdint.h>

#ifdef DC_VI_HIGHRES
#define DC_VI_MAX_WIDTH 640u
#define DC_VI_MAX_HEIGHT 480u
#define DC_VI_TEXTURE_WIDTH 1024u
#define DC_VI_TEXTURE_HEIGHT 512u
#else
#define DC_VI_MAX_WIDTH 320u
#define DC_VI_MAX_HEIGHT 240u
#define DC_VI_TEXTURE_WIDTH 512u
#define DC_VI_TEXTURE_HEIGHT 256u
#endif
#define DC_VI_TEXTURE_BYTES (DC_VI_TEXTURE_WIDTH * DC_VI_TEXTURE_HEIGHT * 2u)

typedef struct {
    uint32_t status, origin, stride, h_start, v_start, x_scale, y_scale;
    unsigned field;
} dc_vi_state;
typedef struct {
    uint16_t pixels[DC_VI_TEXTURE_WIDTH * DC_VI_TEXTURE_HEIGHT] __attribute__((aligned(32)));
    unsigned width, height;
    unsigned interlaced;
} dc_vi_frame;
typedef enum { DC_VI_READY, DC_VI_BLANK, DC_VI_UNSUPPORTED, DC_VI_INVALID } dc_vi_result;

/* Native-resolution preview, not the N64 VI filter pipeline. DC_VI_HIGHRES
 * additionally enables field weaving and vertical fractional interpolation.
 * Input is little-endian word-swapped RDRAM. Errors invalidate dimensions;
 * RGB565 output has DC_VI_TEXTURE_WIDTH stride and zeroed unused padding. */
dc_vi_result dc_vi_convert(const dc_vi_state *state, const uint8_t *memory,
                          size_t memory_size, dc_vi_frame *frame);
#endif
