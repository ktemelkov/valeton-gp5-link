#ifndef BUTTON_H
#define BUTTON_H

#include <stdint.h>
#include <Arduino.h>

#define MAX_BUTTONS 10
#define DEBOUNCE_MS 80


enum ButtonEvent
{
    NO_CHANGE = 0,
    PRESSED = 1,
    RELEASED = 2
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

    void begin(const uint8_t* buttonPins, size_t buttonCount);

    ButtonEvent getEvent(uint8_t buttonPin, bool clearEvent = true);

    const uint8_t* getPins() const { return _buttonPin; }
    size_t getButtonCount() const { return _buttonCount; }

private:
    static void IRAM_ATTR handleButtonInterrupt0() { instance().handleButtonInterrupt(0); }
    static void IRAM_ATTR handleButtonInterrupt1() { instance().handleButtonInterrupt(1); }
    static void IRAM_ATTR handleButtonInterrupt2() { instance().handleButtonInterrupt(2); }
    static void IRAM_ATTR handleButtonInterrupt3() { instance().handleButtonInterrupt(3); }
    static void IRAM_ATTR handleButtonInterrupt4() { instance().handleButtonInterrupt(4); }
    static void IRAM_ATTR handleButtonInterrupt5() { instance().handleButtonInterrupt(5); }
    static void IRAM_ATTR handleButtonInterrupt6() { instance().handleButtonInterrupt(6); }
    static void IRAM_ATTR handleButtonInterrupt7() { instance().handleButtonInterrupt(7); }
    static void IRAM_ATTR handleButtonInterrupt8() { instance().handleButtonInterrupt(8); }
    static void IRAM_ATTR handleButtonInterrupt9() { instance().handleButtonInterrupt(9); }

    void IRAM_ATTR handleButtonInterrupt(uint8_t index);

private:
    uint8_t _buttonPin[MAX_BUTTONS] = {0};
    volatile uint8_t _stableLevel[MAX_BUTTONS] = {1};
    volatile uint32_t _lastIsr[MAX_BUTTONS] = {0};
    volatile ButtonEvent _btnEvent[MAX_BUTTONS] = {NO_CHANGE};

    size_t _buttonCount = 0;
};

#endif // BUTTON_H