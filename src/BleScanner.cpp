#include "BleScanner.h"

#include <string>
#include "esp_log.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_hs_id.h"

SemaphoreHandle_t BleScanner::syncNimbleInit = nullptr;

/**
 * The object is usable as soon as it exists. The Bluetooth stack is only
 * brought up by the first StartScanning(), so a device that never scans never
 * pays for the controller or the host task.
 */
BleScanner::BleScanner()
{
    lockFoundAddress = xSemaphoreCreateMutex();
}

/**
 * Cancels a discovery procedure that is still running. The NimBLE host task is
 * deliberately left running: tearing it down also stops the controller and
 * cannot be done safely from an arbitrary task.
 */
BleScanner::~BleScanner()
{
    if (lockFoundAddress != nullptr)
    {
        vSemaphoreDelete(lockFoundAddress);
        lockFoundAddress = nullptr;
    }
}

/**
 * Initialise the BLE scanner. Returns true on success.
 */
bool BleScanner::Init()
{
    if (syncNimbleInit != nullptr)
    {
        ESP_LOGE(TAG, "%s", "BLE scanner already initialized.");
        return false; // already initialized
    }

    // Initialises and enables the controller, then the NimBLE host.
    const esp_err_t err = nimble_port_init();

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "nimble_port_init failed: %d", err);
        return false;
    }

    syncNimbleInit = xSemaphoreCreateBinary();
    if (syncNimbleInit == nullptr)
    {
        ESP_LOGE(TAG, "%s", "Failed to create the sync semaphore.");
        return false;
    }

    // Scanning needs no security, so only the two lifecycle notifications are
    // wired up. Leaving the store callbacks NULL is safe: with no pairing they
    // are never dereferenced.
    ble_hs_cfg.sync_cb = &BleScanner::onHostSync;
    ble_hs_cfg.reset_cb = &BleScanner::onHostReset;

    // Spawns the "nimble_host" task that drains the default event queue. The
    // GAP callback handed to ble_gap_disc() runs on that task, never here.
    nimble_port_freertos_init(&BleScanner::hostTask);

    // The calls above only queue the host start; wait for the HCI reset and
    // read-version handshake to complete before the first real command.
    const BaseType_t synced = xSemaphoreTake(syncNimbleInit, pdMS_TO_TICKS(2000));

    vSemaphoreDelete(syncNimbleInit);
    syncNimbleInit = nullptr;

    if (synced != pdTRUE)
    {
        ESP_LOGE(TAG, "NimBLE host did not sync (ble_hs_synced()=%d).", ble_hs_synced());
        return false;
    }

    return true;
}

/**
 * Shutdown the BLE scanner and release resources.
 */
void BleScanner::Shutdown()
{
    StopScanning();
}

/**
 * Start a BLE scan with the configured parameters.
 *
 * @return true if the scan was successfully started, false otherwise.
 */
bool BleScanner::StartScanning()
{
    if (pdTRUE != xSemaphoreTake(lockFoundAddress, pdMS_TO_TICKS(20)))
    {
        ESP_LOGE(TAG, "Failed to take lockFoundAddress semaphore.");
        return false;
    }
    memset(&foundAddress, 0, sizeof(foundAddress));
    xSemaphoreGive(lockFoundAddress);

    ble_gap_disc_params params;
    memset(&params, 0, sizeof(params));

    params.itvl = 97;
    params.window = 67;
    params.passive = 0;           // Active scanning: the scan request makes the peer answer with a SCAN_RSP, and the GP-5 only carries its complete local name in that response.
    params.filter_duplicates = 0; // Duplicate filtering suppresses the SCAN_RSP of an address already reported, so it must stay off or the name never arrives.
    params.filter_policy = 0;     // 0 = accept every advertiser; the whitelist is not used.
    params.limited = 0;

    uint8_t ownAddrType = BLE_OWN_ADDR_PUBLIC;
    const int addrRc = ble_hs_id_infer_auto(0, &ownAddrType);

    if (addrRc != 0)
    {
        ESP_LOGE(TAG, "ble_hs_id_infer_auto failed: %d", addrRc);
        return false;
    }

    const int rc = ble_gap_disc(ownAddrType, BLE_HS_FOREVER, &params, &BleScanner::gapEvent, this); // BLE_HS_FOREVER keeps the procedure running until StopScanning();
                                                                                                    // passing 0 here would instead select the stack's short default duration.
    if (rc != 0 && rc != BLE_HS_EALREADY)
    {
        ESP_LOGE(TAG, "ble_gap_disc failed: %d", rc);
        return false;
    }

    return true;
}

