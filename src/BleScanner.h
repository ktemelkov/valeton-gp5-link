#ifndef BLE_SCANNER_IDF_H
#define BLE_SCANNER_IDF_H

#include <stddef.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "host/ble_gap.h"


/**
 * BLE scanner implementation for ESP-IDF using the native NimBLE GAP API.
 *
 * This firmware is built with CONFIG_BT_NIMBLE_ENABLED and Bluedroid switched
 * off, so discovery goes through ble_gap_disc() / ble_gap_disc_cancel() rather
 * than the esp_ble_gap_* calls the old Bluedroid port used.
 *
 * NimBLE owns a single host task ("nimble_host") that runs the default event
 * queue: the GAP callback below always executes there, never on the caller's
 * task. That is what makes it safe for StartScanning()/StopScanning() to be
 * called from app_main's loop while advertisements are arriving.
 *
 * ble_gap_disc() issues the HCI scan-enable command synchronously (it waits on
 * the Command Status/Complete event) and only returns 0 once the controller
 * accepted it, so a successful return really does mean "scanning". That is why
 * there is no extra completion semaphore here - the old Bluedroid port needed
 * one because esp_ble_gap_start_ext_scan() returned while the command was still
 * queued, which made a connect issued right afterwards fail as "busy".
 */
class BleScanner
{
    static constexpr const char* TAG = "BleScanner";
    static constexpr const char* TARGET_DEVICE_NAME = "GP-5 BLE Kalin";

public:
    BleScanner();
    ~BleScanner();

    bool Init();
    void Shutdown();

    /**
     * Bring the controller + NimBLE host up if needed and start an endless,
     * active discovery procedure. Returns false when the stack could not be
     * started or the controller refused the command.
     */
    bool StartScanning();

    /**
     * Cancel discovery. Returns false only when the controller refused; if
     * nothing was running this is a no-op and returns true.
     */
    bool StopScanning();

    /**
     * True while a discovery procedure is in progress. Reads the stack's own
     * state so it stays correct even when the procedure ends on its own.
     */
    bool IsScanning();

    /**
     * Hand out the GP-5 that discovery turned up, if any.
     *
     * Returns false while nothing has matched yet. The device is only ever
     * copied out once the stack confirms that no discovery procedure is
     * running, so the caller can go straight into connecting: issuing a
     * connect while a scan is still in flight is what makes the controller
     * answer "busy".
     *
     * The result is sticky - repeated calls keep returning the same device -
     * and is cleared by the next StartScanning().
     */
    bool GetFoundDevice(ble_addr_t &addr);

private:
    // NimBLE callbacks. They are static because the stack takes plain C
    // function pointers; `arg` carries the instance where one is needed.
    static int gapEvent(ble_gap_event *event, void *arg);
    static void onHostSync(void);
    static void onHostReset(int reason);
    static void hostTask(void *arg);
    
private:
    static SemaphoreHandle_t syncNimbleInit;

    /**
     * Guards every member below. Reports are written on the NimBLE host task
     * while the application reads them from its own task.
     */
    SemaphoreHandle_t lockFoundAddress;
    ble_addr_t foundAddress;
};

#endif // BLE_SCANNER_IDF_H
