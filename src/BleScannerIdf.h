#ifndef BLE_SCANNER_IDF_H
#define BLE_SCANNER_IDF_H

#include <stddef.h>

#include "BleDevice.h"
#include "BleStack.h"
#include "BleSync.h"
#include "IBleScanner.h"

/**
 * Scanner built directly on the ESP-IDF Bluedroid GAP API.
 *
 * It uses the extended scanning calls (esp_ble_gap_set_ext_scan_params() /
 * esp_ble_gap_start_ext_scan()) because this firmware is built with
 * CONFIG_BT_BLE_50_FEATURES_SUPPORTED and without
 * CONFIG_BT_BLE_42_FEATURES_SUPPORTED, which compiles the legacy
 * esp_ble_gap_start_scanning() path out of the stack. Extended scanning also
 * reports legacy 1M-PHY advertisements, so the GP-5 is seen normally.
 *
 * Concurrency: advertisements arrive on the Bluetooth host task while the
 * application reads the list from loopTask, so every access to the list and to
 * the scanning flag goes through mutex_. The completion events are turned into
 * bounded blocking waits with signals so StartScanning()/StopScanning() return
 * only once the controller actually acknowledged the command - the NimBLE
 * version returned while the HCI command was still queued, which is what made
 * a connect issued immediately afterwards fail as "busy".
 */
class BleScannerIdf : public IBleScanner, public BleStack::GapObserver
{
public:
    static const size_t kMaxDevices = 20;

    BleScannerIdf();
    ~BleScannerIdf() override;

    bool StartScanning() override;
    bool StopScanning() override;
    bool isScanning() override;
    const BleAdvertisedDevice *getDevice(size_t index) const override;

    // BleStack::GapObserver - runs on the Bluetooth host task.
    void onGapEvent(esp_gap_ble_cb_event_t event,
                    esp_ble_gap_cb_param_t *param) override;

private:
    bool applyScanParameters();
    void handleAdvReport(const esp_ble_gap_ext_adv_reprot_t &report);
    void handleResult(const BleAdvertisedDevice &device);

    mutable BleMutex mutex_;
    BleSignal scanStarted_;
    BleSignal scanStopped_;

    // Values, not pointers into stack memory: the Bluedroid report buffer is
    // only valid for the duration of the callback.
    BleAdvertisedDevice devices_[kMaxDevices];
    int deviceCount_;
    bool scanning_;
};

#endif // BLE_SCANNER_IDF_H
