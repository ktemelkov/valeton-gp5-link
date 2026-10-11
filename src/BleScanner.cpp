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
    NimBLEDevice::setSecurityAuth(false, false, true);
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
    advDeviceCount = 0;

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
    if (index < advDeviceCount)
    {
        return advDevices[index];
    }

    return nullptr;
}

/**
 *
 */
void BleScanner::onResult(const NimBLEAdvertisedDevice *advertisedDevice)
{
    advDevices[advDeviceCount < 20 ? advDeviceCount++ : 19] = advertisedDevice;
}

/**
 *
 */
void BleScanner::onScanEnd(const NimBLEScanResults &results, int reason)
{
}
