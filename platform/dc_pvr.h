#ifndef DC_PVR_H
#define DC_PVR_H

/*
 * There is no PowerVR backend in this tree. Flycast/Reicast (the production
 * DC emulators) still keep a software path for framebuffer read-back; N64
 * display lists are immediate-mode and order-dependent, while PVR2 is
 * tile-based and deferred. See PORTING.md Phase 7.
 *
 * dc_pvr_available() is 0 until that work exists. Menu drawing goes through
 * dc_draw_* into an RGB565 buffer, not TA/PVR lists.
 */
int dc_pvr_available(void);

#endif
