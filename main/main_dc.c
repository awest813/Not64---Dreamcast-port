/**
 * Dreamcast / host entry: diagnostics, interpreter, CPU test, I/O smoke.
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#ifndef DC_HOST_STUB
#include <kos.h>
#endif

#include "../platform/dc_memory.h"
#include "../platform/dc_gfx.h"
#include "../fileBrowser/fileBrowser.h"
#include "../fileBrowser/fileBrowser-kos.h"
#include "../main/winlnxdefs.h"
#include "../main/plugin.h"
#include "../gc_input/controller.h"
#include "../main/wii64config.h"
#include "../main/rom.h"
#include "../main/ROM-Cache.h"
#include "../r4300/r4300.h"
#include "../r4300/interupt.h"
#include "../gc_memory/memory.h"
#include "../gc_memory/TLB-Cache.h"
#include "../gc_memory/pif.h"
#include "../gc_memory/flashram.h"

#ifndef DC_HOST_STUB
KOS_INIT_FLAGS(INIT_DEFAULT);
#ifdef DC_EMBED_VITEST
extern unsigned char romdisk[];
KOS_INIT_ROMDISK(romdisk);
#endif
#endif

extern unsigned long dc_interp_step_limit;
extern BOOL hasLoadedROM;
extern void init_controller_ts(void);
extern void controller_DC_set_start_pulse(unsigned vi);
extern void auto_assign_controllers(void);
extern unsigned int audio_dc_buffered(void);
extern void native_ReadController(int Control, unsigned char *Command);

static const char *capture_path;
static GFX_INFO gfx_info;
static AUDIO_INFO audio_info;
static CONTROL_INFO control_info;
static RSP_INFO rsp_info;

static void print_budget(void)
{
	printf("Not64 Dreamcast bring-up\n");
	printf("  main RAM          %d MiB\n", DC_MAIN_RAM_SIZE / DC_MB);
	printf("  OS/code reserve   %d MiB\n", DC_OS_AND_CODE_RESERVE / DC_MB);
	printf("  N64 RDRAM         %d MiB\n", DC_N64_RDRAM_SIZE / DC_MB);
	printf("  ROM stream        %d MiB\n", DC_ROM_STREAM_SIZE / DC_MB);
	printf("  TLB/misc          %d KiB\n", DC_TLB_MISC_SIZE / DC_KB);
	printf("  tex cache         %d\n", DC_TEXCACHE_SIZE);
	printf("  audio ring        %d KiB\n", DC_AUDIO_RING_SIZE / DC_KB);
	printf("  heap remainder    %d bytes\n", DC_HEAP_REMAINDER);
}

static void list_rom_dir(void)
{
	fileBrowser_file *entries = NULL;
	int n, i;

	fileBrowser_kos_bind();
	if (romFile_init(romFile_topLevel) != 0) {
		printf("rom dir init failed: %s\n", romFile_topLevel->name);
		return;
	}

	n = romFile_readDir(romFile_topLevel, &entries);
	printf("ROM dir %s (%d entries)\n", romFile_topLevel->name, n);
	if (n < 0) {
		printf("  (create this folder and add .z64/.n64 dumps later)\n");
		return;
	}
	for (i = 0; i < n && i < 16; ++i) {
		const char *base = strrchr(entries[i].name, '/');
		base = base ? base + 1 : entries[i].name;
		if (base[0] == '.')
			continue;
		{
			const char *ext = strrchr(base, '.');
			if (ext && strcmp(ext, ".py") == 0)
				continue;
		}
		printf("  %s%s\n", entries[i].name,
		       (entries[i].attr & FILE_BROWSER_ATTR_DIR) ? "/" : "");
	}
	if (n > 16)
		printf("  ... %d more\n", n - 16);
	free(entries);
	romFile_deinit(romFile_topLevel);
}

static void probe_controllers(void)
{
	int i;
	controller_DC.refreshAvailable();
	for (i = 0; i < 4; ++i)
		printf("Maple %d: %s\n", i,
		       controller_DC.available[i] ? "present" : "empty");
}

static int probe_saves(void)
{
	fileBrowser_file marker;
	char buf[32];
	const char *msg = "not64-dc-host\n";
	int n;

	fileBrowser_kos_bind();
	if (saveFile_init(saveFile_dir) != 0) {
		printf("save dir init failed: %s\n", saveFile_dir->name);
		return -1;
	}

	memset(&marker, 0, sizeof(marker));
	strncpy(marker.name, saveFile_dir->name, FILE_BROWSER_MAX_PATH_LEN - 16);
	marker.name[FILE_BROWSER_MAX_PATH_LEN - 16] = '\0';
	strcat(marker.name, "/dc_host.txt");
	n = saveFile_writeFile(&marker, (void *)msg, (unsigned int)strlen(msg));
	if (n < (int)strlen(msg)) {
		printf("save write failed (%d)\n", n);
		return -1;
	}
	marker.offset = 0;
	memset(buf, 0, sizeof(buf));
	n = saveFile_readFile(&marker, buf, sizeof(buf) - 1);
	printf("save %s (%d bytes): %s", marker.name, n, buf);
	saveFile_deinit(saveFile_dir);
	return n > 0 ? 0 : -1;
}

static int smoke_io(void)
{
	BUTTONS keys;
	int fail = 0;

#ifdef DC_HOST_STUB
	controller_DC_host_set(0, DC_CONT_A, 200, 80); /* A + analog */
