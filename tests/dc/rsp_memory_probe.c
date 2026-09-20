/* Separate translation unit: exercise the RSP's actual accessors without
 * mixing its u8/u16/u32 function names with the platform's type aliases. */
#include "../../rsp_hle/memory.h"
#include "../../rsp_hle/wintypes.h"
#include "../../rsp_hle/Rsp_#1.1.h"

uint32_t rsp_probe_word(const unsigned char *memory, unsigned offset)
{
    return *u32(memory, offset);
}
uint16_t rsp_probe_half(const unsigned char *memory, unsigned offset)
{
    return *u16(memory, offset);
}
uint8_t rsp_probe_byte(const unsigned char *memory, unsigned offset)
{
    return *u8(memory, offset);
}
size_t rsp_info_size(void) { return sizeof(RSP_INFO); }
size_t rsp_info_callback_offset(void) { return offsetof(RSP_INFO, CheckInterrupts); }
