#include "BleClientIdf.h"

#include <string.h>

#include <esp_gatt_defs.h>

#include "BleAdvDataParser.h"
#include "debug.h"

// Target 128-bit Service and Characteristic UUIDs, in the canonical
// most-significant-byte-first order used throughout this port.
static const char *MIDI_Service_UUID_Str =
    "03B80E5A-EDE8-4B33-A751-6CE34EC4C700";
static const char *MIDI_Char_UUID_Str =
    "7772E5DB-3868-4112-A1A9-F2669D106BF3";

static const char *BLE_Device_Name = "GP-5 BLE Kalin";

// Bluedroid identifies a GATTC application by a 16-bit id that it reuses as an
// internal uuid16, so it has to be unique among registered profiles.
static const uint16_t kMidiAppId = 0x5A11;

// Cached, parsed forms of the two UUIDs. Parsed once at static-init time so
// neither the scan path nor the connect path does string work.
struct MidiUuids
{
    uint8_t service[16];
    uint8_t characteristic[16];

    MidiUuids()
    {
        if (!BleAdvDataParser::uuid128FromText(MIDI_Service_UUID_Str, service))
        {
            memset(service, 0, sizeof(service));
        }

        if (!BleAdvDataParser::uuid128FromText(MIDI_Char_UUID_Str,
                                               characteristic))
        {
            memset(characteristic, 0, sizeof(characteristic));
        }
    }
};

static const MidiUuids &midiUuids()
{
    static MidiUuids uuids;
    return uuids;
}

BleClientIdf::BleClientIdf()
    : gattcIf_(ESP_GATT_IF_NONE)
    , connId_(0)
    , connected_(false)
    , linkClosing_(false)
    , notifyCallback_(nullptr)
{
    memset(peerAddress_, 0, sizeof(peerAddress_));
    peerAddressType_ = 0;
}

BleClientIdf::~BleClientIdf()
{
    BleStack::instance().removeGapObserver(this);
    BleStack::instance().registerGattcObserver(nullptr);
}

bool BleClientIdf::isAllowedDevice(
    const BleAdvertisedDevice *advDevice) const
{
    if (!advDevice)
    {
        return false;
    }

    return advDevice->isConnectable() &&
           advDevice->isAdvertisingService(midiUuids().service) &&
           advDevice->getName().compare(BLE_Device_Name) == 0;
}

bool BleClientIdf::isConnected() const
{
    BleLock lock(mutex_);
    return connected_;
}

bool BleClientIdf::prepareApp()
{
    BleStack &stack = BleStack::instance();

    if (!stack.ensureStarted())
    {
        return false;
    }

    if (!stack.configureSecurity())
    {
        return false;
    }

    // GAP is needed for the security dialogue and connection-parameter events;
    // GATTC for everything about the link itself. Both registrations are
    // idempotent, so calling this before every connect is cheap.
    stack.addGapObserver(this);
    stack.registerGattcObserver(this);

    {
        // Registering twice would replace the interface the stack already gave
        // us, so only the first connect pays for it.
        BleLock lock(mutex_);
        if (gattcIf_ != ESP_GATT_IF_NONE)
        {
            return true;
        }
    }

    uint16_t appId = kMidiAppId;
    if (esp_ble_gattc_app_register(appId) != ESP_OK)
    {
        DEBUG_MSG("%s", "esp_ble_gattc_app_register failed.\n");
        return false;
    }

    if (!appRegistered_.wait(kCommandTimeoutMs))
    {
        DEBUG_MSG("%s", "GATTC app registration was not acknowledged.\n");
        return false;
    }

    BleLock lock(mutex_);
    return gattcIf_ != ESP_GATT_IF_NONE;
}

