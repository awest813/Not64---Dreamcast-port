#ifndef DC_SETTINGS_H
#define DC_SETTINGS_H

#include "../gc_input/controller.h"

/*
 * Versioned ASCII settings, following mupen64plus.cfg: `[Section]`,
 * `key = value`, `#` comments, unknown keys ignored. Little-endian hosts
 * and SH4 both read the same bytes. Wii64's raw struct dump is not used.
 *
 * Flycast/RetroArch-style screens (Settings, Controls) are opened from the
 * ROM browser with Y and X. skipMenu stays file-only.
 */

#define DC_SETTINGS_VERSION 1

enum {
	DC_SET_AUDIO = 0,
	DC_SET_FPS,
	DC_SET_DEBUG,
	DC_SET_AUTOSAVE,
	DC_SET_AUTOLOAD,
	DC_SET_ASPECT,
	DC_SET_VIDEO,
	DC_SET_SAVES,
	DC_SET_VILIMIT,
	DC_SET_LOGFILE,
	DC_SET_SKIPMENU,
	DC_SET_COUNT
};

void dc_settings_defaults(void);
void dc_settings_boot(void);
int dc_settings_load(const char *path);
int dc_settings_save(const char *path);
const char *dc_settings_path(void);

int dc_settings_count(void);
int dc_settings_visible_count(void);
const char *dc_settings_label(int i);
const char *dc_settings_value(int i);
const char *dc_settings_help(int i);
void dc_settings_cycle(int i, int dir);

void dc_settings_enter(void);
int dc_settings_step(const BUTTONS *keys);
void dc_settings_draw(void);

int dc_controls_row_count(void);
void dc_controls_enter(void);
int dc_controls_step(const BUTTONS *keys);
void dc_controls_draw(void);

#ifdef DC_HOST_STUB
int dc_settings_selftest(void);
#endif

#endif
