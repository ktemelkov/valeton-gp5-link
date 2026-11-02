#include "BleAdvDataParser.h"

#include <ctype.h>
#include <stdio.h>

// Advertising data types, see the Bluetooth Core Spec "Assigned Numbers" GAP
// AD types. Values match esp_ble_ad_type_t in esp_gap_ble_api.h; they are
// repeated here so the parser does not depend on a stack header.
static const uint8_t AD_TYPE_FLAG = 0x01;
static const uint8_t AD_TYPE_128SRV_PART = 0x06;
static const uint8_t AD_TYPE_128SRV_CMPL = 0x07;
static const uint8_t AD_TYPE_NAME_SHORT = 0x08;
static const uint8_t AD_TYPE_NAME_CMPL = 0x09;

// LE General/Undirected discoverable flag, which is what makes a device a
// connectable one for our purposes.
static const uint8_t FLAG_LE_GENERAL_DISCOVERABLE = 0x02;
static const uint8_t FLAG_LE_SIMULTANEOUS_MASK = 0x18;
static const uint8_t FLAG_LE_SIMULTANEOUS_BOTH = 0x18;

// Flags that already carry a connectable meaning on their own.
static const uint8_t AD_TYPE_DIRECT_ADV = 0x15;

bool BleAdvDataParser::uuid128FromText(const char *text, uint8_t out[16])
{
    if (!text)
    {
        return false;
    }

    int written = 0;
    const char *cursor = text;

    while (*cursor && written < 16)
    {
        if (*cursor == '-')
        {
            ++cursor;
            continue;
        }

        if (!isxdigit(static_cast<unsigned char>(cursor[0])) ||
            !isxdigit(static_cast<unsigned char>(cursor[1])))
        {
            return false;
        }

        unsigned byte = 0;
        if (sscanf(cursor, "%02hhx", &byte) != 1)
        {
            return false;
        }

        out[written++] = static_cast<uint8_t>(byte);
        cursor += 2;
    }

    return written == 16;
}

bool BleAdvDataParser::matchesUuid128(const uint8_t *field, uint8_t fieldLength,
                                      const uint8_t uuidMsbFirst[16])
{
    // 128-bit UUIDs are carried least-significant byte first on air, the
    // canonical text form is most-significant byte first, so compare from the
    // opposite ends.
    if (fieldLength < 16)
    {
        return false;
    }

    for (int i = 0; i < 16; ++i)
    {
        if (field[i] != uuidMsbFirst[15 - i])
        {
            return false;
        }
    }

    return true;
}

bool BleAdvDataParser::advertisesService(const uint8_t *data, uint8_t length,
                                         const uint8_t uuidMsbFirst[16])
{
    if (!data || !uuidMsbFirst)
    {
        return false;
    }

    uint8_t offset = 0;

    while (offset < length)
    {
        const uint8_t fieldLength = data[offset];
        if (fieldLength == 0 || offset + 1 + fieldLength > length)
        {
            break;
        }

        const uint8_t type = data[offset + 1];
        const uint8_t *field = &data[offset + 2];
        const uint8_t fieldSize = fieldLength - 1;

        if (type == AD_TYPE_128SRV_PART || type == AD_TYPE_128SRV_CMPL)
        {
            // The field may hold several consecutive 16-byte UUIDs.
            for (uint8_t pos = 0; pos + 16 <= fieldSize; pos += 16)
            {
                if (matchesUuid128(field + pos, 16, uuidMsbFirst))
                {
                    return true;
                }
            }
        }

        offset += fieldLength + 1;
    }

    return false;
}

void BleAdvDataParser::parse(const uint8_t *data, uint8_t length,
                             BleAdvertisedDevice &device)
{
    if (!data || length == 0)
    {
        return;
    }

    uint8_t offset = 0;

    while (offset < length)
    {
        const uint8_t fieldLength = data[offset];
        if (fieldLength == 0 || offset + 1 + fieldLength > length)
        {
            break;
        }

        const uint8_t type = data[offset + 1];
        const uint8_t *field = &data[offset + 2];
        const uint8_t fieldSize = fieldLength - 1;

        switch (type)
        {
        case AD_TYPE_FLAG:
            if (fieldSize >= 1 &&
                (field[0] & FLAG_LE_GENERAL_DISCOVERABLE))
            {
                device.connectable = true;
            }
            break;

        case AD_TYPE_NAME_CMPL:
        {
            const uint8_t copyLength =
                fieldSize > BleAdvertisedDevice::kMaxNameLength
                    ? BleAdvertisedDevice::kMaxNameLength
                    : fieldSize;
            memcpy(device.name, field, copyLength);
            device.name[copyLength] = '\0';
            break;
        }

        case AD_TYPE_NAME_SHORT:
            // A short name is only useful when nothing better has been seen:
            // the complete name arrives in the scan response.
            if (device.name[0] == '\0')
            {
                const uint8_t copyLength =
                    fieldSize > BleAdvertisedDevice::kMaxNameLength
                        ? BleAdvertisedDevice::kMaxNameLength
                        : fieldSize;
                memcpy(device.name, field, copyLength);
                device.name[copyLength] = '\0';
            }
            break;

        case AD_TYPE_128SRV_PART:
        case AD_TYPE_128SRV_CMPL:
            // The field may hold several consecutive 16-byte UUIDs, each one
            // carried least-significant byte first.
            for (uint8_t pos = 0; pos + 16 <= fieldSize; pos += 16)
            {
                addServiceUuid(device, field + pos);
            }
            break;

        default:
            break;
        }

        offset += fieldLength + 1;
    }
}

void BleAdvDataParser::addServiceUuid(BleAdvertisedDevice &device,
                                      const uint8_t field[16])
{
    if (device.serviceUuidCount >= BleAdvertisedDevice::kMaxServiceUuids)
    {
        return;
    }

    uint8_t *slot = device.serviceUuids[device.serviceUuidCount];

    for (int i = 0; i < 16; ++i)
    {
        slot[i] = field[15 - i];
    }

    for (uint8_t i = 0; i < device.serviceUuidCount; ++i)
    {
        if (memcmp(device.serviceUuids[i], slot,
                   BleAdvertisedDevice::kUuidLength) == 0)
        {
            return;
        }
    }

    device.serviceUuidCount++;
}
