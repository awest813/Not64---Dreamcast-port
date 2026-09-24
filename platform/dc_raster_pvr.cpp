#include "dc_raster_pvr.h"
#include "dc_memory.h"
#include "../mupen64_soft_gfx/rdp.h"
#include <kos.h>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <cstddef>

extern "C" int stop;
namespace {
constexpr unsigned W=320,H=240,PITCH=512,SLOTS=128,VRAM_BUDGET=2*1024*1024;
struct TextureKey {
    unsigned char tmem[4096];
    unsigned short palette[256];
    Descriptor d[2];
    int mux[2],cycle,lut,filter,alphaCoverage,alphaThreshold;
    float colors[20];
};
struct Texture {
    TextureKey key;
    uint32_t hash,age;
    pvr_ptr_t memory;
    unsigned width,height,bytes,format;
};
Texture cache[SLOTS];
TextureKey key;
unsigned clockAge,usedBytes,draws,hits,misses,submissions;
unsigned lastSlot=SLOTS,transparentFills;
unsigned rejected[8];
uint64_t waitUs,readbackUs,textureUs,importUs;
#ifdef DC_RASTER_PROFILE
struct TextureTimer {
    uint64_t start=timer_us_gettime64();
    ~TextureTimer(){textureUs+=timer_us_gettime64()-start;}
};
#endif
pvr_ptr_t output,initial;
unsigned short *destination;
bool pending;
bool exactFillBatch;
bool failed;
void fail(const char *stage) {
    if(!failed)std::fprintf(stderr,"PVR raster failed: %s\n",stage);
    failed=true;
    stop=1;
}
alignas(32) unsigned short pixels[PITCH*256];
alignas(32) uint32_t colors[256*256];
// Both modes share the qualified opaque one-cycle fill path. Track its
// exact union, so readback cannot modify alpha or RGB outside the written area.
uint32_t written[W*H/32];
static_assert(sizeof(cache)+sizeof(key)+sizeof(pixels)+sizeof(colors)+sizeof(written)+4096<=DC_TEXCACHE_SIZE,
              "PVR raster scratch exceeds the main-RAM budget");
uint32_t hashKey(const TextureKey &k) {
    auto p=reinterpret_cast<const uint32_t*>(&k);
    uint32_t h=2166136261u;
    for(unsigned i=0;i<sizeof(k)/4;i++) h=(h^p[i])*16777619u;
    return h;
}
unsigned dimension(const Descriptor &d,bool vertical) {
    int mode=vertical?d.cmt:d.cms,mask=vertical?d.maskt:d.masks;
    int extent=vertical?d.sampleHeight:d.sampleWidth;
    if(!(mode&2) && mask) extent=(1<<mask)*((mode&1)?2:1);
    if(extent<=0 || extent>256) return 0;
    unsigned n=8; while(n<(unsigned)extent)n*=2;
    if(!(mode&2) && !mask && n==(unsigned)extent && n<256)n*=2;
    // Unclamped coordinates must repeat at the native power-of-two period.
    if(!(mode&2) && mask && n!=(unsigned)extent)return 0;
    return n;
}
float shiftScale(int shift) {
    return shift<=10 ? 1.0f/(1<<shift) : (float)(1<<(16-shift));
}
void header(Texture *texture,bool blend,bool linear,int clamp) {
    pvr_poly_cxt_t c;
    if(texture) {
        pvr_poly_cxt_txr(&c,PVR_LIST_TR_POLY,
            texture->format|PVR_TXRFMT_NONTWIDDLED,
            texture->width,texture->height,texture->memory,
            linear?PVR_FILTER_BILINEAR:PVR_FILTER_NONE);
        c.txr.env=PVR_TXRENV_MODULATEALPHA;
        c.txr.uv_clamp=(pvr_uv_clamp_t)clamp;
    } else pvr_poly_cxt_col(&c,PVR_LIST_TR_POLY);
    c.gen.culling=PVR_CULLING_NONE;
    c.gen.alpha=true;
    c.depth.comparison=PVR_DEPTHCMP_ALWAYS;
    c.depth.write=PVR_DEPTHWRITE_DISABLE;
    c.blend.src=blend?PVR_BLEND_SRCALPHA:PVR_BLEND_ONE;
    c.blend.dst=blend?PVR_BLEND_INVSRCALPHA:PVR_BLEND_ZERO;
    alignas(32) pvr_poly_hdr_t h; pvr_poly_compile(&h,&c);
    if(pvr_prim(&h,sizeof(h))<0)fail("polygon header");
}
void vertex(float x,float y,float z,float u,float v,uint32_t argb,bool end) {
    if(failed)return;
    alignas(32) pvr_vertex_t p={}; p.flags=end?PVR_CMD_VERTEX_EOL:PVR_CMD_VERTEX;
    p.x=x;p.y=y;p.z=z;p.u=u;p.v=v;p.argb=argb;
    if(pvr_prim(&p,sizeof(p))<0)fail("vertex");
}
void quad(float x0,float y0,float x1,float y1,uint32_t color) {
    vertex(x0,y0,1,0,0,color,false);vertex(x1,y0,1,0,0,color,false);
    vertex(x0,y1,1,0,0,color,false);vertex(x1,y1,1,0,0,color,true);
}
uint32_t argb(Color32 c) {
    uint32_t v=(uint32_t)(int)c;return (v>>8)|(v<<24);
}
}

