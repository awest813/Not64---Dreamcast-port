/**
 * Dreamcast settings file and Settings / Controls screens.
 *
 * File format follows mupen64plus.cfg: `[Section]`, `key = value`, `#`
 * comments, unknown keys ignored. Wii64 wrote a host-endian struct; that is
 * not portable to SH4. Screens follow Flycast/RetroArch: a list of labelled
 * options with the current value on the right, help on the last row, B saves
 * and returns (mupen64plus-ui-console writes config on exit).
 */

#include "dc_settings.h"
#include "dc_menu/dc_draw.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../main/wii64config.h"
#include "../main/timers.h"
#include "../fileBrowser/fileBrowser.h"
#include "../fileBrowser/fileBrowser-kos.h"
#include "dc_debug.h"

extern char audioEnabled;
extern char printToSD;
extern char saveEnabled;
extern char *get_savespath(void);

static const char *onoff[] = { "Off", "On" };
static const char *aspect[] = { "4:3", "16:9", "16:9 pillar" };
static const char *video[] = { "VGA", "RGB / 480i" };
static const char *saves[] = { "SD", "VMU (later)" };
static const char *vilimit[] = { "Off", "Wait for VI", "Wait for frame" };

struct dc_setting {
	const char *key;
	const char *label;
	const char *help;
	const char *const *names;
	int nnames;
};

static const struct dc_setting table[DC_SET_COUNT] = {
	{ "audio",    "Audio",           "AICA drain is still a stub; this gates the ring.", onoff, 2 },
	{ "fps",      "Show FPS",        "Overlay once a renderer exists.", onoff, 2 },
	{ "debug",    "Debug overlay",   "Mirror log lines to the console.", onoff, 2 },
	{ "autosave", "Auto-save SRAM",  "Write native saves when leaving a game.", onoff, 2 },
	{ "autoload", "Auto-load SRAM",  "Load native saves on ROM boot.", onoff, 2 },
	{ "aspect",   "Aspect",          "16:9 pillar-boxes the N64 4:3 frame.", aspect, 3 },
	{ "video",    "Video out",       "VGA box vs RGB/composite. Auto-detect is a human call.", video, 2 },
	{ "saves",    "Save device",     "VMU layout for FlashRAM is a human call.", saves, 2 },
	{ "vilimit",  "VI limiter",      "Same three modes as Wii64.", vilimit, 3 },
	{ "logfile",  "Log file",        "Append saves/not64.log (session + errors).", onoff, 2 },
	{ "skipMenu", "Skip menu",       "Bring-up / argv path. Keep Off on hardware.", onoff, 2 },
};

#define COL_BG    DC_RGB(0x10, 0x14, 0x20)
#define COL_BAR   DC_RGB(0x28, 0x50, 0x90)
#define COL_TITLE DC_RGB(0xFF, 0xFF, 0xFF)
#define COL_TEXT  DC_RGB(0xC8, 0xC8, 0xC8)
#define COL_PICK  DC_RGB(0xFF, 0xFF, 0x80)
#define COL_HINT  DC_RGB(0x80, 0x88, 0x98)

#define DC_PAK_PORTS 4
#define DC_STICK_EDGE 40

static unsigned menu_now(const BUTTONS *keys)
{
	unsigned now = 0;
	int x, y;

	if (keys->B_BUTTON) now |= 1u;
	if (keys->A_BUTTON) now |= 2u;
	if (keys->L_DPAD) now |= 4u;
	if (keys->R_DPAD) now |= 8u;
	if (keys->U_DPAD) now |= 16u;
	if (keys->D_DPAD) now |= 32u;
	x = (int)(signed char)keys->X_AXIS;
	y = (int)(signed char)keys->Y_AXIS;
	if (x <= -DC_STICK_EDGE) now |= 4u;
	if (x >= DC_STICK_EDGE) now |= 8u;
	if (y >= DC_STICK_EDGE) now |= 16u;
	if (y <= -DC_STICK_EDGE) now |= 32u;
	return now;
}

static int set_cursor, set_prev, ctl_cursor, ctl_scroll, ctl_prev;

