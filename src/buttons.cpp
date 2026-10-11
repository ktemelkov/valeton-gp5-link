#include "buttons.h"

/**
 *
 */
Buttons &Buttons::instance()
{
    static Buttons s_instance;
    return s_instance;
}

/**
 *
 */
void Buttons::begin(const uint8_t *buttonPins, size_t buttonCount)
{
    void (*interrupts[MAX_BUTTONS])() = {
        handleButtonInterrupt0,
        handleButtonInterrupt1,
        handleButtonInterrupt2,
        handleButtonInterrupt3,
        handleButtonInterrupt4,
        handleButtonInterrupt5,
        handleButtonInterrupt6,
        handleButtonInterrupt7,
        handleButtonInterrupt8,
        handleButtonInterrupt9};

    _buttonCount = 0;

    for (size_t i = 0; i < buttonCount && i < MAX_BUTTONS; ++i)
    {
        _buttonPin[i] = buttonPins[i];
        _lastIsr[i] = 0;
        _btnEvent[i] = NO_CHANGE;
        _buttonCount++;

        pinMode(_buttonPin[i], INPUT_PULLUP);
        _stableLevel[i] = (uint8_t)digitalRead(_buttonPin[i]);
        attachInterrupt(digitalPinToInterrupt(_buttonPin[i]), interrupts[i], CHANGE);
    }
}

/**
 *
 */
ButtonEvent Buttons::getEvent(uint8_t buttonPin, bool clearEvent)
{
    ButtonEvent event = NO_CHANGE;

    noInterrupts();

    for (size_t i = 0; i < _buttonCount; ++i)
    {
        if (_buttonPin[i] == buttonPin)
        {
            event = _btnEvent[i];
            if (clearEvent)
            {
                _btnEvent[i] = NO_CHANGE; // Reset the event after reading
            }
            break;
        }
    }

    interrupts();
    return event;
}

/**
 *
 */
void IRAM_ATTR Buttons::handleButtonInterrupt(uint8_t index)
{
    uint32_t now = millis();
    if ((uint32_t)(now - _lastIsr[index]) < DEBOUNCE_MS)
    {
        return;
    }

    uint8_t level = (uint8_t)gpio_get_level((gpio_num_t)_buttonPin[index]);
    if (level == _stableLevel[index])
    {
        return;
    }

    _lastIsr[index] = now;
    _stableLevel[index] = level;
    _btnEvent[index] = (level == 0) ? PRESSED : RELEASED;
}