bool PVRRaster::eligible(RDP *r,bool &blend,bool alphaTestBaked) {
    BL &b=*r->bl;
    bool alphaTest=(b.alphaCompare&1) && ((b.alphaCompare&2) || b.blendColor.getAlpha()>0);
    if(b.format || b.size!=2 || b.width!=(int)W || b.z_cmp || b.z_upd ||
       (alphaTest && !alphaTestBaked) || b.cvg_x_alpha || b.cImg==b.zImg || !b.validPixel(b.cImg,W-1,H-1,2))return false;
    if(r->cycleType==0) {
        if(!b.force_bl && b.psa1==&b.pixelColor) {blend=false;return true;}
        if(b.force_bl && b.psa1==&b.pixelColor && b.psb1==&b.memoryColor &&
           b.pca1==&b.pixelColor && b.pcb1==&b.invertedAlpha) {blend=true;return true;}
    }
    // Two identity blender stages, common for opaque textured backgrounds.
    if(r->cycleType==1 && b.psa1==&b.pixelColor && b.psb1==&b.pixelColor &&
       b.pca1==&b.zero && b.pcb1==&b.one && b.psa2==&b.blendedPixelColor &&
       (!b.force_bl || (b.psb2==&b.blendedPixelColor && b.pca2==&b.zero && b.pcb2==&b.one))) {
        blend=false;return true;
    }
    return false;
}

bool PVRRaster::begin(RDP *r,bool preserve,bool exactFill) {
    if(failed)return false;
    if(pending && (destination!=(unsigned short*)r->bl->cImg || exactFillBatch!=exactFill))flush();
    if(failed)return false;
    if(pending)return true;
    if(!output)output=pvr_mem_malloc(PITCH*256*2);
    if(!initial)initial=pvr_mem_malloc(PITCH*256*2);
    if(!output || !initial)return false;
    if(pvr_wait_ready()<0 || pvr_wait_render_done()<0)return false;
    // N64 channel quantization is explicit below; KOS enables an additional
    // framebuffer dither by default, which would alter those channel values.
    vid_set_dithering(false);
    destination=(unsigned short*)r->bl->cImg;
    if(preserve) {
        uint64_t start=timer_us_gettime64();
        for(unsigned y=0;y<H;y++) for(unsigned x=0;x<W;x++) {
            unsigned v=destination[(y*W+x)^S16];
            pixels[y*PITCH+x]=(v&0xffc0)|((v>>1)&31)|((v>>5)&32);
        }
        pvr_txr_load(pixels,initial,sizeof(pixels));
        importUs+=timer_us_gettime64()-start;
    }
    if(pvr_scene_begin_rtt(output,W,H,PITCH)<0)return false;
    if(pvr_list_begin(PVR_LIST_TR_POLY)<0) {
        pvr_scene_finish();fail("begin list");return false;
    }
    pending=true; draws=0;
    exactFillBatch=exactFill;
    if(exactFill)std::memset(written,0,sizeof(written));
    if(preserve) {
        Texture t={};t.memory=initial;t.width=PITCH;t.height=256;t.format=PVR_TXRFMT_RGB565;
        header(&t,false,false,PVR_UVCLAMP_UV);
        vertex(0,0,1,0,0,0xffffffff,false);vertex(W,0,1,(float)W/PITCH,0,0xffffffff,false);
        vertex(0,H,1,0,(float)H/256,0xffffffff,false);
        vertex(W,H,1,(float)W/PITCH,(float)H/256,0xffffffff,true);
    }
    return true;
}

