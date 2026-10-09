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
#include "dc_menu/dc_ui.h"
#include "dc_storage.h"
#include <stddef.h>
#include <errno.h>
#include <limits.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../main/wii64config.h"
#include "../main/timers.h"
#include "../fileBrowser/fileBrowser.h"
#include "../fileBrowser/fileBrowser-kos.h"
#include "dc_debug.h"
#include "dc_gfx.h"

extern char audioEnabled;
#include "dc_audio.h"
extern char printToSD;
extern char saveEnabled;
extern char *get_savespath(void);

static const char *onoff[] = { "Off", "On" };
static const char *aspect[] = { "Original", "Full screen", "Wide TV" };
static const char *video[] = { "VGA", "RGB / 480i" };
static const char *saves[] = { "SD", "VMU (later)" };
static const char *vilimit[] = { "Off", "Wait for VI", "Wait for frame" };
static const char *skipframes[] = { "Off", "1", "2", "3", "4", "5", "2 / 3", "2 / 3 / 3 / 3" };

struct dc_setting {
	const char *key;
	const char *label;
	const char *help;
	const char *const *names;
	int nnames;
};

static const struct dc_setting table[DC_SET_COUNT] = {
	{ "audio",    "Audio",           "Play game sound.", onoff, 2 },
	{ "fps",      "Show FPS",        "Overlay once a renderer exists.", onoff, 2 },
	{ "debug",    "Debug overlay",   "Show diagnostic messages on the console.", onoff, 2 },
	{ "autosave", "Auto-save progress", "Keep cartridge and Controller Pak saves on SD.", onoff, 2 },
	{ "autoload", "Auto-load progress", "Restore cartridge and Controller Pak saves at launch.", onoff, 2 },
	{ "aspect",   "Aspect",          "Original proportions, fill the screen, or correct a wide TV.", aspect, 3 },
	{ "video",    "Video out",       "VGA box vs RGB/composite. Auto-detect is a human call.", video, 2 },
	{ "saves",    "Save device",     "VMU layout for FlashRAM is a human call.", saves, 2 },
	{ "vilimit",  "VI limiter",      "Same three modes as Wii64.", vilimit, 3 },
	{ "logfile",  "Log file",        "Write diagnostic messages to SD.", onoff, 2 },
	{ "frameskip", "Frameskip",       "Higher values draw less often. The last option uses a repeating skip pattern for Stadium 2.", skipframes, 8 },
	{ "skipMenu", "Skip menu",       "Bring-up / argv path. Keep Off on hardware.", onoff, 2 },
};


static int set_cursor, set_top, ctl_cursor, ctl_top, ctl_port;
static dc_ui_input set_input, ctl_input;
static char settings_status[96] = "Settings apply immediately.";
static int settings_missing;
static controller_config_t mappings[4];
static int mappings_ready;
static const int visible[] = { DC_SET_AUDIO, DC_SET_AUTOSAVE, DC_SET_AUTOLOAD,
    DC_SET_FRAMESKIP, DC_SET_ASPECT, DC_SET_DEBUG, DC_SET_LOGFILE };
