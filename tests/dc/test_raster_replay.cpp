// Synthetic RDP command replays shared by the ILP32 reference and real KOS/PVR.
// No ROM/checkpoint data. Compare emulated framebuffer words, before VI scaling.
#include <cstdio>
#include <cstring>
#include <ctime>
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
struct RasterReplayProbe {
    static void wrapEpoch(TX &tx) {
        // Seed tags from the next generation so wrap must actually clear them.
        for(auto &entry:tx.sampleCache)if(entry.epoch==tx.sampleEpoch)entry.epoch=1;
        tx.sampleEpoch=~0u;
    }
    static unsigned cached(TX &tx) {
        unsigned count=0;
        for(auto &entry:tx.sampleCache)if(entry.epoch==tx.sampleEpoch)++count;
        return count;
    }
    static unsigned state(RDP &r) {
        CC &c=*r.cc; BL &b=*r.bl;
        Color32 values[]={c.texel0,c.texel1,c.texel0Alpha,c.texel1Alpha,
            c.combined,c.combinedAlpha,c.shade,c.shadeAlpha,
            b.pixelColor,b.blendedPixelColor,b.memoryColor,b.invertedAlpha,b.shadeColor};
        const unsigned char *bytes=reinterpret_cast<const unsigned char*>(values);
        unsigned hash=2166136261u;
        for(unsigned i=0;i<sizeof(values);i++)hash=(hash^bytes[i])*16777619u;
        return hash;
    }
};
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
static unsigned checksum(unsigned address,unsigned words) {
    unsigned hash=2166136261u;
    for(unsigned i=0;i<words;i++)hash=(hash^get16(address+i*2))*16777619u;
    return hash;
}
static unsigned long long micros() {
#ifdef DC_RASTER_PVR
    return timer_us_gettime64();
#else
    return (unsigned long long)std::clock()*1000000/CLOCKS_PER_SEC;
#endif
}
static void texture_spans(GFX_INFO info) {
    RDP r(info);
    r.setCImg(0,2,320,ram+target);
    r.setZImg(ram+0x80000);
    // Include every five-bit channel value, both alpha values, and row changes.
    for(unsigned y=0;y<8;y++)for(unsigned x=0;x<32;x++)
        put16(0x100+2*(y*32+x),initial(x,y));
    r.setTImg(0,2,32,ram+0x100);
    r.setTile(0,2,8,0,0,0,2,0,0,2,0,0);
    r.setTileSize(0,0,31,7,0);
    r.loadTile(0,0,0,31,7);
    r.setOtherMode_h(20,0);
    r.setOtherMode_l(3,0);
    // One-cycle TEXEL0, including alpha.
    r.setCombineMode((15u<<5)|31u,
        (15u<<24)|(7u<<21)|(7u<<18)|(1u<<6)|(7u<<3)|1u);
    r.setScissor(0,0,320,240,0);
    r.texRect(0,0,0,32,8,0,0,1,1);
    flush();
    unsigned errors=0;
    for(unsigned y=0;y<8;y++)for(unsigned x=0;x<32;x++)
        if(get16(target+2*(y*320+x))!=(initial(x,y)|1))++errors;
    // Differential cases exercise fractional/filter/scissor combinations and
    // rejection at alpha, depth, blending, empty and invalid-address boundaries.
    for(unsigned n=0;n<96;n++) {
        for(unsigned i=0;i<320*240;i++) {
            put16(target+2*i,initial(i%320,i/320));
            put16(0x80000+2*i,0xffff);
        }
        r.setCImg(0,2,320,ram+target);
        unsigned blend=n%8==3?0x10:n%8==4?0x20:n%8==5?0x4000:n%8==6?0x1000:0;
        if(n>=48)blend|=0x2000; // Alpha-from-coverage must survive in final state.
        r.setOtherMode_l(3,blend>>3);
        r.setBlendColor(128);
        r.setOtherMode_l(0,n%8==1?1:n%8==2?3:0);
        r.setOtherMode_h(12,(n/8)%3==0?0:(n/8)%3==1?2:3);
        r.setScissor(n%4==0?3.5f:0,n%3==0?2.25f:0,319,239,0);
        float step=n%2?0.375f:1.0f;
        float left=n%8==7?-4.0f:0.25f;
        if(n>=88)r.setCImg(0,2,320,ram+sizeof(ram)-128);
        r.texRect(0,left,0.5f,n%16==15?left:39.75f,13.25f,0.125f,0.5f,step,step);
        flush();
        std::printf("REPLAY span-case %u color=%08x depth=%08x tail=%08x state=%08x\n",n,
            checksum(target,320*240),checksum(0x80000,320*240),checksum(sizeof(ram)-128,64),RasterReplayProbe::state(r));
    }
    r.setCImg(0,2,320,ram+target);
    r.setOtherMode_l(3,0);r.setOtherMode_l(0,0);r.setOtherMode_h(12,0);
    r.setScissor(0,0,320,240,0);
    unsigned long long start=micros();
    for(unsigned n=0;n<24;n++)r.texRect(0,0,0,320,240,0,0,1,1);
    flush();
    std::printf("REPLAY texture-spans mismatches=%u %s\n",errors,errors?"FAIL":"PASS");
    std::printf("REPLAY span-benchmark draws=24 us=%llu hash=%08x\n",micros()-start,checksum(target,320*240));
    failures+=(errors!=0);
}
static unsigned sample_hash(TX &tx,TF &tf) {
    unsigned hash=2166136261u;
    for(int pass=0;pass<2;pass++)for(int y=-2;y<10;y++)for(int x=-2;x<18;x++) {
        Color32 c=tx.getTexel(x*0.75f,y*0.75f,0,&tf);
        const unsigned char *p=reinterpret_cast<const unsigned char*>(&c);
        for(unsigned i=0;i<sizeof(c);i++)hash=(hash^p[i])*16777619u;
    }
    // Deliberate cache-index collisions and out-of-range coordinates.
    const int collisions[]={0,256,0,-256,0};
    for(int x:collisions) {
        Color32 c=tx.getTexel(x,0,0,&tf);
        hash=(hash^(unsigned)(int)c)*16777619u;
    }
    const float boundaries[]={-0.0f,0.0f,-0.01f,0.49999997f,0.5f,0.99999994f,
        1.0f,1.00000012f,255.99998f,8388607.5f,8388608.0f,-8388607.5f};
    for(float value:boundaries) {
        Color32 c=tx.getTexel(value,0.25f,0,&tf);
        const unsigned char *p=reinterpret_cast<const unsigned char*>(&c);
        for(unsigned i=0;i<sizeof(c);i++)hash=(hash^p[i])*16777619u;
    }
    return hash;
}
static void texture_cache(GFX_INFO info) {
    TX tx(info);TF tf;
    const unsigned source=0x1000,pal=0x3000;
    unsigned errors=0;
    for(unsigned n=0;n<64;n++) {
        const int formats[]={0,0,2,2,3,3,3,4};
        const int sizes[]={2,3,0,1,0,1,2,1};
        int fmt=formats[n%8],size=sizes[n%8];
        for(unsigned i=0;i<1024;i++)ram[(source+i)^3]=(i*37+n*11)&255;
        for(unsigned i=0;i<256;i++)put16(pal+i*2,initial(i,n));
        tx.setTImg(0,2,256,ram+pal);
        tx.setTile(0,2,1,256,7,0,2,0,0,2,0,0);
        tx.loadTLUT(7,256);
        tx.setTextureLUT(2);
        tx.setTImg(fmt,size,16,ram+source);
        tx.setTile(fmt,size,8,0,0,0,2,0,0,2,0,0);
        tx.setTileSize(0,0,15,7,0);
        tx.loadBlock(0,0,0,255,0);
        tf.setTextureFilter(n%3==0?0:n%3==1?2:3);
        unsigned before=sample_hash(tx,tf);
        for(unsigned i=0;i<1024;i++)ram[(source+i)^3]^=0x5a;
        switch(n%6) {
        case 0:tx.loadBlock(0,0,0,255,0);break;
        case 1:tx.loadTile(0,0,0,15,7);break;
        case 2:
            for(unsigned i=0;i<256;i++)put16(pal+i*2,initial(i+7,n+3));
            tx.setTImg(0,2,256,ram+pal);tx.loadTLUT(7,256);break;
        case 3:tx.setTextureLUT(3);break;
        case 4:tx.setTile(fmt,size,8,0,0,3,1,3,1,1,4,1);break;
        case 5:tx.setTileSize(2,1,6,4,0);break;
        }
        if(n==63) { RasterReplayProbe::wrapEpoch(tx);tx.setTileSize(0,0,7,3,0); }
        unsigned after=sample_hash(tx,tf);
#ifndef DC_SOFT_REFERENCE
        if(!RasterReplayProbe::cached(tx))++errors;
#endif
        std::printf("REPLAY cache-case %u before=%08x after=%08x\n",n,before,after);
    }
    // A valid first row is written before an invalid second row aborts loadTile.
    tx.setTextureLUT(2);tx.setTImg(0,2,4,ram+source);
    tx.setTile(0,2,1,511,0,0,2,0,0,2,0,0);tx.setTileSize(0,0,3,1,0);
    tx.loadBlock(0,0,0,3,0);sample_hash(tx,tf);
    tx.getTexel(0,0,0,nullptr); // Keep this exact key hot across the partial write.
    for(unsigned i=0;i<4;i++)put16(source+i*2,0xf801);
    tx.loadTile(0,0,0,3,1);
    if((unsigned)(int)tx.getTexel(0,0,0,nullptr)!=0xff0000ffu)++errors;
    std::printf("REPLAY texture-cache mismatches=%u %s\n",errors,errors?"FAIL":"PASS");
    failures+=(errors!=0);
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
    texture_spans(info);
    texture_cache(info);
    std::printf("REPLAY RESULT failures=%u stop=%d %s\n",failures,stop,
        failures||stop?"FAIL":"PASS");
#ifdef DC_RASTER_PVR
    pvr_shutdown();
    // Keep serial results available to the target capture tool.
    for(;;)thd_sleep(1000);
#endif
    return failures||stop?1:0;
}
