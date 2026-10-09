/* Pause UI uses raw controller input so remapping never locks out navigation. */
#include "dc_overlay.h"
#include "dc_menu/dc_draw.h"
#include "dc_menu/dc_menu.h"
#include "dc_menu/dc_ui.h"
#include "dc_settings.h"
#include "dc_nativesaves.h"
#include "dc_debug.h"
#include "../main/savestates.h"
#include "../main/rom.h"
#include <stdio.h>
#include <string.h>
extern int stop;
enum { OV_CONTINUE, OV_RESET, OV_MENU, OV_SAVE, OV_LOAD, OV_SETTINGS, OV_CONTROLS, OV_COUNT };
static const char *const labels[] = {"Continue game","Restart game","Return to games","Save state","Load state","Settings","Controls"};
static int cursor, top, action, pending, sub_screen, connected;
static dc_ui_input input;
static char status[128];
static void refresh_status(void) {
    snprintf(status,sizeof(status),"Slot %u: %s",savestates_get_slot(),savestates_exists(SAVESTATE)?"saved state available":"empty");
}
void dc_overlay_enter(void) {
    cursor=top=0; pending=-1; sub_screen=0; connected=1;
    action=DC_OVERLAY_NONE;
    dc_ui_input_init(&input,1); refresh_status();
}
static void state_operation(int operation) {
    int save=operation==OV_SAVE;
    if (!save && !savestates_exists(LOADSTATE)) {
        snprintf(status,sizeof(status),"Slot %u is empty. Choose another slot.",savestates_get_slot());
        return;
    }
    dc_ui_begin(save?"Saving state":"Loading state","Please wait for the storage operation to finish.");
    dc_ui_text(7,DC_UI_TEXT,save?"Writing to SD...":"Reading and checking the saved state...");
    dc_ui_finish("Keep the SD device connected.","");
    savestates_job=save?SAVESTATE:LOADSTATE;
    if(save)savestates_save(); else savestates_load();
    if(savestates_ok())snprintf(status,sizeof(status),"Slot %u %s.",savestates_get_slot(),save?"saved":"restored");
    else snprintf(status,sizeof(status),"%s failed: %s",save?"Save":"Load",savestates_error());
    dc_log(DC_LOG_INFO,"%s",status);
    dc_ui_input_init(&input,1);
}
int dc_overlay_step(const BUTTONS *keys) {
    unsigned pressed;
    if(sub_screen) {
        int running=sub_screen==1?dc_settings_step(keys):dc_controls_step(keys);
        if(!running) { dc_settings_save(NULL); sub_screen=0; dc_ui_input_init(&input,1); snprintf(status,sizeof(status),"%s",dc_settings_status()); }
        return 1;
    }
    pressed=dc_ui_input_step(&input,keys);
    if(pending>=0) {
        if(pressed&DC_UI_B) { pending=-1; dc_ui_input_init(&input,1); }
        else if(pressed&DC_UI_A) {
            int selection=pending;
            pending=-1;
            if(selection==OV_SAVE)state_operation(selection);
            else {
                dc_ui_begin("Saving game progress","Please keep the SD device connected.");
                dc_ui_finish("Writing cartridge and Controller Pak saves...","");
                if (dc_nativesave_save()) {
                    snprintf(status,sizeof(status),"SD save failed. Check SD, or turn auto-save off to leave.");
                    dc_ui_input_init(&input,1);
                } else {
                    action=selection==OV_RESET?DC_OVERLAY_RESET:DC_OVERLAY_MENU; stop=1; return 0;
                }
            }
        }
        return 1;
    }
    if(pressed&DC_UI_B) { action=DC_OVERLAY_CONTINUE; return 0; }
    dc_ui_scroll(&cursor,&top,OV_COUNT,DC_UI_ROWS,pressed&~(DC_UI_PAGE_UP|DC_UI_PAGE_DOWN));
    if(pressed&(DC_UI_LEFT|DC_UI_PAGE_UP))savestates_select_slot((savestates_get_slot()+9)%10);
    if(pressed&(DC_UI_RIGHT|DC_UI_PAGE_DOWN))savestates_select_slot((savestates_get_slot()+1)%10);
    if(pressed&(DC_UI_LEFT|DC_UI_RIGHT|DC_UI_PAGE_UP|DC_UI_PAGE_DOWN))refresh_status();
    if(pressed&(DC_UI_A|DC_UI_START)) {
        switch(cursor) {
        case OV_CONTINUE: action=DC_OVERLAY_CONTINUE; return 0;
        case OV_RESET: case OV_MENU: pending=cursor; dc_ui_input_init(&input,1); break;
        case OV_SAVE:
            if(savestates_exists(SAVESTATE)) { pending=cursor; dc_ui_input_init(&input,1); }
            else state_operation(cursor);
            break;
        case OV_LOAD: state_operation(cursor); break;
        case OV_SETTINGS: sub_screen=1; dc_settings_enter(); break;
        case OV_CONTROLS: sub_screen=2; dc_controls_enter(); break;
        }
    }
    return 1;
}
void dc_overlay_draw(void) {
    int i;
    char subtitle[96];
    if(sub_screen) { if(sub_screen==1)dc_settings_draw(); else dc_controls_draw(); return; }
    if(pending>=0) {
        dc_ui_begin(pending==OV_SAVE?"Overwrite saved state?":pending==OV_RESET?"Restart game?":"Return to games?","A confirms; B keeps the game paused.");
        dc_ui_paragraph(6,3,DC_UI_TEXT,pending==OV_SAVE?"The state in this slot will be replaced. Use B to cancel and choose an empty slot.":pending==OV_RESET?"Restart from the beginning? Unsaved progress will be lost.":"Leave the current game? Cartridge and Memory Pak saves are written to SD when auto-save is enabled.");
        dc_ui_finish(status,"A confirm   B cancel"); return;
    }
    snprintf(subtitle,sizeof(subtitle),"%s   /   Slot %u",ROM_SETTINGS.goodname,savestates_get_slot());
    dc_ui_begin("Paused",subtitle);
    for(i=0;i<OV_COUNT;i++)dc_ui_row(DC_UI_FIRST_ROW+i,labels[i],i==OV_SAVE||i==OV_LOAD?"SD":"",cursor==i);
    dc_ui_paragraph(14,2,DC_UI_MUTED,"State slots keep a snapshot of this game. Cartridge and Controller Pak saves are separate.");
    dc_ui_finish(connected?status:"Reconnect a controller to continue.","A select   Left/Right or triggers: slot   B continue");
}
int dc_overlay_run(void) {
    BUTTONS keys;
    int frames=0;
    dc_overlay_enter();
    if(dc_draw_init()) { action=DC_OVERLAY_CONTINUE; return action; }
    for(;;) {
        dc_overlay_draw();
        connected=dc_ui_poll(&keys,NULL);
        if(!dc_overlay_step(&keys))break;
#ifdef DC_HOST_STUB
        if(++frames>DC_MENU_HOST_FRAME_CAP){action=DC_OVERLAY_CONTINUE;break;}
#else
        (void)frames;
#endif
    }
    dc_draw_shutdown(); return action;
}
int dc_overlay_take_action(void) { int result=action; action=DC_OVERLAY_NONE; return result; }
int dc_overlay_has_action(void) { return action!=DC_OVERLAY_NONE; }
#ifdef DC_HOST_STUB
static void tap(unsigned button) {
    BUTTONS k; memset(&k,0,sizeof(k)); dc_overlay_step(&k);
    if(button==DC_UI_DOWN)k.D_DPAD=1;
    if(button==DC_UI_A)k.A_BUTTON=1;
    if(button==DC_UI_B)k.B_BUTTON=1;
    dc_overlay_step(&k);
}
int dc_overlay_selftest(void) {
    int fails=0;
    BUTTONS k;
    dc_overlay_enter(); stop=0; memset(&k,0,sizeof(k)); k.A_BUTTON=k.B_BUTTON=k.START_BUTTON=1;
    if(!dc_overlay_step(&k) || action)fails++;
    dc_draw_init(); dc_overlay_draw();
    if(!strstr(dc_draw_host_row(1),"Paused") || !strstr(dc_draw_host_row(4),"Continue"))fails++;
    tap(DC_UI_DOWN); tap(DC_UI_DOWN); tap(DC_UI_A);
    if(pending!=OV_MENU || stop)fails++;
    tap(DC_UI_B); if(pending!=-1 || stop)fails++;
    tap(DC_UI_A); tap(DC_UI_A);
    if(!stop || dc_overlay_take_action()!=DC_OVERLAY_MENU)fails++;
    stop=0; dc_overlay_enter(); tap(DC_UI_A);
    if(dc_overlay_take_action()!=DC_OVERLAY_CONTINUE)fails++;
    dc_draw_shutdown();
    printf("overlay selftest: %s (%d failure(s))\n",fails?"FAIL":"PASS",fails);
    return fails;
}
#endif
