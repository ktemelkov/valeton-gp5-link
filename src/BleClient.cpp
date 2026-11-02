#include "BleClient.h"

#include <cstring>

#include "esp_log.h"

#include "host/ble_att.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_id.h"
#include "host/ble_hs_mbuf.h"
#include "host/ble_l2cap.h"
#include "host/ble_store.h"

namespace
{
    /**
     * How many times a whole connect-and-discover sequence is retried before
     * giving up. The GP-5 sometimes refuses a link it has just torn down, so a
     * second or third attempt is what usually succeeds.
     */
    constexpr int kMaxConnectAttempts = 3;

    /**
     * Added to every bounded wait so the stack's own, more precisely timed
     * failure always arrives first and is the one reported. Without the slack
     * the caller would time out at the very moment the real status shows up.
     */
    constexpr uint32_t kWaitSlackMs = 500;

    /** How long the application mutex may be held by somebody else. */
    constexpr uint32_t kLockTimeoutMs = 1000;
} // namespace

/**
 * Every member is a FreeRTOS object here, so the object is safe to use but does
 * nothing until BleScanner has brought the NimBLE host up.
 *
 * The completion signals are counting semaphores capped at one token: a
 * callback that arrives after its waiter already gave up must not be able to
 * hand a free token to the next operation.
 */
BleClient::BleClient()
{
    lock = xSemaphoreCreateMutex();
    linkUp = xSemaphoreCreateCounting(1, 0);
    linkDown = xSemaphoreCreateCounting(1, 0);
    serviceDone = xSemaphoreCreateCounting(1, 0);
    characteristicDone = xSemaphoreCreateCounting(1, 0);
    writeDone = xSemaphoreCreateCounting(1, 0);

    if (lock == nullptr || linkUp == nullptr || linkDown == nullptr ||
        serviceDone == nullptr || characteristicDone == nullptr || writeDone == nullptr)
    {
        ESP_LOGE(TAG, "%s", "Failed to create the client's semaphores.");
    }
}

/**
 * Releases the synchronisation objects only. The link, if any, is left alone:
 * the NimBLE host task outlives this object and keeps its own state, and
 * tearing a connection down from a destructor is not something a caller can
 * act on anyway.
 */
BleClient::~BleClient()
{
    if (writeDone != nullptr)
    {
        vSemaphoreDelete(writeDone);
        writeDone = nullptr;
    }
    if (characteristicDone != nullptr)
    {
        vSemaphoreDelete(characteristicDone);
        characteristicDone = nullptr;
    }
    if (serviceDone != nullptr)
    {
        vSemaphoreDelete(serviceDone);
        serviceDone = nullptr;
    }
    if (linkDown != nullptr)
    {
        vSemaphoreDelete(linkDown);
        linkDown = nullptr;
    }
    if (linkUp != nullptr)
    {
        vSemaphoreDelete(linkUp);
        linkUp = nullptr;
    }
    if (lock != nullptr)
    {
        vSemaphoreDelete(lock);
        lock = nullptr;
    }
}

/**
 * Report whether the transport is ready, meaning the link is up and the MIDI
 * characteristic has been resolved.
 *
 * @return true if Send() can be called.
 */
bool BleClient::IsConnected()
{
    if (!enter())
    {
        return false;
    }

    const bool ready = connected;
    xSemaphoreGive(lock);

    return ready;
}

/**
 * Connect to a device and resolve the MIDI characteristic, retrying up to
 * kMaxConnectAttempts times.
 *
 * Returns only once the link carries a usable MIDI write handle or the last
 * attempt has been abandoned, so the caller can act on the result directly.
 *
 * @param device The peer address reported by the scanner.
 *
 * @return true if the client is ready to send.
 */
