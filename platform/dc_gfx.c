#include "dc_gfx.h"
#include "dc_memory.h"
#include "dc_video.h"
#include "../main/plugin.h"
#include <stdio.h>
#include <string.h>
#ifdef DC_PERF
#ifdef DC_HOST_STUB
#include <time.h>
static uint64_t perf_now(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000 + t.tv_nsec / 1000;
}
#else
#include <kos.h>
static uint64_t perf_now(void) { return timer_us_gettime64(); }
#endif
static uint64_t perf_begin, perf_raster, perf_convert, perf_present;
static void perf_touch(void) { if (!perf_begin) perf_begin = perf_now(); }
#endif

#ifdef DC_SOFT_GFX
extern int dc_soft_dlist(const GFX_INFO *);
extern void dc_soft_reset(void);
#endif

static GFX_INFO info;
static dc_gfx_stats stats;
static int initialized, opened, video_ready;
static dc_vi_frame frame;
static int last_blank = -1;
static uint32_t frame_limit;
extern int stop;
#ifdef DC_VI_HIGHRES
extern int vi_field;
#endif

void dc_gfx_set_frame_limit(uint32_t limit) { frame_limit = limit; }
const dc_vi_frame *dc_gfx_get_frame(void) { return opened ? &frame : NULL; }
const char *dc_gfx_backend_name(void) { return dc_video_name(); }

static int active(void)
{
    if (opened) return 1;
    ++stats.invalid_calls;
    return 0;
}

dc_gfx_stats dc_gfx_get_stats(void) { return stats; }
int dc_gfx_is_open(void) { return opened; }

void closeDLL_gfx(void)
{
#ifdef DC_SOFT_GFX
    dc_soft_reset();
#endif
    if (video_ready) dc_video_shutdown();
    video_ready = 0;
    frame.width = frame.height = 0;
    opened = initialized = 0;
    memset(&info, 0, sizeof(info));
}

BOOL initiateGFX(GFX_INFO candidate)
{
    closeDLL_gfx();
#ifdef DC_PERF
    perf_begin = perf_raster = perf_convert = perf_present = 0;
#endif
    memset(&stats, 0, sizeof(stats));
    if (!candidate.MemoryBswaped || !candidate.RDRAM || !candidate.DMEM ||
        !candidate.IMEM || !candidate.MI_INTR_REG || !candidate.DPC_START_REG ||
        !candidate.DPC_END_REG || !candidate.DPC_CURRENT_REG ||
        !candidate.DPC_STATUS_REG || !candidate.VI_STATUS_REG ||
        !candidate.VI_ORIGIN_REG || !candidate.VI_WIDTH_REG ||
        !candidate.VI_H_START_REG || !candidate.VI_V_START_REG ||
        !candidate.VI_X_SCALE_REG || !candidate.VI_Y_SCALE_REG ||
        !candidate.CheckInterrupts)
        return FALSE;
    if (!dc_video_init()) return FALSE;
    video_ready = 1;
    info = candidate;
    initialized = 1;
    return TRUE;
}

void romOpen_gfx(void)
{
    if (!initialized) { ++stats.invalid_calls; return; }
    if (!video_ready) {
        if (!dc_video_init()) { ++stats.invalid_calls; return; }
        video_ready = 1;
    }
    memset(&stats, 0, sizeof(stats));
    frame.width = frame.height = 0;
    last_blank = -1;
    opened = 1;
}
void romClosed_gfx(void)
{
#ifdef DC_SOFT_GFX
    dc_soft_reset();
#endif
    if (video_ready) dc_video_shutdown();
    video_ready = opened = 0;
    frame.width = frame.height = 0;
}
void changeWindow(void) {}

