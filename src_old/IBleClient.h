#ifndef I_BLE_CLIENT_H
#define I_BLE_CLIENT_H

#include <stddef.h>
#include <stdint.h>

#include "BleDevice.h"

/**
 * Callback for data pushed by the peripheral.
 *
 * The NimBLE based client took a NimBLE-specific signature, but subscribe()
 * has no callers in this firmware, so the interface is free to use a
 * stack-neutral one instead of leaking a NimBLE type into every consumer.
 */
typedef void (*BleNotifyCallback)(const uint8_t *data, uint16_t length,
                                  bool isNotify);

/**
 * What the application needs from the MIDI connection, with no reference to
 * any BLE stack.
 *
 * PowerManager and PresetManager hold a BleClient& today; pointing them at
 * this interface instead means the stack behind the connection can be replaced
 * without touching either of them (Dependency Inversion). Method names and
 * shapes match the existing BleClient so the swap is a type change only.
 */
class IBleClient
{
public:
    virtual ~IBleClient() {}

    /**
     * Connect to @p advDevice and resolve the MIDI characteristic. Returns true
     * only when the transport is ready to send.
     */
    virtual bool connect(const BleAdvertisedDevice *advDevice) = 0;

    /**
     * Tear the connection down. Safe to call when not connected.
     */
    virtual bool disconnect() = 0;

    /**
     * Subscribe to notifications from the characteristic that connect()
     * resolved.
     */
    virtual bool subscribe(BleNotifyCallback callback) = 0;

    /**
     * Write @p length bytes to the MIDI characteristic. Returns as soon as the
     * write has been handed to the controller - the MIDI transport is
     * write-without-response, so there is no acknowledgement to wait for.
     */
    virtual bool send(const uint8_t *data, size_t length) = 0;

    virtual bool isConnected() const = 0;

    /**
     * The allow-list: true when this advertisement is a GP-5 we may connect to.
     */
    virtual bool isAllowedDevice(const BleAdvertisedDevice *advDevice) const = 0;
};

#endif // I_BLE_CLIENT_H
