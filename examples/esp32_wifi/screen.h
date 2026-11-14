#include "style.h"
static void paint(void)
{
    char line[160];rect(0,0,480,272,UI_BG);
    ui_round(12,6,21,21,6,UI_BLUE);ui_wifi(14,7,18,3,UI_WHITE,UI_WHITE);
    ui_bold("Wi-Fi设置",42,9,220,UI_INK);
    if(view!=VIEW_LIST){ui_chevron(402,12,1,UI_BLUE);text("返回",415,9,478,UI_BLUE);}
    else if(count){
        snprintf(line,sizeof line,"%u/%u",selected/5+1,(count+4)/5);ui_center(line,286,9,52,UI_SUB);
        ui_round(344,4,62,24,7,UI_WHITE);ui_center("上页",344,8,62,selected>=5?UI_BLUE:UI_SUB);
        ui_round(412,4,62,24,7,UI_WHITE);ui_center("下页",412,8,62,(selected/5+1)*5<count?UI_BLUE:UI_SUB);
    }
    if(view==VIEW_LIST){
        if(!capable)snprintf(line,sizeof line,"%s",connected()?"ESP未就绪 · 按R重新检测":"USB未连接 · 请连接ESP32");
        else snprintf(line,sizeof line,"%s%s",wifi_settings_network.connected?"已连接  ":"已保存  ",wifi_settings_network.connected?wifi_settings_network.current:wifi_settings_network.saved[0]?wifi_settings_network.saved:"尚无网络");
        ui_dot(18,44,3,capable&&wifi_settings_network.connected?UI_GREEN:UI_SUB);text_ellipsis(line,29,36,467,UI_SUB);
        if(!count){
            ui_round(10,62,460,160,11,UI_WHITE);ui_round(210,77,60,60,17,UI_TINT);ui_wifi(217,80,46,3,UI_BLUE,UI_BLUE);
            ui_center(scan_pending?"正在寻找附近的网络…":"连接ESP后，扫描附近的网络",16,153,448,UI_INK);
            ui_center("支持2.4GHz · 密码仅保存在ESP",16,184,448,UI_SUB);
        }else{
            ui_round(10,62,460,160,10,UI_WHITE);unsigned first=selected/5*5;
            for(unsigned row=0;row<5&&first+row<count;row++){
                unsigned i=first+row;int y=62+(int)row*32;int chosen_row=i==selected;
                if(chosen_row){ui_round(11,y+1,458,30,8,UI_TINT);ui_round(11,y+8,3,16,1,UI_BLUE);}
                else if(row)rect(24,y,432,1,UI_LINE);
                text_ellipsis(wifi_settings_aps[i].ssid,24,y+8,294,chosen_row?UI_BLUE:UI_INK);
                text(wifi_security_name(wifi_settings_aps[i].auth),302,y+8,383,UI_SUB);
                if(wifi_settings_aps[i].auth)ui_lock(391,y+10,UI_SUB);
                int rssi=wifi_settings_aps[i].rssi;unsigned strength=rssi>=-55?3:rssi>=-72?2:1;
                ui_wifi(416,y+6,24,strength,UI_SUB,UI_LINE);
                if(capable&&wifi_settings_network.connected&&!strcmp(wifi_settings_aps[i].ssid,wifi_settings_network.current))ui_check(449,y+11,UI_BLUE);
                else ui_chevron(452,y+12,0,UI_RGB(0xa8a8b0));
            }
        }
    }else if(view==VIEW_EDIT){
        ui_round(10,35,460,25,7,UI_WHITE);text_ellipsis(edit_ssid?"手动添加网络":chosen,20,40,356,UI_INK);
        ui_round(365,39,94,18,5,UI_BG);ui_center(wifi_security_name(auth),365,40,94,UI_SUB);
        ui_round(8,65,464,36,9,UI_RGB(0xc3daf8));ui_round(9,66,462,34,8,UI_WHITE);
        if(!edit_ssid)ui_lock(21,77,UI_SUB);else text("名",18,75,38,UI_SUB);
        unsigned n=editor.length;
        if(!n&&reuse_password)text("已保存密码，直接连接或输入新密码",42,75,460,UI_SUB);
        else if(!n)text(edit_ssid?"网络名称":"输入Wi-Fi密码",42,75,460,UI_RGB(0x8e8e93));
        else if(!edit_ssid&&!editor.reveal){unsigned shown=n>48?48:n;for(unsigned i=0;i<shown;i++)ui_dot(45+(int)i*8,83,2,UI_INK);}
        else {char display[65];memcpy(display,editor.value,n+1);text(display+(n>51?n-51:0),42,75,460,UI_INK);h1net_secure_clear(display,sizeof display);}
        for(unsigned row=0;row<4;row++)for(unsigned col=0;col<12;col++){
            int x=(int)col*40+3,y=108+(int)row*26;
            ui_round(x,y+1,34,23,4,UI_RGB(0xc7c7cc));ui_round(x,y,34,22,4,UI_WHITE);
            char key[2]={keyboard(row)[col],0};ui_center(key,x,y+3,34,UI_INK);
        }
        snprintf(line,sizeof line,"%s %u/%u · Shift %s · Fn %s",edit_ssid?"名称":"密码",editor.length,editor.maximum,editor.upper?"大写":"小写",editor.numeric?"数字":"字母");
        text(line,13,215,473,UI_SUB);
    }else if(view==VIEW_STATUS){
        int failed=capable&&wifi_settings_network.outcome==H1WIFI_FAILED;
        uint16_t accent=!capable?UI_SUB:failed?UI_RED:wifi_settings_network.connected?UI_GREEN:UI_BLUE;
        ui_round(10,37,460,185,11,UI_WHITE);ui_round(23,48,36,36,10,accent);ui_wifi(27,49,29,3,UI_WHITE,UI_WHITE);
        text_ellipsis(wifi_settings_network.current[0]?wifi_settings_network.current:"尚未连接网络",72,47,454,UI_INK);
        const char *brief=!capable?"USB未连接 · 上次记录":wifi_settings_network.outcome==H1WIFI_FORGETTING?"正在忘记网络…":wifi_settings_network.outcome==H1WIFI_JOINING?"正在连接，最多约20秒…":wifi_settings_network.connected?"已连接到Wi-Fi":"未连接到Wi-Fi";
        text(brief,72,71,454,UI_SUB);rect(24,96,432,1,UI_LINE);
        text("IP地址",24,108,105,UI_SUB);uint32_t ip=wifi_settings_network.ip;
        snprintf(line,sizeof line,"%lu.%lu.%lu.%lu",(unsigned long)(ip&255),(unsigned long)((ip>>8)&255),(unsigned long)((ip>>16)&255),(unsigned long)(ip>>24));text(line,110,108,306,UI_INK);
        snprintf(line,sizeof line,"%ld dBm",(long)wifi_settings_network.rssi);text(line,358,108,457,UI_SUB);
        rect(110,133,346,1,UI_LINE);text("已保存",24,144,105,UI_SUB);text_ellipsis(wifi_settings_network.saved[0]?wifi_settings_network.saved:"无",110,144,455,UI_INK);
        const char *state=!capable?"USB未就绪，Wi-Fi状态待确认":wifi_settings_network.outcome==H1WIFI_FORGETTING?"正在删除保存的网络…":wifi_settings_network.outcome==H1WIFI_JOINING?"连接成功后，自动保存到ESP":wifi_settings_network.outcome==H1WIFI_SAVED?"连接成功，设置已保存":wifi_settings_network.outcome==H1WIFI_CANCELLED?"连接已取消，原配置保留":failed?"连接失败，原配置保留":wifi_settings_network.connected?"网络已就绪":"扫描并选择一个网络开始";
        ui_round(22,174,436,23,6,failed?UI_RGB(0xffeded):UI_BG);
        if(capable&&wifi_settings_network.connected&&!failed)ui_check(32,181,accent);else ui_dot(37,185,3,accent);
        text(state,53,178,450,accent);
        ui_center(capable&&wifi_settings_network.connected?"退出设置，打开H1新闻即可使用":"请确认路由器已开启2.4GHz网络",20,202,440,UI_SUB);
    }else{
        ui_round(10,44,460,176,12,UI_WHITE);ui_round(26,62,43,43,13,UI_RGB(0xffeded));ui_wifi(31,64,33,3,UI_RED,UI_RED);
        ui_bold("忘记这个网络？",85,61,452,UI_INK);text_ellipsis(wifi_settings_network.saved[0]?wifi_settings_network.saved:"尚无保存的网络",85,87,452,UI_SUB);
        rect(26,119,428,1,UI_LINE);text("ESP将删除保存的网络和密码。",27,135,454,UI_INK);
        text("下次连接时，需要重新输入密码。",27,168,454,UI_SUB);
    }
    ui_dot(12,239,3,UI_BLUE);text_ellipsis(wifi_settings_notice,23,231,474,UI_SUB);
    rect(0,251,480,21,UI_WHITE);rect(0,250,480,1,UI_LINE);
    if(view==VIEW_EDIT){
        const char *labels[]={editor.upper?"小写":"大写","空格","删除",edit_ssid?(auth?"改为开放":"改为加密"):(editor.reveal?"隐藏":"显示"),edit_ssid?"下一步":"连接"};
        for(unsigned i=0;i<5;i++)ui_button(labels[i],(int)i*96+4,88,i==4?UI_BLUE:UI_BG,i==4?UI_WHITE:UI_BLUE);
    }else if(view==VIEW_FORGET){ui_button("忘记网络 Enter",7,226,UI_RED,UI_WHITE);ui_button("取消 Esc",247,226,UI_TINT,UI_BLUE);}
    else {
        const char *list[]={"扫描 R","手动 H","状态 S","退出 Esc"};const char *status[]={"扫描 R","忘记 F","取消 C","返回 Esc"};
        for(unsigned i=0;i<4;i++)ui_button(view==VIEW_LIST?list[i]:status[i],(int)i*120+5,110,i==0?UI_TINT:UI_WHITE,view==VIEW_STATUS&&i==1?UI_RED:UI_BLUE);
    }
    h1_game_framebuffer fb;
    if(h1_game_framebuffer_get(&fb)&&fb.width==480&&fb.height==272&&
       h1_game_blit_rgb565(&fb,pixels,480,0,0,480,272))presents++;
    dirty=0;last_paint=h1_timer_read_ms();publish();
}
