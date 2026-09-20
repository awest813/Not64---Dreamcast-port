/**
 * Dreamcast settings file and the Settings / Controls screens.
 *
 * Format follows mupen64plus.cfg: UTF-8 ASCII, `key=value`, `#` comments.
 * Wii64 wrote a host-endian struct to settings.cfg; that is not portable
 * to SH4 and is not versioned.
 */

#include "dc_settings.h"
#include "dc_menu/dc_draw.h"
#include "dc_menu/dc_menu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../main/wii64config.h"
#include "../main/timers.h"
#include "../fileBrowser/fileBrowser.h"

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
	{ "debug",    "Debug overlay",   "Also writes ./saves/not64.log when On.", onoff, 2 },
	{ "autosave", "Auto-save SRAM",  "Write native saves when leaving a game.", onoff, 2 },
	{ "autoload", "Auto-load SRAM",  "Load native saves on ROM boot.", onoff, 2 },
	{ "aspect",   "Aspect",          "16:9 pillar-boxes the N64 4:3 frame.", aspect, 3 },
	{ "video",    "Video out",       "VGA box vs RGB/composite. Auto-detect is a human call.", video, 2 },
	{ "saves",    "Save device",     "VMU layout for FlashRAM is a human call.", saves, 2 },
	{ "vilimit",  "VI limiter",      "Same three modes as Wii64.", vilimit, 3 },
	{ "skipmenu", "Skip menu",       "Bring-up / argv path. Keep Off on hardware.", onoff, 2 },
};

