#include "BleApiService.h"
#include "PresetManager.h"

#include "esp_log.h"
#include "host/ble_gatt.h"
#include "host/ble_hs_mbuf.h"
#include <cstring>

static constexpr const char *MIDI_Service_UUID_Str = "03B80E5A-EDE8-4B33-A751-6CE34EC4C700";
static constexpr const char *MIDI_Char_UUID_Str = "7772E5DB-3868-4112-A1A9-F2669D106BF3";
static constexpr const char *Configuration_Service_UUID = "9c7b1000-6e4d-4f42-9a31-2d5e8b0c7f60";
static constexpr const char *Configuration_Char_UUID = "9c7b1001-6e4d-4f42-9a31-2d5e8b0c7f60";
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
    notifications = xQueueCreate(32, sizeof(Notification));
    if (notifications == nullptr)
        ESP_LOGE(TAG, "Failed to create MIDI notification queue.");
}

/**
 *
 */
void BleApiService::setup(BleEndpoint* endpoint, PresetManager *presets)
{
    this->endpoint = endpoint;
    this->presets = presets;

    if (lockChannel == nullptr || notifications == nullptr || endpoint == nullptr)
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
    pServer->registerAppServicesFirst(false);
    
    NimBLEService *configurationService = pServer->createService(Configuration_Service_UUID);
    configurationChannel = configurationService->createCharacteristic(Configuration_Char_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY);
    configurationChannel->setCallbacks(this);

    NimBLEService *pMidiService = pServer->createService(MIDI_Service_UUID_Str);
    midiChannel = pMidiService->createCharacteristic(MIDI_Char_UUID_Str, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::NOTIFY);
    midiChannel->setCallbacks(this);
    
    if (!pServer->start())
    {
        ESP_LOGE(TAG, "Failed to start BLE API server.");
        return;
    }

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
        subscribed = false;
        configurationSubscribed = false;
        ++configurationGeneration;
        ++connectionGeneration;
        xSemaphoreGive(lockChannel);
    }
    pServer->updateConnParams(connInfo.getConnHandle(), 12, 24, 0, 200);
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
        {
            midiConnectionHandle = BLE_HS_CONN_HANDLE_NONE;
            subscribed = false;
            configurationSubscribed = false;
            ++configurationGeneration;
            ++connectionGeneration;
        }
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

    if (pCharacteristic->getHandle() == configurationChannel->getHandle())
    {
        if (lockChannel == nullptr || pdTRUE != xSemaphoreTake(lockChannel, portMAX_DELAY))
        {
            ESP_LOGE(TAG, "Cannot handle configuration request: channel lock unavailable.");
            return;
        }
        const bool ready = configurationSubscribed && midiConnectionHandle == connInfo.getConnHandle();
        xSemaphoreGive(lockChannel);
        NimBLEServer *server = NimBLEDevice::getServer();
        if (!ready || server == nullptr || server->getPeerMTU(connInfo.getConnHandle()) < 135)
        {
            ESP_LOGE(TAG, "Configuration API requires a subscribed client and MTU of at least 135.");
            return;
        }
        if (presets == nullptr)
        {
            ESP_LOGE(TAG, "Configuration API unavailable: preset manager not configured.");
            return;
        }
        ApiReply reply(*this);
        presets->handleApi(val.data(), val.size(), reply);
        return;
    }

    if (pCharacteristic->getHandle() != midiChannel->getHandle())
    {
        ESP_LOGE(TAG, "Write received on unknown characteristic.");
        return;
    }

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
void BleApiService::onSubscribe(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo, uint16_t subValue)
{
    if (lockChannel != nullptr && pdTRUE == xSemaphoreTake(lockChannel, portMAX_DELAY))
    {
        if (midiConnectionHandle == connInfo.getConnHandle())
        {
            const bool enabled = (subValue & 1) != 0;
            if (pCharacteristic == configurationChannel)
            {
                if (configurationSubscribed != enabled)
                    ++configurationGeneration;
                configurationSubscribed = enabled;
            }
            else if (pCharacteristic == midiChannel)
            {
                if (subscribed != enabled)
                    ++connectionGeneration;
                subscribed = enabled;
            }
        }
        xSemaphoreGive(lockChannel);
    }
    NimBLEServer *server = NimBLEDevice::getServer();
    ESP_LOGI(TAG, "%s subscription: handle=%u, notifications=%u, MTU=%u, interval=%u (1.25ms), latency=%u",
             pCharacteristic == configurationChannel ? "Configuration" : "MIDI",
             connInfo.getConnHandle(), static_cast<unsigned>(subValue & 1),
             server != nullptr ? server->getPeerMTU(connInfo.getConnHandle()) : 0,
             connInfo.getConnInterval(), connInfo.getConnLatency());
}

void BleApiService::onStatus(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo, int code)
{
    if (code != 0)
        ESP_LOGE(TAG, "%s notification status: handle=%u, rc=%d",
                 pCharacteristic == configurationChannel ? "Configuration" : "MIDI", connInfo.getConnHandle(), code);
}

/**
 *
 */
bool BleApiService::Send(const uint8_t *data, size_t length)
{
    return enqueue(data, length, false);
}