static int get_int(int i)
{
	switch (i) {
	case DC_SET_AUDIO:    return audioEnabled ? 1 : 0;
	case DC_SET_FPS:      return showFPSonScreen ? 1 : 0;
	case DC_SET_DEBUG:    return printToScreen ? 1 : 0;
	case DC_SET_LOGFILE:  return printToSD ? 1 : 0;
	case DC_SET_AUTOSAVE: return autoSave ? 1 : 0;
	case DC_SET_AUTOLOAD: return autoLoadSave ? 1 : 0;
	case DC_SET_ASPECT:   return (int)(unsigned char)screenMode;
	case DC_SET_VIDEO:    return videoMode ? 1 : 0;
	case DC_SET_SAVES:    return (nativeSaveDevice == NATIVESAVEDEVICE_CARDA) ? 1 : 0;
	case DC_SET_VILIMIT:  return (int)Timers.limitVIs;
	case DC_SET_SKIPMENU: return skipMenu ? 1 : 0;
	default:              return 0;
	}
}

static void set_int(int i, int v)
{
	if (v < 0)
		v = 0;
	if (i >= 0 && i < DC_SET_COUNT && v >= table[i].nnames)
		v = table[i].nnames - 1;

	switch (i) {
	case DC_SET_AUDIO:    audioEnabled = (char)v; break;
	case DC_SET_FPS:      showFPSonScreen = (char)v; break;
	case DC_SET_DEBUG:    printToScreen = (char)v; break;
	case DC_SET_LOGFILE:  dc_debug_set_file(v); break;
	case DC_SET_AUTOSAVE: autoSave = (char)v; saveEnabled = (char)v; break;
	case DC_SET_AUTOLOAD: autoLoadSave = (char)v; break;
	case DC_SET_ASPECT:   screenMode = (char)v; break;
	case DC_SET_VIDEO:    videoMode = (char)v; break;
	case DC_SET_SAVES:
		nativeSaveDevice = v ? NATIVESAVEDEVICE_CARDA : NATIVESAVEDEVICE_SD;
		saveStateDevice = SAVESTATEDEVICE_SD;
		break;
	case DC_SET_VILIMIT:  Timers.limitVIs = (char)v; break;
	case DC_SET_SKIPMENU: skipMenu = (char)v; break;
	default: break;
	}
}

void dc_settings_defaults(void)
{
	skipMenu = 0;
	audioEnabled = 1;
	showFPSonScreen = 1;
	printToScreen = 1;
	printToSD = 1;
	autoSave = 1;
	autoLoadSave = 1;
	saveEnabled = 1;
	screenMode = SCREENMODE_4x3;
	videoMode = 0;
	nativeSaveDevice = NATIVESAVEDEVICE_SD;
	saveStateDevice = SAVESTATEDEVICE_SD;
	Timers.limitVIs = LIMITVIS_WAIT_FOR_VI;
	padAutoAssign = PADAUTOASSIGN_AUTOMATIC;
	loadButtonSlot = LOADBUTTON_DEFAULT;
	glN64_useFrameBufferTextures = 0;
	glN64_use2xSaiTextures = 0;
	renderCpuFramebuffer = 0;
	pixelClock = 0;
	trapFilter = 0;
	{
		int i;

		for (i = 0; i < 4; ++i)
			pakMode[i] = PAKMODE_MEMPAK;
	}
	apply_pak_modes();
}

int dc_settings_count(void)
{
	return DC_SET_COUNT;
}

int dc_settings_visible_count(void)
{
	/* skipMenu is file-only: turning it On from the UI would lock a
	 * hardware boot out of the menu (argc<=1). Edit settings.cfg. */
	return DC_SET_SKIPMENU;
}

const char *dc_settings_label(int i)
{
	if (i < 0 || i >= DC_SET_COUNT)
		return "";
	return table[i].label;
}

const char *dc_settings_value(int i)
{
	int v;

	if (i < 0 || i >= DC_SET_COUNT)
		return "";
	v = get_int(i);
	if (v < 0 || v >= table[i].nnames)
		return "?";
	return table[i].names[v];
}

const char *dc_settings_help(int i)
{
	if (i < 0 || i >= DC_SET_COUNT)
		return "";
	return table[i].help;
}

void dc_settings_cycle(int i, int dir)
{
	int v, n;

	if (i < 0 || i >= DC_SET_COUNT)
		return;
	n = table[i].nnames;
	v = get_int(i) + (dir >= 0 ? 1 : -1);
	if (v < 0)
		v = n - 1;
	if (v >= n)
		v = 0;
	set_int(i, v);
}

const char *dc_settings_path(void)
{
	static char path[FILE_BROWSER_MAX_PATH_LEN];
	const char *dir = get_savespath();

	if (!dir)
		dir = "./saves";
	snprintf(path, sizeof(path), "%s/settings.cfg", dir);
	return path;
}

