#include "dc_ui.h"
#include <stdio.h>
#include <string.h>

void dc_ui_input_init(dc_ui_input *input, int wait_for_release) {
    memset(input, 0, sizeof(*input));
    input->release=wait_for_release;
}

unsigned dc_ui_input_step(dc_ui_input *input, const BUTTONS *keys) {
    unsigned now=0, pressed;
    int x,y,i;
    static const unsigned directions[]={DC_UI_UP,DC_UI_DOWN,DC_UI_LEFT,DC_UI_RIGHT};
    if(!input || !keys) return 0;
    x=(signed char)keys->X_AXIS; y=(signed char)keys->Y_AXIS;
    if(keys->A_BUTTON) now|=DC_UI_A;
    if(keys->B_BUTTON) now|=DC_UI_B;
    if(keys->START_BUTTON) now|=DC_UI_START;
    if(keys->U_DPAD || y>=((input->prev&DC_UI_UP)?20:40)) now|=DC_UI_UP;
    if(keys->D_DPAD || y<=-((input->prev&DC_UI_DOWN)?20:40)) now|=DC_UI_DOWN;
    if(keys->L_DPAD || x<=-((input->prev&DC_UI_LEFT)?20:40)) now|=DC_UI_LEFT;
    if(keys->R_DPAD || x>=((input->prev&DC_UI_RIGHT)?20:40)) now|=DC_UI_RIGHT;
    if(keys->Z_TRIG) now|=DC_UI_PAGE_UP;
    if(keys->R_TRIG) now|=DC_UI_PAGE_DOWN;
    if(input->release) {
        if(!now) input->release=0;
        input->prev=now;
        return 0;
    }
    if((now&(DC_UI_UP|DC_UI_DOWN))==(DC_UI_UP|DC_UI_DOWN)) now&=~(DC_UI_UP|DC_UI_DOWN);
    if((now&(DC_UI_LEFT|DC_UI_RIGHT))==(DC_UI_LEFT|DC_UI_RIGHT)) now&=~(DC_UI_LEFT|DC_UI_RIGHT);
    if((now&(DC_UI_PAGE_UP|DC_UI_PAGE_DOWN))==(DC_UI_PAGE_UP|DC_UI_PAGE_DOWN)) now&=~(DC_UI_PAGE_UP|DC_UI_PAGE_DOWN);
    pressed=now&~input->prev;
    for(i=0;i<4;i++) {
        unsigned bit=directions[i];
        if(!(now&bit)) input->held[i]=0;
        else if(pressed&bit) input->held[i]=20;
        else if(input->held[i] && --input->held[i]==0) {
            pressed|=bit; input->held[i]=4;
        }
    }
    input->prev=now;
    return pressed;
}

int dc_ui_poll(BUTTONS *keys, unsigned *raw_out) {
    unsigned raw=0;
    int x=0,y=0,lt=0,rt=0;
    int connected=controller_DC_poll_raw(0,&raw,&x,&y,&lt,&rt);
    memset(keys,0,sizeof(*keys));
    if(raw_out) *raw_out=connected?raw:0;
    if(!connected) return 0;
    keys->A_BUTTON=!!(raw&DC_CONT_A); keys->B_BUTTON=!!(raw&DC_CONT_B);
    keys->START_BUTTON=!!(raw&DC_CONT_START);
    keys->U_DPAD=!!(raw&DC_CONT_DPAD_UP); keys->D_DPAD=!!(raw&DC_CONT_DPAD_DOWN);
    keys->L_DPAD=!!(raw&DC_CONT_DPAD_LEFT); keys->R_DPAD=!!(raw&DC_CONT_DPAD_RIGHT);
    keys->Z_TRIG=lt>=48; keys->R_TRIG=rt>=48;
    if(x>80)x=80;
    if(x< -80)x=-80;
    y=-y;
    if(y>80)y=80;
    if(y< -80)y=-80;
    keys->X_AXIS=(signed char)x; keys->Y_AXIS=(signed char)y;
    return 1;
}

void dc_ui_scroll(int *cursor,int *top,int count,int rows,unsigned input) {
    if(count<=0){*cursor=*top=0;return;}
    if(input&DC_UI_UP) --*cursor;
    if(input&DC_UI_DOWN) ++*cursor;
    if(*cursor<0)*cursor=count-1;
    if(*cursor>=count)*cursor=0;
    if(input&DC_UI_PAGE_UP){*cursor-=rows;if(*cursor<0)*cursor=0;}
    if(input&DC_UI_PAGE_DOWN){*cursor+=rows;if(*cursor>=count)*cursor=count-1;}
    if(*cursor<*top)*top=*cursor;
    if(*cursor>=*top+rows)*top=*cursor-rows+1;
    if(*top>count-rows)*top=count-rows;
    if(*top<0)*top=0;
}