bool BleClient::Connect(const ble_addr_t &device)
{
    for (int attempt = 1; attempt <= kMaxConnectAttempts; attempt++)
    {
        ESP_LOGI(TAG, "Connect attempt %d of %d.", attempt, kMaxConnectAttempts);

        if (!enter())
        {
            return false;
        }

        // Anything still in flight from the previous attempt has to be
        // discarded first, otherwise its completion would be mistaken for the
        // result of a step in this one.
        drain(linkUp);
        drain(linkDown);
        drain(serviceDone);
        drain(characteristicDone);
        drain(writeDone);
        linkUpStatus = 0;
        serviceStatus = 0;
        characteristicStatus = 0;
        writeStatus = 0;
        discServiceStart = 0;
        discServiceEnd = 0;
        discValueHandle = 0;
        discProperties = 0;
        xSemaphoreGive(lock);

        forgetBond(device);

        if (!connectLink(device))
        {
            Disconnect();
            continue;
        }

        if (!enter())
        {
            return false;
        }
        const uint16_t handle = connHandle;
        xSemaphoreGive(lock);

        // Nothing exchanges the MTU on its own - neither the GAP nor the GATT
        // layer does it as part of establishing a link - so the request has to
        // come from here. The negotiated value arrives as the
        // BLE_GAP_EVENT_MTU this triggers and is read back authoritatively at
        // the end of the sequence.
        const int mtuRc = ble_gattc_exchange_mtu(handle, nullptr, nullptr);
        if (mtuRc != 0)
        {
            ESP_LOGW(TAG, "ble_gattc_exchange_mtu failed: %d", mtuRc);
        }

        const int svcRc = ble_gattc_disc_svc_by_uuid(handle, &TARGET_SERVICE_UUID.u,
                                                    &BleClient::discSvcEvent, this);
        if (svcRc != 0)
        {
            ESP_LOGE(TAG, "ble_gattc_disc_svc_by_uuid failed: %d", svcRc);
            Disconnect();
            continue;
        }

        if (!awaitSignal(serviceDone, kDiscoveryTimeoutMs + kWaitSlackMs))
        {
            ESP_LOGE(TAG, "%s", "Service discovery timed out.");
            Disconnect();
            continue;
        }

        if (!enter())
        {
            return false;
        }
        const int serviceResult = serviceStatus;
        const uint16_t serviceStart = discServiceStart;
        const uint16_t serviceEnd = discServiceEnd;
        xSemaphoreGive(lock);

        if (serviceResult != 0)
        {
            ESP_LOGE(TAG, "MIDI service not found, status: %d", serviceResult);
            Disconnect();
            continue;
        }

        if (serviceEnd == 0 || !discoverMidiChannel(handle, serviceStart, serviceEnd))
        {
            Disconnect();
            continue;
        }

        if (!enter())
        {
            return false;
        }
        connected = true;
        // The exchange-mtu response is handled before any later ATT round trip
        // on this link, so by now the value the host reports is the one the
        // peer agreed to. ble_att_mtu() takes the host lock internally and
        // returns 0 once the link is gone, which is why it is read here rather
        // than in Send().
        const uint16_t negotiated = ble_att_mtu(handle);
        if (negotiated != 0)
        {
            attMtu = negotiated;
        }
        const uint16_t readyMtu = attMtu;
        const uint16_t readyValue = midiValueHandle;
        const uint8_t readyProps = midiProperties;
        xSemaphoreGive(lock);

        ESP_LOGI(TAG, "Ready: handle=%u value_handle=%u props=0x%02x mtu=%u",
                 static_cast<unsigned>(handle), static_cast<unsigned>(readyValue),
                 static_cast<unsigned>(readyProps), static_cast<unsigned>(readyMtu));

        return true;
    }

    ESP_LOGE(TAG, "%s", "Giving up on this device.");
    return false;
}

/**
 * Tear the link down and wait for the teardown to be confirmed, so the caller
 * can start scanning again straight away - a connect issued while the old link
 * still exists is refused with BLE_HS_EBUSY.
 *
 * Safe to call when there is no connection.
 */
void BleClient::Disconnect()
{
    if (!enter())
    {
        return;
    }

    if (!linkOpen)
    {
        xSemaphoreGive(lock);
        return;
    }

    const uint16_t handle = connHandle;
    drain(linkDown);
    xSemaphoreGive(lock);

    const int rc = ble_gap_terminate(handle, BLE_ERR_CONN_TERM_LOCAL);
    if (rc == 0)
    {
        if (!awaitSignal(linkDown, kDisconnectTimeoutMs + kWaitSlackMs))
        {
            ESP_LOGW(TAG, "No disconnect confirmation for handle %u.",
                     static_cast<unsigned>(handle));
        }
    }
    else if (rc != BLE_HS_ENOTCONN)
    {
        ESP_LOGE(TAG, "ble_gap_terminate failed: %d", rc);
    }

    if (!enter())
    {
        return;
    }
    resetState();
    xSemaphoreGive(lock);
}

