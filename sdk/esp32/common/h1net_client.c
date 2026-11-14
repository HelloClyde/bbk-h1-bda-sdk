/* SPDX-License-Identifier: Apache-2.0
 * H1 ESP32 contributors. SDK import with copyright-holder authorization. */
#include "h1net_client.h"

static void received_frame(void *context,const h1net_frame *frame)
{
    h1net_client *c=(h1net_client *)context;
    if(c->response_ready || frame->opcode!=(uint8_t)(c->expected_opcode|0x80u) ||
       frame->sequence!=c->expected_sequence){c->response_invalid=1;return;}
    c->response_length=frame->length;c->remote_status=frame->status;
    for(unsigned i=0;i<frame->length;++i)c->response[i]=frame->payload[i];
    c->response_ready=1;
}
void h1net_client_init(h1net_client *c,const h1net_io *io,uint32_t timeout)
{
    c->io=*io;c->sequence=c->expected_sequence=c->remote_status=0;
    c->expected_opcode=0;c->response_length=0;
    c->ready=c->broken=c->response_ready=c->response_invalid=c->pending_rpc=0;
    c->rpc_timeout_ms=timeout?timeout:4000u;h1net_parser_init(&c->parser);
    c->max_jobs=1;c->features=0;
}
static int fail(h1net_client *c,int error)
{ c->broken=1;c->ready=0;return error; }
static int expired(h1net_client *c,uint32_t start)
{ return (uint32_t)(c->io.now_ms(c->io.context)-start)>=c->rpc_timeout_ms; }
int h1net_rpc_begin(h1net_client *c,h1net_rpc_task *t,uint8_t op,const void *payload,unsigned length)
{
    if(c->broken)return H1NET_IO;
    if(c->pending_rpc || !op || op>=128 || length>H1NET_MAX_PAYLOAD || (length&&!payload))return H1NET_PROTOCOL;
    if(!c->io.write || !c->io.read || !c->io.now_ms)return fail(c,H1NET_IO);
    uint16_t seq=(uint16_t)(c->sequence+1u);
    t->size=(unsigned)h1net_encode(t->packet,sizeof t->packet,op,seq,0,payload,length);
    if(!t->size)return H1NET_PROTOCOL;
    c->sequence=c->expected_sequence=seq;c->expected_opcode=op;
    c->response_ready=c->response_invalid=0;c->pending_rpc=1;
    t->offset=0;t->phase=1;t->start=c->io.now_ms(c->io.context);
    t->bad_headers=c->parser.bad_headers;t->bad_crc=c->parser.bad_crc;return 0;
}
int h1net_rpc_poll(h1net_client *c,h1net_rpc_task *t)
{
    int error=0;
    if(!c->pending_rpc || !t->phase)return H1NET_PROTOCOL;
    if(c->broken){error=H1NET_IO;goto done;}
    if(expired(c,t->start)){error=H1NET_TIMEOUT;goto done;}
    if(t->phase==1){
        int n=c->io.write(c->io.context,t->packet+t->offset,t->size-t->offset);
        if(n<0 || (unsigned)n>t->size-t->offset){error=H1NET_IO;goto done;}
        t->offset+=(unsigned)n;if(t->offset<t->size)return 0;t->phase=2;
    }
    if(t->phase==2){
        int n=c->io.finish_write?c->io.finish_write(c->io.context):1;
        if(n<0){error=H1NET_IO;goto done;}if(!n)return 0;t->phase=3;
    }
    {uint8_t data[64];int n=c->io.read(c->io.context,data,sizeof data);
     if(n<0 || (unsigned)n>sizeof data){error=H1NET_IO;goto done;}
     if(n)h1net_parser_feed(&c->parser,data,(unsigned)n,received_frame,c);}
    if(c->response_invalid || c->parser.bad_headers!=t->bad_headers || c->parser.bad_crc!=t->bad_crc){error=H1NET_PROTOCOL;goto done;}
    if(!c->response_ready)return 0;
    t->phase=0;c->pending_rpc=0;return c->remote_status?H1NET_REMOTE:1;
done:t->phase=0;c->pending_rpc=0;return fail(c,error);
}
int h1net_rpc(h1net_client *c,uint8_t op,const void *payload,unsigned length,
             void *response,unsigned capacity,unsigned *received)
{
    uint8_t packet[H1NET_MAX_FRAME],data[64];
    if(c->broken)return H1NET_IO;
    if(c->pending_rpc)return H1NET_PROTOCOL;
    if(!op || op>=128 || length>H1NET_MAX_PAYLOAD || (capacity && !response))return H1NET_PROTOCOL;
    if(!c->io.write || !c->io.read || !c->io.idle || !c->io.now_ms)return fail(c,H1NET_IO);
    uint16_t seq=(uint16_t)(c->sequence+1u);c->sequence=seq;
    size_t size=h1net_encode(packet,sizeof packet,op,seq,0,payload,length);
    if(!size)return H1NET_PROTOCOL;
    c->expected_opcode=op;c->expected_sequence=seq;
    c->response_ready=c->response_invalid=0;
    uint32_t start=c->io.now_ms(c->io.context),bad_headers=c->parser.bad_headers,bad_crc=c->parser.bad_crc;
    unsigned offset=0;
    while(offset<size){
        if(expired(c,start))return fail(c,H1NET_TIMEOUT);
        int n=c->io.write(c->io.context,packet+offset,(unsigned)size-offset);
        if(n<0 || (unsigned)n>size-offset)return fail(c,H1NET_IO);
        offset+=(unsigned)n;if(!n)c->io.idle(c->io.context);
    }
    if(c->io.finish_write){
        for(;;){
            if(expired(c,start))return fail(c,H1NET_TIMEOUT);
            int result=c->io.finish_write(c->io.context);
            if(result<0)return fail(c,H1NET_IO);
            if(result)break;
            c->io.idle(c->io.context);
        }
    }
    while(!c->response_ready){
        if(expired(c,start))return fail(c,H1NET_TIMEOUT);
        int n=c->io.read(c->io.context,data,sizeof data);
        if(n<0 || (unsigned)n>sizeof data)return fail(c,H1NET_IO);
        if(n)h1net_parser_feed(&c->parser,data,(unsigned)n,received_frame,c);
        if(c->response_invalid || c->parser.bad_headers!=bad_headers || c->parser.bad_crc!=bad_crc)
            return fail(c,H1NET_PROTOCOL);
        if(!n)c->io.idle(c->io.context);
    }
    if(c->response_length>capacity)return fail(c,H1NET_PROTOCOL);
    if(c->remote_status)return H1NET_REMOTE;
    for(unsigned i=0;i<c->response_length;++i)((uint8_t *)response)[i]=c->response[i];
    if(received)*received=c->response_length;
    return 0;
}
int h1net_hello(h1net_client *c)
{
    static const char identity[]=H1NET_IDENTITY;
    uint8_t response[64];unsigned n=0;
    int error=h1net_rpc(c,H1NET_HELLO,0,0,response,sizeof response,&n);
    if(error)return error;
    if(n!=sizeof identity-1)return fail(c,H1NET_PROTOCOL);
    for(unsigned i=0;i<n;++i)if(response[i]!=(uint8_t)identity[i])return fail(c,H1NET_PROTOCOL);
    c->ready=1;c->max_jobs=1;c->features=0;return 0;
}
int h1net_get_status(h1net_client *c,uint32_t status[6])
{
    uint8_t response[24];unsigned n=0;
    int error=h1net_rpc(c,H1NET_STATUS,0,0,response,sizeof response,&n);
    if(error)return error;
    if(n!=sizeof response)return fail(c,H1NET_PROTOCOL);
    for(unsigned i=0;i<6;++i)status[i]=h1net_u32(response+i*4);
    if(status[0]!=1 || status[1]>1 || !status[5] || status[5]>H1NET_MAX_DOWNLOAD)
        return fail(c,H1NET_PROTOCOL);
    return 0;
}
int h1net_capabilities(h1net_client *c)
{
    uint8_t response[16];unsigned n=0;
    int error=h1net_rpc(c,H1NET_CAPABILITIES,0,0,response,sizeof response,&n);if(error)return error;
    if(n!=16 || h1net_u32(response)!=1 || !h1net_u32(response+4) || h1net_u32(response+4)>H1NET_MAX_JOBS ||
       !h1net_u32(response+8))return fail(c,H1NET_PROTOCOL);
    c->max_jobs=h1net_u32(response+4);c->features=h1net_u32(response+12);return 0;
}
int h1net_http_release(h1net_client *c,uint32_t job)
{
    uint8_t request[4];unsigned n=0;h1net_put32(request,job);
    int error=h1net_rpc(c,H1NET_HTTP_RELEASE,request,4,0,0,&n);
    return error?error:(n?fail(c,H1NET_PROTOCOL):0);
}
int h1net_http_open(h1net_client *c,const char *url,uint32_t maximum,uint32_t timeout,uint32_t *job)
{
    uint8_t request[8+H1NET_MAX_URL],response[4];unsigned n=0,length=0;
    if(!url || !job || !maximum || maximum>H1NET_MAX_DOWNLOAD || timeout<1000 || timeout>60000)
        return H1NET_PROTOCOL;
    while(length<=H1NET_MAX_URL && url[length])++length;
    if(!h1net_valid_url((const uint8_t *)url,length))return H1NET_PROTOCOL;
    h1net_put32(request,maximum);h1net_put32(request+4,timeout);
    for(unsigned i=0;i<length;++i)request[8+i]=(uint8_t)url[i];
    int error=h1net_rpc(c,H1NET_HTTP_OPEN,request,length+8,response,sizeof response,&n);
    if(error)return error;
    if(n!=4 || !h1net_u32(response))return fail(c,H1NET_PROTOCOL);
    *job=h1net_u32(response);return 0;
}
int h1net_http_info(h1net_client *c,uint32_t job,h1net_info *info)
{
    uint8_t request[4],response[32];unsigned n=0;
    h1net_put32(request,job);
    int error=h1net_rpc(c,H1NET_HTTP_INFO,request,4,response,sizeof response,&n);
    if(error)return error;
    if(n!=32)return fail(c,H1NET_PROTOCOL);
    info->job=h1net_u32(response);info->state=h1net_u32(response+4);
    info->http_status=h1net_u32(response+8);info->content_length=h1net_u32(response+12);
    info->received=h1net_u32(response+16);info->delivered=h1net_u32(response+20);
    info->buffered=h1net_u32(response+24);info->error=h1net_u32(response+28);
    if(info->job!=job || info->state>H1NET_CANCELLED || info->received<info->delivered ||
       info->buffered!=info->received-info->delivered || info->received>H1NET_MAX_DOWNLOAD)
        return fail(c,H1NET_PROTOCOL);
    return 0;
}
int h1net_http_request_open(h1net_client *c,const char *url,const char *method,const char *headers,
                          const void *body,unsigned body_length,uint32_t maximum,uint32_t timeout,uint32_t *job)
{
    uint8_t request[H1NET_MAX_PAYLOAD],response[4];unsigned n=0,u=0,h=0,method_id=0;
    if(job)*job=0;
    if(!job || !url || !method || !(c->features&H1NET_CAP_HTTP_REQUEST) ||
       body_length>H1NET_MAX_REQUEST_BODY || (body_length && !body) ||
       !maximum || maximum>H1NET_MAX_DOWNLOAD || timeout<1000 || timeout>60000)return H1NET_PROTOCOL;
    if(method[0]=='G' && method[1]=='E' && method[2]=='T' && !method[3])method_id=1;
    else if(method[0]=='P' && method[1]=='O' && method[2]=='S' && method[3]=='T' && !method[4])method_id=2;
    if(!method_id || (method_id==1 && body_length))return H1NET_PROTOCOL;
    while(u<=H1NET_RANGE_URL && url[u])++u;
    if(headers)while(h<=H1NET_MAX_HEADERS && headers[h])++h;
    if(!h1net_valid_long_url((const uint8_t *)url,u) || !h1net_valid_headers((const uint8_t *)headers,h))return H1NET_PROTOCOL;
    unsigned total=u+h+body_length,offset=0;int error=0;
    while(offset<total){
        unsigned count=total-offset;if(count>H1NET_MAX_PAYLOAD-4)count=H1NET_MAX_PAYLOAD-4;
        h1net_put16(request,(uint16_t)offset);h1net_put16(request+2,(uint16_t)total);
        for(unsigned i=0;i<count;++i){unsigned at=offset+i;
            request[4+i]=at<u?(uint8_t)url[at]:at<u+h?(uint8_t)headers[at-u]:((const uint8_t *)body)[at-u-h];}
        error=h1net_rpc(c,H1NET_REQUEST_CHUNK,request,count+4,response,sizeof response,&n);
        h1net_secure_clear(request,sizeof request);
        if(error)goto done;
        if(n!=2 || h1net_u16(response)!=offset+count){error=fail(c,H1NET_PROTOCOL);goto done;}
        offset+=count;
    }
    h1net_put32(request,method_id);h1net_put32(request+4,u);h1net_put32(request+8,h);h1net_put32(request+12,body_length);
    h1net_put32(request+16,maximum);h1net_put32(request+20,timeout);
    error=h1net_rpc(c,H1NET_HTTP_REQUEST,request,24,response,sizeof response,&n);
    if(!error){if(n!=4 || !h1net_u32(response))error=fail(c,H1NET_PROTOCOL);else *job=h1net_u32(response);}
done:
    h1net_secure_clear(request,sizeof request);return error;
}
int h1net_http_read(h1net_client *c,uint32_t job,uint32_t offset,uint8_t *data,
                   unsigned wanted,unsigned *received)
{
    uint8_t request[8],response[H1NET_MAX_PAYLOAD];unsigned n=0;
    if(!wanted || wanted>500 || !data || !received)return H1NET_PROTOCOL;
    h1net_put32(request,job);h1net_put32(request+4,wanted);
    int error=h1net_rpc(c,H1NET_HTTP_READ,request,8,response,sizeof response,&n);
    if(error)return error;
    if(n<12 || h1net_u32(response)!=job || h1net_u32(response+4)!=offset ||
       h1net_u32(response+8)!=n-12 || n-12>wanted)return fail(c,H1NET_PROTOCOL);
    *received=n-12;for(unsigned i=0;i<*received;++i)data[i]=response[12+i];
    return 0;
}
int h1net_http_cancel(h1net_client *c,uint32_t job)
{
    uint8_t request[4];unsigned n=0;h1net_put32(request,job);
    int error=h1net_rpc(c,H1NET_HTTP_CANCEL,request,4,0,0,&n);
    return error?error:(n?fail(c,H1NET_PROTOCOL):0);
}
int h1net_http_meta(h1net_client *c,uint32_t job,char url[H1NET_MAX_URL+1],char type[97])
{
    uint8_t request[4],response[H1NET_MAX_PAYLOAD];unsigned n=0;h1net_put32(request,job);
    int error=h1net_rpc(c,H1NET_HTTP_META,request,4,response,sizeof response,&n);if(error)return error;
    if(n<8 || h1net_u32(response)!=job)return fail(c,H1NET_PROTOCOL);
    unsigned u=h1net_u16(response+4),t=h1net_u16(response+6);
    if(u>H1NET_MAX_URL || t>96 || n!=8+u+t || !h1net_valid_url(response+8,u))return fail(c,H1NET_PROTOCOL);
    for(unsigned i=0;i<u;++i)url[i]=(char)response[8+i];url[u]=0;
    for(unsigned i=0;i<t;++i){if(response[8+u+i]<32 || response[8+u+i]==127)return fail(c,H1NET_PROTOCOL);type[i]=(char)response[8+u+i];}
    type[t]=0;return 0;
}
static int download_job(h1net_client *c,uint32_t job,uint32_t maximum,uint32_t timeout,
                        h1net_sink_fn sink,h1net_progress_fn progress,void *context,h1net_info *result)
{
    uint8_t data[500];uint32_t offset=0;int error;
    uint32_t last_progress=c->io.now_ms(c->io.context);
    for(;;){
        error=h1net_http_info(c,job,result);if(error)break;
        if(result->received>maximum || result->delivered!=offset){error=fail(c,H1NET_PROTOCOL);break;}
        if(progress && progress(context,result)){error=H1NET_ABORTED;break;}
        if(result->state==H1NET_FAILED || result->state==H1NET_CANCELLED){error=H1NET_REMOTE;break;}
        if(result->buffered){
            unsigned count=0,wanted=result->buffered>sizeof data?sizeof data:result->buffered;
            error=h1net_http_read(c,job,offset,data,wanted,&count);if(error)break;
            if(!count || count>maximum-offset){error=fail(c,H1NET_PROTOCOL);break;}
            if(sink(context,data,count)){error=H1NET_SINK;break;}
            offset+=count;
            last_progress=c->io.now_ms(c->io.context);
        }else if(result->state==H1NET_DONE){
            if(result->received!=offset || (result->content_length!=0xffffffffu &&
                                           result->content_length!=offset))return fail(c,H1NET_PROTOCOL);
            return 0;
        }else c->io.idle(c->io.context);
        if((uint32_t)(c->io.now_ms(c->io.context)-last_progress)>timeout+5000u){error=H1NET_TIMEOUT;break;}
    }
    if(!c->broken)(void)h1net_http_cancel(c,job);
    if(!(c->features&H1NET_CAP_RELEASE))c->ready=0;
    return error;
}

