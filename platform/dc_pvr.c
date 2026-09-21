#include "dc_video.h"
#include "dc_pvr.h"
#include "dc_debug.h"
#include <kos.h>
#include <stdio.h>

/* Two textures ping-pong and the vertex buffer is double-buffered, so the
 * texture upload for frame N+1 overlaps the render of frame N. Safety: KOS
 * clears ta_busy (what pvr_wait_ready waits on) only when it starts the
 * render of the just-submitted scene, and renders are strictly sequential —
 * so once wait_ready returns, the render that read the texture being
 * overwritten has completed. No render-done wait is needed per frame. */
#define TEXTURE_COUNT 2

static int initialized;
static pvr_ptr_t textures[TEXTURE_COUNT];
static pvr_poly_hdr_t headers[TEXTURE_COUNT];
static unsigned texture_next;
static unsigned texture_current, cached_width, cached_height;
static uint32_t cached_hash;
static int cached_valid;
static int present_failed;
#ifdef DC_EMBED_VITEST
static uint64_t wait_us, hash_us, upload_us, submit_us;
static unsigned samples, uploads;
#endif

/* Drain TA work and renders on shutdown or failed submission only. */
static int pvr_idle(void)
{
    return pvr_wait_ready() >= 0 && pvr_wait_render_done() >= 0;
}

int dc_video_init(void)
{
    pvr_init_params_t params = {
        .opb_sizes = { PVR_BINSIZE_16, 0, 0, 0, 0 },
        .vertex_buf_size = 128 * 1024,
        .opb_overflow_count = 1
        /* vbuf_doublebuf_disabled left 0: with a single vertex buffer KOS
         * waits for render-done inside pvr_scene_begin, which would put the
         * serialization back. */
    };
    pvr_poly_cxt_t context;
    int i;
    if (!dc_video_claim_display()) return 0;
    if (pvr_init(&params) < 0) {
        dc_log(DC_LOG_ERROR, "PVR: pvr_init failed");
        dc_video_release_display();
        return 0;
    }
    initialized = 1;
#ifdef DC_EMBED_VITEST
    wait_us = hash_us = upload_us = submit_us = 0;
    samples = uploads = 0;
#endif
    for (i = 0; i < TEXTURE_COUNT; ++i) {
        textures[i] = pvr_mem_malloc(DC_VI_TEXTURE_BYTES);
        if (!textures[i]) {
            dc_log(DC_LOG_ERROR, "PVR: texture %d alloc %u failed",
                   i, (unsigned)DC_VI_TEXTURE_BYTES);
            dc_video_shutdown();
            return 0;
        }
        pvr_poly_cxt_txr(&context, PVR_LIST_OP_POLY,
                        PVR_TXRFMT_RGB565 | PVR_TXRFMT_NONTWIDDLED,
                        DC_VI_TEXTURE_WIDTH, DC_VI_TEXTURE_HEIGHT, textures[i],
                        PVR_FILTER_NONE);
        context.gen.culling = PVR_CULLING_NONE;
        context.depth.comparison = PVR_DEPTHCMP_ALWAYS;
        context.depth.write = PVR_DEPTHWRITE_DISABLE;
        context.txr.env = PVR_TXRENV_REPLACE;
        pvr_poly_compile(&headers[i], &context);
    }
    texture_next = 0;
    texture_current = 0;
    cached_valid = 0;
    present_failed = 0;
#ifdef DC_VIDEO_UPLOAD_SKIP
    puts("PVR upload-skip: enabled");
#else
    puts("PVR upload-skip: disabled");
#endif
    pvr_set_bg_color(0, 0, 0);
    /* Exact serial line: tests/dc/check_target_log.py */
    printf("PVR: staging=%u texture=%u VRAM-free=%lu bytes\n",
           (unsigned)DC_VI_TEXTURE_BYTES,
           (unsigned)(TEXTURE_COUNT * DC_VI_TEXTURE_BYTES),
           (unsigned long)pvr_mem_available());
    return 1;
}