/**
 * Write a MIDI message to the characteristic.
 *
 * NimBLE does not check the characteristic's declared properties for the caller:
 * a Write Request to an attribute that only supports write-without-response is
 * answered with BLE_ATT_ERR_WRITE_NOT_PERMITTED, which reaches this class as
 * BLE_HS_ATT_ERR(0x03), 259. So the property flags read during discovery decide
 * the form, and the negotiated MTU decides whether the message fits into the
 * single ATT payload that a Write Command or Write Request carries.
 *
 * @param data   The message bytes, owned by the caller for the whole call.
 * @param length How many bytes to write.
 *
 * @return true if the message was handed to the stack, or, for a write that
 *         expects an acknowledgement, confirmed by the peripheral.
 */
bool BleClient::Send(const uint8_t *data, size_t length)
{
    if (data == nullptr || length == 0)
    {
        ESP_LOGE(TAG, "%s", "Refusing to send an empty message.");
        return false;
    }

    if (length > UINT16_MAX)
    {
        ESP_LOGE(TAG, "Message too long to write: %u bytes.", static_cast<unsigned>(length));
        return false;
    }

    if (!enter())
    {
        return false;
    }

    if (!connected || midiValueHandle == 0)
    {
        xSemaphoreGive(lock);
        ESP_LOGE(TAG, "%s", "Not connected, message dropped.");
        return false;
    }

    const uint16_t handle = connHandle;
    const uint16_t valueHandle = midiValueHandle;
    const uint8_t properties = midiProperties;
    const uint16_t payload = (attMtu > kAttWriteOverhead)
                                 ? static_cast<uint16_t>(attMtu - kAttWriteOverhead)
                                 : 0;

    // Write Command first: MIDI is a stream, so waiting on a per-message
    // acknowledgement only costs a round trip, and this peripheral declares the
    // characteristic write-without-response only. A characteristic that supports
    // nothing but Write Request still needs one, and anything larger than a
    // single PDU has to go through prepare/execute.
    const bool noRsp = (properties & BLE_GATT_CHR_F_WRITE_NO_RSP) != 0;
    const bool withRsp = (properties & BLE_GATT_CHR_F_WRITE) != 0;
    const bool fits = length <= payload;

    if ((properties & (BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_WRITE)) == 0)
    {
        xSemaphoreGive(lock);
        ESP_LOGE(TAG, "Characteristic is not writable, properties 0x%02x.", properties);
        return false;
    }

    if (!fits && !withRsp)
    {
        // Splitting a message across Write Commands is not an option: each one
        // arrives as a complete attribute value, so the peripheral would see
        // several truncated MIDI messages. Prepare/execute, which does reassemble,
        // is only available on a characteristic that supports writes with
        // response. Raising the negotiated MTU is the only correct fix.
        xSemaphoreGive(lock);
        ESP_LOGE(TAG, "Message of %u bytes exceeds the %u byte ATT payload (MTU %u).",
                 static_cast<unsigned>(length), static_cast<unsigned>(payload),
                 static_cast<unsigned>(attMtu));
        return false;
    }

    drain(writeDone);
    writeStatus = 0;

    int rc;
    bool awaiting = false;

    if (noRsp && fits)
    {
        rc = ble_gattc_write_no_rsp_flat(handle, valueHandle, data,
                                         static_cast<uint16_t>(length));
    }
    else if (withRsp && fits)
    {
        awaiting = true;
        rc = ble_gattc_write_flat(handle, valueHandle, data,
                                  static_cast<uint16_t>(length),
                                  &BleClient::writeEvent, this);
    }
    else
    {
        // ble_gattc_write_long() owns the mbuf whichever way it goes, so there
        // is no free on the failure path.
        struct os_mbuf *om = ble_hs_mbuf_from_flat(data, static_cast<uint16_t>(length));
        if (om == nullptr)
        {
            xSemaphoreGive(lock);
            ESP_LOGE(TAG, "Out of mbuf space for a %u byte message.",
                     static_cast<unsigned>(length));
            return false;
        }

        awaiting = true;
        rc = ble_gattc_write_long(handle, valueHandle, 0, om,
                                  &BleClient::writeEvent, this);
    }

    xSemaphoreGive(lock);

    if (rc != 0)
    {
        ESP_LOGE(TAG, "Write could not be issued: %d", rc);
        return false;
    }

    if (!awaiting)
    {
        return true;
    }

    if (!awaitSignal(writeDone, kWriteTimeoutMs + kWaitSlackMs))
    {
        ESP_LOGE(TAG, "%s", "Write was not acknowledged in time.");
        return false;
    }

    if (!enter())
    {
        return false;
    }
    const int status = writeStatus;
    xSemaphoreGive(lock);

    if (status != 0)
    {
        ESP_LOGE(TAG, "Write rejected, status: %d", status);
        return false;
    }

    return true;
}

