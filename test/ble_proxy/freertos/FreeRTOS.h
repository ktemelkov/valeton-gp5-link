#pragma once

#include <cassert>
#include <cstdint>

constexpr int pdTRUE = 1;
constexpr uint32_t portMAX_DELAY = UINT32_MAX;
#define pdMS_TO_TICKS(ms) (ms)
inline void vTaskDelay(uint32_t) {}
using TickType_t = uint32_t;
inline TickType_t mockTicks = 0;
inline TickType_t xTaskGetTickCount() { return mockTicks; }
