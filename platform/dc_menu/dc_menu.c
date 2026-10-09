/**
 * Not64 Dreamcast menu — Phase 8a ROM browser.
 *
 * See dc_menu.h for scope. Drawing goes through dc_draw.h. The ROM list
 * reads unshifted Maple (controller_DC_poll_raw) so C-shift cannot steal
 * the D-pad; getKeys() is the in-game map. Y opens Settings, X opens
 * Controls (Flycast/RetroArch extra screens, mupen64plus.cfg on disk).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dc_menu.h"
#include "dc_draw.h"
#include "dc_ui.h"
#include "../dc_settings.h"
#include "../../gc_input/controller.h"

extern void getKeys(int Control, BUTTONS *Keys);

/* Edge-detection mask. Built by hand rather than from BUTTONS.Value: the bit
 * order inside that bitfield is implementation-defined, and DWORD is 8 bytes
 * on an LP64 host and 4 on SH4, so the raw word is not a portable key. */
enum {
	DC_MENU_EDGE_A     = 1u << 0,
	DC_MENU_EDGE_B     = 1u << 1,
	DC_MENU_EDGE_START = 1u << 2,
	DC_MENU_EDGE_Z     = 1u << 3,
	DC_MENU_EDGE_R     = 1u << 4,
	DC_MENU_EDGE_LEFT  = 1u << 5,
	DC_MENU_EDGE_RIGHT = 1u << 6
};

static unsigned int decode_edges(const BUTTONS *k)
{
	unsigned int m = 0;

	if (k->A_BUTTON)     m |= DC_MENU_EDGE_A;
	if (k->B_BUTTON)     m |= DC_MENU_EDGE_B;
	if (k->START_BUTTON) m |= DC_MENU_EDGE_START;
	if (k->Z_TRIG)       m |= DC_MENU_EDGE_Z;
	if (k->R_TRIG)       m |= DC_MENU_EDGE_R;
	if (k->L_DPAD)       m |= DC_MENU_EDGE_LEFT;
	if (k->R_DPAD)       m |= DC_MENU_EDGE_RIGHT;
	return m;
}

/* ---- list ------------------------------------------------------------- */

static const char *basename_of(const char *path)
{
	const char *slash = strrchr(path, '/');
	return slash ? slash + 1 : path;
}

static int ext_is_rom(const char *path)
{
	static const char *exts[] = { ".z64", ".n64", ".v64" };
	size_t len = strlen(path);
	unsigned int i;

	for (i = 0; i < sizeof(exts) / sizeof(exts[0]); ++i) {
		size_t elen = strlen(exts[i]);
		const char *tail;
		unsigned int c;

		if (len < elen)
			continue;
		tail = path + len - elen;
		for (c = 0; c < elen; ++c) {
			char a = tail[c], b = exts[i][c];
			if (a >= 'A' && a <= 'Z')
				a = (char)(a - 'A' + 'a');
			if (a != b)
				break;
		}
		if (c == elen)
			return 1;
	}
	return 0;
}

static int name_cmp(const void *a, const void *b)
{
	const dc_menu_entry *ea = (const dc_menu_entry *)a;
	const dc_menu_entry *eb = (const dc_menu_entry *)b;
	const char *x = ea->label, *y = eb->label;
	if (ea->directory != eb->directory) return ea->directory ? -1 : 1;

	for (; *x && *y; ++x, ++y) {
		char cx = *x, cy = *y;
		if (cx >= 'A' && cx <= 'Z') cx = (char)(cx - 'A' + 'a');
		if (cy >= 'A' && cy <= 'Z') cy = (char)(cy - 'A' + 'a');
		if (cx != cy)
			return (cx < cy) ? -1 : 1;
	}
	if (*x == *y)
		return 0;
	return *x ? 1 : -1;
}