bool BleApiService::enqueue(const uint8_t *data, size_t length, bool api)
{
    if ((data == nullptr && length != 0) || length > sizeof(Notification::data) ||
        lockChannel == nullptr || (api ? configurationChannel : midiChannel) == nullptr || notifications == nullptr)
    {
        ESP_LOGE(TAG, "Failed to forward notification: invalid buffer or BLE API service not ready.");
        return false;
    }

    Notification notification{};
    bool enabled;
    if (pdTRUE != xSemaphoreTake(lockChannel, portMAX_DELAY))
    {
        ESP_LOGE(TAG, "Failed to take BLE API channel lock.");
        return false;
    }
    notification.connectionHandle = midiConnectionHandle;
    notification.api = api;
    notification.generation = api ? configurationGeneration : connectionGeneration;
    enabled = api ? configurationSubscribed : subscribed;
    xSemaphoreGive(lockChannel);

    NimBLEServer *pServer = NimBLEDevice::getServer();
    if (pServer == nullptr || notification.connectionHandle == BLE_HS_CONN_HANDLE_NONE || !enabled)
    {
        ESP_LOGW(TAG, "Cannot forward notification: no connected/subscribed BLE API client.");
        return false;
    }

    const uint16_t mtu = pServer->getPeerMTU(notification.connectionHandle);
    if (mtu < 3 || length > static_cast<size_t>(mtu - 3))
    {
        ESP_LOGE(TAG, "Notification (%u bytes) exceeds negotiated MTU payload (%u bytes).",
                 static_cast<unsigned>(length), mtu >= 3 ? static_cast<unsigned>(mtu - 3) : 0);
        return false;
    }

    if (length != 0)
        std::memcpy(notification.data, data, length);
    notification.length = static_cast<uint16_t>(length);
    notification.queuedAt = xTaskGetTickCount();
    if (xQueueSend(notifications, &notification, 0) != pdTRUE)
    {
        ESP_LOGE(TAG, "BLE notification queue full: dropping %u bytes.", static_cast<unsigned>(length));
        return false;
    }
    if (!api && length == 212 && data[5] == 1 && data[6] == 5)
        ESP_LOGI(TAG, "Patch-name batch queued: %u", data[7] * 16u + data[8]);
    return true;
}

void BleApiService::processNotifications()
{
    for (uint8_t processed = 0; processed < 5; ++processed)
    {
        Notification notification;
        if (notifications == nullptr || midiChannel == nullptr ||
            xQueuePeek(notifications, &notification, 0) != pdTRUE)
            return;

        if (pdTRUE != xSemaphoreTake(lockChannel, portMAX_DELAY))
        {
            ESP_LOGE(TAG, "Cannot process BLE notification: channel lock unavailable.");
            return;
        }
        const bool current = (notification.api ? configurationSubscribed : subscribed) &&
                             notification.connectionHandle == midiConnectionHandle &&
                             notification.generation == (notification.api ? configurationGeneration : connectionGeneration);
        xSemaphoreGive(lockChannel);

        int code = 0;
        const uint32_t lifetimeMs = notification.api ? 2000 : 10000;
        bool discard = !current;
        if (!current)
            ESP_LOGW(TAG, "Discarding queued BLE notification from an ended subscription.");
        else if (static_cast<TickType_t>(xTaskGetTickCount() - notification.queuedAt) >= pdMS_TO_TICKS(lifetimeMs))
        {
            ESP_LOGE(TAG, "BLE notification expired after %u ms: handle=%u, bytes=%u.",
                     static_cast<unsigned>(lifetimeMs), notification.connectionHandle, notification.length);
            discard = true;
        }
        else
        {
            const uint16_t mtu = NimBLEDevice::getServer()->getPeerMTU(notification.connectionHandle);
            if (mtu < 3 || notification.length > mtu - 3)
            {
                ESP_LOGE(TAG, "Queued BLE notification exceeds current MTU: bytes=%u, MTU=%u.",
                         notification.length, mtu);
                discard = true;
            }
            else
            {
                // Use the host API to retain the error code without sleeping in a BLE callback.
                os_mbuf *buffer = ble_hs_mbuf_from_flat(notification.data, notification.length);
                code = buffer == nullptr ? BLE_HS_ENOMEM :
                       ble_gatts_notify_custom(notification.connectionHandle,
                           (notification.api ? configurationChannel : midiChannel)->getHandle(), buffer);
                if (code == BLE_HS_ENOMEM || code == BLE_HS_EAGAIN || code == BLE_HS_EBUSY)
                {
                    if (!retrying)
                        ESP_LOGW(TAG, "BLE notification deferred: rc=%d, bytes=%u. Retrying from main loop.",
                                 code, notification.length);
                    retrying = true;
                    return;
                }
                if (code != 0)
                    ESP_LOGE(TAG, "Queued BLE notification failed: handle=%u, bytes=%u, rc=%d.",
                             notification.connectionHandle, notification.length, code);
            }
        }
        if (!discard && code == 0 && retrying)
            ESP_LOGI(TAG, "Deferred BLE notification sent.");
        if (!notification.api && notification.length == 212 &&
            notification.data[5] == 1 && notification.data[6] == 5)
            ESP_LOGI(TAG, "Patch-name batch forwarding: batch=%u, discarded=%u, rc=%d",
                     notification.data[7] * 16u + notification.data[8], static_cast<unsigned>(discard), code);
        retrying = false;
        xQueueReceive(notifications, &notification, 0);
    }
}