static int get_int(int i)
{
	switch (i) {
	case DC_SET_AUDIO:    return audioEnabled ? 1 : 0;
	case DC_SET_FPS:      return showFPSonScreen ? 1 : 0;
	case DC_SET_DEBUG:    return printToScreen ? 1 : 0;
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
	case DC_SET_DEBUG:    printToScreen = (char)v; printToSD = (char)v; break;
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
	printToSD = 0;
	autoSave = 1;
	autoLoadSave = 1;
	saveEnabled = 1;
	screenMode = SCREENMODE_4x3;
	videoMode = 0; /* VGA; auto-detect is still a human decision */
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
}

int dc_settings_count(void)
{
	return DC_SET_COUNT;
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

int dc_settings_save(const char *path)
{
	FILE *fp;
	int i;

	if (!path)
		path = dc_settings_path();
	fp = fopen(path, "w");
	if (!fp)
		return -1;
	fprintf(fp, "# not64-dc %d\n", DC_SETTINGS_VERSION);
	for (i = 0; i < DC_SET_COUNT; ++i)
		fprintf(fp, "%s=%d\n", table[i].key, get_int(i));
	fclose(fp);
	return 0;
}

int dc_settings_load(const char *path)
{
	FILE *fp;
	char line[128];
	int saw_header = 0;

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
		if (line[0] == '#' || line[0] == '\0') {
			if (!strncmp(line, "# not64-dc ", 11))
				saw_header = 1;
			continue;
		}
		eq = strchr(line, '=');
		if (!eq)
			continue;
		*eq = '\0';
		key = line;
		val = eq + 1;
		v = atoi(val);
		for (i = 0; i < DC_SET_COUNT; ++i) {
			if (!strcmp(key, table[i].key)) {
				set_int(i, v);
				break;
			}
		}
	}
	fclose(fp);
	(void)saw_header;
	return 0;
}

void dc_settings_draw(int cursor)
{
	int i, y;
	char shown[80];

	dc_draw_clear(DC_COL_BG);
	dc_draw_fill_rect(0, 0, DC_FB_W, 56, DC_COL_BG2);
	dc_draw_fill_rect(0, 0, 8, 56, DC_COL_ACCENT);
	dc_draw_text_scaled(24, 10, DC_COL_TEXT, "Settings", 2);
	dc_draw_text(24, 40, DC_COL_DIM, "Inspired by mupen64plus.cfg, not Wii64 structs");

	dc_draw_fill_rect(16, 68, DC_FB_W - 32, 320, DC_COL_PANEL);
	dc_draw_rect(16, 68, DC_FB_W - 32, 320, DC_COL_LINE);

	for (i = 0; i < DC_SET_COUNT; ++i) {
		int sel = (i == cursor);
		y = 80 + i * 22;
		if (sel)
			dc_draw_fill_rect(24, y - 4, DC_FB_W - 48, 20, DC_COL_SEL_BG);
		dc_draw_text(32, y, sel ? DC_COL_TEXT : DC_COL_FILE,
			     dc_settings_label(i));
		dc_menu_ellipsize(shown, sizeof(shown), dc_settings_value(i), 18);
		dc_draw_text(DC_FB_W - 32 - dc_draw_text_width(shown), y,
			     sel ? DC_COL_ACCENT2 : DC_COL_DIM, shown);
	}

	dc_draw_text(32, 360, DC_COL_WARN, dc_settings_help(cursor));
	dc_draw_fill_rect(0, 424, DC_FB_W, 56, DC_COL_BG2);
	dc_draw_fill_rect(0, 424, DC_FB_W, 2, DC_COL_LINE);
	dc_draw_text(24, 440, DC_COL_DIM, "A/Left/Right change    B back (saves)");
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
	{ "Y + left trigger",  "L" },
	{ "Both triggers + D-pad", "C-Up / C-Down / C-Left / C-Right" },
	{ "X",                 "unassigned" },
	{ "Start + A + B",     "Return to menu (once 8b exists)" },
};

int dc_controls_row_count(void)
{
	return (int)(sizeof(controls) / sizeof(controls[0]));
}

void dc_controls_draw(int scroll)
{
	int i, y, n = dc_controls_row_count();
	int vis = 9;

	if (scroll < 0)
		scroll = 0;
	if (scroll > n - vis && n > vis)
		scroll = n - vis;
	if (n <= vis)
		scroll = 0;

	dc_draw_clear(DC_COL_BG);
	dc_draw_fill_rect(0, 0, DC_FB_W, 56, DC_COL_BG2);
	dc_draw_fill_rect(0, 0, 8, 56, DC_COL_ACCENT);
	dc_draw_text_scaled(24, 10, DC_COL_TEXT, "Controls", 2);
	dc_draw_text(24, 40, DC_COL_DIM, "Retail DC pad has no C, Z, L or R");

	dc_draw_fill_rect(16, 68, DC_FB_W - 32, 330, DC_COL_PANEL);
	dc_draw_rect(16, 68, DC_FB_W - 32, 330, DC_COL_LINE);

	dc_draw_text(32, 80, DC_COL_ACCENT2, "Dreamcast");
	dc_draw_text(280, 80, DC_COL_ACCENT2, "N64");

	for (i = 0; i < vis && scroll + i < n; ++i) {
		const struct dc_ctrl_row *r = &controls[scroll + i];
		y = 108 + i * 26;
		dc_draw_text(32, y, DC_COL_FILE, r->dc);
		dc_draw_text(280, y, DC_COL_TEXT, r->n64);
	}

	dc_draw_text(32, 360, DC_COL_WARN,
		     "Both triggers withhold Z+R so a C-press is not also Z+R.");
	dc_draw_fill_rect(0, 424, DC_FB_W, 56, DC_COL_BG2);
	dc_draw_fill_rect(0, 424, DC_FB_W, 2, DC_COL_LINE);
	dc_draw_text(24, 440, DC_COL_DIM, "B back    This screen is the map; 8c slots come later");
}

#ifdef DC_HOST_STUB
int dc_settings_selftest(void)
{
	int fails = 0;
	char path[] = "/tmp/not64-settings-test.cfg";
	int audio0;

	dc_settings_defaults();
	if (audioEnabled != 1) {
		printf("settings FAIL: default audio\n");
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
	if (dc_controls_row_count() < 7) {
		printf("settings FAIL: controls legend\n");
		fails++;
	}
	remove(path);
	dc_settings_defaults();
	printf("settings selftest: %s (%d failure(s))\n",
	       fails ? "FAIL" : "PASS", fails);
	return fails;
}
#endif
