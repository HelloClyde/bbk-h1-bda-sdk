#include "core.h"
void wifi_editor_clear(wifi_editor *e,unsigned maximum)
{h1net_secure_clear(e,sizeof *e);e->maximum=maximum>63?63:maximum;}
int wifi_editor_insert(wifi_editor *e,unsigned c)
{if(c<32||c>126||e->length>=e->maximum)return 0;e->value[e->length++]=(char)c;e->value[e->length]=0;return 1;}
void wifi_editor_delete(wifi_editor *e)
{if(e->length){e->value[--e->length]=0;e->value[e->length+1]=0;}}
unsigned wifi_editor_key(const wifi_editor *e,unsigned key)
{
    /* H1/Y100 V1.41 matrix positions, checked against SDK h1_input.h. */
    static const unsigned char matrix[44]={0,'q','w','e','r','t','y','u','a','s','d','f','g','h','j',0,
        'z','x','c','v','b','n',0,' ',0,0,0,0,0,'i','o','p','k','l',0,0,'m',0,0,0,0,0,0,0};
    if(key>=44)return 0;unsigned c=matrix[key];
    if(e->numeric){const char *letters="qwertyuiop",*digits="1234567890";for(unsigned i=0;i<10;i++)if(c==(unsigned)letters[i])return digits[i];}
    return e->upper&&c>='a'&&c<='z'?c-32:c;
}
const char *wifi_security_name(unsigned auth)
{return auth==0?"开放":auth==6?"WPA3":auth==7?"WPA2/3":auth==1?"WEP":auth==5?"企业认证":h1wifi_auth_supported(auth)?"WPA/WPA2":"不支持";}
const char *wifi_error_message(unsigned error)
{
    switch(error){
    case H1WIFI_ERR_AUTH:return "认证失败：检查密码和加密方式；旧配置保留";
    case H1WIFI_ERR_NO_AP:return "找不到该网络；请靠近路由器后重新扫描";
    case H1WIFI_ERR_TIMEOUT:return "连接超时；旧配置保留，可稍后重试";
    case H1WIFI_ERR_UNSUPPORTED:return "暂不支持此认证方式";
    case H1WIFI_ERR_STORAGE:return "ESP保存失败；旧配置保留";
    default:return "连接失败；旧配置保留，请重新扫描或重试";
    }
}
