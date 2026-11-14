/* SPDX-License-Identifier: Apache-2.0
 * H1 ESP32 contributors. SDK import with copyright-holder authorization. */
#include "h1net_platform.h"
#include <string.h>
#include <stdio.h>
#include "mbedtls/base64.h"
#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/stream_buffer.h"
#include "freertos/queue.h"
#include "usb/usb_host.h"
#include "usb/cdc_acm_host.h"
#include "soc/usb_dwc_struct.h"
#include "soc/usb_wrap_struct.h"
#include "soc/rtc_cntl_struct.h"

#define USB_GONE BIT0
#define USB_OVERFLOW BIT1
#define USB_OPEN BIT2
#define USB_RX BIT3
#define USB_BRIDGE BIT4
static StreamBufferHandle_t received;
static EventGroupHandle_t usb_events;
static cdc_acm_dev_hdl_t device;
static h1net_parser parser;
static h1net_server server;
typedef struct {unsigned operation,length;uint32_t sequence;uint8_t data[528];} bridge_command;
static QueueHandle_t bridge_commands;
static bool bridging;
bool h1net_usb_bridge_submit(unsigned operation,uint32_t sequence,const uint8_t *data,unsigned length)
{
    if(!bridge_commands || !h1net_usb_connected() || operation<H1_BRIDGE_OPEN || operation>H1_BRIDGE_DATA ||
       length>528 || (length&&!data))return false;
    bridge_command command={.operation=operation,.sequence=sequence,.length=length};
    if(length)memcpy(command.data,data,length);
    if(xQueueSend(bridge_commands,&command,0)!=pdTRUE)return false;
    xEventGroupSetBits(usb_events,USB_BRIDGE);
    return true;
}
static void bridge_service(void)
{
    bridge_command command;
    while(xQueueReceive(bridge_commands,&command,0)==pdTRUE){
        if(command.operation==H1_BRIDGE_CLOSE){
            bridging=false;h1net_parser_init(&parser);
            (void)cdc_acm_host_set_control_line_state(device,false,false);
            printf("\nH1DBG/1 MODE 0\n");fflush(stdout);continue;
        }
        if(command.operation==H1_BRIDGE_OPEN){
            esp_err_t error=cdc_acm_host_set_control_line_state(device,false,false);
            if(error==ESP_OK)error=cdc_acm_host_set_control_line_state(device,true,false);
            if(error!=ESP_OK){printf("\nH1DBG/1 ERROR control-line\n");fflush(stdout);continue;}
            xStreamBufferReset(received);h1net_http_disconnected();bridging=true;
            printf("\nH1DBG/1 MODE 1\n");fflush(stdout);continue;
        }
        if(!bridging){printf("\nH1DBG/1 ERROR bridge-closed\n");fflush(stdout);continue;}
        esp_err_t error=cdc_acm_host_data_tx_blocking(device,command.data,command.length,1500);
        if(error!=ESP_OK){
            xEventGroupSetBits(usb_events,USB_GONE);
            printf("\nH1DBG/1 ERROR usb-write\n");
        }else printf("\nH1DBG/1 ACK %lu\n",(unsigned long)command.sequence);
        fflush(stdout);
    }
}
static void bridge_receive(const uint8_t *data,unsigned length)
{
    unsigned char encoded[348];size_t used=0;
    if(mbedtls_base64_encode(encoded,sizeof encoded,&used,data,length)){
        printf("\nH1DBG/1 ERROR encoding\n");
    }else printf("\nH1DBG/1 DATA %.*s\n",(int)used,encoded);
    fflush(stdout);
}
static portMUX_TYPE diagnostic_lock=portMUX_INITIALIZER_UNLOCKED;
static h1net_usb_diagnostics diagnostic={.stage="starting"};

static void set_stage(const char *stage,esp_err_t error)
{
    portENTER_CRITICAL(&diagnostic_lock);
    diagnostic.stage=stage;diagnostic.last_error=error;
    portEXIT_CRITICAL(&diagnostic_lock);
}
void h1net_usb_get_diagnostics(h1net_usb_diagnostics *out)
{
    portENTER_CRITICAL(&diagnostic_lock);
    *out=diagnostic;
    out->parser_bad_crc=parser.bad_crc;out->parser_bad_headers=parser.bad_headers;
    portEXIT_CRITICAL(&diagnostic_lock);
    usb_host_lib_info_t info;
    if(out->host_ready){
        if(usb_host_lib_info(&info)==ESP_OK)out->devices=info.num_devices;
        /* Read-only snapshots: these status/configuration registers do not clear on read.
         * Port power is the controller's intent, not a measurement of connector VBUS. */
        usb_dwc_hprt_reg_t port={.val=USB_DWC.hprt_reg.val};
        out->raw_hprt=port.val;out->raw_wrap=USB_WRAP.otg_conf.val;out->raw_rtc=RTCCNTL.usb_conf.val;
        out->phy_otg=RTCCNTL.usb_conf.sw_hw_usb_phy_sel && RTCCNTL.usb_conf.sw_usb_phy_sel &&
            !USB_WRAP.otg_conf.phy_sel && USB_WRAP.otg_conf.pad_enable;
        out->host_mode=USB_DWC.gintsts_reg.curmod;
        out->port_powered=port.prtpwr;out->port_attached=port.prtconnsts;out->line_state=port.prtlnsts;
    }
}
static void device_enumerated(usb_device_handle_t usb_device)
{
    const usb_device_desc_t *desc;usb_device_info_t info;
    if(usb_host_get_device_descriptor(usb_device,&desc)!=ESP_OK ||
       usb_host_device_info(usb_device,&info)!=ESP_OK)return;
    ESP_LOGI("h1_usb","Enumerated addr=%u vid:pid=%04x:%04x class=%02x speed=%s parent_port=%u",
             info.dev_addr,desc->idVendor,desc->idProduct,desc->bDeviceClass,
             info.speed==USB_SPEED_LOW?"low":info.speed==USB_SPEED_FULL?"full":"high",info.parent.port_num);
}

