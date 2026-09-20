#ifndef DC_DEBUG_H
#define DC_DEBUG_H

#include "../gui/DEBUG.h"

unsigned dc_debug_seq(void);
void dc_debug_close(void);
void dc_debug_reset(void);

#ifdef DC_HOST_STUB
int dc_debug_selftest(void);
#endif

#endif