int dc_menu_list_load(dc_menu_list *list, fileBrowser_file *dir)
{
	fileBrowser_file *raw = NULL;
	int found, i, kept = 0;

	if (!list || !dir)
		return FILE_BROWSER_ERROR;

	list->items = NULL;
	list->count = 0;
	list->dir[0] = '\0';
	list->error = 0;
	strncpy(list->dir, dir->name, sizeof(list->dir) - 1);

	if (!romFile_readDir) {
		list->error=FILE_BROWSER_ERROR;
		return FILE_BROWSER_ERROR;
	}

	found = romFile_readDir(dir, &raw);
	if (found < 0) { list->error=found; free(raw); return found; }
	if (found == 0) {
		free(raw);
		return 0;
	}

	list->items = (dc_menu_entry *)malloc(sizeof(dc_menu_entry) * (size_t)found);
	if (!list->items) {
		list->error=FILE_BROWSER_ERROR;
		free(raw);
		return FILE_BROWSER_ERROR;
	}

	for (i = 0; i < found; ++i) {
		dc_menu_entry *e;

		if (!(raw[i].attr & FILE_BROWSER_ATTR_DIR) && !ext_is_rom(raw[i].name))
			continue;

		e = &list->items[kept];
		memset(e, 0, sizeof(*e));
		strncpy(e->path, raw[i].name, FILE_BROWSER_MAX_PATH_LEN - 1);
		strncpy(e->label, basename_of(raw[i].name), DC_MENU_MAX_NAME - 1);
		e->size = raw[i].size;
		e->directory = !!(raw[i].attr & FILE_BROWSER_ATTR_DIR);
		kept++;
	}
	free(raw);

	if (kept == 0) {
		free(list->items);
		list->items = NULL;
		return 0;
	}

	qsort(list->items, (size_t)kept, sizeof(dc_menu_entry), name_cmp);
	list->count = kept;
	return kept;
}

void dc_menu_list_free(dc_menu_list *list)
{
	if (!list)
		return;
	free(list->items);
	list->items = NULL;
	list->count = 0;
}

#ifndef DC_HOST_STUB
/* Where a Dreamcast keeps ROMs, in the order worth trying. Burning a CD-R is
 * how most people run homebrew, so the disc comes first; /sd/not64/roms is the
 * SD-adapter layout; /pc is dcload over serial or BBA while developing.
 * A console with no SD adapter simply has no /sd, which is why a directory
 * that will not open has to mean "nothing here", not "give up". */
static const char *const dc_rom_dirs[] = {
	"/cd/roms",
	"/cd",
	"/sd/not64/roms",
	"/pc/roms"
};
#define DC_ROM_DIR_COUNT (sizeof(dc_rom_dirs) / sizeof(dc_rom_dirs[0]))
#endif

/* ---- input ------------------------------------------------------------ */

void dc_menu_state_init(dc_menu_state *st, const dc_menu_list *list, int rows)
{
	if (!st)
		return;
	memset(st, 0, sizeof(*st));
	st->list = list;
	st->rows = rows > 0 ? rows : 1;
}

/* Keep the cursor inside the list and the window around the cursor. */
static void clamp_view(dc_menu_state *st)
{
	int count = st->list ? st->list->count : 0;

	if (count <= 0) {
		st->cursor = 0;
		st->top = 0;
		return;
	}
	if (st->cursor < 0)
		st->cursor = count - 1;		/* wrap: few entries is the norm */
	if (st->cursor >= count)
		st->cursor = 0;

	if (st->cursor < st->top)
		st->top = st->cursor;
	if (st->cursor >= st->top + st->rows)
		st->top = st->cursor - st->rows + 1;

	if (st->top > count - st->rows)
		st->top = count - st->rows;
	if (st->top < 0)
		st->top = 0;
}

/* Direction the pad is pushing this frame: -1 up, +1 down, 0 none. The D-pad
 * and the analog stick both drive it, with separate on/off thresholds on the
 * stick so a resting hand does not chatter. */
static int wanted_dir(const dc_menu_state *st, const BUTTONS *k)
{
	int y = (int)(signed char)k->Y_AXIS;
	int on = (st->repeat_dir != 0) ? DC_MENU_STICK_OFF : DC_MENU_STICK_ON;

	if (k->U_DPAD || y >= on)
		return -1;
	if (k->D_DPAD || y <= -on)
		return 1;
	return 0;
}

