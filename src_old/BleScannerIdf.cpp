#include "BleScannerIdf.h"

#include <string.h>

#include "BleAdvDataParser.h"
#include "debug.h"

// Mirrors the NimBLE scanner, which used interval 97 / window 67 (units of
// 0.625 ms) - roughly 60 % duty cycle, active scanning, no duplicate
// suppression so scan responses carrying the complete local name still arrive.
static const uint16_t kScanInterval = 97;
static const uint16_t kScanWindow = 67;

// Timeout for the controller's scan start/stop completion, in milliseconds.
static const uint32_t kCommandTimeoutMs = 1000;

// Legacy advertising event types that mean "you may connect to me". Extended
// reports reuse the legacy event type field for 1M-PHY advertisements, which is
// the only kind the GP-5 emits.
static const uint8_t kLegacyAdvInd = 0x13;
static const uint8_t kLegacyDirectInd = 0x15;
static const uint8_t kLegacyScanInd = 0x12;
static const uint8_t kLegacyNonConnInd = 0x10;
static const uint8_t kLegacyScanRspToAdvInd = 0x1b;
static const uint8_t kLegacyScanRspToScanInd = 0x1a;

static bool isConnectableEventType(uint8_t eventType)
{
    switch (eventType)
    {
    case kLegacyAdvInd:
    case kLegacyDirectInd:
    case kLegacyScanInd:
    case kLegacyScanRspToAdvInd:
    case kLegacyScanRspToScanInd:
        return true;
    case kLegacyNonConnInd:
        return false;
    default:
        // Extended (non-legacy) advertisements encode connectability in the
        // AD flags instead, which BleAdvDataParser::parse() already read.
        return true;
    }
}

BleScannerIdf::BleScannerIdf()
    : deviceCount_(-1)
    , scanning_(false)
{
    BleStack::instance().addGapObserver(this);
}

BleScannerIdf::~BleScannerIdf()
{
    BleStack::instance().removeGapObserver(this);
}

bool BleScannerIdf::applyScanParameters()
{
    esp_ble_ext_scan_params_t params;
    memset(&params, 0, sizeof(params));

    params.own_addr_type = BLE_ADDR_TYPE_PUBLIC;
    params.filter_policy = BLE_SCAN_FILTER_ALLOW_ALL;

    // Duplicates must stay enabled off: the complete local name usually only
    // arrives in the scan response, and de-duplicating would drop it.
    params.scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE;

    // The GP-5 is a legacy 1M-PHY advertiser, so only the uncoded PHY needs to
    // be configured; leaving the coded PHY at the zeroed default is harmless
    // because it is not requested.
    params.cfg_mask = ESP_BLE_GAP_EXT_SCAN_CFG_UNCODE_MASK;
    params.uncoded_cfg.scan_type = BLE_SCAN_TYPE_ACTIVE;
    params.uncoded_cfg.scan_interval = kScanInterval;
    params.uncoded_cfg.scan_window = kScanWindow;

    esp_err_t err = esp_ble_gap_set_ext_scan_params(&params);
    if (err != ESP_OK)
    {
        DEBUG_MSG("esp_ble_gap_set_ext_scan_params failed: %d\n", err);
        return false;
    }

    return true;
}

bool BleScannerIdf::StartScanning()
{
    BleStack &stack = BleStack::instance();

    if (!stack.ensureStarted())
    {
        return false;
    }

    stack.applyTxPower();

    {
        // -1 means "no entries", matching the sentinel the NimBLE scanner used,
        // so getDevice(0) returns nullptr on an empty list rather than a blank
        // slot. Clearing happens before the command is issued so a report that
        // arrives immediately cannot be merged into a stale entry.
        BleLock lock(mutex_);
        deviceCount_ = -1;
        memset(devices_, 0, sizeof(devices_));
    }

    scanStarted_.clear();

    if (!applyScanParameters())
    {
        return false;
    }

    // duration 0 and period 0 mean "scan until told to stop", matching the
    // start(0, false, true) call the NimBLE scanner made.
    esp_err_t err = esp_ble_gap_start_ext_scan(0, 0);
    if (err != ESP_OK)
    {
        DEBUG_MSG("esp_ble_gap_start_ext_scan failed: %d\n", err);
        return false;
    }

    // Returning only once the controller confirms keeps the caller's view of
    // "scanning" truthful, which matters because main.cpp stops scanning and
    // then immediately connects.
    if (!scanStarted_.wait(kCommandTimeoutMs))
    {
        DEBUG_MSG("%s", "Scan start was not acknowledged.\n");
        return false;
    }

    return true;
}

