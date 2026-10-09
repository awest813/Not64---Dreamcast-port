#include "dc_video.h"
#include <string.h>
#include "../main/wii64config.h"
#define XXH_INLINE_ALL
#include "../main/xxhash.h"

uint32_t dc_video_frame_hash(const dc_vi_frame *frame)
{
    return XXH32(frame->pixels,
                 dc_video_pvr_upload_bytes(frame->width, frame->height), 0);
}

void dc_video_viewport(unsigned width, unsigned height, int aspect,
                       int *left, int *top, int *right, int *bottom)
{
    int w=(int)width*(width<=320?2:1), h=(int)height*(height<=240?2:1);
    if (aspect==SCREENMODE_16x9) { w=640; h=480; }
    else if (aspect==SCREENMODE_16x9_PILLARBOX) w=w*3/4;
    *left=(640-w)/2; *top=(480-h)/2;
    *right=*left+w; *bottom=*top+h;
}

int dc_video_expand_2x(const dc_vi_frame *frame, uint16_t *out, size_t count)
{
    unsigned x, y, left, top;
    if (!frame || !out || count < 640u * 480u ||
        frame->width > DC_VI_MAX_WIDTH || frame->height > DC_VI_MAX_HEIGHT) return 0;
    memset(out, 0, 640u * 480u * sizeof(*out));
    if (frame->width && frame->height && screenMode!=SCREENMODE_4x3) {
        int l,t,r,b;
        dc_video_viewport(frame->width,frame->height,screenMode,&l,&t,&r,&b);
        for (y=(unsigned)t;y<(unsigned)b;y++) {
            unsigned sy=(y-t)*frame->height/(b-t);
            for (x=(unsigned)l;x<(unsigned)r;x++)
                out[y*640+x]=frame->pixels[sy*DC_VI_TEXTURE_WIDTH+(x-l)*frame->width/(r-l)];
        }
        return 1;
    }
    if (frame->width > 320 || frame->height > 240) {
        unsigned sx = frame->width <= 320 ? 2 : 1;
        unsigned sy = frame->height <= 240 ? 2 : 1;
        left = (640 - frame->width * sx) / 2;
        top = (480 - frame->height * sy) / 2;
        for (y = 0; y < frame->height * sy; ++y)
            for (x = 0; x < frame->width * sx; ++x)
                out[(top+y)*640+left+x] = frame->pixels[(y/sy)*DC_VI_TEXTURE_WIDTH+x/sx];
        return 1;
    }
    left = (640u - frame->width * 2u) / 2u;
    top = (480u - frame->height * 2u) / 2u;
    if (((uintptr_t)out & 3u) == 0u) {
        /* Aligned destination: a doubled pixel pair is one 32-bit store, and
         * the second scanline of the pair is a copy of the first. Callers may
         * pass a 2-byte-aligned buffer (tests do), so keep the scalar path. */
        for (y = 0; y < frame->height; ++y) {
            uint32_t *row = (uint32_t *)(out + (top + y * 2u) * 640u + left);
            const uint16_t *src = frame->pixels + y * DC_VI_TEXTURE_WIDTH;
            for (x = 0; x < frame->width; ++x)
                row[x] = (uint32_t)src[x] * 0x10001u;
            memcpy((uint16_t *)row + 640u, row, frame->width * 2u * sizeof(uint16_t));
        }
        return 1;
    }
    for (y = 0; y < frame->height; ++y) for (x = 0; x < frame->width; ++x) {
        size_t i = (top + y * 2u) * 640u + left + x * 2u;
        uint16_t c = frame->pixels[y * DC_VI_TEXTURE_WIDTH + x];
        out[i] = out[i + 1] = out[i + 640] = out[i + 641] = c;
    }
    return 1;
}

size_t dc_video_pvr_upload_bytes(unsigned width, unsigned height)
{
    if (!width || !height || width > DC_VI_MAX_WIDTH || height > DC_VI_MAX_HEIGHT)
        return 0;
    return (size_t)height * DC_VI_TEXTURE_WIDTH * sizeof(uint16_t);
}

#ifdef DC_HOST_STUB
int dc_video_prepare_ui(void) { return 0; }
int dc_video_init(void) { return 1; }
int dc_video_present(const dc_vi_frame *frame) { return frame != NULL; }
void dc_video_shutdown(void) {}
const char *dc_video_name(void) { return "headless"; }
#endif