dc_menu_action dc_menu_step(dc_menu_state *st, const BUTTONS *keys)
{
	unsigned int now, pressed;
	int count, dir, step = 0;

	if (!st || !keys)
		return DC_MENU_NONE;

	count = st->list ? st->list->count : 0;
	now = decode_edges(keys);
	pressed = now & ~st->prev;	/* act on the press, not the hold */
	st->prev = now;

	/* A opens, B backs out, Start rescans, and Left/Right select storage. */
	if (pressed & DC_MENU_EDGE_B)
		return DC_MENU_CANCEL;
	if (pressed & DC_MENU_EDGE_START) return DC_MENU_REFRESH;
	if (pressed & DC_MENU_EDGE_LEFT) return DC_MENU_SOURCE_PREV;
	if (pressed & DC_MENU_EDGE_RIGHT) return DC_MENU_SOURCE_NEXT;
	if (count > 0 && (pressed & DC_MENU_EDGE_A)) return DC_MENU_PICK;

	if (count == 0) {
		st->repeat_dir = 0;
		st->repeat_in = 0;
		return DC_MENU_NONE;
	}

	/* Triggers page. On the Dreamcast map the left trigger is Z and the
	 * right trigger is R, so both are a single reachable press. */
	if (pressed & DC_MENU_EDGE_Z)
		step = -st->rows;
	else if (pressed & DC_MENU_EDGE_R)
		step = st->rows;

	if (step) {
		/* Paging clamps rather than wraps: holding a trigger to walk a
		 * long list should stop at the end, not cycle past it. */
		st->cursor += step;
		if (st->cursor < 0)
			st->cursor = 0;
		if (st->cursor >= count)
			st->cursor = count - 1;
		clamp_view(st);
		return DC_MENU_NONE;
	}

	dir = wanted_dir(st, keys);
	if (dir == 0) {
		st->repeat_dir = 0;
		st->repeat_in = 0;
		return DC_MENU_NONE;
	}

	if (dir != st->repeat_dir) {
		/* Fresh push: move once, then wait before repeating. */
		st->repeat_dir = dir;
		st->repeat_in = DC_MENU_REPEAT_FIRST;
		st->cursor += dir;
		clamp_view(st);
		return DC_MENU_NONE;
	}

	if (--st->repeat_in <= 0) {
		st->repeat_in = DC_MENU_REPEAT_NEXT;
		st->cursor += dir;
		clamp_view(st);
	}
	return DC_MENU_NONE;
}

/* ---- browser presentation and navigation ----------------------------- */

static void size_text(char *buf, size_t n, unsigned bytes) {
    if(bytes>=1024u*1024u)snprintf(buf,n,"%u MiB",bytes/(1024u*1024u));
    else if(bytes>=1024u)snprintf(buf,n,"%u KiB",bytes/1024u);
    else snprintf(buf,n,"%u B",bytes);
}

void dc_menu_draw(const dc_menu_state *st,const char *title) {
    char heading[96],size[20];
    int i,rows;
    if(!st)return;
    snprintf(heading,sizeof(heading),"%s   %d / %d",title?title:"Games",
        st->list && st->list->count?st->cursor+1:0,st->list?st->list->count:0);
    dc_ui_begin(heading,st->list?st->list->dir:"");
    if(!st->list || !st->list->count) {
        dc_ui_text(6,DC_UI_TEXT,st->list && st->list->error?"This source is unavailable.":"No games in this folder.");
        dc_ui_paragraph(8,3,DC_UI_MUTED,"Add .z64, .n64 or .v64 game backups. Use Left/Right to choose another source or Start to scan again.");
    } else {
        rows=st->rows<DC_UI_ROWS?st->rows:DC_UI_ROWS;
        for(i=0;i<rows && st->top+i<st->list->count;i++) {
            const dc_menu_entry *entry=&st->list->items[st->top+i];
            if(entry->directory)strcpy(size,"Folder");else size_text(size,sizeof(size),entry->size);
            dc_ui_row(DC_UI_FIRST_ROW+i,entry->label,size,st->top+i==st->cursor);
        }
        dc_ui_paragraph(14,2,DC_UI_MUTED,st->list->items[st->cursor].label);
    }
    dc_ui_finish(dc_settings_status(),"A play/open  B back  X controls  Y settings  Left/Right source  L/R page  Start scan");
}

