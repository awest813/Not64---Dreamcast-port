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

    // Coordinate translation preserves clamp-before-mask ordering, mirrors,
    // negative coordinates, and independent S/T modes.
    {
        TX mapped(info), reference(info);
        for(unsigned i=0;i<64;i++) byte(0x800+i,i*3+1);
        mapped.setTImg(4,1,8,ram+0x800); reference.setTImg(4,1,8,ram+0x800);
        mapped.setTile(4,1,1,0,0,0,0,0,0,0,0,0);
        reference.setTile(4,1,1,0,0,0,0,0,0,0,0,0);
        mapped.setTileSize(0,0,7,7,0); reference.setTileSize(0,0,7,7,0);
        mapped.loadTile(0,0,0,7,7); reference.loadTile(0,0,0,7,7);
        for(int sm=0;sm<4;sm++) for(int tm=0;tm<4;tm++)
        for(int mask=0;mask<=3;mask++) {
            mapped.setTile(4,1,1,0,0,0,tm,mask,0,sm,mask,0);
            for(int y=-17;y<=17;y++) for(int x=-17;x<=17;x++) {
                int coords[2]={x,y},modes[2]={sm,tm};
                for(int a=0;a<2;a++) {
                    int &q=coords[a];
                    if(modes[a]&2) { if(q<0)q=0; if(q>=8)q=7; }
                    if(mask) {
                        int span=1<<mask; bool mirror=(modes[a]&1)&&(q&span);
                        q&=span-1; if(mirror)q=span-1-q;
                    }
                }
                Color32 expected=reference.getTexel(coords[0],coords[1],0,NULL);
                Color32 actual=mapped.getTexel(x,y,0,NULL);
                CHECK((unsigned)(int)actual==(unsigned)(int)expected);
            }
        }
    }

    // DXT=0 blocks are already arranged for TMEM's odd-row word swap.
    // Normal block/tile uploads must sample identically to that layout.
    for(unsigned width=8;width<=16;width*=2) {
        TX pre(info), block(info), tiled(info);
        for(unsigned i=0;i<width*4;i++) {
            byte(0x900+i,i*3);
            byte(0x940+(i^(((i/width)&1)?4:0)),i*3);
        }
        TX *textures[]={&pre,&block,&tiled};
        for(unsigned i=0;i<3;i++) {
            textures[i]->setTImg(4,1,width,ram+(i==0?0x940:0x900));
            textures[i]->setTile(4,1,width/8,0,0,0,2,0,0,2,0,0);
            textures[i]->setTileSize(0,0,width-1,3,0);
        }
        pre.loadBlock(0,0,0,width*4-1,0);
        block.loadBlock(0,0,0,width*4-1,2048/(width/8));
        tiled.loadTile(0,0,0,width-1,3);
        for(auto txp:textures) for(int y=0;y<4;y++) for(int x=0;x<(int)width;x++) {
            unsigned v=(y*width+x)*3;
            CHECK((unsigned)(int)txp->getTexel(x,y,0,NULL)==v*0x01010101u);
        }
    }

    // One-cycle rendering uses the second mux cycle: texture times shade.
    CC combiner;
    combiner.setShade(Color32(128,255,0,255));
    combiner.setCombineMode((1u<<5)|4u,(15u<<24)|(7u<<21)|(7u<<18)|(7u<<6)|(7u<<3)|1u);
    c=combiner.combine1(Color32(255,128,128,255));
    CHECK((unsigned)(int)c==0x808000ffu);
    c=Color32(-10,300,20,400); CHECK((unsigned)(int)c==0x00ff14ffu);
    // Zero RGB products must still evaluate alpha independently, in both
    // cycles, and a subsequent mux change must restore the full equation.
    {
        CC direct;
        direct.setPrimColor(0x315579ab,0,0);
        direct.setEnvColor(0x24688ace);
        for(unsigned equal=0;equal<2;equal++) {
            unsigned a=equal?1:2, b=1, factor=equal?4:31;
            unsigned hi=(a<<20)|(factor<<15)|(1<<12)|(4<<9)|(a<<5)|factor;
            unsigned lo=(b<<28)|(b<<24)|(1<<21)|(4<<18)|(3<<15)|
                        (7<<12)|(7<<9)|(5<<6)|(7<<3)|7;
            direct.setCombineMode(hi,lo);
            direct.setShade(Color32(43,123,221,255));
            c=direct.combine2(Color32(200,17,88,91),Color32(33,44,55,66));
            CHECK((unsigned)(int)c==0x24688a5bu);
            c=direct.combine1(Color32(9,10,11,37));
            CHECK((unsigned)(int)c==0x24688a25u);
            // Feed the first cycle's direct result into the second cycle.
            direct.setCombineMode(hi,(lo & ~(7u<<6)) | (0u<<6));
            c=direct.combine2(Color32(200,17,88,91),Color32(33,44,55,66));
            CHECK((unsigned)(int)c==0x3155795bu);
        }
        direct.setCombineMode((1u<<5)|4u,(15u<<24)|(7u<<21)|(7u<<18)|(7u<<6)|(7u<<3)|1u);
        direct.setShade(Color32(128,255,0,255));
        c=direct.combine1(Color32(255,128,128,255));
        CHECK((unsigned)(int)c==0x808000ffu);
    }
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

    // Stadium 2's untextured two-cycle depth triangles. Both scanline halves
    // must draw; alpha/depth rejection must preserve both destination planes.
    {
        RDP tri(info);
        tri.setCImg(0,2,8,ram+0x24000); tri.setZImg(ram+0x25000);
        tri.setScissor(0,0,8,8,0); tri.setOtherMode_h(20,1);
        tri.setOtherMode_l(3,0x30>>3); tri.setOtherMode_l(0,1);
        tri.setBlendColor(128);
        unsigned hi=(15u<<20)|(31u<<15)|(7u<<12)|(7u<<9)|(15u<<5)|31u;
        unsigned lo=(15u<<28)|(15u<<24)|(7u<<21)|(7u<<18)|(4u<<15)|(7u<<12)|(4u<<9)|(7u<<3);
        tri.setCombineMode(hi,lo); // shade, then combined
        Vektor<float,4> a,b,c;
        a[0]=1; a[1]=0; b[0]=7; b[1]=3; c[0]=1; c[1]=7;
        Color32 red(255,0,0,255), transparent(255,0,0,0), blue(0,0,255,255);
        std::memset(ram+0x24000,0,128); std::memset(ram+0x25000,0xff,128);
        tri.tri_shade_zbuff(a,b,c,transparent,transparent,transparent,100,100,100);
        CHECK(half(0x24000+2*(1*8+2))==0 && half(0x25000+2*(1*8+2))==0xffff);
        tri.tri_shade_zbuff(a,b,c,red,red,red,100,100,100);
        CHECK(half(0x24000+2*(1*8+2))==0xf801 && half(0x24000+2*(4*8+2))==0xf801);
        unsigned depth=half(0x25000+2*(1*8+2)); CHECK(depth!=0xffff);
        tri.tri_shade_zbuff(a,b,c,blue,blue,blue,200,200,200);
        CHECK(half(0x24000+2*(1*8+2))==0xf801 && half(0x25000+2*(1*8+2))==depth);
        tri.setPrimColor(0x0000ffff,0,0);
        tri.setCombineMode(hi,(lo&~((7u<<15)|(7u<<9)))|(3u<<15)|(3u<<9));
        tri.tri_shade_zbuff(a,b,c,red,red,red,50,50,50);
        CHECK(half(0x24000+2*(1*8+2))==0x003f);
        CHECK(half(0x24000+2*7)==0); // outside the triangle
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
        // E5 swaps the derivatives: clipped x=1 reads source row 1,
        // while moving down advances S. Red/green over blue/white transposes.
        rect.setOtherMode_h(20,1); rect.setScissor(1,0,2,2,0);
        word(0x22000,0); word(0x22008,0);
        rect.texRect(0,0,0,2,2,0,0,1,1,true);
        CHECK(half(0x22000)==0 && half(0x22002)==0x003f);
        CHECK(half(0x22008)==0 && half(0x2200a)==0xffff);
    }

    // A constant fill still blends against each destination pixel and applies
    // position-dependent alpha dither. Hoisting the combiner must preserve both.
    {
        RDP rect(info);
        BL reference(info);
        rect.setCImg(0,2,4,ram+0x23000);
        reference.setCImg(0,2,4,ram+0x23100);
        rect.setScissor(0,0,4,4,0);
        rect.setOtherMode_h(20,0);
        rect.setOtherMode_l(3,0x00404000>>3);
        reference.setBlender(0x00404000);
        rect.setOtherMode_l(0,3);
        reference.setAlphaCompare(3);
        rect.setCombineMode((15u<<5)|31u,
            (15u<<24)|(7u<<21)|(7u<<18)|(3u<<6)|(7u<<3)|3u);
        rect.setPrimColor(0xff402080,0,0);
        for(unsigned i=0;i<8;i++) {
            unsigned v=(i&1)?0x07c1003fu:0x003f07c1u;
            word(0x23000+i*4,v); word(0x23100+i*4,v);
        }
        rect.fillRect(0,0,4,4);
        for(int y=0;y<4;y++) for(int x=0;x<4;x++)
            reference.cycle1ModeDraw(x,y,Color32(255,64,32,128));
        CHECK(std::memcmp(ram+0x23000,ram+0x23100,32)==0);
        CHECK(half(0x23000)!=half(0x23002));
    }

    // Inclusive fill endpoints must not extend an exclusive scissor by a row.
    // Stadium 2 fills through y=240 with a y<240 scissor; the following
    // allocation contains its framebuffer descriptor, not drawable pixels.
    {
        RDP rect(info);
        rect.setCImg(0,2,320,ram+0x40000);
        rect.setScissor(0,0,320,240,0);
        rect.setOtherMode_h(20,3);
        rect.setFillColor(0x00010001);
        word(0x40000+320*240*2,0x803b4a00);
        rect.fillRect(0,152,117,240);
        CHECK(half(0x40000+(239*320+116)*2)==1);
        CHECK(*(unsigned *)(ram+0x40000+320*240*2)==0x803b4a00);
        // Oversized rectangles clip on the right as well as the bottom.
        rect.setCImg(0,2,8,ram+0x28000);
        rect.setScissor(0,0,4,2,0);
        rect.fillRect(0,0,8,8);
        CHECK(half(0x28000+3*2)==1 && half(0x28000+4*2)==0);
        CHECK(half(0x28000+(8+3)*2)==1 && half(0x28000+16*2)==0);
    }

    // Copy-mode cutouts use source RGBA16 alpha even with blend alpha zero.
    // Preserve the copied alpha bit for later framebuffer-as-texture reads.
    {
        BL copy(info);
        copy.setCImg(0,2,4,ram+0x29000);
        word(0x29000,0x07c107c1);
        copy.setBlendColor(0);
        copy.setAlphaCompare(1);
        copy.copyModeDraw(0,0,Color32(255,0,0,0));
        CHECK(half(0x29000)==0x07c1);
        copy.copyModeDraw(1,0,Color32(255,0,0,255));
        CHECK(half(0x29002)==0xf801);
        copy.setAlphaCompare(2); // compare disabled, dither bit alone
        copy.setBlendColor(0xffffffff);
        copy.copyModeDraw(0,0,Color32(0,0,255,0));
        CHECK(half(0x29000)==0x003e);
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
    // The E5 decoder consumes both following half-command payloads.
    word(0x3000,0xe5000000); word(0x3004,0);
    word(0x3008,0xe1000000); word(0x300c,0);
    word(0x3010,0xf1000000); word(0x3014,0x04000400);
    word(0x3018,0xdf000000); word(0x301c,0);
    { RSP flipped(info); CHECK(flipped.succeeded()); }
    task[12]=0x3ffff0; word(0x3ffff0,0xe5000000); word(0x3ffff4,0);
    { RSP truncated(info); CHECK(!truncated.succeeded()); }
    task[12]=0x3000;
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
