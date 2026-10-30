#ifndef BLE_DEVICE_H
#define BLE_DEVICE_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <string>

/**
 * Stack-neutral description of a device seen in an advertisement.
 *
 * The NimBLE based classes hand a `NimBLEAdvertisedDevice` to the application,
 * which forces every consumer (main.cpp, the allow-list check, the client) to
 * depend on a specific BLE stack. This struct carries only what the project
 * actually uses - address, address type, RSSI, advertised name, connectability
 * and the advertised service UUIDs - so the scanner and the client can be
 * swapped independently of the code that consumes them.
 *
 * The scanner stays agnostic about *which* service the application cares
 * about: it records the advertised 128-bit UUIDs and the client asks whether a
 * particular one is present. That keeps the MIDI knowledge in the client,
 * where the allow-list already lives.
 *
 * It is a plain value type: no pointers into a stack-owned object, so it stays
 * valid after the scan callback that produced it has returned.
 */
struct BleAdvertisedDevice
{
    static const size_t kAddressLength = 6;
    static const size_t kUuidLength = 16;
    static const size_t kMaxNameLength = 31;
    static const size_t kMaxServiceUuids = 4;

    uint8_t address[kAddressLength];
    uint8_t addressType;
    int8_t rssi;
    bool connectable;
    char name[kMaxNameLength + 1];

    /** Advertised 128-bit service UUIDs, most-significant byte first. */
    uint8_t serviceUuids[kMaxServiceUuids][kUuidLength];
    uint8_t serviceUuidCount;

    BleAdvertisedDevice()
        : addressType(0)
        , rssi(0)
        , connectable(false)
        , serviceUuidCount(0)
    {
        memset(address, 0, sizeof(address));
        memset(serviceUuids, 0, sizeof(serviceUuids));
        name[0] = '\0';
    }

    bool sameAddress(const BleAdvertisedDevice &other) const
    {
        return memcmp(address, other.address, sizeof(address)) == 0;
    }

    /**
     * Mirrors NimBLEAdvertisedDevice::getName(), which returns a std::string
     * and is compared with std::string::compare() by callers.
     */
    std::string getName() const { return std::string(name); }

    /**
     * Mirrors NimBLEAdvertisedDevice::isConnectable().
     */
    bool isConnectable() const { return connectable; }

    /**
     * Mirrors NimBLEAdvertisedDevice::isAdvertisingService().
     *
     * @param uuidMsbFirst a 128-bit UUID in the canonical text order, e.g. the
     *                     result of BleAdvDataParser::uuid128FromText().
     */
    bool isAdvertisingService(const uint8_t uuidMsbFirst[kUuidLength]) const
    {
        for (uint8_t i = 0; i < serviceUuidCount; ++i)
        {
            if (memcmp(serviceUuids[i], uuidMsbFirst, kUuidLength) == 0)
            {
                return true;
            }
        }

        return false;
    }
};

#endif // BLE_DEVICE_H