#ifdef DC_SOFT_GFX
extern int dc_soft_dlist(const GFX_INFO *);
#endif
void processDList(void)
{
#ifdef DC_SOFT_GFX
    if (active()) {
        ++stats.display_lists;
#ifdef DC_PERF
        perf_touch();
        uint64_t begin = perf_now();
#endif
        if (!dc_soft_dlist(&info)) { ++stats.decode_failures; stop = 1; }
#ifdef DC_PERF
        perf_raster += perf_now() - begin;
#endif
    }
#else
    if (active() && ++stats.display_lists == 1) {
        unsigned long *task = (unsigned long *)(info.DMEM + 0xfc0);
        fprintf(stderr, "DC graphics: RSP display lists unsupported (VI-only backend)\n");
        fprintf(stderr, "RSP task: ucode=%08lx data=%08lx size=%lu list=%08lx bytes=%lu\n",
                task[4], task[6], task[7], task[12], task[13]);
        for (unsigned i = 0; i < task[7] && i < 2048; ++i) {
            uint8_t b;
            if (dc_gfx_read_u8(task[6] + i, &b) && b == 'R') {
                char text[96] = {0};
                for (unsigned j = 0; j < sizeof(text)-1; ++j) {
                    if (!dc_gfx_read_u8(task[6]+i+j, &b) || b < 32 || b > 126) break;
                    text[j] = b;
                }
                if (!strncmp(text, "RSP", 3)) fprintf(stderr, "RSP microcode: %s\n", text);
            }
        }
    }
#endif
}
void processRDPList(void)
{
    if (active() && ++stats.rdp_lists == 1)
        fprintf(stderr, "DC graphics: raw RDP lists unsupported (VI-only backend)\n");
    /* Preserve current core-owned DP interrupt behavior until the decoder lands. */
}
void updateScreen(void)
{
    dc_vi_state state;
    dc_vi_result result;
    int blank;
    if (!active()) return;
    ++stats.vi_updates;
    state = (dc_vi_state) {
        *info.VI_STATUS_REG, *info.VI_ORIGIN_REG, *info.VI_WIDTH_REG,
        *info.VI_H_START_REG, *info.VI_V_START_REG,
        *info.VI_X_SCALE_REG, *info.VI_Y_SCALE_REG
    };
#ifdef DC_VI_HIGHRES
    state.field = vi_field & 1;
#endif
#ifdef DC_PERF
    perf_touch();
    uint64_t begin = perf_now();
#endif
    result = dc_vi_convert(&state, info.RDRAM, DC_N64_RDRAM_SIZE, &frame);
#ifdef DC_PERF
    perf_convert += perf_now() - begin;
#endif
    blank = result != DC_VI_READY;
    if (result == DC_VI_READY) ++stats.converted;
    else if (result == DC_VI_BLANK) ++stats.blanked;
    else if (result == DC_VI_INVALID) {
        if (++stats.invalid_vi == 1)
            fprintf(stderr, "DC VI: invalid framebuffer range/stride status=%08lx origin=%08lx stride=%lu h=%08lx v=%08lx x=%08lx y=%08lx\n",
                    (unsigned long)state.status, (unsigned long)state.origin,
                    (unsigned long)state.stride, (unsigned long)state.h_start,
                    (unsigned long)state.v_start, (unsigned long)state.x_scale,
                    (unsigned long)state.y_scale);
    } else {
        if (++stats.unsupported_vi == 1)
            fprintf(stderr, "DC VI: unsupported scanout mode status=%08lx origin=%08lx stride=%lu h=%08lx v=%08lx x=%08lx y=%08lx\n",
                    (unsigned long)state.status, (unsigned long)state.origin,
                    (unsigned long)state.stride, (unsigned long)state.h_start,
                    (unsigned long)state.v_start, (unsigned long)state.x_scale,
                    (unsigned long)state.y_scale);
    }
    /* Blank once on transition. Never leave the previous game image visible
     * after blanking, invalid bounds, or an unsupported mode. */
    if (!blank || last_blank != blank) {
#ifdef DC_PERF
        begin = perf_now();
#endif
        if (!dc_video_present(&frame)) { ++stats.present_failures; stop = 1; return; }
#ifdef DC_PERF
        perf_present += perf_now() - begin;
#endif
        if (!blank) ++stats.presented;
#ifdef DC_SOFT_GFX
        if (!blank && stats.presented % 60 == 0)
            printf("Graphics progress: frames=%lu lists=%lu\n", (unsigned long)stats.presented, (unsigned long)stats.display_lists);
#endif
    }
    last_blank = blank;
#ifdef DC_PERF
    if (!blank && stats.presented && stats.presented % 60 == 0) {
        uint64_t total = perf_now() - perf_begin;
        printf("Game perf: frames=%lu total-us=%llu raster-us=%llu convert-us=%llu present-us=%llu\n",
               (unsigned long)stats.presented, (unsigned long long)total,
               (unsigned long long)perf_raster, (unsigned long long)perf_convert,
               (unsigned long long)perf_present);
    }
#endif
    if (frame_limit && stats.presented >= frame_limit) stop = 1;
}
void viStatusChanged(void) { if (active()) ++stats.status_changes; }
void viWidthChanged(void) { if (active()) ++stats.width_changes; }
void showCFB(void) { if (active()) ++stats.cfb_requests; }
void readScreen(void **dest, long *width, long *height)
{
    if (dest) *dest = NULL;
    if (width) *width = 0;
    if (height) *height = 0;
}

