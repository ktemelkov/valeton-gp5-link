#pragma once
#include "ble_hs_mbuf.h"
#include "NimBLEDevice.h"
inline int mockNotifyCode = 0;
inline std::vector<std::vector<uint8_t>> mockSentPackets;
inline int ble_gatts_notify_custom(uint16_t handle, uint16_t attribute, os_mbuf *buffer)
{
    std::unique_ptr<os_mbuf> owned(buffer);
    assert(mockLocksHeld == 0);
    assert(attribute == 42 || attribute == 43);
    if (mockNotifyCode != 0) return mockNotifyCode;
    auto &characteristic = attribute == 43 ? NimBLEDevice::server.configurationService.characteristic :
                                            NimBLEDevice::server.service.characteristic;
    if (!characteristic.notify(buffer->bytes.data(), buffer->bytes.size(), handle)) return 7;
    mockSentPackets.push_back(buffer->bytes);
    return 0;
}
