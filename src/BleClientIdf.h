#ifndef BLE_CLIENT_IDF_H
#define BLE_CLIENT_IDF_H

#include <stdint.h>

#include "BleGatt.h"
#include "BleStack.h"
#include "BleSync.h"
#include "IBleClient.h"

/**
 * MIDI GATT client built directly on the ESP-IDF Bluedroid GATTC API.
 *
 * Structure follows the single-responsibility split the rest of the port uses:
 * BleStack owns the controller and the callback dispatch, BleGattCache knows
 * how to read the cached GATT database, and this class owns exactly one thing
 * - the state of a single connection to a GP-5 and the MIDI write path.
 *
 * Concurrency model
 * -----------------
 * Bluedroid delivers every GATTC and GAP event on the Bluetooth host task,
 * while connect()/send()/disconnect() are called from loopTask. So:
 *
 *  - every field below is only touched with mutex_ held, and no method holds
 *    the mutex across a blocking wait, which would stall the host task and
 *    deadlock the completion it is waiting for;
 *  - asynchronous commands are turned into synchronous operations by waiting
 *    on a BleSignal that the event handler posts. That is what makes
 *    connect() return only when the link is genuinely usable, instead of
 *    returning while the HCI command is still queued - the race that forced
 *    the NimBLE version to poll ble_gap_conn_active() by hand;
 *  - waits are bounded, so a peripheral that never answers cannot wedge the
 *    main loop; the caller goes back to scanning instead.
 */
class BleClientIdf : public IBleClient,
                     public BleStack::GapObserver,
                     public BleStack::GattcObserver
{
public:
    BleClientIdf();
    ~BleClientIdf() override;

    bool connect(const BleAdvertisedDevice *advDevice) override;
    bool disconnect() override;
    bool subscribe(BleNotifyCallback callback) override;
    bool send(const uint8_t *data, size_t length) override;
    bool isConnected() const override;
    bool isAllowedDevice(const BleAdvertisedDevice *advDevice) const override;

    // BleStack observers - these run on the Bluetooth host task.
    void onGapEvent(esp_gap_ble_cb_event_t event,
                    esp_ble_gap_cb_param_t *param) override;
    void onGattcEvent(esp_gattc_cb_event_t event, esp_gatt_if_t gattcIf,
                      esp_ble_gattc_cb_param_t *param) override;

private:
    static const int kConnectAttempts = 3;
    static const uint32_t kOpenTimeoutMs = 5000;
    static const uint32_t kDiscoveryTimeoutMs = 5000;
    static const uint32_t kCommandTimeoutMs = 1000;

    // Connection parameters to ask the peer for: 15 ms interval, no latency,
    // 2 s supervision timeout - the same values the NimBLE client used.
    static const uint16_t kConnInterval = 12;
    static const uint16_t kConnLatency = 0;
    static const uint16_t kConnSupervisionTimeout = 200;

    // A peer that pushes an interval this long, or a timeout this short, is
    // trying to make the link expensive or fragile; re-assert our own instead.
    static const uint16_t kMaxAcceptableInterval = 320;
    static const uint16_t kMinAcceptableTimeout = 100;

    bool prepareApp();
    bool forgetBond(const uint8_t address[6]);
    bool openLink(const BleAdvertisedDevice &device);
    bool discoverMidiChannel();
    void resetConnectionState();
    bool awaitIdle(uint32_t timeoutMs);
    void handleSecurityEvent(esp_gap_ble_cb_event_t event,
                             esp_ble_gap_cb_param_t *param);
    bool isPeer(const uint8_t address[6]) const;

    mutable BleMutex mutex_;

    // One signal per asynchronous command whose completion matters. They are
    // separate objects because two commands can otherwise complete out of
    // order and wake the wrong waiter.
    BleSignal appRegistered_;
    BleSignal linkUp_;
    BleSignal linkDown_;
    BleSignal discoveryDone_;

    esp_gatt_if_t gattcIf_;
    uint16_t connId_;
    bool connected_;
    bool linkClosing_;
    uint8_t peerAddress_[BleAdvertisedDevice::kAddressLength];
    uint8_t peerAddressType_;

    BleGattCache::Characteristic midiChar_;
    BleNotifyCallback notifyCallback_;
};

#endif // BLE_CLIENT_IDF_H
