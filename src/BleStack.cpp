#include "BleStack.h"

#include "debug.h"

#include <string.h>

#include <esp_bt_main.h>
#include <esp_gap_ble_api.h>

// The Arduino core releases the Bluetooth controller memory during
// initArduino() unless btInUse() reports that the application needs it. The
// core ships a weak btInUse() that already returns true, but it is only
// linked in when esp32-hal-bt.c is pulled in, so provide a strong definition
// here to guarantee the memory survives.
extern "C" bool btInUse();

extern "C" bool btInUse()
{
    return true;
}

BleStack &BleStack::instance()
{
    // Meyers singleton: constructed on first use, thread-safe in C++11 and
    // later, and it does not depend on static-initialisation order.
    static BleStack singleton;
    return singleton;
}

BleStack::BleStack()
    : running_(false)
    , gattcObserver_(nullptr)
{
    for (size_t i = 0; i < kMaxGapObservers; ++i)
    {
        gapObservers_[i] = nullptr;
    }
}

bool BleStack::isRunning() const
{
    BleLock lock(mutex_);
    return running_;
}

bool BleStack::ensureStarted()
{
    BleLock lock(mutex_);

    if (running_)
    {
        return true;
    }

    if (!startController())
    {
        return false;
    }

    if (!startHost())
    {
        return false;
    }

    running_ = true;
    return true;
}

bool BleStack::startController()
{
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED)
    {
        return true;
    }

    // Classic Bluetooth is never used and the C3 has no separate memory pool
    // for it, so nothing to release here - unlike the original ESP32, where
    // esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT) saved RAM.

    esp_bt_controller_config_t btConfig = BT_CONTROLLER_INIT_CONFIG_DEFAULT();

    esp_err_t err = esp_bt_controller_init(&btConfig);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        DEBUG_MSG("esp_bt_controller_init failed: %d\n", err);
        return false;
    }

    // Enabling is asynchronous: the controller reports ready through a status
    // callback, so poll for it instead of assuming the call returned done.
    for (int i = 0; i < 500; ++i)
    {
        if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED)
        {
            break;
        }

        if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_IDLE)
        {
            err = esp_bt_controller_enable(ESP_BT_MODE_BLE);
            if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
            {
                DEBUG_MSG("esp_bt_controller_enable failed: %d\n", err);
                return false;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(2));
    }

    if (esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_ENABLED)
    {
        DEBUG_MSG("%s", "Bluetooth controller did not come up.\n");
        return false;
    }

    return true;
}

bool BleStack::startHost()
{
    esp_bluedroid_status_t status = esp_bluedroid_get_status();

    if (status == ESP_BLUEDROID_STATUS_UNINITIALIZED)
    {
        esp_err_t err = esp_bluedroid_init();
        if (err != ESP_OK)
        {
            DEBUG_MSG("esp_bluedroid_init failed: %d\n", err);
            return false;
        }
        status = esp_bluedroid_get_status();
    }

    if (status == ESP_BLUEDROID_STATUS_INITIALIZED)
    {
        esp_err_t err = esp_bluedroid_enable();
        if (err != ESP_OK)
        {
            DEBUG_MSG("esp_bluedroid_enable failed: %d\n", err);
            return false;
        }
        status = esp_bluedroid_get_status();
    }

    if (status != ESP_BLUEDROID_STATUS_ENABLED)
    {
        DEBUG_MSG("%s", "Bluedroid host stack failed to enable.\n");
        return false;
    }

    // Install the dispatchers before anyone can issue a command, otherwise the
    // completion events of the first command have nowhere to go.
    esp_err_t err = esp_ble_gap_register_callback(&BleStack::gapCallback);
    if (err != ESP_OK)
    {
        DEBUG_MSG("esp_ble_gap_register_callback failed: %d\n", err);
        return false;
    }

    err = esp_ble_gattc_register_callback(&BleStack::gattcCallback);
    if (err != ESP_OK)
    {
        DEBUG_MSG("esp_ble_gattc_register_callback failed: %d\n", err);
        return false;
    }

    return true;
}