void PVRRaster::flush() {
    if(!pending)return;
    uint64_t start=timer_us_gettime64();
    if(pvr_list_finish()<0)fail("finish list");
    if(pvr_scene_finish()<0)fail("finish scene");
    if(pvr_wait_ready()<0)fail("wait TA");
    if(pvr_wait_render_done()<0)fail("wait render");
    waitUs+=timer_us_gettime64()-start;
    if(failed){pending=false;return;}
    start=timer_us_gettime64();
    auto src=(volatile unsigned short*)output;
    if(exactFillBatch) {
        // Skip untouched groups without scanning every pixel. W is a multiple
        // of 32, so each mask stays within one source row.
        static_assert(W%32==0,"fill masks must not cross rows");
        for(unsigned block=0;block<W*H/32;block++) {
            unsigned mask=written[block];
            if(!mask)continue;
            unsigned pixel=block*32;
            auto row=src+(pixel/W)*PITCH+(pixel%W);
            for(unsigned bit=0;mask;bit++,mask>>=1)if(mask&1) {
                unsigned v=row[bit];
                destination[(pixel+bit)^S16]=(v&0xffc0)|((v&31)<<1)|1;
            }
        }
    } else {
        auto srcWords=(volatile uint32_t*)output;
        auto dstWords=(uint32_t*)destination;
        if(!((uintptr_t)destination&3)) {
            for(unsigned y=0;y<H;y++)for(unsigned x=0;x<W/2;x++) {
                uint32_t v=srcWords[y*PITCH/2+x];
                uint32_t packed=(v&0xffc0ffc0u)|((v&0x001f001fu)<<1)|0x00010001u;
                dstWords[y*W/2+x]=(packed<<16)|(packed>>16);
            }
        } else {
            for(unsigned y=0;y<H;y++)for(unsigned x=0;x<W;x++) {
                unsigned v=src[y*PITCH+x];
                destination[(y*W+x)^S16]=(v&0xffc0)|((v&31)<<1)|1;
            }
        }
    }
    readbackUs+=timer_us_gettime64()-start;
    pending=false;
    if(++submissions%60==0) {
        printf("PVR raster: submissions=%u hits=%u misses=%u VRAM=%u\n",submissions,hits,misses,usedBytes);
        printf("PVR fallback: state=%u scissor=%u shade=%u size=%u dual=%u rgb=%u alpha=%u alloc=%u\n",
            rejected[0],rejected[1],rejected[2],rejected[3],rejected[4],rejected[5],rejected[6],rejected[7]);
        printf("PVR transparent fills skipped=%u\n",transparentFills);
        printf("PVR costs us: wait=%llu readback=%llu texture=%llu import=%llu\n",waitUs,readbackUs,textureUs,importUs);
    }
}

void PVRRaster::reset() {
    flush();
    // A failed completion wait must not free storage still used by the GPU.
    // Graphics shutdown tears down the PVR allocator after this reset.
    bool idle=!failed || (pvr_wait_ready()>=0 && pvr_wait_render_done()>=0);
    for(auto &t:cache)if(t.memory){if(idle)pvr_mem_free(t.memory);t.memory=nullptr;}
    if(output && idle)pvr_mem_free(output);
    if(initial && idle)pvr_mem_free(initial);
    output=initial=nullptr;destination=nullptr;
    exactFillBatch=false;
    usedBytes=clockAge=hits=misses=submissions=0;lastSlot=SLOTS;transparentFills=0;
    std::memset(rejected,0,sizeof(rejected));
    waitUs=readbackUs=textureUs=importUs=0;
    failed=false;
}

unsigned PVRRaster::completedScenes() { return submissions; }