#endif
	memset(&keys, 0, sizeof(keys));
	getKeys(0, &keys);
	printf("input0 A=%u X=%d Y=%d\n",
	       (unsigned)keys.A_BUTTON, (int)keys.X_AXIS, (int)keys.Y_AXIS);
#ifdef DC_HOST_STUB
	if (!keys.A_BUTTON) {
		printf("input smoke: expected A held on host inject\n");
		fail = 1;
	}
#endif

	ai_register.ai_dram_addr = 0;
	ai_register.ai_len = 64;
	memset(rdram, 0x5A, 64);
	aiLenChanged();
	printf("audio ring buffered %u bytes\n", audio_dc_buffered());
	if (audio_dc_buffered() < 64) {
		printf("audio smoke: ring did not accept AI DMA\n");
		fail = 1;
	}
	return fail;
}

static int smoke_pif(void)
{
	int fail = 0;
	unsigned char native_cmd[8];
	unsigned char status_type = 0;

#ifdef DC_HOST_STUB
	controller_DC_host_set(0, DC_CONT_A, 200, 80);
#endif

	if (!Controls[0].Present) {
		printf("pif smoke: pad 0 not Present\n");
		return 1;
	}

	memset(PIF_RAMb, 0, 0x40);
	PIF_RAMb[0] = 0x01;
	PIF_RAMb[1] = 0x03;
	PIF_RAMb[2] = 0x00;
	update_pif_write();
	status_type = PIF_RAMb[3];
	if (status_type != 0x05) {
		printf("pif smoke: status type=0x%02x want 0x05\n", status_type);
		fail = 1;
	}

	memset(PIF_RAMb, 0, 0x40);
	PIF_RAMb[0] = 0x01;
	PIF_RAMb[1] = 0x04;
	PIF_RAMb[2] = 0x01;
	update_pif_read();
	if (!(PIF_RAMb[3] & 0x80u)) {
		printf("pif smoke: buttons %02x %02x %02x %02x missing A\n",
		       PIF_RAMb[3], PIF_RAMb[4], PIF_RAMb[5], PIF_RAMb[6]);
		fail = 1;
	}
	if ((signed char)PIF_RAMb[5] != 72 || (signed char)PIF_RAMb[6] != 48) {
		printf("pif smoke: analog X=%d Y=%d want 72,48\n",
		       (int)(signed char)PIF_RAMb[5],
		       (int)(signed char)PIF_RAMb[6]);
		fail = 1;
	}

	memset(native_cmd, 0, sizeof(native_cmd));
	native_cmd[0] = 0x01;
	native_cmd[1] = 0x04;
	native_cmd[2] = 0x01;
	native_ReadController(0, native_cmd);
	if (!(native_cmd[3] & 0x80u)) {
		printf("pif smoke: native_ReadController missing A\n");
		fail = 1;
	}

#ifdef DC_HOST_STUB
    /* Check the wire packet, not the compiler's native bitfield layout. */
    controller_DC_host_set(0, DC_CONT_START, 128, 128);
    native_ReadController(0, native_cmd);
    if (native_cmd[3] != 0x10 || native_cmd[4] || native_cmd[5] || native_cmd[6]) {
        printf("pif smoke: Start packet has wrong Joybus bit order\n");
        fail = 1;
    }
    controller_DC_host_set(0, 0, 128, 128);
#endif

	printf("pif smoke %s (status=0x%02x A=%u X=%d Y=%d)\n",
	       fail ? "FAIL" : "PASS",
	       status_type,
	       (unsigned)!!(PIF_RAMb[3] & 0x80u),
	       (int)(signed char)PIF_RAMb[5],
	       (int)(signed char)PIF_RAMb[6]);
	return fail;
}