bool h1net_usb_connected(void)
{ return usb_events && (xEventGroupGetBits(usb_events)&USB_OPEN); }
static bool data_received(const uint8_t *data,size_t length,void *unused)
{
    (void)unused;
    if(xStreamBufferSend(received,data,length,0)!=length)xEventGroupSetBits(usb_events,USB_OVERFLOW);
    xEventGroupSetBits(usb_events,USB_RX);
    return true;
}
static void event(const cdc_acm_host_dev_event_data_t *data,void *unused)
{
    (void)unused;
    if(data->type==CDC_ACM_HOST_DEVICE_DISCONNECTED || data->type==CDC_ACM_HOST_ERROR){
        ESP_LOGW("h1_usb","CDC event=%d",data->type);
        xEventGroupSetBits(usb_events,USB_GONE);
    }
}
static void usb_events_task(void *unused)
{
    (void)unused;
    for(;;){
        uint32_t flags=0;
        esp_err_t result=usb_host_lib_handle_events(portMAX_DELAY,&flags);
        if(result!=ESP_OK){ESP_LOGE("h1_usb","USB host events: %s",esp_err_to_name(result));vTaskDelay(pdMS_TO_TICKS(100));}
        if(flags&USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS)(void)usb_host_device_free_all();
    }
}
static void request(void *unused,const h1net_frame *frame)
{
    (void)unused;uint8_t response[H1NET_MAX_FRAME];
    if(xEventGroupGetBits(usb_events)&(USB_GONE|USB_OVERFLOW))return;
    int64_t started=esp_timer_get_time();
    size_t size=h1net_server_dispatch(&server,frame,response,sizeof response);
    if(frame->opcode==H1NET_WIFI_CONNECT)h1net_secure_clear((void *)frame->payload,frame->length);
    int64_t dispatched=esp_timer_get_time();
    esp_err_t sent=size?cdc_acm_host_data_tx_blocking(device,response,size,1500):ESP_FAIL;
    uint32_t waited=(uint32_t)(esp_timer_get_time()-dispatched);
    portENTER_CRITICAL(&diagnostic_lock);
    diagnostic.rpc_frames++;diagnostic.request_bytes+=H1NET_HEADER+frame->length+4u;
    if(sent==ESP_OK)diagnostic.response_bytes+=(uint32_t)size;else diagnostic.tx_errors++;
    if(frame->opcode==H1NET_HTTP_READ)diagnostic.http_reads++;
    diagnostic.dispatch_us+=(uint64_t)(dispatched-started);diagnostic.tx_wait_us+=waited;
    if(waited>diagnostic.tx_wait_us_max)diagnostic.tx_wait_us_max=waited;
    portEXIT_CRITICAL(&diagnostic_lock);
    if(sent!=ESP_OK)xEventGroupSetBits(usb_events,USB_GONE);
}
void h1net_usb_task(void *unused)
{
    (void)unused;
    /* Compile DEBUG messages, but enable them only for USB enumeration. */
    esp_log_level_set("HUB",ESP_LOG_DEBUG);esp_log_level_set("ENUM",ESP_LOG_DEBUG);
    esp_log_level_set("EXT_HUB",ESP_LOG_DEBUG);esp_log_level_set("EXT_PORT",ESP_LOG_DEBUG);
    esp_log_level_set("HCD DWC",ESP_LOG_DEBUG);
    received=xStreamBufferCreate(4096,1);usb_events=xEventGroupCreate();
    bridge_commands=xQueueCreate(4,sizeof(bridge_command));
    assert(received && usb_events && bridge_commands);
    const usb_host_config_t host={.skip_phy_setup=false,.intr_flags=ESP_INTR_FLAG_LEVEL1};
    ESP_ERROR_CHECK(usb_host_install(&host));
    portENTER_CRITICAL(&diagnostic_lock);diagnostic.host_ready=true;portEXIT_CRITICAL(&diagnostic_lock);
    ESP_ERROR_CHECK(xTaskCreate(usb_events_task,"usb_events",4096,NULL,20,NULL)==pdPASS?ESP_OK:ESP_ERR_NO_MEM);
    const cdc_acm_host_driver_config_t driver={
        .driver_task_stack_size=4096,.driver_task_priority=10,.xCoreID=0,.new_dev_cb=device_enumerated,
    };
    ESP_ERROR_CHECK(cdc_acm_host_install(&driver));
    ESP_LOGI("h1_usb","USB Host ready; looking for H1 07c4:a4a6 interface 0");
    const cdc_acm_host_device_config_t config={
        .connection_timeout_ms=1000,.out_buffer_size=1024,.in_buffer_size=1024,
        .user_arg=NULL,.event_cb=event,.data_cb=data_received,
    };
    esp_err_t previous_open_error=ESP_OK;
    uint32_t previous_hprt=UINT32_MAX,previous_wrap=UINT32_MAX,previous_rtc=UINT32_MAX;
    for(;;){
        device=NULL;
        xEventGroupClearBits(usb_events,USB_GONE|USB_OVERFLOW|USB_OPEN);
        xQueueReset(bridge_commands);
        xStreamBufferReset(received);h1net_parser_init(&parser);
        h1net_backend backend;h1net_http_backend(&backend);h1net_server_init(&server,&backend);
        esp_err_t error=cdc_acm_host_open(0x07c4,0xa4a6,0,&config,&device);
        if(error!=ESP_OK){
            set_stage("cdc_open",error);
            if(error!=previous_open_error)ESP_LOGW("h1_usb","CDC open: %s",esp_err_to_name(error));
            h1net_usb_diagnostics bus;h1net_usb_get_diagnostics(&bus);
            if(bus.raw_hprt!=previous_hprt || bus.raw_wrap!=previous_wrap || bus.raw_rtc!=previous_rtc){
                ESP_LOGI("h1_usb","USB bus host=%u phy_otg=%u port_power=%u attached=%u line=%u hprt=%08lx wrap=%08lx rtc=%08lx",
                    bus.host_mode,bus.phy_otg,bus.port_powered,bus.port_attached,bus.line_state,
                    (unsigned long)bus.raw_hprt,(unsigned long)bus.raw_wrap,(unsigned long)bus.raw_rtc);
                previous_hprt=bus.raw_hprt;previous_wrap=bus.raw_wrap;previous_rtc=bus.raw_rtc;
            }
            previous_open_error=error;vTaskDelay(pdMS_TO_TICKS(100));continue;
        }
        previous_open_error=ESP_OK;set_stage("line_coding",ESP_OK);
        const cdc_acm_line_coding_t line={.dwDTERate=115200,.bCharFormat=0,.bParityType=0,.bDataBits=8};
        error=cdc_acm_host_line_coding_set(device,&line);
        if(error==ESP_OK){set_stage("control_line",ESP_OK);error=cdc_acm_host_set_control_line_state(device,true,false);}
        if(error!=ESP_OK){
            h1net_usb_diagnostics status;h1net_usb_get_diagnostics(&status);set_stage(status.stage,error);
            ESP_LOGW("h1_usb","CDC %s: %s",status.stage,esp_err_to_name(error));
            cdc_acm_host_close(device);device=NULL;vTaskDelay(pdMS_TO_TICKS(100));continue;
        }
        set_stage("connected",ESP_OK);
        xEventGroupSetBits(usb_events,USB_OPEN);ESP_LOGI("h1_usb","H1 CDC connected; waiting for H1NET/1 HELLO");
        while(!(xEventGroupGetBits(usb_events)&(USB_GONE|USB_OVERFLOW))){
            /* Clear before checking both queues. A producer arriving after
             * either check leaves its bit set, so the wait cannot lose a wake.
             * Never borrow the stream buffer's task-notification slot. */
            xEventGroupClearBits(usb_events,USB_RX|USB_BRIDGE);
            bridge_service();
            uint8_t data[256];size_t n=xStreamBufferReceive(received,data,sizeof data,0);
            if(n){if(bridging)bridge_receive(data,n);else h1net_parser_feed(&parser,data,n,request,NULL);}
            else (void)xEventGroupWaitBits(usb_events,USB_RX|USB_BRIDGE|USB_GONE|USB_OVERFLOW,
                                          pdFALSE,pdFALSE,pdMS_TO_TICKS(10));
        }
        if(bridging){printf("\nH1DBG/1 GONE\n");fflush(stdout);}
        bridging=false;
        xEventGroupClearBits(usb_events,USB_OPEN);h1net_http_disconnected();
        set_stage("disconnected",ESP_OK);
        (void)cdc_acm_host_close(device);device=NULL;ESP_LOGI("h1_usb","H1 disconnected; HTTP cancelled");
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