static char *trim(char *s)
{
	char *e;

	while (*s == ' ' || *s == '\t')
		s++;
	e = s + strlen(s);
	while (e > s && (e[-1] == ' ' || e[-1] == '\t'))
		*--e = '\0';
	return s;
}

static int parse_val(const char *s)
{
	if (!strcmp(s, "True") || !strcmp(s, "true") ||
	    !strcmp(s, "On") || !strcmp(s, "on"))
		return 1;
	if (!strcmp(s, "False") || !strcmp(s, "false") ||
	    !strcmp(s, "Off") || !strcmp(s, "off"))
		return 0;
	return atoi(s);
}

int dc_settings_save(const char *path)
{
	FILE *fp;
	int i;

	fileBrowser_kos_bind();
	if (saveFile_dir)
		fileBrowser_kos_init(saveFile_dir);

	if (!path)
		path = dc_settings_path();
	fp = fopen(path, "w");
	if (!fp)
		return -1;
	fprintf(fp, "# Not64 Dreamcast settings (mupen64plus.cfg layout)\n");
	fprintf(fp, "# Unknown keys are ignored. skipMenu is file-only.\n");
	fprintf(fp, "# not64-dc %d\n\n", DC_SETTINGS_VERSION);
	fprintf(fp, "[Core]\n");
	for (i = 0; i < DC_SET_COUNT; ++i) {
		fprintf(fp, "# %s\n", table[i].help);
		fprintf(fp, "%s = %d\n", table[i].key, get_int(i));
	}
	fprintf(fp, "\n[Input]\n");
	fprintf(fp, "# 0 = Mem Pak, 1 = Rumble Pak. Jump Pack send needs KOS.\n");
	for (i = 0; i < 4; ++i)
		fprintf(fp, "pak%d = %d\n", i + 1,
			pakMode[i] == PAKMODE_RUMBLEPAK ? 1 : 0);
	fclose(fp);
	return 0;
}

int dc_settings_load(const char *path)
{
	FILE *fp;
	char line[192];

	if (!path)
		path = dc_settings_path();
	fp = fopen(path, "r");
	if (!fp)
		return -1;
	while (fgets(line, sizeof(line), fp)) {
		char *eq, *key, *val, *nl;
		int i, v;

		nl = strchr(line, '\n');
		if (nl)
			*nl = '\0';
		nl = strchr(line, '\r');
		if (nl)
			*nl = '\0';
		key = trim(line);
		if (key[0] == '#' || key[0] == '\0' || key[0] == '[')
			continue;
		eq = strchr(key, '=');
		if (!eq)
			continue;
		*eq = '\0';
		key = trim(key);
		val = trim(eq + 1);
		v = parse_val(val);
		if (!strncmp(key, "pak", 3) && key[3] >= '1' && key[3] <= '4' &&
		    !key[4]) {
			pakMode[key[3] - '1'] =
				v ? PAKMODE_RUMBLEPAK : PAKMODE_MEMPAK;
			continue;
		}
		for (i = 0; i < DC_SET_COUNT; ++i) {
			if (!strcmp(key, table[i].key)) {
				set_int(i, v);
				break;
			}
		}
	}
	fclose(fp);
	apply_pak_modes();
	return 0;
}

void dc_settings_boot(void)
{
	dc_settings_defaults();
	if (dc_settings_load(NULL) != 0)
		(void)dc_settings_save(NULL);
}

void dc_settings_enter(void)
{
	set_cursor = 0;
	set_prev = ~0; /* swallow a held stick/button from the ROM browser */
}

int dc_settings_step(const BUTTONS *keys)
{
	unsigned now = 0, pressed;
	int vis = dc_settings_visible_count();

	if (!keys)
		return 1;
	now = menu_now(keys);
	pressed = now & ~(unsigned)set_prev;
	set_prev = (int)now;
	if (pressed & 1u)
		return 0;
	if (pressed & 4u)
		dc_settings_cycle(set_cursor, -1);
	if (pressed & (2u | 8u))
		dc_settings_cycle(set_cursor, 1);
	if (pressed & 16u)
		set_cursor--;
	if (pressed & 32u)
		set_cursor++;
	if (set_cursor < 0)
		set_cursor = vis - 1;
	if (set_cursor >= vis)
		set_cursor = 0;
	return 1;
}