void PVRRaster::readMemory(const void *source,unsigned bytes) {
    if(!pending || !bytes)return;
    // TMEM loads use word-swapped byte addresses. Include both partial words.
    uintptr_t first=(uintptr_t)source & ~(uintptr_t)3;
    uintptr_t end=((uintptr_t)source+bytes+3) & ~(uintptr_t)3;
    uintptr_t base=(uintptr_t)destination;
    if(first<base+W*H*2 && end>base)flush();
}
int PVRRaster::texture(RDP *r,int tile,Color32 shade,int alphaThreshold) {
#ifdef DC_RASTER_PROFILE
    TextureTimer timing;
#endif
    TX &tx=*r->tx; CC &cc=*r->cc;
    tile&=7;
    const Descriptor &d=tx.descriptor[tile],&e=tx.descriptor[(tile+1)&7];
    unsigned width=dimension(d,false),height=dimension(d,true);
    if(!width || !height){++rejected[3];return -1;}
    if(r->cycleType==1 && (d.shifts!=e.shifts || d.shiftt!=e.shiftt ||
       d.uls!=e.uls || d.ult!=e.ult || dimension(e,false)!=width || dimension(e,true)!=height)){++rejected[4];return -1;}
    // Do not bake equations that depend on a preceding pixel's COMBINED.
    Color32 *first[]={r->cycleType?cc.pa0:cc.pa1,r->cycleType?cc.pb0:cc.pb1,
                     r->cycleType?cc.pc0:cc.pc1,r->cycleType?cc.pd0:cc.pd1};
    bool direct=r->cycleType?cc.directColor0:cc.directColor1;
    for(int i=direct?3:0;i<4;i++)if(first[i]==&cc.combined || first[i]==&cc.combinedAlpha){++rejected[5];return -1;}
    float *alpha[]={r->cycleType?cc.pAa0:cc.pAa1,r->cycleType?cc.pAb0:cc.pAb1,
                    r->cycleType?cc.pAc0:cc.pAc1,r->cycleType?cc.pAd0:cc.pAd1};
    bool directAlpha=alpha[0]==alpha[1] || alpha[2]==cc.zero.getAlphap();
    for(int i=directAlpha?3:0;i<4;i++)if(alpha[i]==cc.combined.getAlphap()){++rejected[6];return -1;}
    // TMEM/palette are overwritten only if the immediately preceding texture
    // cannot be reused. Avoid clearing/copying 4.5 KiB for each strip triangle.
    std::memset(key.d,0,sizeof(key)-offsetof(TextureKey,d));
    key.d[0]=d;if(r->cycleType)key.d[1]=e;
    key.mux[0]=cc.oldCycle1;key.mux[1]=cc.oldCycle2;key.cycle=r->cycleType;
    key.lut=tx.textureLUT;key.filter=r->tf->getTextureFilter();
    key.alphaCoverage=r->bl->alpha_cvg_sel;key.alphaThreshold=alphaThreshold;
    Color32 inputs[]={cc.primColor,cc.envColor,shade,cc.primLOD,cc.LODFraction};
    for(int i=0;i<5;i++){key.colors[i*4]=inputs[i].getR();key.colors[i*4+1]=inputs[i].getG();key.colors[i*4+2]=inputs[i].getB();key.colors[i*4+3]=inputs[i].getAlpha();}
    if(lastSlot<SLOTS && cache[lastSlot].memory &&
       !std::memcmp(cache[lastSlot].key.d,key.d,sizeof(key)-offsetof(TextureKey,d)) &&
       !std::memcmp(cache[lastSlot].key.tmem,tx.tmem,sizeof(key.tmem)) &&
       !std::memcmp(cache[lastSlot].key.palette,tx.paletteData,sizeof(key.palette))) {
        cache[lastSlot].age=++clockAge;++hits;return lastSlot;
    }
    std::memcpy(key.tmem,tx.tmem,sizeof(key.tmem));std::memcpy(key.palette,tx.paletteData,sizeof(key.palette));
    uint32_t hash=hashKey(key);
    unsigned slot=0;bool foundEmpty=false;
    for(unsigned i=0;i<SLOTS;i++) {
        if(cache[i].memory && cache[i].hash==hash && !std::memcmp(&cache[i].key,&key,sizeof(key))) {
            cache[i].age=++clockAge;++hits;lastSlot=i;return i;
        }
        if(!cache[i].memory){slot=i;foundEmpty=true;}
        else if(!foundEmpty && cache[i].age<cache[slot].age)slot=i;
    }
    unsigned bytes=width*height*2;
    if(cache[slot].memory || usedBytes+bytes>VRAM_BUDGET) {
        flush();
        if(cache[slot].memory){usedBytes-=cache[slot].bytes;pvr_mem_free(cache[slot].memory);cache[slot].memory=nullptr;}
        while(usedBytes+bytes>VRAM_BUDGET) {
            unsigned oldest=SLOTS;
            for(unsigned i=0;i<SLOTS;i++)if(cache[i].memory && (oldest==SLOTS || cache[i].age<cache[oldest].age))oldest=i;
            if(oldest==SLOTS)return -1;
            usedBytes-=cache[oldest].bytes;pvr_mem_free(cache[oldest].memory);cache[oldest].memory=nullptr;
        }
    }
    auto memory=pvr_mem_malloc(bytes);if(!memory){++rejected[7];return -1;}
    CC savedCombiner=cc;
    cc.setShade(shade);
    bool opaque=true,binaryAlpha=true;
    float xs=shiftScale(d.shifts),ys=shiftScale(d.shiftt);
    for(unsigned y=0;y<height;y++)for(unsigned x=0;x<width;x++) {
        float s=(x+d.uls)/xs,t=(y+d.ult)/ys;
        Color32 a=tx.getTexel(s,t,tile,nullptr),c;
        if(r->cycleType)c=cc.combine2(a,tx.getTexel(s,t,(tile+1)&7,nullptr));
        else c=cc.combine1(a);
        c.clamp();if(r->bl->alpha_cvg_sel && !r->bl->cvg_x_alpha)c.setAlpha(255);
        if(alphaThreshold>=0 && c.getAlpha()<alphaThreshold)c.setAlpha(0);
        uint32_t v=(uint32_t)(int)c;colors[y*width+x]=v;
        unsigned alpha=v&255;
        if(alpha!=255)opaque=false;
        if(alpha!=0 && alpha!=255)binaryAlpha=false;
    }
    // Cache misses must not leave the combiner at the last baked texel.
    cc=savedCombiner;
    for(unsigned i=0;i<width*height;i++) {
        uint32_t c=colors[i];
        // Cutouts retain all five N64 RGB bits without increasing VRAM use.
        // Classify the baked alpha, since the combiner can make even RGBA16
        // sources translucent. Fractional alpha still needs ARGB4444.
        pixels[i]=opaque ? ((c>>16)&0xf800)|((c>>13)&0x7e0)|((c>>11)&31) :
            binaryAlpha ? ((c<<8)&0x8000)|((c>>17)&0x7c00)|((c>>14)&0x3e0)|((c>>11)&31) :
            ((c<<8)&0xf000)|((c>>20)&0xf00)|((c>>16)&0xf0)|((c>>12)&15);
    }
    pvr_txr_load(pixels,memory,bytes);
    Texture &entry=cache[slot];entry.key=key;entry.hash=hash;entry.age=++clockAge;
    entry.memory=memory;entry.width=width;entry.height=height;entry.bytes=bytes;
    entry.format=opaque?PVR_TXRFMT_RGB565:binaryAlpha?PVR_TXRFMT_ARGB1555:PVR_TXRFMT_ARGB4444;
    usedBytes+=bytes;++misses;lastSlot=slot;return slot;
}