/**
 * Stop the ongoing BLE scan if any.
 *
 * @return true if scanning was successfully stopped or was not active, false otherwise.
 */
bool BleScanner::StopScanning()
{
    const int ret = ble_gap_disc_cancel();

    if (ret != 0 && ret != BLE_HS_EALREADY)
    {
        ESP_LOGE(TAG, "ble_gap_disc_cancel failed: %d", ret);
        return false;
    }

    return true;
}

/**
 * Check if a BLE scan is currently active.
 *
 * @return true if a scan is in progress, false otherwise.
 */
bool BleScanner::IsScanning()
{
    // Asked of the stack instead of mirrored in a member: the procedure also
    // ends by itself (controller reset, preempted by a connect) and a cached
    // flag would drift out of sync with that.
    return ble_gap_disc_active() != 0;
}

/**
 * Get the address of the found target device, if any.
 *
 * @param addr Reference to a ble_addr_t structure to receive the address.
 *
 * @return true if a device has been found, false otherwise.
 */
bool BleScanner::GetFoundDevice(ble_addr_t &addr)
{
    if (pdTRUE != xSemaphoreTake(lockFoundAddress, pdMS_TO_TICKS(20)))
    {
        ESP_LOGE(TAG, "Failed to take lockFoundAddress semaphore.");
        return false;
    }
    memcpy(&addr, &foundAddress, sizeof(addr));
    xSemaphoreGive(lockFoundAddress);

    return (foundAddress.val[0] != 0 || foundAddress.val[1] != 0 || foundAddress.val[2] != 0 ||
            foundAddress.val[3] != 0 || foundAddress.val[4] != 0 || foundAddress.val[5] != 0);
}

/**
 * Handles GAP events from the NimBLE host.
 * Returns 0 on success.
 */
int BleScanner::gapEvent(ble_gap_event *event, void *arg)
{
    BleScanner *self = static_cast<BleScanner *>(arg);

    if (event == nullptr || self == nullptr)
    {
        return 0;
    }

    switch (event->type)
    {
    case BLE_GAP_EVENT_DISC:
    {
        // ESP_LOGI(TAG, "Discovered device: %02X:%02X:%02X:%02X:%02X:%02X",
        //         event->disc.addr.val[5], event->disc.addr.val[4], event->disc.addr.val[3],
        //         event->disc.addr.val[2], event->disc.addr.val[1], event->disc.addr.val[0]);

        ble_hs_adv_fields fields;
        int ret = ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data);

        if (ret != 0)
        {
            // ESP_LOGE(TAG, "Failed to parse advertisement fields, ret=%d", ret);
            return ret;
        }

        if (fields.name_is_complete && fields.name_len == strlen(BleScanner::TARGET_DEVICE_NAME) && strncmp((const char *)fields.name, BleScanner::TARGET_DEVICE_NAME, fields.name_len) == 0)
        {
            ESP_LOGI(TAG, "Found target device with matching name: %s", std::string((const char *)fields.name, fields.name_len).c_str());

            if (pdTRUE != xSemaphoreTake(self->lockFoundAddress, pdMS_TO_TICKS(20)))
            {
                ESP_LOGE(TAG, "Failed to take lockFoundAddress semaphore.");
                return 0;
            }
            memcpy(&self->foundAddress, &event->disc.addr, sizeof(self->foundAddress));
            xSemaphoreGive(self->lockFoundAddress);
        }
    }
    break;

    case BLE_GAP_EVENT_DISC_COMPLETE:
        ESP_LOGI(TAG, "discovery complete, reason=%d", event->disc_complete.reason);
        break;

    default:
        break;
    }

    return 0;
}

/**
 *
 */
void BleScanner::onHostSync(void)
{
    if (BleScanner::syncNimbleInit != nullptr)
    {
        xSemaphoreGive(BleScanner::syncNimbleInit);
    }
}

/**
 *
 */
void BleScanner::onHostReset(int reason)
{
    ESP_LOGE(TAG, "NimBLE host reset, reason=%d", reason);
}

/**
 *
 */
void BleScanner::hostTask(void *arg)
{
    (void)arg;
    nimble_port_run();
}
