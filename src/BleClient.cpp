#include "BleClient.h"

#include "debug.h"

// Target 128-bit Service and Characteristic UUIDs
static const char *MIDI_Service_UUID_Str = "03B80E5A-EDE8-4B33-A751-6CE34EC4C700";
static const char *MIDI_Char_UUID_Str = "7772E5DB-3868-4112-A1A9-F2669D106BF3";

static const char *BLE_Device_Name = "GP-5 BLE Kalin";


bool BleClient::isAllowedDevice(const NimBLEAdvertisedDevice *advDevice) const
{
    return advDevice->isConnectable() &&
           advDevice->isAdvertisingService(NimBLEUUID(MIDI_Service_UUID_Str)) &&
           advDevice->getName().compare(BLE_Device_Name) == 0;
}

/**
 *
 */
bool BleClient::connect(const NimBLEAdvertisedDevice *advDevice)
{
    connected   = false;
    midiChannel = nullptr;

    // Bond keys are stored in NVS and therefore survive a deep sleep, but the
    // GP-5 may have forgotten them in the meantime. If we then offer a stale
    // key the peripheral refuses the security procedure and terminates the
    // link as soon as it is up, which is what shows up as reason 574 (0x23E).
    // NimBLE only raises BLE_GAP_EVENT_REPEAT_PAIRING when *we* still hold a
    // bond, and NimBLEClient does not act on that event, so the reliable fix
    // is to never start a connection with keys that may be stale.
    if (NimBLEDevice::isBonded(advDevice->getAddress()))
    {
        DEBUG_MSG("%s", "Clearing stored bond for peer before connecting.\n");
        NimBLEDevice::deleteBond(advDevice->getAddress());
    }

    NimBLEClient *pClient = prepareClient();
    if (!pClient)
    {
        DEBUG_MSG("%s", "Failed to create client.\n");
        return false;
    }

    if (!pClient->connect(advDevice->getAddress(), false))
    {
        int reason = pClient->getLastError();
        DEBUG_MSG("Failed to connect, reason: %d (0x%X)\n", reason, reason);

        // Drop every key we know about and retry once from a clean state.
        NimBLEDevice::deleteAllBonds();

        // Give the host task time to finish tearing the failed link down,
        // otherwise the retry can come back with BLE_HS_EBUSY (2).
        delay(250);

        pClient = prepareClient();
        if (!pClient)
        {
            DEBUG_MSG("%s", "Failed to get a client for the retry.\n");
            return false;
        }

        if (!pClient->connect(advDevice->getAddress(), false))
        {
            reason = pClient->getLastError();
            DEBUG_MSG("Retry failed, reason: %d (0x%X)\n", reason, reason);
            return false;
        }

        DEBUG_MSG("%s", "Connected successfully on retry!\n");
    }

    DEBUG_MSG("%s", "Connected successfully! Discovering services...\n");

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
        {
            return nullptr;
        }
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
//    pClient->updateConnParams(60, 120, 0, 200);

    DEBUG_MSG("Connected to: %s\n", pClient->getPeerAddress().toString().c_str());
    DEBUG_MSG("RSSI: %d\n", pClient->getRssi());
}

/**
 *
 */
void BleClient::onConnectFail(NimBLEClient *pClient, int reason)
{
    DEBUG_MSG("Failed to connect to: %s, reason: %d (0x%X)\n",
              pClient->getPeerAddress().toString().c_str(), reason,
              reason);
    midiChannel = nullptr;
    connected = false;
}

/**
 *
 */
void BleClient::onDisconnect(NimBLEClient *pClient, int reason)
{
    DEBUG_MSG("Disconnected from: %s, reason: %d (0x%X)\n",
              pClient->getPeerAddress().toString().c_str(), reason,
              reason);
    midiChannel = nullptr;
    connected = false;
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
        DEBUG_MSG("Rejecting peer conn params: itvl %d-%d, latency %d, timeout %d\n",
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
    DEBUG_MSG("%s", "Passkey requested, injecting 000000\n");
    NimBLEDevice::injectPassKey(connInfo, 0);
}

/**
 * Pairing: numeric comparison (Secure Connections). We have no display/keyboard,
 * so accept the comparison automatically.
 */
void BleClient::onConfirmPasskey(NimBLEConnInfo &connInfo, uint32_t pin)
{
    DEBUG_MSG("Numeric comparison requested, accepting pin: %u\n", pin);
    NimBLEDevice::injectConfirmPasskey(connInfo, true);
}

/**
 * Pairing/encryption finished. If encryption failed the peer will very likely
 * drop the link, so log the state to make that visible.
 */
void BleClient::onAuthenticationComplete(NimBLEConnInfo &connInfo)
{
    DEBUG_MSG("Authentication complete. Encrypted: %d, Authenticated: %d\n",
              connInfo.isEncrypted(), connInfo.isAuthenticated());
}