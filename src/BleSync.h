#ifndef BLE_SYNC_H
#define BLE_SYNC_H

#include <stdint.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

/**
 * A non-recursive mutual exclusion lock.
 *
 * Every object that is shared between the application task (app_main /
 * loopTask) and the Bluetooth host task (BTU/BTC), where all Bluedroid GAP
 * and GATTC callbacks run, has to be guarded by something. FreeRTOS semaphores
 * are used directly instead of std::mutex because they are available in every
 * context, work before the C++ runtime is fully up, and support timeouts.
 */
class BleMutex
{
public:
    BleMutex();
    ~BleMutex();

    BleMutex(const BleMutex &) = delete;
    BleMutex &operator=(const BleMutex &) = delete;

    void lock() const;
    void unlock() const;
    bool tryLock(uint32_t timeoutMs) const;

private:
    SemaphoreHandle_t handle_;
};

/**
 * RAII helper for BleMutex.
 */
class BleLock
{
public:
    explicit BleLock(const BleMutex &mutex);
    ~BleLock();

    BleLock(const BleLock &) = delete;
    BleLock &operator=(const BleLock &) = delete;

private:
    const BleMutex &mutex_;
};

/**
 * A one-shot level signal used to turn an asynchronous Bluedroid command plus
 * its completion event into a bounded blocking wait.
 *
 * notify() is called from the Bluetooth host task, wait() from the
 * application task. clear() drops a signal left over from an earlier
 * operation so a wait never returns because of a stale event.
 */
class BleSignal
{
public:
    BleSignal();
    ~BleSignal();

    BleSignal(const BleSignal &) = delete;
    BleSignal &operator=(const BleSignal &) = delete;

    void notify();
    bool wait(uint32_t timeoutMs);
    void clear();

private:
    SemaphoreHandle_t handle_;
};

#endif // BLE_SYNC_H
