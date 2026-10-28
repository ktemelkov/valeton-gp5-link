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
    // Copies, not pointers: NimBLE deletes the advertised devices held by the
    // scan results as soon as scanning is (re)started, which would leave any
    // stored pointers dangling.
    NimBLEAdvertisedDevice advDevices[20];
    int advDeviceCount = -1;
};


#endif // BLE_SCANNER_H