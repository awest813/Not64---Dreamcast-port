#include "dc_video.h"
#include <kos.h>
#include <string.h>

static int claimed;
static vid_mode_t previous_mode;
static const char *previous_debug_device;

int dc_video_claim_display(void)
{
    if (claimed || !vid_mode) return 0;
    previous_mode = *vid_mode;
    previous_debug_device = dbgio_dev_get();
    /* Serial/dcload logging may continue; framebuffer text may not. */
    if (previous_debug_device && !strcmp(previous_debug_device, "fb") &&
        dbgio_dev_select("scif") < 0) return 0;
    vid_set_mode(DM_640x480, PM_RGB565);
    claimed = 1;
    return 1;
}
void dc_video_release_display(void)
{
    if (!claimed) return;
    vid_set_mode_ex(&previous_mode);
    if (previous_debug_device) dbgio_dev_select(previous_debug_device);
    claimed = 0;
}

#ifndef DC_VIDEO_PVR
int dc_video_init(void)
{
    vid_mode_t mode;
    if (!dc_video_claim_display()) return 0;
    mode = *vid_mode;
    mode.fb_count = 2;
    mode.fb_curr = 0;
    vid_set_mode_ex(&mode);
    vid_clear(0, 0, 0);
    vid_flip(0); /* vram_s now addresses the back buffer. */
    vid_clear(0, 0, 0);
    return 1;
}
int dc_video_present(const dc_vi_frame *frame)
{
    if (!claimed || !dc_video_expand_2x(frame, vram_s, 640u * 480u)) return 0;
    vid_waitvbl();
    vid_flip(-1);
    return 1;
}
void dc_video_shutdown(void) { dc_video_release_display(); }
const char *dc_video_name(void) { return "software"; }
#endif
