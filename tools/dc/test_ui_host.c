/* Standalone frontend contract tests. Core runtime services are stubbed;
 * menus, Maple mapping, config parsing, drawing and file I/O are real. */
#include "../../platform/dc_menu/dc_ui.h"
#include "../../platform/dc_menu/dc_menu.h"
#include "../../platform/dc_settings.h"
#include "../../platform/dc_overlay.h"
#include "../../platform/dc_video.h"
#include "../../platform/dc_storage.h"
#include "../../platform/dc_gfx.h"
#include "../../main/timers.h"
#include "../../main/rom.h"
#include "../../main/wii64config.h"
#include "../../main/savestates.h"
#include "../../fileBrowser/fileBrowser-kos.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
timers Timers;
rom_settings ROM_SETTINGS={"UI TEST",0};
char audioEnabled=1,printToSD=1;
int stop,savestates_job;
static unsigned frameskip,slot;
static int state_present,state_ok,native_result;
static const char *state_error="";
char *get_savespath(void){return "./saves";}
void apply_pak_modes(void){}
void audio_dc_set_enabled(int on){audioEnabled=!!on;}
void dc_debug_set_file(int on){printToSD=!!on;}
void dc_gfx_set_frameskip(unsigned n){frameskip=n;}
unsigned dc_gfx_get_frameskip(void){return frameskip;}
void dc_log(int level,const char *fmt,...){(void)level;(void)fmt;}
void savestates_select_slot(unsigned n){slot=n%10;}
unsigned savestates_get_slot(void){return slot;}
int savestates_exists(int mode){(void)mode;return state_present;}
void savestates_save(void){state_ok=1;state_present=1;}
void savestates_load(void){state_ok=state_present;}
int savestates_ok(void){return state_ok;}
const char *savestates_error(void){return state_error;}
int dc_nativesave_save(void){return native_result;}
static int fails;
#define CHECK(test) do { if(!(test)){printf("FAIL line %d: %s\n",__LINE__,#test);fails++;} }while(0)
static void tap(int kind){BUTTONS k;memset(&k,0,sizeof(k));dc_overlay_step(&k);if(kind==0)k.D_DPAD=1;if(kind==1)k.A_BUTTON=1;if(kind==2)k.B_BUTTON=1;dc_overlay_step(&k);}
static void input_tests(void){
    dc_ui_input input;BUTTONS k;int i,count=0;
    memset(&k,0,sizeof(k));dc_ui_input_init(&input,1);k.A_BUTTON=1;
    CHECK(!dc_ui_input_step(&input,&k));k.B_BUTTON=1;CHECK(!dc_ui_input_step(&input,&k));k.A_BUTTON=0;CHECK(!dc_ui_input_step(&input,&k));
    memset(&k,0,sizeof(k));CHECK(!dc_ui_input_step(&input,&k));k.A_BUTTON=1;CHECK(dc_ui_input_step(&input,&k)==DC_UI_A);CHECK(!dc_ui_input_step(&input,&k));
    memset(&k,0,sizeof(k));dc_ui_input_step(&input,&k);k.D_DPAD=1;CHECK(dc_ui_input_step(&input,&k)==DC_UI_DOWN);
    for(i=0;i<28;i++)if(dc_ui_input_step(&input,&k)&DC_UI_DOWN)count++;
    CHECK(count==3);k.U_DPAD=1;CHECK(!dc_ui_input_step(&input,&k));
    memset(&k,0,sizeof(k));dc_ui_input_step(&input,&k);k.X_AXIS=39;CHECK(!dc_ui_input_step(&input,&k));k.X_AXIS=40;CHECK(dc_ui_input_step(&input,&k)==DC_UI_RIGHT);k.X_AXIS=25;CHECK(!dc_ui_input_step(&input,&k));k.X_AXIS=19;CHECK(!dc_ui_input_step(&input,&k));k.X_AXIS=40;CHECK(dc_ui_input_step(&input,&k)==DC_UI_RIGHT);
    controller_DC_host_set(0,DC_CONT_DPAD_UP,0,-80);controller_DC_host_set_triggers(0,60,60);CHECK(dc_ui_poll(&k,NULL));CHECK(k.U_DPAD && k.Y_AXIS==80 && k.Z_TRIG && k.R_TRIG);
    controller_DC_host_set(0,0,0,0);controller_DC_host_set_triggers(0,0,0);
}
static void overlay_tests(void){
    BUTTONS k;
    dc_overlay_enter();state_present=1;stop=0;
    tap(0);tap(0);tap(0);tap(1);
    CHECK(!stop); /* overwrite is pending */
    state_ok=0;tap(2);CHECK(!state_ok);tap(1);tap(1);CHECK(state_ok);
    dc_overlay_enter();tap(0);tap(0);native_result=-1;tap(1);tap(1);CHECK(!stop);
    native_result=0;tap(1);tap(1);CHECK(stop && dc_overlay_take_action()==DC_OVERLAY_MENU);stop=0;
    dc_overlay_enter();memset(&k,0,sizeof(k));dc_overlay_step(&k);k.R_TRIG=1;dc_overlay_step(&k);CHECK(savestates_get_slot()==1);
    dc_overlay_enter();tap(0);tap(0);tap(0);tap(0);tap(0);tap(1);dc_overlay_draw();CHECK(strstr(dc_draw_host_row(1),"Settings"));tap(2);
}
static void file_tests(void){
    fileBrowser_file dir={0};dc_menu_list list={0};char error[192];FILE *f;
    fileBrowser_kos_bind();strcpy(dir.name,"ui-fixtures");CHECK(dc_menu_list_load(&list,&dir)==5);CHECK(list.items[0].directory);CHECK(!strcmp(list.items[1].label,"a.v64"));dc_menu_list_free(&list);
    strcpy(dir.name,"ui-fixtures/empty");CHECK(dc_menu_list_load(&list,&dir)==0 && !list.error);dc_menu_list_free(&list);
    strcpy(dir.name,"ui-fixtures/missing");CHECK(dc_menu_list_load(&list,&dir)<0 && list.error);dc_menu_list_free(&list);
    CHECK(dc_menu_validate("ui-fixtures/b.z64",error,sizeof(error)));CHECK(dc_menu_validate("ui-fixtures/a.v64",error,sizeof(error)));CHECK(dc_menu_validate("ui-fixtures/c.n64",error,sizeof(error)));
    CHECK(!dc_menu_validate("ui-fixtures/bad.z64",error,sizeof(error)) && *error);
    {
        dc_menu_entry choice;
        const unsigned inputs[]={DC_CONT_A,DC_CONT_A,0,DC_CONT_A,0,DC_CONT_B,0,DC_CONT_DPAD_DOWN,0,DC_CONT_A};
        strcpy(dir.name,"ui-fixtures");
        controller_DC_host_set(0,0,0,0);
        controller_DC_host_queue(inputs,sizeof(inputs)/sizeof(inputs[0]));
        CHECK(dc_menu_run(&dir,&choice)==1);
        CHECK(!strcmp(choice.label,"a.v64"));
        controller_DC_host_set(0,0,0,0);
        dc_draw_init();
    }
    f=fopen("ui-fixtures/original","wb");fputs("previous",f);fclose(f);
    CHECK(dc_storage_commit("ui-fixtures/missing-temp","ui-fixtures/original")<0);
    f=fopen("ui-fixtures/original","rb");CHECK(f!=NULL);if(f){char data[9]={0};fread(data,1,8,f);fclose(f);CHECK(!strcmp(data,"previous"));}
    f=fopen("ui-fixtures/new","wb");fputs("next",f);fclose(f);CHECK(!dc_storage_commit("ui-fixtures/new","ui-fixtures/original"));
}
static void layout_tests(void){
    char longtext[256],out[128];int c,t,l,r,b,top;
    memset(longtext,'W',255);longtext[255]=0;
    dc_ui_ellipsize(out,sizeof(out),longtext,49);CHECK(strlen(out)==49 && !strcmp(out+46,"..."));
    dc_ui_begin(longtext,longtext);dc_ui_row(4,longtext,longtext,1);dc_ui_finish(longtext,longtext);
    for(c=0;c<dc_draw_host_rows();c++){const char *row=dc_draw_host_row(c);CHECK(strlen(row)==53);CHECK(row[0]==' ' && row[1]==' ' && row[51]==' ' && row[52]==' ');}
    c=19;top=10;dc_ui_scroll(&c,&top,20,10,DC_UI_DOWN);CHECK(c==0 && top==0);dc_ui_scroll(&c,&top,20,10,DC_UI_UP);CHECK(c==19 && top==10);
    for(t=0;t<3;t++){dc_video_viewport(320,240,t,&l,&top,&r,&b);CHECK(l>=0 && top>=0 && r<=640 && b<=480);}
    dc_video_viewport(320,240,SCREENMODE_16x9_PILLARBOX,&l,&top,&r,&b);CHECK(l==80 && r==560 && top==0 && b==480);
}
int main(void){
    dc_draw_init();dc_settings_defaults();input_tests();file_tests();layout_tests();
    fails+=dc_settings_selftest();dc_draw_init();overlay_tests();fails+=dc_overlay_selftest();
    printf("Frontend contracts: %s (%d failures)\n",fails?"FAIL":"PASS",fails);return !!fails;
}
