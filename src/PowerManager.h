#ifndef __AUTO_SLEEP_H
#define __AUTO_SLEEP_H

#include <stdint.h>
#include <buttons.h>

/**
 *
 */
class PowerManager
{
public:
    PowerManager(Buttons& buttons);

    bool shouldSleep();
    void doSleep();

private:
    void configureButtonWakeupForDeepSleep();

private:
    Buttons &buttons;
    uint32_t lastPressMs = 0;
    uint32_t lastReleaseMs = 0;
};

#endif // __AUTO_SLEEP_H