bool PVRRaster::triangle(RDP *r,Vektor<float,4>& v0,Vektor<float,4>& v1,Vektor<float,4>& v2,
    Color32& c0,Color32& c1,Color32& c2,float s0,float t0,float s1,float t1,float s2,float t2,
    int tile,float w0,float w1,float w2,bool pointAlpha) {
#ifdef DC_RASTER_STRICT
    // Texture filtering, baked combiner state and alpha precision have not yet
    // passed the strict replay contract. Keep the complete operation in software.
    ++rejected[0];return false;
#endif
    bool blend;
    if(!eligible(r,blend,pointAlpha) || w0<=0 || w1<=0 || w2<=0){++rejected[0];return false;}
    RS &rs=*r->rs;
    if(rs.sulx>0 || rs.suly>0 || rs.slrx<W || rs.slry<H){++rejected[1];return false;}
    CC &c=*r->cc;
    bool shadeUsed=false;
    Color32 *rgb[]={c.pa0,c.pb0,c.pc0,c.pd0,c.pa1,c.pb1,c.pc1,c.pd1};
    float *alpha[]={c.pAa0,c.pAb0,c.pAc0,c.pAd0,c.pAa1,c.pAb1,c.pAc1,c.pAd1};
    for(int i=r->cycleType?0:4;i<8;i++)
        if(rgb[i]==&c.shade || rgb[i]==&c.shadeAlpha || alpha[i]==c.shade.getAlphap())shadeUsed=true;
    if(shadeUsed && (std::memcmp(&c0,&c1,sizeof(c0)) || std::memcmp(&c0,&c2,sizeof(c0)))){++rejected[2];return false;}
    const auto &bounds=r->tx->descriptor[tile&7];
    if(!(bounds.cms&2) && !bounds.masks) {
        float scale=shiftScale(bounds.shifts);
        float lo=std::min(s0,std::min(s1,s2))*scale-bounds.uls;
        float hi=std::max(s0,std::max(s1,s2))*scale-bounds.uls;
        if(lo< -0.0001f || hi>dimension(bounds,false)-1+0.0001f){return false;}
    }
    if(!(bounds.cmt&2) && !bounds.maskt) {
        float scale=shiftScale(bounds.shiftt);
        float lo=std::min(t0,std::min(t1,t2))*scale-bounds.ult;
        float hi=std::max(t0,std::max(t1,t2))*scale-bounds.ult;
        if(lo< -0.0001f || hi>dimension(bounds,true)-1+0.0001f){return false;}
    }
    int slot=texture(r,tile,shadeUsed?c0:Color32(0),pointAlpha?(int)r->bl->blendColor.getAlpha():-1);if(slot<0)return false;
    c.setShade(c0);
    if(draws>=400)flush();
    if(!begin(r))return false;
    auto &d=r->tx->descriptor[tile&7];auto &tex=cache[slot];
    int clamp=((d.cms&2)?PVR_UVCLAMP_U:0)|((d.cmt&2)?PVR_UVCLAMP_V:0);
    header(&tex,blend,!pointAlpha && r->tf->getTextureFilter()!=0,clamp);
    float xs=shiftScale(d.shifts),ys=shiftScale(d.shiftt);
    float center=pointAlpha?0.0f:0.5f;
    vertex(v0[0],v0[1],1/w0,(s0*xs-d.uls+center)/tex.width,(t0*ys-d.ult+center)/tex.height,0xffffffff,false);
    vertex(v1[0],v1[1],1/w1,(s1*xs-d.uls+center)/tex.width,(t1*ys-d.ult+center)/tex.height,0xffffffff,false);
    vertex(v2[0],v2[1],1/w2,(s2*xs-d.uls+center)/tex.width,(t2*ys-d.ult+center)/tex.height,0xffffffff,true);
    ++draws;return true;
}

