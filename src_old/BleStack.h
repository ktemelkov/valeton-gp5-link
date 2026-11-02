#ifndef BLE_STACK_H
#define BLE_STACK_H

#include <stddef.h>
#include <stdint.h>

#include "BleSync.h"

#include <esp_bt.h>
#include <esp_gap_ble_api.h>
#include <esp_gattc_api.h>

/**
 * Owns the lifecycle of the ESP32 Bluetooth controller and the Bluedroid host
 * stack.
 *
 * This is the single place that touches esp_bt_controller_*, esp_bluedroid_*
 * and the global security parameters. Neither the scanner nor the client may
 * bring the stack up or tear it down - they only ask for it to be ready, which
 * keeps one owner for a resource that is process-wide (Single Responsibility,
 * and the reason the class is a singleton rather than an instance member).
 *
 * The stack is deliberately never shut down: this firmware runs BLE for the
 * whole lifetime of the app and deep-sleeps instead, and Bluedroid's disable
 * path is not safe to re-enter from a callback context.
 */
class BleStack
{
public:
    /**
     * The one and only instance. Construction is cheap and does not touch the
     * hardware, so it is safe to reach this from another object's constructor.
     */
    static BleStack &instance();

    /**
     * Bring the controller and Bluedroid up if that has not happened yet.
     * Safe to call from several places and more than once.
     */
    bool ensureStarted();

    /**
     * True once the controller and Bluedroid are both enabled.
     */
    bool isRunning() const;

    /**
     * Configure the pairing behaviour shared by every consumer: no input, no
     * output, bonding with Secure Connections, and a static passkey of 000000
     * for the peers that insist on passkey entry.
     */
    bool configureSecurity();

    /**
     * Raise the transmit power used while scanning to the controller maximum,
     * matching what the NimBLE based scanner did with setPower(ESP_PWR_LVL_P9).
     */
    void applyTxPower();

    /**
     * Observer registration.
     *
     * GAP events are a single broadcast stream that several consumers need at
     * once: the scanner wants the report and scan-completion events while the
     * client wants the security, passkey and connection-parameter events, and
     * both are alive simultaneously. So GAP uses a small observer list.
     *
     * GATTC events, in contrast, are addressed to a registered application, and
     * this firmware registers exactly one - the MIDI client - so one slot is
     * enough and no fan-out is invented for a case that does not exist.
     *
     * Registration is bounded and idempotent; unregistering a slot that is not
     * registered is a no-op. Observers must unregister before they are
     * destroyed, which their destructors do.
     */
    static const size_t kMaxGapObservers = 4;

    class GapObserver
    {
    public:
        virtual ~GapObserver() {}
        virtual void onGapEvent(esp_gap_ble_cb_event_t event,
                                esp_ble_gap_cb_param_t *param) = 0;
    };

    class GattcObserver
    {
    public:
        virtual ~GattcObserver() {}
        virtual void onGattcEvent(esp_gattc_cb_event_t event,
                                  esp_gatt_if_t gattcIf,
                                  esp_ble_gattc_cb_param_t *param) = 0;
    };

    bool addGapObserver(GapObserver *observer);
    void removeGapObserver(GapObserver *observer);
    void registerGattcObserver(GattcObserver *observer);

    BleStack(const BleStack &) = delete;
    BleStack &operator=(const BleStack &) = delete;

private:
    BleStack();

    bool startController();
    bool startHost();

    static void gapCallback(esp_gap_ble_cb_event_t event,
                            esp_ble_gap_cb_param_t *param);
    static void gattcCallback(esp_gattc_cb_event_t event,
                              esp_gatt_if_t gattcIf,
                              esp_ble_gattc_cb_param_t *param);

    mutable BleMutex mutex_;
    bool running_;
    GapObserver *gapObservers_[kMaxGapObservers];
    GattcObserver *gattcObserver_;
};

#endif // BLE_STACK_H