static int check_cputest(void)
{
	int fail = 0;
	unsigned long got;

	if (strcmp(ROM_SETTINGS.goodname, "DC CPUTEST") != 0)
		return 0;

#define DC_EXPECT_REG(n, v) \
	do { \
		got = (unsigned long)(reg[(n)] & 0xffffffffu); \
		if (got != (unsigned long)(v)) { \
			printf("CPUTEST r%d=0x%lx want 0x%x\n", (n), got, (v)); \
			fail = 1; \
		} \
	} while (0)

	DC_EXPECT_REG(1, 0x1234);
	DC_EXPECT_REG(2, 0x00FF);
	DC_EXPECT_REG(3, 0x1333);
	DC_EXPECT_REG(4, 0x12CB);
	DC_EXPECT_REG(5, 0x80000000u);
	DC_EXPECT_REG(6, 0x1333);
	DC_EXPECT_REG(7, 0xFF00);
	DC_EXPECT_REG(9, 0x0030);
	DC_EXPECT_REG(10, 0x1334);
	DC_EXPECT_REG(11, 0x0001);
#undef DC_EXPECT_REG

	got = (unsigned long)(rdram[0] & 0xffffffffu);
	if (got != 0x1333u) {
		printf("CPUTEST rdram[0]=0x%lx want 0x1333\n", got);
		fail = 1;
	}
	if (interp_addr != 0xa4000074 && interp_addr != 0xa4000078) {
		printf("CPUTEST interp_addr=0x%08lx (expected IPL BEQ spin)\n",
		       interp_addr);
		fail = 1;
	}

	printf("CPUTEST %s\n", fail ? "FAIL" : "PASS");
	return fail;
}

/* Validate CPU-produced pixels through the same bounded accessor intended for
 * VI scanout. This is a framebuffer fixture, not a renderer test yet. */
static int check_vitest(void)
{
    static const uint16_t colors[8] = {
        0xffff, 0xffc1, 0x07ff, 0x07c1, 0xf83f, 0xf801, 0x003f, 0x0001
    };
    uint32_t complete;
    unsigned x, y;
    if (strcmp(ROM_SETTINGS.goodname, "DC VITEST") != 0) return 0;
    if (!dc_gfx_read_u32(0x200, &complete) || complete != 0x56495445 ||
        vi_register.vi_origin != 0x10000 || vi_register.vi_width != 320 ||
        vi_register.vi_status != 2 || vi_register.vi_v_sync != 525 ||
        vi_register.vi_h_start != ((108u << 16) | 748u) ||
        vi_register.vi_v_start != ((37u << 16) | 517u) ||
        vi_register.vi_x_scale != 512 || vi_register.vi_y_scale != 1024) {
        fprintf(stderr, "VITEST FAIL: incomplete frame or incorrect VI registers\n");
        return 1;
    }
    for (y = 0; y < 240; ++y) for (x = 0; x < 320; ++x) {
        uint16_t actual, expected = colors[x / 40];
        if (y == 0 && x == 0) expected = 0xf801;
        if (y == 0 && x == 319) expected = 0x07c1;
        if (y == 239 && x == 0) expected = 0x003f;
        if (y == 239 && x == 319) expected = 0xffff;
        if (!dc_gfx_read_u16(0x10000 + (y * 320 + x) * 2, &actual) || actual != expected) {
            fprintf(stderr, "VITEST FAIL: pixel %u,%u\n", x, y);
            return 1;
        }
    }
    if (!dc_gfx_get_stats().presented) {
        fprintf(stderr, "VITEST FAIL: no converted frame presented\n");
        return 1;
    }
    puts("VITEST PASS (76800 CPU-written pixels + VI scanout)");
    return 0;
}

