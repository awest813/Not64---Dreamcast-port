#ifndef DC_DEBUG_H
#define DC_DEBUG_H

#include <stddef.h>
#include "../gui/DEBUG.h"

enum {
	DC_LOG_ERROR = 0,
	DC_LOG_INFO = 1
};

unsigned dc_debug_seq(void);
void dc_debug_close(void);
void dc_debug_reset(void);
void dc_debug_set_file(int on);
const char *dc_debug_log_path(void);
int dc_debug_last(char *buf, size_t n);

/* printf-style: ring always; file if printToSD; stdout if overlay or ERROR. */
void dc_log(int level, const char *fmt, ...);

#ifdef DC_HOST_STUB
int dc_debug_selftest(void);
#endif

#endif
