#ifndef BLE_ADV_DATA_PARSER_H
#define BLE_ADV_DATA_PARSER_H

#include <stdint.h>

#include "BleDevice.h"

/**
 * Parses the advertising payload of a received advertisement.
 *
 * Single responsibility: turn a raw AD structure list into the fields of a
 * BleAdvertisedDevice. Keeping this out of the scanner means the scanner only
 * has to deal with the GAP API and the device list, and the parsing can be
 * unit tested or reused without a Bluetooth stack present.
 */
class BleAdvDataParser
{
public:
    /**
     * Convert a canonical "03B80E5A-EDE8-4B33-A751-6CE34EC4C700" style UUID
     * into the 16 most-significant-byte-first representation.
     *
     * @return false when the string is not a 128-bit UUID.
     */
    static bool uuid128FromText(const char *text, uint8_t out[16]);

    /**
     * Fill in name, connectability and the advertised service UUIDs by walking
     * the AD structures of a single report.
     *
     * Scan responses are separate reports from the same advertiser, so the
     * fields already present in @p device are kept unless this report provides
     * a better value: the name is only overwritten by a complete name,
     * connectability is sticky, and service UUIDs accumulate.
     */
    static void parse(const uint8_t *data, uint8_t length,
                      BleAdvertisedDevice &device);

    /**
     * True when a 128-bit service UUID appears in a single report, which is how
     * the GP-5 is recognised without having to consult the accumulated list.
     */
    static bool advertisesService(const uint8_t *data, uint8_t length,
                                  const uint8_t uuidMsbFirst[16]);

private:
    static bool matchesUuid128(const uint8_t *field, uint8_t fieldLength,
                               const uint8_t uuidMsbFirst[16]);

    static void addServiceUuid(BleAdvertisedDevice &device,
                               const uint8_t field[16]);
};

#endif // BLE_ADV_DATA_PARSER_H
