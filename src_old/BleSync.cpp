#include "BleSync.h"

#include <esp_timer.h>

BleMutex::BleMutex()
    : handle_(xSemaphoreCreateMutex())
{
}

BleMutex::~BleMutex()
{
    if (handle_)
    {
        vSemaphoreDelete(handle_);
        handle_ = nullptr;
    }
}

void BleMutex::lock() const
{
    if (handle_)
    {
        xSemaphoreTake(handle_, portMAX_DELAY);
    }
}

bool BleMutex::tryLock(uint32_t timeoutMs) const
{
    if (!handle_)
    {
        return false;
    }

    return xSemaphoreTake(handle_, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

void BleMutex::unlock() const
{
    if (handle_)
    {
        xSemaphoreGive(handle_);
    }
}

BleLock::BleLock(const BleMutex &mutex)
    : mutex_(mutex)
{
    mutex_.lock();
}

BleLock::~BleLock()
{
    mutex_.unlock();
}

BleSignal::BleSignal()
    : handle_(xSemaphoreCreateBinary())
{
}

BleSignal::~BleSignal()
{
    if (handle_)
    {
        vSemaphoreDelete(handle_);
        handle_ = nullptr;
    }
}

void BleSignal::notify()
{
    if (!handle_)
    {
        return;
    }

    // Bluedroid delivers GAP/GATTC events on the BTC task, which is an ordinary
    // FreeRTOS task rather than an interrupt, so the task-level give is both
    // correct and non-blocking: giving an already-given binary semaphore simply
    // returns pdFALSE.
    xSemaphoreGive(handle_);
}

bool BleSignal::wait(uint32_t timeoutMs)
{
    if (!handle_)
    {
        return false;
    }

    return xSemaphoreTake(handle_, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

void BleSignal::clear()
{
    if (!handle_)
    {
        return;
    }

    // Drain every pending count, not just one, in case an operation produced
    // more than one completion event.
    while (xSemaphoreTake(handle_, static_cast<TickType_t>(0)) == pdTRUE)
    {
    }
}