int h1net_download(h1net_client *c,const char *url,uint32_t maximum,uint32_t timeout,
                  h1net_sink_fn sink,h1net_progress_fn progress,void *context,h1net_info *result)
{
    uint32_t job=0;int error;
    if(!sink || !result)return H1NET_PROTOCOL;*result=(h1net_info){0};
    if(!c->ready && (error=h1net_hello(c)))return error;
    error=h1net_http_open(c,url,maximum,timeout,&job);if(error)return error;
    return download_job(c,job,maximum,timeout,sink,progress,context,result);
}
int h1net_http_range_open(h1net_client *c,const char *url,const h1net_range *range,uint32_t *job)
{
    uint8_t request[H1NET_MAX_PAYLOAD],response[4];unsigned length=0,referer=0,n=0;int error;
    if(job)*job=0;
    if(!url || !range || !job)return H1NET_PROTOCOL;
    while(length<=H1NET_RANGE_URL && url[length])++length;
    if(!h1net_valid_long_url((const uint8_t *)url,length) || !range->length || range->length>H1NET_MAX_DOWNLOAD ||
       range->offset>=range->total || range->length>range->total-range->offset || range->timeout_ms<1000 || range->timeout_ms>60000)return H1NET_PROTOCOL;
    if(range->referer){while(referer<=H1NET_MAX_URL && range->referer[referer])++referer;}
    if(referer && !h1net_valid_url((const uint8_t *)range->referer,referer))return H1NET_PROTOCOL;
    if(!c->ready && (error=h1net_hello(c)))return error;
    for(unsigned offset=0;offset<length;){
        unsigned count=length-offset;if(count>H1NET_MAX_PAYLOAD-4)count=H1NET_MAX_PAYLOAD-4;
        h1net_put16(request,(uint16_t)offset);h1net_put16(request+2,(uint16_t)length);
        for(unsigned i=0;i<count;++i)request[4+i]=(uint8_t)url[offset+i];
        error=h1net_rpc(c,H1NET_URL_CHUNK,request,count+4,response,sizeof response,&n);if(error)return error;
        offset+=count;if(n!=2 || h1net_u16(response)!=offset)return fail(c,H1NET_PROTOCOL);
    }
    h1net_put32(request,range->offset);h1net_put32(request+4,range->length);
    h1net_put32(request+8,range->total);h1net_put32(request+12,range->timeout_ms);
    for(unsigned i=0;i<referer;++i)request[16+i]=(uint8_t)range->referer[i];
    error=h1net_rpc(c,H1NET_HTTP_RANGE,request,16+referer,response,sizeof response,&n);if(error)return error;
    if(n!=4 || !h1net_u32(response))return fail(c,H1NET_PROTOCOL);
    *job=h1net_u32(response);return 0;
}
int h1net_download_range(h1net_client *c,const char *url,const h1net_range *range,
                        h1net_sink_fn sink,h1net_progress_fn progress,void *context,h1net_info *result)
{
    uint32_t job=0;int error;
    if(!url || !range || !sink || !result)return H1NET_PROTOCOL;*result=(h1net_info){0};
    error=h1net_http_range_open(c,url,range,&job);if(error)return error;
    error=download_job(c,job,range->length,range->timeout_ms,sink,progress,context,result);
    if(!error && (result->http_status!=206 || result->received!=range->length))return fail(c,H1NET_PROTOCOL);
    return error;
}
