#ifndef BLE_INTERFACE_H
#define BLE_INTERFACE_H

#include "NimBLEDevice.h"
#include "BleEndpoint.h"

/**
 *
 */
class BleApiService : public NimBLECharacteristicCallbacks, public NimBLEDescriptorCallbacks, public NimBLEServerCallbacks, public BleEndpoint
{
public:
    BleApiService();

    void setup(BleEndpoint* endpoint);
    bool Send(const uint8_t* data, size_t length) override;

    // NimBLEServerCallbacks
    void onConnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo) override;
    void onDisconnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo, int reason) override;

    // NimBLECharacteristicCallbacks
    void onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo) override;

    // NimBLESecurityCallbacks
    void onAuthenticationComplete(NimBLEConnInfo& connInfo) override;

private:
    NimBLECharacteristic* midiChannel;
    uint16_t midiConnectionHandle;
    BleEndpoint* endpoint;

    SemaphoreHandle_t lockChannel;    
};

#endif // BLE_INTERFACE_H
