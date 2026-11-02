#include "Buttons.h"

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "Buttons";

namespace
{

    // Guards the event slots shared between the GPIO ISR and readers.
    portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

    // Only one GPIO ISR service may exist for the whole app.
    bool s_isrServiceInstalled = false;

    // Set by begin() before any handler can fire, so the ISR never has to go
    // through instance()'s lazy-initialisation guard.
    Buttons *s_self = NULL;

    // Debounce window in microseconds, so the ISR needs no 64-bit division.
    constexpr uint32_t kDebounceUs = DEBOUNCE_MS * 1000U;

    // ISR-safe tick (esp_timer_get_time is placed in IRAM); wraps every ~71 min,
    // which the unsigned subtraction below handles correctly.
    inline uint32_t nowUs()
    {
        return static_cast<uint32_t>(esp_timer_get_time());
    }

} // namespace

/**
 *
 */
Buttons &Buttons::instance()
{
    static Buttons s_instance;
    return s_instance;
}

/**
 * Configures the given pins as pulled-up inputs with change interrupts and
 * records their initial (released) level.
 */
void Buttons::begin(const uint8_t *buttonPins, uint16_t buttonCount)
{
    _buttonCount = 0;
    s_self = this;

    if (buttonPins == NULL || buttonCount == 0)
    {
        ESP_LOGW(TAG, "begin() called without pins");
        return;
    }

    if (!s_isrServiceInstalled)
    {
        esp_err_t err = gpio_install_isr_service(0);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
        {
            ESP_LOGE(TAG, "gpio_install_isr_service failed: %s", esp_err_to_name(err));
            return;
        }
        s_isrServiceInstalled = true;
    }

    const uint16_t count = (buttonCount > MAX_BUTTONS) ? MAX_BUTTONS : buttonCount;

    for (uint16_t i = 0; i < count; ++i)
    {
        const uint8_t pin = buttonPins[i];

        gpio_config_t io = {};
        io.pin_bit_mask = 1ULL << pin;
        io.mode = GPIO_MODE_INPUT;
        io.pull_up_en = GPIO_PULLUP_ENABLE;
        io.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io.intr_type = GPIO_INTR_ANYEDGE;

        esp_err_t err = gpio_config(&io);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "gpio_config failed for pin %u: %s", pin, esp_err_to_name(err));
            continue;
        }

        err = gpio_isr_handler_add(static_cast<gpio_num_t>(pin), &Buttons::isr,
                                   reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "gpio_isr_handler_add failed for pin %u: %s", pin, esp_err_to_name(err));
            continue;
        }

        _buttonPin[i] = pin;
        _lastIsr[i] = 0;
        _stableLevel[i] = static_cast<uint8_t>(gpio_get_level(static_cast<gpio_num_t>(pin)));
        _btnEvent[i] = NO_CHANGE;
        _buttonCount++;

        ESP_LOGI(TAG, "button %u on gpio %u (level %u)", i, pin, _stableLevel[i]);
    }
}

/**
 * Returns the pending event for a button pin, optionally consuming it.
 */
ButtonEvent Buttons::getEvent(uint8_t buttonPin, bool clearEvent)
{
    ButtonEvent event = NO_CHANGE;

    portENTER_CRITICAL(&s_mux);

    const uint8_t index = indexOfPin(buttonPin);
    if (index != 0xff)
    {
        event = _btnEvent[index];
        if (clearEvent)
        {
            _btnEvent[index] = NO_CHANGE;
        }
    }

    portEXIT_CRITICAL(&s_mux);
    return event;
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
    Buttons *self = s_self;
    if (self == NULL)
    {
        return;
    }
    self->handleInterrupt(static_cast<uint8_t>(reinterpret_cast<uintptr_t>(args)));
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

    const uint32_t now = nowUs();

    if ((now - _lastIsr[index]) < kDebounceUs)
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
    // Buttons are wired to ground, so a low level means pressed.
    _btnEvent[index] = (level == 0) ? PRESSED : RELEASED;
}