bool BleClientIdf::forgetBond(const uint8_t address[6])
{
    // Returns the count directly, or ESP_FAIL when the security database is not
    // ready; either way there is nothing to remove.
    const int devNum = esp_ble_get_bond_device_num();
    if (devNum <= 0)
    {
        return true;
    }

    esp_ble_bond_dev_t list[8];
    int listNum = static_cast<int>(sizeof(list) / sizeof(list[0]));
    if (listNum > devNum)
    {
        listNum = devNum;
    }

    if (esp_ble_get_bond_device_list(&listNum, list) != ESP_OK)
    {
        return false;
    }

    for (int i = 0; i < listNum; ++i)
    {
        if (memcmp(list[i].bd_addr, address,
                   BleAdvertisedDevice::kAddressLength) != 0)
        {
            continue;
        }

        // Bond keys live in NVS and survive deep sleep, but the GP-5 may have
        // forgotten them meanwhile. Offering a stale key makes the peripheral
        // refuse security and drop the link right after it comes up, which is
        // the reason-574 disconnect the NimBLE client worked around the same
        // way. Removing the bond first is cheaper than detecting the failure.
        // A removal that fails is logged but not fatal - the retry loop is what
        // decides whether the link is usable.
        esp_err_t err = esp_ble_remove_bond_device(list[i].bd_addr);
        DEBUG_MSG("Removing stored bond, status: %d\n", err);
    }

    return true;
}

