/* SPDX-License-Identifier: Apache-2.0
 * H1 ESP32 contributors. SDK import with copyright-holder authorization. */
#include "h1net_wifi_config.h"
unsigned h1wifi_length(const char *s,unsigned maximum)
{unsigned n=0;if(!s)return maximum+1;while(n<=maximum&&s[n])n++;return n;}
int h1wifi_auth_supported(unsigned auth)
{return auth==0||auth==2||auth==3||auth==4||auth==6||auth==7;}
int h1wifi_credentials_valid(const uint8_t *ssid,unsigned sn,const uint8_t *password,unsigned pn,unsigned auth,unsigned reuse)
{
    if(!ssid||!sn||sn>32||pn>63||!h1wifi_auth_supported(auth)||reuse>1||(!password&&pn))return 0;
    for(unsigned i=0;i<sn;i++)if(!ssid[i])return 0;
    for(unsigned i=0;i<pn;i++)if(password[i]<32||password[i]==127)return 0;
    return auth==0?(!pn&&!reuse):reuse?!pn:(pn>=8);
}
void h1wifi_status_encode(uint8_t out[H1WIFI_STATUS_BYTES],const h1wifi_status *s)
{
    const uint32_t words[11]={1,s->operation,s->outcome,s->error,s->reason,s->connected,s->ip,
        (uint32_t)s->rssi,s->scan_id,s->scan_state,s->scan_count};
    for(unsigned i=0;i<H1WIFI_STATUS_BYTES;i++)out[i]=0;
    for(unsigned i=0;i<11;i++)h1net_put32(out+4*i,words[i]);
    unsigned a=h1wifi_length(s->current,32),b=h1wifi_length(s->saved,32);if(a>32)a=0;if(b>32)b=0;
    out[44]=(uint8_t)a;out[77]=(uint8_t)b;
    for(unsigned i=0;i<a;i++)out[45+i]=(uint8_t)s->current[i];
    for(unsigned i=0;i<b;i++)out[78+i]=(uint8_t)s->saved[i];
}
int h1wifi_status_decode(h1wifi_status *s,const uint8_t *p,unsigned n)
{
    if(n!=H1WIFI_STATUS_BYTES||h1net_u32(p)!=1||p[44]>32||p[77]>32||
       h1net_u32(p+8)>H1WIFI_FORGETTING||h1net_u32(p+12)>H1WIFI_ERR_DRIVER||
       h1net_u32(p+20)>1||h1net_u32(p+36)>H1WIFI_SCAN_FAILED||h1net_u32(p+40)>H1WIFI_AP_MAX)return 0;
    s->operation=h1net_u32(p+4);s->outcome=h1net_u32(p+8);s->error=h1net_u32(p+12);
    s->reason=h1net_u32(p+16);s->connected=h1net_u32(p+20);s->ip=h1net_u32(p+24);
    s->rssi=(int32_t)h1net_u32(p+28);s->scan_id=h1net_u32(p+32);s->scan_state=h1net_u32(p+36);s->scan_count=h1net_u32(p+40);
    for(unsigned i=0;i<p[44];i++){if(!p[45+i])return 0;s->current[i]=(char)p[45+i];}s->current[p[44]]=0;
    for(unsigned i=0;i<p[77];i++){if(!p[78+i])return 0;s->saved[i]=(char)p[78+i];}s->saved[p[77]]=0;return 1;
}
void h1wifi_ap_encode(uint8_t out[H1WIFI_AP_BYTES],const h1wifi_ap *a)
{
    unsigned n=h1wifi_length(a->ssid,32);if(n>32)n=0;
    for(unsigned i=0;i<H1WIFI_AP_BYTES;i++)out[i]=0;
    out[0]=(uint8_t)n;out[1]=(uint8_t)(int8_t)a->rssi;out[2]=(uint8_t)a->auth;out[3]=(uint8_t)a->channel;
    for(unsigned i=0;i<n;i++)out[4+i]=(uint8_t)a->ssid[i];
}
int h1wifi_ap_decode(h1wifi_ap *a,const uint8_t *p,unsigned n)
{
    if(n!=H1WIFI_AP_BYTES||p[0]>32||!p[3]||p[3]>14)return 0;
    for(unsigned i=0;i<p[0];i++){if(!p[4+i])return 0;a->ssid[i]=(char)p[4+i];}a->ssid[p[0]]=0;
    a->rssi=(int8_t)p[1];a->auth=p[2];a->channel=p[3];return 1;
}
