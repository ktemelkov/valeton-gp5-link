#pragma once

#include "freertos/semphr.h"
#include <functional>
#include <string>

constexpr uint16_t BLE_HS_CONN_HANDLE_NONE = UINT16_MAX;
constexpr int BLE_HS_IO_NO_INPUT_OUTPUT = 0, ESP_PWR_LVL_N0 = 0;
class NimBLEUUID
{
public:
    explicit NimBLEUUID(uint16_t) {}
};
namespace NIMBLE_PROPERTY
{
    constexpr uint32_t READ = 1, WRITE = 2, WRITE_NR = 4, NOTIFY = 8;
}

class NimBLEAddress
{
public:
    std::string toString() const { return "mock"; }
};

class NimBLEConnInfo
{
public:
    uint16_t handle = 0;
    uint16_t getConnHandle() const { return handle; }
    NimBLEAddress getIdAddress() const { return {}; }
    bool isEncrypted() const { return true; }
    bool isAuthenticated() const { return true; }
    uint16_t getConnInterval() const { return 24; }
    uint16_t getConnLatency() const { return 0; }
};

class NimBLEClient;
class NimBLEServer;
class NimBLECharacteristic;
struct ble_gap_upd_params
{
    uint16_t supervision_timeout;
    uint16_t itvl_min;
};

class NimBLEClientCallbacks
{
public:
    virtual ~NimBLEClientCallbacks() = default;
    virtual void onConnect(NimBLEClient*) {}
    virtual void onConnectFail(NimBLEClient*, int) {}
    virtual void onDisconnect(NimBLEClient*, int) {}
    virtual bool onConnParamsUpdateRequest(NimBLEClient*, const ble_gap_upd_params*) { return true; }
    virtual void onPassKeyEntry(NimBLEConnInfo&) {}
    virtual void onConfirmPasskey(NimBLEConnInfo&, uint32_t) {}
    virtual void onAuthenticationComplete(NimBLEConnInfo&) {}
};

class NimBLEServerCallbacks
{
public:
    virtual ~NimBLEServerCallbacks() = default;
    virtual void onConnect(NimBLEServer*, NimBLEConnInfo&) {}
    virtual void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int) {}
    virtual void onAuthenticationComplete(NimBLEConnInfo&) {}
};

class NimBLECharacteristicCallbacks
{
public:
    virtual ~NimBLECharacteristicCallbacks() = default;
    virtual void onWrite(NimBLECharacteristic*, NimBLEConnInfo&) {}
    virtual void onSubscribe(NimBLECharacteristic*, NimBLEConnInfo&, uint16_t) {}
    virtual void onStatus(NimBLECharacteristic*, NimBLEConnInfo&, int) {}
};
class NimBLEDescriptorCallbacks {};

class NimBLERemoteCharacteristic
{
public:
    NimBLEClient* client = nullptr;
    std::function<void(NimBLERemoteCharacteristic*, uint8_t*, size_t, bool)> callback;
    std::vector<uint8_t> written;
    int writes = 0;
    bool writeResult = true;
    bool lastResponse = true;
    bool canWriteNoResponse() const { return true; }
    bool canNotify() const { return true; }
    bool subscribe(bool notifications, decltype(callback) handler)
    {
        assert(notifications);
        callback = handler;
        return true;
    }
    NimBLEClient* getClient() const { return client; }
    NimBLEAddress getUUID() const { return {}; }
    bool writeValue(const uint8_t* data, size_t length, bool response)
    {
        assert(mockLocksHeld == 1);
        written.assign(data, data + length);
        ++writes;
        lastResponse = response;
        return writeResult;
    }
};

class NimBLERemoteService
{
public:
    NimBLERemoteCharacteristic characteristic;
    NimBLERemoteCharacteristic* getCharacteristic(const char*) { return &characteristic; }
};

