#pragma once
#include "FreeRTOS.h"
#include <cstring>
#include <deque>
#include <memory>
#include <vector>

struct MockQueue
{
    size_t capacity, itemSize;
    std::deque<std::vector<uint8_t>> items;
};
using QueueHandle_t = MockQueue*;
inline std::vector<std::unique_ptr<MockQueue>> mockQueues;
inline QueueHandle_t xQueueCreate(size_t count, size_t size)
{
    mockQueues.emplace_back(new MockQueue{count, size, {}});
    return mockQueues.back().get();
}
inline int xQueueSend(QueueHandle_t queue, const void *data, uint32_t)
{
    if (queue->items.size() == queue->capacity) return 0;
    const auto *bytes = static_cast<const uint8_t*>(data);
    queue->items.emplace_back(bytes, bytes + queue->itemSize);
    return pdTRUE;
}
inline int xQueuePeek(QueueHandle_t queue, void *data, uint32_t)
{
    if (queue->items.empty()) return 0;
    std::memcpy(data, queue->items.front().data(), queue->itemSize);
    return pdTRUE;
}
inline int xQueueReceive(QueueHandle_t queue, void *data, uint32_t ticks)
{
    if (xQueuePeek(queue, data, ticks) != pdTRUE) return 0;
    queue->items.pop_front();
    return pdTRUE;
}
