#ifndef I_BLE_SCANNER_H
#define I_BLE_SCANNER_H

#include <stddef.h>

#include "BleDevice.h"

/**
 * What the application needs from a scanner, with no reference to any BLE
 * stack.
 *
 * PowerManager and main.cpp only ever ask these four questions, so depending
 * on this interface instead of a concrete class means a different stack can be
 * dropped in without touching them (Open/Closed, Dependency Inversion).
 *
 * The method names deliberately match the existing BleScanner so switching
 * over is a type-name change and nothing else.
 */
class IBleScanner
{
public:
    virtual ~IBleScanner() {}

    /**
     * Start discovery. Returns false when the stack could not be brought up or
     * the controller refused the command.
     */
    virtual bool StartScanning() = 0;

    /**
     * Stop discovery. Returns false when nothing was running or the controller
     * refused the command.
     */
    virtual bool StopScanning() = 0;

    /**
     * True while discovery is in progress.
     */
    virtual bool isScanning() = 0;

    /**
     * The device at @p index, or nullptr once the list is exhausted. Callers
     * iterate from 0 with index++ until they get nullptr, which is exactly how
     * main.cpp uses the NimBLE based scanner.
     */
    virtual const BleAdvertisedDevice *getDevice(size_t index) const = 0;
};

#endif // I_BLE_SCANNER_H
