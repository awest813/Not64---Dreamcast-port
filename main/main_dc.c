/**
 * Dreamcast / host bring-up entry.
 * Does not boot the N64 core yet (Phase 2).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef DC_HOST_STUB
#include <kos.h>
#endif

#include "../platform/dc_memory.h"
#include "../fileBrowser/fileBrowser.h"
#include "../fileBrowser/fileBrowser-kos.h"
#include "../gc_input/controller.h"

#ifndef DC_HOST_STUB
KOS_INIT_FLAGS(INIT_DEFAULT);
#endif

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
	for (i = 0; i < n && i < 16; ++i)
		printf("  %s%s\n", entries[i].name,
		       (entries[i].attr & FILE_BROWSER_ATTR_DIR) ? "/" : "");
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

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;

#ifndef DC_HOST_STUB
	vid_set_mode(DM_640x480, PM_RGB565);
#endif

	print_budget();
	list_rom_dir();
	probe_controllers();
	printf("Phase 1 complete: no N64 core linked yet. See PORTING.md.\n");

#ifndef DC_HOST_STUB
	{
		int frames;
		for (frames = 0; frames < 120; ++frames)
			thd_sleep(16);
	}
#endif
	return 0;
}