/**
 * Issue a connect and wait for the link to come up or for the attempt to fail.
 *
 * The connect procedure is given the same deadline as the wait, so the stack
 * reports its own timeout rather than the caller simply losing patience; the
 * extra slack only matters if the host never answers at all.
 *
 * @param device The peer address to connect to.
 *
 * @return true if the link is established.
 */
bool BleClient::connectLink(const ble_addr_t &device)
{
    ble_gap_conn_params params;
    memset(&params, 0, sizeof(params));

    params.scan_itvl = 0x0010;
    params.scan_window = 0x0010;
    params.itvl_min = kConnInterval;
    params.itvl_max = kConnInterval;
    params.latency = kConnLatency;
    params.supervision_timeout = kConnSupervisionTimeout;
    params.min_ce_len = 0;
    params.max_ce_len = 0;

    uint8_t ownAddrType = BLE_OWN_ADDR_PUBLIC;
    const int addrRc = ble_hs_id_infer_auto(0, &ownAddrType);
    if (addrRc != 0)
    {
        ESP_LOGE(TAG, "ble_hs_id_infer_auto failed: %d", addrRc);
        return false;
    }

    const int rc = ble_gap_connect(ownAddrType, &device,
                                   static_cast<int32_t>(kConnectTimeoutMs),
                                   &params, &BleClient::gapEvent, this);

    if (rc == BLE_HS_EBUSY)
    {
        // The controller refuses a connect while a scan is still running, and
        // it is the scan that has to go away, not this attempt.
        ESP_LOGE(TAG, "%s", "ble_gap_connect refused: discovery is still active.");
        return false;
    }

    if (rc == BLE_HS_EALREADY)
    {
        // A procedure left over from an earlier attempt. Cancelling it produces
        // the completion event, and the next attempt starts clean.
        ESP_LOGW(TAG, "%s", "A connect procedure was still active, cancelling it.");
        ble_gap_conn_cancel();
        awaitSignal(linkUp, kDisconnectTimeoutMs + kWaitSlackMs);
        return false;
    }

    if (rc != 0)
    {
        ESP_LOGE(TAG, "ble_gap_connect failed: %d", rc);
        return false;
    }

    if (!awaitSignal(linkUp, kConnectTimeoutMs + kWaitSlackMs))
    {
        ESP_LOGW(TAG, "%s", "No connect event, cancelling the procedure.");
        ble_gap_conn_cancel();

        // The cancellation is itself reported through the same event, and
        // waiting for it keeps the semaphore clean for the next attempt.
        awaitSignal(linkUp, kDisconnectTimeoutMs + kWaitSlackMs);
        return false;
    }

    if (!enter())
    {
        return false;
    }
    const int status = linkUpStatus;
    const uint16_t handle = connHandle;
    xSemaphoreGive(lock);

    if (status != 0)
    {
        ESP_LOGE(TAG, "Link did not come up, status: %d", status);
        return false;
    }

    ESP_LOGI(TAG, "Link established, handle %u.", static_cast<unsigned>(handle));
    return true;
}

/**
 * Find the MIDI characteristic inside an already located service and remember
 * its value handle.
 *
 * @param handle       The connection to query.
 * @param serviceStart First handle of the service.
 * @param serviceEnd   Last handle of the service.
 *
 * @return true if the characteristic was found.
 */
