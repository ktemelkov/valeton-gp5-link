#ifndef __AUTO_SLEEP_H
#define __AUTO_SLEEP_H

#include <stdint.h>
#include <stddef.h>

/**
 *
 */
class PowerManager
{
public:
    PowerManager();

    void triggerWatchdog();

    bool shouldSleep();
    void doSleep(const uint8_t* wakePins, size_t wakePinCount);

private:
    uint32_t lastPressMs = 0;
    uint32_t lastReleaseMs = 0;
};

#endif // __AUTO_SLEEP_H