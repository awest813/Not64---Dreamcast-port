#include "dc_vi.h"
#include <string.h>

dc_vi_result dc_vi_convert(const dc_vi_state *s, const uint8_t *mem,
                          size_t size, dc_vi_frame *frame)
{
    uint32_t type, stride, origin, xs, ys, xo, yo, hx0, hx1, vy0, vy1;
    uint32_t width, height, bytes, x, y;
    uint64_t end;
    if (!frame) return DC_VI_INVALID;
    frame->width = frame->height = 0;
    if (!s || !mem || (size & 3u)) return DC_VI_INVALID;
    type = s->status & 3u;
    if (!type) return DC_VI_BLANK;
    if (type == 1 || (s->status & 0x40u)) return DC_VI_UNSUPPORTED;
    stride = s->stride & 0xfffu;
    hx0 = (s->h_start >> 16) & 0x3ffu;
    hx1 = s->h_start & 0x3ffu;
    vy0 = (s->v_start >> 16) & 0x3ffu;
    vy1 = s->v_start & 0x3ffu;
    xs = s->x_scale & 0xfffu;
    ys = s->y_scale & 0xfffu;
    if (!stride || !xs || !ys || hx1 <= hx0 || vy1 <= vy0) return DC_VI_BLANK;
    /* Fractional crop/extent needs a resampler. Support integer crops only. */
    xo = (s->x_scale >> 16) & 0xfffu;
    yo = (s->y_scale >> 16) & 0xfffu;
    if ((xo & 1023u) || (yo & 1023u) ||
        (((hx1 - hx0) * xs) & 1023u) || (((vy1 - vy0) * ys) & 2047u))
        return DC_VI_UNSUPPORTED;
    xo >>= 10; yo >>= 10;
    width = ((hx1 - hx0) * xs) >> 10;
    height = ((vy1 - vy0) * ys) >> 11;
    if (!width || !height) return DC_VI_BLANK;
    if (width > DC_VI_MAX_WIDTH || height > DC_VI_MAX_HEIGHT) return DC_VI_UNSUPPORTED;
    if (xo + width > stride) return DC_VI_INVALID;
    origin = s->origin & 0xffffffu;
    bytes = type == 2 ? 2 : 4;
    if (origin & (bytes - 1u)) return DC_VI_INVALID;
    end = (uint64_t)origin + ((uint64_t)(yo + height - 1) * stride + xo + width) * bytes;
    if (end > size) return DC_VI_INVALID;

    memset(frame->pixels, 0, sizeof(frame->pixels));
    for (y = 0; y < height; ++y) {
        uint32_t row = origin + ((yo + y) * stride + xo) * bytes;
        for (x = 0; x < width; ++x) {
            uint16_t color;
            if (type == 2) {
                uint16_t n64;
                unsigned green;
                memcpy(&n64, mem + ((row + x * 2) ^ 2u), 2);
                green = (n64 >> 6) & 31u;
                color = (n64 & 0xf800u) | ((green * 2u + (green >> 4)) << 5) |
                        ((n64 >> 1) & 31u);
            } else {
                uint32_t n64;
                memcpy(&n64, mem + row + x * 4, 4);
                color = ((n64 >> 16) & 0xf800u) | ((n64 >> 13) & 0x07e0u) |
                        ((n64 >> 11) & 0x001fu);
            }
            frame->pixels[y * DC_VI_TEXTURE_WIDTH + x] = color;
        }
    }
    frame->width = width; frame->height = height;
    return DC_VI_READY;
}
