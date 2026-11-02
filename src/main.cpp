#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "hal/gpio_types.h"
#include "esp_log.h"

#include "Led.h"
#include "Buttons.h"
#include "PresetManager.h"
#include "PowerManager.h"

#include "BleScanner.h"
#include "BleClient.h"

#define BUTTON1_PIN GPIO_NUM_5
#define BUTTON2_PIN GPIO_NUM_4

#define LED_PIN GPIO_NUM_21

/**
 *
 */
extern "C" void app_main(void)
{
    Led led(LED_PIN);
    led.off();

    const uint8_t buttonPins[] = {BUTTON1_PIN, BUTTON2_PIN};
    Buttons &buttons = Buttons::instance();
    buttons.begin(buttonPins, sizeof(buttonPins) / sizeof(buttonPins[0]));

    PowerManager powerManager(buttons);
    PresetManager presetManager;

    BleScanner bleScanner;
    BleClient bleClient;

    if (!bleScanner.Init())
    {
        ESP_LOGE("main", "Failed to initialize BLE scanner.");
        return;
    }

    bleScanner.StartScanning();

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(20));

        if (powerManager.shouldSleep())
        {
            bleClient.Disconnect();
            bleScanner.Shutdown();
            powerManager.doSleep();
        }

        if (!bleClient.IsConnected())
        {
            if (!bleScanner.IsScanning())
            {
                bleScanner.StartScanning();
                continue;
            }

            ble_addr_t device;
            if (!bleScanner.GetFoundDevice(device))
            {
                continue;
            }

            bleScanner.StopScanning();
            bleClient.Connect(device);
            continue;
        }

        if (buttons.getEvent(BUTTON1_PIN) == PRESSED)
        {
            led.toggle();
            auto msg = presetManager.nextPreset();
            bleClient.Send(msg.data, msg.length);
        }

        if (buttons.getEvent(BUTTON2_PIN) == PRESSED)
        {
            led.toggle();
            auto msg = presetManager.nextGroup();
            bleClient.Send(msg.data, msg.length);
        }        
    }
}
