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

void fileBrowser_kos_bind(void);

#endif