#ifdef DC_EMBED_VITEST
/* Exercise the real target presenters without rerunning the emulated CPU.
 * Alternate ROM close/reopen with complete plugin destruction/reinitialization. */
static int stress_video(void)
{
    unsigned cycle, n;
    const unsigned cycles = 8;
    uint64_t total_us = 0;
#ifdef DC_VIDEO_PVR
    size_t free_vram = pvr_mem_available();
#endif
    dc_gfx_set_frame_limit(0);
    for (cycle = 0; cycle < cycles; ++cycle) {
        dc_gfx_stats stats;
        romClosed_gfx();
        romClosed_gfx(); /* Idempotent release must not free twice. */
        if (dc_gfx_is_open() || dc_gfx_get_frame()) return 1;
        if (cycle & 1) {
            closeDLL_gfx();
            if (!initiateGFX(gfx_info)) return 1;
        }
        romOpen_gfx();
        if (!dc_gfx_is_open()) return 1;
#ifdef DC_VIDEO_PVR
        if (pvr_mem_available() != free_vram) {
            puts("VIDEO STRESS FAIL: PVR allocation changed across reopen");
            return 1;
        }
#endif
        stop = 0;
        vi_register.vi_status = 0;
        updateScreen();
        vi_register.vi_status = 2;
        updateScreen();
        vi_register.vi_origin = DC_N64_RDRAM_SIZE;
        updateScreen();
        vi_register.vi_origin = 0x10000;
        updateScreen();
        vi_register.vi_status = 1;
        updateScreen();
        vi_register.vi_status = 2;
        updateScreen();
        for (n = 0; n < 16; ++n) {
            uint64_t begin = timer_us_gettime64();
            updateScreen();
            total_us += timer_us_gettime64() - begin;
        }
        stats = dc_gfx_get_stats();
        if (stop || stats.present_failures || stats.presented != 19 ||
            stats.vi_updates != 22 || stats.blanked != 1 ||
            stats.invalid_vi != 1 || stats.unsupported_vi != 1 || check_vitest()) {
            printf("VIDEO STRESS FAIL: cycle=%u\n", cycle + 1);
            return 1;
        }
    }
    printf("VIDEO STRESS PASS: backend=%s cycles=%u valid-frames=%u scanout-present-avg-us=%lu\n",
           dc_gfx_backend_name(), cycles, cycles * 19,
           (unsigned long)(total_us / (cycles * 16)));
    return 0;
}
#endif