void dc_menu_message(const char *title,const char *message) {
    if(dc_draw_init()!=0)return;
    dc_ui_begin(title?title:"Starting game","Please wait");
    dc_ui_paragraph(5,6,DC_UI_TEXT,message);
    dc_ui_finish("Reading large games from disc can take time.","");
    dc_draw_shutdown_keep();
}

void dc_menu_error(const char *message) {
    if(dc_draw_init()!=0)return;
    dc_ui_confirm("Unable to start game",message,"return to games");
    dc_draw_shutdown();
}

int dc_menu_validate(const char *path,char *error,size_t size) {
    unsigned char h[4];
    FILE *f=fopen(path,"rb");
    long length;
    const char *why=NULL;
    if(!f)why="The game could not be opened. Check the disc or SD card and scan again.";
    else {
        if(fseek(f,0,SEEK_END) || (length=ftell(f))<4096 || length>64*1024*1024 || (length&3))
            why="The game must be a complete, word-aligned N64 image between 4 KiB and 64 MiB.";
        else if(fseek(f,0,SEEK_SET) || fread(h,1,4,f)!=4 ||
            !((h[0]==0x80 && h[1]==0x37 && h[2]==0x12 && h[3]==0x40) ||
              (h[0]==0x37 && h[1]==0x80 && h[2]==0x40 && h[3]==0x12) ||
              (h[0]==0x40 && h[1]==0x12 && h[2]==0x37 && h[3]==0x80)))
            why="This file is not a recognized N64 game image. Supported formats are .z64, .n64 and .v64.";
        fclose(f);
    }
    if(error && size)snprintf(error,size,"%s",why?why:"");
    return !why;
}

static void run_screen(void (*enter)(void),int (*step)(const BUTTONS *),void (*draw)(void)) {
    BUTTONS keys;
    int frames=0;
    enter();
    for(;;) {
        draw();
        dc_ui_poll(&keys,NULL);
        if(!step(&keys))break;
#ifdef DC_HOST_STUB
        if(++frames>DC_MENU_HOST_FRAME_CAP)break;
#else
        (void)frames;
#endif
    }
    dc_settings_save(NULL); /* The visible status reports persistence failure. */
}

static int load_folder(dc_menu_list *list,const char *path) {
    fileBrowser_file folder;
    memset(&folder,0,sizeof(folder));
    snprintf(folder.name,sizeof(folder.name),"%s",path);
    /* Read only: never create a missing ROM source while searching. */
    return dc_menu_list_load(list,&folder);
}

