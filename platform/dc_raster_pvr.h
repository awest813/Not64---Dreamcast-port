#ifndef DC_RASTER_PVR_H
#define DC_RASTER_PVR_H
#include "../mupen64_soft_gfx/vektor.h"
#include "../mupen64_soft_gfx/color.h"
class RDP;
// Experimental hybrid geometry backend. Every rejected operation is flushed
// back into RDRAM before the software renderer handles it.
class PVRRaster {
    static bool eligible(RDP *rdp, bool &blend, bool alphaTestBaked=false);
    static int texture(RDP *rdp, int tile, Color32 shade, int alphaThreshold=-1);
    static bool begin(RDP *rdp, bool preserve=true);
public:
    static bool triangle(RDP *rdp, Vektor<float,4>& v0, Vektor<float,4>& v1,
        Vektor<float,4>& v2, Color32& c0, Color32& c1, Color32& c2,
        float s0,float t0,float s1,float t1,float s2,float t2,int tile,
        float w0,float w1,float w2,bool pointAlpha=false);
    static bool fill(RDP *rdp,float ux,float uy,float lx,float ly);
    static bool rectangle(RDP *rdp,int tile,float ux,float uy,float lx,float ly,
        float s,float t,float ds,float dt);
    static void flush();
    static void reset();
    static void readMemory(const void *source,unsigned bytes);
};
#endif
