#include "../main/winlnxdefs.h"
#include "../main/plugin.h"
#include "../main/savestates.h"
#include <string.h>

int savestates_job;

void savestates_save(void) {}
void savestates_load(void) {}
int savestates_exists(int mode) { (void)mode; return 0; }
void savestates_select_slot(unsigned int s) { (void)s; }
void savestates_select_filename(void) {}