static BOOL gfx_info_init(void)
{
	gfx_info.MemoryBswaped = TRUE;
	gfx_info.HEADER = (BYTE*)&ROM_HEADER;
	gfx_info.RDRAM = (BYTE*)rdram;
	gfx_info.DMEM = (BYTE*)SP_DMEM;
	gfx_info.IMEM = (BYTE*)SP_IMEM;
	gfx_info.MI_INTR_REG = &(MI_register.mi_intr_reg);
	gfx_info.DPC_START_REG = &(dpc_register.dpc_start);
	gfx_info.DPC_END_REG = &(dpc_register.dpc_end);
	gfx_info.DPC_CURRENT_REG = &(dpc_register.dpc_current);
	gfx_info.DPC_STATUS_REG = &(dpc_register.dpc_status);
	gfx_info.DPC_CLOCK_REG = &(dpc_register.dpc_clock);
	gfx_info.DPC_BUFBUSY_REG = &(dpc_register.dpc_bufbusy);
	gfx_info.DPC_PIPEBUSY_REG = &(dpc_register.dpc_pipebusy);
	gfx_info.DPC_TMEM_REG = &(dpc_register.dpc_tmem);
	gfx_info.VI_STATUS_REG = &(vi_register.vi_status);
	gfx_info.VI_ORIGIN_REG = &(vi_register.vi_origin);
	gfx_info.VI_WIDTH_REG = &(vi_register.vi_width);
	gfx_info.VI_INTR_REG = &(vi_register.vi_v_intr);
	gfx_info.VI_V_CURRENT_LINE_REG = &(vi_register.vi_current);
	gfx_info.VI_TIMING_REG = &(vi_register.vi_burst);
	gfx_info.VI_V_SYNC_REG = &(vi_register.vi_v_sync);
	gfx_info.VI_H_SYNC_REG = &(vi_register.vi_h_sync);
	gfx_info.VI_LEAP_REG = &(vi_register.vi_leap);
	gfx_info.VI_H_START_REG = &(vi_register.vi_h_start);
	gfx_info.VI_V_START_REG = &(vi_register.vi_v_start);
	gfx_info.VI_V_BURST_REG = &(vi_register.vi_v_burst);
	gfx_info.VI_X_SCALE_REG = &(vi_register.vi_x_scale);
	gfx_info.VI_Y_SCALE_REG = &(vi_register.vi_y_scale);
	gfx_info.CheckInterrupts = check_interupt;
	return initiateGFX(gfx_info);
}

static void audio_info_init(void)
{
	audio_info.MemoryBswaped = TRUE;
	audio_info.HEADER = (BYTE*)&ROM_HEADER;
	audio_info.RDRAM = (BYTE*)rdram;
	audio_info.DMEM = (BYTE*)SP_DMEM;
	audio_info.IMEM = (BYTE*)SP_IMEM;
	audio_info.MI_INTR_REG = &(MI_register.mi_intr_reg);
	audio_info.AI_DRAM_ADDR_REG = &(ai_register.ai_dram_addr);
	audio_info.AI_LEN_REG = &(ai_register.ai_len);
	audio_info.AI_CONTROL_REG = &(ai_register.ai_control);
	audio_info.AI_STATUS_REG = &(ai_register.ai_status);
	audio_info.AI_DACRATE_REG = &(ai_register.ai_dacrate);
	audio_info.AI_BITRATE_REG = &(ai_register.ai_bitrate);
	audio_info.CheckInterrupts = check_interupt;
	initiateAudio(audio_info);
}

static void rsp_info_init(void)
{
	static int cycle_count;
	rsp_info.MemoryBswaped = TRUE;
	rsp_info.RDRAM = (BYTE*)rdram;
	rsp_info.DMEM = (BYTE*)SP_DMEM;
	rsp_info.IMEM = (BYTE*)SP_IMEM;
	rsp_info.MI_INTR_REG = &MI_register.mi_intr_reg;
	rsp_info.SP_MEM_ADDR_REG = &sp_register.sp_mem_addr_reg;
	rsp_info.SP_DRAM_ADDR_REG = &sp_register.sp_dram_addr_reg;
	rsp_info.SP_RD_LEN_REG = &sp_register.sp_rd_len_reg;
	rsp_info.SP_WR_LEN_REG = &sp_register.sp_wr_len_reg;
	rsp_info.SP_STATUS_REG = &sp_register.sp_status_reg;
	rsp_info.SP_DMA_FULL_REG = &sp_register.sp_dma_full_reg;
	rsp_info.SP_DMA_BUSY_REG = &sp_register.sp_dma_busy_reg;
	rsp_info.SP_PC_REG = &rsp_register.rsp_pc;
	rsp_info.SP_SEMAPHORE_REG = &sp_register.sp_semaphore_reg;
	rsp_info.DPC_START_REG = &dpc_register.dpc_start;
	rsp_info.DPC_END_REG = &dpc_register.dpc_end;
	rsp_info.DPC_CURRENT_REG = &dpc_register.dpc_current;
	rsp_info.DPC_STATUS_REG = &dpc_register.dpc_status;
	rsp_info.DPC_CLOCK_REG = &dpc_register.dpc_clock;
	rsp_info.DPC_BUFBUSY_REG = &dpc_register.dpc_bufbusy;
	rsp_info.DPC_PIPEBUSY_REG = &dpc_register.dpc_pipebusy;
	rsp_info.DPC_TMEM_REG = &dpc_register.dpc_tmem;
	rsp_info.CheckInterrupts = check_interupt;
	rsp_info.ProcessDlistList = processDList;
	rsp_info.ProcessAlistList = processAList;
	rsp_info.ProcessRdpList = processRDPList;
	rsp_info.ShowCFB = showCFB;
	initiateRSP(rsp_info, (DWORD*)&cycle_count);
}

