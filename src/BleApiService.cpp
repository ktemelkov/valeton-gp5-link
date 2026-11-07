#include "BleApiService.h"

#include "esp_log.h"

static constexpr const char *MIDI_Service_UUID_Str = "03B80E5A-EDE8-4B33-A751-6CE34EC4C700";
static constexpr const char *MIDI_Char_UUID_Str = "7772E5DB-3868-4112-A1A9-F2669D106BF3";
static constexpr uint16_t GP5_Service_UUID = 0x0808;
static constexpr const char *GP5_Advertised_Name = "GP5 Proxy";
static constexpr const char *TAG = "BleApiService";

/**
 *
 */
BleApiService::BleApiService() : midiChannel(nullptr), midiConnectionHandle(BLE_HS_CONN_HANDLE_NONE), endpoint(nullptr), lockChannel(nullptr)
{
    lockChannel = xSemaphoreCreateMutex();

    if (lockChannel == nullptr)
    {
        ESP_LOGE(TAG, "%s", "Failed to create the lockChannel semaphore.");
    }
}

/**
 *
 */
void BleApiService::setup(BleEndpoint* endpoint)
{
    this->endpoint = endpoint;

    if (lockChannel == nullptr || endpoint == nullptr)
    {
        ESP_LOGE(TAG, "Cannot set up BLE API service: missing channel lock or endpoint.");
        return;
    }

    NimBLEDevice::init(GP5_Advertised_Name);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    NimBLEDevice::setSecurityAuth(false, false, true);
    NimBLEDevice::setPower(ESP_PWR_LVL_N0); /** +0db */

    NimBLEServer *pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(this, false);

    NimBLEService *pService = pServer->createService(MIDI_Service_UUID_Str);
    NimBLECharacteristic *pCharacteristic = pService->createCharacteristic(MIDI_Char_UUID_Str, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::NOTIFY);
    pCharacteristic->setCallbacks(this);
    if (!pServer->start())
    {
        ESP_LOGE(TAG, "Failed to start BLE API server.");
        return;
    }

    midiChannel = pCharacteristic;

    NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
    
    if (!pAdvertising->setName(GP5_Advertised_Name))
    {
        ESP_LOGE(TAG, "BleApiService: Failed to set advertised name\n");
    }

    if (!pAdvertising->addServiceUUID(NimBLEUUID((uint16_t)GP5_Service_UUID)))
    {
        ESP_LOGE(TAG, "BleApiService: Failed to add GP5 service UUID to advertising\n");
    }

    if (!pAdvertising->addServiceUUID(MIDI_Service_UUID_Str))
    {
        ESP_LOGE(TAG, "BleApiService: Failed to add MIDI service UUID to advertising\n");
    }

    pAdvertising->enableScanResponse(true);

    if (!pAdvertising->start())
    {
        ESP_LOGE(TAG, "BleApiService: Failed to start advertising\n");
    }
}

/**
 *
 */
void BleApiService::onConnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo)
{
    ESP_LOGI(TAG, "BleApiService: Client connected %s\n", connInfo.getIdAddress().toString().c_str());

    if (pServer == nullptr || lockChannel == nullptr)
    {
        ESP_LOGE(TAG, "Cannot track BLE API connection: missing server or channel lock.");
        return;
    }

    if (pdTRUE == xSemaphoreTake(lockChannel, portMAX_DELAY))
    {
        midiConnectionHandle = connInfo.getConnHandle();
        xSemaphoreGive(lockChannel);
    }
}

/**
 *
 */
void BleApiService::onAuthenticationComplete(NimBLEConnInfo &connInfo)
{
    ESP_LOGI(TAG, "BleApiService: Authentication complete for %s\n", connInfo.getIdAddress().toString().c_str());
}

/**
 *
 */
void BleApiService::onDisconnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo, int reason)
{
    if (lockChannel != nullptr && pdTRUE == xSemaphoreTake(lockChannel, portMAX_DELAY))
    {
        if (midiConnectionHandle == connInfo.getConnHandle())
            midiConnectionHandle = BLE_HS_CONN_HANDLE_NONE;
        xSemaphoreGive(lockChannel);
    }
    
    ESP_LOGI(TAG, "BleApiService: Client disconnected: %s, reason: %d\n", connInfo.getIdAddress().toString().c_str(), reason);
    if (pServer == nullptr || !pServer->getAdvertising()->start())
        ESP_LOGE(TAG, "Failed to restart BLE API advertising.");
}

/**
 *
 */
void BleApiService::onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo)
{
    ESP_LOGI(TAG, "BleApiService: onWrite called for characteristic %s from %s\n",
             pCharacteristic->getUUID().toString().c_str(),
             connInfo.getIdAddress().toString().c_str());

    auto val = pCharacteristic->getValue();

    ESP_LOGI(TAG, "BleApiService: Received %u bytes\n", static_cast<unsigned>(val.size()));
    ESP_LOG_BUFFER_HEX(TAG, val.data(), val.size());

    if (endpoint == nullptr)
    {
        ESP_LOGE(TAG, "Failed to forward write: BLE endpoint not configured.");
        return;
    }

    if (!endpoint->Send(val.data(), val.size()))
        ESP_LOGE(TAG, "Failed to forward MIDI write to BLE endpoint.");
}

/**
 *
 */
bool BleApiService::Send(const uint8_t *data, size_t length)
{
    if ((data == nullptr && length != 0) || lockChannel == nullptr || midiChannel == nullptr)
    {
        ESP_LOGE(TAG, "Failed to forward notification: invalid buffer or BLE API service not ready.");
        return false;
    }

    uint16_t connectionHandle;
    if (pdTRUE != xSemaphoreTake(lockChannel, portMAX_DELAY))
    {
        ESP_LOGE(TAG, "Failed to take BLE API channel lock.");
        return false;
    }
    connectionHandle = midiConnectionHandle;
    xSemaphoreGive(lockChannel);

    NimBLEServer *pServer = NimBLEDevice::getServer();
    if (pServer == nullptr || connectionHandle == BLE_HS_CONN_HANDLE_NONE)
    {
        ESP_LOGW(TAG, "Cannot forward notification: no BLE API client connected.");
        return false;
    }

    const uint16_t mtu = pServer->getPeerMTU(connectionHandle);
    if (mtu < 3 || length > static_cast<size_t>(mtu - 3))
    {
        ESP_LOGE(TAG, "Notification (%u bytes) exceeds negotiated MTU payload (%u bytes).",
                 static_cast<unsigned>(length), mtu >= 3 ? static_cast<unsigned>(mtu - 3) : 0);
        return false;
    }

    const bool sent = midiChannel->notify(data, length, connectionHandle);
    if (!sent)
        ESP_LOGE(TAG, "Failed to forward MIDI notification to BLE API client.");

    return sent;
}
