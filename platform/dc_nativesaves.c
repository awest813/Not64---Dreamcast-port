/**
 * Native EEPROM/SRAM/Flash/mempak files on SD (Phase P3).
 *
 * Wii only flushed these from libgui. Dreamcast has no that menu, so boot
 * and overlay Reset/Return-to-menu must do it. VMU layout is still a human
 * call; this path is always the SD/POSIX save dir.
 */

#include "dc_nativesaves.h"
#include "dc_debug.h"

#include "../main/wii64config.h"
#include "../fileBrowser/fileBrowser.h"
#include "../fileBrowser/fileBrowser-kos.h"
#include "../gc_memory/Saves.h"

#ifdef DC_HOST_STUB
#include "../gc_memory/pif.h"
#include "../gc_memory/dma.h"
#include "../main/rom.h"
#include <stdio.h>
#include <string.h>
#endif

extern char saveEnabled;
extern char autoLoadSave;
extern char autoSave;

void dc_nativesave_load(void)
{
	int n = 0;

	if (!autoLoadSave)
		return;
	fileBrowser_kos_bind();
	if (!saveFile_dir || !saveFile_init || !saveFile_readFile)
		return;
	if (saveFile_init(saveFile_dir) != 0)
		return;
	n += loadEeprom(saveFile_dir);
	n += loadSram(saveFile_dir);
	n += loadMempak(saveFile_dir);
	n += loadFlashram(saveFile_dir);
	if (n > 0)
		dc_log(DC_LOG_INFO, "native save: loaded %d file(s)", n);
}

void dc_nativesave_save(void)
{
	int n = 0;

	if (!autoSave && !saveEnabled)
		return;
	fileBrowser_kos_bind();
	if (!saveFile_dir || !saveFile_init || !saveFile_writeFile)
		return;
	if (saveFile_init(saveFile_dir) != 0)
		return;
	n += saveEeprom(saveFile_dir);
	n += saveSram(saveFile_dir);
	n += saveMempak(saveFile_dir);
	n += saveFlashram(saveFile_dir);
	if (n > 0)
		dc_log(DC_LOG_INFO, "native save: wrote %d file(s)", n);
}

#ifdef DC_HOST_STUB
int dc_nativesave_selftest(void)
{
	int fails = 0;
	char path[512];
	char old_auto_save = autoSave;
	char old_auto_load = autoLoadSave;

	autoSave = 1;
	autoLoadSave = 1;
	fileBrowser_kos_bind();
	snprintf(path, sizeof(path), "%s/%s%s.eep",
		 saveFile_dir ? saveFile_dir->name : "./saves",
		 ROM_SETTINGS.goodname, saveregionstr());
	remove(path);

	dc_eeprom_debug_set(0, 0x3C);
	dc_eeprom_debug_set(1, 0xA5);
	dc_nativesave_save();
	if (dc_eeprom_debug_get(0) != 0x3C) {
		printf("nativesave FAIL: poke did not stick\n");
		fails++;
	}
	init_eeprom();
	if (dc_eeprom_debug_get(0) == 0x3C) {
		printf("nativesave FAIL: init_eeprom left the poke\n");
		fails++;
	}
	dc_nativesave_load();
	if (dc_eeprom_debug_get(0) != 0x3C || dc_eeprom_debug_get(1) != 0xA5) {
		printf("nativesave FAIL: roundtrip %02x %02x want 3c a5\n",
		       dc_eeprom_debug_get(0), dc_eeprom_debug_get(1));
		fails++;
	}
	remove(path);
	init_eeprom();
	autoSave = old_auto_save;
	autoLoadSave = old_auto_load;
	printf("nativesave selftest: %s (%d failure(s))\n",
	       fails ? "FAIL" : "PASS", fails);
	return fails;
}
#endif