struct binding { const char *key, *label; size_t offset; int kind; };
#define BIND(field, label, kind) {#field, label, offsetof(controller_config_t, field), kind}
static const struct binding bindings[] = {
    BIND(A,"N64 A",0), BIND(B,"N64 B",0), BIND(START,"N64 Start",0),
    BIND(Z,"N64 Z",0), BIND(L,"N64 L",0), BIND(R,"N64 R",0),
    BIND(CU,"C-Up",0), BIND(CD,"C-Down",0), BIND(CL,"C-Left",0), BIND(CR,"C-Right",0),
    BIND(DU,"D-pad Up",0), BIND(DD,"D-pad Down",0), BIND(DL,"D-pad Left",0), BIND(DR,"D-pad Right",0),
    BIND(analog,"Analog source",1), BIND(exit,"Pause shortcut",2)
};
#define BIND_COUNT ((int)(sizeof(bindings)/sizeof(bindings[0])))
static const int button_choices[] = {0,1,2,3,4,5,6,7,8,9,13,14,15,16,17,18,19};
static button_tp *binding_ptr(int port, int row) {
    return (button_tp *)((char *)&mappings[port] + bindings[row].offset);
}
static button_tp binding_button(int row, int index) {
    int kind = bindings[row].kind, i;
    if (kind == 1) return index >= 0 && index < controller_DC.num_analog_sources ? &controller_DC.analog_sources[index] : NULL;
    if (kind == 2) return index >= 0 && index < controller_DC.num_menu_combos ? &controller_DC.menu_combos[index] : NULL;
    for (i=0; i<(int)(sizeof(button_choices)/sizeof(button_choices[0])); i++)
        if (index == button_choices[i]) return &controller_DC.buttons[index];
    return NULL;
}
void dc_controls_apply(void) {
    int i;
    if (!mappings_ready) {
        for (i=0;i<4;i++) mappings[i] = controller_DC.config_default;
        mappings_ready=1;
    }
    for (i=0;i<4;i++) controller_DC.config[i] = mappings[i];
}
const char *dc_settings_status(void) { return settings_status; }

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
	case DC_SET_FRAMESKIP: return (int)dc_gfx_get_frameskip();
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
	case DC_SET_AUDIO:    audio_dc_set_enabled(v); break;
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
	case DC_SET_FRAMESKIP: dc_gfx_set_frameskip((unsigned)v); break;
	case DC_SET_SKIPMENU: skipMenu = (char)v; break;
	default: break;
	}
}