bool BleScannerIdf::StopScanning()
{
    if (!BleStack::instance().isRunning())
    {
        return false;
    }

    scanStopped_.clear();

    esp_err_t err = esp_ble_gap_stop_ext_scan();
    if (err != ESP_OK)
    {
        DEBUG_MSG("esp_ble_gap_stop_ext_scan failed: %d\n", err);
        return false;
    }

    if (!scanStopped_.wait(kCommandTimeoutMs))
    {
        DEBUG_MSG("%s", "Scan stop was not acknowledged.\n");
        return false;
    }

    return true;
}

bool BleScannerIdf::isScanning()
{
    BleLock lock(mutex_);
    return scanning_;
}

const BleAdvertisedDevice *BleScannerIdf::getDevice(size_t index) const
{
    BleLock lock(mutex_);

    if (deviceCount_ < 0 || index > static_cast<size_t>(deviceCount_))
    {
        return nullptr;
    }

    return &devices_[index];
}

void BleScannerIdf::onGapEvent(esp_gap_ble_cb_event_t event,
                               esp_ble_gap_cb_param_t *param)
{
    if (!param)
    {
        return;
    }

    switch (event)
    {
    case ESP_GAP_BLE_EXT_ADV_REPORT_EVT:
        handleAdvReport(param->ext_adv_report.params);
        break;

    case ESP_GAP_BLE_EXT_SCAN_START_COMPLETE_EVT:
    {
        BleLock lock(mutex_);
        // The status is an esp_bt_status_t, a different enum from the GATT one,
        // so compare the value rather than the enumerators.
        scanning_ = static_cast<int>(param->ext_scan_start.status) == ESP_OK;
        scanStarted_.notify();
        break;
    }

    case ESP_GAP_BLE_EXT_SCAN_STOP_COMPLETE_EVT:
    {
        BleLock lock(mutex_);
        scanning_ = false;
        scanStopped_.notify();
        break;
    }

    case ESP_GAP_BLE_SCAN_TIMEOUT_EVT:
    {
        // The controller stopped on its own, so the flag has to follow reality
        // even though no one asked for a stop.
        BleLock lock(mutex_);
        scanning_ = false;
        break;
    }

    default:
        break;
    }
}

void BleScannerIdf::handleAdvReport(const esp_ble_gap_ext_adv_reprot_t &report)
{
    BleAdvertisedDevice device;

    memcpy(device.address, report.addr, sizeof(device.address));
    device.addressType = report.addr_type;
    device.rssi = report.rssi;
    device.connectable = isConnectableEventType(static_cast<uint8_t>(report.event_type));

    BleAdvDataParser::parse(report.adv_data, report.adv_data_len, device);

    handleResult(device);
}

void BleScannerIdf::handleResult(const BleAdvertisedDevice &device)
{
    BleLock lock(mutex_);

    for (int i = 0; i <= deviceCount_; ++i)
    {
        if (devices_[i].sameAddress(device))
        {
            // Keep the strongest signal seen and never lose a name or service
            // list that an earlier report already provided.
            BleAdvertisedDevice &slot = devices_[i];

            if (device.rssi > slot.rssi)
            {
                slot.rssi = device.rssi;
            }

            if (device.name[0] != '\0')
            {
                memcpy(slot.name, device.name, sizeof(slot.name));
            }

            slot.connectable = slot.connectable || device.connectable;

            for (uint8_t i2 = 0; i2 < device.serviceUuidCount; ++i2)
            {
                bool known = false;

                for (uint8_t j = 0; j < slot.serviceUuidCount; ++j)
                {
                    if (memcmp(slot.serviceUuids[j], device.serviceUuids[i2],
                               BleAdvertisedDevice::kUuidLength) == 0)
                    {
                        known = true;
                        break;
                    }
                }

                if (!known &&
                    slot.serviceUuidCount <
                        BleAdvertisedDevice::kMaxServiceUuids)
                {
                    memcpy(slot.serviceUuids[slot.serviceUuidCount],
                           device.serviceUuids[i2],
                           BleAdvertisedDevice::kUuidLength);
                    slot.serviceUuidCount++;
                }
            }

            return;
        }
    }

    // The NimBLE scanner grew to index 19 and then kept overwriting the last
    // slot, so main.cpp's iteration length stays bounded the same way.
    if (deviceCount_ < static_cast<int>(kMaxDevices) - 1)
    {
        ++deviceCount_;
    }

    devices_[deviceCount_] = device;
}
