#pragma once
#include <vector>
#include <cstdint>
#include <memory>
constexpr int BLE_HS_EAGAIN = 1, BLE_HS_ENOMEM = 6, BLE_HS_EBUSY = 15;
struct os_mbuf { std::vector<uint8_t> bytes; };
inline int mockAllocationFailures = 0;
inline os_mbuf *ble_hs_mbuf_from_flat(const void *data, uint16_t length)
{
    if (mockAllocationFailures > 0) { --mockAllocationFailures; return nullptr; }
    const auto *bytes = static_cast<const uint8_t*>(data);
    return new os_mbuf{{bytes, bytes + length}};
}
