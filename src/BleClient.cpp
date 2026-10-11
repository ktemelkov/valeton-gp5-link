#include "BleClient.h"

#include "debug.h"

// Target 128-bit Service and Characteristic UUIDs
static const char *MIDI_Service_UUID_Str = "03B80E5A-EDE8-4B33-A751-6CE34EC4C700";
static const char *MIDI_Char_UUID_Str = "7772E5DB-3868-4112-A1A9-F2669D106BF3";

/**
 *
 */
bool BleClient::connect(const NimBLEAdvertisedDevice *advDevice)
{
    if (!advDevice->isAdvertisingService(NimBLEUUID(MIDI_Service_UUID_Str)) || advDevice->getName().compare(0, 7, "RemTone") != 0)
    {
        return false;
    }

    NimBLEClient *pClient = NimBLEDevice::getDisconnectedClient();

    if (!pClient)
    {
        pClient = NimBLEDevice::createClient(advDevice->getAddress());
        pClient->setSelfDelete(true, true);

        if (!pClient)
        {
            DEBUG_MSG("%s", "Failed to create client.\n");
            return false;
        }
    }

    pClient->setClientCallbacks(this, false);
    pClient->setConnectionParams(12, 12, 0, 150);
    pClient->setConnectTimeout(5 * 1000);

    if (!pClient->connect(advDevice, true))
    {
        DEBUG_MSG("%s", "Failed to connect.\n");
        return false;
    }

    NimBLERemoteService *pSvc = pClient->getService(MIDI_Service_UUID_Str);

    if (!pSvc)
    {
        DEBUG_MSG("%s", "MIDI service not found.\n");
        pClient->disconnect();
        return false;
    }

    NimBLERemoteCharacteristic *pChr = pSvc->getCharacteristic(MIDI_Char_UUID_Str);

    if (!pChr)
    {
        DEBUG_MSG("%s", "MIDI characteristic not found.\n");
        pClient->disconnect();
        return false;
    }

    midiChannel = pChr;    
    connected = true;    

    return true;
}

/**
 *
 */
bool BleClient::disconnect()
{
    if (!connected)
    {
        return false;
    }

    NimBLEClient *pClient = midiChannel->getClient();

    if (!pClient)
    {
        DEBUG_MSG("%s", "Failed to get client from characteristic.\n");
        return false;
    }

    pClient->disconnect();
    midiChannel = nullptr;
    connected = false;

    return true;
}

/**
 *
 */
bool BleClient::subscribe(void (*deviceNotifyCB)(NimBLERemoteCharacteristic *, uint8_t *, size_t, bool))
{
    if (!connected || !midiChannel) {
        DEBUG_MSG("%s", "Not connected to a device.\n");
        return false;
    }

    if (!midiChannel->canNotify() || !midiChannel->subscribe(true, deviceNotifyCB))
    {
      DEBUG_MSG("%s", "Failed to subscribe to notifications.\n");
      return false;
    }

    return true;
}

/**
 *
 */
bool BleClient::send(uint8_t *data, size_t length)
{
    if (!connected || !midiChannel) {
        DEBUG_MSG("%s", "Not connected to a device.\n");
        return false;
    }

    return midiChannel->writeValue(data, length, false);
}

/**
 *
 */
void BleClient::onConnect(NimBLEClient *pClient)
{
    pClient->updateConnParams(60, 120, 0, 200);

    DEBUG_MSG("Connected to: %s\n", pClient->getPeerAddress().toString().c_str());
    DEBUG_MSG("RSSI: %d\n", pClient->getRssi());
}

/**
 *
 */
void BleClient::onConnectFail(NimBLEClient *pClient, int reason)
{
    DEBUG_MSG("Failed to connect to: %s, reason: %d\n", pClient->getPeerAddress().toString().c_str(), reason);
    midiChannel = nullptr;
    connected = false;
}

/**
 *
 */
void BleClient::onDisconnect(NimBLEClient *pClient, int reason)
{
    DEBUG_MSG("Disconnected from: %s, reason: %d\n", pClient->getPeerAddress().toString().c_str(), reason);
    midiChannel = nullptr;
    connected = false;
}