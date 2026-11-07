#ifndef BLE_INTERFACE_H
#define BLE_INTERFACE_H

#include "NimBLEDevice.h"
#include "BleEndpoint.h"
#include "freertos/queue.h"
class PresetManager;

/**
 *
 */
class BleApiService : public NimBLECharacteristicCallbacks, public NimBLEDescriptorCallbacks, public NimBLEServerCallbacks, public BleEndpoint
{
public:
    BleApiService();

    void setup(BleEndpoint* endpoint, PresetManager *presets = nullptr);
    bool Send(const uint8_t* data, size_t length) override;
    void processNotifications();

    // NimBLEServerCallbacks
    void onConnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo) override;
    void onDisconnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo, int reason) override;

    // NimBLECharacteristicCallbacks
    void onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo) override;
    void onSubscribe(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo, uint16_t subValue) override;
    void onStatus(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo, int code) override;

    // NimBLESecurityCallbacks
    void onAuthenticationComplete(NimBLEConnInfo& connInfo) override;

private:
    class ApiReply : public BleEndpoint
    {
    public:
        explicit ApiReply(BleApiService &owner) : owner(owner) {}
        bool Send(const uint8_t *data, size_t length) override { return owner.enqueue(data, length, true); }
    private:
        BleApiService &owner;
    };
    bool enqueue(const uint8_t *data, size_t length, bool api);
    NimBLECharacteristic* midiChannel;
    NimBLECharacteristic* configurationChannel = nullptr;
    uint16_t midiConnectionHandle;
    BleEndpoint* endpoint;
    PresetManager *presets = nullptr;
    struct Notification
    {
        uint8_t data[512];
        uint16_t length;
        uint16_t connectionHandle;
        uint32_t generation;
        TickType_t queuedAt;
        bool api;
    };
    QueueHandle_t notifications = nullptr;
    uint32_t connectionGeneration = 0;
    bool subscribed = false;
    bool configurationSubscribed = false;
    uint32_t configurationGeneration = 0;
    bool retrying = false;

    SemaphoreHandle_t lockChannel;    
};

#endif // BLE_INTERFACE_H
