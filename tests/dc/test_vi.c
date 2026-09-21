#include "../../platform/dc_vi.h"
#include "../../platform/dc_video.h"
#include <stdio.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "VI FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)
static uint8_t memory[256];
static dc_vi_frame frame;
static uint16_t output[640 * 480 + 2];
static uint16_t output_aligned[640 * 480] __attribute__((aligned(32)));
static void pixel16(unsigned addr, uint16_t value) { memcpy(memory + (addr ^ 2u), &value, 2); }
static void pixel32(unsigned addr, uint32_t value) { memcpy(memory + addr, &value, 4); }

int main(void)
{
    static const uint16_t rgba16[8] = {0xf801, 0x07c1, 0x003f, 0xffff, 0xffc1, 0x07ff, 0xf83f, 0x0001};
    static const uint16_t rgb565[8] = {0xf800, 0x07e0, 0x001f, 0xffff, 0xffe0, 0x07ff, 0xf81f, 0};
    dc_vi_state state = {2, 16, 6, (10u << 16) | 18u, (20u << 16) | 24u, 512, 1024}, bad;
    unsigned x, y;
    memset(memory, 0xa5, sizeof(memory));
    for (y = 0; y < 2; ++y) for (x = 0; x < 4; ++x)
        pixel16(16 + (y * 6 + x) * 2, rgba16[y * 4 + x]);
    CHECK(dc_vi_convert(&state, memory, sizeof(memory), &frame) == DC_VI_READY);
    CHECK(frame.width == 4 && frame.height == 2);
    for (y = 0; y < 2; ++y) for (x = 0; x < 4; ++x)
        CHECK(frame.pixels[y * DC_VI_TEXTURE_WIDTH + x] == rgb565[y * 4 + x]);
    CHECK(frame.pixels[4] == 0 && frame.pixels[DC_VI_TEXTURE_WIDTH * 2] == 0);
    CHECK(frame.pixels[DC_VI_TEXTURE_WIDTH * DC_VI_TEXTURE_HEIGHT - 1] == 0);
    {
        uint32_t hash = dc_video_frame_hash(&frame);
        CHECK(hash == dc_video_frame_hash(&frame));
        frame.pixels[DC_VI_TEXTURE_WIDTH * 2 - 1] ^= 1;
        CHECK(hash != dc_video_frame_hash(&frame));
        frame.pixels[DC_VI_TEXTURE_WIDTH * 2 - 1] ^= 1;
        frame.pixels[DC_VI_TEXTURE_WIDTH * 2] ^= 1;
        CHECK(hash == dc_video_frame_hash(&frame));
        frame.pixels[DC_VI_TEXTURE_WIDTH * 2] ^= 1;
    }

    /* Verify the actual software presentation kernel and its black borders. */
    output[0] = 0x1234; output[640 * 480 + 1] = 0xabcd;
    CHECK(dc_video_expand_2x(&frame, output + 1, 640 * 480));
    CHECK(output[0] == 0x1234 && output[640 * 480 + 1] == 0xabcd);
    CHECK(output[1] == 0);
    for (y = 0; y < 4; ++y) for (x = 0; x < 8; ++x)
        CHECK(output[1 + (238 + y) * 640 + 316 + x] == rgb565[(y / 2) * 4 + x / 2]);
    CHECK(!dc_video_expand_2x(&frame, output, 640 * 480 - 1));

    /* Same frame through the 32-bit-aligned fast path. */
    CHECK(dc_video_expand_2x(&frame, output_aligned, 640 * 480));
    for (y = 0; y < 4; ++y) for (x = 0; x < 8; ++x)
        CHECK(output_aligned[(238 + y) * 640 + 316 + x] == rgb565[(y / 2) * 4 + x / 2]);
    CHECK(output_aligned[0] == 0 && output_aligned[640 * 480 - 1] == 0);

    /* Odd width on 4-aligned rows: pair loop plus the high-half tail read. */
    bad = state; bad.h_start = (10u << 16) | 16u;
    CHECK(dc_vi_convert(&bad, memory, sizeof(memory), &frame) == DC_VI_READY);
    CHECK(frame.width == 3 && frame.height == 2);
    for (y = 0; y < 2; ++y) {
        for (x = 0; x < 3; ++x)
            CHECK(frame.pixels[y * DC_VI_TEXTURE_WIDTH + x] == rgb565[y * 4 + x]);
        CHECK(frame.pixels[y * DC_VI_TEXTURE_WIDTH + 3] == 0);
    }

    /* Integer source crop uses both offsets and the source row stride. */
    bad = state; bad.x_scale |= 1024u << 16; bad.y_scale |= 1024u << 16;
    pixel16(16 + (1 * 6 + 1) * 2, 0xf801);
    pixel16(16 + (2 * 6 + 4) * 2, 0x003f);
    CHECK(dc_vi_convert(&bad, memory, sizeof(memory), &frame) == DC_VI_READY);
    CHECK(frame.pixels[0] == 0xf800 && frame.pixels[512 + 3] == 0x001f);

    /* RGBA8888 has a different byte layout; alpha is not part of scanout. */
    bad = state; bad.status = 3; bad.stride = 4;
    pixel32(16, 0xff000000); pixel32(20, 0x00ff00ff);
    pixel32(24, 0x0000ffff); pixel32(28, 0xffffffff);
    CHECK(dc_vi_convert(&bad, memory, sizeof(memory), &frame) == DC_VI_READY);
    for (x = 0; x < 4; ++x) CHECK(frame.pixels[x] == rgb565[x]);

    /* Last legal halfword and word; an extra row must fail before any read. */
    bad = (dc_vi_state){2, sizeof(memory) - 2, 1, 2, 2, 512, 1024};
    pixel16(sizeof(memory) - 2, 0xf83f);
    CHECK(dc_vi_convert(&bad, memory, sizeof(memory), &frame) == DC_VI_READY);
    CHECK(frame.pixels[0] == 0xf81f);
    bad.v_start = 4;
    CHECK(dc_vi_convert(&bad, memory, sizeof(memory), &frame) == DC_VI_INVALID);
    CHECK(!frame.width && !frame.height);
    CHECK(dc_video_expand_2x(&frame, output, 640 * 480));
    for (x = 0; x < 640 * 480; ++x) CHECK(output[x] == 0);
    CHECK(dc_video_pvr_upload_bytes(0, 240) == 0);
    CHECK(dc_video_pvr_upload_bytes(320, 0) == 0);
    CHECK(dc_video_pvr_upload_bytes(321, 240) == 0);
    CHECK(dc_video_pvr_upload_bytes(320, 241) == 0);
    CHECK(dc_video_pvr_upload_bytes(320, 240) == 240u * DC_VI_TEXTURE_WIDTH * 2u);
    CHECK(dc_video_pvr_upload_bytes(4, 2) == 2u * DC_VI_TEXTURE_WIDTH * 2u);
    CHECK(dc_video_pvr_upload_bytes(320, 240) < DC_VI_TEXTURE_BYTES);
    bad.v_start = 2; bad.status = 3; bad.origin = sizeof(memory) - 4;
    pixel32(sizeof(memory) - 4, 0x12345678);
    CHECK(dc_vi_convert(&bad, memory, sizeof(memory), &frame) == DC_VI_READY);
    CHECK(frame.pixels[0] == 0x11aa);

#define RESULT(field, value, expected) do { bad = state; bad.field = (value); \
    CHECK(dc_vi_convert(&bad, memory, sizeof(memory), &frame) == (expected)); \
    CHECK(!frame.width && !frame.height); } while (0)
    RESULT(status, 0, DC_VI_BLANK);
    RESULT(status, 1, DC_VI_UNSUPPORTED);
    RESULT(status, 2 | 0x40, DC_VI_UNSUPPORTED);
    RESULT(stride, 0, DC_VI_BLANK);
    RESULT(stride, 3, DC_VI_INVALID);
    RESULT(x_scale, 0, DC_VI_BLANK);
    RESULT(y_scale, 0, DC_VI_BLANK);
    RESULT(h_start, (20u << 16) | 10, DC_VI_BLANK);
    RESULT(v_start, 0, DC_VI_BLANK);
    RESULT(x_scale, 512 | (1u << 16), DC_VI_UNSUPPORTED);
    RESULT(y_scale, 1024 | (1u << 16), DC_VI_UNSUPPORTED);
    RESULT(h_start, 1023, DC_VI_UNSUPPORTED);
    RESULT(h_start, 800, DC_VI_UNSUPPORTED);
    RESULT(origin, 17, DC_VI_INVALID);
    RESULT(origin, 0xfffffc, DC_VI_INVALID);
    CHECK(dc_vi_convert(&state, NULL, sizeof(memory), &frame) == DC_VI_INVALID);
    CHECK(dc_vi_convert(NULL, memory, sizeof(memory), &frame) == DC_VI_INVALID);
    CHECK(dc_vi_convert(&state, memory, 0, &frame) == DC_VI_INVALID);
    CHECK(dc_vi_convert(&state, memory, sizeof(memory) - 1, &frame) == DC_VI_INVALID);
    CHECK(dc_vi_convert(&state, memory, sizeof(memory), NULL) == DC_VI_INVALID);
    puts("VI conversion + software presentation PASS");
    return 0;
}
