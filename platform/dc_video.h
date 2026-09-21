#ifndef DC_VIDEO_H
#define DC_VIDEO_H
#include "dc_vi.h"
int dc_video_init(void);
/* Empty dimensions mean black. Return only when CPU pixels are safe to reuse. */
int dc_video_present(const dc_vi_frame *frame);
void dc_video_shutdown(void);
const char *dc_video_name(void);
/* CPU output kernel shared by software KOS output and host verification. */
int dc_video_expand_2x(const dc_vi_frame *frame, uint16_t *out, size_t count);
/* Linear 512-wide RGB565 PVR upload: used rows only. Zero if blank/invalid. */
size_t dc_video_pvr_upload_bytes(unsigned width, unsigned height);
/* Hash exactly the uploaded rows; callers must compare dimensions as well. */
uint32_t dc_video_frame_hash(const dc_vi_frame *frame);
#ifndef DC_HOST_STUB
int dc_video_claim_display(void);
void dc_video_release_display(void);
#endif
#endif