static int load_and_step(const char *path, unsigned long steps)
{
	fileBrowser_file romfile;
	int ret;

	memset(&romfile, 0, sizeof(romfile));
	strncpy(romfile.name, path, FILE_BROWSER_MAX_PATH_LEN - 1);
	{
		FILE *fp = fopen(path, "rb");
		if (!fp) {
			printf("ROM open failed: %s\n", path);
			return -1;
		}
		fseek(fp, 0, SEEK_END);
		romfile.size = (unsigned int)ftell(fp);
		fclose(fp);
	}

	fileBrowser_kos_bind();

	format_mempacks();
	reset_flashram();
	init_eeprom();
	TLBCache_init();

	ret = rom_read(&romfile);
	if (ret) {
		printf("rom_read failed (%d)\n", ret);
		TLBCache_deinit();
		ROMCache_deinit();
		return ret;
	}

	hasLoadedROM = TRUE;
	printf("Loaded '%s' (%d bytes)\n",
	       ROM_SETTINGS.goodname, rom_length);

	init_memory();
	if (!gfx_info_init()) {
		fprintf(stderr, "Graphics initialization failed\n");
		closeDLL_gfx();
		TLBCache_deinit();
		ROMCache_deinit();
		hasLoadedROM = FALSE;
		return 1;
	}
	audio_info_init();
	init_controller_ts();
	control_info.MemoryBswaped = TRUE;
	control_info.HEADER = (BYTE*)&ROM_HEADER;
	control_info.Controls = Controls;
	{
		int i;
		for (i = 0; i < 4; i++) {
			Controls[i].Present = FALSE;
			Controls[i].RawData = FALSE;
			Controls[i].Plugin = PLUGIN_NONE;
		}
	}
	initiateControllers(control_info);
	auto_assign_controllers();
	rsp_info_init();
	romOpen_gfx();
	if (!dc_gfx_is_open()) {
		fprintf(stderr, "Graphics open failed\n");
		closeDLL_gfx(); TLBCache_deinit(); ROMCache_deinit();
		hasLoadedROM = FALSE;
		return 1;
	}
	romOpen_audio();
	romOpen_input();

	dynacore = 2;
	cpu_init();
	printf("Header name: '%s'  country=0x%02x  CIC_Chip=%lu  PC=0x%08x\n",
	       ROM_SETTINGS.goodname, ROM_HEADER.Country_code, CIC_Chip, ROM_HEADER.PC);

#ifdef DC_HOST_STUB
	if (!strncmp(ROM_SETTINGS.goodname, "DC ", 3) && (smoke_io() || smoke_pif())) {
		romClosed_gfx();
		closeDLL_gfx();
		cpu_deinit();
		TLBCache_deinit();
		ROMCache_deinit();
		hasLoadedROM = FALSE;
		return 1;
	}

#endif
	dc_interp_step_limit = steps;
	go();
	printf("Interpreter stopped (budget=%lu interp_addr=0x%08lx stop=%d)\n",
	       steps, interp_addr, stop);
	ret = check_cputest() | check_vitest();
	{
		dc_gfx_stats stats = dc_gfx_get_stats();
		printf("Graphics %s: VI=%lu presented=%lu DList=%lu RDP=%lu invalid=%lu unsupported=%lu failures=%lu\n",
               dc_gfx_backend_name(), (unsigned long)stats.vi_updates,
               (unsigned long)stats.presented, (unsigned long)stats.display_lists,
               (unsigned long)stats.rdp_lists, (unsigned long)stats.invalid_vi,
               (unsigned long)stats.unsupported_vi, (unsigned long)stats.present_failures);
#ifdef DC_SOFT_GFX
        printf("Software graphics: decode-failures=%lu\n", (unsigned long)stats.decode_failures);
#endif
        if (stats.present_failures || stats.decode_failures) ret = 1;
	}
    if (capture_path && !dc_gfx_capture_ppm(capture_path)) {
        fprintf(stderr, "Framebuffer capture failed: %s\n", capture_path);
        ret = 1;
    }
#ifdef DC_EMBED_VITEST
    if (!ret && stress_video()) ret = 1;
#endif
#ifndef DC_HOST_STUB
    /* Keep the last submitted image visible before restoring console video. */
#if defined(DC_EMBED_VITEST) || defined(DC_GAME_DISC)
    /* Leave the diagnostic result visible for emulator/hardware inspection. */
    thd_sleep(60000);
#else
    thd_sleep(1000);
#endif
#endif
	romClosed_gfx();
	closeDLL_gfx();
	cpu_deinit();
	TLBCache_deinit();
	ROMCache_deinit();
	hasLoadedROM = FALSE;
	return ret;
}

