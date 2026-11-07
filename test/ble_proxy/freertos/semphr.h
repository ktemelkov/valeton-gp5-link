#pragma once

#include "FreeRTOS.h"
#include <memory>
#include <vector>

struct MockSemaphore { bool held = false; };
using SemaphoreHandle_t = MockSemaphore*;
inline std::vector<std::unique_ptr<MockSemaphore>> mockSemaphores;
inline int mockLocksHeld = 0;

inline SemaphoreHandle_t xSemaphoreCreateMutex()
{
    mockSemaphores.emplace_back(new MockSemaphore);
    return mockSemaphores.back().get();
}

inline int xSemaphoreTake(SemaphoreHandle_t semaphore, uint32_t)
{
    assert(semaphore != nullptr && !semaphore->held);
    semaphore->held = true;
    ++mockLocksHeld;
    return pdTRUE;
}

inline void xSemaphoreGive(SemaphoreHandle_t semaphore)
{
    assert(semaphore != nullptr && semaphore->held);
    semaphore->held = false;
    --mockLocksHeld;
}