void dc_settings_defaults(void)
{
	int port;
	for (port=0;port<4;port++) mappings[port]=controller_DC.config_default;
	mappings_ready=1;
	dc_controls_apply();
	skipMenu = 0;
	dc_gfx_set_frameskip(0);
#ifdef DC_GAME_FRAMESKIP
	dc_gfx_set_frameskip(DC_GAME_FRAMESKIP);
#endif
	audio_dc_set_enabled(1);
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
    return (int)(sizeof(visible)/sizeof(visible[0]));
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
    snprintf(settings_status,sizeof(settings_status),"Unsaved changes. B saves and returns.");
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


static int parse_val(const char *s, int *value)
{
    char *end;
    long v;
    if (!strcmp(s,"True") || !strcmp(s,"true") || !strcmp(s,"On") || !strcmp(s,"on")) { *value=1; return 1; }
    if (!strcmp(s,"False") || !strcmp(s,"false") || !strcmp(s,"Off") || !strcmp(s,"off")) { *value=0; return 1; }
    errno=0; v=strtol(s,&end,10);
    if (end==s || *end || errno || v<0 || v>INT_MAX) return 0;
    *value=(int)v; return 1;
}

int dc_settings_save(const char *path)
{
    FILE *fp;
    char temporary[256];
    int i,j,failed=0;
    fileBrowser_kos_bind();
    if (saveFile_dir) fileBrowser_kos_init(saveFile_dir);
    if (!path) path=dc_settings_path();
    if (snprintf(temporary,sizeof(temporary),"%s.tmp",path)>=(int)sizeof(temporary)) goto failure;
    fp=fopen(temporary,"w");
    if (!fp) goto failure;
    dc_controls_apply();
    fprintf(fp,"# Not64 DC settings version %d\n[Core]\n",DC_SETTINGS_VERSION);
    for (i=0;i<DC_SET_COUNT;i++) fprintf(fp,"%s = %d\n",table[i].key,get_int(i));
    fprintf(fp,"\n[Input]\n");
    for (i=0;i<4;i++) {
        fprintf(fp,"pak%d = %d\n",i+1,pakMode[i]==PAKMODE_RUMBLEPAK?1:0);
        for (j=0;j<BIND_COUNT;j++) fprintf(fp,"port%d_%s = %d\n",i+1,bindings[j].key,(*binding_ptr(i,j))->index);
        fprintf(fp,"port%d_inverted = %d\n",i+1,mappings[i].inverted);
    }
    if (ferror(fp) || fflush(fp)) failed=1;
    if (fclose(fp)) failed=1;
    if (failed || dc_storage_commit(temporary,path)) { remove(temporary); goto failure; }
    snprintf(settings_status,sizeof(settings_status),"Settings saved to SD.");
    return 0;
failure:
    snprintf(settings_status,sizeof(settings_status),"SD save failed. Changes apply this session.");
    return -1;
}

int dc_settings_load(const char *path)
{
    FILE *fp;
    char line[192];
    int failed;
    settings_missing=0;
    if (!path) path=dc_settings_path();
    fp=fopen(path,"r");
    if (!fp) {
        settings_missing=errno==ENOENT;
        snprintf(settings_status,sizeof(settings_status),"%s",settings_missing?
            "No saved settings. Using current settings.":"Settings unreadable. Check SD; file kept.");
        return -1;
    }
    dc_controls_apply();
    while (fgets(line,sizeof(line),fp)) {
        char *eq,*key,*val,*nl;
        int i,j,v;
        nl=strpbrk(line,"\r\n"); if(nl)*nl=0;
        key=trim(line);
        if (*key=='#' || !*key || *key=='[') continue;
        eq=strchr(key,'='); if(!eq)continue;
        *eq=0; key=trim(key); val=trim(eq+1);
        if (!parse_val(val,&v)) continue;
        if (!strncmp(key,"pak",3) && key[3]>='1' && key[3]<='4' && !key[4] && v<=1) {
            pakMode[key[3]-'1']=v?PAKMODE_RUMBLEPAK:PAKMODE_MEMPAK;
            continue;
        }
        if (!strncmp(key,"port",4) && key[4]>='1' && key[4]<='4' && key[5]=='_') {
            i=key[4]-'1';
            if (!strcmp(key+6,"inverted") && v<=3) mappings[i].inverted=v;
            for (j=0;j<BIND_COUNT;j++) if (!strcmp(key+6,bindings[j].key)) {
                button_tp b=binding_button(j,v);
                if (b) *binding_ptr(i,j)=b;
                break;
            }
            continue;
        }
        for (i=0;i<DC_SET_COUNT;i++) if (!strcmp(key,table[i].key)) {
            if(v<table[i].nnames)set_int(i,v);
            break;
        }
    }
    failed=ferror(fp); if(fclose(fp))failed=1;
    dc_controls_apply(); apply_pak_modes();
    snprintf(settings_status,sizeof(settings_status),"%s",failed?"Settings unreadable. Check SD; file kept.":"Settings loaded.");
    return failed?-1:0;
}

void dc_settings_boot(void) {
    dc_settings_defaults();
    /* Only a missing file warrants creating defaults. Keep unreadable files. */
    if (dc_settings_load(NULL) && settings_missing) (void)dc_settings_save(NULL);
}
void dc_settings_enter(void) {
    set_cursor=set_top=0;
    dc_ui_input_init(&set_input,1);
}
int dc_settings_step(const BUTTONS *keys) {
    unsigned pressed=dc_ui_input_step(&set_input,keys);
    if (pressed&DC_UI_B) return 0;
    dc_ui_scroll(&set_cursor,&set_top,dc_settings_visible_count(),DC_UI_ROWS,pressed);
    if (pressed&DC_UI_LEFT) dc_settings_cycle(visible[set_cursor],-1);
    if (pressed&(DC_UI_A|DC_UI_RIGHT)) dc_settings_cycle(visible[set_cursor],1);
    return 1;
}
void dc_settings_draw(void) {
    int i;
    dc_ui_begin("Settings","Changes apply immediately; B saves and returns.");
    for (i=0;i<DC_UI_ROWS && set_top+i<dc_settings_visible_count();i++) {
        int row=set_top+i, id=visible[row];
        dc_ui_row(DC_UI_FIRST_ROW+i,dc_settings_label(id),dc_settings_value(id),row==set_cursor);
    }
    dc_ui_paragraph(14,2,DC_UI_MUTED,dc_settings_help(visible[set_cursor]));
    dc_ui_finish(settings_status,"Up/Down select   Left/Right or A change   B back");
}

int dc_controls_row_count(void) { return ctl_port<0?8:BIND_COUNT+3; }
void dc_controls_enter(void) {
    dc_controls_apply();
    ctl_cursor=ctl_top=0; ctl_port=-1;
    dc_ui_input_init(&ctl_input,1);
}
static void cycle_binding(int port,int row,int dir) {
    button_tp *b=binding_ptr(port,row);
    int i,index=(*b)->index,kind=bindings[row].kind;
    if (kind) {
        int count=kind==1?controller_DC.num_analog_sources:controller_DC.num_menu_combos;
        *b=binding_button(row,(index+dir+count)%count);
    } else {
        int count=(int)(sizeof(button_choices)/sizeof(button_choices[0]));
        for (i=0;i<count;i++) if(button_choices[i]==index)break;
        *b=binding_button(row,button_choices[(i+dir+count)%count]);
    }
}
int dc_controls_step(const BUTTONS *keys) {
    unsigned pressed=dc_ui_input_step(&ctl_input,keys);
    if (pressed&DC_UI_B) {
        if (ctl_port<0)return 0;
        ctl_cursor=ctl_port; ctl_port=-1; ctl_top=0;
        dc_ui_input_init(&ctl_input,1); return 1;
    }
    /* The overview's last four rows explain controls; only ports can be edited. */
    dc_ui_scroll(&ctl_cursor,&ctl_top,ctl_port<0?4:dc_controls_row_count(),DC_UI_ROWS,pressed);
    if (ctl_port<0) {
        if (ctl_cursor<4 && (pressed&(DC_UI_A|DC_UI_START))) {
            ctl_port=ctl_cursor; ctl_cursor=ctl_top=0; dc_ui_input_init(&ctl_input,1);
        }
    } else if (pressed&(DC_UI_A|DC_UI_LEFT|DC_UI_RIGHT)) {
        int dir=pressed&DC_UI_LEFT?-1:1;
        if (ctl_cursor<BIND_COUNT) cycle_binding(ctl_port,ctl_cursor,dir);
        else if (ctl_cursor==BIND_COUNT) mappings[ctl_port].inverted^=2;
        else if (ctl_cursor==BIND_COUNT+1) mappings[ctl_port].inverted^=1;
        else if (pressed&DC_UI_A) mappings[ctl_port]=controller_DC.config_default;
        dc_controls_apply();
        snprintf(settings_status,sizeof(settings_status),"Unsaved changes. B saves and returns.");
    }
    return 1;
}
void dc_controls_draw(void) {
    char title[64],label[64];
    int i;
    snprintf(title,sizeof(title),"Controls%s",ctl_port<0?"":" / Edit mapping");
    snprintf(label,sizeof(label),ctl_port<0?"Select a controller port to edit its mapping.":"Port %d   Menu controls stay unchanged.",ctl_port+1);
    dc_ui_begin(title,label);
    for (i=0;i<DC_UI_ROWS && ctl_top+i<dc_controls_row_count();i++) {
        int row=ctl_top+i;
        const char *name,*value;
        if (ctl_port<0) {
            static const char *tips[]={"Both triggers + D-pad","Start + A + B","Memory Pak saves","Rumble"};
            static const char *values[]={"Default C buttons","Default pause","SD required","Unavailable"};
            if(row<4) { snprintf(label,sizeof(label),"Port %d mapping",row+1); name=label; value="Edit"; }
            else { name=tips[row-4]; value=values[row-4]; }
        } else if (row<BIND_COUNT) { name=bindings[row].label; value=(*binding_ptr(ctl_port,row))->name; }
        else if(row==BIND_COUNT) { name="Invert horizontal"; value=onoff[!!(mappings[ctl_port].inverted&2)]; }
        else if(row==BIND_COUNT+1) { name="Invert vertical"; value=onoff[!!(mappings[ctl_port].inverted&1)]; }
        else { name="Restore this port"; value="A: defaults"; }
        dc_ui_row(DC_UI_FIRST_ROW+i,name,value,row==ctl_cursor);
    }
    dc_ui_paragraph(14,2,DC_UI_MUTED,ctl_port<0?"Mappings are saved with settings. Standard controllers use Memory Paks; Jump Pack rumble is not available.":"Choose the Dreamcast input for each N64 control. Both triggers plus a direction selects a C button.");
    dc_ui_finish(settings_status,ctl_port<0?"Up/Down select   A edit port   B back":"Up/Down select   Left/Right or A change   B back");
}

#ifdef DC_HOST_STUB
int dc_settings_selftest(void) {
    int fails=0;
    const char *path="not64-settings-test.cfg";
    FILE *fp;
    BUTTONS k;
    dc_settings_defaults();
    dc_settings_cycle(DC_SET_AUDIO,1);
    mappings[0].A=&controller_DC.buttons[7];
    mappings[3].inverted=3;
    dc_gfx_set_frameskip(DC_FRAMESKIP_STEADY);
    if(dc_settings_save(path))fails++;
    dc_settings_defaults();
    if(dc_settings_load(path) || audioEnabled || mappings[0].A->index!=7 || mappings[3].inverted!=3 || dc_gfx_get_frameskip()!=DC_FRAMESKIP_STEADY)fails++;
    controller_DC.config[0]=controller_DC.config_default;
    dc_controls_apply();
    if(controller_DC.config[0].A->index!=7)fails++;
    fp=fopen(path,"w");
    if(fp) { fputs("audio = garbage\nframeskip = 99\nport1_A = 12\nport4_inverted = -1\n",fp); fclose(fp); }
    if(dc_settings_load(path) || audioEnabled || dc_gfx_get_frameskip()!=DC_FRAMESKIP_STEADY || mappings[0].A->index!=7 || mappings[3].inverted!=3)fails++;
    if(!dc_settings_save("missing-directory/settings.cfg") || !strstr(settings_status,"this session"))fails++;
    dc_settings_enter(); memset(&k,0,sizeof(k)); k.A_BUTTON=1;
    dc_settings_step(&k); if(audioEnabled)fails++;
    memset(&k,0,sizeof(k)); dc_settings_step(&k); k.A_BUTTON=1; dc_settings_step(&k); if(!audioEnabled)fails++;
    dc_draw_init(); dc_settings_draw();
    if(!strstr(dc_draw_host_row(1),"Settings") || !strstr(dc_draw_host_row(4),"Audio"))fails++;
    dc_controls_enter(); dc_controls_draw();
    if(!strstr(dc_draw_host_row(4),"Port 1"))fails++;
    memset(&k,0,sizeof(k));dc_controls_step(&k);k.U_DPAD=1;dc_controls_step(&k);
    dc_controls_draw();
    if(ctl_cursor!=3 || !strstr(dc_draw_host_row(7),"> Port 4"))fails++;
    dc_controls_enter();
    memset(&k,0,sizeof(k)); dc_controls_step(&k); k.A_BUTTON=1; dc_controls_step(&k);
    if(ctl_port!=0)fails++;
    dc_controls_draw();
    if(!strstr(dc_draw_host_row(2),"Menu controls stay unchanged.") || strstr(dc_draw_host_row(2),"..."))fails++;
    memset(&k,0,sizeof(k)); dc_controls_step(&k); k.R_DPAD=1; dc_controls_step(&k);
    if(controller_DC.config[0].A->index!=8)fails++;
    dc_draw_shutdown(); remove(path); dc_settings_defaults();
    printf("settings selftest: %s (%d failure(s))\n",fails?"FAIL":"PASS",fails);
    return fails;
}
#endif
