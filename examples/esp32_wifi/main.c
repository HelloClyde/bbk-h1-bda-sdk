/* SPDX-License-Identifier: Apache-2.0
 * Wi-Fi settings example: original H1 UI, SDK-owned transport/protocol. */
#include "h1_sdk.h"
#include "core.h"
#include "compat.h"
#include "h1_game.h"
#include "h1_touch.h"
#include "drawing.h"
#define NULL ((void *)0)
enum {VIEW_LIST,VIEW_EDIT,VIEW_STATUS,VIEW_FORGET};
static h1_esp32 esp;
static int connected(void){return esp.state!=H1_ESP32_WAIT_USB;}
volatile uint32_t wifi_settings_state[24]={0x31534657u,1};
char wifi_settings_notice[160]="请连接ESP32；无需电脑即可设置Wi-Fi";
h1wifi_status wifi_settings_network;
h1wifi_ap wifi_settings_aps[H1WIFI_AP_MAX];
static h1wifi_ap candidates[H1WIFI_AP_MAX];

static wifi_editor editor;
static char chosen[33];static unsigned auth=3,edit_ssid,reuse_password;
static unsigned view,count,selected,closing,owned,dirty=1,capable,next_op,active_op,scan_pending,auto_scan;
static unsigned candidate_count,page_offset,inputs,presents,pen_target,open_ok;
static uint32_t scan_id,last_status,last_paint,observed_operation,observed_outcome;
static int panic_code,last_result;
static unsigned forget_pending;


static void paint(void);static void poll_input(void);



