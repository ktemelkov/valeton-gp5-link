#include "BleClient.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static constexpr const char *TAG = "BleClient";

// Target 128-bit Service and Characteristic UUIDs
static const char *MIDI_Service_UUID_Str = "03B80E5A-EDE8-4B33-A751-6CE34EC4C700";
static const char *MIDI_Char_UUID_Str = "7772E5DB-3868-4112-A1A9-F2669D106BF3";

/**
 *
 */
BleClient::BleClient()
    : midiChannel(nullptr), lockChannel(nullptr), endpoint(nullptr)
{
    lockChannel = xSemaphoreCreateMutex();

    if (lockChannel == nullptr)
    {
        ESP_LOGE(TAG, "%s", "Failed to create the lockClient semaphore.");
    }
}

/**
 *
 */
bool BleClient::Connect(const NimBLEAddress &deviceAddress)
{
    if (IsConnected())
        Disconnect();

    NimBLEDevice::deleteAllBonds();
    vTaskDelay(pdMS_TO_TICKS(250));

    NimBLEClient *pClient = prepareClient();
    if (!pClient)
    {
        ESP_LOGE(TAG, "Failed to create client.\n");
        return false;
    }

    if (!pClient->connect(deviceAddress, false, false, true))
    {
        int reason = pClient->getLastError();
        ESP_LOGE(TAG, "Failed to connect, reason: %d (0x%X)\n", reason, reason);
        return false;
    }

    ESP_LOGI(TAG, "Connected successfully! Discovering services...\n");

    NimBLERemoteService *pSvc = pClient->getService(MIDI_Service_UUID_Str);

    if (!pSvc)
    {
        ESP_LOGE(TAG, "MIDI service not found.\n");
        pClient->disconnect();
        vTaskDelay(pdMS_TO_TICKS(250));
        return false;
    }

    NimBLERemoteCharacteristic *pChr = pSvc->getCharacteristic(MIDI_Char_UUID_Str);

    if (!pChr)
    {
        ESP_LOGE(TAG, "MIDI characteristic not found.\n");
        pClient->disconnect();
        vTaskDelay(pdMS_TO_TICKS(250));
        return false;
    }

    if (!pChr->canWriteNoResponse())
    {
        ESP_LOGE(TAG, "MIDI characteristic does not support writes without response.");
        pClient->disconnect();
        vTaskDelay(pdMS_TO_TICKS(250));
        return false;
    }

    if (!pChr->canNotify() || !pChr->subscribe(true,
        [this](NimBLERemoteCharacteristic *characteristic, uint8_t *data, size_t length, bool isNotify)
        {
            deviceNotifyCB(characteristic, data, length, isNotify);
        }))
    {
        ESP_LOGE(TAG, "Failed to subscribe to notifications.\n");
        pClient->disconnect();
        vTaskDelay(pdMS_TO_TICKS(250));
        return false;
    }

    setMidiChannel(pChr);

    ESP_LOGI(TAG, "MIDI characteristic ready; negotiated MTU: %u", pClient->getMTU());
    return true;
}

/**
 * Return a client ready for a connection attempt. Self-delete is turned off
 * so the object is still alive after a failed connect, which is when we read
 * getLastError() and re-use it for the retry.
 */
NimBLEClient *BleClient::prepareClient()
{
    NimBLEClient *pClient = NimBLEDevice::getDisconnectedClient();

    if (!pClient)
    {
        pClient = NimBLEDevice::createClient();

        if (!pClient)
            return nullptr;
    }

    pClient->setSelfDelete(false, false);
    pClient->setClientCallbacks(this, false);

    // Set connection parameters: interval 15ms, latency 0, supervision timeout 2s
    pClient->setConnectionParams(12, 12, 0, 200);
    pClient->setConnectTimeout(5 * 1000);

    return pClient;
}

/**
 *
 */
bool BleClient::Disconnect()
{
    if (pdTRUE == xSemaphoreTake(lockChannel, pdMS_TO_TICKS(1000)))
    {
        if (!midiChannel)
        {
            xSemaphoreGive(lockChannel);
            return true;
        }

        NimBLEClient *pClient = midiChannel->getClient();

        if (!pClient)
        {
            ESP_LOGE(TAG, "Failed to get client from characteristic.\n");
            xSemaphoreGive(lockChannel);
            return false;
        }

        pClient->disconnect();
        xSemaphoreGive(lockChannel);

        while (IsConnected())
            vTaskDelay(pdMS_TO_TICKS(100));

        pClient->setClientCallbacks(nullptr, false);
        return true;
    }

    return false;
}

/**
 * Check if the client is currently connected to a MIDI device.
 */
bool BleClient::IsConnected() const
{
    if (lockChannel == nullptr)
        return false;

    if (pdTRUE == xSemaphoreTake(lockChannel, portMAX_DELAY))
    {
        bool isConnected = midiChannel != nullptr;
        xSemaphoreGive(lockChannel);

        return isConnected;
    }

    return false;
}

/**
 *
 */