bool BleClientIdf::awaitIdle(uint32_t timeoutMs)
{
    // The previous link has to be fully released before a new one is opened,
    // otherwise the controller answers the open with "already exists". Scan
    // teardown no longer needs waiting for: StopScanning() blocks until the
    // controller confirms, which the NimBLE path could not do.
    const uint64_t deadlineUs =
        static_cast<uint64_t>(esp_timer_get_time()) +
        static_cast<uint64_t>(timeoutMs) * 1000ULL;

    for (;;)
    {
        {
            BleLock lock(mutex_);
            if (!linkClosing_ && connId_ == 0)
            {
                return true;
            }
        }

        if (static_cast<uint64_t>(esp_timer_get_time()) >= deadlineUs)
        {
            return false;
        }

        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

bool BleClientIdf::openLink(const BleAdvertisedDevice &device)
{
    esp_gatt_if_t gattcIf;

    {
        BleLock lock(mutex_);
        gattcIf = gattcIf_;
    }

    if (gattcIf == ESP_GATT_IF_NONE)
    {
        return false;
    }

    esp_ble_gap_conn_params_t phy1m;
    memset(&phy1m, 0, sizeof(phy1m));
    phy1m.scan_interval = 0x40;
    phy1m.scan_window = 0x40;
    phy1m.interval_min = kConnInterval;
    phy1m.interval_max = kConnInterval;
    phy1m.latency = kConnLatency;
    phy1m.supervision_timeout = kConnSupervisionTimeout;

    // esp_ble_gap_set_prefer_conn_params() is compiled out of this build
    // because CONFIG_BT_BLE_42_FEATURES_SUPPORTED is off, so the extended
    // variant is used. It only records a preference for the peer, which is
    // exactly what is wanted before the link exists.
    esp_err_t err = esp_ble_gap_prefer_ext_connect_params_set(
        const_cast<uint8_t *>(device.address), ESP_BLE_GAP_PHY_1M_PREF_MASK,
        &phy1m, nullptr, nullptr);
    if (err != ESP_OK)
    {
        DEBUG_MSG("prefer_ext_connect_params failed: %d\n", err);
    }

    linkUp_.clear();

    // is_direct=false: the peer is not resolving our address, so plain direct
    // connection advertising is not applicable. aux_open is the extended
    // counterpart of esp_ble_gattc_open(), which is also unavailable here.
    err = esp_ble_gattc_aux_open(gattcIf, const_cast<uint8_t *>(device.address),
                                 static_cast<esp_ble_addr_type_t>(
                                     device.addressType),
                                 false);
    if (err != ESP_OK)
    {
        DEBUG_MSG("esp_ble_gattc_aux_open failed: %d\n", err);
        return false;
    }

    if (!linkUp_.wait(kOpenTimeoutMs))
    {
        DEBUG_MSG("%s", "No response to the connection request.\n");
        return false;
    }

    BleLock lock(mutex_);
    return connId_ != 0;
}

bool BleClientIdf::discoverMidiChannel()
{
    uint16_t connId;
    esp_gatt_if_t gattcIf;

    {
        BleLock lock(mutex_);
        connId = connId_;
        gattcIf = gattcIf_;

        if (connId == 0 || gattcIf == ESP_GATT_IF_NONE)
        {
            return false;
        }
    }

    discoveryDone_.clear();

    // A NULL filter discovers the whole database, which is what the cache is
    // then read from. Filtering by service UUID would work too, but discovering
    // everything keeps the lookup path identical for any future characteristic.
    esp_err_t err = esp_ble_gattc_search_service(gattcIf, connId, nullptr);
    if (err != ESP_OK)
    {
        DEBUG_MSG("esp_ble_gattc_search_service failed: %d\n", err);
        return false;
    }

    if (!discoveryDone_.wait(kDiscoveryTimeoutMs))
    {
        DEBUG_MSG("%s", "Service discovery did not complete.\n");
        return false;
    }

    // Read from the stack's cache, not from an object we own: Bluedroid keeps
    // the discovered database internally and hands out handles on request.
    BleGattCache::Characteristic characteristic = BleGattCache::findCharacteristic(
        gattcIf, connId, midiUuids().service, midiUuids().characteristic, 0);

    if (!characteristic.found())
    {
        DEBUG_MSG("%s", "MIDI characteristic not found on peer.\n");
        return false;
    }

    BleLock lock(mutex_);
    midiChar_ = characteristic;
    return true;
}

bool BleClientIdf::connect(const BleAdvertisedDevice *advDevice)
{
    if (!advDevice)
    {
        return false;
    }

    resetConnectionState();

    if (!prepareApp())
    {
        return false;
    }

    {
        BleLock lock(mutex_);
        memcpy(peerAddress_, advDevice->address, sizeof(peerAddress_));
        peerAddressType_ = advDevice->addressType;
    }

    forgetBond(advDevice->address);

    for (int attempt = 1; attempt <= kConnectAttempts; ++attempt)
    {
        if (!awaitIdle(kCommandTimeoutMs))
        {
            DEBUG_MSG("%s", "BLE link still busy before connect attempt.\n");
            continue;
        }

        if (!openLink(*advDevice))
        {
            DEBUG_MSG("Attempt %d/%d failed to connect.\n", attempt,
                      kConnectAttempts);
            continue;
        }

        if (discoverMidiChannel())
        {
            BleLock lock(mutex_);
            connected_ = true;
            DEBUG_MSG("Connected to %s.\n", BLE_Device_Name);
            return true;
        }

        // The database was wrong, so the link is useless even though it is up.
        // disconnect() releases it and the next attempt starts clean.
        disconnect();
    }

    return false;
}

void BleClientIdf::resetConnectionState()
{
    BleLock lock(mutex_);
    connected_ = false;
    linkClosing_ = false;
    connId_ = 0;
    midiChar_ = BleGattCache::Characteristic();
}

bool BleClientIdf::disconnect()
{
    uint8_t peer[BleAdvertisedDevice::kAddressLength];

    {
        BleLock lock(mutex_);
        memcpy(peer, peerAddress_, sizeof(peer));

        if (gattcIf_ == ESP_GATT_IF_NONE)
        {
            return true;
        }

        if (connId_ == 0)
        {
            return true;
        }

        connected_ = false;
    }

    linkDown_.clear();

    esp_err_t err = esp_ble_gap_disconnect(peer);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        DEBUG_MSG("esp_ble_gap_disconnect failed: %d\n", err);
    }

    if (!linkDown_.wait(kCommandTimeoutMs))
    {
        // The link may already be gone; force the state so the next connect does
        // not inherit a stale connection id.
        DEBUG_MSG("%s", "Disconnect was not acknowledged.\n");
        BleLock lock(mutex_);
        connId_ = 0;
        linkClosing_ = false;
    }

    return true;
}

bool BleClientIdf::subscribe(BleNotifyCallback callback)
{
    uint16_t valueHandle;
    uint16_t cccdHandle;
    uint16_t connId;
    esp_gatt_if_t gattcIf;
    uint8_t peer[BleAdvertisedDevice::kAddressLength];

    {
        BleLock lock(mutex_);
        if (!connected_)
        {
            return false;
        }

        valueHandle = midiChar_.valueHandle;
        cccdHandle = midiChar_.cccdHandle;
        connId = connId_;
        gattcIf = gattcIf_;
        memcpy(peer, peerAddress_, sizeof(peer));
        notifyCallback_ = callback;
    }

    if (esp_ble_gattc_register_for_notify(gattcIf, peer, valueHandle) != ESP_OK)
    {
        DEBUG_MSG("%s", "esp_ble_gattc_register_for_notify failed.\n");
        return false;
    }

    if (cccdHandle == 0)
    {
        // Nothing to enable on the peer; the local registration alone is all
        // this side can do.
        return true;
    }

    // Notifications are off by default, so the CCCD has to be written even
    // though the MIDI transport itself is write-only in practice.
    uint16_t cccdValue = 0x0001;

    // Returns esp_err_t, not esp_gatt_status_t: the ATT status for the write
    // arrives separately as ESP_GATTC_WRITE_DESCR_EVT.
    esp_err_t err = esp_ble_gattc_write_char_descr(
        gattcIf, connId, cccdHandle, sizeof(cccdValue),
        reinterpret_cast<uint8_t *>(&cccdValue), ESP_GATT_WRITE_TYPE_RSP,
        ESP_GATT_AUTH_REQ_NONE);

    if (err != ESP_OK)
    {
        DEBUG_MSG("CCCD write rejected: %d\n", err);
        return false;
    }

    return true;
}

bool BleClientIdf::send(const uint8_t *data, size_t length)
{
    uint16_t handle;
    uint16_t connId;
    esp_gatt_if_t gattcIf;

    {
        BleLock lock(mutex_);

        if (!connected_ || !midiChar_.found())
        {
            return false;
        }

        handle = midiChar_.valueHandle;
        connId = connId_;
        gattcIf = gattcIf_;
    }

    // Write-without-response, matching the NimBLE client's
    // writeValue(data, length, false). MIDI over BLE never waits for the ATT
    // acknowledgement, and waiting would add a round trip per preset change.
    esp_err_t err = esp_ble_gattc_write_char(
        gattcIf, connId, handle, static_cast<uint16_t>(length),
        const_cast<uint8_t *>(data), ESP_GATT_WRITE_TYPE_NO_RSP,
        ESP_GATT_AUTH_REQ_NONE);

    if (err != ESP_OK)
    {
        DEBUG_MSG("MIDI write rejected: %d\n", err);
        return false;
    }

    return true;
}

bool BleClientIdf::isPeer(const uint8_t address[6]) const
{
    // Called from the event handlers, which never hold mutex_; the peer address
    // only changes between connect() calls, so a plain read is enough here.
    return memcmp(peerAddress_, address, sizeof(peerAddress_)) == 0;
}

void BleClientIdf::handleSecurityEvent(esp_gap_ble_cb_event_t event,
                                       esp_ble_gap_cb_param_t *param)
{
    switch (event)
    {
    case ESP_GAP_BLE_SEC_REQ_EVT:
        // Accept every pairing request: the allow-list already decided that
        // this peer is a GP-5.
        esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr, true);
        break;

    case ESP_GAP_BLE_PASSKEY_REQ_EVT:
        // The peer asks us to enter its passkey. Injecting 000000 is what the
        // NimBLE client did through onPassKeyEntry().
        esp_ble_passkey_reply(param->ble_security.ble_req.bd_addr, true, 0);
        break;

    case ESP_GAP_BLE_PASSKEY_NOTIF_EVT:
    case ESP_GAP_BLE_NC_REQ_EVT:
        // We are the display/verifier side: accept the key the peer showed
        // without user interaction, matching onConfirmPasskey().
        esp_ble_confirm_reply(param->ble_security.key_notif.bd_addr, true);
        break;

    case ESP_GAP_BLE_AUTH_CMPL_EVT:
        DEBUG_MSG("Pairing %s, reason: %d\n",
                  param->ble_security.auth_cmpl.success ? "succeeded"
                                                        : "failed",
                  param->ble_security.auth_cmpl.fail_reason);
        break;

    case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
    {
        // Bluedroid gives no hook for refusing a peer's connection-parameter
        // update, so the only lever available is to push our own preference
        // back after the fact, which is only legal while the link is up. The
        // parameter struct is an anonymous union member, hence auto.
        const auto &p = param->update_conn_params;

        if (p.timeout < kMinAcceptableTimeout ||
            p.conn_int > kMaxAcceptableInterval)
        {
            DEBUG_MSG("Peer chose interval %d timeout %d, re-asserting.\n",
                      p.conn_int, p.timeout);

            esp_ble_conn_update_params_t ours;
            memset(&ours, 0, sizeof(ours));
            memcpy(ours.bda, p.bda, sizeof(ours.bda));
            ours.min_int = kConnInterval;
            ours.max_int = kConnInterval;
            ours.latency = kConnLatency;
            ours.timeout = kConnSupervisionTimeout;

            esp_ble_gap_update_conn_params(&ours);
        }
        break;
    }

    default:
        break;
    }
}

