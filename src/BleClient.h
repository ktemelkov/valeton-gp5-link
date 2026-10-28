#ifndef __BLE_CONNECTION_H
#define __BLE_CONNECTION_H

#include <NimBLEDevice.h>


/**
 *
 */
class BleClient : public NimBLEClientCallbacks
{
public:
    bool connect(const NimBLEAdvertisedDevice *advDevice);
    bool disconnect();
    bool subscribe(void (*deviceNotifyCB)(NimBLERemoteCharacteristic*, uint8_t*, size_t, bool));
    bool send(uint8_t* data, size_t length);

    bool isConnected() const { return connected; }

    bool isAllowedDevice(const NimBLEAdvertisedDevice *advDevice) const;

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

private:
    bool connected = false;
    NimBLERemoteCharacteristic* midiChannel = nullptr;
};

#endif // __BLE_CONNECTION_H