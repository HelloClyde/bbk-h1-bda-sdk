/* SPDX-License-Identifier: Apache-2.0
 * H1 ESP32 contributors. SDK import with copyright-holder authorization. */
#include "h1net_platform.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_LOGI("h1net","H1NET/1 firmware 0.4.4; H1 Wi-Fi setup + HTTP/audio; TLS PSRAM allocation; PSRAM=%lu bytes",
             (unsigned long)(esp_psram_is_initialized()?esp_psram_get_size():0));
    h1net_wifi_init();h1net_http_init();
    ESP_ERROR_CHECK(xTaskCreate(h1net_config_task,"h1_config",6144,NULL,4,NULL)==pdPASS?ESP_OK:ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreate(h1net_usb_task,"h1_usb",8192,NULL,6,NULL)==pdPASS?ESP_OK:ESP_ERR_NO_MEM);
}