int dc_ui_columns(void){return dc_draw_width()/dc_draw_char_w()-4;}

void dc_ui_ellipsize(char *out,size_t size,const char *text,int columns) {
    size_t len,n,i;
    if(!size)return;
    if(!text)text="";
    if(columns<0)columns=0;
    n=(size_t)columns; if(n>=size)n=size-1;
    len=strlen(text); if(len<n)n=len;
    for(i=0;i<n;i++)out[i]=(unsigned char)text[i]>=32 && (unsigned char)text[i]<=126?text[i]:'?';
    if(len>n && n>=3)memcpy(out+n-3,"...",3);
    out[n]=0;
}

void dc_ui_text(int row,uint16_t color,const char *text) {
    char line[128];
    if(row<1 || row>=dc_draw_height()/dc_draw_char_h()-1)return;
    dc_ui_ellipsize(line,sizeof(line),text,dc_ui_columns());
    dc_draw_text(2*dc_draw_char_w(),row*dc_draw_char_h(),color,line);
}

void dc_ui_paragraph(int row,int lines,uint16_t color,const char *text) {
    char line[128];
    int cols=dc_ui_columns(),n;
    if(!text)return;
    while(*text && lines>0) {
        size_t len=strlen(text);
        while(*text==' ')text++;
        n=(int)strlen(text); if(n>cols)n=cols;
        if(lines>1 && n==cols && text[n] && text[n]!=' ') {
            int cut=n;while(cut>0 && text[cut]!=' ')cut--;
            if(cut>0)n=cut;
        }
        if(lines==1 && len>(size_t)n)dc_ui_ellipsize(line,sizeof(line),text,cols);
        else {memcpy(line,text,(size_t)n);line[n]=0;}
        dc_ui_text(row++,color,line);
        text+=n; lines--;
    }
}

void dc_ui_begin(const char *title,const char *subtitle) {
    char heading[128];
    dc_draw_begin(DC_UI_BG);
    dc_draw_fill_rect(24,24,dc_draw_width()-48,48,DC_UI_PANEL);
    dc_draw_fill_rect(24,24,4,48,DC_UI_ACCENT);
    snprintf(heading,sizeof(heading),"NOT64 DC  /  %s",title?title:"Games");
    dc_ui_text(1,DC_UI_TEXT,heading);
    dc_ui_text(2,DC_UI_MUTED,subtitle);
    dc_draw_fill_rect(24,3*dc_draw_char_h()+12,dc_draw_width()-48,1,DC_UI_FOCUS);
}

void dc_ui_row(int row,const char *label,const char *value,int selected) {
    char line[128],name[128],tail[48];
    int cols=dc_ui_columns()-2,vlen;
    dc_ui_ellipsize(tail,sizeof(tail),value,cols/2);
    vlen=(int)strlen(tail);
    dc_ui_ellipsize(name,sizeof(name),label,cols-(vlen?vlen+2:0));
    snprintf(line,sizeof(line),"%c %-*s%s",selected?'>':' ',cols-vlen,name,tail);
    if(selected) {
        dc_draw_fill_rect(24,row*dc_draw_char_h(),dc_draw_width()-48,dc_draw_char_h(),DC_UI_FOCUS);
        dc_draw_fill_rect(24,row*dc_draw_char_h(),4,dc_draw_char_h(),DC_UI_ACCENT);
    }
    dc_ui_text(row,selected?DC_UI_TEXT:DC_UI_MUTED,line);
}

void dc_ui_finish(const char *status,const char *hints) {
    dc_ui_text(16,DC_UI_WARN,status);
    dc_draw_fill_rect(24,17*dc_draw_char_h()-4,dc_draw_width()-48,1,DC_UI_FOCUS);
    dc_ui_paragraph(17,2,DC_UI_MUTED,hints);
    dc_draw_end();
}

int dc_ui_confirm(const char *title,const char *message,const char *verb) {
    dc_ui_input input;
    unsigned pressed;
    BUTTONS keys;
    int frames=0,connected;
    char hint[96];
    dc_ui_input_init(&input,1);
    snprintf(hint,sizeof(hint),"A %s   B cancel",verb?verb:"continue");
    for(;;) {
        connected=dc_ui_poll(&keys,NULL);
        dc_ui_begin(title,"Please confirm");
        dc_ui_paragraph(5,6,DC_UI_TEXT,message);
        dc_ui_finish(connected?"":"Reconnect a controller to continue.",hint);
        pressed=dc_ui_input_step(&input,&keys);
        if(pressed&DC_UI_B)return 0;
        if(pressed&DC_UI_A)return 1;
#ifdef DC_HOST_STUB
        if(++frames>3600)return 0;
#else
        (void)frames;
#endif
    }
}
