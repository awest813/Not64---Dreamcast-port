#include "../../platform/dc_gfx.h"
#include "../../platform/dc_memory.h"
#include "../../main/plugin.h"
#include "../../gc_memory/memory.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#ifdef DC_INTERP_MEMORY
#include "../../platform/dc_interp_memory.h"
static unsigned memory_handler_calls;
static void memory_handler_probe(void) { ++memory_handler_calls; }
#endif

uint32_t rsp_probe_word(const unsigned char *, unsigned);
uint16_t rsp_probe_half(const unsigned char *, unsigned);
uint8_t rsp_probe_byte(const unsigned char *, unsigned);
size_t rsp_info_size(void);
size_t rsp_info_callback_offset(void);
extern int stop;
#ifdef DC_VI_HIGHRES
extern int vi_field;
#endif

static unsigned interrupt_calls;
static void interrupt_probe(void) { ++interrupt_calls; }
#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); return 1; \
} } while (0)

static GFX_INFO fixture(void)
{
    GFX_INFO g = {0};
    g.MemoryBswaped = TRUE;
    g.RDRAM = rdramb;
    g.DMEM = SP_DMEMb;
    g.IMEM = SP_IMEMb;
    g.MI_INTR_REG = &MI_register.mi_intr_reg;
    g.DPC_START_REG = &dpc_register.dpc_start;
    g.DPC_END_REG = &dpc_register.dpc_end;
    g.DPC_CURRENT_REG = &dpc_register.dpc_current;
    g.DPC_STATUS_REG = &dpc_register.dpc_status;
    g.VI_STATUS_REG = &vi_register.vi_status;
    g.VI_ORIGIN_REG = &vi_register.vi_origin;
    g.VI_WIDTH_REG = &vi_register.vi_width;
    g.VI_H_START_REG = &vi_register.vi_h_start;
    g.VI_V_START_REG = &vi_register.vi_v_start;
    g.VI_X_SCALE_REG = &vi_register.vi_x_scale;
    g.VI_Y_SCALE_REG = &vi_register.vi_y_scale;
    g.CheckInterrupts = interrupt_probe;
    return g;
}