int dc_video_present(const dc_vi_frame *frame)
{
    int result = 1;
    size_t upload;
    unsigned slot;
    uint32_t hash = 0;
    int changed;
#ifdef DC_EMBED_VITEST
    uint64_t begin = timer_us_gettime64(), ready, hashed, uploaded;
#endif
    if (!initialized || present_failed || !textures[0] || !textures[1] || !frame ||
        frame->width > DC_VI_MAX_WIDTH || frame->height > DC_VI_MAX_HEIGHT)
        return 0;
    if (pvr_wait_ready() < 0) {
        dc_log(DC_LOG_ERROR, "PVR: wait failed before present");
        return 0;
    }
#ifdef DC_EMBED_VITEST
    ready = timer_us_gettime64();
#endif
    upload = dc_video_pvr_upload_bytes(frame->width, frame->height);
#ifdef DC_VIDEO_UPLOAD_SKIP
    if (upload) hash = dc_video_frame_hash(frame);
    changed = upload && (!cached_valid || cached_hash != hash ||
                        cached_width != frame->width || cached_height != frame->height);
#else
    changed = upload != 0;
#endif
#ifdef DC_EMBED_VITEST
    hashed = timer_us_gettime64();
#endif
    slot = changed ? texture_next : texture_current;
    if (changed) {
        /* Blocking PVR DMA hands the copy to the G2 DMA engine instead of
         * the SH4 store queues; fall back to the CPU copy if the channel
         * refuses (misalignment would be a programming error on our side,
         * and the busy case cannot happen: the previous blocking DMA done). */
        if (pvr_txr_load_dma(frame->pixels, textures[slot], upload, true, NULL, NULL) < 0)
            pvr_txr_load(frame->pixels, textures[slot], upload);
        cached_hash = hash;
        cached_width = frame->width;
        cached_height = frame->height;
        cached_valid = 1;
        texture_current = slot;
        texture_next = (slot + 1) % TEXTURE_COUNT;
    }
#ifdef DC_EMBED_VITEST
    uploaded = timer_us_gettime64();
    if (changed) {
        upload_us += uploaded - hashed;
        ++uploads;
    }
#endif
    pvr_scene_begin();
    if (pvr_list_begin(PVR_LIST_OP_POLY) < 0) {
        pvr_scene_finish();
        /* If draining fails, reject further presents until reopen: either
         * texture may still be owned by a render after this failed scene. */
        present_failed = !pvr_idle();
        cached_valid = 0;
        return 0;
    }
    if (upload) {
        float left = (640.0f - frame->width * 2.0f) / 2.0f;
        float top = (480.0f - frame->height * 2.0f) / 2.0f;
        float right = left + frame->width * 2.0f;
        float bottom = top + frame->height * 2.0f;
        float u = (float)frame->width / DC_VI_TEXTURE_WIDTH;
        float v = (float)frame->height / DC_VI_TEXTURE_HEIGHT;
        pvr_vertex_t vertices[4] __attribute__((aligned(32))) = {
            { .flags=PVR_CMD_VERTEX, .x=left, .y=top, .z=1.0f, .u=0, .v=0, .argb=0xffffffff },
            { .flags=PVR_CMD_VERTEX, .x=right, .y=top, .z=1.0f, .u=u, .v=0, .argb=0xffffffff },
            { .flags=PVR_CMD_VERTEX, .x=left, .y=bottom, .z=1.0f, .u=0, .v=v, .argb=0xffffffff },
            { .flags=PVR_CMD_VERTEX_EOL, .x=right, .y=bottom, .z=1.0f, .u=u, .v=v, .argb=0xffffffff }
        };
        if (pvr_prim(&headers[slot], sizeof(headers[slot])) < 0 ||
            pvr_prim(vertices, sizeof(vertices)) < 0) result = 0;
    }
    if (pvr_list_finish() < 0) result = 0;
    if (pvr_scene_finish() < 0) result = 0;
    if (!result) {
        present_failed = !pvr_idle();
        cached_valid = 0;
    }
#ifdef DC_EMBED_VITEST
    wait_us += ready - begin;
    hash_us += hashed - ready;
    submit_us += timer_us_gettime64() - uploaded;
    ++samples;
#endif
    return result;
}

void dc_video_shutdown(void)
{
    int i;
    if (initialized) {
#ifdef DC_EMBED_VITEST
        if (samples)
            printf("PVR timing: samples=%u uploads=%u wait-avg-us=%lu upload-avg-us=%lu submit-avg-us=%lu hash-avg-us=%lu\n",
                   samples, uploads, (unsigned long)(wait_us / samples),
                   (unsigned long)(uploads ? upload_us / uploads : 0),
                   (unsigned long)(submit_us / samples),
                   (unsigned long)(hash_us / samples));
#endif
        if (!pvr_idle())
            dc_log(DC_LOG_ERROR, "PVR: wait failed; freeing textures before shutdown");
        /* pvr_shutdown tears down the allocator; free user VRAM first even if
         * the wait failed — GPU work is abandoned on shutdown. */
        for (i = 0; i < TEXTURE_COUNT; ++i) {
            if (textures[i]) pvr_mem_free(textures[i]);
            textures[i] = NULL;
        }
        pvr_shutdown();
    }
    initialized = 0;
    texture_next = 0;
    dc_video_release_display();
}
const char *dc_video_name(void) { return "pvr"; }