static int context_open(void)
{owned=h1_game_open();return owned;}
static void context_close(void)
{if(owned){h1_game_close();owned=0;}}
static void notice(const char *s){snprintf(wifi_settings_notice,sizeof wifi_settings_notice,"%s",s);dirty=1;}
static void publish(void)
{
    wifi_settings_state[2]=view;wifi_settings_state[3]=capable;wifi_settings_state[4]=connected();
    wifi_settings_state[5]=count;wifi_settings_state[6]=selected;wifi_settings_state[7]=scan_pending;
    wifi_settings_state[8]=active_op;wifi_settings_state[9]=next_op;wifi_settings_state[10]=editor.length;
    wifi_settings_state[11]=editor.reveal;wifi_settings_state[12]=edit_ssid;wifi_settings_state[13]=wifi_settings_network.outcome;
    wifi_settings_state[14]=wifi_settings_network.error;wifi_settings_state[15]=wifi_settings_network.connected;
    wifi_settings_state[16]=wifi_settings_network.operation;wifi_settings_state[17]=wifi_settings_network.scan_state;
    wifi_settings_state[18]=inputs;wifi_settings_state[19]=presents;wifi_settings_state[20]=(uint32_t)last_result;
    wifi_settings_state[21]=reuse_password;wifi_settings_state[22]=(uint32_t)panic_code;wifi_settings_state[23]=closing;
}
static void queue(unsigned op)
{if(next_op&&next_op!=H1NET_WIFI_STATUS){notice("正在处理操作，请稍候");return;}next_op=op;}
static void begin_edit(int manual)
{
    if(!capable){notice("请先连接ESP32，固件需要0.4.0或更新版");return;}
    if(!manual&&!count)return;
    if(!manual&&!h1wifi_auth_supported(wifi_settings_aps[selected].auth)){notice("该网络使用WEP或企业认证，暂不支持");return;}
    edit_ssid=manual;auth=manual?3:wifi_settings_aps[selected].auth;
    snprintf(chosen,sizeof chosen,"%s",manual?"":wifi_settings_aps[selected].ssid);
    wifi_editor_clear(&editor,manual?32:63);view=VIEW_EDIT;
    reuse_password=!manual&&auth&&wifi_settings_network.saved[0]&&!strcmp(chosen,wifi_settings_network.saved);
    notice(manual?"输入网络名称，Enter继续；开放按钮切换认证":auth?reuse_password?"密码已保存在ESP；直接连接或输入新密码":"输入8～63字节密码，大小写需准确":"开放网络无需密码，按连接即可");
}
static void submit(void)
{
    if(edit_ssid){if(!editor.length){notice("网络名称不能为空");return;}
        memcpy(chosen,editor.value,editor.length+1);edit_ssid=0;reuse_password=0;wifi_editor_clear(&editor,63);
        notice(auth?"输入密码；支持英文、数字、空格和符号":"开放网络无需密码，按连接即可");return;}
    if(!h1wifi_credentials_valid((const uint8_t *)chosen,(unsigned)strlen(chosen),(const uint8_t *)editor.value,editor.length,auth,reuse_password)){
        notice("密码需要8～63字节；开放网络不需要密码");return;}
    queue(H1NET_WIFI_CONNECT);
}
static void key_action(int key)
{
    inputs++;
    if(key==H1_KEY_ESCAPE||key==H1_KEY_BACK){
        if(next_op==H1NET_WIFI_CONNECT)next_op=0;
        if(view==VIEW_LIST){closing=1;return;}
        if(view==VIEW_EDIT){wifi_editor_clear(&editor,63);reuse_password=0;view=VIEW_LIST;notice("已取消输入，未更改ESP配置");return;}
        view=VIEW_LIST;dirty=1;return;
    }
    if(next_op==H1NET_WIFI_CONNECT)return;
    if(view==VIEW_EDIT){
        if(key==H1_KEY_ENTER||key==H1_KEY_CONFIRM){submit();return;}
        if(key==H1_KEY_SHIFT){editor.upper^=1;dirty=1;return;}
        if(key==H1_KEY_FN||key==H1_KEY_SYMBOL){editor.numeric^=1;dirty=1;return;}
        if(key==H1_KEY_DELETE){reuse_password=0;wifi_editor_delete(&editor);dirty=1;return;}
        unsigned c=wifi_editor_key(&editor,(unsigned)key);
        if(c&&(edit_ssid||auth)){reuse_password=0;if(!wifi_editor_insert(&editor,c))notice("已到输入长度上限");dirty=1;}return;
    }
    if(view==VIEW_FORGET){if(key==H1_KEY_ENTER||key==H1_KEY_CONFIRM){queue(H1NET_WIFI_FORGET);view=VIEW_STATUS;}return;}
    if(key==H1_KEY_R){if(capable)queue(H1NET_WIFI_SCAN);else if(connected())notice("正在自动检测ESP32连接…");else notice("等待USB连接，或检查ESP固件版本");return;}
    if(key==H1_KEY_S){view=VIEW_STATUS;dirty=1;return;}
    if(view==VIEW_STATUS){
        if(key==H1_KEY_F&&capable){view=VIEW_FORGET;notice("确认后删除ESP保存的网络；Esc取消");}
        if(key==H1_KEY_C&&wifi_settings_network.outcome==H1WIFI_JOINING)queue(H1NET_WIFI_CANCEL);
        return;
    }
    if(key==H1_KEY_H){begin_edit(1);return;}
    if(key==H1_KEY_UP||key==H1_KEY_PAGE_UP){if(selected)selected--;dirty=1;}
    if(key==H1_KEY_DOWN||key==H1_KEY_PAGE_DOWN){if(selected+1<count)selected++;dirty=1;}
    if(key==H1_KEY_ENTER||key==H1_KEY_CONFIRM)begin_edit(0);
}
static const char *keyboard(unsigned row)
{
    static const char *lower[]={"1234567890-=","qwertyuiop[]","asdfghjkl;'\\","zxcvbnm,./` "};
    static const char *upper[]={"!@#$%^&*()_+","QWERTYUIOP{}","ASDFGHJKL:\"|","ZXCVBNM<>?~ "};
    return editor.upper?upper[row]:lower[row];
}
static unsigned hit(int x,int y)
{
    if(x<0||x>=480||y<0||y>=272)return 0;
    if(y<28&&view==VIEW_LIST&&x>=344)return x<412?2:3;
    if(y<28&&x>385&&view!=VIEW_LIST)return 1;
    if(view==VIEW_EDIT){
        if(y>=108&&y<212)return 100+(unsigned)((y-108)/26)*12+(unsigned)(x/40);
        if(y>=251)return 200+(unsigned)(x/96);return 0;
    }
    if(y>=251)return 10+(unsigned)(x/120);
    if(view==VIEW_LIST&&y>=62&&y<222)return 20+(unsigned)((y-62)/32);
    return 0;
}
static void touch_action(unsigned target)
{
    if(target==1){key_action(H1_KEY_ESCAPE);return;}
    if(view==VIEW_LIST&&(target==2||target==3)){
        unsigned page=selected/5;if(target==2&&page)selected=(page-1)*5;
        if(target==3&&(page+1)*5<count)selected=(page+1)*5;dirty=1;return;
    }
    if(next_op==H1NET_WIFI_CONNECT)return;
    if(view==VIEW_EDIT){
        if(target>=100&&target<148){unsigned pos=target-100,c=(unsigned char)keyboard(pos/12)[pos%12];
            if(edit_ssid||auth){reuse_password=0;if(!wifi_editor_insert(&editor,c))notice("已到输入长度上限");dirty=1;}return;}
        if(target==200)key_action(H1_KEY_SHIFT);
        else if(target==201){if(edit_ssid||auth){reuse_password=0;wifi_editor_insert(&editor,' ');dirty=1;}}
        else if(target==202)key_action(H1_KEY_DELETE);
        else if(target==203){if(edit_ssid)auth=auth?0:3;else editor.reveal^=1;dirty=1;}
        else if(target==204)submit();return;
    }
    if(view==VIEW_LIST){
        if(target>=20&&target<25){unsigned i=selected/5*5+target-20;if(i<count){selected=i;begin_edit(0);}return;}
        if(target==10)key_action(H1_KEY_R);if(target==11)key_action(H1_KEY_H);if(target==12)key_action(H1_KEY_S);if(target==13)key_action(H1_KEY_ESCAPE);
    }else if(view==VIEW_FORGET){if(target==10||target==11)key_action(H1_KEY_ENTER);else key_action(H1_KEY_ESCAPE);}
    else {if(target==10)key_action(H1_KEY_R);if(target==11)key_action(H1_KEY_F);if(target==12)key_action(H1_KEY_C);if(target==13)key_action(H1_KEY_ESCAPE);}
}
static void poll_input(void)
{
    for(unsigned i=0;i<32&&!closing;i++){int code=-1,key=-1;h1_event_fetch(&code,&key);if(code==-1)break;
        if(code==H1_EVENT_KEY_DOWN)key_action(key);
        else if(code==11||code==8){h1_u16 x,y;if(!h1_touch_position(&x,&y)){if(code==8)pen_target=0;continue;}
            if(code==11)pen_target=hit(x,y);else {unsigned target=pen_target;pen_target=0;if(target&&target==hit(x,y))touch_action(target);}}
    }
}
#include "screen.h"
static void idle(void *unused){(void)unused;poll_input();}
static void network_step(void)
{
    unsigned was=capable;
    enum h1_esp32_state state=h1_esp32_poll(&esp);
    capable=(state==H1_ESP32_READY||state==H1_ESP32_WIFI_OFFLINE)&&
            (h1_esp32_features(&esp)&H1NET_CAP_WIFI_CONFIG);
    if(!capable){
        if(was){next_op=active_op=scan_pending=0;wifi_editor_clear(&editor,63);reuse_password=0;
            notice("连接已变化；正在自动检测，不会重发上次设置");}
        return;
    }
    if(!was){auto_scan=1;next_op=H1NET_WIFI_STATUS;notice("ESP已连接，正在读取网络状态…");}
    if(!next_op&&h1_timer_read_ms()-last_status>=500)next_op=H1NET_WIFI_STATUS;
    if(!next_op)return;
    unsigned op=next_op;next_op=0;active_op=op;int r=0;
    if(op==H1NET_WIFI_STATUS){
        r=h1_esp32_wifi_status(&esp,&wifi_settings_network);
        if(!r){last_status=h1_timer_read_ms();dirty=1;
            unsigned outcome=wifi_settings_network.outcome;
            if(wifi_settings_network.operation!=observed_operation||outcome!=observed_outcome){
                observed_operation=wifi_settings_network.operation;observed_outcome=outcome;
                if(outcome==H1WIFI_SAVED)notice("连接成功，配置已保存；其他联网应用可直接使用");
                else if(outcome==H1WIFI_FAILED)notice(wifi_error_message(wifi_settings_network.error));
                else if(outcome==H1WIFI_CANCELLED)notice("已取消连接，原网络配置保留");
            }
            if(forget_pending&&outcome==H1WIFI_IDLE&&!wifi_settings_network.saved[0]){
                forget_pending=0;notice("已忘记网络，保存的凭据已删除");}
            if(auto_scan){auto_scan=0;next_op=H1NET_WIFI_SCAN;}
            else if(scan_pending&&wifi_settings_network.scan_id==scan_id){
                if(wifi_settings_network.scan_state==H1WIFI_SCAN_DONE){page_offset=candidate_count=0;next_op=H1NET_WIFI_PAGE;}
                else if(wifi_settings_network.scan_state==H1WIFI_SCAN_FAILED){scan_pending=0;notice("扫描失败；R重试，旧列表保留");}
            }
        }
    }else if(op==H1NET_WIFI_SCAN){
        r=h1_esp32_wifi_scan(&esp,&scan_id);
        if(!r){scan_pending=1;notice("正在扫描附近的2.4GHz网络…");}
    }else if(op==H1NET_WIFI_PAGE){
        unsigned n=12,total=0;
        r=h1_esp32_wifi_page(&esp,scan_id,page_offset,candidates+candidate_count,&n,&total);
        if(!r){candidate_count+=n;page_offset=candidate_count;
            if(candidate_count<total)next_op=H1NET_WIFI_PAGE;
            else{count=candidate_count;memcpy(wifi_settings_aps,candidates,count*sizeof *candidates);
                selected=0;scan_pending=0;dirty=1;notice(count?"选择网络，输入密码后连接":"未发现网络；R重扫或H手动添加");}
        }
    }else if(op==H1NET_WIFI_CONNECT){
        r=h1_esp32_wifi_connect(&esp,chosen,editor.value,auth,(int)reuse_password,&wifi_settings_network.operation);
        if(r!=H1_ESP32_AGAIN){wifi_editor_clear(&editor,63);reuse_password=0;view=VIEW_STATUS;dirty=1;}
        if(!r){wifi_settings_network.outcome=H1WIFI_JOINING;notice("正在连接，取得IP后才保存新配置");next_op=H1NET_WIFI_STATUS;}
    }else if(op==H1NET_WIFI_FORGET){
        r=h1_esp32_wifi_forget(&esp,&wifi_settings_network.operation);
        if(!r){forget_pending=1;notice("正在删除已保存的网络…");next_op=H1NET_WIFI_STATUS;}
    }else if(op==H1NET_WIFI_CANCEL){
        r=h1_esp32_wifi_cancel(&esp,wifi_settings_network.operation);
        if(!r){notice("已请求取消，正在核对状态…");next_op=H1NET_WIFI_STATUS;}
    }
    active_op=0;last_result=r;
    if(r==H1_ESP32_AGAIN)next_op=op; /* no request was accepted; safe local scheduling */
    else if(r){char line[128];snprintf(line,sizeof line,"通信错误：%s",h1_esp32_error_string(r));notice(line);}
}
int h1_app_main(void)
{
    int r=h1_esp32_open(&esp,idle,NULL);
    if(r)return h1_message_box(0,h1_esp32_error_string(r),"Wi-Fi setup",0);
    open_ok=1;
    if(!context_open()){h1_esp32_close(&esp);return 1;}
    h1_timer_start();wifi_editor_clear(&editor,63);paint();
    while(!closing){poll_input();network_step();
        if(dirty&&h1_timer_read_ms()-last_paint>=40)paint();
    }
    wifi_editor_clear(&editor,63);h1net_secure_clear(chosen,sizeof chosen);reuse_password=0;
    h1_esp32_close(&esp);h1_timer_stop();context_close();publish();return panic_code;
}