int main(void)
{
    GFX_INFO g = fixture(), bad;
    dc_gfx_stats s;
    uint8_t b;
    uint16_t h;
    uint32_t w;
    FrameBufferInfo buffers[6];
    unsigned i;

    CHECK(sizeof(rdram) == 4u * 1024u * 1024u);
    CHECK(SP_IMEMb - SP_DMEMb == 4096);
    CHECK(rsp_info_size() == sizeof(RSP_INFO));
    CHECK(rsp_info_callback_offset() == offsetof(RSP_INFO, CheckInterrupts));
    CHECK(init_memory() == 0);
#ifdef DC_INTERP_MEMORY
    /* A framebuffer/device hook installed in an ordinary RAM page must still
     * run at every width. Address-only shortcuts would bypass these calls. */
    {
        void (**saved)() = rwmem[0x8000];
        void (*hooks[8])() = {memory_handler_probe,memory_handler_probe,
            memory_handler_probe,memory_handler_probe,memory_handler_probe,
            memory_handler_probe,memory_handler_probe,memory_handler_probe};
        rwmem[0x8000] = hooks;
        address=0x80001000u;
        read_word_in_memory(); read_byte_in_memory();
        read_hword_in_memory(); read_dword_in_memory();
        write_word_in_memory(); write_byte_in_memory();
        write_hword_in_memory(); write_dword_in_memory();
        CHECK(memory_handler_calls == 8);
        rwmem[0x8000] = saved;
        dword=0x123456789abcdef0ull; write_dword_in_memory();
        address=0xa0001000u; dword=0; read_dword_in_memory();
        CHECK(dword == 0x123456789abcdef0ull);
        CHECK(rsp_probe_word(rdramb,0x1000) == 0x12345678u);
        CHECK(rsp_probe_word(rdramb,0x1004) == 0x9abcdef0u);
    }
#endif

    closeDLL_gfx();
    romOpen_gfx();
    CHECK(!dc_gfx_is_open());
    CHECK(!dc_gfx_read_u32(0, &w));
    bad = g; bad.MemoryBswaped = FALSE;
    CHECK(!initiateGFX(bad));
    bad = g; bad.VI_ORIGIN_REG = NULL;
    CHECK(!initiateGFX(bad));
    CHECK(initiateGFX(g));
    CHECK(!dc_gfx_is_open());
    romOpen_gfx();
    CHECK(dc_gfx_is_open());

    /* Games may poll CURRENT before programming VSYNC. All bus widths must
     * tolerate reset timing without a host division-by-zero trap. */
    vi_register.vi_v_sync = 0;
    address=0xa4400010u; read_word_in_memory();
    address=0xa4400011u; read_byte_in_memory();
    address=0xa4400010u; read_hword_in_memory();
    address=0xa4400010u; read_dword_in_memory();

    /* Use CPU bus dispatch, not direct RDRAM writes. Adjacent words expose
     * host-width mistakes hidden by the earlier word-zero CPU smoke. */
    for (i = 0; i < 4; ++i) {
        address = 0x80001000u + 4 * i;
        word = 0x12345678u + i;
        write_word_in_memory();
        CHECK(dc_gfx_read_u32(0x1000 + 4 * i, &w) && w == 0x12345678u + i);
        CHECK(rsp_probe_word(rdramb, 0x1000 + 4 * i) == w);
    }
    CHECK(dc_gfx_read_u8(0x1000, &b) && b == 0x12);
    CHECK(dc_gfx_read_u8(0x1003, &b) && b == 0x78);
    CHECK(dc_gfx_read_u16(0x1000, &h) && h == 0x1234);
    CHECK(dc_gfx_read_u16(0x1002, &h) && h == 0x5678);
    address = 0xa0001001u; byte = 0xab; write_byte_in_memory();
    address = 0x80001002u; hword = 0xcdef; write_hword_in_memory();
    CHECK(dc_gfx_read_u32(0x1000, &w) && w == 0x12abcdefu);
    CHECK(rsp_probe_byte(rdramb, 0x1001) == 0xab);
    CHECK(rsp_probe_half(rdramb, 0x1002) == 0xcdef);
    address = 0x80001000u; read_word_in_memory(); CHECK(word == w);
    address = 0x80001001u; read_byte_in_memory(); CHECK(byte == 0xab);
    address = 0x80001002u; read_hword_in_memory(); CHECK(hword == 0xcdef);

    address = 0xa4000ffcu; word = 0x89abcdefu; write_word_in_memory();
    address = 0xa4001000u; word = 0x10203040u; write_word_in_memory();
    CHECK(rsp_probe_word(SP_DMEMb, 0xffc) == 0x89abcdefu);
    CHECK(rsp_probe_word(SP_IMEMb, 0) == 0x10203040u);
    CHECK(*fast_mem_access(0xa4001000u) == 0x10203040u);

    address = 0x803ffffcu; word = 0xaabbccddu; write_word_in_memory();
    CHECK(dc_gfx_read_u8(DC_N64_RDRAM_SIZE - 1, &b) && b == 0xdd);
    CHECK(dc_gfx_read_u16(DC_N64_RDRAM_SIZE - 2, &h) && h == 0xccdd);
    CHECK(dc_gfx_read_u32(DC_N64_RDRAM_SIZE - 4, &w) && w == 0xaabbccddu);
    w = 0xfeedface;
    CHECK(!dc_gfx_read_u32(DC_N64_RDRAM_SIZE, &w) && w == 0xfeedface);
    CHECK(!dc_gfx_read_u8(UINT32_MAX, &b));
    CHECK(!dc_gfx_read_u16(UINT32_MAX - 1, &h));
    CHECK(!dc_gfx_read_u32(UINT32_MAX - 3, &w));
    CHECK(!dc_gfx_read_u16(1, &h));
    CHECK(!dc_gfx_read_u32(2, &w));
    CHECK(!dc_gfx_read_u8(0, NULL));

    processDList(); processDList(); processRDPList();
    updateScreen(); viStatusChanged(); viWidthChanged(); showCFB();
    fBRead(0); fBWrite(0, 4);
    memset(buffers, 0xff, sizeof(buffers));
    fBGetFrameBufferInfo(buffers);
    for (i = 0; i < 6; ++i) CHECK(buffers[i].addr == 0 && buffers[i].size == 0);
    s = dc_gfx_get_stats();
    CHECK(s.display_lists == 2 && s.rdp_lists == 1 && s.vi_updates == 1);
    CHECK(s.status_changes == 1 && s.width_changes == 1 && s.cfb_requests == 1);
    CHECK(s.fb_reads == 1 && s.fb_writes == 1 && s.invalid_calls == 0);
    CHECK(interrupt_calls == 0); /* The diagnostic backend must not signal DP. */
    romClosed_gfx();
    CHECK(!dc_gfx_read_u32(0x1000, &w));
    updateScreen();
    CHECK(dc_gfx_get_stats().invalid_calls == 1);
    romOpen_gfx();
    CHECK(dc_gfx_is_open() && dc_gfx_get_stats().vi_updates == 0);
    closeDLL_gfx(); closeDLL_gfx();
    CHECK(!dc_gfx_is_open() && !dc_gfx_read_u8(0, &b));
    /* VI callback integration: valid frame -> blank -> unsupported -> valid.
     * Blanking must invalidate the capture and must not exhaust the frame limit. */
    CHECK(initiateGFX(g)); romOpen_gfx();
    dc_gfx_set_frame_limit(2);
    stop = 0;
    vi_register.vi_status = 2;
    vi_register.vi_origin = 0x1000;
    vi_register.vi_width = 4;
    vi_register.vi_h_start = 8;
    vi_register.vi_v_start = 4;
    vi_register.vi_x_scale = 512;
    vi_register.vi_y_scale = 1024;
    updateScreen();
    CHECK(dc_gfx_get_frame()->width == 4 && dc_gfx_get_stats().presented == 1 && !stop);
    vi_register.vi_status = 0;
    updateScreen(); updateScreen();
    CHECK(dc_gfx_get_frame()->width == 0 && !stop);
    CHECK(!dc_gfx_capture_ppm("build/dc/should-not-exist.ppm"));
    vi_register.vi_status = 1; /* Reserved pixel type, unsupported in both modes. */
    updateScreen();
    CHECK(dc_gfx_get_stats().unsupported_vi == 1 && !stop);
    vi_register.vi_status = 2;
    vi_register.vi_origin = DC_N64_RDRAM_SIZE;
    updateScreen();
    CHECK(dc_gfx_get_stats().invalid_vi == 1 && !stop);
    vi_register.vi_origin = 0x1000;
    updateScreen();
    CHECK(dc_gfx_get_stats().presented == 2 && stop);
    CHECK(!dc_gfx_capture_ppm("/nonexistent/not64-frame.ppm"));
    dc_gfx_set_frame_limit(0);
    /* Frameskip cannot freeze CPU-produced VI images before a display list.
     * Changing the source or requesting CFB must refresh the scanout. */
    dc_gfx_set_frameskip(3);
    address=0x80001000u; word=0xf801f801u; write_word_in_memory();
    updateScreen();
    CHECK(dc_gfx_get_frame()->pixels[0] == 0xf800u);
    address=0x80001000u; word=0x07c107c1u; write_word_in_memory();
    updateScreen();
    CHECK(dc_gfx_get_frame()->pixels[0] == 0x07e0u);
    showCFB();
    address=0x80001000u; word=0x003f003fu; write_word_in_memory();
    updateScreen();
    CHECK(dc_gfx_get_frame()->pixels[0] == 0x001fu);
    romClosed_gfx(); romOpen_gfx(); updateScreen();
    CHECK(dc_gfx_get_frame()->pixels[0] == 0x001fu);
#ifdef DC_SOFT_GFX
    /* A valid no-op F3DEX2 list establishes a drawn-list boundary. Repeated
     * scanout can reuse it, but CFB and framebuffer writes must invalidate it. */
    {
        const char *uc="RSP Gfx ucode F3DEX fifo 2.08";
        unsigned long *task=(unsigned long *)(SP_DMEMb+0xfc0);
        memset(SP_DMEMb+0xfc0,0,64);
        for(i=0;uc[i];++i)rdramb[(0x2000+i)^3]=(unsigned char)uc[i];
        task[6]=0x2000; task[7]=2048; task[12]=0x3000;
        address=0x80003000u;word=0xdf000000u;write_word_in_memory();
        address=0x80003004u;word=0;write_word_in_memory();
        stop=0;processDList();
        CHECK(dc_gfx_get_stats().decode_failures==0 && dc_gfx_get_stats().rendered_lists==1);
        updateScreen();
        CHECK(dc_gfx_get_frame()->pixels[0]==0x001fu);
        address=0x80001000u;word=0x07c107c1u;write_word_in_memory();
        updateScreen();
        CHECK(dc_gfx_get_frame()->pixels[0]==0x001fu);
        CHECK(dc_gfx_get_stats().new_render_presentations==1);
        showCFB();updateScreen();
        CHECK(dc_gfx_get_frame()->pixels[0]==0x07e0u);
        address=0x80001000u;word=0xf801f801u;write_word_in_memory();
        fBWrite(0x1000,4);updateScreen();
        CHECK(dc_gfx_get_frame()->pixels[0]==0xf800u);
        vi_register.vi_origin=0x1004;updateScreen();
        CHECK(dc_gfx_get_frame()->pixels[0]!=0xf800u);
        vi_register.vi_origin=0x1000;updateScreen();
        CHECK(dc_gfx_get_frame()->pixels[0]==0xf800u);
    }
#endif
    #ifdef DC_SOFT_GFX
#ifdef DC_VI_HIGHRES
    /* Both woven fields need conversion after a fresh draw. Once both hold
     * that drawing, alternating fields can reuse their own scanout snapshots. */
    {
        unsigned converted;
        romClosed_gfx();romOpen_gfx();dc_gfx_set_frameskip(3);
        vi_register.vi_status=0x42;vi_register.vi_width=2;
        vi_register.vi_h_start=2;vi_register.vi_v_start=4;
        vi_register.vi_x_scale=1024;vi_register.vi_y_scale=2048;
        address=0x80001000u;word=0xf801f801u;write_word_in_memory();
        address=0x80001004u;word=0x07c107c1u;write_word_in_memory();
        address=0x80001008u;word=0xf801f801u;write_word_in_memory();
        address=0x8000100cu;word=0x07c107c1u;write_word_in_memory();
        stop=0;processDList();
        vi_field=0;vi_register.vi_origin=0x1000;updateScreen();
        vi_field=1;vi_register.vi_origin=0x1004;updateScreen();
        converted=dc_gfx_get_stats().converted;CHECK(converted==2);
        CHECK(dc_gfx_get_frame()->pixels[0]==0xf800u);
        CHECK(dc_gfx_get_frame()->pixels[DC_VI_TEXTURE_WIDTH]==0x07e0u);
        address=0x80001000u;word=0x003f003fu;write_word_in_memory();
        address=0x80001004u;word=0xf801f801u;write_word_in_memory();
        vi_field=0;vi_register.vi_origin=0x1000;updateScreen();
        vi_field=1;vi_register.vi_origin=0x1004;updateScreen();
        CHECK(dc_gfx_get_stats().converted==converted);
        CHECK(dc_gfx_get_frame()->pixels[0]==0xf800u);
        CHECK(dc_gfx_get_frame()->pixels[DC_VI_TEXTURE_WIDTH]==0x07e0u);
        for(unsigned n=0;n<4;n++)processDList(); // Next drawn list is five.
        vi_field=0;vi_register.vi_origin=0x1000;updateScreen();
        vi_field=1;vi_register.vi_origin=0x1004;updateScreen();
        CHECK(dc_gfx_get_stats().converted==converted+2);
        CHECK(dc_gfx_get_frame()->pixels[0]==0x001fu);
        CHECK(dc_gfx_get_frame()->pixels[DC_VI_TEXTURE_WIDTH]==0xf800u);
        fBWrite(0x1000,4);
        vi_field=0;vi_register.vi_origin=0x1000;updateScreen();
        vi_field=1;vi_register.vi_origin=0x1004;updateScreen();
        CHECK(dc_gfx_get_stats().converted==converted+4);
        vi_register.vi_status=2;updateScreen();
        vi_register.vi_status=0x42;updateScreen();
        CHECK(dc_gfx_get_stats().converted==converted+6);
        CHECK(dc_gfx_get_frame()->pixels[0]==0); // Mode change cleared old field.
    }
#endif
    /* Balanced skipping draws at lists 1,4,8,11, preserving every FullSync.
     * Reopening a ROM restarts the same cadence. */
    {
        unsigned before;
        address=0x80003000u;word=0xe9000000u;write_word_in_memory();
        address=0x80003008u;word=0xdf000000u;write_word_in_memory();
        address=0x8000300cu;word=0;write_word_in_memory();
        for(unsigned run=0;run<2;run++) {
            romClosed_gfx();romOpen_gfx();dc_gfx_set_frameskip(DC_FRAMESKIP_BALANCED);
            before=interrupt_calls;
            for(unsigned list=0;list<14;list++) {
                unsigned expected=0;
                for(unsigned n=0;n<=list;n++)if(n%7==0 || n%7==3)++expected;
                stop=0;processDList();
                s=dc_gfx_get_stats();
                CHECK(s.rendered_lists==expected && s.skipped_lists==list+1-expected);
                CHECK(s.decode_failures==0 && !stop);
            }
            CHECK(interrupt_calls-before==14);
        }
    }
#endif
#ifdef DC_SOFT_GFX
    /* A steady period has four drawings and every rolling thirty-list window
     * has eight. Lifecycle and mode changes restart it; all FullSyncs run. */
    for(unsigned run=0;run<2;++run) {
        romClosed_gfx();romOpen_gfx();dc_gfx_set_frameskip(DC_FRAMESKIP_STEADY);
        unsigned before=interrupt_calls,drawn[61]={0},skipped=0;
        for(unsigned list=0;list<60;++list) {
            stop=0;processDList();s=dc_gfx_get_stats();
            drawn[list+1]=s.rendered_lists;
            CHECK(s.rendered_lists+s.skipped_lists==list+1);
            CHECK(drawn[list+1]-drawn[list]<=1);
            if(drawn[list+1]!=drawn[list])skipped=0;else ++skipped;
            CHECK(skipped<=3);
            if(!list)CHECK(s.rendered_lists==1);
            if(list>=29)CHECK(drawn[list+1]-drawn[list+1-30]==8);
            CHECK(!s.decode_failures && !stop);
        }
        CHECK(interrupt_calls-before==60);
        CHECK(s.rendered_lists==16 && s.skipped_lists==44);
    }
#endif
    dc_gfx_set_frameskip(99); CHECK(dc_gfx_get_frameskip()==DC_FRAMESKIP_STEADY);
    dc_gfx_set_frameskip(0);
    /* A failed re-init must discard old pointers/state. */
    CHECK(!initiateGFX(bad));
    CHECK(!dc_gfx_read_u8(0, &b));

    puts("memory contract + graphics lifecycle PASS");
    return 0;
}
