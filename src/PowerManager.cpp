#include "PowerManager.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_pm.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_sleep.h"

static const char *TAG = "PowerManager";

static constexpr uint32_t DEEP_SLEEP_IDLE_MS = 15UL * 60UL * 1000UL;
static constexpr uint32_t HOLD_FOR_DEEP_SLEEP_MS = 4 * 1000UL;

/**
 *
 */
PowerManager::PowerManager(Buttons &buttons)
    : buttons(buttons)
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
bool PowerManager::shouldSleep()
{
    bool shouldSleep = false;
    uint32_t nowMs = static_cast<uint64_t>(esp_timer_get_time()) / 1000;

    for (size_t i = 0; i < buttons.getButtonCount(); ++i)
    {
        ButtonEvent event = buttons.getEvent(buttons.getPins()[i], false);

        if (event == PRESSED)
        {
            lastPressMs = nowMs;
        }
        else if (event == RELEASED)
        {
            if (lastPressMs > lastReleaseMs && (uint32_t)(nowMs - lastPressMs) >= HOLD_FOR_DEEP_SLEEP_MS)
            {
                ESP_LOGI(TAG, "A button is being held for %u ms, entering deep sleep.\n", HOLD_FOR_DEEP_SLEEP_MS);
                shouldSleep = true;
            }

            lastReleaseMs = nowMs;
        }
    }

    if ((uint32_t)(nowMs - lastPressMs) >= DEEP_SLEEP_IDLE_MS)
    {
        ESP_LOGI(TAG, "No button press for %u ms, entering deep sleep.\n", DEEP_SLEEP_IDLE_MS);
        shouldSleep = true;
    }

    return shouldSleep;
}

void PowerManager::doSleep()
{
    configureButtonWakeupForDeepSleep();

    vTaskDelay(pdMS_TO_TICKS(20));
    esp_deep_sleep_start();
}

/**
 *
 */
void PowerManager::configureButtonWakeupForDeepSleep()
{
    uint64_t wakeMask = 0;
    for (size_t i = 0; i < buttons.getButtonCount(); ++i)
    {
        uint8_t pin = buttons.getPins()[i];
        wakeMask |= (1ULL << pin);
    }

    ESP_ERROR_CHECK(esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL));
    ESP_ERROR_CHECK(esp_deep_sleep_enable_gpio_wakeup(wakeMask, ESP_GPIO_WAKEUP_GPIO_LOW));
}
