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
#ifndef DC_HOST_STUB
int dc_video_claim_display(void);
void dc_video_release_display(void);
#endif
#endif