bool BleStack::configureSecurity()
{
    if (!ensureStarted())
    {
        return false;
    }

    // Bonding + Secure Connections, which is what the GP-5 expects. No MITM:
    // this device has neither a display nor a keyboard.
    esp_ble_auth_req_t authReq = ESP_LE_AUTH_BOND;
    esp_ble_io_cap_t iocap = ESP_IO_CAP_NONE;
    uint8_t keySize = 16;
    uint8_t initKey = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    uint8_t rspKey = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    uint8_t passkey[6] = {'0', '0', '0', '0', '0', '0'};

    bool ok = true;
    ok = esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &authReq,
                                        sizeof(authReq)) == ESP_OK && ok;
    ok = esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &iocap,
                                        sizeof(iocap)) == ESP_OK && ok;
    ok = esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &keySize,
                                        sizeof(keySize)) == ESP_OK && ok;
    ok = esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &initKey,
                                        sizeof(initKey)) == ESP_OK && ok;
    ok = esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &rspKey,
                                        sizeof(rspKey)) == ESP_OK && ok;
    ok = esp_ble_gap_set_security_param(ESP_BLE_SM_SET_STATIC_PASSKEY, passkey,
                                        sizeof(passkey)) == ESP_OK && ok;

    if (!ok)
    {
        DEBUG_MSG("%s", "Failed to set Bluedroid security parameters.\n");
    }

    return ok;
}

void BleStack::applyTxPower()
{
    if (!ensureStarted())
    {
        return;
    }

    // The controller default is already +9 dBm through
    // CONFIG_BT_CTRL_DFT_TX_POWER_LEVEL_P9, but setting it explicitly keeps
    // the behaviour identical to the NimBLE scanner regardless of config.
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_SCAN, ESP_PWR_LVL_P9);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P9);
}

bool BleStack::addGapObserver(GapObserver *observer)
{
    if (!observer)
    {
        return false;
    }

    BleLock lock(mutex_);

    for (size_t i = 0; i < kMaxGapObservers; ++i)
    {
        if (gapObservers_[i] == observer)
        {
            return true;
        }
    }

    for (size_t i = 0; i < kMaxGapObservers; ++i)
    {
        if (!gapObservers_[i])
        {
            gapObservers_[i] = observer;
            return true;
        }
    }

    DEBUG_MSG("%s", "No free GAP observer slot.\n");
    return false;
}

void BleStack::removeGapObserver(GapObserver *observer)
{
    BleLock lock(mutex_);

    for (size_t i = 0; i < kMaxGapObservers; ++i)
    {
        if (gapObservers_[i] == observer)
        {
            gapObservers_[i] = nullptr;
        }
    }
}

void BleStack::registerGattcObserver(GattcObserver *observer)
{
    BleLock lock(mutex_);
    gattcObserver_ = observer;
}

void BleStack::gapCallback(esp_gap_ble_cb_event_t event,
                           esp_ble_gap_cb_param_t *param)
{
    // Runs on the Bluetooth host task. The observer list is snapshotted under
    // the mutex and then dispatched to with the mutex released: observers call
    // back into BleStack (for example to answer a security request), and the
    // mutex is not recursive, so holding it across a callback would deadlock.
    BleStack &self = instance();
    GapObserver *snapshot[kMaxGapObservers];

    {
        BleLock lock(self.mutex_);
        for (size_t i = 0; i < kMaxGapObservers; ++i)
        {
            snapshot[i] = self.gapObservers_[i];
        }
    }

    for (size_t i = 0; i < kMaxGapObservers; ++i)
    {
        if (snapshot[i])
        {
            snapshot[i]->onGapEvent(event, param);
        }
    }
}

void BleStack::gattcCallback(esp_gattc_cb_event_t event,
                             esp_gatt_if_t gattcIf,
                             esp_ble_gattc_cb_param_t *param)
{
    BleStack &self = instance();
    GattcObserver *observer;

    {
        BleLock lock(self.mutex_);
        observer = self.gattcObserver_;
    }

    if (observer)
    {
        observer->onGattcEvent(event, gattcIf, param);
    }
}
