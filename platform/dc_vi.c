#include "dc_vi.h"
#include <string.h>

/* RGBA5551 (already byte-order corrected) to RGB565. */
static uint16_t vi_rgba16_to_rgb565(uint32_t n64)
{
    uint32_t green = (n64 >> 6) & 31u;
    return (uint16_t)((n64 & 0xf800u) | ((green * 2u + (green >> 4)) << 5) |
                      ((n64 >> 1) & 31u));
}

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

    /* The contract is "512-pixel stride, zeroed unused padding". Zero only the
     * padding this frame's row loop will not overwrite — the row tails and the
     * rows below height — instead of re-clearing all 256 KiB every VI update. */
    memset(frame->pixels + height * DC_VI_TEXTURE_WIDTH, 0,
           (DC_VI_TEXTURE_HEIGHT - height) * DC_VI_TEXTURE_WIDTH * sizeof(uint16_t));
    for (y = 0; y < height; ++y) {
        memset(frame->pixels + y * DC_VI_TEXTURE_WIDTH + width, 0,
               (DC_VI_TEXTURE_WIDTH - width) * sizeof(uint16_t));
    }
    for (y = 0; y < height; ++y) {
        uint32_t row = origin + ((yo + y) * stride + xo) * bytes;
        uint16_t *dst = frame->pixels + y * DC_VI_TEXTURE_WIDTH;
        if (type == 2) {
            if (!(row & 3u) && !((uintptr_t)(mem + row) & 3u)) {
                /* Aligned rows: both pixels of a pair live in one word, even
                 * pixel in the high half, odd in the low (the ^2 quirk). One
                 * aligned load replaces two halfword reads per pair. */
                const uint8_t *src = mem + row;
                unsigned pairs = width >> 1;
                while (pairs--) {
                    uint32_t w, rgb;
                    memcpy(&w, src, 4);
                    src += 4;
                    /* Convert both packed pixels in parallel, then swap
                     * halves from N64 word order to the little-endian scanout. */
                    rgb = (w & 0xffc0ffc0u) | ((w >> 1) & 0x001f001fu) |
                          ((w >> 5) & 0x00200020u);
                    rgb = (rgb << 16) | (rgb >> 16);
                    memcpy(dst, &rgb, 4);
                    dst += 2;
                }
                if (width & 1u) {
                    uint32_t tail;
                    /* Tail pixel is even, so the ^2 quirk reads the high
                     * half of its word (src+2), exactly like the pair loop. */
                    memcpy(&tail, src + 2, 2);
                    dst[0] = vi_rgba16_to_rgb565(tail);
                }
            } else {
                /* Odd row start: pairs straddle words, so keep the per-pixel
                 * halfword reads (they stay two-byte aligned either way). */
                for (x = 0; x < width; ++x) {
                    uint32_t n64;
                    memcpy(&n64, mem + ((row + x * 2) ^ 2u), 2);
                    dst[x] = vi_rgba16_to_rgb565(n64);
                }
            }
        } else {
            const uint8_t *src = mem + row;
            for (x = 0; x < width; ++x) {
                uint32_t n64;
                memcpy(&n64, src, 4);
                src += 4;
                dst[x] = (uint16_t)(((n64 >> 16) & 0xf800u) |
                                    ((n64 >> 13) & 0x07e0u) |
                                    ((n64 >> 11) & 0x001fu));
            }
        }
    }
    frame->width = width; frame->height = height;
    return DC_VI_READY;
}
