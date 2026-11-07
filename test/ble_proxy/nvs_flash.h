#pragma once
#include "nvs.h"
inline esp_err_t nvs_flash_init() { return mockStorageFailure ? 2 : ESP_OK; }