bool BleClient::Send(const uint8_t *data, size_t length)
{
    if (data == nullptr && length != 0)
    {
        ESP_LOGE(TAG, "Failed to send data: null packet buffer.");
        return false;
    }

    if (lockChannel != nullptr && pdTRUE == xSemaphoreTake(lockChannel, portMAX_DELAY))
    {
        if (!midiChannel)
        {
            xSemaphoreGive(lockChannel);
            ESP_LOGE(TAG, "Failed to send data: MIDI channel not available.\n");
            return false;
        }

        ESP_LOG_BUFFER_HEX(TAG, data, length);

        // Avoid NimBLE's long-write path, which can truncate a SysEx packet.
        const uint16_t mtu = midiChannel->getClient()->getMTU();
        if (mtu < 3 || length > static_cast<size_t>(mtu - 3))
        {
            xSemaphoreGive(lockChannel);
            ESP_LOGE(TAG, "MIDI packet (%u bytes) exceeds negotiated MTU payload (%u bytes).",
                     static_cast<unsigned>(length), mtu >= 3 ? static_cast<unsigned>(mtu - 3) : 0);
            return false;
        }

        const bool sent = midiChannel->writeValue(data, length, false);
        xSemaphoreGive(lockChannel);

        if (!sent)
            ESP_LOGE(TAG, "Failed to write MIDI packet without response.");

        return sent;
    }

    ESP_LOGE(TAG, "Failed to send data: MIDI channel not available.\n");
    return false;
}

/**
 * Callback for handling notifications from the MIDI characteristic.
 */
void BleClient::deviceNotifyCB(NimBLERemoteCharacteristic* pChr, uint8_t* data, size_t length, bool isNotify)
{
    if (length == 212 && data[5] == 1 && data[6] == 5)
    {
        ESP_LOGI(TAG, "GP-5 patch-name batch received: %u", data[7] * 16u + data[8]);
    }
    else if (length == 212)
    {
        ESP_LOGI(TAG, "GP-5 list notification: type=%u/%u, batch=%u", data[5], data[6], data[7] * 16u + data[8]);
    }
    else
    {
        ESP_LOGI(TAG, "Notification received from characteristic: %s", pChr->getUUID().toString().c_str());
        ESP_LOG_BUFFER_HEX(TAG, data, length);
    }

    if (endpoint == nullptr)
    {
        ESP_LOGE(TAG, "Failed to forward notification: BLE endpoint not configured.");
        return;
    }

    if (!endpoint->Send(data, length))
        ESP_LOGE(TAG, "Failed to forward MIDI notification to BLE endpoint.");
}

/**
 *
 */
void BleClient::onConnect(NimBLEClient *pClient)
{
    ESP_LOGI(TAG, "Connected to: %s", pClient->getPeerAddress().toString().c_str());
    ESP_LOGI(TAG, "RSSI: %d\n", pClient->getRssi());

    pClient->updateConnParams(12, 12, 0, 200);
}

/**
 *
 */
void BleClient::onConnectFail(NimBLEClient *pClient, int reason)
{
    ESP_LOGE(TAG, "Failed to connect to: %s, reason: %d (0x%X)\n",
             pClient->getPeerAddress().toString().c_str(), reason,
             reason);

    setMidiChannel(nullptr);
}

/**
 *
 */
void BleClient::onDisconnect(NimBLEClient *pClient, int reason)
{
    ESP_LOGI(TAG, "Disconnected from: %s, reason: %d (0x%X)\n",
             pClient->getPeerAddress().toString().c_str(), reason,
             reason);

    setMidiChannel(nullptr);
}

/**
 * The peer asked us to accept new connection parameters. Reject requests that
 * would starve the link (very long interval or too short supervision timeout),
 * as those lead to supervision timeout disconnects (reason 0x08).
 */
bool BleClient::onConnParamsUpdateRequest(NimBLEClient *pClient, const ble_gap_upd_params *params)
{
    // Units: interval = 1.25ms, timeout = 10ms
    if (params->supervision_timeout < 100 /* < 1s */ ||
        params->itvl_min > 320 /* > 400ms */)
    {
        ESP_LOGI(TAG, "Rejecting peer conn params: itvl %d-%d, latency %d, timeout %d\n",
                 params->itvl_min, params->itvl_max, params->latency,
                 params->supervision_timeout);
        return false;
    }

    return true;
}

/**
 * Pairing: the peer uses passkey entry and expects us to send the passkey.
 * BLE MIDI devices typically use 000000 (or just-works).
 */
void BleClient::onPassKeyEntry(NimBLEConnInfo &connInfo)
{
    ESP_LOGI(TAG, "Passkey requested, injecting 000000\n");
    NimBLEDevice::injectPassKey(connInfo, 0);
}

/**
 * Pairing: numeric comparison (Secure Connections). We have no display/keyboard,
 * so accept the comparison automatically.
 */
void BleClient::onConfirmPasskey(NimBLEConnInfo &connInfo, uint32_t pin)
{
    ESP_LOGI(TAG, "Numeric comparison requested, accepting pin: %u\n", pin);
    NimBLEDevice::injectConfirmPasskey(connInfo, true);
}

/**
 * Pairing/encryption finished. If encryption failed the peer will very likely
 * drop the link, so log the state to make that visible.
 */
void BleClient::onAuthenticationComplete(NimBLEConnInfo &connInfo)
{
    ESP_LOGI(TAG, "Authentication complete. Encrypted: %d, Authenticated: %d\n",
             connInfo.isEncrypted(), connInfo.isAuthenticated());
}

/**
 *
 */
void BleClient::setMidiChannel(NimBLERemoteCharacteristic *channel)
{
    if (xSemaphoreTake(lockChannel, portMAX_DELAY) == pdTRUE)
    {
        midiChannel = channel;
        xSemaphoreGive(lockChannel);
    }
}
