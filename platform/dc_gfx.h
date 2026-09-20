#ifndef DC_GFX_H
#define DC_GFX_H

#include <stdint.h>
#include "dc_vi.h"

/* CPU framebuffer scanout only; RDP and display-list rendering are pending. */
typedef struct {
    uint32_t display_lists, rdp_lists, vi_updates, status_changes, width_changes;
    uint32_t cfb_requests, fb_reads, fb_writes, invalid_calls;
    uint32_t decode_failures;
    uint32_t converted, presented, blanked, invalid_vi, unsupported_vi, present_failures;
} dc_gfx_stats;

dc_gfx_stats dc_gfx_get_stats(void);
void dc_gfx_set_frame_limit(uint32_t limit);
const dc_vi_frame *dc_gfx_get_frame(void);
const char *dc_gfx_backend_name(void);
int dc_gfx_capture_ppm(const char *path);
int dc_gfx_is_open(void);
/* Read N64-order values from physical RDRAM offsets. No wrapping at 4 MiB. */
int dc_gfx_read_u8(uint32_t offset, uint8_t *value);
int dc_gfx_read_u16(uint32_t offset, uint16_t *value);
int dc_gfx_read_u32(uint32_t offset, uint32_t *value);

#endif
