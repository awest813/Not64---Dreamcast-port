#ifndef DC_OVERLAY_H
#define DC_OVERLAY_H

#include "../gc_input/controller.h"

/*
 * Phase 8b pause overlay. Wii64 used GetKeys' exit combo to set stop=1 and
 * drop back into libgui. Here Start+A+B (the default Maple exit combo) opens
 * an immediate-mode screen: Continue, Reset, return to the ROM browser,
 * and savestate slot UI. Slots write `saves/<goodname>.stN` (little-endian dump).
 */

enum {
	DC_OVERLAY_NONE = 0,
	DC_OVERLAY_CONTINUE,
	DC_OVERLAY_RESET,
	DC_OVERLAY_MENU
};

void dc_overlay_enter(void);
int dc_overlay_step(const BUTTONS *keys);
void dc_overlay_draw(void);
int dc_overlay_run(void);
int dc_overlay_take_action(void);

#ifdef DC_HOST_STUB
int dc_overlay_selftest(void);
#endif

#endif
