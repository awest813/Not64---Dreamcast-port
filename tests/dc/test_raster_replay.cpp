// Synthetic RDP command replays shared by the ILP32 reference and real KOS/PVR.
// No ROM/checkpoint data. Compare emulated framebuffer words, before VI scaling.
#include <cstdio>
#include <cstring>
#include "../../mupen64_soft_gfx/rdp.h"
#ifdef DC_RASTER_PVR
#include <kos.h>
#include "../../platform/dc_raster_pvr.h"
KOS_INIT_FLAGS(INIT_DEFAULT);
#endif

extern "C" { int stop=0; }
static unsigned char ram[4*1024*1024];
static unsigned failures;
static const unsigned target=0x10000;
static void put16(unsigned address,unsigned value) {
    unsigned short v=value;
    std::memcpy(ram+(address^2),&v,2);
}
static unsigned get16(unsigned address) {
    unsigned short v;
    std::memcpy(&v,ram+(address^2),2);
    return v;
}
static void flush() {
#ifdef DC_RASTER_PVR
    PVRRaster::flush();
#endif
}
static void require_gpu(const char *fixture,unsigned minimum=1) {
#ifdef DC_RASTER_PVR
    unsigned scenes=PVRRaster::completedScenes();
    std::printf("REPLAY %s completed-scenes=%u\n",fixture,scenes);
    if(scenes<minimum)++failures;
#else
    (void)fixture;
    (void)minimum;
#endif
}
static void prim(RDP &r) {
    r.setOtherMode_h(20,0);
    r.setOtherMode_l(3,0);
    r.setCombineMode((15u<<5)|31u,
        (15u<<24)|(7u<<21)|(7u<<18)|(3u<<6)|(7u<<3)|3u);
    r.setPrimColor(0xff0000ffu,0,0);
}
static unsigned initial(unsigned x,unsigned y) {
    return (((x*3+y)&31)<<11)|(((x+y*5)&31)<<6)|(((x*7+y)&31)<<1)|((x+y)&1);
}
static void partial_fill(GFX_INFO info) {
    RDP r(info);
    r.setCImg(0,2,320,ram+target);
    r.setScissor(0,0,320,240,0);
    prim(r);
    for(unsigned y=0;y<240;y++)for(unsigned x=0;x<320;x++)
        put16(target+2*(y*320+x),initial(x,y));
    // Two disjoint GPU candidates: preserve the gap, not just the outer bounds.
    r.fillRect(4,5,12,9);
    r.fillRect(20,12,24,15);
    flush();
    unsigned rgb=0,alpha=0,first=~0u;
    for(unsigned y=0;y<240;y++)for(unsigned x=0;x<320;x++) {
        bool covered=(x>=4 && x<12 && y>=5 && y<9) ||
                     (x>=20 && x<24 && y>=12 && y<15);
        unsigned expected=covered?0xf801:initial(x,y);
        unsigned actual=get16(target+2*(y*320+x));
        if((actual^expected)&0xfffe)++rgb;
        if((actual^expected)&1)++alpha;
        if(actual!=expected && first==~0u)first=y*320+x;
    }
    std::printf("REPLAY partial-fill rgb=%u alpha=%u first=%u %s\n",
        rgb,alpha,first,(rgb||alpha)?"FAIL":"PASS");
    failures+=(rgb||alpha);
    require_gpu("partial-fill");
}
static void combined_transition(GFX_INFO info) {
    RDP r(info);
    r.setScissor(0,0,320,240,0);
    // Uniform red makes the expected result independent of filtering/edges.
    for(unsigned i=0;i<4;i++)put16(0x100+i*2,0xf801);
    r.setTImg(0,2,2,ram+0x100);
    for(int tile=0;tile<2;tile++) {
        r.setTile(0,2,1,0,tile,0,2,0,0,2,0,0);
        r.setTileSize(0,0,1,1,tile);
    }
    r.loadTile(0,0,0,1,1);
    for(unsigned pass=0;pass<2;pass++) {
        r.setCImg(0,2,320,ram+target);
        r.setOtherMode_h(20,1);
        r.setOtherMode_l(3,((3u<<26)|(2u<<18))>>3);
        // First cycle = TEXEL0; second = COMBINED, including alpha.
        r.setCombineMode((15u<<20)|(31u<<15)|(7u<<12)|(7u<<9)|(15u<<5)|31u,
            (15u<<28)|(15u<<24)|(7u<<21)|(7u<<18)|(1u<<15)|(7u<<12)|(1u<<9)|(7u<<3));
        r.texRect(0,0,0,2,2,0,0,1,1);
        flush();
        unsigned producer=get16(target);
        // Width forces software fallback. Its one-cycle fill consumes COMBINED.
        r.setCImg(0,2,16,ram+0x40000);
        r.setOtherMode_h(20,0);
        r.setOtherMode_l(3,0);
        r.fillRect(0,0,2,2);
        flush();
        unsigned consumer=get16(0x40000);
        bool bad=producer!=0xf801 || consumer!=0xf801;
        std::printf("REPLAY combined-%s producer=%04x consumer=%04x %s\n",
            pass?"hot":"cold",producer,consumer,bad?"FAIL":"PASS");
        failures+=bad;
    }
}
static void opaque_ramp(GFX_INFO info) {
    RDP r(info);
    r.setCImg(0,2,320,ram+target);
    r.setScissor(0,0,320,240,0);
    prim(r);
    for(unsigned i=0;i<512;i++) {
        unsigned x=i&255,y=30+i/256;
        unsigned red=x,green=(x*37)&255,blue=255-x;
        r.setPrimColor((red<<24)|(green<<16)|(blue<<8)|255,0,0);
        r.fillRect(x,y,x+1,y+1);
    }
    flush();
    unsigned errors=0;
    for(unsigned i=0;i<512;i++) {
        unsigned x=i&255,y=30+i/256;
        unsigned red=x,green=(x*37)&255,blue=255-x;
        unsigned expected=((red>>3)<<11)|((green>>3)<<6)|((blue>>3)<<1)|1;
        unsigned actual=get16(target+2*(y*320+x));
        if(actual!=expected) {
            if(errors<4)std::printf("REPLAY ramp x=%u expected=%04x actual=%04x\n",x,expected,actual);
            ++errors;
        }
    }
    std::printf("REPLAY opaque-ramp mismatches=%u %s\n",errors,errors?"FAIL":"PASS");
    failures+=(errors!=0);
    require_gpu("opaque-ramp",2);
}
static void mixed_transition(GFX_INFO info) {
    RDP r(info);
    r.setCImg(0,2,320,ram+target);
    r.setScissor(0,0,320,240,0);
    prim(r);
    for(unsigned y=0;y<240;y++)for(unsigned x=0;x<320;x++)
        put16(target+2*(y*320+x),initial(x,y));
    r.fillRect(4,5,12,9);
    // Positive alpha threshold forces fallback; every transparent sample fails.
    r.setBlendColor(128);
    r.setOtherMode_l(0,1);
    r.setPrimColor(0,0,0);
    r.fillRect(0,0,32,32);
    // CPU observes and edits the prior GPU region between completed batches.
    put16(target+2*(5*320+4),0);
    r.setOtherMode_l(0,0);
    r.setPrimColor(0x00ff00ff,0,0);
    r.fillRect(20,12,24,15);
    flush();
    unsigned errors=0;
    for(unsigned y=0;y<240;y++)for(unsigned x=0;x<320;x++) {
        unsigned expected=initial(x,y);
        if(x>=4 && x<12 && y>=5 && y<9)expected=0xf801;
        if(x==4 && y==5)expected=0;
        if(x>=20 && x<24 && y>=12 && y<15)expected=0x07c1;
        if(get16(target+2*(y*320+x))!=expected)++errors;
    }
    std::printf("REPLAY mixed-transition mismatches=%u %s\n",errors,errors?"FAIL":"PASS");
    failures+=(errors!=0);
    require_gpu("mixed-transition",2);
}
int main() {
#ifdef DC_RASTER_PVR
    vid_set_mode(DM_640x480,PM_RGB565);
    pvr_init_params_t params={};
    params.opb_sizes[0]=PVR_BINSIZE_16;
    params.opb_sizes[2]=PVR_BINSIZE_16;
    params.autosort_disabled=1;
    params.vertex_buf_size=128*1024;
    params.opb_overflow_count=1;
    if(pvr_init(&params)<0)return 2;
#endif
    GFX_INFO info={};info.RDRAM=ram;info.MemoryBswaped=TRUE;
    partial_fill(info);
    opaque_ramp(info);
    mixed_transition(info);
    combined_transition(info);
    std::printf("REPLAY RESULT failures=%u stop=%d %s\n",failures,stop,
        failures||stop?"FAIL":"PASS");
#ifdef DC_RASTER_PVR
    pvr_shutdown();
    // Keep serial results available to the target capture tool.
    for(;;)thd_sleep(1000);
#endif
    return failures||stop?1:0;
}