void dc_settings_draw(void)
{
	const int cw = dc_draw_char_w();
	const int ch = dc_draw_char_h();
	char line[128];
	int i, vis = dc_settings_visible_count();

	dc_draw_begin(COL_BG);
	dc_draw_fill_rect(0, 0, dc_draw_width(), ch, COL_BAR);
	dc_draw_text(cw, 0, COL_TITLE, "Settings");
	for (i = 0; i < vis; ++i) {
		int y = ch * (i + 2);
		if (i == set_cursor)
			dc_draw_fill_rect(0, y, dc_draw_width(), ch, COL_BAR);
		snprintf(line, sizeof(line), "%c %-16.16s %s",
			 i == set_cursor ? '>' : ' ',
			 dc_settings_label(i), dc_settings_value(i));
		dc_draw_text(cw, y, i == set_cursor ? COL_PICK : COL_TEXT, line);
	}
	dc_draw_text(cw, dc_draw_height() - ch * 2, COL_HINT,
		     dc_settings_help(set_cursor));
	dc_draw_text(cw, dc_draw_height() - ch, COL_HINT,
		     "A/R change   L previous   B back");
	dc_draw_end();
}

struct dc_ctrl_row {
	const char *dc;
	const char *n64;
};

static const struct dc_ctrl_row controls[] = {
	{ "A / B / Start",     "A / B / Start" },
	{ "Analog stick",      "Analog stick  (-80..+80, deadzone 10)" },
	{ "D-pad",             "D-pad" },
	{ "Left trigger",      "Z" },
	{ "Right trigger",     "R" },
	{ "Y + left trigger",  "L  (no Z)" },
	{ "X",                 "L  (Z still available on LT)" },
	{ "Both triggers + D-pad", "C-Up / C-Down / C-Left / C-Right" },
	{ "Y (in menu)",       "Settings" },
	{ "X (in menu)",       "Controls" },
	{ "Start+A+B (in game)", "Pause overlay (reset / menu / slots)" },
};

int dc_controls_row_count(void)
{
	return DC_PAK_PORTS + (int)(sizeof(controls) / sizeof(controls[0]));
}

void dc_controls_enter(void)
{
	ctl_cursor = 0;
	ctl_scroll = 0;
	ctl_prev = ~0; /* swallow a held stick from the ROM browser */
}

int dc_controls_step(const BUTTONS *keys)
{
	unsigned now = 0, pressed;
	int n = dc_controls_row_count();
	int rows, vis;

	if (!keys)
		return 1;
	rows = dc_draw_height() / dc_draw_char_h() - 3;
	if (rows < 1)
		rows = 1;
	vis = n < rows ? n : rows;
	now = menu_now(keys);
	pressed = now & ~(unsigned)ctl_prev;
	ctl_prev = (int)now;
	if (pressed & 1u)
		return 0;
	if (pressed & 16u)
		ctl_cursor--;
	if (pressed & 32u)
		ctl_cursor++;
	if (ctl_cursor < 0)
		ctl_cursor = n - 1;
	if (ctl_cursor >= n)
		ctl_cursor = 0;
	if (ctl_cursor < ctl_scroll)
		ctl_scroll = ctl_cursor;
	if (ctl_cursor >= ctl_scroll + vis)
		ctl_scroll = ctl_cursor - vis + 1;
	if ((pressed & (2u | 4u | 8u)) && ctl_cursor < DC_PAK_PORTS) {
		int dir = (pressed & 4u) ? -1 : 1;
		int v = (pakMode[ctl_cursor] == PAKMODE_RUMBLEPAK) ? 1 : 0;

		v += dir;
		if (v < 0)
			v = 1;
		if (v > 1)
			v = 0;
		pakMode[ctl_cursor] = v ? PAKMODE_RUMBLEPAK : PAKMODE_MEMPAK;
		apply_pak_modes();
	}
	return 1;
}

