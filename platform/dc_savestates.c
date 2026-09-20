#include "../main/winlnxdefs.h"
#include "../main/plugin.h"
#include "../main/savestates.h"
#include "../fileBrowser/fileBrowser.h"
#include "dc_debug.h"
#include <stdio.h>
#include <string.h>

extern char *get_savespath(void);

int savestates_job;

static unsigned slot;

static const char *slot_path(void)
{
	static char path[FILE_BROWSER_MAX_PATH_LEN];
	const char *dir = get_savespath();

	if (!dir)
		dir = "./saves";
	snprintf(path, sizeof(path), "%s/not64.st%u", dir, slot);
	return path;
}

unsigned savestates_get_slot(void)
{
	return slot;
}

const char *savestates_filename(void)
{
	return slot_path();
}

void savestates_save(void)
{
	dc_log(DC_LOG_INFO, "savestate: save slot %u not implemented (%s)",
	       slot, slot_path());
	savestates_job &= ~SAVESTATE;
}

void savestates_load(void)
{
	dc_log(DC_LOG_INFO, "savestate: load slot %u not implemented (%s)",
	       slot, slot_path());
	savestates_job &= ~LOADSTATE;
}

int savestates_exists(int mode)
{
	FILE *fp;

	(void)mode;
	fp = fopen(slot_path(), "rb");
	if (!fp)
		return 0;
	fclose(fp);
	return 1;
}

void savestates_select_slot(unsigned int s)
{
	if (s > 9)
		s = 9;
	slot = s;
}

void savestates_select_filename(void)
{
}
