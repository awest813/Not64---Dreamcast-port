#include "dc_video.h"
#include <kos.h>
#include <stdio.h>

static int initialized;
static pvr_ptr_t texture;
static pvr_poly_hdr_t header;
#ifdef DC_EMBED_VITEST
static uint64_t wait_us, upload_us, submit_us;
static unsigned samples, uploads;
#endif

int dc_video_init(void)
{
    pvr_init_params_t params = {
        .opb_sizes = { PVR_BINSIZE_16, 0, 0, 0, 0 },
        .vertex_buf_size = 128 * 1024,
        .opb_overflow_count = 1,
        .vbuf_doublebuf_disabled = 1
    };
    pvr_poly_cxt_t context;
    if (!dc_video_claim_display()) return 0;
    if (pvr_init(&params) < 0) {
        dc_video_release_display();
        return 0;
    }
    initialized = 1;
#ifdef DC_EMBED_VITEST
    wait_us = upload_us = submit_us = 0;
    samples = uploads = 0;
#endif
    texture = pvr_mem_malloc(DC_VI_TEXTURE_BYTES);
    if (!texture) { dc_video_shutdown(); return 0; }
    pvr_poly_cxt_txr(&context, PVR_LIST_OP_POLY,
                    PVR_TXRFMT_RGB565 | PVR_TXRFMT_NONTWIDDLED,
                    DC_VI_TEXTURE_WIDTH, DC_VI_TEXTURE_HEIGHT, texture, PVR_FILTER_NONE);
    context.gen.culling = PVR_CULLING_NONE;
    context.depth.comparison = PVR_DEPTHCMP_ALWAYS;
    context.depth.write = PVR_DEPTHWRITE_DISABLE;
    context.txr.env = PVR_TXRENV_REPLACE;
    pvr_poly_compile(&header, &context);
    pvr_set_bg_color(0, 0, 0);
    printf("PVR: staging=%u texture=%u VRAM-free=%lu bytes\n",
           (unsigned)DC_VI_TEXTURE_BYTES, (unsigned)DC_VI_TEXTURE_BYTES,
           (unsigned long)pvr_mem_available());
    return 1;
}

int dc_video_present(const dc_vi_frame *frame)
{
    int result = 1;
#ifdef DC_EMBED_VITEST
    uint64_t begin = timer_us_gettime64(), ready, uploaded;
#endif
    if (!initialized || !frame || frame->width > DC_VI_MAX_WIDTH ||
        frame->height > DC_VI_MAX_HEIGHT) return 0;
    /* Readiness for another scene does not release textures from the previous
     * scene. Wait for rendering before overwriting this single texture. */
    /* First drain queued TA work, which can still start a render; then wait
     * for that render. Reversing these waits can overwrite a queued texture. */
    if (pvr_wait_ready() < 0 || pvr_wait_render_done() < 0) return 0;
#ifdef DC_EMBED_VITEST
    ready = timer_us_gettime64();
#endif
    if (frame->width && frame->height)
        pvr_txr_load(frame->pixels, texture, DC_VI_TEXTURE_BYTES);
#ifdef DC_EMBED_VITEST
    uploaded = timer_us_gettime64();
    if (frame->width && frame->height) {
        upload_us += uploaded - ready;
        ++uploads;
    }
#endif
    pvr_scene_begin();
    if (pvr_list_begin(PVR_LIST_OP_POLY) < 0) {
        pvr_scene_finish();
        return 0;
    }
    if (frame->width && frame->height) {
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
        if (pvr_prim(&header, sizeof(header)) < 0 ||
            pvr_prim(vertices, sizeof(vertices)) < 0) result = 0;
    }
    if (pvr_list_finish() < 0) result = 0;
    if (pvr_scene_finish() < 0) result = 0;
#ifdef DC_EMBED_VITEST
    wait_us += ready - begin;
    submit_us += timer_us_gettime64() - uploaded;
    ++samples;
#endif
    return result;
}

void dc_video_shutdown(void)
{
    if (initialized) {
#ifdef DC_EMBED_VITEST
        if (samples)
            printf("PVR timing: samples=%u uploads=%u wait-avg-us=%lu upload-avg-us=%lu submit-avg-us=%lu\n",
                   samples, uploads, (unsigned long)(wait_us / samples),
                   (unsigned long)(uploads ? upload_us / uploads : 0),
                   (unsigned long)(submit_us / samples));
#endif
        if (pvr_wait_ready() == 0 && pvr_wait_render_done() == 0 && texture)
            pvr_mem_free(texture);
        /* Shutdown disables rendering even if the completion wait failed. */
        pvr_shutdown();
    }
    initialized = 0;
    texture = NULL;
    dc_video_release_display();
}
const char *dc_video_name(void) { return "pvr"; }
