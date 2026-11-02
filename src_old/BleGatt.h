#ifndef BLE_GATT_H
#define BLE_GATT_H

#include <stdint.h>

#include <esp_gattc_api.h>

/**
 * Reads the GATT database that Bluedroid cached for a connection.
 *
 * After service discovery the client does not own any object representing a
 * service or characteristic - the data lives in the stack's cache and is read
 * back through the esp_ble_gattc_get_*() calls. Wrapping that in one place
 * keeps the client free of the cache's paging and UUID plumbing, and gives the
 * client a single testable question to ask: "where is the MIDI
 * characteristic, and what is its CCCD handle?".
 */
class BleGattCache
{
public:
    struct Characteristic
    {
        uint16_t serviceStartHandle;
        uint16_t serviceEndHandle;
        uint16_t valueHandle;
        uint16_t cccdHandle;   // 0 when the characteristic has no CCCD
        uint8_t properties;
        uint16_t mtu;

        Characteristic()
            : serviceStartHandle(0)
            , serviceEndHandle(0)
            , valueHandle(0)
            , cccdHandle(0)
            , properties(0)
            , mtu(0)
        {
        }

        bool found() const { return valueHandle != 0; }
        bool canNotify() const { return (properties & ESP_GATT_CHAR_PROP_BIT_NOTIFY) != 0; }
        bool canWriteNoResponse() const
        {
            return (properties & ESP_GATT_CHAR_PROP_BIT_WRITE_NR) != 0;
        }
    };

    /**
     * Look up a 128-bit characteristic inside a 128-bit service and resolve its
     * Client Characteristic Configuration descriptor.
     *
     * @param mtu  the negotiated MTU for the connection, carried along so the
     *             caller can size writes without another round trip.
     * @return a Characteristic whose found() is false when either the service
     *         or the characteristic is not in the cache.
     */
    static Characteristic findCharacteristic(esp_gatt_if_t gattcIf,
                                             uint16_t connId,
                                             const uint8_t serviceUuidMsbFirst[16],
                                             const uint8_t charUuidMsbFirst[16],
                                             uint16_t mtu);

    /**
     * Build a 128-bit esp_bt_uuid_t from the most-significant-byte-first form
     * used in the rest of this project.
     */
    static esp_bt_uuid_t uuid128(const uint8_t msbFirst[16]);

private:
    // Each helper pages through the cache in kQueryBatch sized chunks.
    static bool findService(esp_gatt_if_t gattcIf, uint16_t connId,
                            const uint8_t serviceUuidMsbFirst[16],
                            uint16_t &startHandle, uint16_t &endHandle);

    static bool findCharacteristicHandles(esp_gatt_if_t gattcIf,
                                          uint16_t connId,
                                          uint16_t serviceStart,
                                          uint16_t serviceEnd,
                                          const uint8_t charUuidMsbFirst[16],
                                          uint16_t &valueHandle,
                                          uint8_t &properties);

    static bool findCccd(esp_gatt_if_t gattcIf, uint16_t connId,
                         uint16_t charHandle, uint16_t &cccdHandle);
};

#endif // BLE_GATT_H