bool BleClient::discoverMidiChannel(uint16_t handle, uint16_t serviceStart,
                                    uint16_t serviceEnd)
{
    const int rc = ble_gattc_disc_chrs_by_uuid(handle, serviceStart, serviceEnd,
                                               &MIDI_CHARACTERISTIC_UUID.u,
                                               &BleClient::discChrEvent, this);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "ble_gattc_disc_chrs_by_uuid failed: %d", rc);
        return false;
    }

    if (!awaitSignal(characteristicDone, kDiscoveryTimeoutMs + kWaitSlackMs))
    {
        ESP_LOGE(TAG, "%s", "Characteristic discovery timed out.");
        return false;
    }

    if (!enter())
    {
        return false;
    }

    const bool found = (characteristicStatus == 0) && (discValueHandle != 0);
    if (found)
    {
        midiValueHandle = discValueHandle;
        midiProperties = discProperties;
    }
    const int status = characteristicStatus;
    const uint8_t properties = discProperties;
    xSemaphoreGive(lock);

    if (!found)
    {
        ESP_LOGE(TAG, "MIDI characteristic not found, status: %d", status);
    }
    else
    {
        ESP_LOGI(TAG, "MIDI characteristic at handle %u, properties 0x%02x.",
                 static_cast<unsigned>(discValueHandle), static_cast<unsigned>(properties));
    }

    return found;
}

/**
 * Remove everything stored about one peer: our keys, its keys, its CCCDs and
 * its resolvable address records. Only this peer is touched, so a bond with
 * another device survives.
 *
 * @param device The peer whose stored state should be dropped.
 */
void BleClient::forgetBond(const ble_addr_t &device)
{
    const int rc = ble_store_util_delete_peer(&device);

    // BLE_HS_ENOTSUP simply means no store is configured, which is the normal
    // case here and nothing to warn about.
    if (rc != 0 && rc != BLE_HS_ENOTSUP)
    {
        ESP_LOGD(TAG, "ble_store_util_delete_peer: %d", rc);
    }
}

/**
 * Forget the whole connection. Called with `lock` held.
 *
 * The completion semaphores are deliberately not drained here - that happens at
 * the start of the next attempt, where it is followed by fresh work rather than
 * by a caller that may still be looking at the old state.
 */
void BleClient::resetState()
{
    connected = false;
    linkOpen = false;
    connHandle = 0;
    midiValueHandle = 0;
    midiProperties = 0;
    discServiceStart = 0;
    discServiceEnd = 0;
    discValueHandle = 0;
    discProperties = 0;
    attMtu = BLE_ATT_MTU_DFLT;
    linkUpStatus = 0;
    serviceStatus = 0;
    characteristicStatus = 0;
    writeStatus = 0;
}

/**
 * Take every token a semaphore is holding. Called with `lock` held.
 *
 * @param signal The semaphore to empty.
 */
void BleClient::drain(SemaphoreHandle_t signal)
{
    if (signal == nullptr)
    {
        return;
    }

    while (xSemaphoreTake(signal, 0) == pdTRUE)
    {
    }
}

/**
 * Take the object lock.
 *
 * @return true if the lock is held by the caller, which must give it back.
 */
bool BleClient::enter()
{
    if (lock == nullptr || xSemaphoreTake(lock, pdMS_TO_TICKS(kLockTimeoutMs)) != pdTRUE)
    {
        ESP_LOGE(TAG, "%s", "Failed to take the client lock.");
        return false;
    }

    return true;
}

/**
 * Wait for a completion signal. The lock must not be held: the callback that
 * gives the signal needs it.
 *
 * @param signal    The semaphore to wait on.
 * @param timeoutMs How long to wait.
 *
 * @return true if the signal arrived.
 */
