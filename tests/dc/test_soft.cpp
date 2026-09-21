#include <cstdio>
#include <cstring>
#include <cmath>
#include "../../mupen64_soft_gfx/rsp.h"
#include "../../mupen64_soft_gfx/tx.h"

static unsigned char ram[0x400000], dmem[4096];
static DWORD intr;
static unsigned interrupts;
static void interrupt() { ++interrupts; }
static void byte(unsigned at, unsigned v) { ram[at ^ 3] = v; }
static void word(unsigned at, unsigned v) { std::memcpy(ram+at,&v,4); }
static unsigned half(unsigned at) { unsigned short v; std::memcpy(&v,ram+(at^2),2); return v; }
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c); return 1; } } while(0)

int main() {
    GFX_INFO info={}; info.RDRAM=ram; info.DMEM=dmem; info.MI_INTR_REG=&intr;
    info.MemoryBswaped=TRUE; info.CheckInterrupts=interrupt;
    TX tx(info);
    // RGBA16 loads preserve both adjacent pixels and clamp the final texel.
    byte(0x100,0xf8); byte(0x101,1); byte(0x102,0x07); byte(0x103,0xc1);
    tx.setTImg(0,2,2,ram+0x100); tx.setTile(0,2,1,0,0,0,2,0,0,2,0,0);
    tx.loadBlock(0,0,0,1,0); tx.setTileSize(0,0,1,0,0);
    Color32 c=tx.getTexel(0,0,0,NULL); CHECK((unsigned)(int)c==0xff0000ffu);
    c=tx.getTexel(1,0,0,NULL); CHECK((unsigned)(int)c==0x00ff00ffu);
    c=tx.getTexel(2,0,0,NULL); CHECK((unsigned)(int)c==0x00ff00ffu);
    c=tx.getTexel(0,0,8,NULL); CHECK((unsigned)(int)c==0xff0000ffu);
    // Copy mode must accept fractional coordinates without dereferencing TF.
    c=tx.getTexel(0.25f,0,0,NULL); CHECK((unsigned)(int)c==0xff0000ffu);
    // I4 intensity expands to RGB and alpha.
    byte(0x200,0xf5);
    tx.setTImg(4,0,2,ram+0x200); tx.setTile(4,0,1,0,0,0,2,0,0,2,0,0);
    tx.loadBlock(0,0,0,1,0); c=tx.getTexel(1,0,0,NULL); CHECK((unsigned)(int)c==0x55555555u);
    // CI4 bank selection and palette loads use entries, not eight-byte overreads.
    byte(0x300,0x12); byte(0x400,0xf8); byte(0x401,1); byte(0x402,0x07); byte(0x403,0xc1);
    tx.setTextureLUT(2); tx.setTile(0,2,0,256+17,7,0,0,0,0,0,0,0);
    tx.setTImg(0,2,2,ram+0x400); tx.loadTLUT(7,2);
    tx.setTImg(2,0,2,ram+0x300); tx.setTile(2,0,1,0,0,1,2,0,0,2,0,0);
    tx.loadBlock(0,0,0,1,0);
    c=tx.getTexel(0,0,0,NULL); CHECK((unsigned)(int)c==0xff0000ffu);
    c=tx.getTexel(1,0,0,NULL); CHECK((unsigned)(int)c==0x00ff00ffu);
    // A transfer beyond RDRAM must preserve the previous texture.
    tx.setTImg(2,0,2,ram+0x400000); tx.loadBlock(0,0,0,1,0);
    c=tx.getTexel(0,0,0,NULL); CHECK((unsigned)(int)c==0xff0000ffu);

    // One-cycle rendering uses the second mux cycle: texture times shade.
    CC combiner;
    combiner.setShade(Color32(128,255,0,255));
    combiner.setCombineMode((1u<<5)|4u,(15u<<24)|(7u<<21)|(7u<<18)|(7u<<6)|(7u<<3)|1u);
    c=combiner.combine1(Color32(255,128,128,255));
    CHECK((unsigned)(int)c==0x808000ffu);
    c=Color32(-10,300,20,400); CHECK((unsigned)(int)c==0x00ff14ffu);
    TF filter; filter.setTextureFilter(2);
    Color32 corners[4]={Color32(255,0,0,255),Color32(0,255,0,255),Color32(0,0,255,255),Color32(255,255,255,255)};
    float distances[4]={0.125f,0.625f,1.125f,0.625f};
    c=filter.filter(corners,distances); CHECK((unsigned)(int)c==0xbf7f3fffu);

    // Optimized point sampling must match the four-corner reference,
    // including halfway ties, negative coordinates and clamped edges.
    {
        TX point(info); TF nearest;
        byte(0x600,0xf8); byte(0x601,0); byte(0x602,0x07); byte(0x603,0xc1);
        byte(0x604,0); byte(0x605,0x3f); byte(0x606,0xff); byte(0x607,0xff);
        point.setTImg(0,2,2,ram+0x600);
        point.setTile(0,2,1,0,0,0,2,0,0,2,0,0);
        point.setTileSize(0,0,1,1,0); point.loadTile(0,0,0,1,1);
        for(int y=-8;y<=16;y++) for(int x=-8;x<=16;x++) {
            float s=x/4.0f,t=y/4.0f,fs=floorf(s),ft=floorf(t),dx=s-fs,dy=t-ft;
            Color32 q[4]={point.getTexel(fs,ft,0,NULL),point.getTexel(fs+1,ft,0,NULL),
                          point.getTexel(fs+1,ft+1,0,NULL),point.getTexel(fs,ft+1,0,NULL)};
            float d[4]={dx*dx+dy*dy,(1-dx)*(1-dx)+dy*dy,
                        (1-dx)*(1-dx)+(1-dy)*(1-dy),dx*dx+(1-dy)*(1-dy)};
            CHECK((int)point.getTexel(s,t,0,&nearest)==(int)nearest.filter(q,d));
            for(int mode=2;mode<=3;mode++) {
                TF filtered; filtered.setTextureFilter(mode);
                float u=(d[0]-d[1]+1)*0.5f, v=(d[0]-d[3]+1)*0.5f;
                float w[4]={};
                if(mode==3) for(int i=0;i<4;i++) w[i]=0.25f;
                else if(u+v<=1) { w[0]=1-u-v; w[1]=u; w[3]=v; }
                else { w[2]=u+v-1; w[1]=1-v; w[3]=1-u; }
                Color32 expected(0,0,0,0); float alpha=0;
                // Retain the original all-four-corner accumulation as oracle.
                for(int i=0;i<4;i++) { expected+=q[i]*w[i]; alpha+=q[i].getAlpha()*w[i]; }
                expected.setAlpha(alpha);
                if(s==fs && t==ft) expected=q[0];
                Color32 actual=point.getTexel(s,t,0,&filtered);
                CHECK(actual.getR()==expected.getR() && actual.getG()==expected.getG() &&
                      actual.getB()==expected.getB() && actual.getAlpha()==expected.getAlpha());
            }
        }
    }

    // Transparent texels must not overwrite either the framebuffer or depth.
    BL blender(info); blender.setCImg(0,2,4,ram+0x20000); blender.setZImg(ram+0x21000);
    word(0x20000,0x07c107c1); word(0x21000,0xffffffff);
    blender.setAlphaCompare(1); blender.setBlendColor(128);
    blender.setBlender(0x30); // Z compare and update, opaque color.
    blender.cycle1ModeDraw(0,0,Color32(255,0,0,0),100);
    CHECK(half(0x20000)==0x07c1 && half(0x21000)==0xffff);
    blender.cycle1ModeDraw(0,0,Color32(255,0,0,128),100);
    CHECK(half(0x20000)==0xf801 && half(0x21000)!=0xffff);
    blender.setAlphaCompare(0); blender.setBlender(0x3000);
    blender.cycle2ModeDraw(0,0,Color32(0,0,255,0));
    CHECK(half(0x20000)==0xf801);
    blender.setBlender(0x00404000); // pixel*alpha + memory*(1-alpha)
    blender.cycle1ModeDraw(0,0,Color32(0,0,255,128));
    CHECK((half(0x20000)&0xf800)==0x7800 && (half(0x20000)&0x3e)==0x20);
    // Changing only force_bl must refresh the framebuffer-read decision.
    blender.setBlender(0x00400000);
    word(0x20000,0x07c107c1);
    blender.cycle1ModeDraw(0,0,Color32(255,0,0,128));
    CHECK(half(0x20000)==0xf801);
    blender.setBlender(0x00404000);
    word(0x20000,0x07c107c1);
    blender.cycle1ModeDraw(0,0,Color32(255,0,0,128));
    CHECK((half(0x20000)&0xf800)==0x8000 && (half(0x20000)&0x7c0)==0x3c0);
    // A direct memory-color source needs a read even without forced blend.
    blender.setBlender(0x40000000);
    word(0x20000,0x07c107c1);
    blender.cycle1ModeDraw(0,0,Color32(255,0,0,255));
    CHECK(half(0x20000)==0x07c1);
    blender.setBlender(0x10000000); // second cycle selects memory directly
    blender.cycle2ModeDraw(0,0,Color32(255,0,0,255));
    CHECK(half(0x20000)==0x07c1);
    blender.setBlender(0x00400000); // first cycle blends even without force_bl
    blender.cycle2ModeDraw(0,0,Color32(255,0,0,128));
    CHECK((half(0x20000)&0xf800)==0x8000 && (half(0x20000)&0x7c0)==0x3c0);
    {
        BL alpha(info); alpha.setCImg(0,2,4,ram+0x20000);
        alpha.setBlender(0x00044000); // memory alpha, but no memory RGB
        alpha.cycle1ModeDraw(0,0,Color32(64,0,0,128));
        CHECK((half(0x20000)&0xf800)==0x6000);
    }

    // Two-cycle texture rectangles (used by Zelda's opening): cycle 0
    // takes TEXEL0, cycle 1 takes COMBINED. Clip away red, retain green.
    {
        RDP rect(info);
        rect.setCImg(0,2,4,ram+0x22000);
        rect.setScissor(1,0,2,1,0);
        rect.setOtherMode_h(20,1);
        rect.setOtherMode_l(3,0);
        rect.setOtherMode_h(12,0);
        rect.setTImg(0,2,2,ram+0x100);
        rect.setTile(0,2,1,0,0,0,2,0,0,2,0,0);
        rect.setTileSize(0,0,1,0,0);
        rect.loadTile(0,0,0,1,0);
        rect.setCombineMode((15u<<20)|(31u<<15)|(7u<<12)|(7u<<9)|(15u<<5)|31u,
                            (15u<<28)|(15u<<24)|(7u<<21)|(7u<<18)|(1u<<15)|(7u<<12)|(1u<<9)|(7u<<3));
        rect.texRect(0,0,0,2,1,0,0,1,1);
        CHECK(half(0x22000)==0 && half(0x22002)==0x07c1);
        CHECK(half(0x22004)==0);
        // Copy rectangles advance one texel per four derivative units,
        // including the pixels discarded by the scissor.
        word(0x22000,0);
        rect.setOtherMode_h(20,2);
        rect.texRect(0,0,0,1,0,0,0,4,1);
        CHECK(half(0x22000)==0 && (half(0x22002)&0xfffe)==0x07c0);
        byte(0x104,0); byte(0x105,0x3f);
        byte(0x106,0xff); byte(0x107,0xff);
        rect.setTileSize(0,0,1,1,0);
        rect.loadTile(0,0,0,1,1);
        rect.setScissor(1,1,2,2,0);
        rect.texRect(0,0,0,1,1,0,0,4,1);
        CHECK(half(0x22008)==0 && (half(0x2200a)&0xfffe)==0xfffe);
    }

    // Execute an actual F3DEX2 display list: 4x4 red fill and FullSync.
    const char *uc="RSP Gfx ucode F3DEX fifo 2.08";
    for(unsigned i=0;i<std::strlen(uc);i++) byte(0x2000+i,uc[i]);
    unsigned long *task=(unsigned long *)(dmem+0xfc0);
    task[6]=0x2000; task[7]=2048; task[12]=0x3000;
    const unsigned commands[][2]={
        {0xff100003,0x10000}, {0xed000000,0x00010010},
        {0xef300000,0}, {0xf7000000,0xf801f801},
        {0xf600c00c,0}, {0xe9000000,0}, {0xdf000000,0}
    };
    for(unsigned i=0;i<sizeof(commands)/sizeof(commands[0]);i++) {
        word(0x3000+i*8,commands[i][0]); word(0x3004+i*8,commands[i][1]);
    }
    RSP rsp(info);
    CHECK(rsp.succeeded());
    CHECK(intr==0x20 && interrupts==1);
    for(unsigned i=0;i<16;i++) CHECK(half(0x10000+i*2)==0xf801);
    CHECK(half(0xfffe)==0 && half(0x10020)==0);
    // CULLDL returns from a child list, rather than ending its parent.
    word(0x3000,0xde000000); word(0x3004,0x3100);
    word(0x3008,0xe9000000); word(0x300c,0);
    word(0x3010,0xdf000000); word(0x3014,0);
    word(0x3100,0x01001002); word(0x3104,0x4000); // one vertex at slot 0
    word(0x3108,0x03000000); word(0x310c,0);
    word(0x3110,0x11000000); word(0x3114,0); // must be culled
    byte(0x4000,0); byte(0x4001,2); // x=2, outside identity clip volume
    intr=interrupts=0;
    { RSP culled(info); CHECK(culled.succeeded() && intr==0x20 && interrupts==1); }
    byte(0x4001,0); intr=interrupts=0;
    { RSP visible(info); CHECK(!visible.succeeded() && interrupts==0); }
    // Outside vertices on opposite sides still span the visible volume.
    // Cull only when every vertex shares at least one outside plane.
    word(0x3100,0x01002004); word(0x310c,2); // slots 0 through 1
    byte(0x4001,2); byte(0x4010,0xff); byte(0x4011,0xfe);
    intr=interrupts=0;
    { RSP spanning(info); CHECK(!spanning.succeeded() && interrupts==0); }
    byte(0x4010,0); byte(0x4011,3);
    { RSP outside(info); CHECK(outside.succeeded() && intr==0x20 && interrupts==1); }
    // An unloaded endpoint and a reversed range must stop safely.
    intr=interrupts=0; word(0x310c,4);
    { RSP unloaded(info); CHECK(!unloaded.succeeded() && interrupts==0); }
    word(0x3108,0x03000002); word(0x310c,0);
    { RSP reversed(info); CHECK(!reversed.succeeded() && interrupts==0); }
    intr=interrupts=0; word(0x3000,0x11000000);
    { RSP bad(info); CHECK(!bad.succeeded() && intr==0 && interrupts==0); }
    task[12]=0x3ffffc;
    { RSP bad(info); CHECK(!bad.succeeded()); }
    task[12]=0x3000; word(0x3000,0xde000000); word(0x3004,0x3000);
    { RSP bad(info); CHECK(!bad.succeeded()); }
    std::puts("Software graphics: texture formats, bounds, F3DEX2 fill and FullSync PASS");
}