bool PVRRaster::fill(RDP *r,float ux,float uy,float lx,float ly) {
    bool blend=false; Color32 color;
#ifdef DC_RASTER_STRICT
    // Fill-cycle word packing/scissor edges and blended fills need separate
    // proof. Only the ordinary opaque one-cycle rectangle is enabled here.
    if(r->cycleType!=0 || !eligible(r,blend) || blend)return false;
#endif
    if(r->cycleType==3) {
        BL &b=*r->bl;
        if(b.format || b.size!=2 || b.width!=(int)W || !b.validPixel(b.cImg,W-1,H-1,2))return false;
        unsigned v=b.fillColor;
        if(b.cImg==b.zImg || !(v&1) || (v>>16)!=(v&65535))return false;
        unsigned red=(v>>11)&31,green=(v>>6)&31,blue=(v>>1)&31;
        color=Color32((red<<3)|(red>>2),(green<<3)|(green>>2),(blue<<3)|(blue>>2),255);
        lx++;ly++;
    } else {
        if(r->cycleType!=0 || !eligible(r,blend))return false;
        color=r->cc->combine1(0);color.clamp();
        if(r->bl->alpha_cvg_sel)color.setAlpha(255);
    }
    int x0=(int)std::max(ux,r->rs->sulx),y0=(int)std::max(uy,r->rs->suly);
    int x1=(int)std::min(lx,r->rs->slrx),y1=(int)std::min(ly,r->rs->slry);
    if(x0>=x1 || y0>=y1)return true;
    if(x0<0 || y0<0 || x1>(int)W || y1>(int)H)return false;
    // With standard source-alpha blending, a zero-alpha fill leaves RGB
    // unchanged. The software path also sets alpha=1, so skip only when the
    // destination already has that alpha (or a pending RTT will normalize it).
    if(blend && color.getAlpha()==0) {
        // Sparse exact batches do not normalize alpha outside their union.
        // Complete them before examining destination alpha for this shortcut.
        if(pending && exactFillBatch)flush();
        bool unchanged=pending;
        if(!pending) {
            auto dst=(const unsigned short*)r->bl->cImg;
            unchanged=true;
            for(int y=y0;y<y1 && unchanged;y++)for(int x=x0;x<x1;x++)
                if(!(dst[(y*W+x)^S16]&1)){unchanged=false;break;}
        }
        if(unchanged){++transparentFills;return true;}
    }
    if(draws>=400)flush();
    bool exactFill=r->cycleType==0 && !blend;
    if(exactFill) {
        // Opaque fills overwrite their covered samples; no imported RGB is read.
        // Quantize before submission so PVR's color conversion cannot change the
        // reference's five-bit channels through different rounding/dithering.
        unsigned rgba=(unsigned)(int)color;
        unsigned red=(rgba>>27)&31,green=(rgba>>19)&31,blue=(rgba>>11)&31;
        // Exact quantization-bin origins survive both truncating and rounding
        // RGB565 readback. Bit replication would bias rounded channels upward.
        color=Color32(red<<3,green<<3,blue<<3,255);
        if(!begin(r,false,true))return false;
    } else {
        bool full=x0==0 && y0==0 && x1==(int)W && y1==(int)H && (!blend || color.getAlpha()==255);
        if(!begin(r,!full))return false;
    }
    if(exactFill) {
        for(int y=y0;y<y1;y++)for(int x=x0;x<x1;x++) {
            unsigned pixel=y*W+x;
            written[pixel/32]|=1u<<(pixel%32);
        }
    }
    header(nullptr,blend,false,0);quad(x0,y0,x1,y1,argb(color));++draws;return true;
}

