#include "Buttons.h"

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "Buttons";

/**
 *
 */
Buttons &Buttons::instance()
{
    static Buttons s_instance;
    return s_instance;
}

/**
 * @brief Initializes the button manager, setting all pins to default states.
 * This constructor sets all button pins to 0, stable levels to 1 (released),
 * last ISR timestamps to 0, and button states to UNDEFINED_STATE.
 */
Buttons::Buttons()
{
    for (uint16_t i = 0; i < MAX_BUTTONS; ++i)
    {
        _buttonPin[i] = 0;
        _stableLevel[i] = 1;
        _lastIsr[i] = 0;
        _btnState[i] = UNDEFINED_STATE;
    }
}

/**
 * Configures the given pins as pulled-up inputs with change interrupts and
 * records their initial (released) level.
 */
void Buttons::begin(const uint8_t *buttonPins, uint16_t buttonCount)
{
    _buttonCount = 0;

    if (buttonPins == NULL || buttonCount == 0)
    {
        ESP_LOGW(TAG, "begin() called without pins");
        return;
    }

    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE(TAG, "gpio_install_isr_service failed: %s", esp_err_to_name(err));
        return;
    }

    const uint16_t count = (buttonCount > MAX_BUTTONS) ? MAX_BUTTONS : buttonCount;

    for (uint16_t i = 0; i < count; ++i)
    {
        const uint8_t pin = buttonPins[i];
        const uint16_t index = _buttonCount;

        gpio_config_t io = {};
        io.pin_bit_mask = 1ULL << pin;
        io.mode = GPIO_MODE_INPUT;
        io.pull_up_en = GPIO_PULLUP_ENABLE;
        io.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io.intr_type = GPIO_INTR_DISABLE;

        esp_err_t err = gpio_config(&io);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "gpio_config failed for pin %u: %s", pin, esp_err_to_name(err));
            continue;
        }

        const uint8_t level = static_cast<uint8_t>(gpio_get_level(static_cast<gpio_num_t>(pin)));
        _buttonPin[index] = pin;
        _stableLevel[index] = level;
        _lastIsr[index] = level == 0
                              ? static_cast<uint32_t>(esp_timer_get_time() / 1000)
                              : 0;
        _btnState[index] = level == 0 ? PRESSED_STATE : RELEASED_STATE;

        err = gpio_isr_handler_add(static_cast<gpio_num_t>(pin), &Buttons::isr,
                                   reinterpret_cast<void *>(static_cast<uintptr_t>(index)));
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "gpio_isr_handler_add failed for pin %u: %s", pin, esp_err_to_name(err));
            continue;
        }

        _buttonCount++;

        err = gpio_set_intr_type(static_cast<gpio_num_t>(pin), GPIO_INTR_ANYEDGE);
        if (err != ESP_OK)
        {
            _buttonCount--;
            const esp_err_t removeErr = gpio_isr_handler_remove(static_cast<gpio_num_t>(pin));
            if (removeErr != ESP_OK)
            {
                ESP_LOGE(TAG, "gpio_isr_handler_remove failed for pin %u: %s",
                         pin, esp_err_to_name(removeErr));
            }
            ESP_LOGE(TAG, "gpio_set_intr_type failed for pin %u: %s", pin, esp_err_to_name(err));
            continue;
        }

        ESP_LOGI(TAG, "button %u on gpio %u (level %u)", index, pin, level);
    }
}

/**
 * Returns the pending event for a button pin, optionally consuming it.
 */
ButtonEvent Buttons::getEvent(uint8_t buttonPin)
{
    const uint8_t index = indexOfPin(buttonPin);

    if (index != 0xff)
    {
        const uint32_t now = static_cast<uint32_t>(esp_timer_get_time() / 1000);

        // Buttons are wired to ground, so a low level means pressed.
        if (_stableLevel[index] == 0 && _btnState[index] == RELEASED_STATE)
        {
            _btnState[index] = PRESSED_STATE;
            return PRESSED;
        }

        if (_stableLevel[index] != 0 && _btnState[index] != RELEASED_STATE)
        {
            ButtonEvent ret = (_btnState[index] != LONG_PRESSED_STATE) ? RELEASED : NO_CHANGE;

            _btnState[index] = RELEASED_STATE;
            return ret;
        }

        if (_stableLevel[index] == 0 && (now - _lastIsr[index]) > HOLD_MS && _btnState[index] != LONG_PRESSED_STATE)
        {
            _btnState[index] = LONG_PRESSED_STATE;
            return HOLD;
        }
    }

    return NO_CHANGE;
}

/**
 * @brief Returns the current state of a button pin.
 * @param buttonPin The GPIO pin number of the button.
 * @return The current state of the button (PRESSED_STATE or RELEASED_STATE).
 */
ButtonState Buttons::getState(uint8_t buttonPin)
{
    const uint8_t index = indexOfPin(buttonPin);
    if (index != 0xff)
    {
        return _btnState[index];
    }

    return RELEASED_STATE;
}

/**
 * @brief Sets the current state of a button pin.
 * @param buttonPin The GPIO pin number of the button.
 * @param state The state to set for the button.
 */
void Buttons::setState(uint8_t buttonPin, ButtonState state)
{
    const uint8_t index = indexOfPin(buttonPin);
    if (index != 0xff)
    {
        _btnState[index] = state;
    }
}


/**
 * Index of a pin in the pin table, or 0xff when the pin is not registered.
 */
uint8_t Buttons::indexOfPin(uint8_t buttonPin) const
{
    for (uint16_t i = 0; i < _buttonCount; ++i)
    {
        if (_buttonPin[i] == buttonPin)
        {
            return static_cast<uint8_t>(i);
        }
    }
    return 0xff;
}

/**
 * GPIO ISR service entry point, `args` carries the button index.
 */
/**
 * GPIO ISR service entry point, `args` carries the button index.
 * IRAM_ATTR is declared in the header; repeating it here would clash sections.
 */
void Buttons::isr(void *args)
{
    instance().handleInterrupt(static_cast<uint8_t>(reinterpret_cast<uintptr_t>(args)));
}

/**
 * Debounced edge detection: a level is only accepted when it differs from the
 * stored one and the previously accepted edge is older than DEBOUNCE_MS.
 */
void Buttons::handleInterrupt(uint8_t index)
{
    if (index >= _buttonCount)
    {
        return;
    }

    const uint32_t now = static_cast<uint32_t>(esp_timer_get_time() / 1000);

    if ((now - _lastIsr[index]) < DEBOUNCE_MS)
    {
        return;
    }

    const uint8_t level = static_cast<uint8_t>(gpio_get_level(static_cast<gpio_num_t>(_buttonPin[index])));

    if (level == _stableLevel[index])
    {
        return;
    }

    _lastIsr[index] = now;
    _stableLevel[index] = level;
}