static void fb_read(DWORD addr) { (void)addr; if (active()) ++stats.fb_reads; }
static void fb_write(DWORD addr, DWORD size)
{
    (void)addr; (void)size;
    if (active()) ++stats.fb_writes;
}
static void fb_info(void *dest)
{
    /* Plugin ABI requires six entries; report no tracked render targets. */
    if (dest) memset(dest, 0, 6 * sizeof(FrameBufferInfo));
}
void (*fBRead)(DWORD) = fb_read;
void (*fBWrite)(DWORD, DWORD) = fb_write;
void (*fBGetFrameBufferInfo)(void *) = fb_info;

int dc_gfx_read_u8(uint32_t offset, uint8_t *value)
{
    if (!opened || !value || offset >= DC_N64_RDRAM_SIZE) return 0;
    *value = info.RDRAM[offset ^ 3u];
    return 1;
}
int dc_gfx_read_u16(uint32_t offset, uint16_t *value)
{
    if (!opened || !value || (offset & 1u) || offset > DC_N64_RDRAM_SIZE - 2u)
        return 0;
    memcpy(value, info.RDRAM + (offset ^ 2u), sizeof(*value));
    return 1;
}
int dc_gfx_read_u32(uint32_t offset, uint32_t *value)
{
    if (!opened || !value || (offset & 3u) || offset > DC_N64_RDRAM_SIZE - 4u)
        return 0;
    memcpy(value, info.RDRAM + offset, sizeof(*value));
    return 1;
}

int dc_gfx_capture_ppm(const char *path)
{
    FILE *file;
    unsigned x, y;
    int ok;
    if (!opened || !frame.width || !frame.height || !path) return 0;
    file = fopen(path, "wb");
    if (!file) return 0;
    ok = fprintf(file, "P6\n%u %u\n255\n", frame.width, frame.height) > 0;
    for (y = 0; ok && y < frame.height; ++y) {
        unsigned char row[DC_VI_MAX_WIDTH * 3];
        for (x = 0; x < frame.width; ++x) {
            uint16_t c = frame.pixels[y * DC_VI_TEXTURE_WIDTH + x];
            unsigned r = c >> 11, g = (c >> 5) & 63u, b = c & 31u;
            row[x * 3] = (r << 3) | (r >> 2);
            row[x * 3 + 1] = (g << 2) | (g >> 4);
            row[x * 3 + 2] = (b << 3) | (b >> 2);
        }
        ok = fwrite(row, 3, frame.width, file) == frame.width;
    }
    if (fclose(file)) ok = 0;
    return ok;
}
