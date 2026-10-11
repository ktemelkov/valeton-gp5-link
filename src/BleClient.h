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

private:
  void onConnect(NimBLEClient *pClient);
  void onConnectFail(NimBLEClient *pClient, int reason);
  void onDisconnect(NimBLEClient *pClient, int reason);

private:
    bool connected = false;
    NimBLERemoteCharacteristic* midiChannel = nullptr;
};

#endif // __BLE_CONNECTION_H