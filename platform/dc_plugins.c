#include "../main/winlnxdefs.h"
#include "../main/plugin.h"
#include "../gc_memory/memory.h"
#include "../r4300/interupt.h"
#include <string.h>
#include <stdio.h>

CONTROL Controls[4];

void display_loading_progress(int p) { (void)p; }
void display_MD5calculating_progress(int p) { (void)p; }
int ask_bad(void) { return 1; }
int ask_hack(void) { return 1; }
void warn_savestate_from_another_rom(void) {}
void warn_savestate_not_exist(void) {}
#ifdef DC_HOST_STUB
char *get_currentpath(void) { return "."; }
char *get_savespath(void) { return "./saves"; }
#else
/* Must track fileBrowser-kos.c's KOS_ROM_DIR / KOS_SAVE_DIR. */
char *get_currentpath(void) { return "/sd/not64"; }
char *get_savespath(void) { return "/sd/not64/saves"; }
#endif

void keyDown(WPARAM wParam, LPARAM lParam)
{
	(void)wParam;
	(void)lParam;
}

void keyUp(WPARAM wParam, LPARAM lParam)
{
	(void)wParam;
	(void)lParam;
}
