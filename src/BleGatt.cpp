#include "BleGatt.h"

#include <string.h>

#include <esp_gatt_defs.h>

static const uint16_t kQueryBatch = 8;

static bool sameUuid128(const esp_bt_uuid_t &candidate,
                        const uint8_t expected[16])
{
    if (candidate.len != ESP_UUID_LEN_128)
    {
        return false;
    }

    // The cache holds 128-bit UUIDs in the order they arrive from the air,
    // which is least-significant byte first, while the canonical text form the
    // project uses is most-significant byte first. Rather than depend on which
    // one a given build produces, accept either - a 128-bit UUID cannot collide
    // with its own reversal.
    bool forward = true;
    bool reversed = true;

    for (int i = 0; i < 16; ++i)
    {
        if (candidate.uuid.uuid128[i] != expected[i])
        {
            forward = false;
        }

        if (candidate.uuid.uuid128[i] != expected[15 - i])
        {
            reversed = false;
        }
    }

    return forward || reversed;
}

esp_bt_uuid_t BleGattCache::uuid128(const uint8_t msbFirst[16])
{
    esp_bt_uuid_t uuid;
    memset(&uuid, 0, sizeof(uuid));
    uuid.len = ESP_UUID_LEN_128;
    memcpy(uuid.uuid.uuid128, msbFirst, ESP_UUID_LEN_128);
    return uuid;
}

bool BleGattCache::findService(esp_gatt_if_t gattcIf, uint16_t connId,
                               const uint8_t serviceUuidMsbFirst[16],
                               uint16_t &startHandle, uint16_t &endHandle)
{
    startHandle = 0;
    endHandle = 0;

    esp_gattc_service_elem_t batch[kQueryBatch];
    uint16_t offset = 0;

    // Passing a NULL filter walks the whole cached database, which sidesteps
    // any question about how the stack compares 128-bit UUIDs internally.
    for (;;)
    {
        // On entry *count is the capacity of the caller's buffer, on exit it is
        // the total number of matching entries in the cache, so the number of
        // entries actually written has to be derived rather than trusted.
        uint16_t count = kQueryBatch;
        memset(batch, 0, sizeof(batch));

        esp_gatt_status_t status = esp_ble_gattc_get_service(
            gattcIf, connId, nullptr, batch, &count, offset);

        if (status != ESP_GATT_OK)
        {
            return false;
        }

        const uint16_t total = count;
        const uint16_t filled =
            (offset + kQueryBatch <= total) ? kQueryBatch
                                            : static_cast<uint16_t>(total - offset);

        for (uint16_t i = 0; i < filled; ++i)
        {
            if (sameUuid128(batch[i].uuid, serviceUuidMsbFirst))
            {
                startHandle = batch[i].start_handle;
                endHandle = batch[i].end_handle;
                return true;
            }
        }

        offset += filled;

        if (offset >= total)
        {
            return false;
        }
    }
}

bool BleGattCache::findCharacteristicHandles(
    esp_gatt_if_t gattcIf, uint16_t connId, uint16_t serviceStart,
    uint16_t serviceEnd, const uint8_t charUuidMsbFirst[16],
    uint16_t &valueHandle, uint8_t &properties)
{
    valueHandle = 0;
    properties = 0;

    esp_gattc_char_elem_t batch[kQueryBatch];
    uint16_t offset = 0;

    for (;;)
    {
        uint16_t count = kQueryBatch;
        memset(batch, 0, sizeof(batch));

        esp_gatt_status_t status = esp_ble_gattc_get_all_char(
            gattcIf, connId, serviceStart, serviceEnd, batch, &count, offset);

        if (status != ESP_GATT_OK)
        {
            return false;
        }

        const uint16_t total = count;
        const uint16_t filled =
            (offset + kQueryBatch <= total) ? kQueryBatch
                                            : static_cast<uint16_t>(total - offset);

        for (uint16_t i = 0; i < filled; ++i)
        {
            if (sameUuid128(batch[i].uuid, charUuidMsbFirst))
            {
                valueHandle = batch[i].char_handle;
                properties = static_cast<uint8_t>(batch[i].properties);
                return true;
            }
        }

        offset += filled;

        if (offset >= total)
        {
            return false;
        }
    }
}

bool BleGattCache::findCccd(esp_gatt_if_t gattcIf, uint16_t connId,
                            uint16_t charHandle, uint16_t &cccdHandle)
{
    cccdHandle = 0;

    esp_gattc_descr_elem_t batch[kQueryBatch];
    uint16_t offset = 0;

    for (;;)
    {
        uint16_t count = kQueryBatch;
        memset(batch, 0, sizeof(batch));

        esp_gatt_status_t status = esp_ble_gattc_get_all_descr(
            gattcIf, connId, charHandle, batch, &count, offset);

        if (status != ESP_GATT_OK || count == 0)
        {
            return false;
        }

        for (uint16_t i = 0; i < count; ++i)
        {
            if (batch[i].uuid.len == ESP_UUID_LEN_16 &&
                batch[i].uuid.uuid.uuid16 == ESP_GATT_UUID_CHAR_CLIENT_CONFIG)
            {
                cccdHandle = batch[i].handle;
                return true;
            }
        }

        offset += count;
    }
}

BleGattCache::Characteristic
BleGattCache::findCharacteristic(esp_gatt_if_t gattcIf, uint16_t connId,
                                 const uint8_t serviceUuidMsbFirst[16],
                                 const uint8_t charUuidMsbFirst[16],
                                 uint16_t mtu)
{
    Characteristic result;

    if (!findService(gattcIf, connId, serviceUuidMsbFirst,
                     result.serviceStartHandle, result.serviceEndHandle))
    {
        return result;
    }

    if (!findCharacteristicHandles(gattcIf, connId, result.serviceStartHandle,
                                   result.serviceEndHandle, charUuidMsbFirst,
                                   result.valueHandle, result.properties))
    {
        result.serviceStartHandle = 0;
        result.serviceEndHandle = 0;
        return result;
    }

    // A characteristic without a CCCD simply cannot be subscribed to; that is
    // not a failed lookup, since the MIDI transport only needs writes.
    findCccd(gattcIf, connId, result.valueHandle, result.cccdHandle);

    result.mtu = mtu;
    return result;
}
