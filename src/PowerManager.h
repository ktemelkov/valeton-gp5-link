#ifndef __AUTO_SLEEP_H
#define __AUTO_SLEEP_H

#include <Arduino.h>
#include <buttons.h>
#include "BleClient.h"
#include "BleScanner.h"

/**
 *
 */
class PowerManager
{
public:
    PowerManager(Buttons& buttons, BleClient& bleClient, BleScanner& bleScanner);

    void update();

private:
    void configureButtonWakeupForDeepSleep();

private:
    Buttons &buttons;
    BleClient& bleClient;
    BleScanner& bleScanner;
    uint32_t lastPressMs = 0;
    uint32_t lastReleaseMs = 0;

    bool shouldSleep = false;
};

#endif // __AUTO_SLEEP_H