#ifndef BUTTON_H
#define BUTTON_H

#include <stdint.h>

#include "esp_attr.h"

#define MAX_BUTTONS 10
#define DEBOUNCE_MS 80
#define HOLD_MS 1000

enum ButtonEvent
{
    NO_CHANGE = 0,
    PRESSED = 1,
    RELEASED = 2,
    HOLD = 3,
};

enum ButtonState
{
    RELEASED_STATE = 0,
    PRESSED_STATE = 1
};

/**
 * @brief Singleton class to manage button states.
 */
class Buttons
{
private:
    Buttons() = default;

public:
    static Buttons& instance();

    void begin(const uint8_t* buttonPins, uint16_t buttonCount);

    ButtonEvent getEvent(uint8_t buttonPin, bool clearEvent = true);

    const uint8_t* getPins() const { return _buttonPin; }
    uint16_t getButtonCount() const { return _buttonCount; }

private:
    /** GPIO ISR service entry point, `args` carries the button index. */
    static void IRAM_ATTR isr(void *args);

    void IRAM_ATTR handleInterrupt(uint8_t index);
    uint8_t indexOfPin(uint8_t buttonPin) const;

private:
    uint8_t _buttonPin[MAX_BUTTONS] = {0};
    volatile uint8_t _stableLevel[MAX_BUTTONS] = {1};
    volatile uint32_t _lastIsr[MAX_BUTTONS] = {0};
    volatile ButtonEvent _btnEvent[MAX_BUTTONS] = {NO_CHANGE};

    uint16_t _buttonCount = 0;
};

#endif // BUTTON_H