void dc_controls_draw(void)
{
	const int cw = dc_draw_char_w();
	const int ch = dc_draw_char_h();
	char line[128];
	int i, n = dc_controls_row_count();
	int rows = dc_draw_height() / ch - 3;
	int vis;

	if (rows < 1)
		rows = 1;
	vis = n < rows ? n : rows;

	dc_draw_begin(COL_BG);
	dc_draw_fill_rect(0, 0, dc_draw_width(), ch, COL_BAR);
	dc_draw_text(cw, 0, COL_TITLE, "Controls");
	for (i = 0; i < vis && ctl_scroll + i < n; ++i) {
		int row = ctl_scroll + i;
		int y = ch * (i + 2);
		int pick = (row == ctl_cursor);

		if (pick)
			dc_draw_fill_rect(0, y, dc_draw_width(), ch, COL_BAR);
		if (row < DC_PAK_PORTS)
			snprintf(line, sizeof(line), "%c Port %d pak       %s",
				 pick ? '>' : ' ', row + 1,
				 pakMode[row] == PAKMODE_RUMBLEPAK ?
					 "Rumble Pak" : "Mem Pak");
		else {
			const struct dc_ctrl_row *r = &controls[row - DC_PAK_PORTS];
			snprintf(line, sizeof(line), "%c %-22.22s %s",
				 pick ? '>' : ' ', r->dc, r->n64);
		}
		dc_draw_text(cw, y, pick ? COL_PICK : COL_TEXT, line);
	}
	dc_draw_text(cw, dc_draw_height() - ch, COL_HINT,
		     "A/R pak   L previous   B back");
	dc_draw_end();
}

