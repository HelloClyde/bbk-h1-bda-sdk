#ifndef H1_WIFI_SETTINGS_CORE_H
#define H1_WIFI_SETTINGS_CORE_H
#include "h1net_wifi_config.h"
typedef struct {char value[64];unsigned length,maximum,upper,numeric,reveal;} wifi_editor;
void wifi_editor_clear(wifi_editor *,unsigned);
int wifi_editor_insert(wifi_editor *,unsigned);
void wifi_editor_delete(wifi_editor *);
unsigned wifi_editor_key(const wifi_editor *,unsigned);
const char *wifi_security_name(unsigned);
const char *wifi_error_message(unsigned);
#endif
