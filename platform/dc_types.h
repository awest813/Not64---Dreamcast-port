/**
 * Dreamcast type aliases matching libOGC gctypes.h enough for the portable core.
 */

#ifndef PLATFORM_DC_TYPES_H
#define PLATFORM_DC_TYPES_H

#include <stdint.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;
typedef int64_t  s64;

typedef volatile u8  vu8;
typedef volatile u16 vu16;
typedef volatile u32 vu32;
typedef volatile s8  vs8;
typedef volatile s16 vs16;
typedef volatile s32 vs32;

typedef int BOOL;

#ifndef TRUE
#define TRUE  1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#ifndef ATTRIBUTE_ALIGN
#define ATTRIBUTE_ALIGN(x) __attribute__((aligned(x)))
#endif

#endif
