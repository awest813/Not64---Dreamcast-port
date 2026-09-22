#include "../../platform/dc_vi.h"
#include "../../platform/dc_video.h"
#include <stdio.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"HIRES VI FAIL line %d: %s\n",__LINE__,#c); return 1; } } while(0)
static uint8_t memory[4*1024*1024];
static dc_vi_frame frame;
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
    puts("High-resolution field weave, fractional offset, bounds and presentation PASS");
    return 0;
}
