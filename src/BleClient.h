#ifndef BLECLIENT_H
#define BLECLIENT_H

#include <cstddef>
#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "host/ble_att.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"

/**
 * MIDI GATT client for the GP-5, built directly on the native NimBLE host.
 *
 * BleScanner owns the controller and the NimBLE host task - it is the one that
 * calls nimble_port_init() and brings ble_hs_cfg up - so this class assumes a
 * running stack and only ever issues commands against it.
 *
 * Concurrency
 * -----------
 * NimBLE delivers every GAP and GATT callback on its own "nimble_host" task,
 * while Connect()/Send()/Disconnect() are called from app_main's loop. Every
 * member below is therefore only touched with `lock` held, and no method holds
 * the lock across a blocking wait - that would stall the very callback it is
 * waiting for and deadlock the operation.
 *
 * Blocking model
 * --------------
 * Connect() is synchronous: it turns the asynchronous connect + discovery
 * sequence into one call that returns only once the MIDI characteristic has
 * been resolved or the attempt has been given up on. Each step waits on a
 * counting semaphore the callback side gives, with a bounded timeout, so a
 * peripheral that accepts the link and then goes silent sends the caller back
 * to scanning instead of wedging the main loop.
 */
class BleClient
{
    static constexpr const char *TAG = "BleClient";

    /**
     * MIDI over BLE service, 03B80E5A-EDE8-4B33-A751-6CE34EC4C700, stored
     * least-significant byte first the way NimBLE keeps UUIDs on air.
     *
     * BLE_UUID128_DECLARE is deliberately not used: it expands to
     * `((ble_uuid_t *) (&(ble_uuid128_t) {...}))`, a compound-literal cast that
     * is C-only and yields a pointer. BLE_UUID128_INIT gives the same aggregate
     * in a form C++ can turn into a constant object.
     */
    static constexpr ble_uuid128_t TARGET_SERVICE_UUID = BLE_UUID128_INIT(0x00, 0xC7, 0xC4, 0x4E, 0xE3, 0x6C, 0x51, 0xA7, 0x33, 0x4B, 0xE8, 0xED, 0x5A, 0x0E, 0xB8, 0x03);

    /**
     * The characteristic MIDI messages are written to,
     * 7772E5DB-3868-4112-A1A9-F2669D106BF3, in the same least-significant byte
     * first order as the service above. This is the only attribute ever written;
     * its declared properties, not a guess, decide the write type - see Send().
     */
    static constexpr ble_uuid128_t MIDI_CHARACTERISTIC_UUID = BLE_UUID128_INIT(0xF3, 0x6B, 0x10, 0x9D, 0x66, 0xF2, 0xA9, 0xA1, 0x12, 0x41, 0x68, 0x38, 0xDB, 0xE5, 0x72, 0x77);

    // Connection parameters to ask the peer for: 15 ms interval, no latency,
    // 2 s supervision timeout - the values the previous client used.
    static constexpr uint16_t kConnInterval = 12;
    static constexpr uint16_t kConnLatency = 0;
    static constexpr uint16_t kConnSupervisionTimeout = 200;

    // How long a single step may take. Bounded on purpose: a GP-5 that accepts
    // the link and then stops answering must return the caller to scanning.
    static constexpr uint32_t kConnectTimeoutMs = 5000;
    static constexpr uint32_t kDiscoveryTimeoutMs = 5000;
    static constexpr uint32_t kDisconnectTimeoutMs = 2000;
    static constexpr uint32_t kWriteTimeoutMs = 2000;

    // Bytes a single write spends before any MIDI byte fits. ATT_MTU already
    // counts the L2CAP payload, and both a Write Request and a Write Command
    // are just the 1-byte opcode plus the 2-byte attribute handle.
    static constexpr uint16_t kAttWriteOverhead = 3;

public:
    BleClient();
    ~BleClient();

    BleClient(const BleClient &) = delete;
    BleClient &operator=(const BleClient &) = delete;

    bool IsConnected();

    /**
     * Connect to @p device and resolve the MIDI characteristic.
     *
     * Blocks until the transport is ready to send or the attempt is given up
     * on; returns true only in the former case. A scan that is still running
     * makes the controller refuse the connect outright (BLE_HS_EBUSY), so
     * discovery has to be off first - the main loop already does that.
     */
    bool Connect(const ble_addr_t &device);

    /**
     * Tear the link down. Safe to call when not connected, and it waits for
     * the teardown to finish so the caller can go straight back to scanning.
     */
    void Disconnect();

