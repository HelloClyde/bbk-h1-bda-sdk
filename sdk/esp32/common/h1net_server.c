/* SPDX-License-Identifier: Apache-2.0
 * H1 ESP32 contributors. SDK import with copyright-holder authorization. */
#include "h1net_server.h"

static void clear_request(h1net_server *s)
{ h1net_secure_clear(s->request_data,sizeof s->request_data);s->request_used=s->request_total=0; }
void h1net_server_init(h1net_server *s,const h1net_backend *backend)
{ s->backend=*backend;s->last_sequence=0;s->ready=0;s->url_used=s->url_total=0;clear_request(s); }
size_t h1net_server_dispatch(h1net_server *s,const h1net_frame *r,uint8_t *out,size_t capacity)
{
    /* Do not consume a job's data unless its largest reply can be retained. */
    if(!out || capacity<H1NET_MAX_FRAME)return 0;
    uint8_t data[H1NET_MAX_PAYLOAD];unsigned length=0;
    uint16_t error=H1NET_OK;
    if(!r->opcode || r->opcode>=128 || r->status){error=H1NET_INVALID;goto reply;}
    if(r->opcode==H1NET_HELLO){
        clear_request(s);
        if(r->length){error=H1NET_INVALID;goto reply;}
        s->ready=0;error=s->backend.reset(s->backend.context);
        if(error)goto reply;
        if(s->backend.compute_reset)s->backend.compute_reset(s->backend.context);
        static const char identity[]=H1NET_IDENTITY;
        for(unsigned i=0;i<sizeof identity-1;++i)data[i]=(uint8_t)identity[i];
        length=sizeof identity-1;s->ready=1;s->last_sequence=r->sequence;s->url_used=s->url_total=0;goto reply;
    }
    if(!s->ready){error=H1NET_NOT_CONNECTED;goto reply;}
    if(r->sequence!=(uint16_t)(s->last_sequence+1u)){error=H1NET_INVALID;goto reply;}
    s->last_sequence=r->sequence;
    switch(r->opcode){
    case H1NET_CAPABILITIES:{
        if(r->length || !s->backend.capabilities){error=H1NET_INVALID;break;}
        uint32_t words[4];s->backend.capabilities(s->backend.context,words);
        if(s->backend.compute)words[3]|=H1NET_CAP_AUDIO_AAC;
        if(s->backend.request)words[3]|=H1NET_CAP_HTTP_REQUEST;
        if(s->backend.wifi)words[3]|=H1NET_CAP_WIFI_CONFIG;
        if(s->backend.headers)words[3]|=H1NET_CAP_HTTP_HEADERS;
        for(unsigned i=0;i<4;i++)h1net_put32(data+i*4,words[i]);
        length=16;break;
    }
    case H1NET_HTTP_RELEASE:
        if(r->length!=4 || !h1net_u32(r->payload) || !s->backend.release){error=H1NET_INVALID;break;}
        error=s->backend.release(s->backend.context,h1net_u32(r->payload));break;
    case H1NET_REQUEST_CHUNK:{
        if(r->length<5 || !s->backend.request){error=H1NET_INVALID;clear_request(s);break;}
        unsigned offset=h1net_u16(r->payload),total=h1net_u16(r->payload+2),n=r->length-4;
        if(!total || total>H1NET_MAX_REQUEST || offset>total || n>total-offset){error=H1NET_INVALID;clear_request(s);break;}
        if(!offset){clear_request(s);s->request_total=total;}
        if(offset!=s->request_used || total!=s->request_total){error=H1NET_INVALID;clear_request(s);break;}
        for(unsigned i=0;i<n;++i)s->request_data[offset+i]=r->payload[4+i];
        s->request_used+=n;h1net_put16(data,(uint16_t)s->request_used);length=2;break;
    }
    case H1NET_HTTP_REQUEST:{
        if(r->length!=24 || !s->backend.request){error=H1NET_INVALID;clear_request(s);break;}
        uint32_t method=h1net_u32(r->payload),u=h1net_u32(r->payload+4),h=h1net_u32(r->payload+8),b=h1net_u32(r->payload+12),
                 maximum=h1net_u32(r->payload+16),timeout=h1net_u32(r->payload+20),job=0;
        if((method!=1 && method!=2) || !u || u>H1NET_RANGE_URL || h>H1NET_MAX_HEADERS || b>H1NET_MAX_REQUEST_BODY ||
           (method==1 && b) || !maximum || maximum>H1NET_MAX_DOWNLOAD || timeout<1000 || timeout>60000 ||
           s->request_used!=s->request_total || s->request_used!=u+h+b ||
           !h1net_valid_long_url(s->request_data,u) || !h1net_valid_headers(s->request_data+u,h)){
            error=H1NET_INVALID;clear_request(s);break;
        }
        char url[H1NET_RANGE_URL+1],headers[H1NET_MAX_HEADERS+1];
        for(unsigned i=0;i<u;++i)url[i]=(char)s->request_data[i];
        url[u]=0;
        for(unsigned i=0;i<h;++i)headers[i]=(char)s->request_data[u+i];
        headers[h]=0;
        error=s->backend.request(s->backend.context,url,method==1?"GET":"POST",headers,s->request_data+u+h,b,maximum,timeout,&job);
        h1net_secure_clear(headers,sizeof headers);clear_request(s);
        if(!error){if(!job){error=H1NET_INTERNAL;break;}h1net_put32(data,job);length=4;}
        break;
    }
    case H1NET_URL_CHUNK:{
        if(r->length<5){error=H1NET_INVALID;break;}
        unsigned offset=h1net_u16(r->payload),total=h1net_u16(r->payload+2),n=r->length-4;
        if(!total || total>H1NET_RANGE_URL || offset>total || n>total-offset){error=H1NET_INVALID;break;}
        if(!offset){s->url_used=0;s->url_total=total;}
        if(offset!=s->url_used || total!=s->url_total){error=H1NET_INVALID;break;}
        for(unsigned i=0;i<n;++i)if(r->payload[4+i]<=32 || r->payload[4+i]==127 || r->payload[4+i]=='\\'){error=H1NET_INVALID;break;}
        if(error)break;
        for(unsigned i=0;i<n;++i)s->range_url[offset+i]=(char)r->payload[4+i];
        s->url_used+=n;s->range_url[s->url_used]=0;h1net_put16(data,(uint16_t)s->url_used);length=2;break;
    }
    case H1NET_HTTP_RANGE:{
        if(r->length<16 || r->length>16+H1NET_MAX_URL || !s->backend.range || s->url_used!=s->url_total ||
           !h1net_valid_long_url((const uint8_t *)s->range_url,s->url_used)){error=H1NET_INVALID;break;}
        char referer[H1NET_MAX_URL+1];unsigned n=r->length-16;
        if(n && !h1net_valid_url(r->payload+16,n)){error=H1NET_INVALID;break;}
        for(unsigned i=0;i<n;++i)referer[i]=(char)r->payload[16+i];
        referer[n]=0;
        h1net_range range={h1net_u32(r->payload),h1net_u32(r->payload+4),h1net_u32(r->payload+8),h1net_u32(r->payload+12),referer};
        if(!range.length || range.length>H1NET_MAX_DOWNLOAD || range.offset>=range.total || range.length>range.total-range.offset ||
           range.timeout_ms<1000 || range.timeout_ms>60000){error=H1NET_INVALID;break;}
        uint32_t job=0;error=s->backend.range(s->backend.context,s->range_url,&range,&job);
        if(!error){s->url_used=s->url_total=0;if(!job){error=H1NET_INTERNAL;break;}h1net_put32(data,job);length=4;}break;
    }
    case H1NET_STATUS:{
        if(r->length){error=H1NET_INVALID;break;}
        uint32_t status[6];s->backend.status(s->backend.context,status);
        for(unsigned i=0;i<6;++i)h1net_put32(data+i*4,status[i]);
        length=24;break;
    }
    case H1NET_HTTP_OPEN:case H1NET_HTTP_OPEN_BACKGROUND:{
        if(r->length<9 || r->length>H1NET_MAX_URL+8 ||
           !h1net_valid_url(r->payload+8,r->length-8)){error=H1NET_INVALID;break;}
        uint32_t maximum=h1net_u32(r->payload),timeout=h1net_u32(r->payload+4),job=0;
        if(!maximum || maximum>H1NET_MAX_DOWNLOAD || timeout<1000 || timeout>60000){
            error=H1NET_INVALID;break;
        }
        char url[H1NET_MAX_URL+1];
        for(unsigned i=8;i<r->length;++i)url[i-8]=(char)r->payload[i];
        url[r->length-8]=0;
        if(r->opcode==H1NET_HTTP_OPEN_BACKGROUND){
            if(!s->backend.background){error=H1NET_INVALID;break;}
            error=s->backend.background(s->backend.context,url,maximum,timeout,&job);
        }else error=s->backend.open(s->backend.context,url,maximum,timeout,&job);
        if(!error){if(!job){error=H1NET_INTERNAL;break;}h1net_put32(data,job);length=4;}
        break;
    }
    case H1NET_HTTP_INFO:{
        if(r->length!=4 || !h1net_u32(r->payload)){error=H1NET_INVALID;break;}
        h1net_info info;uint32_t job=h1net_u32(r->payload);
        error=s->backend.info(s->backend.context,job,&info);if(error)break;
        if(info.job!=job || info.state>H1NET_CANCELLED || info.received<info.delivered ||
           info.buffered!=info.received-info.delivered){error=H1NET_INTERNAL;break;}
        uint32_t words[8]={info.job,info.state,info.http_status,info.content_length,
                          info.received,info.delivered,info.buffered,info.error};
        for(unsigned i=0;i<8;++i)h1net_put32(data+i*4,words[i]);
        length=32;break;
    }
    case H1NET_HTTP_READ:{
        if(r->length!=8){error=H1NET_INVALID;break;}
        uint32_t job=h1net_u32(r->payload),wanted=h1net_u32(r->payload+4),offset=0;
        unsigned count=0;
        if(!job || !wanted || wanted>500){error=H1NET_INVALID;break;}
        error=s->backend.read(s->backend.context,job,wanted,data+12,&offset,&count);
        if(!error){
            if(count>wanted){error=H1NET_INTERNAL;break;}
            h1net_put32(data,job);h1net_put32(data+4,offset);h1net_put32(data+8,count);length=count+12;
        }
        break;
    }
    case H1NET_HTTP_HEADERS:{
        if(r->length!=12||!s->backend.headers){error=H1NET_INVALID;break;}
        uint32_t job=h1net_u32(r->payload),offset=h1net_u32(r->payload+4),wanted=h1net_u32(r->payload+8);
        unsigned total=0,count=0;
        if(!job||!wanted||wanted>500){error=H1NET_INVALID;break;}
        error=s->backend.headers(s->backend.context,job,offset,wanted,data+12,&count,&total);
        if(!error){if(total>8192||offset>total||count>wanted||count>total-offset){error=H1NET_INTERNAL;break;}
            h1net_put32(data,job);h1net_put32(data+4,offset);h1net_put32(data+8,total);length=12+count;}
        break;
    }
    case H1NET_HTTP_META:{
        if(r->length!=4 || !h1net_u32(r->payload)){error=H1NET_INVALID;break;}
        if(!s->backend.meta){error=H1NET_INVALID;break;}
        char url[H1NET_MAX_URL+1]={0},type[97]={0};uint32_t job=h1net_u32(r->payload);
        error=s->backend.meta(s->backend.context,job,url,type);if(error)break;
        unsigned u=0,t=0;while(u<H1NET_MAX_URL && url[u])++u;while(t<96 && type[t])++t;
        if(!h1net_valid_url((const uint8_t *)url,u) || url[u] || type[t]){error=H1NET_INTERNAL;break;}
        h1net_put32(data,job);h1net_put16(data+4,(uint16_t)u);h1net_put16(data+6,(uint16_t)t);
        for(unsigned i=0;i<u;++i)data[8+i]=(uint8_t)url[i];
        for(unsigned i=0;i<t;++i)data[8+u+i]=(uint8_t)type[i];
        length=8+u+t;break;
    }
    case H1NET_HTTP_CANCEL:
        if(r->length!=4 || !h1net_u32(r->payload)){error=H1NET_INVALID;break;}
        error=s->backend.cancel(s->backend.context,h1net_u32(r->payload));break;
    case H1NET_WIFI_SCAN:case H1NET_WIFI_PAGE:case H1NET_WIFI_STATUS:
    case H1NET_WIFI_CONNECT:case H1NET_WIFI_FORGET:case H1NET_WIFI_CANCEL:
        if(!s->backend.wifi){error=H1NET_INVALID;break;}
        error=s->backend.wifi(s->backend.context,r->opcode,r->payload,r->length,data,&length);
        if(length>H1NET_MAX_PAYLOAD){error=H1NET_INTERNAL;length=0;}
        break;
    default:
        if(!s->backend.compute||!s->backend.compute(s->backend.context,r->opcode,r->payload,r->length,&error,data,&length))error=H1NET_INVALID;
        if(length>H1NET_MAX_PAYLOAD){error=H1NET_INTERNAL;length=0;}
        break;
    }
reply:
    if(r->opcode==H1NET_HTTP_REQUEST)clear_request(s);
    return h1net_encode(out,capacity,(uint8_t)(r->opcode|0x80u),r->sequence,error,
                        data,error?0:length);
}
