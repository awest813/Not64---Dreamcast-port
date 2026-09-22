#include "../../platform/dc_gfx.h"
#include "../../platform/dc_memory.h"
#include "../../main/plugin.h"
#include "../../gc_memory/memory.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

uint32_t rsp_probe_word(const unsigned char *, unsigned);
uint16_t rsp_probe_half(const unsigned char *, unsigned);
uint8_t rsp_probe_byte(const unsigned char *, unsigned);
size_t rsp_info_size(void);
size_t rsp_info_callback_offset(void);
extern int stop;

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
    /* A failed re-init must discard old pointers/state. */
    CHECK(!initiateGFX(bad));
    CHECK(!dc_gfx_read_u8(0, &b));

    puts("memory contract + graphics lifecycle PASS");
    return 0;
}
