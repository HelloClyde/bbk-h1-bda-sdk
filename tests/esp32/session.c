/* SPDX-License-Identifier: Apache-2.0
 * Exercise the real wire parser, client, server and SDK facade together.
 * The peripheral's network/USB boundary is deterministic, not physical. */
#include "h1_esp32.h"
#include "h1net_server.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#undef assert
#define assert(x) do { if(!(x)){fprintf(stderr,"CHECK failed at %s:%d: %s\n",__FILE__,__LINE__,#x);exit(1);} } while(0)

static h1_esp32 esp;
static h1net_server server;
static h1net_parser parser;
static uint8_t reply[528];
static unsigned reply_used,reply_offset;
static uint32_t clock_value,usb_epoch=1,features=63;
static int plugged=1,wifi=1,silent,corrupt,block,failed,bad_length;
static int fail_release;
static unsigned calls[32],releases,idle_count,reenter;
static char last_method[8];
static unsigned posted;
static unsigned read_steps;
static struct {unsigned used,offset,size,status;} jobs[4];
static unsigned body_size=1301,http_status=200;
static uint16_t reset(void *p){(void)p;memset(jobs,0,sizeof jobs);return 0;}
static void status(void *p,uint32_t out[6])
{(void)p;uint32_t v[6]={1,(uint32_t)wifi,1,0x0100007f,0,H1NET_MAX_DOWNLOAD};memcpy(out,v,sizeof v);}
static void caps(void *p,uint32_t out[4])
{(void)p;uint32_t v[4]={1,4,8192,features};memcpy(out,v,sizeof v);}
static uint16_t request(void *p,const char *url,const char *method,const char *headers,
                        const void *body,unsigned n,uint32_t max,uint32_t timeout,uint32_t *id)
{
    (void)p;(void)headers;(void)body;assert(strlen(url)<=2048);assert(timeout>=1000&&timeout<=60000);
    strcpy(last_method,method);posted=n;
    for(unsigned i=0;i<4;i++)if(!jobs[i].used){
        jobs[i].used=1;jobs[i].offset=0;jobs[i].size=body_size>max?max:body_size;
        jobs[i].status=http_status;*id=i+1;return 0;
    }return H1NET_BUSY;
}
static uint16_t range_open(void *p,const char *url,const h1net_range *r,uint32_t *id)
{
    uint16_t v=request(p,url,"GET","",0,0,r->length,r->timeout_ms,id);
    if(!v){jobs[*id-1].status=206;jobs[*id-1].size=r->length;}return v;
}
static uint16_t info(void *p,uint32_t id,h1net_info *out)
{
    (void)p;if(!id||id>4||!jobs[id-1].used)return H1NET_BAD_JOB;
    unsigned j=id-1,size=jobs[j].size,offset=jobs[j].offset;
    h1net_info v={id,failed?H1NET_FAILED:block?H1NET_RECEIVING:H1NET_DONE,
        jobs[j].status,bad_length?size+1:size,block?offset:size,offset,block?0:size-offset,
        failed?H1NET_ERROR_TIMEOUT:0};*out=v;return 0;
}
static uint16_t read_body(void *p,uint32_t id,unsigned want,uint8_t *out,uint32_t *offset,unsigned *n)
{
    (void)p;if(!id||id>4||!jobs[id-1].used)return H1NET_BAD_JOB;
    unsigned j=id-1;*offset=jobs[j].offset;*n=jobs[j].size-jobs[j].offset;if(*n>want)*n=want;
    for(unsigned i=0;i<*n;i++)out[i]=(uint8_t)((jobs[j].offset+i)%251);
    jobs[j].offset+=*n;return 0;
}
static uint16_t release(void *p,uint32_t id)
{(void)p;if(fail_release)return H1NET_BUSY;if(!id||id>4||!jobs[id-1].used)return H1NET_BAD_JOB;jobs[id-1].used=0;releases++;return 0;}
static uint16_t fields(void *p,uint32_t id,uint32_t offset,unsigned wanted,uint8_t *out,unsigned *n,unsigned *total)
{
    (void)p;(void)id;static const char data[]="Set-Cookie: first=fixture\r\nSet-Cookie: second=fixture\r\n";
    *total=sizeof data-1;if(offset>*total)return H1NET_INVALID;
    *n=*total-offset;if(*n>wanted)*n=wanted;memcpy(out,data+offset,*n);return 0;
}
static uint16_t wifi_call(void *ctx,uint8_t op,const uint8_t *p,unsigned n,uint8_t *out,unsigned *got)
{
    (void)ctx;*got=0;
    if(op==H1NET_WIFI_STATUS){h1wifi_status s={0};s.connected=wifi;s.scan_id=7;s.scan_state=H1WIFI_SCAN_DONE;s.scan_count=1;
        strcpy(s.current,"fixture-ap");h1wifi_status_encode(out,&s);*got=110;}
    else if(op==H1NET_WIFI_PAGE){assert(n==6);h1net_put32(out,h1net_u32(p));out[4]=H1WIFI_SCAN_DONE;out[5]=1;out[6]=1;out[7]=0;
        h1wifi_ap ap={"fixture-ap",-45,3,6};h1wifi_ap_encode(out+8,&ap);*got=44;}
    else if(op==H1NET_WIFI_CONNECT){assert(n>=4);assert(h1wifi_credentials_valid(p+4,p[0],p+4+p[0],p[1],p[2],p[3]));h1net_put32(out,9);*got=4;}
    else if(op==H1NET_WIFI_SCAN||op==H1NET_WIFI_FORGET){h1net_put32(out,7);*got=4;}
    return 0;
}
static void frame(void *ctx,const h1net_frame *f)
{
    (void)ctx;calls[f->opcode]++;
    if(silent&&f->opcode==H1NET_HTTP_REQUEST)return;
    assert(reply_offset==reply_used);
    reply_used=(unsigned)h1net_server_dispatch(&server,f,reply,sizeof reply);reply_offset=0;
    if(corrupt)reply[reply_used-1]^=1;
}
static int write_bytes(void *p,const uint8_t *data,unsigned n)
{(void)p;if(n>23)n=23;h1net_parser_feed(&parser,data,n,frame,0);return (int)n;}
static int read_bytes(void *p,uint8_t *out,unsigned n)
{
    if(++read_steps%3u==0)return 0; /* realistic short wait; exercises idle/reentry guard */
    (void)p;unsigned count=reply_used-reply_offset;if(count>n)count=n;if(count>19)count=19;
    memcpy(out,reply+reply_offset,count);reply_offset+=count;return (int)count;
}
static uint32_t now(void *p){(void)p;return clock_value++;}
static int connected(void *p){(void)p;return plugged;}
static uint32_t epoch(void *p){(void)p;return usb_epoch;}
static void bind(void *p){(void)p;reply_offset=reply_used=0;h1net_parser_init(&parser);}
static void idle(void *p)
{
    (void)p;clock_value+=3;idle_count++;
    if(reenter){h1wifi_status s;assert(h1_esp32_wifi_status(&esp,&s)==H1_ESP32_AGAIN);}
}
static void prepare(void)
{
    clock_value=0;features=63;plugged=wifi=1;silent=corrupt=block=failed=bad_length=0;
    idle_count=reenter=releases=0;body_size=1301;http_status=200;memset(calls,0,sizeof calls);
    fail_release=0;
    h1net_backend b={0};b.reset=reset;b.status=status;b.capabilities=caps;b.request=request;
    b.info=info;b.read=read_body;b.range=range_open;b.release=release;b.headers=fields;b.wifi=wifi_call;
    h1net_server_init(&server,&b);reset(0);bind(0);
    h1_esp32_driver d={{0,write_bytes,read_bytes,0,idle,now},0,connected,epoch,bind,0};
    assert(!h1_esp32_init(&esp,&d));
}
static void ready(void)
{for(unsigned i=0;i<1000;i++)if(h1_esp32_poll(&esp)==(wifi?H1_ESP32_READY:H1_ESP32_WIFI_OFFLINE))return;assert(!"probe did not finish");}
static void test_detection(void)
{
    prepare();plugged=0;assert(h1_esp32_poll(&esp)==H1_ESP32_WAIT_USB);
    plugged=1;ready();assert(calls[1]==1&&calls[10]==1);assert(idle_count==0);assert(esp.client.features==63);
    wifi=0;clock_value+=1001;ready();assert(esp.state==H1_ESP32_WIFI_OFFLINE);
    wifi=1;clock_value+=1001;ready();assert(esp.state==H1_ESP32_READY);
}
static void test_download(void)
{
    prepare();ready();uint8_t out[1400];unsigned n=0;
    reenter=1;assert(!h1_esp32_get(&esp,"https://example.com/data",out,sizeof out,&n));
    assert(n==1301&&releases==1&&idle_count);for(unsigned i=0;i<n;i++)assert(out[i]==i%251);
}
static void test_jobs_headers(void)
{
    prepare();ready();h1_esp32_stream s[5]={{0}};h1_esp32_request q={"https://example.com/",0,0,0,0,0,0};
    for(unsigned i=0;i<4;i++)assert(!h1_esp32_http_open(&esp,s+i,&q));
    assert(h1_esp32_http_open(&esp,s+4,&q)==H1NET_REMOTE&&esp.error.remote_status==H1NET_BUSY);
    char fields_out[128];assert(!h1_esp32_http_headers(&esp,s,fields_out,sizeof fields_out));
    assert(strstr(fields_out,"first=fixture")&&strstr(fields_out,"second=fixture"));
    memset(fields_out,0x55,sizeof fields_out);assert(h1_esp32_http_headers(&esp,s,fields_out,4)==H1_ESP32_LIMIT);
    assert(!fields_out[0]&&!fields_out[3]);
    for(unsigned i=0;i<4;i++)assert(!h1_esp32_http_close(&esp,s+i));assert(releases==4);
}
static void test_request_range(void)
{
    prepare();ready();char url[2049],body[16384];memset(url,'a',sizeof url);memcpy(url,"https://example.com/",20);url[2048]=0;
    memset(body,'x',sizeof body);h1_esp32_request q={url,"POST","X-Fixture: yes",body,sizeof body,0,0};h1_esp32_stream s={0};
    assert(!h1_esp32_http_open(&esp,&s,&q));assert(!strcmp(last_method,"POST")&&posted==sizeof body);
    for(unsigned i=0;i<sizeof server.request_data;i++)assert(!server.request_data[i]);
    assert(!h1_esp32_http_close(&esp,&s));
    h1net_range range={100,32,1000,30000,0};assert(!h1_esp32_http_range(&esp,&s,url,&range));
    uint8_t out[32];assert(h1_esp32_http_read(&esp,&s,out,32)==32);assert(!h1_esp32_http_read(&esp,&s,out,32));
    assert(s.info.http_status==206);assert(!h1_esp32_http_close(&esp,&s));
}
static void test_stale(void)
{
    prepare();ready();h1_esp32_stream s={0};h1_esp32_request q={"https://example.com/",0,0,0,0,0,0};
    assert(!h1_esp32_http_open(&esp,&s,&q));usb_epoch++;uint8_t out[4];
    assert(h1_esp32_http_read(&esp,&s,out,4)==H1_ESP32_STALE);
    ready();unsigned before=calls[11];assert(h1_esp32_http_close(&esp,&s)==H1_ESP32_STALE);
    assert(!s.job&&calls[11]==before); /* old numeric job must never release a new job */
}
static void test_errors(void)
{
    prepare();ready();uint8_t out[1400];unsigned n;http_status=403;
    assert(h1_esp32_get(&esp,"https://example.com/",out,sizeof out,&n)==H1_ESP32_HTTP);
    assert(esp.error.http_status==403&&releases==1);
    http_status=200;failed=1;
    assert(h1_esp32_get(&esp,"https://example.com/",out,sizeof out,&n)==H1NET_REMOTE);
    assert(esp.error.backend_error==H1NET_ERROR_TIMEOUT&&esp.error.remote_status==0&&releases==2);
    failed=0;bad_length=1;
    assert(h1_esp32_get(&esp,"https://example.com/",out,sizeof out,&n)==H1NET_PROTOCOL);
}
static void test_reinit_handle(void)
{
    prepare();ready();h1_esp32_stream old={0},current={0};h1_esp32_request q={"https://example.com/",0,0,0,0,0,0};
    assert(!h1_esp32_http_open(&esp,&old,&q));prepare();ready();
    assert(!h1_esp32_http_open(&esp,&current,&q));assert(old.job==current.job);
    assert(h1_esp32_http_close(&esp,&old)==H1_ESP32_STALE);
    assert(jobs[0].used&&releases==0);assert(!h1_esp32_http_close(&esp,&current));
}
static void test_release_failure(void)
{
    prepare();ready();fail_release=1;uint8_t out[1400];unsigned n;
    assert(h1_esp32_get(&esp,"https://example.com/",out,sizeof out,&n)==H1NET_REMOTE);
    assert(esp.state==H1_ESP32_DEVICE_ERROR);fail_release=0;ready();
    for(unsigned i=0;i<4;i++)assert(!jobs[i].used);
}
static void test_timeout_no_replay(void)
{
    prepare();ready();silent=1;h1_esp32_stream s={0};h1_esp32_request q={"https://example.com/","POST",0,"fixture",7,0,0};
    assert(h1_esp32_http_open(&esp,&s,&q)==H1NET_TIMEOUT);
    assert(calls[H1NET_HTTP_REQUEST]==1);silent=0;ready();assert(calls[H1NET_HTTP_REQUEST]==1);
}
static void test_crc_and_caps(void)
{
    prepare();corrupt=1;for(unsigned i=0;i<100;i++)h1_esp32_poll(&esp);
    assert(esp.state==H1_ESP32_DEVICE_ERROR&&esp.error.code==H1NET_PROTOCOL);
    corrupt=0;clock_value+=3001;ready();
    prepare();features=0;ready();h1_esp32_stream s={0};h1_esp32_request q={"https://example.com/",0,0,0,0,0,0};
    assert(h1_esp32_http_open(&esp,&s,&q)==H1_ESP32_UNSUPPORTED);
}
static void test_wifi(void)
{
    prepare();wifi=0;ready();uint32_t id;assert(!h1_esp32_wifi_scan(&esp,&id)&&id==7);
    h1wifi_ap ap[12];unsigned count=12,total=0;
    assert(!h1_esp32_wifi_page(&esp,id,0,ap,&count,&total));assert(count==1&&total==1&&!strcmp(ap[0].ssid,"fixture-ap"));
    assert(!h1_esp32_wifi_connect(&esp,"fixture-ap","fixture-pass",3,0,&id)&&id==9);
    assert(h1_esp32_wifi_connect(&esp,"fixture-ap","short",3,0,&id)==H1_ESP32_ARGUMENT);
    assert(!h1_esp32_wifi_connect(&esp,"fixture-ap","",3,1,&id));
    assert(!h1_esp32_wifi_cancel(&esp,id));assert(!h1_esp32_wifi_forget(&esp,&id));
    h1wifi_status s;assert(!h1_esp32_wifi_status(&esp,&s)&&!s.connected);
}
int main(void)
{
    test_detection();test_download();test_jobs_headers();test_request_range();test_stale();
    test_errors();test_timeout_no_replay();test_crc_and_caps();test_wifi();
    test_reinit_handle();test_release_failure();
    puts("PASS: 11 ESP32 integration scenarios (real protocol/facade; simulated peripheral)");return 0;
}
