#ifndef DC_RASTER_PIXELS_H
#define DC_RASTER_PIXELS_H
#include <cstdint>
#include "../mupen64_soft_gfx/global.h"

// Import the even-width, word-swapped RGBA16 framebuffer into RGB565 staging.
// Preserve padding; a halfword-aligned framebuffer retains the scalar path.
static inline void dc_raster_import_rows(const uint16_t *source, uint16_t *dest,
                                        unsigned width, unsigned height, unsigned pitch) {
    if (S16 == 1 && !((uintptr_t(source) | uintptr_t(dest)) & 3)) {
        auto src = reinterpret_cast<const uint32_t *>(source);
        auto dst = reinterpret_cast<uint32_t *>(dest);
        for (unsigned y=0; y<height; ++y) for (unsigned x=0; x<width/2; ++x) {
            uint32_t v=src[y*width/2+x];
            uint32_t rgb=(v&0xffc0ffc0u)|((v>>1)&0x001f001fu)|((v>>5)&0x00200020u);
            dst[y*pitch/2+x]=(rgb<<16)|(rgb>>16);
        }
    } else {
        for (unsigned y=0; y<height; ++y) for (unsigned x=0; x<width; ++x) {
            unsigned v=source[(y*width+x)^S16];
            dest[y*pitch+x]=(v&0xffc0)|((v>>1)&31)|((v>>5)&32);
        }
    }
}
// Aligned native-width batches import only the row intervals sampled by their
// backdrop. Source stride remains the native framebuffer width.
static inline void dc_raster_import_region(const uint16_t *source,uint16_t *dest,
    unsigned width,unsigned pitch,unsigned x0,unsigned y0,unsigned x1,unsigned y1) {
    for(unsigned y=y0;y<y1;++y)
        dc_raster_import_rows(source+y*width+x0,dest+y*pitch+x0,x1-x0,1,pitch);
}
#endif
