#include "dc_raster_pvr.h"
#include "dc_raster_pixels.h"
#ifdef DC_RASTER_TRI_ORIGIN
#include "dc_raster_origin.h"
#endif
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
#ifdef DC_RASTER_BATCH_CACHE
constexpr unsigned SLOTS=256;
#else
constexpr unsigned SLOTS=128;
#endif
constexpr unsigned VRAM_BUDGET=2*1024*1024;
#ifdef DC_RASTER_WIDE
constexpr unsigned MAX_W=640,MAX_H=480,MAX_PITCH=1024,MAX_ROWS=512;
unsigned W=320,H=240,PITCH=512,rows=256;
#else
constexpr unsigned W=320,H=240,PITCH=512,rows=256;
constexpr unsigned MAX_W=W,MAX_H=H,MAX_PITCH=PITCH,MAX_ROWS=rows;
#endif
struct TextureKey {
    unsigned char tmem[4096];
    unsigned short palette[256];
    Descriptor d[2];
    int mux[2],cycle,lut,filter,alphaCoverage,alphaThreshold,shadeMode;
#ifdef DC_RASTER_FILTER_GRID
    int filterGrid;
#endif
    float colors[20];
};
struct Texture {
    TextureKey key;
    uint32_t hash,age,batch;
#ifdef DC_RASTER_CACHE_INDEX
    unsigned next;
#endif
    pvr_ptr_t memory;
    unsigned width,height,bytes,format;
#ifdef DC_RASTER_FILTER_GRID
    unsigned filterScale;
#endif
#ifdef DC_RASTER_TEXTURE_ALPHA
    bool rawOpaque;
#endif
};
Texture cache[SLOTS];
TextureKey key;
unsigned clockAge,usedBytes,draws,hits,misses,submissions,batchSerial,cacheEvictions,pinnedFlushes;
unsigned lastSlot=SLOTS,transparentFills;
const TX *lastTextureSource;
#ifdef DC_RASTER_CACHE_INDEX
constexpr unsigned HASH_BUCKETS=512;
unsigned buckets[HASH_BUCKETS]; // slot+1; zero is the empty chain.
const TX *contentSource;
uint32_t contentHash;
void removeIndex(unsigned slot) {
    unsigned *link=&buckets[cache[slot].hash&(HASH_BUCKETS-1)];
    while(*link && *link!=slot+1)link=&cache[*link-1].next;
    if(*link)*link=cache[slot].next;
    cache[slot].next=0;
}
void insertIndex(unsigned slot) {
    unsigned &head=buckets[cache[slot].hash&(HASH_BUCKETS-1)];
    cache[slot].next=head;head=slot+1;
}
#endif
unsigned rejected[8];
#ifdef DC_RASTER_FILTER_GRID
unsigned filterGrids;
#ifdef DC_RASTER_REPLAY
bool useFilterGrid=true;
#endif
#endif
#ifdef DC_RASTER_TRI_ORIGIN
unsigned origins,originGuards;
#ifdef DC_RASTER_REPLAY
bool useOrigin=true;
#endif
#endif
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
#ifdef DC_RASTER_WIDE
bool regionBatch,preserveRegion;
unsigned regionX0,regionY0,regionX1,regionY1;
void includeRegion(float x0,float y0,float x1,float y1) {
    if(!regionBatch)return;
    x0=std::max(0.0f,std::min((float)W,x0));
    x1=std::max(0.0f,std::min((float)W,x1));
    y0=std::max(0.0f,std::min((float)H,y0));
    y1=std::max(0.0f,std::min((float)H,y1));
    regionX0=std::min(regionX0,(unsigned)std::floor(x0));
    regionY0=std::min(regionY0,(unsigned)std::floor(y0));
    regionX1=std::max(regionX1,(unsigned)std::ceil(x1));
    regionY1=std::max(regionY1,(unsigned)std::ceil(y1));
}
#endif
bool failed;
void fail(const char *stage) {
    if(!failed)std::fprintf(stderr,"PVR raster failed: %s\n",stage);
    failed=true;
    stop=1;
}
alignas(32) unsigned short pixels[MAX_PITCH*MAX_ROWS];
alignas(32) uint32_t colors[256*256];
// Both modes share the qualified opaque one-cycle fill path. Track its
// exact union, so readback cannot modify alpha or RGB outside the written area.
uint32_t written[MAX_W*MAX_H/32];
static_assert(sizeof(cache)+sizeof(key)+sizeof(pixels)+sizeof(colors)+sizeof(written)+4096<=DC_TEXCACHE_SIZE,
              "PVR raster scratch exceeds the main-RAM budget");
