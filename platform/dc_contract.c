#include "dc_abi.h"
#include "dc_memory.h"
#include "../main/plugin.h"
#include "../gc_memory/memory.h"
#include <stddef.h>

_Static_assert(sizeof(DWORD) == 4, "plugin words must be 32-bit");
_Static_assert(sizeof(BUTTONS) == 4, "controller packet must be four bytes");
_Static_assert(sizeof(rdram) == DC_N64_RDRAM_SIZE, "RDRAM must be 4 MiB");
_Static_assert(sizeof(SP_DMEM) == 8192, "combined DMEM/IMEM must be 8 KiB");
_Static_assert(sizeof(PIF_RAM) == 64, "PIF RAM must be 64 bytes");
_Static_assert(sizeof(vi_register.vi_origin) == 4, "VI register width");
_Static_assert(offsetof(VI_register, vi_width) == 8, "VI register spacing");
_Static_assert(sizeof(dpc_register.dpc_current) == 4, "DPC register width");
_Static_assert(sizeof(double) == 8, "N64 COP1 requires 64-bit double");

_Static_assert(S8 == 3 && S16 == 2 && Sh16 == 1, "Dreamcast uses word-swapped little-endian memory");
