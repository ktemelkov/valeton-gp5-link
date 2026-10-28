#include "BleScanner.h"
#include "debug.h"

/**
 *
 */
BleScanner::BleScanner()
{
    DEBUG_MSG("%s", "Starting BLE Scanner ...\n");

    NimBLEDevice::init("");
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    // Allow bonding: many BLE MIDI peripherals refuse to pair (and drop the
    // link shortly after connecting) unless bonding is enabled.
    NimBLEDevice::setSecurityAuth(true, false, true);
    NimBLEDevice::setPower(ESP_PWR_LVL_P9); /** +9db */

    NimBLEScan *pScan = NimBLEDevice::getScan();
    pScan->setScanCallbacks(this);
    pScan->setInterval(97);
    pScan->setWindow(67);
}

/**
 *
 */
BleScanner::~BleScanner()
{
    NimBLEDevice::getScan()->setScanCallbacks(nullptr);
}

/**
 *
 */
bool BleScanner::StartScanning()
{
    advDeviceCount = -1;

    NimBLEScan *pScan = NimBLEDevice::getScan();
    pScan->setActiveScan(true);
    return pScan->start(0 /* scan forever */, false, true);
}

/**
 *
 */
bool BleScanner::isScanning()
{
    return NimBLEDevice::getScan()->isScanning();
}

/**
 *
 */
bool BleScanner::StopScanning()
{
    return NimBLEDevice::getScan()->stop();
}

/**
 *
 */
const NimBLEAdvertisedDevice *BleScanner::getDevice(size_t index) const
{
    if (advDeviceCount >= 0 && index <= (size_t)advDeviceCount)
    {
        return &advDevices[index];
    }

    return nullptr;
}

/**
 *
 */
void BleScanner::onResult(const NimBLEAdvertisedDevice *advertisedDevice)
{
    DEBUG_MSG("\n%s", "Found BLE device\n");
    DEBUG_MSG("%s\n\n", advertisedDevice->toString().c_str());

    if (advertisedDevice == nullptr)
    {
        return;
    }

    int slot = advDeviceCount < 19 ? ++advDeviceCount : 19;
    advDevices[slot] = *advertisedDevice;
}

/**
 *
 */
void BleScanner::onScanEnd(const NimBLEScanResults &results, int reason)
{
}
