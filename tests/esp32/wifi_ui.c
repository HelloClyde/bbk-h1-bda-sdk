/* SPDX-License-Identifier: Apache-2.0
 * Actual example UI/editor/font and SDK RGB565 conversion. Native event/time/
 * framebuffer boundaries are modeled; no device access or credentials. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "h1_sdk.h"
#include "h1_game.h"
#include "h1_touch.h"
static h1_u32 framebuffer[480*272];
static h1_u32 fake_time(void){return 100;}
static int fake_open(void){return 1;}
static void fake_close(void){}
static void fake_event(int *a,int *b){*a=*b=-1;}
static int fake_pen(h1_u16 *a,h1_u16 *b){(void)a;(void)b;return 0;}
static int fake_fb(h1_game_framebuffer *fb)
{fb->pixels=framebuffer;fb->width=480;fb->height=272;fb->stride_bytes=1920;fb->bits_per_pixel=32;return 1;}
static int fake_esp_open(h1_esp32 *e,void (*idle)(void *),void *ctx)
{(void)e;(void)idle;(void)ctx;return H1_ESP32_FIRMWARE;}
#define h1_timer_read_ms fake_time
#define h1_game_open fake_open
#define h1_game_close fake_close
#define h1_event_fetch fake_event
#define h1_touch_position fake_pen
#define h1_game_framebuffer_get fake_fb
#define h1_esp32_open fake_esp_open
#include "../../examples/esp32_wifi/main.c"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"UI check failed at %d: %s\n",__LINE__,#x);return 1;}}while(0)
static void snapshot(const char *directory,const char *name)
{
    char path[1024];wifi_snprintf(path,sizeof path,"%s/%s.ppm",directory,name);
    FILE *out=fopen(path,"wb");if(!out)exit(2);fprintf(out,"P6\n480 272\n255\n");
    for(unsigned i=0;i<480*272;i++){unsigned c=framebuffer[i];unsigned char rgb[3]={(unsigned char)(c>>16),(unsigned char)(c>>8),(unsigned char)c};fwrite(rgb,1,3,out);}
    fclose(out);
}
int main(int argc,char **argv)
{
    for(unsigned i=0;i<95;i++)CHECK(bili_font_cp[i]==32+i&&bili_font_width[i]==8);
    CHECK(argc==2);capable=1;esp.state=H1_ESP32_READY;count=2;
    wifi_settings_network.connected=1;wifi_memcpy(wifi_settings_network.current,"H1 Lab",7);
    h1wifi_ap a={"H1 Lab",-46,H1_ESP32_AUTH_WPA2_PSK,6},b={"Guest Wi-Fi",-70,H1_ESP32_AUTH_OPEN,1};
    wifi_settings_aps[0]=a;wifi_settings_aps[1]=b;
    view=VIEW_LIST;paint();snapshot(argv[1],"list");CHECK(presents==1);
    touch_action(20);CHECK(view==VIEW_EDIT&&!edit_ssid);
    key_action(H1_KEY_Q);touch_action(201);CHECK(editor.length==2&&editor.value[0]=='q'&&editor.value[1]==' ');
    paint();snapshot(argv[1],"password");
    key_action(H1_KEY_ESCAPE);CHECK(view==VIEW_LIST);
    for(unsigned i=0;i<64;i++)CHECK(!editor.value[i]);
    key_action(H1_KEY_H);CHECK(view==VIEW_EDIT&&edit_ssid);
    touch_action(203);CHECK(auth==H1_ESP32_AUTH_OPEN);
    key_action(H1_KEY_ESCAPE);key_action(H1_KEY_S);CHECK(view==VIEW_STATUS);
    paint();snapshot(argv[1],"status");
    key_action(H1_KEY_F);CHECK(view==VIEW_FORGET&&next_op!=H1NET_WIFI_FORGET);
    key_action(H1_KEY_ESCAPE);CHECK(view==VIEW_LIST&&next_op!=H1NET_WIFI_FORGET);
    key_action(H1_KEY_S);key_action(H1_KEY_F);key_action(H1_KEY_ENTER);CHECK(next_op==H1NET_WIFI_FORGET);
    puts("PASS: Wi-Fi UI touch/key editing, password wipe, hidden SSID, forget confirmation; 3 modeled frames");return 0;
}
