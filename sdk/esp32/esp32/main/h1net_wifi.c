/* SPDX-License-Identifier: Apache-2.0
 * H1 ESP32 contributors. SDK import with copyright-holder authorization. */
#include "h1net_platform.h"
#include "h1net_wifi_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "cJSON.h"
#include "driver/uart.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "mbedtls/base64.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/timers.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "nvs.h"

#define WIFI_READY BIT0
#define WIFI_DOWN BIT1
#define WIFI_CANCEL BIT2
typedef struct {uint32_t version;char ssid[33],password[64];} credentials;
static EventGroupHandle_t wifi_events;
static esp_netif_t *station;
static TimerHandle_t retry_timer;
static credentials saved;
static credentials pending;
static unsigned pending_auth;
static SemaphoreHandle_t wifi_lock;
static QueueHandle_t control_queue;
static volatile bool managed;
static bool committing;
static volatile uint32_t disconnect_reason;
static h1wifi_status settings;
static h1wifi_ap access_points[H1WIFI_AP_MAX];
static void wifi_control_task(void *);
static bool may_retry(void)
{bool yes;xSemaphoreTake(wifi_lock,portMAX_DELAY);yes=!managed&&saved.ssid[0];xSemaphoreGive(wifi_lock);return yes;}

bool h1net_wifi_connected(void)
{ return wifi_events && (xEventGroupGetBits(wifi_events)&WIFI_READY); }
static void retry(TimerHandle_t timer)
{ (void)timer;if(may_retry())(void)esp_wifi_connect(); }
static void wifi_event(void *unused,esp_event_base_t base,int32_t id,void *event)
{
    (void)unused;
    if(base==WIFI_EVENT && id==WIFI_EVENT_STA_START && may_retry())(void)esp_wifi_connect();
    if(base==WIFI_EVENT && id==WIFI_EVENT_STA_DISCONNECTED){
        wifi_event_sta_disconnected_t *gone=event;disconnect_reason=gone->reason;
        xEventGroupClearBits(wifi_events,WIFI_READY);xEventGroupSetBits(wifi_events,WIFI_DOWN);
        if(may_retry())xTimerReset(retry_timer,0);
    }
    if(base==IP_EVENT && id==IP_EVENT_STA_GOT_IP){
        ip_event_got_ip_t *got=event;
        xEventGroupSetBits(wifi_events,WIFI_READY);xTimerStop(retry_timer,0);
        esp_err_t clock_error=esp_netif_sntp_start();
        if(clock_error!=ESP_OK&&clock_error!=ESP_ERR_INVALID_STATE)
            ESP_LOGW("h1_wifi","Clock restart: %s",esp_err_to_name(clock_error));
        ESP_LOGI("h1_wifi","Connected, IP=" IPSTR,IP2STR(&got->ip_info.ip));
    }
}
void h1net_wifi_init(void)
{
    wifi_lock=xSemaphoreCreateMutex();control_queue=xQueueCreate(1,sizeof(uint8_t));assert(wifi_lock&&control_queue);
    wifi_events=xEventGroupCreate();assert(wifi_events);
    retry_timer=xTimerCreate("wifi_retry",pdMS_TO_TICKS(3000),pdFALSE,NULL,retry);assert(retry_timer);
    ESP_ERROR_CHECK(esp_netif_init());ESP_ERROR_CHECK(esp_event_loop_create_default());
    station=esp_netif_create_default_wifi_sta();assert(station);
    esp_sntp_config_t clock=ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    clock.start=false;
    ESP_ERROR_CHECK(esp_netif_sntp_init(&clock));
    wifi_init_config_t init=WIFI_INIT_CONFIG_DEFAULT();ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,wifi_event,NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,wifi_event,NULL));
    nvs_handle_t nvs;size_t size=sizeof saved;
    if(nvs_open("h1net",NVS_READONLY,&nvs)==ESP_OK){
        if(nvs_get_blob(nvs,"wifi",&saved,&size)!=ESP_OK || size!=sizeof saved ||
           saved.version!=1 || !memchr(saved.ssid,0,sizeof saved.ssid) ||
           !memchr(saved.password,0,sizeof saved.password))memset(&saved,0,sizeof saved);
        nvs_close(nvs);
    }
    settings.rssi=-127;memcpy(settings.saved,saved.ssid,sizeof settings.saved);
    wifi_config_t config={0};
    memcpy(config.sta.ssid,saved.ssid,strlen(saved.ssid));
    memcpy(config.sta.password,saved.password,strlen(saved.password));
    config.sta.pmf_cfg.capable=true;config.sta.pmf_cfg.required=false;
    config.sta.sae_pwe_h2e=WPA3_SAE_PWE_BOTH;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    if(saved.ssid[0])ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA,&config));
    h1net_secure_clear(&config,sizeof config);
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(xTaskCreate(wifi_control_task,"wifi_control",6144,NULL,4,NULL)==pdPASS?ESP_OK:ESP_ERR_NO_MEM);
    if(!saved.ssid[0])ESP_LOGI("h1_wifi","Ready for H1 Wi-Fi setup; no saved network");
}
void h1net_wifi_status(uint32_t out[6])
{
    bool connected=h1net_wifi_connected();esp_netif_ip_info_t ip={0};wifi_ap_record_t ap={0};
    if(connected){esp_netif_get_ip_info(station,&ip);esp_wifi_sta_get_ap_info(&ap);}
    out[0]=1;out[1]=connected?1:0;out[2]=ip.ip.addr;
    out[3]=(uint32_t)(int32_t)(connected?ap.rssi:-127);
    out[4]=esp_get_free_heap_size();out[5]=H1NET_MAX_DOWNLOAD;
}
static esp_err_t store_credentials(const credentials *value)
{
    nvs_handle_t nvs;esp_err_t err=nvs_open("h1net",NVS_READWRITE,&nvs);
    if(err!=ESP_OK)return err;
    err=value?nvs_set_blob(nvs,"wifi",value,sizeof *value):nvs_erase_key(nvs,"wifi");
    if(!value&&err==ESP_ERR_NVS_NOT_FOUND)err=ESP_OK;
    if(err==ESP_OK)err=nvs_commit(nvs);
    nvs_close(nvs);return err;
}
static void snapshot(h1wifi_status *out)
{
    xSemaphoreTake(wifi_lock,portMAX_DELAY);*out=settings;
    memcpy(out->saved,saved.ssid,sizeof out->saved);xSemaphoreGive(wifi_lock);
    uint32_t status[6];h1net_wifi_status(status);out->connected=status[1];out->ip=status[2];out->rssi=(int32_t)status[3];
    wifi_ap_record_t ap;if(out->connected&&esp_wifi_sta_get_ap_info(&ap)==ESP_OK){
        memcpy(out->current,ap.ssid,32);out->current[32]=0;
    }
}
static void disconnect_station(void)
{
    xEventGroupClearBits(wifi_events,WIFI_DOWN);
    if(esp_wifi_disconnect()==ESP_OK)(void)xEventGroupWaitBits(wifi_events,WIFI_DOWN,pdTRUE,pdFALSE,pdMS_TO_TICKS(1000));
    xEventGroupClearBits(wifi_events,WIFI_READY|WIFI_DOWN);
}
static esp_err_t apply_credentials(const credentials *value,unsigned auth)
{
    wifi_config_t config={0};memcpy(config.sta.ssid,value->ssid,strlen(value->ssid));
    memcpy(config.sta.password,value->password,strlen(value->password));
    config.sta.pmf_cfg.capable=true;config.sta.pmf_cfg.required=auth==6;
    config.sta.threshold.authmode=auth==6?WIFI_AUTH_WPA3_PSK:auth?WIFI_AUTH_WPA_PSK:WIFI_AUTH_OPEN;
    config.sta.sae_pwe_h2e=WPA3_SAE_PWE_BOTH;
    esp_err_t err=esp_wifi_set_config(WIFI_IF_STA,&config);h1net_secure_clear(&config,sizeof config);
    return err==ESP_OK?esp_wifi_connect():err;
}
static void resume_saved(void)
{
    credentials previous;xSemaphoreTake(wifi_lock,portMAX_DELAY);previous=saved;xSemaphoreGive(wifi_lock);
    if(previous.ssid[0]&&!h1net_wifi_connected())(void)apply_credentials(&previous,previous.password[0]?3:0);
    h1net_secure_clear(&previous,sizeof previous);
}
static void release_control(bool restore)
{
    if(restore)resume_saved();
    xSemaphoreTake(wifi_lock,portMAX_DELAY);managed=false;committing=false;xSemaphoreGive(wifi_lock);
    if(may_retry()&&!h1net_wifi_connected())xTimerReset(retry_timer,0);
}
static void scan_networks(void)
{
    /* A reconnect in progress makes IDF reject scans. Stop it, retaining NVS. */
    if(!h1net_wifi_connected())disconnect_station();
    wifi_scan_config_t scan={.show_hidden=true};
    esp_err_t err=esp_wifi_scan_start(&scan,true);
    static wifi_ap_record_t records[48];static h1wifi_ap result[H1WIFI_AP_MAX];uint16_t count=48;unsigned used=0;
    if(err==ESP_OK)err=esp_wifi_scan_get_ap_records(&count,records);
    if(err!=ESP_OK)(void)esp_wifi_clear_ap_list();
    if(err==ESP_OK)for(unsigned i=0;i<count&&used<H1WIFI_AP_MAX;i++){
        if(!records[i].ssid[0])continue;
        char ssid[33];memcpy(ssid,records[i].ssid,32);ssid[32]=0;bool duplicate=false;
        for(unsigned j=0;j<used;j++)if(result[j].auth==(unsigned)records[i].authmode&&!strcmp(result[j].ssid,ssid))duplicate=true;
        if(duplicate)continue;
        memcpy(result[used].ssid,ssid,sizeof ssid);result[used].auth=records[i].authmode;
        result[used].rssi=records[i].rssi;result[used].channel=records[i].primary;used++;
    }
    xSemaphoreTake(wifi_lock,portMAX_DELAY);
    if(err==ESP_OK)memcpy(access_points,result,used*sizeof *result);
    settings.scan_count=err==ESP_OK?used:0;settings.scan_state=err==ESP_OK?H1WIFI_SCAN_DONE:H1WIFI_SCAN_FAILED;
    xSemaphoreGive(wifi_lock);release_control(true);
}
static unsigned join_error(uint32_t reason,bool timed_out)
{
    if(timed_out)return H1WIFI_ERR_TIMEOUT;
    if(reason==WIFI_REASON_AUTH_FAIL||reason==WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT||reason==WIFI_REASON_HANDSHAKE_TIMEOUT)return H1WIFI_ERR_AUTH;
    return reason==WIFI_REASON_NO_AP_FOUND?H1WIFI_ERR_NO_AP:H1WIFI_ERR_DRIVER;
}
static void connect_network(void)
{
    credentials candidate;unsigned auth;
    xSemaphoreTake(wifi_lock,portMAX_DELAY);candidate=pending;auth=pending_auth;
    h1net_secure_clear(&pending,sizeof pending);xSemaphoreGive(wifi_lock);
    disconnect_station();disconnect_reason=0;
    xEventGroupClearBits(wifi_events,WIFI_READY|WIFI_DOWN);
    esp_err_t err=apply_credentials(&candidate,auth);
    EventBits_t bits=err==ESP_OK?xEventGroupWaitBits(wifi_events,WIFI_READY|WIFI_DOWN|WIFI_CANCEL,pdFALSE,pdFALSE,pdMS_TO_TICKS(20000)):0;
    unsigned error=0,outcome=H1WIFI_SAVED;uint32_t reason=disconnect_reason;
    if(bits&WIFI_CANCEL)outcome=H1WIFI_CANCELLED;
    else if(err!=ESP_OK){error=H1WIFI_ERR_DRIVER;outcome=H1WIFI_FAILED;}
    else if(!(bits&WIFI_READY)||!h1net_wifi_connected()||(bits&WIFI_DOWN)){
        error=join_error(reason,!(bits&WIFI_DOWN));outcome=H1WIFI_FAILED;
    }
    if(outcome==H1WIFI_SAVED){
        xSemaphoreTake(wifi_lock,portMAX_DELAY);
        if(xEventGroupGetBits(wifi_events)&WIFI_CANCEL)outcome=H1WIFI_CANCELLED;else committing=true;
        xSemaphoreGive(wifi_lock);
        if(outcome==H1WIFI_SAVED&&store_credentials(&candidate)!=ESP_OK){error=H1WIFI_ERR_STORAGE;outcome=H1WIFI_FAILED;}
    }
    if(outcome!=H1WIFI_SAVED)disconnect_station();
    xSemaphoreTake(wifi_lock,portMAX_DELAY);
    if(outcome==H1WIFI_SAVED)saved=candidate;
    settings.outcome=outcome;settings.error=error;settings.reason=reason;
    xSemaphoreGive(wifi_lock);h1net_secure_clear(&candidate,sizeof candidate);
    release_control(outcome!=H1WIFI_SAVED);
}
static void forget_network(void)
{
    esp_err_t err=store_credentials(NULL);
    if(err==ESP_OK){disconnect_station();xSemaphoreTake(wifi_lock,portMAX_DELAY);
        h1net_secure_clear(&saved,sizeof saved);settings.current[0]=0;xSemaphoreGive(wifi_lock);}
    xSemaphoreTake(wifi_lock,portMAX_DELAY);settings.outcome=err==ESP_OK?H1WIFI_IDLE:H1WIFI_FAILED;
    settings.error=err==ESP_OK?0:H1WIFI_ERR_STORAGE;settings.reason=0;xSemaphoreGive(wifi_lock);release_control(false);
}
static void wifi_control_task(void *unused)
{
    (void)unused;uint8_t command;
    for(;;)if(xQueueReceive(control_queue,&command,portMAX_DELAY)==pdTRUE){
        xTimerStop(retry_timer,0);
        if(command==H1NET_WIFI_SCAN)scan_networks();
        else if(command==H1NET_WIFI_CONNECT)connect_network();
        else if(command==H1NET_WIFI_FORGET)forget_network();
    }
}
uint16_t h1net_wifi_control(void *unused,uint8_t op,const uint8_t *data,unsigned length,uint8_t *out,unsigned *size)
{
    (void)unused;*size=0;
    if(op==H1NET_WIFI_STATUS){if(length)return H1NET_INVALID;h1wifi_status status;snapshot(&status);
        h1wifi_status_encode(out,&status);*size=H1WIFI_STATUS_BYTES;return H1NET_OK;}
    if(op==H1NET_WIFI_PAGE){
        if(length!=6)return H1NET_INVALID;
        unsigned first=h1net_u16(data+4);
        xSemaphoreTake(wifi_lock,portMAX_DELAY);
        if(h1net_u32(data)!=settings.scan_id||first>settings.scan_count){xSemaphoreGive(wifi_lock);return H1NET_INVALID;}
        unsigned count=settings.scan_state==H1WIFI_SCAN_DONE?settings.scan_count-first:0;if(count>H1WIFI_PAGE_MAX)count=H1WIFI_PAGE_MAX;
        h1net_put32(out,settings.scan_id);out[4]=(uint8_t)settings.scan_state;out[5]=(uint8_t)count;out[6]=(uint8_t)settings.scan_count;out[7]=0;
        for(unsigned i=0;i<count;i++)h1wifi_ap_encode(out+8+i*H1WIFI_AP_BYTES,access_points+first+i);
        xSemaphoreGive(wifi_lock);*size=8+count*H1WIFI_AP_BYTES;return H1NET_OK;
    }
    if(op==H1NET_WIFI_CANCEL){
        if(length!=4)return H1NET_INVALID;
        xSemaphoreTake(wifi_lock,portMAX_DELAY);
        if(h1net_u32(data)!=settings.operation){xSemaphoreGive(wifi_lock);return H1NET_INVALID;}
        if(committing){xSemaphoreGive(wifi_lock);return H1NET_BUSY;}
        if(managed&&settings.outcome==H1WIFI_JOINING)xEventGroupSetBits(wifi_events,WIFI_CANCEL);
        xSemaphoreGive(wifi_lock);return H1NET_OK;
    }
    if(op!=H1NET_WIFI_SCAN&&op!=H1NET_WIFI_CONNECT&&op!=H1NET_WIFI_FORGET)return H1NET_INVALID;
    if(op!=H1NET_WIFI_CONNECT&&length)return H1NET_INVALID;
    if(op==H1NET_WIFI_CONNECT&&(length<4||length!=4u+data[0]+data[1]||
       !h1wifi_credentials_valid(data+4,data[0],data+4+data[0],data[1],data[2],data[3])))return H1NET_INVALID;
    xSemaphoreTake(wifi_lock,portMAX_DELAY);
    if(managed){xSemaphoreGive(wifi_lock);return H1NET_BUSY;}
    if(op==H1NET_WIFI_CONNECT){
        memset(&pending,0,sizeof pending);pending.version=1;memcpy(pending.ssid,data+4,data[0]);pending_auth=data[2];
        if(data[3]){
            if(strcmp(pending.ssid,saved.ssid)||!saved.password[0]){h1net_secure_clear(&pending,sizeof pending);xSemaphoreGive(wifi_lock);return H1NET_INVALID;}
            memcpy(pending.password,saved.password,sizeof pending.password);
        }else memcpy(pending.password,data+4+data[0],data[1]);
        settings.operation++;if(!settings.operation)settings.operation++;
        settings.outcome=H1WIFI_JOINING;settings.error=settings.reason=0;memcpy(settings.current,pending.ssid,sizeof settings.current);
        xEventGroupClearBits(wifi_events,WIFI_CANCEL);
    }else if(op==H1NET_WIFI_SCAN){settings.scan_id++;if(!settings.scan_id)settings.scan_id++;
        settings.scan_state=H1WIFI_SCANNING;settings.scan_count=0;
    }else {settings.operation++;if(!settings.operation)settings.operation++;
        settings.outcome=H1WIFI_FORGETTING;settings.error=settings.reason=0;}
    managed=true;uint8_t command=op;
    if(xQueueSend(control_queue,&command,0)!=pdTRUE){
        managed=false;h1net_secure_clear(&pending,sizeof pending);
        if(op==H1NET_WIFI_SCAN)settings.scan_state=H1WIFI_SCAN_FAILED;
        else {settings.outcome=H1WIFI_FAILED;settings.error=H1WIFI_ERR_DRIVER;}
        xSemaphoreGive(wifi_lock);return H1NET_INTERNAL;
    }
    h1net_put32(out,op==H1NET_WIFI_SCAN?settings.scan_id:settings.operation);*size=4;
    xSemaphoreGive(wifi_lock);return H1NET_OK;
}
static void config_reply(bool ok,const char *message)
{
    cJSON *reply=cJSON_CreateObject();if(!reply)return;
    cJSON_AddStringToObject(reply,"protocol","H1CFG/1");cJSON_AddBoolToObject(reply,"ok",ok);
    cJSON_AddStringToObject(reply,"message",message);
    char *text=cJSON_PrintUnformatted(reply);
    if(text){printf("\nH1CFG/1 %s\n",text);fflush(stdout);free(text);}cJSON_Delete(reply);
}
static void info_reply(void)
{
    uint32_t status[6];h1net_wifi_status(status);
    cJSON *reply=cJSON_CreateObject();if(!reply)return;
    cJSON_AddStringToObject(reply,"protocol","H1CFG/1");cJSON_AddBoolToObject(reply,"ok",true);
    cJSON_AddStringToObject(reply,"firmware","h1-wifi/0.4.5");
    cJSON_AddStringToObject(reply,"target","esp32s3");
    h1wifi_status wireless;snapshot(&wireless);
    cJSON_AddStringToObject(reply,"ssid",wireless.saved);
    cJSON_AddNumberToObject(reply,"wifi_setup_outcome",wireless.outcome);
    cJSON_AddNumberToObject(reply,"wifi_setup_error",wireless.error);
    cJSON_AddBoolToObject(reply,"wifi_connected",status[1]!=0);
    cJSON_AddBoolToObject(reply,"usb_connected",h1net_usb_connected());
    h1net_usb_diagnostics usb;h1net_usb_get_diagnostics(&usb);
    cJSON_AddBoolToObject(reply,"usb_host_ready",usb.host_ready);
    cJSON_AddNumberToObject(reply,"usb_devices",usb.devices);
    cJSON_AddStringToObject(reply,"usb_cdc_stage",usb.stage);
    cJSON_AddStringToObject(reply,"usb_last_error",esp_err_to_name(usb.last_error));
    cJSON *performance=cJSON_AddObjectToObject(reply,"usb_performance");
    if(performance){
        cJSON_AddNumberToObject(performance,"rpc_frames",usb.rpc_frames);
        cJSON_AddNumberToObject(performance,"request_bytes",usb.request_bytes);
        cJSON_AddNumberToObject(performance,"response_bytes",usb.response_bytes);
        cJSON_AddNumberToObject(performance,"http_reads",usb.http_reads);
        cJSON_AddNumberToObject(performance,"tx_errors",usb.tx_errors);
        cJSON_AddNumberToObject(performance,"tx_wait_us",(double)usb.tx_wait_us);
        cJSON_AddNumberToObject(performance,"tx_wait_us_max",usb.tx_wait_us_max);
        cJSON_AddNumberToObject(performance,"dispatch_us",(double)usb.dispatch_us);
        cJSON_AddNumberToObject(performance,"bad_crc",usb.parser_bad_crc);
        cJSON_AddNumberToObject(performance,"bad_headers",usb.parser_bad_headers);
    }
    h1net_info jobs[H1NET_MAX_JOBS];unsigned job_count=h1net_http_get_diagnostics(jobs);
    cJSON *http=cJSON_AddArrayToObject(reply,"http_jobs");
    for(unsigned i=0;http&&i<job_count;i++){
        cJSON *item=cJSON_CreateObject();if(!item)break;cJSON_AddItemToArray(http,item);
        cJSON_AddNumberToObject(item,"id",jobs[i].job);cJSON_AddNumberToObject(item,"state",jobs[i].state);
        cJSON_AddNumberToObject(item,"http_status",jobs[i].http_status);cJSON_AddNumberToObject(item,"content_length",jobs[i].content_length);
        cJSON_AddNumberToObject(item,"received",jobs[i].received);cJSON_AddNumberToObject(item,"delivered",jobs[i].delivered);
        cJSON_AddNumberToObject(item,"buffered",jobs[i].buffered);cJSON_AddNumberToObject(item,"error",jobs[i].error);
    }
    cJSON *bus=cJSON_AddObjectToObject(reply,"usb_bus");
    if(bus){
        cJSON_AddBoolToObject(bus,"phy_otg",usb.phy_otg);cJSON_AddBoolToObject(bus,"host_mode",usb.host_mode);
        cJSON_AddBoolToObject(bus,"controller_port_power",usb.port_powered);
        cJSON_AddBoolToObject(bus,"port_attached",usb.port_attached);cJSON_AddNumberToObject(bus,"line_state",usb.line_state);
        cJSON_AddNumberToObject(bus,"raw_hprt",usb.raw_hprt);cJSON_AddNumberToObject(bus,"raw_wrap",usb.raw_wrap);
        cJSON_AddNumberToObject(bus,"raw_rtc",usb.raw_rtc);
    }
    cJSON_AddBoolToObject(reply,"clock_ready",time(NULL)>=1704067200);
    char ip[24];snprintf(ip,sizeof ip,"%lu.%lu.%lu.%lu",(unsigned long)(status[2]&255),
                        (unsigned long)((status[2]>>8)&255),(unsigned long)((status[2]>>16)&255),
                        (unsigned long)(status[2]>>24));
    cJSON_AddStringToObject(reply,"ip",ip);cJSON_AddNumberToObject(reply,"rssi",(int32_t)status[3]);
    cJSON_AddNumberToObject(reply,"free_heap",status[4]);
    cJSON_AddNumberToObject(reply,"internal_free_heap",heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
    cJSON_AddNumberToObject(reply,"internal_largest_free_block",heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
    char *text=cJSON_PrintUnformatted(reply);
    if(text){printf("\nH1CFG/1 %s\n",text);fflush(stdout);free(text);}cJSON_Delete(reply);
}
static void configure(const char *text)
{
    const char *end=NULL;cJSON *request=cJSON_ParseWithOpts(text,&end,true);
    if(!request){config_reply(false,"invalid JSON");return;}
    const cJSON *ssid=cJSON_GetObjectItemCaseSensitive(request,"ssid");
    const cJSON *password=cJSON_GetObjectItemCaseSensitive(request,"password");
    if(!cJSON_IsString(ssid) || !cJSON_IsString(password) || !ssid->valuestring[0] ||
       strlen(ssid->valuestring)>32 || strlen(password->valuestring)>63 ||
       (password->valuestring[0] && strlen(password->valuestring)<8)){
        cJSON_Delete(request);config_reply(false,"SSID: 1..32 UTF-8 bytes; password: empty or 8..63 bytes");return;
    }
    credentials next={.version=1};
    memcpy(next.ssid,ssid->valuestring,strlen(ssid->valuestring));
    memcpy(next.password,password->valuestring,strlen(password->valuestring));
    cJSON_Delete(request);xSemaphoreTake(wifi_lock,portMAX_DELAY);
    if(managed){xSemaphoreGive(wifi_lock);h1net_secure_clear(&next,sizeof next);config_reply(false,"Wi-Fi operation busy");return;}
    managed=true;xSemaphoreGive(wifi_lock);xTimerStop(retry_timer,0);
    esp_err_t err=store_credentials(&next);h1net_secure_clear(&next,sizeof next);
    if(err!=ESP_OK){release_control(false);config_reply(false,"could not save Wi-Fi settings");return;}
    config_reply(true,"saved; restarting");vTaskDelay(pdMS_TO_TICKS(300));esp_restart();
}
void h1net_config_task(void *unused)
{
    (void)unused;char line[1024];unsigned used=0;bool overflow=false;
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0,4096,0,0,NULL,0));
    for(;;){
        uint8_t bytes[256];int n=uart_read_bytes(UART_NUM_0,bytes,1,pdMS_TO_TICKS(100));
        if(n!=1)continue;
        size_t queued=0;ESP_ERROR_CHECK(uart_get_buffered_data_len(UART_NUM_0,&queued));
        if(queued>sizeof bytes-1)queued=sizeof bytes-1;
        if(queued){int extra=uart_read_bytes(UART_NUM_0,bytes+1,queued,0);if(extra>0)n+=extra;}
        for(int i=0;i<n;i++){
        uint8_t b=bytes[i];
        if(b=='\r')continue;
        if(b=='\n'){
            if(overflow){config_reply(false,"command too long");h1net_secure_clear(line,sizeof line);used=0;overflow=false;continue;}
            line[used]=0;
            if(!strcmp(line,"H1CFG/1 INFO"))info_reply();
            else if(!strcmp(line,"H1CFG/1 DBG ON")||!strcmp(line,"H1CFG/1 DBG OFF")){
                if(!h1net_usb_bridge_submit(line[12]=='O'&&line[13]=='N'?H1_BRIDGE_OPEN:H1_BRIDGE_CLOSE,0,NULL,0)){
                    printf("\nH1DBG/1 ERROR queue\n");fflush(stdout);
                }
            }
            else if(!strncmp(line,"H1CFG/1 DBG DATA ",17)){
                char *end=NULL;unsigned long sequence=strtoul(line+17,&end,10);
                uint8_t bytes[528];size_t count=0;
                if(end==line+17||*end!=' '||
                   mbedtls_base64_decode(bytes,sizeof bytes,&count,(const unsigned char *)end+1,strlen(end+1))||
                   !count||!h1net_usb_bridge_submit(H1_BRIDGE_DATA,(uint32_t)sequence,bytes,(unsigned)count)){
                    printf("\nH1DBG/1 ERROR data\n");fflush(stdout);
                }
            }
            else if(!strncmp(line,"H1CFG/1 SET ",12))configure(line+12);
            else if(!strcmp(line,"H1CFG/1 CLEAR")){
                xSemaphoreTake(wifi_lock,portMAX_DELAY);bool busy=managed;if(!busy)managed=true;xSemaphoreGive(wifi_lock);
                if(busy)config_reply(false,"Wi-Fi operation busy");
                else if(store_credentials(NULL)!=ESP_OK){release_control(false);config_reply(false,"could not clear settings");}
                else {config_reply(true,"cleared; restarting");vTaskDelay(pdMS_TO_TICKS(300));esp_restart();}
            }
            h1net_secure_clear(line,sizeof line);used=0;continue;
        }
        if(!b){overflow=true;continue;}
        if(used+1<sizeof line && !overflow)line[used++]=(char)b;else overflow=true;
        }
        h1net_secure_clear(bytes,sizeof bytes);
    }
}
