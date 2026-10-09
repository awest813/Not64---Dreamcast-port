#include "../../platform/dc_vi.h"
#include "../../platform/dc_video.h"
#include <stdio.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"HIRES VI FAIL line %d: %s\n",__LINE__,#c); return 1; } } while(0)
static uint8_t memory[4*1024*1024];
static uint8_t unaligned_memory[257];
static dc_vi_frame frame;
static dc_vi_frame cached_frame, reference_frame;
static uint16_t output[640*480];
static void pixel(unsigned address, uint16_t color) { memcpy(memory+(address^2),&color,2); }
int main(void) {
    dc_vi_state s={0x13056,0x369f80,640,0x006c02ec,0x002301fd,0x400,0x02000800,0};
    for(unsigned y=0;y<474;y++) for(unsigned x=0;x<640;x++)
        pixel(s.origin+(y*640+x)*2, y&1?0x003f:0xf801);
    CHECK(dc_vi_convert(&s,memory,sizeof(memory),&frame)==DC_VI_READY);
    CHECK(frame.width==640 && frame.height==474 && frame.interlaced);
    CHECK(frame.pixels[0]==0x8010 && frame.pixels[639]==0x8010);
    CHECK(frame.pixels[DC_VI_TEXTURE_WIDTH]==0); // other field not fabricated
    s.field=1;
    CHECK(dc_vi_convert(&s,memory,sizeof(memory),&frame)==DC_VI_READY);
    CHECK(frame.pixels[DC_VI_TEXTURE_WIDTH]==0x8010 && frame.pixels[0]==0x8010);
    CHECK(frame.pixels[473*DC_VI_TEXTURE_WIDTH+639]==0x8010);
    CHECK(dc_video_expand_2x(&frame,output,640*480));
    CHECK(output[3*640]==0x8010 && output[476*640+639]==0x8010 && output[0]==0);
    CHECK(dc_video_pvr_upload_bytes(640,474)==474*DC_VI_TEXTURE_WIDTH*2);
    // Last source row is required by the fractional interpolation.
    CHECK(dc_vi_convert(&s,memory,s.origin+473*640*2,&frame)==DC_VI_INVALID);
    CHECK(!frame.width && !frame.height);
    s.y_scale=0x800; s.field=0;
    CHECK(dc_vi_convert(&s,memory,sizeof(memory),&frame)==DC_VI_READY);
    CHECK(frame.pixels[0]==0xf800 && frame.pixels[DC_VI_TEXTURE_WIDTH]==0);
    s.field=1; s.origin+=1280;
    CHECK(dc_vi_convert(&s,memory,sizeof(memory),&frame)==DC_VI_READY);
    CHECK(frame.pixels[DC_VI_TEXTURE_WIDTH]==0x001f && frame.pixels[0]==0xf800);
    // RGBA8888 interlace uses word-swapped words, not halfword addressing.
    s=(dc_vi_state){0x43,0,2,2,4,1024,2048,0};
    for(unsigned i=0;i<8;i++) {
        uint32_t color=0x00ff00ff; memcpy(memory+i*4,&color,4);
    }
    CHECK(dc_vi_convert(&s,memory,sizeof(memory),&frame)==DC_VI_READY);
    CHECK(frame.width==2 && frame.height==4 && frame.pixels[0]==0x07e0);
    CHECK(frame.pixels[DC_VI_TEXTURE_WIDTH]==0); // dimensions reset old fields
    s.status=0;
    CHECK(dc_vi_convert(&s,memory,sizeof(memory),&frame)==DC_VI_BLANK);
    s.status=0x43; s.field=1;
    CHECK(dc_vi_convert(&s,memory,sizeof(memory),&frame)==DC_VI_READY);
    CHECK(frame.pixels[0]==0 && frame.pixels[DC_VI_TEXTURE_WIDTH]==0x07e0);
    {
        dc_vi_padding_cache cache={0};
        for(unsigned n=0;n<32;n++) {
            // Interlaced history survives; normal padding reuse is invalidated
            // by every field/mode/blank transition.
            dc_vi_state sequence={n%4<2?0x43:3,0,2,2,4,1024,2048,n&1};
            if(n%7==0)sequence.status=0;
            if(n%11==0)sequence.origin=sizeof(memory);
            for(unsigned i=0;i<8;i++) {
                uint32_t c=(n&1)?0xff0000ff:0x00ff00ff;memcpy(memory+i*4,&c,4);
            }
            dc_vi_result expected=dc_vi_convert(&sequence,memory,sizeof(memory),&reference_frame);
            CHECK(dc_vi_convert_cached(&sequence,memory,sizeof(memory),&cached_frame,&cache)==expected);
            CHECK(memcmp(&cached_frame,&reference_frame,sizeof(cached_frame))==0);
            if(expected!=DC_VI_READY || cached_frame.interlaced)CHECK(!cache.valid);
        }
    }
    /* Native aligned reads must match byte-safe reads for both pixel widths,
     * fractional interpolation, halfword crops, and retained field history. */
    for(unsigned bytes=2;bytes<=4;bytes+=2)for(unsigned n=0;n<48;n++) {
        for(unsigned i=0;i<256;i++)memory[i]=(uint8_t)(i*37+n*23);
        memcpy(unaligned_memory+1,memory,256);
        const unsigned fractions[]={0,1,31,511,512,1023};
        s=(dc_vi_state){bytes==2?0x42:0x43,bytes*(n%2),8+(n/24),3+(n/12)%2,4,1024,
            (fractions[n%6]<<16)|(n/24?2048:1024),n&1};
        CHECK(dc_vi_convert(&s,memory,sizeof(memory),&reference_frame)==DC_VI_READY);
        CHECK(dc_vi_convert(&s,unaligned_memory+1,256,&cached_frame)==DC_VI_READY);
        CHECK(memcmp(&reference_frame,&cached_frame,sizeof(reference_frame))==0);
    }
    /* Every RGBA5551 code, paired with a permutation of all codes, exercises
     * both packed lanes and each channel's rounded half-row average. */
    s=(dc_vi_state){0x42,0,640,640,206,1024,0x02000800,0};
    for(unsigned y=0;y<103;y++)for(unsigned x=0;x<640;x++) {
        unsigned n=y*640+x;
        pixel((y*2*640+x)*2,(uint16_t)n);
        pixel(((y*2+1)*640+x)*2,(uint16_t)(n*40503u+97u));
    }
    CHECK(dc_vi_convert(&s,memory,sizeof(memory),&frame)==DC_VI_READY);
    for(unsigned y=0;y<103;y++)for(unsigned x=0;x<640;x++) {
        unsigned a=(uint16_t)(y*640+x),b=(uint16_t)((y*640+x)*40503u+97u);
        unsigned ag=(a>>6)&31u,bg=(b>>6)&31u;
        unsigned r=((a>>11)+(b>>11)+1)>>1;
        unsigned g=(2*ag+(ag>>4)+2*bg+(bg>>4)+1)>>1;
        unsigned blue=(((a>>1)&31u)+((b>>1)&31u)+1)>>1;
        CHECK(frame.pixels[y*2*DC_VI_TEXTURE_WIDTH+x]==((r<<11)|(g<<5)|blue));
    }
    puts("High-resolution field weave, fractional offset, bounds, presentation and padding reuse PASS");
    return 0;
}