void BleClientIdf::onGapEvent(esp_gap_ble_cb_event_t event,
                              esp_ble_gap_cb_param_t *param)
{
    if (!param)
    {
        return;
    }

    switch (event)
    {
    case ESP_GAP_BLE_SEC_REQ_EVT:
    case ESP_GAP_BLE_PASSKEY_REQ_EVT:
    case ESP_GAP_BLE_PASSKEY_NOTIF_EVT:
    case ESP_GAP_BLE_NC_REQ_EVT:
    case ESP_GAP_BLE_AUTH_CMPL_EVT:
    case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
        handleSecurityEvent(event, param);
        break;

    default:
        // Scan and discovery events belong to the scanner, which is registered
        // as a peer observer on the same list.
        break;
    }
}

void BleClientIdf::onGattcEvent(esp_gattc_cb_event_t event,
                                esp_gatt_if_t gattcIf,
                                esp_ble_gattc_cb_param_t *param)
{
    if (!param)
    {
        return;
    }

    switch (event)
    {
    case ESP_GATTC_REG_EVT:
        if (param->reg.app_id == kMidiAppId)
        {
            BleLock lock(mutex_);
            gattcIf_ = gattcIf;
            appRegistered_.notify();
        }
        break;

    case ESP_GATTC_OPEN_EVT:
    {
        BleLock lock(mutex_);

        if (param->open.status == ESP_GATT_OK)
        {
            connId_ = param->open.conn_id;
            midiChar_.mtu = param->open.mtu;
            memcpy(peerAddress_, param->open.remote_bda,
                   sizeof(peerAddress_));
        }
        else
        {
            connId_ = 0;
            DEBUG_MSG("Open failed, status: %d\n", param->open.status);
        }

        linkUp_.notify();
        break;
    }

    case ESP_GATTC_SEARCH_CMPL_EVT:
        discoveryDone_.notify();
        break;

    case ESP_GATTC_NOTIFY_EVT:
    {
        BleNotifyCallback callback;

        {
            BleLock lock(mutex_);
            callback = notifyCallback_;
        }

        // The value buffer belongs to the stack and is only valid during this
        // callback, so it is handed over synchronously and not stored.
        if (callback)
        {
            callback(param->notify.value, param->notify.value_len,
                     param->notify.is_notify);
        }
        break;
    }

    case ESP_GATTC_DISCONNECT_EVT:
    {
        // Bluedroid raises both DISCONNECT_EVT and CLOSE_EVT for the same
        // teardown; only the first one to arrive is allowed to release the
        // interface, or the second would close a stale one.
        bool first = false;

        {
            BleLock lock(mutex_);
            first = !linkClosing_;
            linkClosing_ = true;
            connected_ = false;
        }

        if (first)
        {
            DEBUG_MSG("Link lost, reason: %d\n", param->disconnect.reason);
        }

        linkDown_.notify();
        break;
    }

    case ESP_GATTC_CLOSE_EVT:
    {
        bool release = false;

        {
            BleLock lock(mutex_);
            release = !linkClosing_ || connId_ != 0;
            linkClosing_ = true;
            connected_ = false;
        }

        if (release)
        {
            // Releases the GATTC interface slot back to the stack; without it
            // the next connect runs out of interfaces. Called from the callback
            // context, which is what the IDF examples do.
            esp_ble_gattc_close(gattcIf, param->close.conn_id);
        }

        {
            BleLock lock(mutex_);
            connId_ = 0;
            midiChar_ = BleGattCache::Characteristic();
        }

        linkDown_.notify();
        break;
    }

    case ESP_GATTC_WRITE_CHAR_EVT:
    case ESP_GATTC_WRITE_DESCR_EVT:
        if (param->write.status != ESP_GATT_OK)
        {
            DEBUG_MSG("Write failed, status: %d\n", param->write.status);
        }
        break;

    case ESP_GATTC_REG_FOR_NOTIFY_EVT:
        if (param->reg_for_notify.status != ESP_GATT_OK)
        {
            DEBUG_MSG("Notify registration failed, status: %d\n",
                      param->reg_for_notify.status);
        }
        break;

    case ESP_GATTC_CONGEST_EVT:
        // Only interesting if writes ever start failing; logged so a stall in
        // preset changes can be traced back to flow control.
        if (param->congest.congested)
        {
            DEBUG_MSG("%s", "GATTC transmit congested.\n");
        }
        break;

    default:
        break;
    }
}
