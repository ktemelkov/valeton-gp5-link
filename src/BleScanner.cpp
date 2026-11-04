#include "BleScanner.h"

#include <string.h>
#include <esp_log.h>

static constexpr const char *TAG = "BleScanner";

// Target 128-bit Service and Characteristic UUIDs
static const char *MIDI_Service_UUID_Str = "03B80E5A-EDE8-4B33-A751-6CE34EC4C700";
static const char *BLE_Device_Name = "GP-5 BLE Kalin";


/**
 * Constructor for the BleScanner class.
 */
BleScanner::BleScanner()
{
    ESP_LOGI(TAG, "Starting BLE Scanner ...\n");

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

    lockDeviceAddress = xSemaphoreCreateMutex();

    if (lockDeviceAddress == nullptr)
    {
        ESP_LOGE(TAG, "%s", "Failed to create the lockDeviceAddress semaphore.");
    }
}

/**
 * Destructor for the BleScanner class.
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
    if (pdTRUE != xSemaphoreTake(lockDeviceAddress, pdMS_TO_TICKS(20)))
    {
        ESP_LOGE(TAG, "Failed to take lockDeviceAddress semaphore.");
        return false;
    }
    advDeviceAddress = NimBLEAddress();
    xSemaphoreGive(lockDeviceAddress);

    NimBLEScan *pScan = NimBLEDevice::getScan();
    pScan->setActiveScan(true);
    return pScan->start(0 /* scan forever */, false, true);
}

/**
 *
 */
bool BleScanner::StopScanning()
{
    NimBLEScan *pScan = NimBLEDevice::getScan();

    for (int i = 0; i < 100 && !pScan->stop(); i++)
        vTaskDelay(pdMS_TO_TICKS(10));

    return pScan->stop();
}

/**
 * Checks if the BLE scanner is currently scanning.
 *
 * @return true if scanning, false otherwise.
 */
bool BleScanner::IsScanning()
{
    NimBLEScan *pScan = NimBLEDevice::getScan();
    return pScan->isScanning();
}

/**
 *
 */
NimBLEAddress BleScanner::getDeviceAddress() const
{
    if (pdTRUE != xSemaphoreTake(lockDeviceAddress, pdMS_TO_TICKS(20)))
    {
        ESP_LOGE(TAG, "Failed to take lockDeviceAddress semaphore.");
        return NimBLEAddress();
    }
    NimBLEAddress retAddr = advDeviceAddress;
    xSemaphoreGive(lockDeviceAddress);

    return retAddr;
}

/**
 *
 */
void BleScanner::onResult(const NimBLEAdvertisedDevice *advertisedDevice)
{
    if (advertisedDevice == nullptr)
        return;

    ESP_LOGI(TAG, "\n%s", "Found BLE device\n");
    ESP_LOGI(TAG, "%s\n\n", advertisedDevice->toString().c_str());

    bool isOurDevice = advertisedDevice->isConnectable() &&
                       advertisedDevice->isAdvertisingService(NimBLEUUID(MIDI_Service_UUID_Str)) &&
                       advertisedDevice->getName().compare(BLE_Device_Name) == 0;

    if (isOurDevice)
    {
        ESP_LOGI(TAG, "Found our BLE device.\n");

        if (pdTRUE != xSemaphoreTake(lockDeviceAddress, pdMS_TO_TICKS(20)))
        {
            ESP_LOGE(TAG, "Failed to take lockDeviceAddress semaphore.");
            return;
        }
        advDeviceAddress = advertisedDevice->getAddress();
        xSemaphoreGive(lockDeviceAddress);
    }
}

/**
 *
 */
void BleScanner::onScanEnd(const NimBLEScanResults &results, int reason)
{
    ESP_LOGI(TAG, "Scan ended with reason: %d", reason);
}