static int positive_number(const char *text, unsigned long *value)
{
    char *end;
    errno = 0;
    *value = strtoul(text, &end, 10);
    return !errno && text[0] && strspn(text, "0123456789") == strlen(text) && !*end && *value;
}

int main(int argc, char **argv)
{
    const char *rompath = "roms/dc_cputest.z64";
    int fail = 0, arg = 1;
    unsigned long steps = 10000, frames = 0;
#ifdef DC_GAME_DISC
    rompath = "/cd/game.z64";
    steps = 500000000;
    controller_DC_set_start_pulse(400);
#endif
#ifdef DC_EMBED_VITEST
    rompath = "/rd/roms/dc_vitest.z64";
    steps = 300000;
#endif
#ifndef DC_HOST_STUB
    frames = 120; /* Prevent an idle ROM presenting thousands of real frames. */
#ifdef DC_GAME_DISC
    frames = 600;
#endif
#endif
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    if (arg < argc && argv[arg][0] != '-') rompath = argv[arg++];
    if (arg < argc && argv[arg][0] != '-') {
        if (!positive_number(argv[arg++], &steps)) goto usage;
    }
    while (arg < argc) {
        if (!strcmp(argv[arg], "--frames") && arg + 1 < argc) {
            if (!positive_number(argv[arg + 1], &frames)) goto usage;
            arg += 2;
        } else if (!strcmp(argv[arg], "--start-at") && arg + 1 < argc) {
            unsigned long start_vi;
            if (!positive_number(argv[arg + 1], &start_vi)) goto usage;
            controller_DC_set_start_pulse((unsigned)start_vi);
            arg += 2;
        } else if (!strcmp(argv[arg], "--capture") && arg + 1 < argc) {
            capture_path = argv[arg + 1];
            arg += 2;
        } else goto usage;
    }
    dc_gfx_set_frame_limit(frames);
#ifndef DC_HOST_STUB
    vid_set_mode(DM_640x480, PM_RGB565);
#endif
    print_budget();
    list_rom_dir();
    probe_controllers();
#if !defined(DC_EMBED_VITEST) && !defined(DC_GAME_DISC)
    if (probe_saves()) fail = 1;
#endif
    printf("Dreamcast interpreter: %lu steps using %s\n", steps, rompath);
    if (load_and_step(rompath, steps)) fail = 1;
    return fail;
usage:
    fprintf(stderr, "Usage: %s [rom.z64 [positive-step-budget]] [--frames positive-count] [--capture output.ppm] [--start-at VI]\n", argv[0]);
    return 1;
}
