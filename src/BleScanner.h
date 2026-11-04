#ifndef BLE_SCANNER_H
#define BLE_SCANNER_H

#include <NimBLEDevice.h>
#include <NimBLEAddress.h>

#include "freertos/semphr.h"

/**
 * BLE scanner built on top of NimBLE.
 *
 * Provides methods to start and stop scanning, check scanning status, and
 * retrieve discovered devices.
 */
class BleScanner : public NimBLEScanCallbacks
{
public:
    BleScanner();
    ~BleScanner();

    bool StartScanning();
    bool StopScanning();
    bool IsScanning();

    NimBLEAddress getDeviceAddress() const;

private:
  void onResult(const NimBLEAdvertisedDevice *advertisedDevice) override;
  void onScanEnd(const NimBLEScanResults &results, int reason) override;

private:
    SemaphoreHandle_t lockDeviceAddress;
    NimBLEAddress advDeviceAddress;
};

#endif // BLE_SCANNER_H
