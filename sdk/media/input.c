/* Adapted from experiments/minigpt/native_input.c; H1/Y100 V1.41 native editor. */
/* Retain the existing GB2312 editor mapping independently of the pixel font. */
#include "h1_sdk.h"
#include "h1_draw.h"
#include "h1_text_input.h"
#include "fonts/ime_table.h"
static const char *input_caption;
typedef unsigned HWND;
typedef int (*PROC)(HWND,unsigned,unsigned,unsigned);
typedef struct {unsigned style,exstyle;const char *caption;unsigned menu,cursor,hosting;PROC proc;int left,top,right,bottom;unsigned background,data;} CREATE;
static void *entry(unsigned off){return h1_runtime_entry(h1_runtime_table(H1_RUNTIME_GUI_TABLE_SLOT),off);}
static int send(HWND w,unsigned m,unsigned p,unsigned l){return ((PROC)entry(0x40))(w,m,p,l);}
static HWND control(const char *cls,const char *caption,unsigned style,int id,int x,int y,int width,int height,HWND parent)
{typedef HWND (*FN)(const char*,const char*,unsigned,unsigned,int,int,int,int,int,HWND,unsigned);return ((FN)entry(0x1a4))(cls,caption,style,0,id,x,y,width,height,parent,0);}
volatile unsigned h1_ime_native_window,h1_ime_native_edit,h1_ime_native_events,h1_ime_native_trace[128];
static char gbk[768];
static HWND controls[7];
static int accepted;
static unsigned decode(unsigned code)
{for(unsigned i=0;i<H1_IME_COUNT;i++)if(h1_ime_gbk[i]==code)return h1_ime_cp[i];return 0;}
static int to_utf8(char *out,unsigned cap)
{unsigned n=0;for(unsigned i=0;gbk[i];i++){unsigned cp=(unsigned char)gbk[i];if(cp>=128){if(!gbk[i+1])return 0;cp=decode((cp<<8)|(unsigned char)gbk[++i]);if(!cp)return 0;}unsigned len=cp<128?1:cp<2048?2:3;if(n+len>=cap)return 0;if(len==1)out[n++]=(char)cp;else{if(len==3)out[n++]=(char)(0xe0|(cp>>12));out[n++]=(char)((len==3?0x80:0xc0)|((cp>>6)&(len==3?63:31)));out[n++]=(char)(0x80|(cp&63));}}out[n]=0;return 1;}
static int from_utf8(const char *s)
{unsigned n=0;while(*s && n<sizeof gbk-3){int cp=h1_utf8_next(&s);if(cp<0)break;if(cp<128)gbk[n++]=(char)cp;else{unsigned i;for(i=0;i<H1_IME_COUNT;i++)if(h1_ime_cp[i]==cp)break;if(i==H1_IME_COUNT)return 0;else{gbk[n++]=(char)(h1_ime_gbk[i]>>8);gbk[n++]=(char)h1_ime_gbk[i];}}}gbk[n]=0;return !*s;}
static void finish(HWND w,int ok)
{
    accepted=ok;if(ok)send(h1_ime_native_edit,0x133,sizeof gbk-1,(unsigned)gbk);
    ((void (*)(void))entry(0x558))();
    /* Notepad destroys its controls before DestroyMainWindow/ThreadCleanup. */
    for(int i=6;i>=0;i--)if(controls[i] && controls[i]!=(unsigned)-1){((int (*)(HWND))entry(0x1a8))(controls[i]);controls[i]=0;}
    ((int (*)(HWND))entry(0x88))(w);((void (*)(HWND))entry(0x4c))(w);
}
static int proc(HWND w,unsigned msg,unsigned wp,unsigned lp)
{
    unsigned idx=(h1_ime_native_events++&31u)*4;h1_ime_native_trace[idx]=w;h1_ime_native_trace[idx+1]=msg;h1_ime_native_trace[idx+2]=wp;h1_ime_native_trace[idx+3]=lp;
    if(msg==0x60){
        h1_ime_native_window=w;
        controls[0]=control("static",input_caption,0x08000000,99,12,5,310,25,w);
        /* 0x800 is ES_READONLY in this firmware's medit implementation. */
        controls[1]=h1_ime_native_edit=control("medit","",0x08103000,100,12,36,456,54,w);
        controls[2]=control("button","\xc8\xb7\xb6\xa8",0x08010000,101,280,95,88,30,w);
        controls[3]=control("button","\xc8\xa1\xcf\xfb",0x08010000,102,380,95,88,30,w);
        controls[4]=control("button","\xc6\xb4\xd2\xf4",0x08010000,103,12,95,88,30,w);
        controls[5]=control("button","ABC",0x08010000,104,112,95,76,30,w);
        controls[6]=control("button","\xc7\xe5\xbf\xd5",0x08010000,105,200,95,68,30,w);
        send(h1_ime_native_edit,0xf0c5,500,0);
        send(h1_ime_native_edit,0x134,0,(unsigned)gbk);
        return 0;
    }
    if(msg==0x120){
        unsigned id=wp&65535;
        if(id==101){finish(w,1);return 0;}
        if(id==102){finish(w,0);return 0;}
        if(id==103 || id==104 || id==105){
            ((void (*)(void))entry(0x558))();
            if(id==105)send(h1_ime_native_edit,0x134,0,(unsigned)"");
            ((HWND (*)(HWND))entry(0x134))(h1_ime_native_edit);
            if(id!=105)((void (*)(unsigned))entry(0x55c))(id==103?2:1);
            ((void (*)(HWND))entry(0x554))(w);return 0;
        }
    }
    if(msg==0x66){finish(w,0);return 0;}
    return ((PROC)entry(0x8c))(w,msg,wp,lp);
}
int h1_input_text(char *utf8,unsigned capacity,const char *caption_gbk)
{
    input_caption=caption_gbk?caption_gbk:"Input";
    if(!utf8||!capacity||!from_utf8(utf8))return 0;
    accepted=0;h1_ime_native_events=0;
    CREATE c={0x08000000,0,input_caption,0,0,0,proc,0,0,480,272,0,0};
    c.background=((unsigned (*)(int))entry(0x2fc))(15);
    HWND w=((HWND (*)(CREATE*))entry(0x84))(&c);if(!w || w==(unsigned)-1)return 0;
    ((int (*)(HWND,int))entry(0x98))(w,0x100);
    ((HWND (*)(HWND))entry(0x134))(h1_ime_native_edit);
    ((void (*)(unsigned))entry(0x564))(0xffff);
    ((void (*)(unsigned))entry(0x55c))(2);
    ((void (*)(HWND))entry(0x554))(w);
    unsigned message[12];
    while(((int (*)(void*,HWND))entry(0x30))(message,0)){
        ((int (*)(void*))entry(0x50))(message);
        ((int (*)(void*))entry(0x54))(message);
    }
    ((void (*)(HWND))entry(0x17c))(w);h1_ime_native_window=0;h1_ime_native_edit=0;
    return accepted && to_utf8(utf8,capacity);
}
