#include "Led.h"

#include "driver/gpio.h"
#include "esp_err.h"


/**
 * Configures the pin as a push-pull output and starts with the LED off.
 */
Led::Led(uint8_t pin)
    : _pin(pin)
{
    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << pin;
    io.mode = GPIO_MODE_OUTPUT;
    io.pull_up_en = GPIO_PULLUP_DISABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type = GPIO_INTR_DISABLE;

    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) {
        return;
    }

    _state = 0;
    writeLevel(_pin, _state);
}

/**
 * Lights the LED.
 */
void Led::on()
{
    _state = 1;
    writeLevel(_pin, _state);
}

/**
 * Turns the LED off.
 */
void Led::off()
{
    _state = 0;
    writeLevel(_pin, _state);
}

/**
 * Flips the current LED state.
 */
void Led::toggle()
{
    _state = _state ? 0 : 1;
    writeLevel(_pin, _state);
}

/**
 * Sets the GPIO pin to the specified state, inverting the logic for active-low LED.
 */
void Led::writeLevel(uint8_t pin, uint8_t state)
{
    gpio_set_level(static_cast<gpio_num_t>(pin), state ? 0 : 1);
}


