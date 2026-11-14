/* SPDX-License-Identifier: Apache-2.0
 * H1 ESP32 contributors. SDK import with copyright-holder authorization. */
#include "h1net_platform.h"
#include "h1net_gzip.h"
#include "h1net_audio.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#define RING_SIZE 8192u
typedef struct {
    h1net_info info;
    uint8_t ring[RING_SIZE];
    unsigned head, tail, count;
    char url[H1NET_RANGE_URL+1];
    char final_url[H1NET_RANGE_URL+1],content_type[97],referer[H1NET_MAX_URL+1];
    h1net_range range;
    bool ranged;
    uint32_t maximum, timeout_ms;
    bool worker_running, cancel, retired, background;
    bool request,post;
    /* Private to this worker, never points into the USB server's staging. */
    char *request_headers;
    uint8_t *request_body;
    unsigned request_length,request_headers_length;
    char *response_headers;
    unsigned response_headers_length;
    bool headers_ready;
} http_job;
static http_job jobs[H1NET_MAX_JOBS];
static SemaphoreHandle_t job_lock;
static uint32_t next_job;
static bool multiplex;
static bool audio_ready;
static void purge(http_job *job)
{
    if(job->response_headers){h1net_secure_clear(job->response_headers,job->response_headers_length);free(job->response_headers);}
    memset(job,0,sizeof *job);
}
static int compute(void *unused,uint8_t op,const uint8_t *p,unsigned n,uint16_t *status,uint8_t *out,unsigned *used)
{(void)unused;return h1net_audio_dispatch(op,p,n,status,out,used);}
static void compute_reset(void *unused){(void)unused;h1net_audio_close();}
typedef struct {
    bool have_length, have_encoding, have_range, gzip, bad;
    uint32_t content_length;
    char type[97];
    char content_range[96];
    http_job *stream_job;
    http_job *header_job;
    int64_t *stream_deadline;
    uint32_t stream_error;
    uint64_t stream_wire_bytes;
} response_meta;

