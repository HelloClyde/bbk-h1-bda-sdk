/* SPDX-License-Identifier: Apache-2.0 */
#include "h1_esp32.h"
static uint32_t generation_counter;

static void clear(void *p, unsigned n) { unsigned char *v=p; while(n--) *v++=0; }
static int error(h1_esp32 *e,int code)
{
    e->error.code=code;
    e->error.remote_status=code==H1NET_REMOTE?e->client.remote_status:0;
    return code;
}
static void invalidate(h1_esp32 *e)
{
    e->client.pending_rpc=0;e->client.broken=1;e->client.ready=0;
    e->phase=0;e->attached=0;
    if(!++generation_counter)++generation_counter;
    e->generation=generation_counter;
}
int h1_esp32_init(h1_esp32 *e,const h1_esp32_driver *d)
{
    if(!e||!d||!d->io.read||!d->io.write||!d->io.now_ms||!d->io.idle||
       !d->connected||!d->epoch||!d->bind)return H1_ESP32_ARGUMENT;
    clear(e,sizeof *e);e->driver=*d;e->state=H1_ESP32_WAIT_USB;
    h1net_client_init(&e->client,&d->io,4000);return 0;
}
void h1_esp32_close(h1_esp32 *e)
{
    if(e->busy)return; /* never shut down from an in-flight idle callback */
    if(e->driver.close)e->driver.close(e->driver.io.context);
    invalidate(e);h1net_secure_clear(&e->client,sizeof e->client);
    e->state=H1_ESP32_WAIT_USB;
}
enum h1_esp32_state h1_esp32_poll(h1_esp32 *e)
{
    if(e->busy)return e->state;
    void *ctx=e->driver.io.context;
    if(e->driver.poll)e->driver.poll(ctx);
    uint32_t now=e->driver.io.now_ms(ctx), epoch=e->driver.epoch(ctx);
    if(!e->driver.connected(ctx)){
        if(e->attached)invalidate(e);
        e->state=H1_ESP32_WAIT_USB;return e->state;
    }
    if(!e->attached||epoch!=e->epoch){
        invalidate(e);e->epoch=epoch;e->attached=1;e->driver.bind(ctx);
        h1net_client_init(&e->client,&e->driver.io,1000);
        e->state=H1_ESP32_CHECKING;e->next_probe=now;
    }
    if(!e->phase){
        if((int32_t)(now-e->next_probe)<0)return e->state;
        e->phase=e->client.ready?3:1;e->client.rpc_timeout_ms=1000;
        int r=h1net_rpc_begin(&e->client,&e->probe,
                              e->phase==1?H1NET_HELLO:H1NET_STATUS,0,0);
        if(r){error(e,r);goto failed;}
    }
    /* One bounded transport/parser step per poll; never io.idle or callbacks. */
    int r=h1net_rpc_poll(&e->client,&e->probe);
    if(!r)return e->state;
    if(r<0){error(e,r);goto failed;}
    const uint8_t *p=e->client.response;unsigned n=e->client.response_length;
    if(e->phase==1){
        static const char identity[]=H1NET_IDENTITY;
        if(n!=sizeof identity-1)goto malformed;
        for(unsigned i=0;i<n;i++)if(p[i]!=(uint8_t)identity[i])goto malformed;
        e->client.ready=1;e->phase=2;
    }else if(e->phase==2){
        if(n!=16||h1net_u32(p)!=1||!h1net_u32(p+4)||
           h1net_u32(p+4)>H1NET_MAX_JOBS||!h1net_u32(p+8))goto malformed;
        e->client.max_jobs=h1net_u32(p+4);e->client.features=h1net_u32(p+12);
        e->phase=3;
    }else {
        if(n!=24||h1net_u32(p)!=1||h1net_u32(p+4)>1||
           !h1net_u32(p+20)||h1net_u32(p+20)>H1NET_MAX_DOWNLOAD)goto malformed;
        for(unsigned i=0;i<6;i++)e->status[i]=h1net_u32(p+i*4);
        e->state=e->status[1]?H1_ESP32_READY:H1_ESP32_WIFI_OFFLINE;
        e->phase=0;e->client.rpc_timeout_ms=4000;e->next_probe=now+1000;
        error(e,0);return e->state;
    }
    r=h1net_rpc_begin(&e->client,&e->probe,
                      e->phase==2?H1NET_CAPABILITIES:H1NET_STATUS,0,0);
    if(r){error(e,r);goto failed;}return e->state;
malformed:error(e,H1NET_PROTOCOL);
failed:
    invalidate(e);e->state=H1_ESP32_DEVICE_ERROR;
    /* Preserve attached epoch and retry after backoff, not on every UI frame. */
    e->attached=1;e->epoch=epoch;e->next_probe=now+3000;
    e->driver.bind(ctx);h1net_client_init(&e->client,&e->driver.io,1000);
    return e->state;
}
uint32_t h1_esp32_features(const h1_esp32 *e){return e->client.features;}
static int enter(h1_esp32 *e,uint32_t feature)
{
    if(e->busy||e->phase)return error(e,H1_ESP32_AGAIN);
    if(!e->driver.connected(e->driver.io.context)||
       e->driver.epoch(e->driver.io.context)!=e->epoch||!e->attached)
        return error(e,H1_ESP32_STALE);
    if(!e->client.ready||e->client.broken)return error(e,H1_ESP32_AGAIN);
    if((e->client.features&feature)!=feature)return error(e,H1_ESP32_UNSUPPORTED);
    e->error.http_status=e->error.backend_error=0;e->busy=1;return 0;
}
static int leave(h1_esp32 *e,int r)
{
    e->busy=0;
    if(e->client.broken){invalidate(e);e->state=H1_ESP32_DEVICE_ERROR;}
    return error(e,r);
}
static int check(h1_esp32 *e,h1_esp32_stream *s)
{
    if(!s||!s->job||s->owner!=e)return error(e,H1_ESP32_ARGUMENT);
    if(s->generation!=e->generation||!e->driver.connected(e->driver.io.context)||
       e->epoch!=e->driver.epoch(e->driver.io.context))return error(e,H1_ESP32_STALE);
    return 0;
}
int h1_esp32_http_open(h1_esp32 *e,h1_esp32_stream *s,const h1_esp32_request *q)
{
    if(!s||s->job||!q||!q->url)return error(e,H1_ESP32_ARGUMENT);
    int r=enter(e,H1NET_CAP_HTTP_REQUEST|H1NET_CAP_RELEASE);if(r)return r;
    clear(s,sizeof *s);s->maximum=q->max_bytes?q->max_bytes:262144;
    r=h1net_http_request_open(&e->client,q->url,q->method?q->method:"GET",
        q->headers?q->headers:"",q->body,q->body_length,s->maximum,
        q->timeout_ms?q->timeout_ms:30000,&s->job);
    s->owner=e;s->generation=e->generation;return leave(e,r);
}
int h1_esp32_http_range(h1_esp32 *e,h1_esp32_stream *s,const char *url,const h1net_range *q)
{
    if(!s||s->job||!q)return error(e,H1_ESP32_ARGUMENT);
    int r=enter(e,H1NET_CAP_RELEASE);if(r)return r;
    clear(s,sizeof *s);s->maximum=q->length;
    r=h1net_http_range_open(&e->client,url,q,&s->job);
    s->owner=e;s->generation=e->generation;return leave(e,r);
}
int h1_esp32_http_read(h1_esp32 *e,h1_esp32_stream *s,void *out,unsigned cap)
{
    if(!out||!cap)return error(e,H1_ESP32_ARGUMENT);
    int r=check(e,s);if(r)return r;
    r=enter(e,0);if(r)return r;
    r=h1net_http_info(&e->client,s->job,&s->info);
    if(r)return leave(e,r);
    h1net_info *i=&s->info;e->error.http_status=i->http_status;e->error.backend_error=i->error;
    if(i->received>s->maximum||i->delivered>i->received||i->delivered!=s->offset||i->buffered>i->received-i->delivered)
        return leave(e,H1NET_PROTOCOL);
    if(i->state==H1NET_FAILED||i->state==H1NET_CANCELLED)return leave(e,H1NET_REMOTE);
    if(i->buffered){
        unsigned n=cap>500?500:cap,got=0;if(n>i->buffered)n=i->buffered;
        r=h1net_http_read(&e->client,s->job,s->offset,out,n,&got);
        if(!r&&!got)r=H1NET_PROTOCOL;
        if(!r)s->offset+=got;
        leave(e,r);return r?r:(int)got;
    }
    if(i->state!=H1NET_DONE)return leave(e,H1_ESP32_AGAIN);
    if(i->content_length!=0xffffffffu&&s->offset!=i->content_length)
        return leave(e,H1NET_PROTOCOL);
    s->eof=1;return leave(e,0);
}
int h1_esp32_http_close(h1_esp32 *e,h1_esp32_stream *s)
{
    if(!s)return error(e,H1_ESP32_ARGUMENT);
    if(!s->job)return 0;
    if(s->owner!=e)return error(e,H1_ESP32_ARGUMENT);
    int r=check(e,s);
    if(r){clear(s,sizeof *s);return r;}
    r=enter(e,H1NET_CAP_RELEASE);if(r)return r;
    r=h1net_http_release(&e->client,s->job);
    if(!r||(r==H1NET_REMOTE&&e->client.remote_status==H1NET_BAD_JOB))clear(s,sizeof *s);
    return leave(e,r);
}
int h1_esp32_http_headers(h1_esp32 *e,h1_esp32_stream *s,char *out,unsigned cap)
{
    if(!out||!cap)return error(e,H1_ESP32_ARGUMENT);out[0]=0;
    int r=check(e,s);if(r)return r;
    r=enter(e,H1NET_CAP_HTTP_HEADERS);if(r)return r;
    unsigned offset=0,total=0;
    do{
        uint8_t req[12],reply[512];unsigned n=0;
        h1net_put32(req,s->job);h1net_put32(req+4,offset);h1net_put32(req+8,500);
        r=h1net_rpc(&e->client,H1NET_HTTP_HEADERS,req,12,reply,sizeof reply,&n);
        if(!r){unsigned size=n>=12?h1net_u32(reply+8):0;
            if(n<12||h1net_u32(reply)!=s->job||h1net_u32(reply+4)!=offset||
               size>8192||offset>size||n-12>size-offset||(offset&&size!=total)||
               (offset<size&&n==12))r=H1NET_PROTOCOL;
            else if(size>=cap)r=H1_ESP32_LIMIT;
            else{total=size;for(unsigned j=12;j<n;j++)out[offset++]=(char)reply[j];}
        }
        h1net_secure_clear(reply,sizeof reply);
        if(r)break;
    }while(offset<total);
    if(r)h1net_secure_clear(out,cap);else out[offset]=0;
    return leave(e,r);
}
int h1_esp32_get(h1_esp32 *e,const char *url,void *out,unsigned cap,unsigned *length)
{
    if(!out||!cap||!length)return error(e,H1_ESP32_ARGUMENT);*length=0;
    h1_esp32_request q={url,0,0,0,0,cap,0};h1_esp32_stream s={0};
    int r=h1_esp32_http_open(e,&s,&q);if(r)return r;
    uint32_t start=e->driver.io.now_ms(e->driver.io.context);
    for(;;){
        uint8_t extra;unsigned space=cap-*length;
        r=h1_esp32_http_read(e,&s,space?(uint8_t *)out+*length:&extra,space?space:1);
        if(r>0){if(!space){r=H1_ESP32_LIMIT;break;}*length+=(unsigned)r;start=e->driver.io.now_ms(e->driver.io.context);continue;}
        if(r==H1_ESP32_AGAIN){
            if(e->driver.io.now_ms(e->driver.io.context)-start>=31000){r=H1NET_TIMEOUT;break;}
            e->driver.io.idle(e->driver.io.context);continue;
        }
        if(!r&&(s.info.http_status<200||s.info.http_status>=300))r=H1_ESP32_HTTP;
        break;
    }
    h1_esp32_error_info saved=e->error;
    int closed=h1_esp32_http_close(e,&s);
    /* A convenience call cannot return a live handle. If RELEASE failed,
     * invalidate the generation so next HELLO reclaims all remote jobs. */
    if(s.job){invalidate(e);e->state=H1_ESP32_DEVICE_ERROR;}
    if(r){e->error=saved;return error(e,r);}return closed;
}
static int wifi_rpc(h1_esp32 *e,unsigned op,const void *p,unsigned n,void *out,unsigned cap,unsigned *got)
{
    int r=enter(e,H1NET_CAP_WIFI_CONFIG);if(r)return r;
    r=h1net_rpc(&e->client,(uint8_t)op,p,n,out,cap,got);return leave(e,r);
}
int h1_esp32_wifi_status(h1_esp32 *e,h1wifi_status *out)
{
    if(!out)return error(e,H1_ESP32_ARGUMENT);
    uint8_t p[H1WIFI_STATUS_BYTES];unsigned n=0;
    int r=wifi_rpc(e,H1NET_WIFI_STATUS,0,0,p,sizeof p,&n);
    if(!r&&!h1wifi_status_decode(out,p,n))r=H1NET_PROTOCOL;return error(e,r);
}
static int wifi_start(h1_esp32 *e,unsigned op,const void *p,unsigned n,uint32_t *id)
{
    if(!id)return error(e,H1_ESP32_ARGUMENT);*id=0;
    uint8_t out[4];unsigned got=0;
    int r=wifi_rpc(e,op,p,n,out,sizeof out,&got);
    if(!r){if(got!=4||!h1net_u32(out))r=H1NET_PROTOCOL;else *id=h1net_u32(out);}
    return error(e,r);
}
int h1_esp32_wifi_scan(h1_esp32 *e,uint32_t *id){return wifi_start(e,H1NET_WIFI_SCAN,0,0,id);}
int h1_esp32_wifi_forget(h1_esp32 *e,uint32_t *id){return wifi_start(e,H1NET_WIFI_FORGET,0,0,id);}
int h1_esp32_wifi_cancel(h1_esp32 *e,uint32_t id)
{
    uint8_t p[4];unsigned n=0;h1net_put32(p,id);
    int r=wifi_rpc(e,H1NET_WIFI_CANCEL,p,4,0,0,&n);
    return error(e,r?r:(n?H1NET_PROTOCOL:0));
}
int h1_esp32_wifi_connect(h1_esp32 *e,const char *ssid,const char *password,
                          unsigned auth,int reuse,uint32_t *id)
{
    if(!ssid||!password||auth>255||reuse<0||reuse>1)return error(e,H1_ESP32_ARGUMENT);
    unsigned a=h1wifi_length(ssid,33),b=h1wifi_length(password,64);
    if(!h1wifi_credentials_valid((const uint8_t *)ssid,a,(const uint8_t *)password,b,auth,(unsigned)reuse))
        return error(e,H1_ESP32_ARGUMENT);
    uint8_t p[100];p[0]=(uint8_t)a;p[1]=(uint8_t)b;p[2]=(uint8_t)auth;p[3]=(uint8_t)reuse;
    for(unsigned i=0;i<a;i++)p[4+i]=(uint8_t)ssid[i];
    for(unsigned i=0;i<b;i++)p[4+a+i]=(uint8_t)password[i];
    int r=wifi_start(e,H1NET_WIFI_CONNECT,p,4+a+b,id);
    h1net_secure_clear(p,sizeof p);return r;
}
int h1_esp32_wifi_page(h1_esp32 *e,uint32_t id,unsigned offset,h1wifi_ap *aps,unsigned *count,unsigned *total)
{
    if(!aps||!count||!total||!*count||*count>12||offset>24)return error(e,H1_ESP32_ARGUMENT);
    unsigned cap=*count;*count=*total=0;uint8_t req[6],p[512];unsigned n=0;
    h1net_put32(req,id);h1net_put16(req+4,(uint16_t)offset);
    int r=wifi_rpc(e,H1NET_WIFI_PAGE,req,6,p,sizeof p,&n);if(r)return r;
    if(n<8||h1net_u32(p)!=id||p[5]>12||p[6]>24||p[7]||n!=8u+p[5]*36u||
       offset>p[6]||p[5]>p[6]-offset)return error(e,H1NET_PROTOCOL);
    if(p[4]==H1WIFI_SCANNING)return error(e,H1_ESP32_AGAIN);
    if(p[4]!=H1WIFI_SCAN_DONE||(!p[5]&&offset<p[6]))return error(e,H1NET_PROTOCOL);
    if(p[5]>cap)return error(e,H1_ESP32_LIMIT);
    h1wifi_ap scratch[12];
    for(unsigned i=0;i<p[5];i++)if(!h1wifi_ap_decode(scratch+i,p+8+i*36,36))return error(e,H1NET_PROTOCOL);
    for(unsigned i=0;i<p[5];i++)aps[i]=scratch[i];*count=p[5];*total=p[6];return error(e,0);
}
int h1_esp32_audio_rpc(h1_esp32 *e,unsigned op,const void *p,unsigned n,void *out,unsigned cap,unsigned *got)
{
    if(op<H1NET_AUDIO_OPEN||op>H1NET_AUDIO_FINISH||!got)return error(e,H1_ESP32_ARGUMENT);
    int r=enter(e,H1NET_CAP_AUDIO_AAC);if(r)return r;
    r=h1net_rpc(&e->client,(uint8_t)op,p,n,out,cap,got);return leave(e,r);
}
const char *h1_esp32_error_string(int e)
{
    switch(e){case 0:return "OK";case H1NET_IO:return "USB I/O failed";
    case H1NET_TIMEOUT:return "RPC/network timeout";case H1NET_PROTOCOL:return "Invalid protocol response";
    case H1NET_REMOTE:return "Peripheral rejected operation; inspect remote/backend status";
    case H1NET_SINK:return "Output sink failed";case H1NET_ABORTED:return "Cancelled";
    case H1_ESP32_AGAIN:return "Pending; pump UI and retry";case H1_ESP32_UNSUPPORTED:return "Firmware capability unavailable";
    case H1_ESP32_STALE:return "USB session changed; discard old stream";case H1_ESP32_ARGUMENT:return "Invalid argument";
    case H1_ESP32_LIMIT:return "Buffer/response limit exceeded";case H1_ESP32_HTTP:return "HTTP status was not 2xx";
    case H1_ESP32_OWNER_BUSY:return "USB controller has another owner";case H1_ESP32_FIRMWARE:return "Unsupported H1 firmware";
    default:return "Unknown error";}
}