#ifdef DC_HOST_STUB
int dc_settings_selftest(void)
{
	int fails = 0;
	char path[] = "/tmp/not64-settings-test.cfg";
	int audio0;
	FILE *fp;

	dc_settings_defaults();
	if (audioEnabled != 1) {
		printf("settings FAIL: default audio\n");
		fails++;
	}
	if (pakMode[0] != PAKMODE_MEMPAK || pakMode[3] != PAKMODE_MEMPAK) {
		printf("settings FAIL: default pak is Mem Pak\n");
		fails++;
	}
	audio0 = audioEnabled;
	dc_settings_cycle(DC_SET_AUDIO, 1);
	if (audioEnabled == audio0) {
		printf("settings FAIL: cycle audio\n");
		fails++;
	}
	dc_settings_cycle(DC_SET_ASPECT, 1);
	dc_settings_cycle(DC_SET_ASPECT, 1);
	if (screenMode != SCREENMODE_16x9_PILLARBOX) {
		printf("settings FAIL: aspect wrap\n");
		fails++;
	}
	dc_settings_cycle(DC_SET_VIDEO, 1);
	if (!videoMode) {
		printf("settings FAIL: video RGB\n");
		fails++;
	}
	if (dc_settings_save(path) != 0) {
		printf("settings FAIL: save\n");
		fails++;
	}
	dc_settings_defaults();
	if (dc_settings_load(path) != 0) {
		printf("settings FAIL: load\n");
		fails++;
	}
	if (audioEnabled == audio0) {
		printf("settings FAIL: load did not restore audio\n");
		fails++;
	}
	if (screenMode != SCREENMODE_16x9_PILLARBOX) {
		printf("settings FAIL: load aspect\n");
		fails++;
	}
	if (!videoMode) {
		printf("settings FAIL: load video\n");
		fails++;
	}
	pakMode[0] = PAKMODE_RUMBLEPAK;
	pakMode[1] = PAKMODE_MEMPAK;
	if (dc_settings_save(path) != 0) {
		printf("settings FAIL: save pak\n");
		fails++;
	}
	dc_settings_defaults();
	if (dc_settings_load(path) != 0 ||
	    pakMode[0] != PAKMODE_RUMBLEPAK ||
	    pakMode[1] != PAKMODE_MEMPAK) {
		printf("settings FAIL: load pak %d %d\n",
		       pakMode[0], pakMode[1]);
		fails++;
	}
	fp = fopen(path, "w");
	if (!fp) {
		printf("settings FAIL: rewrite fixture\n");
		fails++;
	} else {
		fputs("# mupen64plus-style\n[Core]\n# comment\naudio = Off\n"
		      "unknown_key = 9\nfps = True\n[Video]\naspect = 2\n", fp);
		fclose(fp);
		dc_settings_defaults();
		if (dc_settings_load(path) != 0 || audioEnabled ||
		    !showFPSonScreen || screenMode != SCREENMODE_16x9_PILLARBOX) {
			printf("settings FAIL: section/True/unknown-key parse\n");
			fails++;
		}
	}
	if (dc_controls_row_count() < 7) {
		printf("settings FAIL: controls legend\n");
		fails++;
	}
	if (dc_settings_visible_count() >= dc_settings_count() ||
	    !strcmp(dc_settings_label(dc_settings_visible_count() - 1),
		    "Skip menu")) {
		printf("settings FAIL: skipMenu must stay off the screen\n");
		fails++;
	}
	if (!printToSD) {
		printf("settings FAIL: log file should default On\n");
		fails++;
	}
	{
		int overlay0 = printToScreen;
		int file0 = printToSD;

		dc_settings_cycle(DC_SET_DEBUG, 1);
		if (printToScreen == overlay0 || printToSD != file0) {
			printf("settings FAIL: debug overlay must not toggle logfile\n");
			fails++;
		}
		dc_settings_cycle(DC_SET_LOGFILE, 1);
		if (printToSD == file0) {
			printf("settings FAIL: cycle logfile\n");
			fails++;
		}
		if (dc_settings_save(path) != 0) {
			printf("settings FAIL: save logfile\n");
			fails++;
		}
		dc_settings_defaults();
		if (dc_settings_load(path) != 0) {
			printf("settings FAIL: load logfile\n");
			fails++;
		}
		if (printToSD) {
			printf("settings FAIL: load did not restore logfile Off\n");
			fails++;
		}
		if (printToScreen == overlay0) {
			printf("settings FAIL: load did not restore overlay Off\n");
			fails++;
		}
	}
	{
		BUTTONS k;

		dc_settings_defaults();
		dc_settings_enter();
		dc_draw_init();
		dc_settings_draw();
		{
			const char *row = dc_draw_host_row(0);
			if (!row || !strstr(row, "Settings")) {
				printf("settings FAIL: title row '%s'\n", row ? row : "(null)");
				fails++;
			}
			row = dc_draw_host_row(2);
			if (!row || !strstr(row, "Audio")) {
				printf("settings FAIL: audio row '%s'\n", row ? row : "(null)");
				fails++;
			}
		}
		memset(&k, 0, sizeof(k));
		k.X_AXIS = 80;
		dc_settings_step(&k);
		if (!audioEnabled) {
			printf("settings FAIL: opening stick must not cycle Audio\n");
			fails++;
		}
		memset(&k, 0, sizeof(k));
		dc_settings_step(&k);
		k.D_DPAD = 1;
		dc_settings_step(&k);
		memset(&k, 0, sizeof(k));
		dc_settings_step(&k);
		k.A_BUTTON = 1;
		dc_settings_step(&k);
		if (showFPSonScreen) {
			printf("settings FAIL: A did not cycle Show FPS\n");
			fails++;
		}
		memset(&k, 0, sizeof(k));
		k.B_BUTTON = 1;
		if (dc_settings_step(&k)) {
			printf("settings FAIL: B did not leave settings\n");
			fails++;
		}
		dc_controls_enter();
		dc_controls_draw();
		{
			const char *row = dc_draw_host_row(0);
			if (!row || !strstr(row, "Controls")) {
				printf("settings FAIL: controls title '%s'\n",
				       row ? row : "(null)");
				fails++;
			}
			row = dc_draw_host_row(2);
			if (!row || !strstr(row, "Port 1") ||
			    !strstr(row, "Mem Pak")) {
				printf("settings FAIL: port 1 pak '%s'\n",
				       row ? row : "(null)");
				fails++;
			}
		}
		memset(&k, 0, sizeof(k));
		k.X_AXIS = 80;
		dc_controls_step(&k);
		if (pakMode[0] != PAKMODE_MEMPAK) {
			printf("settings FAIL: opening stick must not cycle pak\n");
			fails++;
		}
		memset(&k, 0, sizeof(k));
		dc_controls_step(&k);
		k.A_BUTTON = 1;
		dc_controls_step(&k);
		if (pakMode[0] != PAKMODE_RUMBLEPAK) {
			printf("settings FAIL: A did not cycle Port 1 pak\n");
			fails++;
		}
		{
			int i;
			const char *row;

			memset(&k, 0, sizeof(k));
			dc_controls_step(&k);
			for (i = 0; i < DC_PAK_PORTS; ++i) {
				k.D_DPAD = 1;
				dc_controls_step(&k);
				memset(&k, 0, sizeof(k));
				dc_controls_step(&k);
			}
			dc_controls_draw();
			row = dc_draw_host_row(2 + DC_PAK_PORTS);
			if (!row || !strchr(row, '>') || !strstr(row, "A / B")) {
				printf("settings FAIL: legend cursor '%s'\n",
				       row ? row : "(null)");
				fails++;
			}
		}
		dc_draw_shutdown();
	}
	remove(path);
	dc_settings_defaults();
	printf("settings selftest: %s (%d failure(s))\n",
	       fails ? "FAIL" : "PASS", fails);
	return fails;
}
#endif