bool PVRRaster::rectangle(RDP *r,int tile,float ux,float uy,float lx,float ly,
    float s,float t,float ds,float dt) {
    if(r->cycleType!=0 && r->cycleType!=1)return false;
    float x0=std::max(ux,r->rs->sulx),y0=std::max(uy,r->rs->suly);
    float x1=std::min(lx,r->rs->slrx),y1=std::min(ly,r->rs->slry);
    if(x0>=x1 || y0>=y1)return true;
    s+=(x0-ux)*ds;t+=(y0-uy)*dt;
    float s1=s+(x1-x0)*ds,t1=t+(y1-y0)*dt;
    // At integer coordinates with unit steps and no tile shifts, software
    // filtering returns exactly one texel. Bake a fixed alpha threshold and
    // use nearest sampling; general filtered alpha tests stay in software.
    const Descriptor &tileState=r->tx->descriptor[tile&7];
    bool alphaBlend=false;
    bool pointAlpha=r->cycleType==0 && r->bl->alphaCompare==1 &&
        r->bl->blendColor.getAlpha()>0 && ds==1 && dt==1 &&
        !tileState.shifts && !tileState.shiftt &&
        x0==floorf(x0) && y0==floorf(y0) && x1==floorf(x1) && y1==floorf(y1) &&
        s==floorf(s) && t==floorf(t) && tileState.uls==floorf(tileState.uls) &&
        tileState.ult==floorf(tileState.ult) && eligible(r,alphaBlend,true) && alphaBlend;
    Vektor<float,4> a,b,c,d;
    a[0]=x0;a[1]=y0;b[0]=x1;b[1]=y0;c[0]=x0;c[1]=y1;d[0]=x1;d[1]=y1;
    if(draws>=398)flush(); // Never split a rectangle at the submission limit.
    Color32 shade=r->cc->shade;
    if(!triangle(r,a,b,c,shade,shade,shade,s,t,s1,t,s,t1,tile,1,1,1,pointAlpha))return false;
    // All eligibility/cache checks are identical for the second half.
    return triangle(r,b,d,c,shade,shade,shade,s1,t,s1,t1,s,t1,tile,1,1,1,pointAlpha);
}