static void take(void){xSemaphoreTake(job_lock,portMAX_DELAY);}
static void give(void){xSemaphoreGive(job_lock);}
static uint32_t interrupted(http_job *job,int64_t deadline)
{
    take();bool cancelled=job->cancel;give();
    if(cancelled)return H1NET_ERROR_CANCELLED;
    if(!h1net_wifi_connected())return H1NET_ERROR_WIFI;
    if(esp_timer_get_time()>=deadline)return H1NET_ERROR_TIMEOUT;
    return H1NET_ERROR_NONE;
}
static esp_err_t http_event(esp_http_client_event_t *event)
{
    response_meta *meta=event->user_data;
    /* esp_http_client_read loops until its requested buffer fills. ON_DATA is
     * dispatched by the parser on each transport read, before that loop waits
     * again. Publish here, including body coalesced with fetch_headers, so a
     * short SSE event reaches USB immediately. The read-loop copy is discarded
     * for request jobs; these bytes must never be queued a second time. */
    if(event->event_id==HTTP_EVENT_ON_DATA && meta->stream_job){
        http_job *job=meta->stream_job;unsigned at=0;
        if(meta->stream_error)return ESP_OK;
        if(meta->bad || meta->gzip){meta->stream_error=H1NET_ERROR_HTTP_PROTOCOL;return ESP_OK;}
        if(meta->have_length && meta->content_length>job->maximum){meta->stream_error=H1NET_ERROR_LIMIT;return ESP_OK;}
        while(at<(unsigned)event->data_len){
            uint32_t error=interrupted(job,*meta->stream_deadline);
            if(error){meta->stream_error=error;return ESP_OK;}
            take();
            if(job->cancel){give();meta->stream_error=H1NET_ERROR_CANCELLED;return ESP_OK;}
            unsigned count=(unsigned)event->data_len-at,space=RING_SIZE-job->count;
            if(count>space)count=space;
            if(count>job->maximum-job->info.received)count=job->maximum-job->info.received;
            if(!count){
                bool limit=job->info.received==job->maximum;give();
                if(limit){meta->stream_error=H1NET_ERROR_LIMIT;return ESP_OK;}
                vTaskDelay(pdMS_TO_TICKS(10));continue;
            }
            for(unsigned i=0;i<count;++i){job->ring[job->tail]=((const uint8_t *)event->data)[at+i];job->tail=(job->tail+1u)%RING_SIZE;}
            job->count+=count;job->info.received+=count;job->info.buffered=job->count;
            job->info.http_status=(uint32_t)esp_http_client_get_status_code(event->client);
            job->info.content_length=meta->have_length?meta->content_length:0xffffffffu;
            job->info.state=H1NET_RECEIVING;give();
            at+=count;meta->stream_wire_bytes+=count;
            *meta->stream_deadline=esp_timer_get_time()+(int64_t)job->timeout_ms*1000;
        }
        return ESP_OK;
    }
    if(event->event_id!=HTTP_EVENT_ON_HEADER)return ESP_OK;
    if(meta->header_job){
        const char *name=event->header_key,*value=event->header_value;
        unsigned a=(unsigned)strlen(name),b=(unsigned)strlen(value);bool valid=a!=0;
        for(unsigned i=0;i<a;i++)if((unsigned char)name[i]<=32||(unsigned char)name[i]>=127||name[i]==':')valid=false;
        for(unsigned i=0;i<b;i++)if(((unsigned char)value[i]<32&&value[i]!='\t')||value[i]==127)valid=false;
        take();http_job *job=meta->header_job;unsigned used=job->response_headers_length;
        if(!valid||a+b+4>8192-used)meta->bad=true;
        else {char *copy=realloc(job->response_headers,used+a+b+5);
            if(!copy)meta->bad=true;
            else {job->response_headers=copy;memcpy(copy+used,name,a);used+=a;copy[used++]=':';copy[used++]=' ';
                memcpy(copy+used,value,b);used+=b;copy[used++]='\r';copy[used++]='\n';copy[used]=0;job->response_headers_length=used;}}
        give();
    }
    if(!strcasecmp(event->header_key,"Content-Length")){
        char *end=NULL;errno=0;
        unsigned long long value=strtoull(event->header_value,&end,10);
        if(errno || end==event->header_value || *end || value>UINT32_MAX ||
           (meta->have_length && meta->content_length!=(uint32_t)value))meta->bad=true;
        else {meta->have_length=true;meta->content_length=(uint32_t)value;}
    }
    if(!strcasecmp(event->header_key,"Content-Encoding")){
        /* Duplicate/stacked encodings are not a single gzip representation. */
        const char *value=event->header_value;size_t n=strlen(value);
        while(n && (*value==' ' || *value=='\t')){++value;--n;}
        while(n && (value[n-1]==' ' || value[n-1]=='\t'))--n;
        bool gzip=(n==4 && !strncasecmp(value,"gzip",n)) ||
                  (n==6 && !strncasecmp(value,"x-gzip",n));
        if(meta->have_encoding || (!gzip && !(n==8 && !strncasecmp(value,"identity",n))))meta->bad=true;
        meta->have_encoding=true;meta->gzip=gzip;
    }
    if(!strcasecmp(event->header_key,"Content-Type")){
        size_t n=strlen(event->header_value);if(n>96)meta->bad=true;
        else memcpy(meta->type,event->header_value,n+1);
    }
    if(!strcasecmp(event->header_key,"Content-Range")){
        size_t n=strlen(event->header_value);
        if(meta->have_range || n>=sizeof meta->content_range)meta->bad=true;
        else {memcpy(meta->content_range,event->header_value,n+1);meta->have_range=true;}
    }
    return ESP_OK;
}
static void worker(void *unused)
{
    http_job *job=unused;
    uint8_t block[512],encoded[512];response_meta meta={0};uint32_t error=0;
    h1net_gzip gzip={0};size_t input_at=0,input_size=0;uint64_t wire_bytes=0;
    bool wire_eof=false;
    int64_t deadline=esp_timer_get_time()+(int64_t)job->timeout_ms*1000;
    esp_http_client_config_t config={
        .url=job->url,.timeout_ms=job->timeout_ms<5000?job->timeout_ms:5000,
        .disable_auto_redirect=true,.max_redirection_count=3,
        .crt_bundle_attach=esp_crt_bundle_attach,.event_handler=http_event,.user_data=&meta,
        .buffer_size=1024,.buffer_size_tx=4096,
    };
    esp_http_client_handle_t http=esp_http_client_init(&config);
    if(!http){error=H1NET_ERROR_NETWORK;goto finished;}
    esp_http_client_set_header(http,"Accept-Encoding",job->ranged || job->request?"identity":"gzip, identity");
    esp_http_client_set_header(http,"User-Agent","H1NET/1 ESP32");
    if(job->request){
        if(esp_http_client_set_method(http,job->post?HTTP_METHOD_POST:HTTP_METHOD_GET)!=ESP_OK){error=H1NET_ERROR_NETWORK;goto cleanup;}
        /* The private copy can be split in-place; set_header copies each field. */
        char *line=job->request_headers;
        while(line && *line){
            char *next=strstr(line,"\r\n"),*colon=strchr(line,':');
            if(next){*next=0;next+=2;}
            if(!colon){error=H1NET_ERROR_HTTP_PROTOCOL;goto cleanup;}
            *colon++=0;while(*colon==' ')++colon;
            if(esp_http_client_set_header(http,line,colon)!=ESP_OK){error=H1NET_ERROR_NETWORK;goto cleanup;}
            line=next;
        }
    }
    if(job->ranged){
        char value[64];snprintf(value,sizeof value,"bytes=%lu-%lu",(unsigned long)job->range.offset,(unsigned long)(job->range.offset+job->range.length-1));
        esp_http_client_set_header(http,"Range",value);
        if(job->referer[0])esp_http_client_set_header(http,"Referer",job->referer);
    }
    for(unsigned redirects=0;;++redirects){
        if((error=interrupted(job,deadline)))goto cleanup;
        char active_url[H1NET_RANGE_URL+1];
        if(esp_http_client_get_url(http,active_url,sizeof active_url)!=ESP_OK){error=H1NET_ERROR_HTTP_PROTOCOL;goto cleanup;}
        if(!strncmp(active_url,"https://",8)){
            while(time(NULL)<1704067200){
                if((error=interrupted(job,deadline)))goto cleanup;
                vTaskDelay(pdMS_TO_TICKS(100));
            }
        }
        meta=(response_meta){.stream_job=job->request?job:NULL,.header_job=job->request?job:NULL,.stream_deadline=&deadline};
        if(esp_http_client_open(http,(int)job->request_length)!=ESP_OK){
            error=interrupted(job,deadline);if(!error)error=H1NET_ERROR_NETWORK;goto cleanup;
        }
        for(unsigned sent=0;sent<job->request_length;){
            if((error=interrupted(job,deadline)))goto cleanup;
            int n=esp_http_client_write(http,(const char *)job->request_body+sent,(int)(job->request_length-sent));
            if(n<=0){error=H1NET_ERROR_NETWORK;goto cleanup;}sent+=(unsigned)n;
            deadline=esp_timer_get_time()+(int64_t)job->timeout_ms*1000;
        }
        int64_t header_result=esp_http_client_fetch_headers(http);
        if(header_result<0){error=interrupted(job,deadline);if(!error)error=H1NET_ERROR_NETWORK;goto cleanup;}
        int status=esp_http_client_get_status_code(http);
        take();job->headers_ready=true;give();
        if(!job->request && (status==301 || status==302 || status==303 || status==307 || status==308)){
            if(redirects>=3){error=H1NET_ERROR_HTTP_PROTOCOL;goto cleanup;}
            char before[H1NET_RANGE_URL+1],after[H1NET_RANGE_URL+1];
            if(esp_http_client_get_url(http,before,sizeof before)!=ESP_OK ||
               esp_http_client_set_redirection(http)!=ESP_OK ||
               esp_http_client_get_url(http,after,sizeof after)!=ESP_OK ||
               !(job->ranged?h1net_valid_long_url((const uint8_t *)after,strlen(after)):h1net_valid_url((const uint8_t *)after,strlen(after))) ||
               (!strncmp(before,"https://",8) && strncmp(after,"https://",8))){
                error=H1NET_ERROR_HTTP_PROTOCOL;goto cleanup;
            }
            esp_http_client_close(http);continue;
        }
        take();
        job->info.http_status=(uint32_t)status;
        job->info.content_length=meta.have_length && !meta.gzip?meta.content_length:0xffffffffu;
        memcpy(job->final_url,active_url,strlen(active_url)+1);
        memcpy(job->content_type,meta.type,sizeof job->content_type);
        job->info.state=H1NET_RECEIVING;give();
        if(meta.stream_error){error=meta.stream_error;goto cleanup;}
        if(meta.bad || (job->request && meta.gzip)){error=H1NET_ERROR_HTTP_PROTOCOL;goto cleanup;}
        if(job->ranged){
            char expected[96];snprintf(expected,sizeof expected,"bytes %lu-%lu/%lu",(unsigned long)job->range.offset,
                (unsigned long)(job->range.offset+job->range.length-1),(unsigned long)job->range.total);
            if(status!=206 || meta.gzip || !meta.have_range || strcmp(meta.content_range,expected) ||
               (meta.have_length && meta.content_length!=job->range.length)){error=H1NET_ERROR_HTTP_PROTOCOL;goto cleanup;}
        }
        if(!meta.gzip && meta.have_length && meta.content_length>job->maximum){error=H1NET_ERROR_LIMIT;goto cleanup;}
        break;
    }
    for(;;){
        if((error=interrupted(job,deadline)))break;
        if(job->request){
            int n=esp_http_client_read(http,(char *)block,sizeof block);
            if(meta.stream_error){error=meta.stream_error;break;}
            if(n<0){if(n==-ESP_ERR_HTTP_EAGAIN)continue;error=H1NET_ERROR_NETWORK;break;}
            if(esp_http_client_is_complete_data_received(http)){wire_bytes=meta.stream_wire_bytes;break;}
            if(!n)vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        take();unsigned space=RING_SIZE-job->count;uint32_t received=job->info.received;give();
        if(!space){vTaskDelay(pdMS_TO_TICKS(10));continue;}
        unsigned wanted=space<sizeof block?space:sizeof block;
        /* At the limit read one extra byte to distinguish exact-length EOF from overflow. */
        if(wanted>job->maximum-received+1u)wanted=job->maximum-received+1u;
        int n;
        if(meta.gzip){
            size_t consumed=input_size-input_at,produced=wanted;
            int decoded=h1net_gzip_step(&gzip,encoded+input_at,&consumed,block,&produced,wire_eof);
            input_at+=consumed;
            if(decoded<0){error=decoded==-2?H1NET_ERROR_NETWORK:H1NET_ERROR_HTTP_PROTOCOL;break;}
            n=(int)produced;
            if(!n && decoded==1)break;
            if(!n){
                if(input_at<input_size)continue;
                int raw=esp_http_client_read(http,(char *)encoded,sizeof encoded);
                if(raw<0){
                    if(raw==-ESP_ERR_HTTP_EAGAIN)continue;
                    error=H1NET_ERROR_NETWORK;break;
                }
                input_at=0;input_size=(size_t)raw;wire_bytes+=(unsigned)raw;
                if(raw>0)deadline=esp_timer_get_time()+(int64_t)job->timeout_ms*1000;
                if(!raw){
                    wire_eof=esp_http_client_is_complete_data_received(http);
                    if(!wire_eof)vTaskDelay(pdMS_TO_TICKS(10));
                }
                continue;
            }
        }else n=esp_http_client_read(http,(char *)block,(int)wanted);
        if(n<0){
            if(n==-ESP_ERR_HTTP_EAGAIN)continue;
            error=H1NET_ERROR_NETWORK;break;
        }
        if(!n){
            if(esp_http_client_is_complete_data_received(http))break;
            vTaskDelay(pdMS_TO_TICKS(10));continue;
        }
        if((uint32_t)n>job->maximum-received){error=H1NET_ERROR_LIMIT;break;}
        if(!meta.gzip)wire_bytes+=(unsigned)n;
        take();
        for(int i=0;i<n;++i){job->ring[job->tail]=block[i];job->tail=(job->tail+1u)%RING_SIZE;}
        job->count+=(unsigned)n;job->info.received+=(uint32_t)n;job->info.buffered=job->count;
        give();
        /* Reset on progress so regular USB draining does not exhaust a total
         * wall-clock deadline; cancellation and stalled transfers stay bounded. */
        deadline=esp_timer_get_time()+(int64_t)job->timeout_ms*1000;
    }
    if(!error){
        take();
        if(meta.have_length && wire_bytes!=meta.content_length)
            error=H1NET_ERROR_HTTP_PROTOCOL;
        if(!error && meta.gzip)job->info.content_length=job->info.received;
        give();
    }
cleanup:
    h1net_gzip_close(&gzip);
    if(!error)error=interrupted(job,deadline);
    esp_http_client_close(http);esp_http_client_cleanup(http);
finished:
    if(job->request_headers){h1net_secure_clear(job->request_headers,job->request_headers_length+1u);free(job->request_headers);job->request_headers=NULL;}
    if(job->request_body){h1net_secure_clear(job->request_body,job->request_length);free(job->request_body);job->request_body=NULL;}
    take();
    if(job->cancel)error=H1NET_ERROR_CANCELLED;
    job->info.error=error;
    job->info.state=error?(error==H1NET_ERROR_CANCELLED?H1NET_CANCELLED:H1NET_FAILED):H1NET_DONE;
    job->worker_running=false;if(job->retired)purge(job);give();vTaskDelete(NULL);
}
static http_job *find(uint32_t id)
{
    for(unsigned i=0;i<H1NET_MAX_JOBS;i++)if(id && jobs[i].info.job==id && !jobs[i].retired)return &jobs[i];
    return NULL;
}
static bool allocated(uint32_t id)
{
    for(unsigned i=0;i<H1NET_MAX_JOBS;i++)if(jobs[i].info.job==id)return true;
    return false;
}
static uint16_t reset(void *unused)
{
    (void)unused;take();bool busy=false;
    for(unsigned i=0;i<H1NET_MAX_JOBS;i++)if(jobs[i].worker_running){jobs[i].cancel=true;busy=true;}
    if(busy){give();return H1NET_BUSY;}
    for(unsigned i=0;i<H1NET_MAX_JOBS;i++)purge(&jobs[i]);
    multiplex=false;give();return H1NET_OK;
}
static void status(void *unused,uint32_t out[6])
{(void)unused;h1net_wifi_status(out);}
static void capabilities(void *unused,uint32_t out[4])
{
    (void)unused;take();multiplex=true;give();
    out[0]=1;out[1]=H1NET_MAX_JOBS;out[2]=RING_SIZE;out[3]=H1NET_CAP_RELEASE|H1NET_CAP_BACKGROUND|H1NET_CAP_HTTP_REQUEST;
    out[3]|=H1NET_CAP_HTTP_HEADERS;
}
static uint16_t open_internal(const char *url,uint32_t maximum,uint32_t timeout,const h1net_range *range,uint32_t *id,bool background,
                              const char *method,const char *headers,const void *body,unsigned body_length)
{
    if(!h1net_wifi_connected())return H1NET_NOT_CONNECTED;
    take();http_job *job=NULL;unsigned backgrounds=0;
    for(unsigned i=0;i<H1NET_MAX_JOBS;i++){
        if(jobs[i].retired && !jobs[i].worker_running)purge(&jobs[i]);
        if(jobs[i].info.job && jobs[i].background)++backgrounds;
    }
    if(!multiplex){
        if(background){give();return H1NET_INVALID;}
        job=&jobs[0];if(job->worker_running || job->count){give();return H1NET_BUSY;}
    }else{
        if(background && backgrounds>=H1NET_MAX_JOBS-1){give();return H1NET_BUSY;}
        for(unsigned i=0;i<H1NET_MAX_JOBS;i++)if(!jobs[i].info.job){job=&jobs[i];break;}
        if(!job){give();return H1NET_BUSY;}
    }
    do{if(!++next_job)++next_job;}while(allocated(next_job));
    purge(job);
    job->info=(h1net_info){.job=next_job,.state=H1NET_CONNECTING,.content_length=0xffffffffu};
    job->worker_running=true;job->background=background;
    job->maximum=maximum;job->timeout_ms=timeout;memcpy(job->url,url,strlen(url)+1);*id=next_job;
    job->ranged=range!=NULL;
    if(range){job->range=*range;if(range->referer)memcpy(job->referer,range->referer,strlen(range->referer)+1);job->range.referer=job->referer;}
    if(method){
        job->request=true;job->post=!strcmp(method,"POST");job->request_length=body_length;
        job->request_headers_length=(unsigned)strlen(headers);
        job->request_headers=malloc(job->request_headers_length+1u);
        if(body_length)job->request_body=malloc(body_length);
        if(!job->request_headers || (body_length && !job->request_body)){
            free(job->request_headers);free(job->request_body);memset(job,0,sizeof *job);give();return H1NET_INTERNAL;
        }
        memcpy(job->request_headers,headers,strlen(headers)+1);
        if(body_length)memcpy(job->request_body,body,body_length);
    }
    if(xTaskCreate(worker,"h1_http",16384,job,4,NULL)!=pdPASS){
        if(job->request_headers){h1net_secure_clear(job->request_headers,strlen(headers)+1);free(job->request_headers);}
        if(job->request_body){h1net_secure_clear(job->request_body,body_length);free(job->request_body);}
        memset(job,0,sizeof *job);give();return H1NET_INTERNAL;
    }
    give();return H1NET_OK;
}
static uint16_t open_job(void *p,const char *url,uint32_t maximum,uint32_t timeout,uint32_t *id)
{(void)p;return open_internal(url,maximum,timeout,NULL,id,false,NULL,NULL,NULL,0);}
static uint16_t open_background(void *p,const char *url,uint32_t maximum,uint32_t timeout,uint32_t *id)
{(void)p;return open_internal(url,maximum,timeout,NULL,id,true,NULL,NULL,NULL,0);}
static uint16_t open_range(void *p,const char *url,const h1net_range *range,uint32_t *id)
{(void)p;return open_internal(url,range->length,range->timeout_ms,range,id,false,NULL,NULL,NULL,0);}
static uint16_t open_request(void *p,const char *url,const char *method,const char *headers,const void *body,unsigned body_length,
                             uint32_t maximum,uint32_t timeout,uint32_t *id)
{(void)p;return open_internal(url,maximum,timeout,NULL,id,false,method,headers,body,body_length);}

static uint16_t info(void *unused,uint32_t id,h1net_info *out)
{
    (void)unused;take();http_job *job=find(id);
    if(!job){give();return H1NET_BAD_JOB;}*out=job->info;give();return H1NET_OK;
}
static uint16_t read_body(void *unused,uint32_t id,unsigned wanted,uint8_t *out,uint32_t *offset,unsigned *count)
{
    (void)unused;take();http_job *job=find(id);if(!job){give();return H1NET_BAD_JOB;}
    *offset=job->info.delivered;*count=wanted<job->count?wanted:job->count;
    for(unsigned i=0;i<*count;i++){out[i]=job->ring[job->head];job->head=(job->head+1u)%RING_SIZE;}
    job->count-=*count;job->info.delivered+=*count;job->info.buffered=job->count;give();return H1NET_OK;
}
static uint16_t cancel_job(void *unused,uint32_t id)
{
    (void)unused;take();http_job *job=find(id);if(!job){give();return H1NET_BAD_JOB;}
    job->cancel=true;
    if(!job->worker_running){job->info.state=H1NET_CANCELLED;job->info.error=H1NET_ERROR_CANCELLED;}
    give();return H1NET_OK;
}
static uint16_t release_job(void *unused,uint32_t id)
{
    (void)unused;take();http_job *job=find(id);if(!job){give();return H1NET_BAD_JOB;}
    job->cancel=job->retired=true;job->head=job->tail=job->count=0;
    job->info.delivered=job->info.received;job->info.buffered=0;
    if(!job->worker_running)purge(job);
    give();return H1NET_OK;
}
void h1net_http_init(void)
{job_lock=xSemaphoreCreateMutex();assert(job_lock);audio_ready=h1net_audio_init();}
static uint16_t metadata(void *unused,uint32_t id,char url[H1NET_MAX_URL+1],char type[97])
{
    (void)unused;take();http_job *job=find(id);if(!job){give();return H1NET_BAD_JOB;}
    if(!job->final_url[0]){give();return H1NET_BUSY;}
    if(strlen(job->final_url)>H1NET_MAX_URL){give();return H1NET_INVALID;}
    memcpy(url,job->final_url,strlen(job->final_url)+1);memcpy(type,job->content_type,sizeof job->content_type);give();return H1NET_OK;
}
unsigned h1net_http_get_diagnostics(h1net_info out[H1NET_MAX_JOBS])
{
    unsigned count=0;take();
    for(unsigned i=0;i<H1NET_MAX_JOBS;i++)if(jobs[i].info.job)out[count++]=jobs[i].info;
    give();return count;
}
static uint16_t response_headers(void *unused,uint32_t id,uint32_t offset,unsigned wanted,uint8_t *out,unsigned *count,unsigned *total)
{
    (void)unused;take();http_job *job=find(id);*count=*total=0;
    if(!job){give();return H1NET_BAD_JOB;}
    if(!job->request){give();return H1NET_INVALID;}
    if(!job->headers_ready){give();return H1NET_BUSY;}
    *total=job->response_headers_length;
    if(offset>*total){give();return H1NET_INVALID;}
    *count=*total-offset;if(*count>wanted)*count=wanted;
    if(*count)memcpy(out,job->response_headers+offset,*count);
    give();return H1NET_OK;
}
void h1net_http_backend(h1net_backend *out)
{*out=(h1net_backend){NULL,reset,status,open_job,info,read_body,cancel_job,metadata,open_range,capabilities,release_job,open_background,audio_ready?compute:NULL,audio_ready?compute_reset:NULL,open_request,h1net_wifi_control,response_headers};}
void h1net_http_disconnected(void)
{
    h1net_audio_close();
    take();for(unsigned i=0;i<H1NET_MAX_JOBS;i++){jobs[i].cancel=true;jobs[i].retired=true;}give();
}
