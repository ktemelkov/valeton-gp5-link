#pragma once

inline int mockErrors = 0;
#define ESP_LOGE(...) (++mockErrors)
#define ESP_LOGW(...) (++mockErrors)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOG_BUFFER_HEX(...) ((void)0)
