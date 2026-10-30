#include <PowerManager.h>
#include "debug.h"
#include <esp32c3/pm.h>
#include <esp_pm.h>

static constexpr uint32_t DEEP_SLEEP_IDLE_MS = 15UL * 60UL * 1000UL;
static constexpr uint32_t HOLD_FOR_DEEP_SLEEP_MS = 4 * 1000UL;

/**
 *
 */
PowerManager::PowerManager(Buttons &buttons, IBleClient &bleClient, IBleScanner &bleScanner)
    : buttons(buttons), bleClient(bleClient), bleScanner(bleScanner)
{
    esp_pm_config_esp32c3_t pm_config;
    pm_config.max_freq_mhz = 40;         // Set maximum CPU frequency to 40 MHz
    pm_config.min_freq_mhz = 2;          // Set minimum CPU frequency to 0 MHz
    pm_config.light_sleep_enable = true; // Enable light sleep mode
    esp_pm_configure(&pm_config);
}

/**
 *
 */
void PowerManager::update()
{
    bool shouldSleep = false;

    for (size_t i = 0; i < buttons.getButtonCount(); ++i)
    {
        ButtonEvent event = buttons.getEvent(buttons.getPins()[i], false);

        if (event == PRESSED)
        {
            lastPressMs = millis();
        }
        else if (event == RELEASED)
        {
            if (lastPressMs > lastReleaseMs && (uint32_t)(millis() - lastPressMs) >= HOLD_FOR_DEEP_SLEEP_MS)
            {
                DEBUG_MSG("A button is being held for %u ms, entering deep sleep.\n", HOLD_FOR_DEEP_SLEEP_MS);
                shouldSleep = true;
            }

            lastReleaseMs = millis();
        }
    }

    if ((uint32_t)(millis() - lastPressMs) >= DEEP_SLEEP_IDLE_MS)
    {
        DEBUG_MSG("No button press for %u ms, entering deep sleep.\n", DEEP_SLEEP_IDLE_MS);
        shouldSleep = true;
    }

    if (shouldSleep)
    {
        bleClient.disconnect();
        bleScanner.StopScanning();

        configureButtonWakeupForDeepSleep();

        delay(20);
        esp_deep_sleep_start();
    }
}

/**
 *
 */
void PowerManager::configureButtonWakeupForDeepSleep()
{
    // Keep pins in a known state before sleep.
    uint64_t wakeMask = 0;
    for (size_t i = 0; i < buttons.getButtonCount(); ++i)
    {
        uint8_t pin = buttons.getPins()[i];

        pinMode(pin, INPUT_PULLUP);
        wakeMask |= (1ULL << pin);
    }

    ESP_ERROR_CHECK(esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL));
    ESP_ERROR_CHECK(esp_deep_sleep_enable_gpio_wakeup(wakeMask, ESP_GPIO_WAKEUP_GPIO_LOW));
}
