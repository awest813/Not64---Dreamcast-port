#include "../mupen64_soft_gfx/rsp.h"
#ifdef DC_RASTER_PVR
#include "dc_raster_pvr.h"
#endif
static RDP *renderer;
extern "C" void dc_soft_reset(void) { delete renderer; renderer = NULL; }
extern "C" int dc_soft_dlist(const GFX_INFO *info) {
    if (!renderer) renderer = new RDP(*info);
    RSP rsp(*info, renderer);
#ifdef DC_RASTER_PVR
    PVRRaster::flush();
#endif
    return rsp.succeeded();
}
