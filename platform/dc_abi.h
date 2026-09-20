#ifndef DC_ABI_H
#define DC_ABI_H

/* KOS/newlib defines _BIG_ENDIAN as a numeric constant on little-endian SH4.
 * Legacy endian branches must explicitly select the Dreamcast little-endian
 * path; never use presence of that system constant to detect byte order. */

/* The legacy core stores emulated words in unsigned long throughout CPU,
 * memory, DMA and plugin interfaces. LP64 is not a supported host ABI yet. */
#include <limits.h>
#if ULONG_MAX != 0xffffffffUL || UINT_MAX != 0xffffffffU
#error "Dreamcast core requires 32-bit int/long; use tools/dc/Dockerfile.host (see tools/dc/README.md)"
#endif
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "Dreamcast build requires little-endian word-swapped memory"
#endif

#endif
