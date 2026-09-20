#ifndef FILE_BROWSER_KOS_H
#define FILE_BROWSER_KOS_H

#include "fileBrowser.h"

extern fileBrowser_file topLevel_kos;
extern fileBrowser_file saveDir_kos;

int fileBrowser_kos_readDir(fileBrowser_file*, fileBrowser_file**);
int fileBrowser_kos_readFile(fileBrowser_file*, void*, unsigned int);
int fileBrowser_kos_writeFile(fileBrowser_file*, void*, unsigned int);
int fileBrowser_kos_seekFile(fileBrowser_file*, unsigned int, unsigned int);
int fileBrowser_kos_init(fileBrowser_file* f);
int fileBrowser_kos_deinit(fileBrowser_file* f);

/* Number of real fopen() calls made for reads; the ROM cache used to force
 * one per 64 KiB page-in. */
unsigned long fileBrowser_kos_open_count(void);

void fileBrowser_kos_bind(void);

#endif
