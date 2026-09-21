#include "../mupen64_soft_gfx/rsp.h"
#ifdef DC_RASTER_PVR
#include "dc_raster_pvr.h"
#endif
static RDP *renderer;
#ifdef DC_SOFT_PROFILE
extern "C" void dc_soft_profile_reset();
extern "C" void dc_soft_profile_list();
#endif
extern "C" void dc_soft_reset(void) {
    delete renderer; renderer = NULL;
#ifdef DC_SOFT_PROFILE
    dc_soft_profile_reset();
#endif
}
extern "C" int dc_soft_dlist(const GFX_INFO *info) {
    if (!renderer) renderer = new RDP(*info);
    RSP rsp(*info, renderer);
#ifdef DC_RASTER_PVR
    PVRRaster::flush();
#endif
#ifdef DC_SOFT_PROFILE
    dc_soft_profile_list();
#endif
    return rsp.succeeded();
}