int dc_menu_run(fileBrowser_file *dir,dc_menu_entry *out) {
    dc_menu_list list={0};
    dc_menu_state st;
    char roots[5][FILE_BROWSER_MAX_PATH_LEN],history[16][FILE_BROWSER_MAX_PATH_LEN];
    int cursors[16],tops[16],depth=0,source=0,nroots=0,i,result=0,blocked=1;
    int dirty=1,frames=0,connected=-1;
    unsigned extra_prev=~0u;
    if(!dir || !out)return FILE_BROWSER_ERROR;
#ifndef DC_HOST_STUB
    for(i=0;i<(int)DC_ROM_DIR_COUNT;i++)snprintf(roots[nroots++],sizeof(roots[0]),"%s",dc_rom_dirs[i]);
#else
    snprintf(roots[nroots++],sizeof(roots[0]),"%s",dir->name);
#endif
    for(i=0;i<nroots;i++)if(!strcmp(roots[i],dir->name))break;
    if(i==nroots && nroots<5)snprintf(roots[nroots++],sizeof(roots[0]),"%s",dir->name);
    source=i;
    load_folder(&list,roots[source]);
    if(!list.count)for(i=0;i<nroots;i++)if(i!=source) {
        dc_menu_list probe={0};load_folder(&probe,roots[i]);
        if(probe.count){dc_menu_list_free(&list);list=probe;source=i;break;}
        dc_menu_list_free(&probe);
    }
    if(dc_draw_init()!=0){dc_menu_list_free(&list);return FILE_BROWSER_ERROR;}
    dc_menu_state_init(&st,&list,DC_UI_ROWS);
    for(;;) {
        BUTTONS keys;
        unsigned raw=0,extra,press;
        int online,old_cursor=st.cursor,old_top=st.top;
        dc_menu_action action;
        online=dc_ui_poll(&keys,&raw);
        if(online!=connected){connected=online;dirty=1;}
        if(dirty) {
            dc_menu_draw(&st,"Games");
            if(!connected){dc_ui_text(16,DC_UI_WARN,"Reconnect a controller to continue.");dc_draw_end();}
            dirty=0;
        } else dc_draw_wait();
        extra=raw&(DC_CONT_X|DC_CONT_Y);
        if(blocked) {
            if(!decode_edges(&keys) && !wanted_dir(&st,&keys) && !extra) {
                blocked=0;st.prev=0;st.repeat_dir=0;st.repeat_in=0;extra_prev=0;
            }
            goto next_frame;
        }
        press=extra&~extra_prev;extra_prev=extra;
        if(press&DC_CONT_Y){run_screen(dc_settings_enter,dc_settings_step,dc_settings_draw);blocked=1;dirty=1;goto next_frame;}
        if(press&DC_CONT_X){run_screen(dc_controls_enter,dc_controls_step,dc_controls_draw);blocked=1;dirty=1;goto next_frame;}
        action=dc_menu_step(&st,&keys);
        if(st.cursor!=old_cursor || st.top!=old_top)dirty=1;
        if(action==DC_MENU_PICK) {
            dc_menu_entry entry=list.items[st.cursor];
            if(entry.directory) {
                dc_menu_list next={0};
                if(depth==16){dc_ui_confirm("Folder limit","Go back before opening another nested folder.","return");blocked=dirty=1;goto next_frame;}
                if(load_folder(&next,entry.path)<0) {
                    dc_menu_list_free(&next);
                    dc_ui_confirm("Folder unavailable","The folder could not be read. Check the storage device and scan again.","return");
                } else {
                    snprintf(history[depth],sizeof(history[0]),"%s",list.dir);
                    cursors[depth]=st.cursor;tops[depth++]=st.top;
                    dc_menu_list_free(&list);list=next;dc_menu_state_init(&st,&list,DC_UI_ROWS);
                }
                blocked=dirty=1;
            } else {
                char error[192],message[192];
                if(!dc_menu_validate(entry.path,error,sizeof(error))) {
                    dc_ui_confirm("Unable to start game",error,"return to games");blocked=dirty=1;
                } else {
                    *out=entry;
                    snprintf(message,sizeof(message),"Loading %s",entry.label);
                    dc_menu_message("Starting game",message);result=1;break;
                }
            }
        } else if(action==DC_MENU_CANCEL) {
            if(depth) {
                dc_menu_list next={0};--depth;load_folder(&next,history[depth]);
                dc_menu_list_free(&list);list=next;dc_menu_state_init(&st,&list,DC_UI_ROWS);
                st.cursor=cursors[depth];st.top=tops[depth];clamp_view(&st);
            } else if(dc_ui_confirm("Exit Not64?","Return to the system menu? Your saved settings and game saves will be kept.","exit"))break;
            blocked=dirty=1;
        } else if(action==DC_MENU_REFRESH || action==DC_MENU_SOURCE_PREV || action==DC_MENU_SOURCE_NEXT) {
            char path[FILE_BROWSER_MAX_PATH_LEN];
            if(action!=DC_MENU_REFRESH) {
                source=(source+(action==DC_MENU_SOURCE_NEXT?1:nroots-1))%nroots;depth=0;
                snprintf(path,sizeof(path),"%s",roots[source]);
            } else snprintf(path,sizeof(path),"%s",list.dir);
            dc_menu_list_free(&list);load_folder(&list,path);dc_menu_state_init(&st,&list,DC_UI_ROWS);
            blocked=dirty=1;
        }
next_frame:
#ifdef DC_HOST_STUB
        if(++frames>DC_MENU_HOST_FRAME_CAP){printf("menu: host input timeout\n");break;}
#else
        (void)frames;
#endif
    }
    dc_draw_shutdown();dc_menu_list_free(&list);
    return result;
}
