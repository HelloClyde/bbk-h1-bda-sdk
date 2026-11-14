idf_component_register(
    SRCS "main.c" "h1net_http.c" "h1net_wifi.c" "h1net_usb.c" "h1net_audio.c"
         "../../common/h1net_wire.c" "../../common/h1net_server.c"
         "../../common/h1net_gzip.c"
         "../../common/h1net_wifi_config.c"
    INCLUDE_DIRS "." "../../common"
    REQUIRES esp_wifi esp_event esp_netif nvs_flash esp_http_client esp_timer
             mbedtls json esp_driver_uart vfs esp_psram usb_host_cdc_acm h1_zlib esp_audio_codec
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