uint32_t hashKey(const TextureKey &k) {
    auto p=reinterpret_cast<const uint32_t*>(&k);
    uint32_t h=2166136261u;
    unsigned first=0;
#ifdef DC_RASTER_CACHE_INDEX
    h=contentHash;first=offsetof(TextureKey,d)/4;
#endif
    for(unsigned i=first;i<sizeof(k)/4;i++) h=(h^p[i])*16777619u;
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
void header(Texture *texture,bool blend,bool linear,int clamp,bool offset=false) {
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
    if(offset){c.gen.specular=true;c.gen.color_clamp=true;}
    c.depth.comparison=PVR_DEPTHCMP_ALWAYS;
    c.depth.write=PVR_DEPTHWRITE_DISABLE;
    c.blend.src=blend?PVR_BLEND_SRCALPHA:PVR_BLEND_ONE;
    c.blend.dst=blend?PVR_BLEND_INVSRCALPHA:PVR_BLEND_ZERO;
    alignas(32) pvr_poly_hdr_t h; pvr_poly_compile(&h,&c);
    if(pvr_prim(&h,sizeof(h))<0)fail("polygon header");
}
void vertex(float x,float y,float z,float u,float v,uint32_t argb,bool end,uint32_t offset=0) {
    if(failed)return;
    alignas(32) pvr_vertex_t p={}; p.flags=end?PVR_CMD_VERTEX_EOL:PVR_CMD_VERTEX;
    p.x=x;p.y=y;p.z=z;p.u=u;p.v=v;p.argb=argb;p.oargb=offset;
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

bool PVRRaster::selectTarget(RDP *r) {
#ifdef DC_RASTER_WIDE
    unsigned width=r->bl->width,height=240,pitch=512,textureRows=256;
    if(width==640) {
        if(!(r->rs->slry>0 && r->rs->slry<=480))return false;
        height=(unsigned)std::ceil(r->rs->slry);
        pitch=1024;textureRows=32;while(textureRows<height)textureRows*=2;
    } else if(width!=320)return false;
    if(!r->bl->validPixel(r->bl->cImg,width-1,height-1,2))return false;
    // Finish with the previous canvas dimensions before changing the RTT stride.
    if(pending && (W!=width || H!=height))flush();
    if(failed)return false;
    W=width;H=height;PITCH=pitch;rows=textureRows;
    return true;
#else
    return r->bl->width==(int)W;
#endif
}

bool PVRRaster::eligible(RDP *r,bool &blend,bool alphaTestBaked,bool fullAlpha) {
    BL &b=*r->bl;
    bool alphaTest=(b.alphaCompare&1) && ((b.alphaCompare&2) || b.blendColor.getAlpha()>0);
    if(b.format || b.size!=2 || b.width!=(int)W || b.z_cmp || b.z_upd ||
       (alphaTest && !alphaTestBaked) || (b.cvg_x_alpha && !fullAlpha) || b.cImg==b.zImg || !b.validPixel(b.cImg,W-1,H-1,2))return false;
    if(r->cycleType==0) {
        if(!b.force_bl && b.psa1==&b.pixelColor) {blend=false;return true;}
#if defined(DC_RASTER_SHADE) && defined(DC_RASTER_IDENTITY) && !defined(DC_RASTER_STRICT)
        // force_bl can still select the exact PIXEL*0 + PIXEL*1 identity.
        // This is frequent in Stadium's rental rectangles; it does not need
        // source-alpha blending. Existing alpha/depth checks still apply.
        if(b.force_bl && b.psa1==&b.pixelColor && b.psb1==&b.pixelColor &&
           b.pca1==&b.zero && b.pcb1==&b.one) {blend=false;return true;}
#endif
        if(b.force_bl && b.psa1==&b.pixelColor && b.psb1==&b.memoryColor &&
           b.pca1==&b.pixelColor && b.pcb1==&b.invertedAlpha) {blend=true;return true;}
    }
    // Two identity blender stages, common for opaque textured backgrounds.
    if(r->cycleType==1 && b.psa1==&b.pixelColor && b.psb1==&b.pixelColor &&
       b.pca1==&b.zero && b.pcb1==&b.one && b.psa2==&b.blendedPixelColor &&
       (!b.force_bl || (b.psb2==&b.blendedPixelColor && b.pca2==&b.zero && b.pcb2==&b.one))) {
        blend=false;return true;
    }
#ifdef DC_RASTER_SHADE
    // Identity first cycle, then ordinary source-alpha blending. This remains
    // opt-in because general fast textures have approximate color precision.
    if(r->cycleType==1 && b.psa1==&b.pixelColor && b.psb1==&b.pixelColor &&
       b.pca1==&b.zero && b.pcb1==&b.one && b.force_bl &&
       b.psa2==&b.blendedPixelColor && b.psb2==&b.memoryColor &&
       b.pca2==&b.pixelColor && b.pcb2==&b.invertedAlpha) {
        blend=true;return true;
    }
#endif
    return false;
}

bool PVRRaster::begin(RDP *r,bool preserve,bool exactFill) {
    if(failed)return false;
    if(pending && (destination!=(unsigned short*)r->bl->cImg || exactFillBatch!=exactFill))flush();
    if(failed)return false;
    if(pending)return true;
    if(!output)output=pvr_mem_malloc(MAX_PITCH*MAX_ROWS*2);
    if(!initial)initial=pvr_mem_malloc(MAX_PITCH*MAX_ROWS*2);
    if(!output || !initial)return false;
    if(pvr_wait_ready()<0 || pvr_wait_render_done()<0)return false;
    // N64 channel quantization is explicit below; KOS enables an additional
    // framebuffer dither by default, which would alter those channel values.
    vid_set_dithering(false);
    destination=(unsigned short*)r->bl->cImg;
#ifdef DC_RASTER_WIDE
    regionBatch=W==640 && !exactFill;
    preserveRegion=regionBatch && preserve;
    regionX0=W;regionY0=H;regionX1=regionY1=0;
#endif
    if(preserve
#ifdef DC_RASTER_WIDE
       && !regionBatch
#endif
    ) {
        uint64_t start=timer_us_gettime64();
        dc_raster_import_rows(destination,pixels,W,H,PITCH);
        pvr_txr_load(pixels,initial,PITCH*rows*2);
        importUs+=timer_us_gettime64()-start;
    }
    if(pvr_scene_begin_rtt(output,W,H,PITCH)<0)return false;
    if(pvr_list_begin(PVR_LIST_TR_POLY)<0) {
        pvr_scene_finish();fail("begin list");return false;
    }
    pending=true; draws=0;
    if(!++batchSerial) {
        for(auto &t:cache)t.batch=0;
        batchSerial=1;
    }
    exactFillBatch=exactFill;
    if(exactFill)std::memset(written,0,sizeof(written));
    if(preserve) {
        Texture t={};t.memory=initial;t.width=PITCH;t.height=rows;t.format=PVR_TXRFMT_RGB565;
        header(&t,false,false,PVR_UVCLAMP_UV);
        vertex(0,0,1,0,0,0xffffffff,false);vertex(W,0,1,(float)W/PITCH,0,0xffffffff,false);
        vertex(0,H,1,0,(float)H/rows,0xffffffff,false);
        vertex(W,H,1,(float)W/PITCH,(float)H/rows,0xffffffff,true);
    }
    return true;
}

void PVRRaster::flush() {
    if(!pending)return;
    uint64_t start=timer_us_gettime64();
#ifdef DC_RASTER_WIDE
    if(preserveRegion && regionX0<regionX1 && regionY0<regionY1) {
        // The background texture is consumed only after scene_finish. No CPU
        // framebuffer writer can run within an RDP batch; software/texture
        // read barriers flush it first. Upload the union before rendering.
        unsigned x0=regionX0&~15u,x1=std::min(W,(regionX1+15u)&~15u);
        dc_raster_import_region(destination,pixels,W,PITCH,x0,regionY0,x1,regionY1);
        if(x0==0 && x1==W) {
            pvr_txr_load(pixels+regionY0*PITCH,(uint8_t*)initial+regionY0*PITCH*2,
                         (regionY1-regionY0)*PITCH*2);
        } else for(unsigned y=regionY0;y<regionY1;++y)
            pvr_txr_load(pixels+y*PITCH+x0,(uint8_t*)initial+(y*PITCH+x0)*2,(x1-x0)*2);
        importUs+=timer_us_gettime64()-start;
        start=timer_us_gettime64();
    }
#endif
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
        static_assert(MAX_W%32==0,"fill masks must not cross rows");
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
    }
#ifdef DC_RASTER_WIDE
    else if(regionBatch) {
        auto srcWords=(volatile uint32_t*)output;
        auto dstWords=(uint32_t*)destination;
        for(unsigned y=regionY0;y<regionY1;++y) {
            unsigned x=regionX0;
            if(!((uintptr_t)destination&3)) {
                if((x&1) && x<regionX1) {
                    unsigned v=src[y*PITCH+x];
                    destination[(y*W+x)^S16]=(v&0xffc0)|((v&31)<<1)|1;++x;
                }
                for(;x+1<regionX1;x+=2) {
                    uint32_t v=srcWords[(y*PITCH+x)/2];
                    uint32_t packed=(v&0xffc0ffc0u)|((v&0x001f001fu)<<1)|0x00010001u;
                    dstWords[(y*W+x)/2]=(packed<<16)|(packed>>16);
                }
            }
            for(;x<regionX1;++x) {
                unsigned v=src[y*PITCH+x];
                destination[(y*W+x)^S16]=(v&0xffc0)|((v&31)<<1)|1;
            }
        }
    }
#endif
    else {
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
        printf("PVR cache: evictions=%u pinned-flushes=%u slots=%u\n",cacheEvictions,pinnedFlushes,SLOTS);
        printf("PVR fallback: state=%u scissor=%u shade=%u size=%u dual=%u rgb=%u alpha=%u alloc=%u\n",
            rejected[0],rejected[1],rejected[2],rejected[3],rejected[4],rejected[5],rejected[6],rejected[7]);
        printf("PVR transparent fills skipped=%u\n",transparentFills);
#ifdef DC_RASTER_FILTER_GRID
        printf("PVR filter grids: baked=%u\n",filterGrids);
#endif
#ifdef DC_RASTER_TRI_ORIGIN
        printf("PVR triangle origins: corrected=%u guarded=%u\n",origins,originGuards);
#endif
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
#ifdef DC_RASTER_WIDE
    regionBatch=preserveRegion=false;
#endif
    batchSerial=cacheEvictions=pinnedFlushes=0;
    usedBytes=clockAge=hits=misses=submissions=0;lastSlot=SLOTS;transparentFills=0;
    lastTextureSource=nullptr;
#ifdef DC_RASTER_CACHE_INDEX
    std::memset(buckets,0,sizeof(buckets));contentSource=nullptr;
#endif
    std::memset(rejected,0,sizeof(rejected));
#ifdef DC_RASTER_FILTER_GRID
    filterGrids=0;
#ifdef DC_RASTER_REPLAY
    useFilterGrid=true;
#endif
#endif
#ifdef DC_RASTER_TRI_ORIGIN
    origins=originGuards=0;
#ifdef DC_RASTER_REPLAY
    useOrigin=true;
#endif
#endif
    waitUs=readbackUs=textureUs=importUs=0;
    failed=false;
}

#ifdef DC_RASTER_REPLAY
unsigned PVRRaster::textureMisses(){return misses;}
unsigned PVRRaster::pinnedFlushCount(){return pinnedFlushes;}
unsigned PVRRaster::originCount(){
#ifdef DC_RASTER_TRI_ORIGIN
    return origins;
#else
    return 0;
#endif
}
void PVRRaster::originEnabled(bool enabled){
#ifdef DC_RASTER_TRI_ORIGIN
    useOrigin=enabled;
#else
    (void)enabled;
#endif
}
unsigned PVRRaster::filterGridCount(){
#ifdef DC_RASTER_FILTER_GRID
    return filterGrids;
#else
    return 0;
#endif
}
void PVRRaster::filterGridEnabled(bool enabled){
#ifdef DC_RASTER_FILTER_GRID
    useFilterGrid=enabled;
#else
    (void)enabled;
#endif
}
#endif
unsigned PVRRaster::completedScenes() { return submissions; }

void PVRRaster::textureChanged(const TX *source) {
    if(lastTextureSource==source)lastTextureSource=nullptr;
#ifdef DC_RASTER_CACHE_INDEX
    if(contentSource==source)contentSource=nullptr;
#endif
}

void PVRRaster::readMemory(const void *source,unsigned bytes) {
    if(!pending || !bytes)return;
    // TMEM loads use word-swapped byte addresses. Include both partial words.
    uintptr_t first=(uintptr_t)source & ~(uintptr_t)3;
    uintptr_t end=((uintptr_t)source+bytes+3) & ~(uintptr_t)3;
    uintptr_t base=(uintptr_t)destination;
    if(first<base+W*H*2 && end>base)flush();
}
int PVRRaster::texture(RDP *r,int tile,Color32 shade,int alphaThreshold,int shadeMode,bool coverageMask,bool filterGrid) {
#ifdef DC_RASTER_PROFILE
    TextureTimer timing;
#endif
    TX &tx=*r->tx; CC &cc=*r->cc;
    tile&=7;
    const Descriptor &d=tx.descriptor[tile],&e=tx.descriptor[(tile+1)&7];
    unsigned width=dimension(d,false),height=dimension(d,true);
    if(!width || !height){++rejected[3];return -1;}
    bool secondTexel=r->cycleType==1 && cc.needsSecondTexel();
    if(secondTexel && (d.shifts!=e.shifts || d.shiftt!=e.shiftt ||
       d.uls!=e.uls || d.ult!=e.ult || dimension(e,false)!=width || dimension(e,true)!=height)){++rejected[4];return -1;}
#ifdef DC_RASTER_FILTER_GRID
    // Store exact software-filter samples on a quarter-texel grid. Bilinear
    // interpolation between them reduces the three-point diagonal mismatch.
    // Keep rectangles, cutout alpha, and larger textures on their native grid.
    unsigned filterScale=filterGrid && shadeMode>0 && shadeMode<4 &&
#ifdef DC_RASTER_REPLAY
        useFilterGrid &&
#endif
        r->tf->getTextureFilter()==2 && width<=64 && height<=64?4:1;
#endif
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
    key.d[0]=d;if(secondTexel)key.d[1]=e;
    key.mux[0]=cc.oldCycle1;key.mux[1]=cc.oldCycle2;key.cycle=r->cycleType;
    key.lut=tx.textureLUT;key.filter=r->tf->getTextureFilter();
    key.alphaCoverage=r->bl->alpha_cvg_sel;
    // Cutoffs are byte alpha values. The extra bit distinguishes accepted
    // identity overwrites from ordinary tests which retain fractional alpha.
    key.alphaThreshold=alphaThreshold+(coverageMask?256:0);
    key.shadeMode=shadeMode;
#ifdef DC_RASTER_FILTER_GRID
    key.filterGrid=filterScale;
#endif
    Color32 inputs[]={cc.primColor,cc.envColor,shade,cc.primLOD,cc.LODFraction};
    // Only inputs used by the baked equations belong in the texture key.
    // Vertex modulation/bias modes bake raw texels, so all their constants are
    // submitted separately. Unused primitive/environment color changes must
    // not create duplicate textures and force cache-eviction scene barriers.
    unsigned used[5]={};
    auto rgbInput=[&](Color32 *source) {
        Color32 *rgb[]={&cc.primColor,&cc.envColor,&cc.shade,&cc.primLOD,&cc.LODFraction};
        Color32 *alpha[]={&cc.primAlpha,&cc.envAlpha,&cc.shadeAlpha,nullptr,nullptr};
        for(int i=0;i<5;i++) {
            if(source==rgb[i])used[i]|=1;
            if(alpha[i] && source==alpha[i])used[i]|=2;
        }
    };
    auto alphaInput=[&](float *source) {
        Color32 *values[]={&cc.primColor,&cc.envColor,&cc.shade,&cc.primLOD,&cc.LODFraction};
        for(int i=0;i<5;i++)if(source==values[i]->getAlphap())used[i]|=2;
    };
    if(!shadeMode)for(unsigned cycle=r->cycleType?0:1;cycle<2;cycle++) {
        Color32 *rgb[]={cycle?cc.pa1:cc.pa0,cycle?cc.pb1:cc.pb0,
                        cycle?cc.pc1:cc.pc0,cycle?cc.pd1:cc.pd0};
        float *alpha[]={cycle?cc.pAa1:cc.pAa0,cycle?cc.pAb1:cc.pAb0,
                        cycle?cc.pAc1:cc.pAc0,cycle?cc.pAd1:cc.pAd0};
        bool directRGB=cycle?cc.directColor1:cc.directColor0;
        bool directA=alpha[0]==alpha[1] || alpha[2]==cc.zero.getAlphap();
        for(unsigned i=directRGB?3:0;i<4;i++)rgbInput(rgb[i]);
        for(unsigned i=directA?3:0;i<4;i++)alphaInput(alpha[i]);
    }
    for(int i=0;i<5;i++) {
        if(used[i]&1){key.colors[i*4]=inputs[i].getR();key.colors[i*4+1]=inputs[i].getG();key.colors[i*4+2]=inputs[i].getB();}
        if(used[i]&2)key.colors[i*4+3]=inputs[i].getAlpha();
    }
    if(lastSlot<SLOTS && cache[lastSlot].memory &&
       !std::memcmp(cache[lastSlot].key.d,key.d,sizeof(key)-offsetof(TextureKey,d)) &&
       (lastTextureSource==&tx ||
        (!std::memcmp(cache[lastSlot].key.tmem,tx.tmem,sizeof(key.tmem)) &&
         !std::memcmp(cache[lastSlot].key.palette,tx.paletteData,sizeof(key.palette))))) {
        // Small descriptors/shader inputs are checked above every time. TMEM
        // and palette cannot change between setters on this same live TX.
        lastTextureSource=&tx;
        cache[lastSlot].age=++clockAge;++hits;return lastSlot;
    }
#ifdef DC_RASTER_CACHE_INDEX
    if(contentSource!=&tx) {
#endif
        std::memcpy(key.tmem,tx.tmem,sizeof(key.tmem));std::memcpy(key.palette,tx.paletteData,sizeof(key.palette));
#ifdef DC_RASTER_CACHE_INDEX
        static_assert(offsetof(TextureKey,d)%4==0,"texture content must be word aligned");
        auto words=reinterpret_cast<const uint32_t*>(&key);
        contentHash=2166136261u;
        for(unsigned i=0;i<offsetof(TextureKey,d)/4;i++)contentHash=(contentHash^words[i])*16777619u;
        contentSource=&tx;
    }
#endif
    uint32_t hash=hashKey(key);
#ifdef DC_RASTER_CACHE_INDEX
    for(unsigned node=buckets[hash&(HASH_BUCKETS-1)];node;node=cache[node-1].next) {
        unsigned i=node-1;
        if(cache[i].memory && cache[i].hash==hash && !std::memcmp(&cache[i].key,&key,sizeof(key))) {
            cache[i].age=++clockAge;++hits;lastSlot=i;lastTextureSource=&tx;return i;
        }
    }
#endif
    unsigned slot=0;bool foundEmpty=false;
    for(unsigned i=0;i<SLOTS;i++) {
#ifndef DC_RASTER_CACHE_INDEX
        if(cache[i].memory && cache[i].hash==hash && !std::memcmp(&cache[i].key,&key,sizeof(key))) {
            cache[i].age=++clockAge;++hits;lastSlot=i;lastTextureSource=&tx;return i;
        }
#endif
        if(!cache[i].memory){slot=i;foundEmpty=true;}
        else if(!foundEmpty && cache[i].age<cache[slot].age)slot=i;
    }
#ifdef DC_RASTER_FILTER_GRID
    width*=filterScale;height*=filterScale;
#endif
    unsigned bytes=width*height*2;
    if(cache[slot].memory || usedBytes+bytes>VRAM_BUDGET) {
#ifdef DC_RASTER_BATCH_CACHE
        if(cache[slot].memory && pending && cache[slot].batch==batchSerial) {
            ++pinnedFlushes;flush();
        }
#else
        flush();
#endif
        if(failed)return -1;
        ++cacheEvictions;
        if(cache[slot].memory){
#ifdef DC_RASTER_CACHE_INDEX
            removeIndex(slot);
#endif
            usedBytes-=cache[slot].bytes;pvr_mem_free(cache[slot].memory);cache[slot].memory=nullptr;
        }
        while(usedBytes+bytes>VRAM_BUDGET) {
            unsigned oldest=SLOTS;
            for(unsigned i=0;i<SLOTS;i++)if(cache[i].memory && (oldest==SLOTS || cache[i].age<cache[oldest].age))oldest=i;
            if(oldest==SLOTS)return -1;
#ifdef DC_RASTER_BATCH_CACHE
            if(pending && cache[oldest].batch==batchSerial){++pinnedFlushes;flush();}
            if(failed)return -1;
#endif
#ifdef DC_RASTER_CACHE_INDEX
            removeIndex(oldest);
#endif
            usedBytes-=cache[oldest].bytes;pvr_mem_free(cache[oldest].memory);cache[oldest].memory=nullptr;
        }
    }
    auto memory=pvr_mem_malloc(bytes);if(!memory){++rejected[7];return -1;}
    CC savedCombiner=cc;
    cc.setShade(shade);
    bool opaque=true,binaryAlpha=true;
#ifdef DC_RASTER_TEXTURE_ALPHA
    bool rawOpaque=true;
#endif
    float xs=shiftScale(d.shifts),ys=shiftScale(d.shiftt);
    for(unsigned y=0;y<height;y++)for(unsigned x=0;x<width;x++) {
#ifdef DC_RASTER_FILTER_GRID
        float gx=x,gy=y;
        if(filterScale>1){gx*=0.25f;gy*=0.25f;}
        float s=(gx+d.uls)/xs,t=(gy+d.ult)/ys;
        Color32 a=tx.getTexel(s,t,tile,filterScale>1?r->tf:nullptr),c;
#else
        float s=(x+d.uls)/xs,t=(y+d.ult)/ys;
        Color32 a=tx.getTexel(s,t,tile,nullptr),c;
#endif
#ifdef DC_RASTER_TEXTURE_ALPHA
        if(a.getAlpha()!=255)rawOpaque=false;
#endif
        if(shadeMode){
            // The translucent separable mode requires opaque source texels:
            // filtering binary cutout alpha before squaring is not equivalent
            // to squaring each texel before the GPU filter.
            if(shadeMode==3 && a.getAlpha()!=255) {
                cc=savedCombiner;pvr_mem_free(memory);++rejected[6];return -1;
            }
            if(shadeMode==4 && a.getAlpha()!=0 && a.getAlpha()!=255) {
                cc=savedCombiner;pvr_mem_free(memory);++rejected[6];return -1;
            }
            c=a;if(shadeMode!=4)c.setAlpha(255);
        }
        else if(r->cycleType)c=cc.combine2(a,secondTexel?tx.getTexel(s,t,(tile+1)&7,nullptr):Color32(0));
        else c=cc.combine1(a);
        c.clamp();if(r->bl->alpha_cvg_sel && !r->bl->cvg_x_alpha)c.setAlpha(255);
        if(alphaThreshold>=0) {
            if(c.getAlpha()<alphaThreshold)c.setAlpha(0);
            else if(coverageMask)c.setAlpha(255);
        }
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
#ifdef DC_RASTER_FILTER_GRID
    entry.filterScale=filterScale;
    if(filterScale>1)++filterGrids;
#endif
#ifdef DC_RASTER_TEXTURE_ALPHA
    entry.rawOpaque=rawOpaque;
#endif
#ifdef DC_RASTER_CACHE_INDEX
    insertIndex(slot);
#endif
    usedBytes+=bytes;++misses;lastSlot=slot;lastTextureSource=&tx;return slot;
}

bool PVRRaster::triangle(RDP *r,Vektor<float,4>& v0,Vektor<float,4>& v1,Vektor<float,4>& v2,
    Color32& c0,Color32& c1,Color32& c2,float s0,float t0,float s1,float t1,float s2,float t2,
    int tile,float w0,float w1,float w2,bool pointAlpha,bool clippedRectangle,bool coveragePoint,
    bool rectanglePrimitive) {
#ifdef DC_RASTER_STRICT
    // Texture filtering, baked combiner state and alpha precision have not yet
    // passed the strict replay contract. Keep the complete operation in software.
    ++rejected[0];return false;
#endif
    bool blend;
#ifdef DC_RASTER_WIDE
    for(auto v:{v0,v1,v2})if(!std::isfinite(v[0]) || !std::isfinite(v[1]))return false;
#endif
    if(!selectTarget(r))return false;
    CC &c=*r->cc;
    bool fullAlpha=coveragePoint;
#ifdef DC_RASTER_TEXTURE_ALPHA
    bool requireOpaqueTexture=false;
#endif
#ifdef DC_RASTER_FULL_ALPHA
    // A one-cycle final alpha of exactly 255 cannot discard coverage. Read
    // only current constants, never stale COMBINED/TEXEL registers. Uniform
    // vertex alpha is independent of whether the RGB shade varies.
    if(!fullAlpha && r->cycleType==0 && r->bl->cvg_x_alpha) {
        auto constant=[&](float *p,float &value) {
            for(auto color: {&c.zero,&c.one,&c.primColor,&c.envColor,&c.primLOD,&c.LODFraction})
                if(p==color->getAlphap()){value=color->getAlpha();return std::isfinite(value);}
            if(p==c.shade.getAlphap() && c0.getAlpha()==c1.getAlpha() && c0.getAlpha()==c2.getAlpha()) {
                value=c0.getAlpha();return std::isfinite(value);
            }
            return false;
        };
        float d,a,b;
        if(constant(c.pAd1,d) && d==255) {
            fullAlpha=c.directAlpha1;
            // (A-B)*TEXEL/255 + 255 is exactly 255 if current A==B.
            if(!fullAlpha && constant(c.pAa1,a) && constant(c.pAb1,b) && a==b &&
               (c.pAc1==c.texel0.getAlphap() || c.pAc1==c.texel1.getAlphap() ||
                constant(c.pAc1,d)))fullAlpha=true;
        }
    }
#endif
#ifdef DC_RASTER_TEXTURE_ALPHA
    // Uniform raw alpha255 stays255 under every supported convex filter.
    // Prove the current final equation with that value, then require every
    // raw source texel to satisfy it before submitting anything to PVR.
    // Never infer opacity from baked/clamped alpha or stale TEXEL registers.
    if(!fullAlpha && r->cycleType==0 && r->bl->cvg_x_alpha && !c.needsSecondTexel()) {
        bool usesTexture=false;
        auto alphaValue=[&](float *p,float &value) {
            if(p==c.texel0.getAlphap()){value=255;usesTexture=true;return true;}
            for(auto color:{&c.zero,&c.one,&c.primColor,&c.envColor,&c.primLOD,&c.LODFraction})
                if(p==color->getAlphap()){value=color->getAlpha();return std::isfinite(value);}
            if(p==c.shade.getAlphap() && c0.getAlpha()==c1.getAlpha() && c0.getAlpha()==c2.getAlpha()) {
                value=c0.getAlpha();return std::isfinite(value);
            }
            return false;
        };
        float a,b,factor,d;
        if(alphaValue(c.pAd1,d) && (c.directAlpha1 ||
           (alphaValue(c.pAa1,a) && alphaValue(c.pAb1,b) && alphaValue(c.pAc1,factor)))) {
            float finalAlpha=c.directAlpha1?d:(factor/255.0f)*(a-b)+d;
            if(usesTexture && finalAlpha==255){fullAlpha=true;requireOpaqueTexture=true;}
        }
    }
#endif
    if(!eligible(r,blend,pointAlpha,fullAlpha) || w0<=0 || w1<=0 || w2<=0){++rejected[0];return false;}
    // Identity rectangles overwrite accepted pixels. A binary baked alpha mask
    // implements the software coverage cutoff through source-alpha blending.
    if(coveragePoint)blend=true;
    RS &rs=*r->rs;
    if(rs.sulx>0 || rs.suly>0 || rs.slrx<W || rs.slry<H) {
#ifndef DC_RASTER_RECT_CLIP
        clippedRectangle=false;
#endif
        // Only integer rectangles already clipped by rectangle() qualify.
        // General triangles still require the original full-canvas scissor.
        if(!clippedRectangle){++rejected[1];return false;}
        for(auto v:{v0,v1,v2})if(v[0]<rs.sulx || v[1]<rs.suly ||
            v[0]>rs.slrx || v[1]>rs.slry){++rejected[1];return false;}
    }
    bool shadeUsed=false;
    Color32 *rgb[]={c.pa0,c.pb0,c.pc0,c.pd0,c.pa1,c.pb1,c.pc1,c.pd1};
    float *alpha[]={c.pAa0,c.pAb0,c.pAc0,c.pAd0,c.pAa1,c.pAb1,c.pAc1,c.pAd1};
    for(int i=r->cycleType?0:4;i<8;i++)
        if(rgb[i]==&c.shade || rgb[i]==&c.shadeAlpha || alpha[i]==c.shade.getAlphap())shadeUsed=true;
    bool varyingShade=shadeUsed && (std::memcmp(&c0,&c1,sizeof(c0)) || std::memcmp(&c0,&c2,sizeof(c0)));
    int shadeMode=0;
#ifdef DC_RASTER_SHADE
    // Experimental separable opaque equations only. Offset RGB is added after
    // modulation, so positive bias is not prematurely clipped in the texture.
    if(varyingShade && r->cycleType==1 && !blend && r->bl->alpha_cvg_sel &&
       !r->bl->cvg_x_alpha && !c.needsSecondTexel() &&
       c.directColor1 && c.pd1==&c.combined && c.pb0==&c.primColor) {
        if(c.pa0==&c.texel0 && c.pc0==&c.shade && c.pd0==&c.shade)shadeMode=1;
        if(c.pa0==&c.shade && c.pc0==&c.primAlpha && c.pd0==&c.texel0)shadeMode=2;
        const Color32 shades[]={c0,c1,c2};
        for(auto value:shades) {
            if(!(value.getR()>=0 && value.getR()<=255 && value.getG()>=0 && value.getG()<=255 &&
                 value.getB()>=0 && value.getB()<=255))shadeMode=0;
            // Packed PVR offset channels cannot represent a negative addend.
            if(shadeMode==2 && (value.getR()<c.primColor.getR() ||
               value.getG()<c.primColor.getG() || value.getB()<c.primColor.getB()))shadeMode=0;
        }
    }
    if(varyingShade && r->cycleType==1 && blend && !r->bl->alpha_cvg_sel &&
       !c.needsSecondTexel() && c.directColor1 && c.pd1==&c.combined &&
       c.pa0==&c.shade && c.pb0==&c.zero && c.pc0==&c.texel0 && c.pd0==&c.primColor &&
       c.pAa0==c.texel0.getAlphap() && c.pAb0==c.zero.getAlphap() &&
       c.pAc0==c.texel0.getAlphap() && c.pAd0==c.zero.getAlphap() &&
       c.pAa1==c.combined.getAlphap() && c.pAb1==c.zero.getAlphap() &&
       c.pAc1==c.primColor.getAlphap() && c.pAd1==c.zero.getAlphap()) {
        shadeMode=3;
        for(auto value: {c0,c1,c2})
            if(!(value.getR()>=0 && value.getR()<=255 && value.getG()>=0 && value.getG()<=255 &&
                 value.getB()>=0 && value.getB()<=255))shadeMode=0;
    }
#endif
#ifdef DC_RASTER_CUTOUT
    // (TEXEL0-PRIM)*SHADE+SHADE with direct TEXEL0 alpha through both
    // cycles. Keep source alpha in the texture: filtering that alpha then
    // blending is separable. Fractional source alpha is rejected while baking
    // so this mode retains five-bit RGB in ARGB1555 rather than ARGB4444.
    if(varyingShade && r->cycleType==1 && blend && !r->bl->alpha_cvg_sel &&
       !c.needsSecondTexel() && c.directColor1 && c.pd1==&c.combined &&
       c.pa0==&c.texel0 && c.pb0==&c.primColor && c.pc0==&c.shade && c.pd0==&c.shade &&
       c.directAlpha0 && c.pAd0==c.texel0.getAlphap() &&
       c.directAlpha1 && c.pAd1==c.combined.getAlphap() &&
       c.primColor.getR()>=210 && c.primColor.getG()>=210 && c.primColor.getB()>=210) {
        // Lower primitive RGB amplifies the existing perspective/packed-color
        // approximation beyond the bounded probe. Keep it in software.
        shadeMode=4;
        for(auto value: {c0,c1,c2})
            if(!(value.getR()>=0 && value.getR()<=255 && value.getG()>=0 && value.getG()<=255 &&
                 value.getB()>=0 && value.getB()<=255))shadeMode=0;
    }
#endif
    if(varyingShade && !shadeMode){++rejected[2];return false;}
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
    int threshold=coveragePoint?std::max(32,r->bl->alphaCompare==1?(int)r->bl->blendColor.getAlpha():0):
                  pointAlpha?(int)r->bl->blendColor.getAlpha():-1;
    int slot=texture(r,tile,shadeMode?Color32(0):shadeUsed?c0:Color32(0),threshold,shadeMode,coveragePoint,!rectanglePrimitive);if(slot<0)return false;
#ifdef DC_RASTER_TEXTURE_ALPHA
    if(requireOpaqueTexture && !cache[slot].rawOpaque){++rejected[6];return false;}
#endif
    c.setShade(c0);
    if(draws>=400)flush();
    if(!begin(r,!coveragePoint,coveragePoint))return false;
#ifdef DC_RASTER_WIDE
    includeRegion(std::min(v0[0],std::min(v1[0],v2[0])),std::min(v0[1],std::min(v1[1],v2[1])),
                  std::max(v0[0],std::max(v1[0],v2[0])),std::max(v0[1],std::max(v1[1],v2[1])));
#endif
    auto &d=r->tx->descriptor[tile&7];auto &tex=cache[slot];
    int clamp=((d.cms&2)?PVR_UVCLAMP_U:0)|((d.cmt&2)?PVR_UVCLAMP_V:0);
    tex.batch=batchSerial;
    header(&tex,blend,!pointAlpha && r->tf->getTextureFilter()!=0,clamp,shadeMode!=0);
    float xs=shiftScale(d.shifts),ys=shiftScale(d.shiftt);
    float center=pointAlpha?0.0f:0.5f;
#ifdef DC_RASTER_FILTER_GRID
    float sampleWidth=tex.width,sampleHeight=tex.height;
    if(tex.filterScale>1){sampleWidth*=0.25f;sampleHeight*=0.25f;center=0.125f;}
#endif
    Color32 shades[]={c0,c1,c2};
#ifdef DC_RASTER_TRI_ORIGIN
    bool corrected=false;float ou[3],ov[3],oq[3];
    if(!rectanglePrimitive
#ifdef DC_RASTER_REPLAY
       && useOrigin
#endif
    ) {
        float xy[3][2]={{v0[0],v0[1]},{v1[0],v1[1]},{v2[0],v2[1]}};
#ifdef DC_RASTER_FILTER_GRID
        float u[]={ (s0*xs-d.uls+center)/sampleWidth,(s1*xs-d.uls+center)/sampleWidth,(s2*xs-d.uls+center)/sampleWidth };
        float uv[]={ (t0*ys-d.ult+center)/sampleHeight,(t1*ys-d.ult+center)/sampleHeight,(t2*ys-d.ult+center)/sampleHeight };
#else
        float u[]={ (s0*xs-d.uls+center)/tex.width,(s1*xs-d.uls+center)/tex.width,(s2*xs-d.uls+center)/tex.width };
        float uv[]={ (t0*ys-d.ult+center)/tex.height,(t1*ys-d.ult+center)/tex.height,(t2*ys-d.ult+center)/tex.height };
#endif
        float q[]={1/w0,1/w1,1/w2};DCRasterOrigin plane;
        corrected=plane.prepare(xy) && plane.texture(u,uv,q,ou,ov,oq);
        if(corrected && shadeMode) {
            float dr=plane.shift(c0.getR(),c1.getR(),c2.getR());
            float dg=plane.shift(c0.getG(),c1.getG(),c2.getG());
            float db=plane.shift(c0.getB(),c1.getB(),c2.getB());
            for(auto &shade:shades) {
                shade.setR(shade.getR()-dr);shade.setG(shade.getG()-dg);shade.setB(shade.getB()-db);
                if(!(shade.getR()>=0 && shade.getR()<=255 && shade.getG()>=0 && shade.getG()<=255 &&
                     shade.getB()>=0 && shade.getB()<=255) || (shadeMode==2 &&
                   (shade.getR()<c.primColor.getR() || shade.getG()<c.primColor.getG() || shade.getB()<c.primColor.getB())))corrected=false;
            }
        }
        if(corrected)++origins;
        else {shades[0]=c0;shades[1]=c1;shades[2]=c2;++originGuards;}
    }
#endif
    auto submit=[&](Vektor<float,4> &v,Color32 shade,float w,float s,float t,unsigned index,bool end) {
        uint32_t color=0xffffffff,offset=0;
#ifdef DC_RASTER_TEXEL_SCALE
        // PVR expands packed channel values, whereas the software RGBA16
        // sampler uses bin origins (five-bit value <<3). For unmodulated
        // textures, remove that expansion before the framebuffer quantizer.
        // Fractional ARGB4444 and varying-shade equations retain their path.
        if(!shadeMode) {
            if(tex.format==PVR_TXRFMT_ARGB1555)color=0xfff8f8f8;
            else if(tex.format==PVR_TXRFMT_RGB565)color=0xfff8fcf8;
        }
#endif
        if(shadeMode) {
            Color32 bias;
            if(shadeMode==1 || shadeMode==4) {
                bias=Color32((255-c.primColor.getR())*shade.getR()/255.0f,
                    (255-c.primColor.getG())*shade.getG()/255.0f,
                    (255-c.primColor.getB())*shade.getB()/255.0f,0);
                shade.setAlpha(255);color=argb(shade);
            } else if(shadeMode==2) {
                float factor=c.primColor.getAlpha()/255.0f;
                bias=(shade-c.primColor)*factor;bias.setAlpha(0);
            } else {
                bias=c.primColor;bias.setAlpha(0);
                shade.setAlpha(c.primColor.getAlpha());color=argb(shade);
            }
            offset=argb(bias);
        }
#ifdef DC_RASTER_SHADE_SCALE
        // The offset is already in combiner units. Only undo the GPU's
        // packed-texel expansion in the multiplicative vertex channels.
        if(shadeMode && tex.format!=PVR_TXRFMT_ARGB4444) {
            unsigned greenScale=tex.format==PVR_TXRFMT_RGB565?252:248;
            unsigned red=(((color>>16)&255)*248+127)/255;
            unsigned green=(((color>>8)&255)*greenScale+127)/255;
            unsigned blue=((color&255)*248+127)/255;
            color=(color&0xff000000)|(red<<16)|(green<<8)|blue;
        }
#endif
#ifdef DC_RASTER_TRI_ORIGIN
        if(corrected)vertex(v[0],v[1],oq[index],ou[index],ov[index],color,end,offset);
        else
#endif
#ifdef DC_RASTER_FILTER_GRID
        if(tex.filterScale>1)vertex(v[0],v[1],1/w,(s*xs-d.uls+center)/sampleWidth,(t*ys-d.ult+center)/sampleHeight,color,end,offset);
        else
#endif
        vertex(v[0],v[1],1/w,(s*xs-d.uls+center)/tex.width,(t*ys-d.ult+center)/tex.height,color,end,offset);
    };
    submit(v0,shades[0],w0,s0,t0,0,false);submit(v1,shades[1],w1,s1,t1,1,false);submit(v2,shades[2],w2,s2,t2,2,true);
    ++draws;return true;
}

bool PVRRaster::fill(RDP *r,float ux,float uy,float lx,float ly) {
    if(!selectTarget(r))return false;
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
        // Sparse and regional batches do not normalize alpha outside their union.
        // Complete them before examining destination alpha for this shortcut.
        if(pending && (exactFillBatch
#ifdef DC_RASTER_WIDE
                       || regionBatch
#endif
        ))flush();
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
#ifdef DC_RASTER_WIDE
    includeRegion(x0,y0,x1,y1);
#endif
    header(nullptr,blend,false,0);quad(x0,y0,x1,y1,argb(color));++draws;return true;
}

bool PVRRaster::rectangle(RDP *r,int tile,float ux,float uy,float lx,float ly,
    float s,float t,float ds,float dt) {
    if(r->cycleType!=0 && r->cycleType!=1)return false;
    if(!selectTarget(r))return false;
    float x0=std::max(ux,r->rs->sulx),y0=std::max(uy,r->rs->suly);
    float x1=std::min(lx,r->rs->slrx),y1=std::min(ly,r->rs->slry);
    if(x0>=x1 || y0>=y1)return true;
    s+=(x0-ux)*ds;t+=(y0-uy)*dt;
    float stateS=s,stateT=t;
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
    bool coveragePoint=false;
#ifdef DC_RASTER_CVG_POINT
    // Alpha<32 is discarded by the current software coverage model. Exact
    // integer unit samples plus identity overwrite make this a per-texel mask.
    // Fractional/filter-edge samples, dither, depth and real blending stay on
    // the original path. Texture baking still rejects stale combiner inputs.
    bool identity=false;
    coveragePoint=r->cycleType==0 && r->bl->cvg_x_alpha &&
        (r->bl->alphaCompare==0 || r->bl->alphaCompare==1) &&
        (r->tf->getTextureFilter()==0 || r->tf->getTextureFilter()==2) &&
        !r->cc->needsSecondTexel() && ds==1 && dt==1 &&
        !tileState.shifts && !tileState.shiftt &&
        x0==floorf(x0) && y0==floorf(y0) && x1==floorf(x1) && y1==floorf(y1) &&
        s==floorf(s) && t==floorf(t) && tileState.uls==floorf(tileState.uls) &&
        tileState.ult==floorf(tileState.ult) && eligible(r,identity,true,true) && !identity;
    pointAlpha|=coveragePoint;
#endif
#ifdef DC_RASTER_RECT_SAMPLE
    // Software starts its affine samples at integer pixel coordinates. PVR
    // evaluates the interpolated UV at x+.5,y+.5. Correct only integer,
    // filtered rectangles; point-alpha and fractional coverage keep their
    // qualified paths. Bounds checks may conservatively fall back afterwards.
    bool exactNearest=r->tf->getTextureFilter()==0 && ds==1 && dt==1 &&
        !tileState.shifts && !tileState.shiftt && s==floorf(s) && t==floorf(t);
    if(!pointAlpha && (r->tf->getTextureFilter()==2 || exactNearest) &&
       x0==floorf(x0) && y0==floorf(y0) && x1==floorf(x1) && y1==floorf(y1) &&
       ux==floorf(ux) && uy==floorf(uy)) {
        s-=ds*0.5f;s1-=ds*0.5f;t-=dt*0.5f;t1-=dt*0.5f;
    }
#endif
    bool clippedRectangle=false;
#ifdef DC_RASTER_RECT_CLIP
    clippedRectangle=x0==floorf(x0) && y0==floorf(y0) && x1==floorf(x1) && y1==floorf(y1) &&
        ux==floorf(ux) && uy==floorf(uy);
#endif
    Vektor<float,4> a,b,c,d;
    a[0]=x0;a[1]=y0;b[0]=x1;b[1]=y0;c[0]=x0;c[1]=y1;d[0]=x1;d[1]=y1;
    if(draws>=398)flush(); // Never split a rectangle at the submission limit.
    Color32 shade=r->cc->shade;
    if(!triangle(r,a,b,c,shade,shade,shade,s,t,s1,t,s,t1,tile,1,1,1,pointAlpha,clippedRectangle,coveragePoint,true))return false;
    // All eligibility/cache checks are identical for the second half.
    if(!triangle(r,b,d,c,shade,shade,shade,s1,t,s1,t1,s,t1,tile,1,1,1,pointAlpha,clippedRectangle,coveragePoint,true))return false;
#ifdef DC_RASTER_CVG_POINT
    if(coveragePoint) {
        // No imported backdrop is needed for identity overwrites. Mark only
        // accepted texels so readback preserves both RGB and alpha in holes.
        const Texture &tex=cache[lastSlot];
        auto source=(volatile const uint16_t*)tex.memory;
        const Descriptor &desc=r->tx->descriptor[tile&7];
        int sx=(int)(stateS-desc.uls),sy=(int)(stateT-desc.ult);
        for(int y=(int)y0;y<(int)y1;y++) {
            int ty=sy+y-(int)y0;
            ty=desc.cmt&2?std::max(0,std::min((int)tex.height-1,ty)):(unsigned)ty&(tex.height-1);
            for(int x=(int)x0;x<(int)x1;x++) {
                int tx=sx+x-(int)x0;
                tx=desc.cms&2?std::max(0,std::min((int)tex.width-1,tx)):(unsigned)tx&(tex.width-1);
                if(tex.format==PVR_TXRFMT_RGB565 || (source[ty*tex.width+tx]&0x8000)) {
                    unsigned pixel=y*W+x;written[pixel/32]|=1u<<(pixel%32);
                }
            }
        }
    }
#endif
#ifdef DC_RASTER_TEXEL_SCALE
    // Texture baking restores the pre-draw combiner. Preserve the state of
    // the last software sample, including hot-cache draws and unused TEXEL1.
    // Repeated additions retain the scalar rectangle's rounding order.
    int ix0=(int)x0,iy0=(int)y0,ix1=(int)x1,iy1=(int)y1;
    if(ix0<ix1 && iy0<iy1) {
        for(int y=iy0;y+1<iy1;++y)stateT+=dt;
        for(int x=ix0;x+1<ix1;++x)stateS+=ds;
        Color32 texel=r->tx->getTexel(stateS,stateT,tile,r->tf);
        if(r->cycleType)r->cc->combine2(texel,r->tx->getTexel(stateS,stateT,(tile+1)&7,r->tf));
        else r->cc->combine1(texel);
    }
#endif
    return true;
}
