// Synthetic RDP command replays shared by the ILP32 reference and real KOS/PVR.
// No ROM/checkpoint data. Compare emulated framebuffer words, before VI scaling.
#include <cstdio>
#include <cstring>
#include <ctime>
#include <initializer_list>
#include <algorithm>
#include "../../mupen64_soft_gfx/rdp.h"
#include "../../platform/dc_raster_pixels.h"
#include "../../platform/dc_raster_origin.h"
#include <limits>
#ifdef DC_REPLAY_KOS
#include <kos.h>
KOS_INIT_FLAGS(INIT_DEFAULT);
#endif
#ifdef DC_RASTER_PVR
#include "../../platform/dc_raster_pvr.h"
#endif

extern "C" { int stop=0; }
static unsigned char ram[4*1024*1024];
static unsigned failures;
static const unsigned target=0x10000;
struct RasterReplayProbe {
    static unsigned collisionTexture(RDP &r, bool blue) {
        // TMEM stores network-order RGBA bytes inside little-endian words.
        unsigned a=0x01f801f8,b=0x3f003f00,second=0x12345678;
        unsigned ha=(2166136261u^a)*16777619u,hb=(2166136261u^b)*16777619u;
        unsigned words[2]={blue?b:a,blue?(hb^ha^second):second};
        r.tx->invalidateSamples();
        std::memcpy(r.tx->tmem,words,sizeof(words));
        unsigned hash=2166136261u;
        for(unsigned i=0;i<sizeof(r.tx->tmem)/4;i++) {
            unsigned value;std::memcpy(&value,r.tx->tmem+4*i,4);hash=(hash^value)*16777619u;
        }
        for(unsigned i=0;i<sizeof(r.tx->paletteData)/4;i++) {
            unsigned value;std::memcpy(&value,(unsigned char*)r.tx->paletteData+4*i,4);hash=(hash^value)*16777619u;
        }
        return hash;
    }
    static void shade(RDP &r, Color32 color) { r.cc->setShade(color); }
    static void combined(RDP &r, Color32 color) { r.cc->combined=color; }
    static bool needsSecondTexel(RDP &r) { return r.cc->needsSecondTexel(); }
    static void wrapEpoch(TX &tx) {
        // Seed tags from the next generation so wrap must actually clear them.
        for(auto &entry:tx.sampleCache)if(entry.epoch==tx.sampleEpoch)entry.epoch=1;
        tx.sampleEpoch=~0u;
    }
    static void wrapEpoch(RDP &r) { wrapEpoch(*r.tx); }
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
    static void seedBlender(RDP &r) {
        r.bl->pixelColor=Color32(0x23561788);r.bl->shadeColor=Color32(0x71892344);
        r.bl->invertedAlpha=Color32(0x17263594);r.bl->memoryColor=Color32(0x62817355);
    }
    static void softwareRect(RDP &r,float ds,float dt,bool flip) {
        r.rs->texRect(0,16,16,24,24,0,0,ds,dt,&r,flip);
    }
    static void scalarPixel(RDP &r,unsigned x,unsigned y,float s,float t) {
        Color32 c=r.cc->combine1(r.tx->getTexel(s,t,0,r.tf));
        r.bl->cycle1ModeDraw(x,y,c);
    }
    static Color32 combineValue(RDP &r,const Color32 &texel) {
        return r.cc->combine1(texel);
    }
    static Color32 simpleValue(RDP &r,const Color32 &texel,unsigned &optimized) {
#ifdef DC_SOFT_COMBINE_SPAN
        CC::SimpleSpan span;
        if(r.cc->prepareSimpleSpan(span)) {
            Color32 value=span.combine(texel);r.cc->finishSimpleSpan(texel);++optimized;
            return value;
        }
#endif
        return combineValue(r,texel);
    }
    static bool sameState(RDP &r,RDP &reference) {
        CC &c=*r.cc,&d=*reference.cc;BL &b=*r.bl,&e=*reference.bl;
        Color32 actual[]={c.texel0,c.texel1,c.texel0Alpha,c.texel1Alpha,
            c.combined,c.combinedAlpha,c.shade,c.shadeAlpha,
            b.pixelColor,b.blendedPixelColor,b.memoryColor,b.invertedAlpha,b.shadeColor};
        Color32 expected[]={d.texel0,d.texel1,d.texel0Alpha,d.texel1Alpha,
            d.combined,d.combinedAlpha,d.shade,d.shadeAlpha,
            e.pixelColor,e.blendedPixelColor,e.memoryColor,e.invertedAlpha,e.shadeColor};
        return !std::memcmp(actual,expected,sizeof(actual));
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
    // Disjoint GPU candidates: preserve gaps and cover the final mask/row.
    r.fillRect(4,5,12,9);
    r.fillRect(20,12,24,15);
    r.fillRect(318,238,320,240);
    flush();
    unsigned rgb=0,alpha=0,first=~0u;
    for(unsigned y=0;y<240;y++)for(unsigned x=0;x<320;x++) {
        bool covered=(x>=4 && x<12 && y>=5 && y<9) ||
                     (x>=20 && x<24 && y>=12 && y<15) || (x>=318 && y>=238);
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
static void unused_tile_gpu(GFX_INFO info) {
    RDP r(info);r.setCImg(0,2,320,ram+target);r.setScissor(0,0,320,240,0);
    for(unsigned i=0;i<320*240;i++)put16(target+i*2,1);
    for(unsigned i=0;i<4;i++)put16(0x100+i*2,0xf801);
    r.setTImg(0,2,2,ram+0x100);
    r.setTile(0,2,1,0,0,0,2,0,0,2,0,0);r.setTileSize(0,0,1,1,0);
    r.loadTile(0,0,0,1,1);
    // The unused next tile differs in every coordinate property. None of
    // those properties can disqualify a TEXEL0-only shader.
    r.setTile(0,2,4,64,1,0,1,4,2,1,4,3);r.setTileSize(3,5,18,20,1);
    r.setOtherMode_h(20,1);r.setOtherMode_h(12,0);
    r.setOtherMode_l(3,((3u<<26)|(2u<<18))>>3);
    r.setCombineMode((15u<<20)|(31u<<15)|(7u<<12)|(7u<<9)|(15u<<5)|31u,
        (15u<<28)|(15u<<24)|(7u<<21)|(7u<<18)|(1u<<15)|(7u<<12)|(1u<<9)|(7u<<3));
    r.texRect(0,0,0,4,4,0,0,1,1);flush();
    unsigned errors=0;
    for(unsigned y=0;y<240;y++)for(unsigned x=0;x<320;x++)
        if(get16(target+2*(y*320+x))!=(x<4 && y<4?0xf801u:1u))++errors;
    std::printf("REPLAY unused-tile mismatches=%u %s\n",errors,errors?"FAIL":"PASS");
    failures+=(errors!=0);
#if defined(DC_RASTER_PVR) && !defined(DC_RASTER_STRICT)
    require_gpu("unused-tile");
#endif
}
static void identity_blender(GFX_INFO info) {
    unsigned errors=0;
    RDP r(info);r.setCImg(0,2,320,ram+target);r.setScissor(0,0,320,240,0);
    for(unsigned i=0;i<320*240;i++)put16(target+2*i,1);
    r.setOtherMode_h(20,0);r.setOtherMode_h(12,0);
    r.setOtherMode_l(3,0x0f0a4000u>>3);
    r.setCombineMode((15u<<5)|31u,
        (15u<<24)|(7u<<21)|(7u<<18)|(1u<<6)|(7u<<3)|1u);
    r.setTImg(0,2,2,ram+0x100);r.setTile(0,2,1,0,0,0,2,0,0,2,0,0);
    r.setTileSize(0,0,1,1,0);
    for(unsigned color: {0xf801u,0x07c1u}) {
        for(unsigned i=0;i<4;i++)put16(0x100+2*i,color);
        r.loadTile(0,0,0,1,1);r.texRect(0,16,16,24,24,0,0,0.25f,0.25f);flush();
        for(unsigned y=17;y<23;y++)for(unsigned x=17;x<23;x++)
            errors+=get16(target+2*(y*320+x))!=color;
    }
#ifdef DC_RASTER_PVR
    unsigned scenes=PVRRaster::completedScenes();
#if defined(DC_RASTER_SHADE) && defined(DC_RASTER_IDENTITY) && !defined(DC_RASTER_STRICT)
    errors+=scenes!=2;
#else
    errors+=scenes!=0;
#endif
#endif
    // A nonzero alpha threshold must retain the software rejection path.
    r.setOtherMode_l(0,1);r.setBlendColor(128);
    for(unsigned i=0;i<4;i++)put16(0x100+2*i,0x003e);
    r.loadTile(0,0,0,1,1);r.texRect(0,16,16,24,24,0,0,0.25f,0.25f);flush();
    for(unsigned y=17;y<23;y++)for(unsigned x=17;x<23;x++)
        errors+=get16(target+2*(y*320+x))!=0x07c1;
#ifdef DC_RASTER_PVR
    errors+=PVRRaster::completedScenes()!=scenes;
#endif
    std::printf("REPLAY identity-blender mismatches=%u %s\n",errors,errors?"FAIL":"PASS");
    failures+=(errors!=0);
#if defined(DC_RASTER_PVR) && defined(DC_RASTER_SHADE) && defined(DC_RASTER_IDENTITY) && !defined(DC_RASTER_STRICT)
    require_gpu("identity-blender",2);
#endif
}

static void full_alpha(GFX_INFO info) {
    unsigned errors=0,scenes=0;
    for(unsigned test=0;test<8;test++) {
        RDP r(info);r.setCImg(0,2,320,ram+target);r.setScissor(0,0,320,240,0);
        for(unsigned i=0;i<320*240;i++)put16(target+2*i,1);
        for(unsigned i=0;i<4;i++)put16(0x100+2*i,0xf801);
        r.setTImg(0,2,2,ram+0x100);r.setTile(0,2,1,0,0,0,2,0,0,2,0,0);
        r.setTileSize(0,0,1,1,0);r.loadTile(0,0,0,1,1);
        r.setOtherMode_h(20,0);r.setOtherMode_h(12,0);r.setOtherMode_l(3,0x0f0a7008u>>3);
        r.setPrimColor(test==5?0xff0000fe:test==6?0xff0000ff:0xff0000ff,0,0);
        r.setEnvColor(test==6?0xff00001f:0xff0000ff);
        RasterReplayProbe::shade(r,Color32(255,255,255,test==4?31:255));
        unsigned alpha=test==0 || test==4?4:test==1 || test==5?3:test==2?5:1;
        r.setCombineMode((15u<<5)|31u,
            (15u<<24)|(7u<<21)|(7u<<18)|(1u<<6)|(7u<<3)|alpha);
        if(test==3 || test==6)r.setCombineMode(0x0050d2a1u,0x33a5b6dbu);
        r.texRect(0,16,16,24,24,0,0,0.25f,0.25f);flush();
        unsigned expected=test==4 || test==6?1:0xf801;
        for(unsigned y=0;y<240;y++)for(unsigned x=0;x<320;x++)
            errors+=get16(target+2*(y*320+x))!=(x>=16 && x<24 && y>=16 && y<24?expected:1u);
#ifdef DC_RASTER_PVR
        unsigned actual=PVRRaster::completedScenes();
#if defined(DC_RASTER_FULL_ALPHA) && defined(DC_RASTER_IDENTITY) && !defined(DC_RASTER_STRICT)
        bool gpu=test<4;
#ifdef DC_RASTER_TEXTURE_ALPHA
        gpu=gpu || test==7;
#endif
        errors+=actual!=(gpu?1u:0u);
#else
        errors+=actual!=0;
#endif
        scenes+=actual;
#endif
        std::printf("REPLAY full-alpha-case %u expected=%04x\n",test,expected);
    }
    std::printf("REPLAY full-alpha mismatches=%u %s\n",errors,errors?"FAIL":"PASS");
    failures+=errors!=0;
#ifdef DC_RASTER_PVR
    std::printf("REPLAY full-alpha completed-scenes=%u\n",scenes);
#endif
}
static void texture_alpha(GFX_INFO info) {
    unsigned errors=0,scenes=0;
    for(unsigned test=0;test<9;test++) {
        RDP r(info);r.setCImg(0,2,320,ram+target);r.setScissor(0,0,320,240,0);
        for(unsigned i=0;i<320*240;i++)put16(target+2*i,1);
        r.setOtherMode_h(20,0);r.setOtherMode_h(12,2);r.setOtherMode_l(3,0x0f0a7008u>>3);
        r.setEnvColor(test==2?0xfffffffe:test==3?0xffffff1f:0xffffffff);
        r.setPrimColor(0xffffffff,0,0);
        for(unsigned i=0;i<4;i++)put16(0x100+2*i,test==4?0xf800:0xf801);
        r.setTImg(0,2,2,ram+0x100);r.setTile(0,2,1,0,0,0,2,0,0,2,0,0);
        if(test==8) {
            for(unsigned i=0;i<4;i++)ram[(0x100+i)^3]=0xf8; // IA8: alpha136.
            r.setTImg(3,1,2,ram+0x100);r.setTile(3,1,1,0,0,0,2,0,0,2,0,0);
        }
        r.setTileSize(0,0,1,1,0);r.loadTile(0,0,0,1,1);
        unsigned hi=(15u<<5)|31u;
        unsigned lo=(15u<<24)|(7u<<21)|(7u<<18)|(1u<<6)|(7u<<3)|1u;
        if(test>=1 && test<=3)lo=(lo&~((7u<<21)|(7u<<18)|(7u<<3)|7u))|
            (1u<<21)|(5u<<18)|(7u<<3)|7u; // TEXEL0 alpha * ENV alpha.
        if(test==6)lo=(lo&~7u)|2u; // Unknown/uninitialized TEXEL1, not a proof.
        if(test==7){lo&=~7u;RasterReplayProbe::combined(r,Color32(0xffffffff));}
        r.setCombineMode(hi,lo);
        unsigned draws=test==5?2:1;
        for(unsigned draw=0;draw<draws;draw++) {
            if(draw)r.setEnvColor(0x00ff00ff); // Unused RGB changes on a hot hit.
            r.texRect(0,16,16,24,24,0,0,0.25f,0.25f);flush();
        }
        unsigned expected=test==3 || test==4 || test==6?1:test==8?0xffff:0xf801;
        for(unsigned y=0;y<240;y++)for(unsigned x=0;x<320;x++)
            errors+=get16(target+2*(y*320+x))!=(x>=16 && x<24 && y>=16 && y<24?expected:1u);
#ifdef DC_RASTER_PVR
        unsigned actual=PVRRaster::completedScenes();
#if defined(DC_RASTER_TEXTURE_ALPHA) && defined(DC_RASTER_FULL_ALPHA) && defined(DC_RASTER_IDENTITY) && !defined(DC_RASTER_STRICT)
        errors+=actual!=((test==0 || test==1 || test==5)?draws:0u);
#else
        errors+=actual!=0;
#endif
        scenes+=actual;
#endif
        std::printf("REPLAY texture-alpha-case %u expected=%04x\n",test,expected);
    }
    std::printf("REPLAY texture-alpha mismatches=%u %s\n",errors,errors?"FAIL":"PASS");
    failures+=errors!=0;
#ifdef DC_RASTER_PVR
    std::printf("REPLAY texture-alpha completed-scenes=%u\n",scenes);
#endif
}
static unsigned checksum(unsigned address,unsigned words);
static void coverage_point(GFX_INFO info) {
    unsigned errors=0,scenes=0;
    for(unsigned test=0;test<15;test++) {
        RDP r(info);r.setCImg(0,2,320,ram+target);r.setScissor(0,0,320,240,0);
        for(unsigned i=0;i<320*240;i++)put16(target+2*i,test>=13?0:1);
        unsigned alpha=test==2 || test==8 || test==9 || test==10 || test==12?31:
                       test==3 || test==5 || test==6?32:test==4?254:test==11?128:255;
        r.setEnvColor(0xffffff00u|alpha);
        r.setOtherMode_h(20,0);r.setOtherMode_h(12,2);r.setOtherMode_l(3,0x0f0a7008u>>3);
        if(test==5 || test==6){r.setOtherMode_l(0,1);r.setBlendColor(test==5?64:32);}
        if(test==12)r.setOtherMode_l(0,3);
        if(test==11)r.setOtherMode_l(3,((1u<<22)|(1u<<14)|(1u<<12))>>3);
        r.setCombineMode((15u<<5)|31u,
            (15u<<24)|(1u<<21)|(5u<<18)|(1u<<6)|(7u<<3)|7u); // RGB=TEX0; A=TEX0*ENV.
        if(test==7) {
            const unsigned a[]={0,1,2,3,4,5,6,15};
            for(unsigned y=0;y<8;y++)for(unsigned x=0;x<8;x++)ram[(0x100+y*8+x)^S8]=0xf0|a[x];
            r.setTImg(3,1,8,ram+0x100);r.setTile(3,1,1,0,0,0,2,0,0,2,0,0);
        } else {
            for(unsigned y=0;y<8;y++)for(unsigned x=0;x<8;x++)put16(0x100+2*(y*8+x),x&1?0xf801:0xf800);
            r.setTImg(0,2,8,ram+0x100);r.setTile(0,2,2,0,0,0,2,0,0,2,0,0);
        }
        r.setTileSize(0,0,7,7,0);r.loadTile(0,0,0,7,7);
        unsigned draws=test==1 || test==14?2:1;
        for(unsigned draw=0;draw<draws;draw++) {
            if(draw && test==1)r.setEnvColor(0x00ff00ff); // Hot unused RGB dependency.
            float origin=test==9?16.5f:16;
            r.texRect(0,origin,16,24,24,test==14?draw:0,0,test==8?0.5f:1,test==10?2:1);
            if(test!=14 || draw+1==draws)flush();
        }
        for(unsigned y=0;y<240;y++)for(unsigned x=0;x<320;x++) {
            unsigned expected=test>=13?0:1;
            if(x>=16 && x<24 && y>=16 && y<24 && test<8 && test!=2 && test!=5) {
                if(test==7)expected=x-16>=2?0xffff:1;
                else if((x-16)&1)expected=0xf801;
            }
            if(test==11 && x>=16 && x<24 && y>=16 && y<24 && ((x-16)&1))expected=0x8001;
            if(test>=13 && x>=16 && x<24 && y>=16 && y<24 && (test==14 || ((x-16)&1)))expected=0xf801;
            errors+=get16(target+2*(y*320+x))!=expected;
        }
#ifdef DC_RASTER_PVR
        unsigned actual=PVRRaster::completedScenes();
#if defined(DC_RASTER_CVG_POINT) && !defined(DC_RASTER_STRICT)
        errors+=actual!=(test<8?draws:test>=13?1u:0u);
#else
        errors+=actual!=0;
#endif
        scenes+=actual;
#endif
        std::printf("REPLAY coverage-point-case %u alpha=%u hash=%08x\n",test,alpha,checksum(target,320*240));
    }
    std::printf("REPLAY coverage-point mismatches=%u %s\n",errors,errors?"FAIL":"PASS");
    failures+=errors!=0;
#ifdef DC_RASTER_PVR
    std::printf("REPLAY coverage-point completed-scenes=%u\n",scenes);
#endif
}
static void software_identity_span(GFX_INFO info) {
    unsigned errors=0;
    const unsigned alpha[]={0,31,32,63,64,128,254,255};
    const unsigned referenceTarget=0x40000;
    for(unsigned test=0;test<320;test++) {
        unsigned mode=(test%64)/8,shader=test/64;
        RDP r(info),reference(info);
        for(unsigned i=0;i<320*240;i++) {
            unsigned color=(i*3571u)&0xffffu;
            put16(target+2*i,color);put16(referenceTarget+2*i,color);
        }
        for(unsigned y=0;y<8;y++)for(unsigned x=0;x<8;x++)
            put16(0x100+2*(y*8+x),shader?(x&1?0x5696:0xbead):(x&1?0xf800:0xf801));
        for(RDP *draw:{&r,&reference}) {
            draw->setCImg(0,2,320,ram+(draw==&r?target:referenceTarget));
            draw->setZImg(ram+0x70000);draw->setScissor(0,0,320,240,0);
            draw->setOtherMode_h(20,0);draw->setOtherMode_h(12,2);
            unsigned blender=0x0f0a7008u;
            if(mode==1)blender&=~(1u<<14); // Unforced identity.
            if(mode==4)blender=(1u<<22)|(1u<<14)|(1u<<12); // Real blend fallback.
            if(mode==5)blender|=(1u<<4)|(1u<<5); // Depth fallback.
            draw->setOtherMode_l(3,blender>>3);
            if(mode==2){draw->setOtherMode_l(0,1);draw->setBlendColor(64);}
            if(mode==3)draw->setOtherMode_l(0,3); // Dither fallback.
            draw->setEnvColor((shader?0x8141f100u:0xffffff00u)|alpha[test%8]);
            draw->setPrimColor(0x1fb16300u|alpha[test%8],0,0);
            draw->setCombineMode((15u<<5)|31u,
                (15u<<24)|(1u<<21)|(5u<<18)|(1u<<6)|(7u<<3)|7u);
            if(shader) {
                unsigned factor=shader==2?3:shader==4?0:5;
                unsigned a=shader==3?7:1,c=shader==3?7:factor,d=shader==3?1:7;
                draw->setCombineMode((1u<<5)|factor,
                    (15u<<24)|(a<<21)|(c<<18)|(7u<<6)|(7u<<3)|d);
                RasterReplayProbe::combined(*draw,Color32(43,77,211,alpha[test%8]));
            }
            draw->setTImg(0,2,8,ram+0x100);draw->setTile(0,2,2,0,0,0,2,0,0,2,0,0);
            draw->setTileSize(0,0,7,7,0);draw->loadTile(0,0,0,7,7);
            RasterReplayProbe::seedBlender(*draw);
        }
        float ds=mode==6?0.5f:1,dt=mode==6?0.25f:1;
        // Bypass PVR to test the software span against original scalar BL.
        RasterReplayProbe::softwareRect(r,ds,dt,mode==7);
        for(unsigned y=16;y<24;y++)for(unsigned x=16;x<24;x++) {
            float s=(mode==7?y-16:x-16)*ds,t=(mode==7?x-16:y-16)*dt;
            RasterReplayProbe::scalarPixel(reference,x,y,s,t);
        }
        for(unsigned i=0;i<320*240;i++)errors+=get16(target+2*i)!=get16(referenceTarget+2*i);
        errors+=!RasterReplayProbe::sameState(r,reference);
    }
    std::printf("REPLAY software-identity-span mismatches=%u cases=320 %s\n",errors,errors?"FAIL":"PASS");
#ifdef DC_SOFT_IDENTITY_SPAN
    std::printf("REPLAY identity-span-enabled=1\n");
#else
    std::printf("REPLAY identity-span-enabled=0\n");
#endif
#ifdef DC_SOFT_COMBINE_SPAN
    std::printf("REPLAY combine-span-enabled=1\n");
#else
    std::printf("REPLAY combine-span-enabled=0\n");
#endif
    failures+=errors!=0;
}

static void software_combine_values(GFX_INFO info) {
    unsigned errors=0,optimized=0;
    RDP r(info),reference(info);
    for(unsigned n=0;n<4096;n++) {
        unsigned factor=n%3==1?3:5;
        bool direct=n%3==2;
        for(RDP *draw:{&r,&reference}) {
            draw->setEnvColor(0x8141f100u|((n*73)&255));
            draw->setPrimColor(0xad216300u|((n*91)&255),0,0);
            draw->setCombineMode((1u<<5)|factor,
                (15u<<24)|((direct?7u:1u)<<21)|((direct?7u:factor)<<18)|
                (7u<<6)|(7u<<3)|(direct?1u:7u));
        }
        float value=std::min(255.0f,float(n&255)+float(n>>8)/32);
        Color32 texel(n&15?value:-0.0f,n&31?255-value:-0.0f,
            n&63?float((n*13)&255):-0.0f,n&127?float(n&255):-0.0f);
        Color32 actual=RasterReplayProbe::simpleValue(r,texel,optimized);
        Color32 expected=RasterReplayProbe::combineValue(reference,texel);
        errors+=std::memcmp(&actual,&expected,sizeof(actual))!=0;
        errors+=!RasterReplayProbe::sameState(r,reference);
    }
    std::printf("REPLAY software-combine-values mismatches=%u cases=4096 optimized=%u %s\n",
        errors,optimized,errors?"FAIL":"PASS");
    failures+=errors!=0;
}

static void rectangle_scissor(GFX_INFO info) {
    unsigned errors=0,scenes=0;
    // Check every word, including holes and all four clipped boundaries.
    for(unsigned test=0;test<5;test++) {
        RDP r(info);r.setCImg(0,2,320,ram+target);
        for(unsigned i=0;i<320*240;i++)put16(target+2*i,1);
        for(unsigned i=0;i<4;i++)put16(0x100+2*i,0xf801);
        r.setTImg(0,2,2,ram+0x100);r.setTile(0,2,1,0,0,0,2,0,0,2,0,0);
        r.setTileSize(0,0,1,1,0);r.loadTile(0,0,0,1,1);
        prim(r);r.setCombineMode((15u<<5)|31u,
            (15u<<24)|(7u<<21)|(7u<<18)|(1u<<6)|(7u<<3)|1u);
        unsigned sx=test==1?18:16,sy=test==2?18:16;
        r.setScissor(sx,sy,24,24,0);
        float ux=test==4?15.25f:test==3?24:8;
        float uy=test==3?24:8;
        r.texRect(0,ux,uy,32,32,0,0,0.125f,0.125f);flush();
        for(unsigned y=0;y<240;y++)for(unsigned x=0;x<320;x++)
            errors+=get16(target+2*(y*320+x))!=(test!=3 && x>=sx && x<24 && y>=sy && y<24?0xf801u:1u);
#ifdef DC_RASTER_PVR
        unsigned actual=PVRRaster::completedScenes();
#if defined(DC_RASTER_RECT_CLIP) && !defined(DC_RASTER_STRICT)
        errors+=actual!=(test<3?1u:0u);
#else
        errors+=actual!=0;
#endif
        scenes+=actual;
#endif
    }
    std::printf("REPLAY rectangle-scissor mismatches=%u %s\n",errors,errors?"FAIL":"PASS");
    failures+=errors!=0;
#ifdef DC_RASTER_PVR
    std::printf("REPLAY rectangle-scissor completed-scenes=%u\n",scenes);
#endif
}

static void rectangle_sample(GFX_INFO info) {
    unsigned errors=0,maxError=0,scenes=0,reference[64];
    for(unsigned pass=0;pass<2;pass++) {
        unsigned width=pass?320:16;
#if !defined(DC_RASTER_RECT_SAMPLE) || defined(DC_RASTER_STRICT)
        width=16; // This focused new capability is exercised only when enabled.
#endif
        RDP r(info);r.setCImg(0,2,width,ram+target);r.setScissor(0,0,320,240,0);
        for(unsigned i=0;i<320*240;i++)put16(target+2*i,1);
        for(unsigned y=0;y<4;y++)for(unsigned x=0;x<4;x++)
            put16(0x100+2*(y*4+x),(x*10<<11)|(y*10<<6)|1);
        r.setTImg(0,2,4,ram+0x100);r.setTile(0,2,1,0,0,0,2,0,0,2,0,0);
        r.setTileSize(0,0,3,3,0);r.loadTile(0,0,0,3,3);
        prim(r);r.setOtherMode_h(12,2);
        r.setCombineMode((15u<<5)|31u,
            (15u<<24)|(7u<<21)|(7u<<18)|(1u<<6)|(7u<<3)|1u);
        for(unsigned hot=0;hot<2;hot++) {
            r.texRect(0,0,0,8,8,0,0,0.25f,0.25f);flush();
            for(unsigned y=0;y<8;y++)for(unsigned x=0;x<8;x++) {
                unsigned actual=get16(target+2*(y*width+x)),i=y*8+x;
                if(!pass)reference[i]=actual;
                else {
                    unsigned expected=reference[i];
                    for(unsigned shift:{1u,6u,11u}) {
                        unsigned a=(actual>>shift)&31,b=(expected>>shift)&31;
                        unsigned delta=a>b?a-b:b-a;maxError=std::max(maxError,delta);errors+=delta>1;
                    }
                    errors+=(actual^expected)&1;
                }
            }
        }
#ifdef DC_RASTER_PVR
        if(pass)scenes=PVRRaster::completedScenes();
#if defined(DC_RASTER_RECT_SAMPLE) && !defined(DC_RASTER_STRICT)
        if(pass)errors+=scenes!=2;
#else
        errors+=PVRRaster::completedScenes()!=0;
#endif
#endif
    }
    std::printf("REPLAY rectangle-sample max-channel-error=%u mismatches=%u %s\n",
                maxError,errors,errors?"FAIL":"PASS");
    failures+=errors!=0;
#ifdef DC_RASTER_PVR
    std::printf("REPLAY rectangle-sample completed-scenes=%u\n",scenes);
#endif
}
static void affine_shade(GFX_INFO info) {
    unsigned errors=0,channelError=0,maxError=0,alphaErrors=0,scenes=0;
    unsigned reference[32*16];
    for(unsigned test=0;test<6;test++) {
        auto draw=[&](unsigned width) {
            RDP r(info);r.setCImg(0,2,width,ram+target);r.setScissor(0,0,320,240,0);
            for(unsigned i=0;i<320*240;i++)put16(target+2*i,1);
            for(unsigned i=0;i<4;i++)put16(0x100+2*i,test==5?0x8410:0x8411);
            r.setTImg(0,2,2,ram+0x100);
            r.setTile(0,2,1,0,0,0,2,0,0,2,0,0);r.setTileSize(0,0,1,1,0);
            r.loadTile(0,0,0,1,1);
            r.setOtherMode_h(20,1);r.setOtherMode_h(12,0);
            r.setOtherMode_l(3,(((3u<<26)|(2u<<18)|0x2000))>>3);
            bool additive=test>=2;
            bool translucent=test>=4;
            if(translucent){r.setOtherMode_l(3,0x0c1841c8u>>3);r.setOtherMode_h(12,2);}
            r.setPrimColor(translucent?0x282828b4:additive?0x80808040:0xd2d2d2ff,0,0);
            r.setCombineMode(translucent?0x004093ff:additive?0x00457fff:0x00127fff,
                translucent?0xff0dfe3f:additive?0x3ffcfe3f:0x3ffe7e3f);
            Vektor<float,4> a,b,c;a[0]=0;a[1]=0;b[0]=14;b[1]=16;c[0]=0;c[1]=32;
            Color32 ca=additive?Color32(128,160,192,255):Color32(32,64,96,255);
            Color32 cb=additive?Color32(160,192,224,255):Color32(192,208,224,255);
            Color32 cc=additive?Color32(192,224,255,255):Color32(64,128,192,255);
            if(test==3)ca.setR(64); // A negative offset must fall back.
            float wb=test==0?1.0f:0.75f,wc=test==0?1.0f:1.25f;
            r.tri_shade_txtr_zbuff(a,b,c,ca,cb,cc,0,0,1,0,0,1,0,1,wb,wc,0,0,0);
            flush();
#ifdef DC_RASTER_PVR
            unsigned completed=PVRRaster::completedScenes();
#if defined(DC_RASTER_SHADE) && !defined(DC_RASTER_STRICT)
            bool shouldAccelerate=test<3 || test==4;
            if(width==320 && ((shouldAccelerate && completed==0) || (!shouldAccelerate && completed!=0)))++errors;
#endif
            if(width==320)scenes+=completed;
#endif
        };
        draw(16); // This width forces the exact production software path.
        for(unsigned i=0;i<32*16;i++)reference[i]=get16(target+2*i);
        draw(320);
        unsigned caseMax=0;
        // Interior probes isolate color/interpolation from the existing GPU
        // edge-coverage approximation. This is explicitly a bounded fast-mode
        // color probe; the strict/texture-precision exact tests are unchanged.
        for(unsigned y: {8u,12u,20u,24u})for(unsigned x: {2u,4u}) {
            unsigned expected=reference[y*16+x],actual=get16(target+2*(y*320+x));
            if((expected^actual)&1)++alphaErrors;
            for(unsigned shift: {11u,6u,1u}) {
                unsigned a=(actual>>shift)&31,b=(expected>>shift)&31;
                unsigned error=a>b?a-b:b-a;channelError+=error;
                if(error>caseMax)caseMax=error;
            }
        }
        if(caseMax>maxError)maxError=caseMax;
        if(caseMax>2 || ((test==3 || test==5) && caseMax))++errors;
        std::printf("REPLAY affine-case %u max-channel-error=%u\n",test,caseMax);
    }
    errors+=alphaErrors;
    std::printf("REPLAY affine-shade channel-error=%u max-channel-error=%u alpha-errors=%u eligibility-errors=%u %s\n",
        channelError,maxError,alphaErrors,errors,errors?"FAIL":"PASS");
#if defined(DC_RASTER_PVR) && defined(DC_RASTER_SHADE) && !defined(DC_RASTER_STRICT)
    std::printf("REPLAY affine-shade completed-scenes=%u\n",scenes);
#endif
    failures+=(errors!=0);
}
static void triangle_origin(GFX_INFO info) {
    unsigned errors=0,projectionCases=0,footprintCases=0,sampleCases=0,scenes=0,corrected=0;
    // Independent barycentric evaluation checks the perspective sampling
    // identity at pixel centers, including reversed and fractional triangles.
    auto evaluate=[](const float xy[3][2],const float f[3],float x,float y) {
        float det=(xy[1][1]-xy[2][1])*(xy[0][0]-xy[2][0])+
                  (xy[2][0]-xy[1][0])*(xy[0][1]-xy[2][1]);
        float a=((xy[1][1]-xy[2][1])*(x-xy[2][0])+(xy[2][0]-xy[1][0])*(y-xy[2][1]))/det;
        float b=((xy[2][1]-xy[0][1])*(x-xy[2][0])+(xy[0][0]-xy[2][0])*(y-xy[2][1]))/det;
        return a*f[0]+b*f[1]+(1-a-b)*f[2];
    };
    for(unsigned test=0;test<36;test++) {
        float xy[3][2]={{-2.25f,1.5f},{22.0f+(test%4),3.25f},{5.5f,25.0f}};
        if(test&1){std::swap(xy[1][0],xy[2][0]);std::swap(xy[1][1],xy[2][1]);}
        float u[]={-0.5f+(test%5),0.7f,0.1f},v[]={0.1f,1.1f,0.4f};
        float q[]={1.0f,0.75f+(test%3)*0.1f,1.2f-(test%4)*0.05f},ou[3],ov[3],oq[3];
        DCRasterOrigin origin;
        if(!origin.prepare(xy) || !origin.texture(u,v,q,ou,ov,oq)){++errors;continue;}
        float pu[3],pv[3],au[3],av[3];
        for(unsigned i=0;i<3;i++){pu[i]=u[i]*q[i];pv[i]=v[i]*q[i];au[i]=ou[i]*oq[i];av[i]=ov[i]*oq[i];}
        float x=2+(test%7),y=8+(test%5),eq=evaluate(xy,q,x,y),aq=evaluate(xy,oq,x+0.5f,y+0.5f);
        errors+=std::fabs(eq-aq)>0.00001f;
        errors+=std::fabs(evaluate(xy,pu,x,y)/eq-evaluate(xy,au,x+0.5f,y+0.5f)/aq)>0.00005f;
        errors+=std::fabs(evaluate(xy,pv,x,y)/eq-evaluate(xy,av,x+0.5f,y+0.5f)/aq)>0.00005f;
        ++projectionCases;
    }
    for(unsigned test=0;test<4;test++) {
        float xy[3][2]={{0,0},{1,0},{0,1}},u[]={0,1,0},v[]={0,0,1},q[]={1,1,1},ou[3],ov[3],oq[3];
        DCRasterOrigin origin;bool accepted;
        if(test==0)xy[2][1]=0;
        if(test==1)xy[2][1]=std::numeric_limits<float>::infinity();
        if(test==2){q[0]=0.01f;q[1]=q[2]=10;}
        if(test==3)u[0]=std::numeric_limits<float>::quiet_NaN();
        accepted=origin.prepare(xy) && origin.texture(u,v,q,ou,ov,oq);
        errors+=accepted;++projectionCases;
    }
    static unsigned short footprint[320*240];
    for(unsigned test=0;test<4;test++)for(unsigned pass=0;pass<2;pass++) {
        RDP r(info);r.setCImg(0,2,320,ram+target);r.setScissor(0,0,320,240,0);
        for(unsigned y=0;y<240;y++)for(unsigned x=0;x<320;x++)put16(target+2*(y*320+x),initial(x,y)|1);
        for(unsigned i=0;i<4;i++)put16(0x100+i*2,0x8411);
        r.setTImg(0,2,2,ram+0x100);r.setTile(0,2,1,0,0,0,2,0,0,2,0,0);r.setTileSize(0,0,1,1,0);r.loadTile(0,0,0,1,1);
        prim(r);r.setCombineMode((15u<<5)|31u,(15u<<24)|(7u<<21)|(7u<<18)|(1u<<6)|(7u<<3)|1u);
        Vektor<float,4> a,b,c;a[0]=2;a[1]=2;b[0]=18;b[1]=3;c[0]=4;c[1]=22;
        if(test==1){a[0]+=0.25f;a[1]+=0.75f;b[0]+=0.5f;b[1]+=0.25f;c[0]+=0.5f;c[1]+=0.75f;}
        if(test==2){std::swap(b[0],c[0]);std::swap(b[1],c[1]);}
        if(test==3){a[0]=a[1]=3;b[0]=3.0001f;b[1]=3;c[0]=3;c[1]=3.0001f;}
        Color32 shade(0xffffffff);
#ifdef DC_RASTER_PVR
        PVRRaster::originEnabled(pass!=0);
#endif
        r.tri_shade_txtr_zbuff(a,b,c,shade,shade,shade,0,0,0,0,0,0,0,1,0.75f,1.2f,0,0,0);flush();
        for(unsigned i=0;i<320*240;i++) {
            unsigned value=get16(target+i*2);
            if(!pass)footprint[i]=value;else errors+=value!=footprint[i];
        }
#ifdef DC_RASTER_PVR
        scenes+=PVRRaster::completedScenes();corrected+=PVRRaster::originCount();
#endif
        if(pass)++footprintCases;
    }
    for(unsigned test=0;test<4;test++) {
        unsigned reference[256];
        for(unsigned pass=0;pass<2;pass++) {
            unsigned width=16;
#if defined(DC_RASTER_TRI_ORIGIN) && !defined(DC_RASTER_STRICT)
            if(pass)width=320;
#endif
            RDP r(info);r.setCImg(0,2,width,ram+target);r.setScissor(0,0,320,240,0);
            for(unsigned i=0;i<320*240;i++)put16(target+2*i,1);
            for(unsigned y=0;y<16;y++)for(unsigned x=0;x<16;x++)put16(0x100+2*(y*16+x),
                (((x*3)&31)<<11)|(((y*5)&31)<<6)|(((x*7+y*11)&31)<<1)|1);
            r.setTImg(0,2,16,ram+0x100);r.setTile(0,2,4,0,0,0,2,0,0,2,0,0);r.setTileSize(0,0,15,15,0);r.loadTile(0,0,0,15,15);
            prim(r);r.setCombineMode((15u<<5)|31u,(15u<<24)|(7u<<21)|(7u<<18)|(1u<<6)|(7u<<3)|1u);
            Vektor<float,4> a,b,c;
            if(test&1){a[0]=0;a[1]=16;b[0]=16;b[1]=16;c[0]=16;c[1]=0;}
            else {a[0]=a[1]=0;b[0]=16;b[1]=0;c[0]=0;c[1]=16;}
            if(test&2){std::swap(b[0],c[0]);std::swap(b[1],c[1]);}
            Color32 shade(0xffffffff);
            r.tri_shade_txtr_zbuff(a,b,c,shade,shade,shade,a[0],a[1],b[0],b[1],c[0],c[1],0,1,1,1,0,0,0);flush();
            for(unsigned y=0;y<16;y++)for(unsigned x=0;x<16;x++) {
                unsigned value=get16(target+2*(y*width+x));
                if(!pass)reference[y*16+x]=value;
                // Interior texels isolate sample origin from legacy edge rules.
                else if(x>=2 && y>=2 && x<14 && y<14 &&
                        ((test&1)?x+y>18:x+y<13))errors+=value!=reference[y*16+x];
            }
#ifdef DC_RASTER_PVR
            if(pass){scenes+=PVRRaster::completedScenes();corrected+=PVRRaster::originCount();}
#endif
        }
        ++sampleCases;
    }
#if defined(DC_RASTER_PVR) && defined(DC_RASTER_TRI_ORIGIN) && !defined(DC_RASTER_STRICT)
    errors+=scenes!=12 || corrected!=7;
#endif
    std::printf("REPLAY triangle-origin projection-cases=%u footprint-cases=%u sample-cases=%u mismatches=%u %s\n",
        projectionCases,footprintCases,sampleCases,errors,errors?"FAIL":"PASS");
#ifdef DC_RASTER_PVR
    std::printf("REPLAY triangle-origin corrected=%u completed-scenes=%u\n",corrected,scenes);
#endif
    failures+=errors!=0;
}
static void triangle_filter(GFX_INFO info) {
    unsigned errors=0,baseError=0,gridError=0,maxError=0,cases=0,guards=0,scenes=0,grids=0;
    unsigned reference[2*256];
    static unsigned short guarded[2][320*240];
    for(unsigned test=0;test<12;test++) {
        bool control=test>=8;
        unsigned caseBase=0,caseGrid=0,caseMax=0;
        for(unsigned pass=0;pass<3;pass++) {
            unsigned width=pass?320:16;
            RDP r(info);r.setCImg(0,2,width,ram+target);r.setScissor(0,0,320,240,0);
            for(unsigned i=0;i<320*240;i++)put16(target+2*i,0x0841);
            unsigned tw=test==10?256:4,th=test==10?2:4;
            for(unsigned y=0;y<th;y++)for(unsigned x=0;x<tw;x++) {
                unsigned n=(x+y)&1;
                put16(0x100+2*(y*tw+x),(n?0xf800:0)|((n?8:24)<<6)|((n?31:0)<<1)|1);
            }
            r.setTImg(0,2,tw,ram+0x100);r.setTile(0,2,tw/4,0,0,0,2,0,0,2,0,0);
            r.setTileSize(0,0,tw-1,th-1,0);r.loadTile(0,0,0,tw-1,th-1);
            r.setOtherMode_h(20,1);r.setOtherMode_h(12,test==8?0:test==9?3:2);
            unsigned mode=test<4 || control?1:test<6?2:3;
            r.setOtherMode_l(3,mode==3?0x0c1841c8u>>3:((3u<<26)|(2u<<18)|0x2000)>>3);
            r.setPrimColor(mode==2?0x203040b4:mode==3?0x282828b4:0xffffffff,0,0);
            r.setCombineMode(mode==2?0x00457fff:mode==3?0x004093ff:0x00127fff,
                mode==2?0x3ffcfe3f:mode==3?0xff0dfe3f:0x3ffe7e3f);
            Color32 ca(192,224,248,17),cb(192,224,248,128),cc(192,224,248,240);
            Vektor<float,4> a,b,c;a[0]=a[1]=0;b[0]=16;b[1]=0;c[0]=0;c[1]=16;
            float s0=(test%3)*0.125f,t0=(test%2)*0.125f;
            float wb=(test&1)?0.75f:1,wc=(test&1)?1.2f:1;
#ifdef DC_RASTER_PVR
            PVRRaster::filterGridEnabled(pass==2);
#endif
            for(unsigned hot=0;hot<2;hot++) {
                if(test==11) {
                    RasterReplayProbe::shade(r,ca);
                    r.texRect(0,0,0,12,12,s0,t0,0.25f,0.25f);
                } else r.tri_shade_txtr_zbuff(a,b,c,ca,cb,cc,s0,t0,s0+4,t0,s0,t0+4,0,1,wb,wc,0,0,0);
                flush();
                for(unsigned y=2;y<12;y++)for(unsigned x=2;x<12;x++)if(x+y<13) {
                    unsigned actual=get16(target+2*(y*width+x)),index=hot*256+y*16+x;
                    if(!pass)reference[index]=actual;
                    else if(!control) {
                        unsigned expected=reference[index];errors+=(actual^expected)&1;
                        for(unsigned shift:{11u,6u,1u}) {
                            unsigned av=(actual>>shift)&31,ev=(expected>>shift)&31;
                            unsigned delta=av>ev?av-ev:ev-av;
                            if(pass==1){baseError+=delta;caseBase+=delta;}
                            else {
                                gridError+=delta;caseGrid+=delta;maxError=std::max(maxError,delta);
                                if(delta>caseMax) {
                                    caseMax=delta;
                                    std::printf("REPLAY triangle-filter-peak test=%u x=%u y=%u actual=%04x expected=%04x steps=%u\n",test,x,y,actual,expected,delta);
                                }
                            }
                        }
                    }
                }
                if(control && pass)for(unsigned i=0;i<320*240;i++) {
                    unsigned value=get16(target+2*i);
                    // Compare cold and hot separately; translucent draws can
                    // accumulate even on an unchanged capability path.
                    if(pass==1)guarded[hot][i]=value;
                    if(pass==2)errors+=value!=guarded[hot][i];
                }
            }
#ifdef DC_RASTER_PVR
            if(pass) {
                scenes+=PVRRaster::completedScenes();
                unsigned baked=PVRRaster::filterGridCount();
                if(pass==2)grids+=baked;
                if(control || pass==1)errors+=baked!=0;
#if defined(DC_RASTER_FILTER_GRID) && !defined(DC_RASTER_STRICT)
                else errors+=baked!=1 || PVRRaster::textureMisses()!=1;
#endif
            }
#endif
        }
        if(control)++guards;else ++cases;
        std::printf("REPLAY triangle-filter-case %u baseline-error=%u corrected-error=%u max-channel-error=%u\n",test,caseBase,caseGrid,caseMax);
    }
#if defined(DC_RASTER_FILTER_GRID) && !defined(DC_RASTER_STRICT)
    errors+=maxError>5 || gridError*2>baseError || grids!=8 || scenes!=48;
#endif
    std::printf("REPLAY triangle-filter cases=%u guards=%u baseline-error=%u corrected-error=%u max-channel-error=%u mismatches=%u %s\n",
        cases,guards,baseError,gridError,maxError,errors,errors?"FAIL":"PASS");
#ifdef DC_RASTER_PVR
    std::printf("REPLAY triangle-filter grids=%u completed-scenes=%u\n",grids,scenes);
#endif
    failures+=errors!=0;
}
static void shaded_precision(GFX_INFO info) {
    unsigned errors=0,totalError=0,maxError=0,scenes=0,cases=0;
    for(unsigned mode=0;mode<4;mode++)for(unsigned n=0;n<32;n++) {
        unsigned reference[8];
        for(unsigned pass=0;pass<2;pass++) {
            unsigned width=pass?320:16;
            RDP r(info);r.setCImg(0,2,width,ram+target);r.setScissor(0,0,320,240,0);
            for(unsigned i=0;i<320*240;i++)put16(target+2*i,0x0841);
            unsigned source=(n<<11)|(((n*7)&31)<<6)|(((n*13)&31)<<1)|1;
            for(unsigned i=0;i<4;i++)put16(0x100+2*i,source&~(mode==3 && i==3?1u:0u));
            r.setTImg(0,2,2,ram+0x100);
            r.setTile(0,2,1,0,0,0,2,0,0,2,0,0);r.setTileSize(0,0,1,1,0);r.loadTile(0,0,0,1,1);
            r.setOtherMode_h(20,1);r.setOtherMode_h(12,0);
            r.setOtherMode_l(3,mode>=2?0x0c1841c8u>>3:((3u<<26)|(2u<<18)|0x2000)>>3);
            r.setPrimColor(mode==2?0x282828b4:mode==1?0x80808040:0xd2d2d2ff,0,0);
            r.setCombineMode(mode==2?0x004093ff:mode==1?0x00457fff:0x00127fff,
                mode==3?0x3ffe7238:mode==2?0xff0dfe3f:mode==1?0x3ffcfe3f:0x3ffe7e3f);
            Vektor<float,4> a,b,c;a[0]=0;a[1]=0;b[0]=16;b[1]=0;c[0]=0;c[1]=16;
            // Equal RGB removes interpolation/edge differences. Different
            // unused shade alpha forces each separable varying-shade equation.
            Color32 ca(192,224,248,17),cb(192,224,248,128),cc(192,224,248,240);
            for(unsigned hot=0;hot<2;hot++) {
                r.tri_shade_txtr_zbuff(a,b,c,ca,cb,cc,0,0,0,0,0,0,0,1,1,1,0,0,0);flush();
                for(unsigned k=0;k<4;k++) {
                    unsigned x=2+(k&1)*2,y=2+(k>>1)*2;
                    unsigned actual=get16(target+2*(y*width+x));
                    if(!pass)reference[hot*4+k]=actual;
                    else {
                        unsigned expected=reference[hot*4+k];errors+=(actual^expected)&1;
                        for(unsigned shift:{11u,6u,1u}) {
                            int delta=int((actual>>shift)&31)-int((expected>>shift)&31);
                            unsigned error=delta<0?-delta:delta;totalError+=error;
                            maxError=std::max(maxError,error);
                            unsigned limit=2;
#if !defined(DC_RASTER_PVR) || defined(DC_RASTER_STRICT)
                            limit=0;
#elif defined(DC_RASTER_SHADE_SCALE)
                            limit=1;
#endif
                            errors+=error>limit;
                        }
                    }
                }
            }
#ifdef DC_RASTER_PVR
            if(pass) {
                unsigned completed=PVRRaster::completedScenes();scenes+=completed;
#if defined(DC_RASTER_SHADE) && !defined(DC_RASTER_STRICT)
                if(mode<3
#ifdef DC_RASTER_CUTOUT
                   || mode==3
#endif
                )errors+=completed!=2;
                else errors+=completed!=0;
#else
                errors+=completed!=0;
#endif
            }
#endif
        }
        ++cases;
    }
    std::printf("REPLAY shaded-precision cases=%u channel-error=%u max-channel-error=%u mismatches=%u %s\n",
        cases,totalError,maxError,errors,errors?"FAIL":"PASS");
    failures+=errors!=0;
#ifdef DC_RASTER_PVR
    std::printf("REPLAY shaded-precision completed-scenes=%u\n",scenes);
#endif
}
static void cutout_shade(GFX_INFO info) {
    unsigned errors=0,alphaErrors=0,maxError=0,scenes=0;
    unsigned reference[32*16];
    for(unsigned test=0;test<5;test++) {
        auto draw=[&](unsigned width) {
            RDP r(info);r.setCImg(0,2,width,ram+target);r.setScissor(0,0,320,240,0);
            if(test==3) {
                for(unsigned i=0;i<4;i++)ram[(0x100+i)^3]=0x88; // IA8 fractional alpha must fall back.
                r.setTImg(3,1,2,ram+0x100);r.setTile(3,1,1,0,0,0,2,0,0,2,0,0);
            } else {
                for(unsigned i=0;i<4;i++)put16(0x100+2*i,0x8410|((test==0 || (i&1))?1:0));
                r.setTImg(0,2,2,ram+0x100);r.setTile(0,2,1,0,0,0,2,0,0,2,0,0);
            }
            r.setTileSize(0,0,1,1,0);r.loadTile(0,0,0,1,1);
            r.setOtherMode_h(20,1);r.setOtherMode_h(12,test>=2?2:0);
            r.setOtherMode_l(3,0x0c1841c8u>>3);r.setPrimColor(0xd2d2d2ff,0,0);
            r.setCombineMode(0x00127fff,0x3ffe7238);
            Vektor<float,4> a,b,c;a[0]=0;a[1]=0;b[0]=14;b[1]=16;c[0]=0;c[1]=32;
            Color32 ca(32,64,96,17),cb(192,208,224,128),cc(64,128,192,240);
            for(unsigned pass=0;pass<2;pass++) {
                r.setPrimColor(test==4?0x808080ff:pass?0xd8d8d8ff:0xd2d2d2ff,0,0);
                for(unsigned i=0;i<320*240;i++)put16(target+2*i,0x0841);
                r.tri_shade_txtr_zbuff(a,b,c,ca,cb,cc,0,0,1,0,0,1,0,1,0.75f,1.25f,0,0,0);
                flush();
                if(width==16)
                    for(unsigned i=0;i<32*16;i++)reference[i]=get16(target+2*i);
            }
#ifdef DC_RASTER_PVR
            if(width==320) {
                unsigned completed=PVRRaster::completedScenes();scenes+=completed;
#if defined(DC_RASTER_SHADE) && defined(DC_RASTER_CUTOUT) && !defined(DC_RASTER_STRICT)
                if((test<3 && (completed!=2 || PVRRaster::textureMisses()!=1)) || (test>=3 && completed))++errors;
#else
                if(completed)++errors;
#endif
            }
#endif
        };
        draw(16);draw(320);
        unsigned caseMax=0;
        for(unsigned y: {8u,12u,20u,24u})for(unsigned x: {2u,4u}) {
            unsigned expected=reference[y*16+x],actual=get16(target+2*(y*320+x));
            alphaErrors+=((expected^actual)&1)!=0;
            for(unsigned shift: {11u,6u,1u}) {
                unsigned a=(actual>>shift)&31,b=(expected>>shift)&31,error=a>b?a-b:b-a;
                caseMax=std::max(caseMax,error);
            }
        }
        maxError=std::max(maxError,caseMax);
        if(caseMax>2 || (test>=3 && caseMax))++errors;
        std::printf("REPLAY cutout-case %u max-channel-error=%u\n",test,caseMax);
    }
    errors+=alphaErrors;
    std::printf("REPLAY cutout-shade max-channel-error=%u alpha-errors=%u eligibility-errors=%u %s\n",
        maxError,alphaErrors,errors,errors?"FAIL":"PASS");
#if defined(DC_RASTER_PVR) && defined(DC_RASTER_SHADE) && defined(DC_RASTER_CUTOUT) && !defined(DC_RASTER_STRICT)
    std::printf("REPLAY cutout-shade completed-scenes=%u\n",scenes);
#endif
    failures+=(errors!=0);
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
static void fill_texture_transition(GFX_INFO info) {
    RDP r(info);
    r.setCImg(0,2,320,ram+target);
    r.setScissor(0,0,320,240,0);
    for(unsigned i=0;i<320*240;i++)put16(target+i*2,1);
    prim(r);
    r.fillRect(0,0,4,4);
    for(unsigned i=0;i<64;i++)put16(0x100+i*2,0xffff);
    r.setTImg(0,2,8,ram+0x100);
    r.setTile(0,2,2,0,0,0,2,0,0,2,0,0);
    r.setTileSize(0,0,7,7,0);
    r.loadTile(0,0,0,7,7);
    r.setCombineMode((15u<<5)|31u,
        (15u<<24)|(7u<<21)|(7u<<18)|(1u<<6)|(7u<<3)|1u);
    r.texRect(0,8,0,16,8,0,0,1,1);
    prim(r);
    r.fillRect(20,0,24,4);
    flush();
    unsigned errors=0;
    for(unsigned y=0;y<240;y++)for(unsigned x=0;x<320;x++) {
        unsigned expected=1;
        if(y<4 && (x<4 || (x>=20 && x<24)))expected=0xf801;
        if(y<8 && x>=8 && x<16)expected=0xffff;
        errors+=(get16(target+2*(y*320+x))!=expected);
    }
    // A pending sparse batch cannot imply alpha=1 in an untouched region.
    // Zero-alpha blending must still apply the software destination-alpha rule.
    for(unsigned i=0;i<320*240;i++)put16(target+i*2,0);
    prim(r);
    r.fillRect(0,0,4,4);
    r.setOtherMode_l(3,((1u<<22)|(1u<<14))>>3);
    r.setPrimColor(0,0,0);
    r.fillRect(20,0,24,4);
    flush();
    errors+=(get16(target)!=0xf801 || get16(target+2*20)!=1);
    std::printf("REPLAY fill-texture-transition mismatches=%u %s\n",errors,errors?"FAIL":"PASS");
    failures+=(errors!=0);
#if defined(DC_RASTER_PVR) && !defined(DC_RASTER_STRICT)
    require_gpu("fill-texture-transition",5);
#else
    require_gpu("fill-texture-transition",3);
#endif
}
static void texture_precision(GFX_INFO info) {
    RDP r(info);
    r.setCImg(0,2,320,ram+target);
    r.setScissor(0,0,320,240,0);
    r.setOtherMode_h(20,0);
    // Standard source-alpha blending over black, with point sampling.
    r.setOtherMode_l(3,((1u<<22)|(1u<<14))>>3);
    r.setTImg(0,2,8,ram+0x100);
    r.setTile(0,2,2,0,0,0,2,0,0,2,0,0);
    r.setTileSize(0,0,7,7,0);
#ifdef DC_REPLAY_KOS
    vid_set_dithering(false);
#endif
    unsigned errors=0,channelError=0,maxChannelError=0,transparentErrors=0,fractionalErrors=0;
    // All 32 channel values must survive a cutout's opaque region. Check cold
    // and hot cache draws, and a transparent region against the black backdrop.
    // A final RGBA16 source with primitive alpha checks post-combiner format
    // selection: alpha 136 must remain translucent rather than become binary.
    for(unsigned n=0;n<33;n++) {
        unsigned value=n<32?n:31;
        unsigned texel=(value<<11)|(value<<6)|(value<<1)|1;
        for(unsigned i=0;i<64;i++)put16(0x100+i*2,(n<32 && i<16)?texel&~1u:texel);
        r.loadTile(0,0,0,7,7);
        r.setPrimColor(136,0,0);
        r.setCombineMode((15u<<5)|31u,
            (15u<<24)|(7u<<21)|(7u<<18)|(1u<<6)|(7u<<3)|(n<32?1u:3u));
        for(unsigned pass=0;pass<2;pass++) {
            for(unsigned i=0;i<320*240;i++)put16(target+i*2,1);
            r.texRect(0,0,0,8,8,0,0,1,1);
            flush();
            unsigned channel=n<32?value:17;
            unsigned expected=(channel<<11)|(channel<<6)|(channel<<1)|1;
            unsigned actual=get16(target+2*(4*320+4));
            unsigned transparent=get16(target+2*4);
            if(n<32) {
                transparentErrors+=(transparent!=1);
                for(unsigned shift=1;shift<=11;shift+=5) {
                    unsigned got=(actual>>shift)&31;
                    unsigned delta=got>channel?got-channel:channel-got;
                    channelError+=delta;
                    if(delta>maxChannelError)maxChannelError=delta;
                }
            } else fractionalErrors+=(actual!=expected);
            bool bad=actual!=expected || (n<32 && transparent!=1);
            if(bad && errors<4)std::printf("REPLAY texture-precision n=%u pass=%u expected=%04x actual=%04x transparent=%04x\n",n,pass,expected,actual,transparent);
            errors+=bad;
        }
    }
#if defined(DC_RASTER_PVR) && !defined(DC_RASTER_STRICT)
    require_gpu("texture-precision",66);
    vid_set_dithering(true);
#endif
    std::printf("REPLAY texture-precision mismatches=%u %s\n",errors,errors?"FAIL":"PASS");
    std::printf("REPLAY texture-quality channel-error=%u max-channel-error=%u transparent-errors=%u fractional-errors=%u\n",
        channelError,maxChannelError,transparentErrors,fractionalErrors);
    failures+=(errors!=0);
}
static unsigned checksum(unsigned address,unsigned words) {
    unsigned hash=2166136261u;
    for(unsigned i=0;i<words;i++)hash=(hash^get16(address+i*2))*16777619u;
    return hash;
}
static unsigned long long micros() {
#ifdef DC_REPLAY_KOS
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
static void second_texture(GFX_INFO info) {
    unsigned errors=0;
    for(unsigned n=0;n<64;n++) {
        RDP r(info);
        // A narrow target forces software in both GPU modes. Tiles 7 and 0
        // deliberately differ, including alpha, and exercise tile wrapping.
        r.setCImg(0,2,16,ram+target);r.setZImg(ram+0x80000);
        for(unsigned i=0;i<16*16;i++) {
            put16(target+2*i,initial(i%16,i/16));
            put16(0x80000+2*i,n&16?0:0xffff);
        }
        for(unsigned i=0;i<8*8;i++)put16(0x100+2*i,initial(i%8,i/8)|1);
        for(unsigned i=0;i<8*8;i++)put16(0x300+2*i,initial(i%8+11,i/8+7));
        for(unsigned tile: {7u,0u}) {
            r.setTile(0,2,2,tile?0:32,tile,0,2,0,0,2,0,0);
            r.setTileSize(0,0,7,7,tile);
            r.setTImg(0,2,8,ram+(tile?0x100:0x300));
            r.loadTile(tile,0,0,7,7);
        }
        r.setScissor(n&8?2.5f:0,1.25f,10.5f,n&32?6.5f:15,0);
        r.setOtherMode_h(20,1);r.setOtherMode_h(12,n&4?2:0);
        unsigned blend=0x30;
        if(n&32)blend|=(3u<<26)|(2u<<18); // Identity first blender stage.
        r.setOtherMode_l(3,blend>>3);r.setBlendColor(128);
        r.setOtherMode_l(0,n&16?1:0);
        unsigned hi=(15u<<20)|(31u<<15)|(7u<<12)|(7u<<9)|(15u<<5)|31u;
        unsigned lo=(15u<<28)|(15u<<24)|(7u<<21)|(7u<<18)|(1u<<15)|(7u<<12)|(1u<<9)|(7u<<3);
        switch(n%4) {
        case 1:lo=(lo&~(7u<<15))|(2u<<15);break; // TEXEL1 RGB
        case 2:lo=(lo&~(7u<<9))|(2u<<9);break; // TEXEL1 alpha
        case 3:hi=(hi&~(31u<<15))|(9u<<15); // TEXEL1 alpha as RGB factor
               lo=(lo&~((15u<<28)|(7u<<15)))|(15u<<28)|(7u<<15);break;
        }
        r.setCombineMode(hi,lo);
        if(RasterReplayProbe::needsSecondTexel(r)!=(n%4!=0))++errors;
        if(n%8<4)r.texRect(7,-1,0.5f,14,12.5f,0.25f,0.75f,0.5f,0.375f,n&1);
        else {
            Vektor<float,4> a,b,c;
            a[0]=-2;a[1]=0;b[0]=14;b[1]=5;c[0]=2;c[1]=14;
            Color32 shade(255,255,255,255);
            r.tri_shade_txtr_zbuff(a,b,c,shade,shade,shade,0.25f,0.75f,7,1,1,7,7,
                1,0.75f,1.25f,100,100,100);
        }
        flush();
        unsigned before=RasterReplayProbe::state(r);
        // A later draw can read the final TEXEL1 register even when the
        // preceding two-cycle shader did not use it.
        r.setOtherMode_h(20,0);r.setOtherMode_l(3,0);r.setOtherMode_l(0,0);
        r.setCombineMode((15u<<5)|31u,
            (15u<<24)|(7u<<21)|(7u<<18)|(2u<<6)|(7u<<3)|2u);
        r.fillRect(3,3,5,5);flush();
        std::printf("REPLAY pair-case %u color=%08x depth=%08x before=%08x state=%08x\n",n,
            checksum(target,256),checksum(0x80000,256),before,RasterReplayProbe::state(r));
    }
    std::printf("REPLAY second-texture mismatches=%u %s\n",errors,errors?"FAIL":"PASS");
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
// Adjacent PVR cache hits must stop using their content proof on every TX
// mutation, without confusing content identity with sample-generation numbers.
static void texture_mutations(GFX_INFO info) {
    unsigned errors=0,cases=0;
    RDP r(info);r.setCImg(0,2,320,ram+target);r.setScissor(0,0,320,240,0);
    for(unsigned i=0;i<320*240;i++)put16(target+2*i,1);
    auto setup=[&](RDP &draw) {
        draw.setCImg(0,2,320,ram+target);draw.setScissor(0,0,320,240,0);
        draw.setOtherMode_h(20,0);draw.setOtherMode_h(12,0);draw.setOtherMode_l(3,0);
        draw.setCombineMode((15u<<5)|31u,
            (15u<<24)|(7u<<21)|(7u<<18)|(1u<<6)|(7u<<3)|1u);
        draw.setTile(0,2,1,0,0,0,2,0,0,2,0,0);draw.setTileSize(0,0,1,1,0);
        draw.setTImg(0,2,2,ram+0x100);
    };
    auto source=[&](unsigned color) {for(unsigned i=0;i<4;i++)put16(0x100+2*i,color);};
    auto draw=[&](RDP &renderer,unsigned expected) {
        renderer.texRect(0,16,16,24,24,0,0,0.25f,0.25f);flush();
        unsigned actual=get16(target+2*(17*320+17));
        errors+=actual!=expected;
        std::printf("REPLAY mutation-case %u actual=%04x expected=%04x\n",cases++,actual,expected);
    };
    setup(r);source(0xf801);r.loadTile(0,0,0,1,1);
    draw(r,0xf801);draw(r,0xf801); // Cold then unchanged adjacent hits.
    source(0x07c1);r.loadTile(0,0,0,1,1);draw(r,0x07c1);
    source(0x003f);r.loadBlock(0,0,0,3,0);draw(r,0x003f);
    RasterReplayProbe::wrapEpoch(r);
    source(0xf801);r.loadTile(0,0,0,1,1);draw(r,0xf801);
    // Shader inputs change without a TX write: small key comparisons remain.
    r.setCombineMode((15u<<5)|31u,
        (15u<<24)|(7u<<21)|(7u<<18)|(3u<<6)|(7u<<3)|3u);
    r.setPrimColor(0x0000ffff,0,0);draw(r,0x003f);
    r.setPrimColor(0x00ff00ff,0,0);draw(r,0x07c1);
    setup(r);r.loadTile(0,0,0,1,1);draw(r,0xf801);
    // Independent TX objects can share baked cache entries. Their subsequent
    // writes must invalidate only a proof associated with the live source.
    {
        RDP other(info);setup(other);source(0x07c1);other.loadTile(0,0,0,1,1);
        draw(other,0x07c1);draw(r,0xf801);
    } // Reset the global cache; r still owns valid TMEM.
    draw(r,0xf801);
    // Palette-only writes, tile descriptors and LUT changes are mutations too.
    r.setOtherMode_h(14,2);r.setTile(2,1,1,0,0,0,2,0,0,2,0,0);
    r.setTileSize(0,0,1,1,0);r.setTImg(2,1,2,ram+0x100);
    for(unsigned i=0;i<4;i++)ram[(0x100+i)^3]=1;
    r.loadTile(0,0,0,1,1);
    r.setTile(0,2,1,256,7,0,2,0,0,2,0,0);r.setTImg(0,2,2,ram+0x200);
    put16(0x200,1);put16(0x202,0xf801);r.loadTLUT(7,2);draw(r,0xf801);
    put16(0x202,0x003f);r.loadTLUT(7,2);draw(r,0x003f);draw(r,0x003f);
    // The same texture bytes interpreted through IA16 must not hit RGBA16.
    put16(0x202,0xffff);r.loadTLUT(7,2);r.setOtherMode_h(14,3);draw(r,0xffff);
    std::printf("REPLAY texture-mutations mismatches=%u cases=%u %s\n",errors,cases,errors?"FAIL":"PASS");
    failures+=(errors!=0);
#if defined(DC_RASTER_PVR) && !defined(DC_RASTER_STRICT)
    // The other RDP's destructor resets scene counts halfway through the test.
    require_gpu("texture-mutations",5);
#endif
}

static void texture_hash_collision(GFX_INFO info) {
    RDP r(info);r.setCImg(0,2,320,ram+target);r.setScissor(0,0,320,240,0);
    r.setTile(0,2,1,0,0,0,2,0,0,2,0,0);r.setTileSize(0,0,1,1,0);
    r.setOtherMode_h(20,0);r.setOtherMode_h(12,0);r.setOtherMode_l(3,0);
    r.setCombineMode((15u<<5)|31u,
        (15u<<24)|(7u<<21)|(7u<<18)|(1u<<6)|(7u<<3)|1u);
    unsigned errors=0,firstHash=0;
    for(unsigned pass=0;pass<4;pass++) {
        bool blue=pass&1;
        unsigned hash=RasterReplayProbe::collisionTexture(r,blue);
        if(!pass)firstHash=hash;else errors+=hash!=firstHash;
        r.texRect(0,16,16,24,24,0,0,0,0);flush();
        unsigned actual=get16(target+2*(16*320+16)),expected=blue?0x003f:0xf801;
        errors+=actual!=expected;
        std::printf("REPLAY collision-case %u hash=%08x actual=%04x expected=%04x\n",pass,hash,actual,expected);
#if defined(DC_RASTER_PVR) && !defined(DC_RASTER_STRICT)
        errors+=PVRRaster::textureMisses()!=(pass?2u:1u);
#endif
    }
    std::printf("REPLAY texture-hash-collision mismatches=%u content-hash=%08x %s\n",
        errors,firstHash,errors?"FAIL":"PASS");
    failures+=(errors!=0);
#if defined(DC_RASTER_PVR) && !defined(DC_RASTER_STRICT)
    require_gpu("texture-hash-collision",4);
#endif
}
static void texture_dependencies(GFX_INFO info) {
    unsigned errors=0,cases=0;
    for(unsigned test=0;test<4;test++) {
        RDP r(info);r.setCImg(0,2,320,ram+target);r.setScissor(0,0,320,240,0);
        for(unsigned i=0;i<4;i++)put16(0x100+2*i,0xf801);
        r.setTImg(0,2,2,ram+0x100);r.setTile(0,2,1,0,0,0,2,0,0,2,0,0);
        r.setTileSize(0,0,1,1,0);r.loadTile(0,0,0,1,1);r.setOtherMode_h(12,0);
        r.setOtherMode_h(20,test==3?1:0);
        r.setOtherMode_l(3,test==3?((3u<<26)|(2u<<18)|0x2000)>>3:0);
        unsigned direct=(15u<<24)|(7u<<21)|(7u<<18)|(7u<<3);
        if(test==0)r.setCombineMode((15u<<5)|31u,direct|(1u<<6)|1u);
        if(test==1)r.setCombineMode((15u<<5)|31u,direct|(3u<<6)|6u);
        if(test==2)r.setCombineMode((1u<<5)|10u,direct|(7u<<6)|6u);
        if(test==3)r.setCombineMode((15u<<20)|(31u<<15)|(7u<<12)|(7u<<9)|(5u<<5)|7u,
            (15u<<28)|direct|(1u<<15)|(7u<<12)|(3u<<9)|(3u<<6)|6u);
        r.setPrimColor(0x0000ffff,0,0);r.setEnvColor(0x00ff00ff);
        auto draw=[&](unsigned expected) {
            for(unsigned i=0;i<320*240;i++)put16(target+2*i,1);
            r.texRect(0,16,16,24,24,0,0,0.25f,0.25f);flush();
            unsigned actual=get16(target+2*(17*320+17));errors+=actual!=expected;
            std::printf("REPLAY dependency-case %u actual=%04x expected=%04x\n",cases++,actual,expected);
        };
        unsigned first=test==0 || test==2?0xf801:test==1?0x003f:0x07ff;
        draw(first);
#if defined(DC_RASTER_PVR) && !defined(DC_RASTER_STRICT)
        unsigned cold=PVRRaster::textureMisses();errors+=cold!=1;
#endif
        // Unused constants, and RGB channels of an alpha-only primitive input.
        r.setEnvColor(test==3?0x00ff00ff:0xffffffff);
        r.setPrimColor(test==0 || test==2?0x00ff00ff:0x0000ffff,0.5f,0.75f);
        RasterReplayProbe::shade(r,Color32(12,34,56,78));draw(first);
#if defined(DC_RASTER_PVR) && !defined(DC_RASTER_STRICT)
        errors+=PVRRaster::textureMisses()!=cold;
#endif
        if(test==0) { // Still unused: this alpha also must not create a texture.
            r.setPrimColor(0xabcdef00,0,0);draw(first);
#if defined(DC_RASTER_PVR) && !defined(DC_RASTER_STRICT)
            errors+=PVRRaster::textureMisses()!=cold;
#endif
        } else {
            r.setPrimColor(test==1?0x00ff00ff:0x0000ff00,0,0);
            draw(test==1?0x07c1:test==2?1:0x003f);
#if defined(DC_RASTER_PVR) && !defined(DC_RASTER_STRICT)
            errors+=PVRRaster::textureMisses()!=cold+1;
#endif
        }
    }
    std::printf("REPLAY texture-dependencies mismatches=%u cases=%u %s\n",errors,cases,errors?"FAIL":"PASS");
    failures+=(errors!=0);
}
static void texture_batches(GFX_INFO info) {
    unsigned errors=0,scenes=0;
    for(unsigned kind=0;kind<2;kind++) {
        RDP r(info);r.setCImg(0,2,320,ram+target);r.setScissor(0,0,320,240,0);
        r.setOtherMode_h(20,0);r.setOtherMode_h(12,0);r.setOtherMode_l(3,0);
        r.setCombineMode((15u<<5)|31u,
            (15u<<24)|(7u<<21)|(7u<<18)|(1u<<6)|(7u<<3)|1u);
        for(unsigned i=0;i<320*240;i++)put16(target+2*i,1);
        for(unsigned n=0;n<300;n++) {
            unsigned color=(n&1?0xf800:0)|(n&2?0x07c0:0)|(n&4?0x003e:0)|1;
            for(unsigned i=0;i<4;i++)put16(0x100+2*i,color);
            r.setTImg(0,2,2,ram+0x100);r.setTile(0,2,1,n,0,0,2,0,0,2,0,0);
            r.setTileSize(0,0,1,1,0);r.loadTile(0,0,0,1,1);
            unsigned x=kind?(n%150)*2:n,y=kind?100+(n/150)*5:40;
            if(!kind)r.texRect(0,x,y,x+1,y+1,0,0,0,0);
            else {
                Vektor<float,4> a,b,c;
                a[0]=x-0.5f;a[1]=y-0.5f;b[0]=x+2.5f;b[1]=y-0.5f;c[0]=x-0.5f;c[1]=y+2.5f;
                Color32 white(255,255,255,255);
                r.tri_shade_txtr_zbuff(a,b,c,white,white,white,0,0,0,0,0,0,0,1,1,1,0,0,0);
            }
        }
        flush();
        for(unsigned n=0;n<300;n++) {
            unsigned expected=(n&1?0xf800:0)|(n&2?0x07c0:0)|(n&4?0x003e:0)|1;
            unsigned x=kind?(n%150)*2:n,y=kind?100+(n/150)*5:40;
            errors+=get16(target+2*(y*320+x))!=expected;
        }
#if defined(DC_RASTER_PVR) && !defined(DC_RASTER_STRICT)
        unsigned completed=PVRRaster::completedScenes();scenes+=completed;
#ifdef DC_RASTER_BATCH_CACHE
        // Rectangles hit the 400-primitive limit before exhausting 256 slots.
        // Triangles exhaust the slots first and must preserve pinned textures.
        errors+=completed!=2 || PVRRaster::textureMisses()!=300;
        errors+=PVRRaster::pinnedFlushCount()!=(kind?1u:0u);
#endif
#endif
    }
    std::printf("REPLAY texture-batches mismatches=%u %s\n",errors,errors?"FAIL":"PASS");
#if defined(DC_RASTER_PVR) && !defined(DC_RASTER_STRICT)
    std::printf("REPLAY texture-batches completed-scenes=%u\n",scenes);
#endif
    failures+=(errors!=0);
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
static void framebuffer_import() {
    alignas(32) uint16_t src[130],dst[162];
    unsigned errors=0;
    // Every RGBA16 value, paired with an independent permutation, in both
    // aligned and halfword-aligned inputs/outputs. Check row padding as well.
    for(unsigned sourceOffset=0;sourceOffset<2;++sourceOffset)
      for(unsigned destOffset=0;destOffset<2;++destOffset)
        for(unsigned base=0;base<65536;base+=64) {
            for(unsigned i=0;i<64;++i) {
                unsigned first=base+i,second=(first*40503u+17049u)&65535u;
                src[sourceOffset+(2*i^S16)]=first;
                src[sourceOffset+((2*i+1)^S16)]=second;
            }
            for(auto &v:dst)v=0xdead;
            dc_raster_import_rows(src+sourceOffset,dst+destOffset,64,2,80);
            for(unsigned y=0;y<2;++y)for(unsigned x=0;x<80;++x) {
                unsigned expected=0xdead;
                if(x<64) {
                    unsigned index=y*64+x,raw=base+index/2;
                    if(index&1)raw=(raw*40503u+17049u)&65535u;
                    unsigned r=(raw>>11)&31,g=(raw>>6)&31,b=(raw>>1)&31;
                    expected=(r<<11)|((2*g+(g>>4))<<5)|b;
                }
                errors+=dst[destOffset+y*80+x]!=expected;
            }
            if(destOffset)errors+=dst[0]!=0xdead;
            errors+=dst[destOffset+160]!=0xdead;
        }
    std::printf("REPLAY framebuffer-import mismatches=%u %s\n",errors,errors?"FAIL":"PASS");
    failures+=(errors!=0);
}
#ifdef DC_RASTER_WIDE
static void wide_native(GFX_INFO info) {
    RDP r(info);const unsigned narrow=0xc0000;
    unsigned errors=0;
    for(unsigned y=0;y<480;y++)for(unsigned x=0;x<640;x++)
        put16(target+2*(y*640+x),initial(x,y));
    for(unsigned y=0;y<240;y++)for(unsigned x=0;x<320;x++)
        put16(narrow+2*(y*320+x),initial(x,y));
    r.setCImg(0,2,320,ram+narrow);r.setScissor(0,0,320,240,0);prim(r);
    r.setPrimColor(0x0000ffff,0,0);r.fillRect(316,236,320,240);
    // Canvas changes must complete the prior RTT before changing its stride.
    r.setCImg(0,2,640,ram+target);r.setScissor(0,0,640,480,0);
    r.setPrimColor(0xff0000ff,0,0);r.fillRect(4,5,12,9);
    r.fillRect(638,478,640,480);
    r.setCImg(0,2,320,ram+narrow);r.setScissor(0,0,320,240,0);
    r.setPrimColor(0x00ff00ff,0,0);r.fillRect(0,0,3,3);flush();
    for(unsigned y=0;y<480;y++)for(unsigned x=0;x<640;x++) {
        bool red=(x>=4 && x<12 && y>=5 && y<9) || (x>=638 && y>=478);
        errors+=get16(target+2*(y*640+x))!=(red?0xf801:initial(x,y));
    }
    for(unsigned y=0;y<240;y++)for(unsigned x=0;x<320;x++) {
        unsigned expected=x<3 && y<3?0x07c1:x>=316 && y>=236?0x003f:initial(x,y);
        errors+=get16(narrow+2*(y*320+x))!=expected;
    }
    // Full native row addressing, preserved backdrop and the right/bottom edge
    // also apply to the general texture import/readback batch.
    for(unsigned y=0;y<480;y++)for(unsigned x=0;x<640;x++)
        put16(target+2*(y*640+x),initial(x,y));
    for(unsigned i=0;i<4;i++)put16(0x100+2*i,0xf801);
    r.setCImg(0,2,640,ram+target);r.setScissor(0,0,640,480,0);
    r.setTImg(0,2,2,ram+0x100);r.setTile(0,2,1,0,0,0,2,0,0,2,0,0);
    r.setTileSize(0,0,1,1,0);r.loadTile(0,0,0,1,1);
    r.setOtherMode_h(20,1);r.setOtherMode_h(12,0);
    r.setOtherMode_l(3,((3u<<26)|(2u<<18))>>3);
    r.setCombineMode((15u<<20)|(31u<<15)|(7u<<12)|(7u<<9)|(15u<<5)|31u,
        (15u<<28)|(15u<<24)|(7u<<21)|(7u<<18)|(1u<<15)|(7u<<12)|(1u<<9)|(7u<<3));
    r.texRect(0,625,462,640,480,0,0,1,1);flush();
    for(unsigned y=0;y<480;y++)for(unsigned x=0;x<640;x++)
        errors+=get16(target+2*(y*640+x))!=(x>=625 && y>=462?0xf801u:initial(x,y));
    // A partial blended fill must sample the imported backdrop. Odd horizontal
    // bounds exercise scalar edges around the packed readback; a following
    // batch at the opposite corner must reset the region rather than reuse it.
    for(unsigned y=0;y<480;y++)for(unsigned x=0;x<640;x++)
        put16(target+2*(y*640+x),x>=33 && x<49 && y>=17 && y<33?0x003fu:initial(x,y));
    prim(r);r.setOtherMode_l(3,((1u<<22)|(1u<<14))>>3);
    r.setPrimColor(0xff000080,0,0);r.fillRect(33,17,49,33);flush();
    for(unsigned y=0;y<480;y++)for(unsigned x=0;x<640;x++) {
        unsigned actual=get16(target+2*(y*640+x));
        if(x>=33 && x<49 && y>=17 && y<33) {
            unsigned red=actual>>11,green=(actual>>6)&31u,blue=(actual>>1)&31u;
            errors+=red<15 || red>17 || green!=0 || blue<14 || blue>16 || !(actual&1);
        } else errors+=actual!=initial(x,y);
    }
    // Preserve the first result exactly while refreshing a different backdrop.
    unsigned prior=get16(target+2*(17*640+33));
    for(unsigned y=0;y<7;y++)for(unsigned x=0;x<5;x++)put16(target+2*(y*640+x),0x003f);
    r.setPrimColor(0x00ff0080,0,0);r.fillRect(0,0,5,7);flush();
    errors+=get16(target+2*(17*640+33))!=prior;
    for(unsigned y=0;y<7;y++)for(unsigned x=0;x<5;x++) {
        unsigned actual=get16(target+2*(y*640+x));
        unsigned green=(actual>>6)&31u,blue=(actual>>1)&31u;
        errors+=(actual>>11)!=0 || green<15 || green>17 || blue<14 || blue>16 || !(actual&1);
    }
    // A pending regional texture draw does not normalize alpha elsewhere.
    // A transparent fill there still has to set destination alpha, like software.
    for(unsigned i=0;i<640*480;i++)put16(target+2*i,0);
    r.setOtherMode_h(20,1);r.setOtherMode_l(3,((3u<<26)|(2u<<18))>>3);
    r.setCombineMode((15u<<20)|(31u<<15)|(7u<<12)|(7u<<9)|(15u<<5)|31u,
        (15u<<28)|(15u<<24)|(7u<<21)|(7u<<18)|(1u<<15)|(7u<<12)|(1u<<9)|(7u<<3));
    r.texRect(0,625,462,640,480,0,0,1,1);
    prim(r);r.setOtherMode_l(3,((1u<<22)|(1u<<14))>>3);
    r.setPrimColor(0,0,0);r.fillRect(0,0,4,4);flush();
    for(unsigned y=0;y<480;y++)for(unsigned x=0;x<640;x++)
        errors+=get16(target+2*(y*640+x))!=(x>=625 && y>=462?0xf801u:x<4 && y<4?1u:0u);
    std::printf("REPLAY wide-native mismatches=%u %s\n",errors,errors?"FAIL":"PASS");
    failures+=(errors!=0);
#ifdef DC_RASTER_STRICT
    require_gpu("wide-native",3);
#else
    require_gpu("wide-native",8);
#endif
}
#endif
int main() {
#ifdef DC_REPLAY_KOS
    vid_set_mode(DM_640x480,PM_RGB565);
#endif
#ifdef DC_RASTER_PVR
    pvr_init_params_t params={};
    params.opb_sizes[0]=PVR_BINSIZE_16;
    params.opb_sizes[2]=PVR_BINSIZE_16;
    params.autosort_disabled=1;
    params.vertex_buf_size=128*1024;
    params.opb_overflow_count=1;
    if(pvr_init(&params)<0)return 2;
#endif
#if defined(DC_RASTER_STRICT)
    std::printf("REPLAY backend=kos-pvr-strict\n");
#elif defined(DC_RASTER_PVR)
    std::printf("REPLAY backend=kos-pvr-fast\n");
#elif defined(DC_REPLAY_KOS)
    std::printf("REPLAY backend=kos-software\n");
#else
    std::printf("REPLAY backend=host-software\n");
#endif
    GFX_INFO info={};info.RDRAM=ram;info.MemoryBswaped=TRUE;
#ifdef DC_RASTER_FILTER_GRID
    std::printf("REPLAY filter-grid-enabled=1\n");
#else
    std::printf("REPLAY filter-grid-enabled=0\n");
#endif
#ifdef DC_RASTER_TRI_ORIGIN
    std::printf("REPLAY tri-origin-enabled=1\n");
#else
    std::printf("REPLAY tri-origin-enabled=0\n");
#endif
#ifdef DC_RASTER_SHADE_SCALE
    std::printf("REPLAY shade-scale-enabled=1\n");
#else
    std::printf("REPLAY shade-scale-enabled=0\n");
#endif
    framebuffer_import();
#ifdef DC_RASTER_WIDE
    wide_native(info);
#endif
    partial_fill(info);
    opaque_ramp(info);
    mixed_transition(info);
    fill_texture_transition(info);
    combined_transition(info);
    unused_tile_gpu(info);
    affine_shade(info);
    triangle_origin(info);
    triangle_filter(info);
    shaded_precision(info);
    cutout_shade(info);
    identity_blender(info);
    full_alpha(info);
    texture_alpha(info);
    coverage_point(info);
    software_identity_span(info);
    software_combine_values(info);
    rectangle_scissor(info);
    rectangle_sample(info);
    texture_precision(info);
    texture_spans(info);
    second_texture(info);
    texture_cache(info);
    texture_mutations(info);
    texture_hash_collision(info);
    texture_dependencies(info);
    texture_batches(info);
    std::printf("REPLAY RESULT failures=%u stop=%d %s\n",failures,stop,
        failures||stop?"FAIL":"PASS");
#ifdef DC_RASTER_PVR
    pvr_shutdown();
#endif
#ifdef DC_REPLAY_KOS
    // Keep serial results available to the target capture tool.
    for(;;)thd_sleep(1000);
#endif
    return failures||stop?1:0;
}
