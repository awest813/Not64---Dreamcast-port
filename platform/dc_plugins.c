#include "../main/winlnxdefs.h"
#include "../main/plugin.h"
#include "../gc_memory/memory.h"
#include "../r4300/interupt.h"
#include <string.h>
#include <stdio.h>

CONTROL Controls[4];

static void dummy_void(void) {}
static void dummy_fb_read(DWORD addr) { (void)addr; }
static void dummy_fb_write(DWORD addr, DWORD size) { (void)addr; (void)size; }
static void dummy_fb_info(void *p) { (void)p; }

void (*fBRead)(DWORD addr) = dummy_fb_read;
void (*fBWrite)(DWORD addr, DWORD size) = dummy_fb_write;
void (*fBGetFrameBufferInfo)(void *p) = dummy_fb_info;

void changeWindow(void) {}
void closeDLL_gfx(void) {}
BOOL initiateGFX(GFX_INFO Gfx_Info)
{
	(void)Gfx_Info;
	return TRUE;
}
void processDList(void) {}
void processRDPList(void) {}
void romClosed_gfx(void) {}
void romOpen_gfx(void) {}
void showCFB(void) {}
void updateScreen(void) {}
void viStatusChanged(void) {}
void viWidthChanged(void) {}
void readScreen(void **dest, long *width, long *height)
{
	if (dest) *dest = NULL;
	if (width) *width = 0;
	if (height) *height = 0;
}

void display_loading_progress(int p) { (void)p; }
void display_MD5calculating_progress(int p) { (void)p; }
int ask_bad(void) { return 1; }
int ask_hack(void) { return 1; }
void warn_savestate_from_another_rom(void) {}
void warn_savestate_not_exist(void) {}
char *get_currentpath(void) { return "."; }
char *get_savespath(void) { return "./saves"; }