bool BleClient::awaitSignal(SemaphoreHandle_t signal, uint32_t timeoutMs)
{
    if (signal == nullptr)
    {
        return false;
    }

    return xSemaphoreTake(signal, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

/**
 * GAP events for the connection this client owns. Runs on the NimBLE host task.
 *
 * @return 0 to let the host keep the connection, or a BLE_GAP_REPEAT_PAIRING_*
 *         verdict for a repeat-pairing event.
 */
int BleClient::gapEvent(ble_gap_event *event, void *arg)
{
    BleClient *self = static_cast<BleClient *>(arg);
    if (self == nullptr)
    {
        return 0;
    }

    switch (event->type)
    {
    case BLE_GAP_EVENT_CONNECT:
        // BLE_GAP_EVENT_LINK_ESTAB is the very same transition reported a
        // second time by this host fork, so only CONNECT is handled and the
        // semaphore is given exactly once per attempt.
        if (self->enter())
        {
            self->linkUpStatus = event->connect.status;
            if (event->connect.status == 0)
            {
                self->connHandle = event->connect.conn_handle;
                self->linkOpen = true;
            }
            xSemaphoreGive(self->lock);
            xSemaphoreGive(self->linkUp);
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        if (self->enter())
        {
            self->connected = false;
            self->linkOpen = false;
            xSemaphoreGive(self->lock);
            xSemaphoreGive(self->linkDown);
        }
        ESP_LOGI(TAG, "Disconnected, reason: %d", event->disconnect.reason);
        break;

    case BLE_GAP_EVENT_MTU:
        if (event->mtu.channel_id == BLE_L2CAP_CID_ATT && self->enter())
        {
            self->attMtu = event->mtu.value;
            xSemaphoreGive(self->lock);
        }
        break;

    case BLE_GAP_EVENT_REPEAT_PAIRING:
    {
        // The peer wants to pair again while we still hold a bond. Keeping the
        // old one makes the security procedure fail, so it is dropped and the
        // pairing is allowed to start over.
        struct ble_gap_conn_desc desc;
        if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0)
        {
            ble_store_util_delete_peer(&desc.peer_id_addr);
        }
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }

    default:
        break;
    }

    return 0;
}

/**
 * Reports the services found by a Discover-Primary-Service-by-UUID procedure.
 * Runs on the NimBLE host task; the final callback carries BLE_HS_EDONE.
 *
 * @return 0 to let the procedure continue.
 */
int BleClient::discSvcEvent(uint16_t connHandle, const ble_gatt_error *error,
                            const ble_gatt_svc *service, void *arg)
{
    (void)connHandle;

    BleClient *self = static_cast<BleClient *>(arg);
    if (self == nullptr)
    {
        return 0;
    }

    if (error->status == 0)
    {
        if (service != nullptr && self->enter())
        {
            self->discServiceStart = service->start_handle;
            self->discServiceEnd = service->end_handle;
            xSemaphoreGive(self->lock);
        }
        return 0;
    }

    if (self->enter())
    {
        self->serviceStatus = (error->status == BLE_HS_EDONE) ? 0 : error->status;
        xSemaphoreGive(self->lock);
        xSemaphoreGive(self->serviceDone);
    }

    return 0;
}

/**
 * Reports the characteristics found inside the MIDI service. Runs on the
 * NimBLE host task; the final callback carries BLE_HS_EDONE.
 *
 * @return 0 to let the procedure continue.
 */
int BleClient::discChrEvent(uint16_t connHandle, const ble_gatt_error *error,
                            const ble_gatt_chr *chr, void *arg)
{
    (void)connHandle;

    BleClient *self = static_cast<BleClient *>(arg);
    if (self == nullptr)
    {
        return 0;
    }

    if (error->status == 0)
    {
        if (chr != nullptr && self->enter())
        {
            self->discValueHandle = chr->val_handle;
            self->discProperties = chr->properties;
            xSemaphoreGive(self->lock);
        }
        return 0;
    }

    if (self->enter())
    {
        self->characteristicStatus = (error->status == BLE_HS_EDONE) ? 0 : error->status;
        xSemaphoreGive(self->lock);
        xSemaphoreGive(self->characteristicDone);
    }

    return 0;
}

/**
 * Reports the outcome of a write. Both the single and the long write call this
 * exactly once, with the final status. Runs on the NimBLE host task.
 *
 * @return 0.
 */
int BleClient::writeEvent(uint16_t connHandle, const ble_gatt_error *error,
                          ble_gatt_attr *attr, void *arg)
{
    (void)connHandle;
    (void)attr;

    BleClient *self = static_cast<BleClient *>(arg);
    if (self == nullptr)
    {
        return 0;
    }

    if (self->enter())
    {
        self->writeStatus = error->status;
        xSemaphoreGive(self->lock);
        xSemaphoreGive(self->writeDone);
    }

    return 0;
}