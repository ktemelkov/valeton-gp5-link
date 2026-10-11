#ifndef BLE_SCANNER_H
#define BLE_SCANNER_H

#include <NimBLEDevice.h>

/**
 *
 */
class BleScanner : public NimBLEScanCallbacks
{
public:
    BleScanner();
    ~BleScanner();

    bool StartScanning();
    bool StopScanning();
    bool isScanning();

    const NimBLEAdvertisedDevice *getDevice(size_t index) const;
    
private:
  void onResult(const NimBLEAdvertisedDevice *advertisedDevice) override;
  void onScanEnd(const NimBLEScanResults &results, int reason) override;

private:
    const NimBLEAdvertisedDevice *advDevices[20] = {nullptr};
    size_t advDeviceCount = 0;
};


#endif // BLE_SCANNER_H