    /**
     * Write @p length bytes to the MIDI characteristic.
     *
     * The write type is chosen from the property flags the peripheral itself
     * reported during discovery, never guessed: a request to an attribute that
     * only declares write-without-response is refused with
     * BLE_ATT_ERR_WRITE_NOT_PERMITTED. Write-without-response is preferred when
     * available - MIDI is a stream, so a per-message acknowledgement only adds
     * a round trip - and a request that needs an acknowledgement is used when it
     * does not, chunked through prepare/execute if it exceeds one PDU.
     *
     * @return true if the message was accepted by the stack, or, for a write
     *         that expects one, acknowledged by the peripheral.
     */
    bool Send(const uint8_t *data, size_t length);

private:
    // NimBLE takes plain C function pointers, so every entry point below is
    // static and recovers the instance from its `arg`/`cb_arg`.
    static int gapEvent(ble_gap_event *event, void *arg);
    static int discSvcEvent(uint16_t connHandle, const ble_gatt_error *error,
                            const ble_gatt_svc *service, void *arg);
    static int discChrEvent(uint16_t connHandle, const ble_gatt_error *error,
                            const ble_gatt_chr *chr, void *arg);
    static int writeEvent(uint16_t connHandle, const ble_gatt_error *error,
                          ble_gatt_attr *attr, void *arg);

    /**
     * Issue ble_gap_connect() and wait for the link to come up.
     */
    bool connectLink(const ble_addr_t &device);

    /**
     * Locate the MIDI characteristic within a discovered service. Returns false
     * when the service or the characteristic is missing, which the caller
     * treats as a failed connection.
     */
    bool discoverMidiChannel(uint16_t handle, uint16_t serviceStart,
                             uint16_t serviceEnd);

    /**
     * Drop the stored bond for @p device.
     *
     * Keys live in NVS and survive deep sleep, but the GP-5 may have forgotten
     * them in the meantime; offering a stale key makes it refuse the security
     * procedure and tear the link down as soon as it is up. Only this peer's
     * keys go away - never the whole store.
     */
    void forgetBond(const ble_addr_t &device);

    /** Clear every per-connection field. Called with `lock` held. */
    void resetState();

    /**
     * Drain a completion semaphore so a late signal from an abandoned attempt
     * cannot satisfy the next wait. Called with `lock` held.
     */
    static void drain(SemaphoreHandle_t signal);

    /** Take `lock`, logging and returning false if it could not be taken. */
    bool enter();

    /** Wait for one of the completion semaphores with `lock` released. */
    static bool awaitSignal(SemaphoreHandle_t signal, uint32_t timeoutMs);

private:
    /**
     * Guards every member below. Written on the NimBLE host task, read from
     * the application task.
     */
    SemaphoreHandle_t lock;

    /** Posted by BLE_GAP_EVENT_CONNECT / BLE_GAP_EVENT_DISCONNECT. */
    SemaphoreHandle_t linkUp;
    SemaphoreHandle_t linkDown;

    /** Posted when service discovery reports its final callback. */
    SemaphoreHandle_t serviceDone;

    /** Posted when characteristic discovery reports its final callback. */
    SemaphoreHandle_t characteristicDone;

    /** Posted when a write-with-response is acknowledged. */
    SemaphoreHandle_t writeDone;

    /** Status carried alongside the completion signals, 0 meaning success. */
    int linkUpStatus = 0;
    int serviceStatus = 0;
    int characteristicStatus = 0;
    int writeStatus = 0;

    /**
     * True once the link is up *and* the MIDI characteristic is known. Set
     * only after discovery succeeds, so the main loop never sends into a
     * half-open connection.
     */
    bool connected = false;

    /** Set as soon as the link is up, so a failed teardown can be retried. */
    bool linkOpen = false;

    uint16_t connHandle = 0;
    uint16_t midiValueHandle = 0;

    /**
     * Where the running discovery procedure puts its findings, and what the
     * caller reads back once the matching semaphore has been given.
     *
     * These are members rather than locals on purpose: a procedure whose waiter
     * gave up stays armed, and NimBLE fires its callback when the link finally
     * breaks - which can be after Connect() has already returned. Pointing that
     * callback at the object instead of the stack keeps it harmless.
     */
    uint16_t discServiceStart = 0;
    uint16_t discServiceEnd = 0;
    uint16_t discValueHandle = 0;
    uint8_t discProperties = 0;

    /**
     * The MIDI characteristic's declared properties, BLE_GATT_CHR_F_* bits,
     * exactly as the peripheral reported them. Send() derives the write type
     * from these.
     */
    uint8_t midiProperties = 0;

    /** Largest ATT payload the peer will accept; refreshed per connection. */
    uint16_t attMtu = BLE_ATT_MTU_DFLT;
};

#endif // BLECLIENT_H