class NimBLEClient
{
public:
    NimBLEClientCallbacks* callbacks = nullptr;
    NimBLERemoteService service;
    uint16_t mtu = 27;
    bool connected = false;
    NimBLEClient() { service.characteristic.client = this; }
    bool connect(const NimBLEAddress&, bool, bool, bool)
    {
        connected = true;
        if (callbacks) callbacks->onConnect(this);
        return true;
    }
    bool disconnect()
    {
        connected = false;
        if (callbacks) callbacks->onDisconnect(this, 0);
        return true;
    }
    int getLastError() const { return 0; }
    NimBLERemoteService* getService(const char*) { return &service; }
    void setSelfDelete(bool, bool) {}
    void setClientCallbacks(NimBLEClientCallbacks* handler, bool) { callbacks = handler; }
    void setConnectionParams(int min, int max, int latency, int timeout)
    {
        assert(min == 12 && max == 12 && latency == 0 && timeout == 200);
    }
    void setConnectTimeout(int) {}
    void updateConnParams(int min, int max, int latency, int timeout)
    {
        assert(min == 12 && max == 12 && latency == 0 && timeout == 200);
    }
    NimBLEAddress getPeerAddress() const { return {}; }
    int getRssi() const { return -40; }
    uint16_t getMTU() const { return mtu; }
};

class NimBLECharacteristic
{
public:
    uint16_t attribute = 42;
    uint16_t getHandle() const { return attribute; }
    NimBLECharacteristicCallbacks* callbacks = nullptr;
    std::vector<uint8_t> value;
    std::vector<uint8_t> notified;
    uint32_t properties = 0;
    uint16_t lastHandle = BLE_HS_CONN_HANDLE_NONE;
    int notifications = 0;
    bool subscribed = false;
    bool notifyResult = true;
    NimBLEAddress getUUID() const { return {}; }
    std::vector<uint8_t> getValue() const { return value; }
    void setCallbacks(NimBLECharacteristicCallbacks* handler) { callbacks = handler; }
    bool notify(const uint8_t* data, size_t length, uint16_t handle)
    {
        assert(mockLocksHeld == 0);
        if (!subscribed) return false;
        notified.assign(data, data + length);
        lastHandle = handle;
        ++notifications;
        return notifyResult;
    }
};

class NimBLEService
{
public:
    NimBLECharacteristic characteristic;
    NimBLECharacteristic* createCharacteristic(const char*, uint32_t properties)
    {
        characteristic.properties = properties;
        return &characteristic;
    }
};

class NimBLEAdvertising
{
public:
    int starts = 0;
    bool setName(const char*) { return true; }
    bool addServiceUUID(const char*) { return true; }
    bool addServiceUUID(const NimBLEUUID&) { return true; }
    void enableScanResponse(bool) {}
    bool start() { ++starts; return true; }
};

class NimBLEServer
{
public:
    int parameterUpdates = 0;
    void updateConnParams(uint16_t, uint16_t min, uint16_t max, uint16_t latency, uint16_t timeout)
    {
        assert(min == 12 && max == 24 && latency == 0 && timeout == 200);
        ++parameterUpdates;
    }
    NimBLEServerCallbacks* callbacks = nullptr;
    NimBLEService service;
    NimBLEService configurationService;
    NimBLEAdvertising advertising;
    uint16_t mtu = 27;
    bool started = false;
    void setCallbacks(NimBLEServerCallbacks* handler, bool owned)
    {
        assert(!owned);
        callbacks = handler;
    }
    void registerAppServicesFirst(bool enabled) { assert(!enabled); }
    NimBLEService* createService(const char *uuid)
    {
        if (std::string(uuid) == "9c7b1000-6e4d-4f42-9a31-2d5e8b0c7f60")
        {
            configurationService.characteristic.attribute = 43;
            return &configurationService;
        }
        return &service;
    }
    bool start() { started = true; return true; }
    NimBLEAdvertising* getAdvertising() { return &advertising; }
    uint16_t getPeerMTU(uint16_t) const { return mtu; }
};

class NimBLEDevice
{
public:
    static inline NimBLEClient client;
    static inline NimBLEServer server;
    static void init(const char*) {}
    static void setSecurityIOCap(int) {}
    static void setSecurityAuth(bool, bool, bool) {}
    static void setPower(int) {}
    static void deleteAllBonds() {}
    static NimBLEClient* getDisconnectedClient() { return &client; }
    static NimBLEClient* createClient() { return &client; }
    static NimBLEServer* createServer() { return &server; }
    static NimBLEServer* getServer() { return &server; }
    static NimBLEAdvertising* getAdvertising() { return server.getAdvertising(); }
    static void injectPassKey(NimBLEConnInfo&, int) {}
    static void injectConfirmPasskey(NimBLEConnInfo&, bool) {}
};
