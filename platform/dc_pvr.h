#ifndef DC_PVR_H
#define DC_PVR_H

/*
 * KallistiOS VIDEO=pvr presenter — not an RDP/TA geometry backend.
 *
 * dc_pvr.c implements dc_video_* from dc_video.h: one 512×256 linear RGB565
 * texture and one opaque 2×-centered quad. It scanouts converted VI frames
 * after graphics init. Menu drawing stays on dc_draw_* into vram_s; those
 * calls are not TA lists.
 *
 * GFX=none still has empty processDList/processRDPList. GFX=soft rasterizes
 * in RDRAM and uses this same presenter. Mid-frame colour-buffer read-back
 * (the Flycast software path) is not provided. See PORTING.md Phase 7 and
 * PVR_PLAN.md stages 3+.
 *
 * There is no dc_pvr_available(); link VIDEO=pvr or the software/headless
 * dc_video_* implementation.
 */

#endif
