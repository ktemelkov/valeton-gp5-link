#ifndef BLE_CLIENT_H
#define BLE_CLIENT_H

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <stdint.h>
#include <NimBLEDevice.h>

#include "BleEndpoint.h"

/**
 * 
 */
class BleClient : public NimBLEClientCallbacks, public BleEndpoint
{
public:
    BleClient();

    bool Connect(const NimBLEAddress &deviceAddress);
    bool Disconnect();
    bool Send(const uint8_t *data, size_t length) override;
    bool IsConnected() const;

    void setup(BleEndpoint* endpoint) { this->endpoint = endpoint; }

private:
  NimBLEClient *prepareClient();

  void onConnect(NimBLEClient *pClient) override;
  void onConnectFail(NimBLEClient *pClient, int reason) override;
  void onDisconnect(NimBLEClient *pClient, int reason) override;

  // Security / pairing callbacks
  bool onConnParamsUpdateRequest(NimBLEClient *pClient, const ble_gap_upd_params *params) override;
  void onPassKeyEntry(NimBLEConnInfo &connInfo) override;
  void onConfirmPasskey(NimBLEConnInfo &connInfo, uint32_t pin) override;
  void onAuthenticationComplete(NimBLEConnInfo &connInfo) override;

  void setMidiChannel(NimBLERemoteCharacteristic* channel);
 
  void deviceNotifyCB(NimBLERemoteCharacteristic* pChr, uint8_t* data, size_t length, bool isNotify);

private:
    NimBLERemoteCharacteristic* midiChannel;
    SemaphoreHandle_t lockChannel;
    BleEndpoint* endpoint;
};

#endif // BLE_CLIENT_H
