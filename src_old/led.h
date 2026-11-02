#ifndef __LED_H
#define __LED_H

#include <Arduino.h>

/**
 * 
 */
class Led
{
public:
    Led(uint8_t pin);

    void on();
    void off();
    void toggle();

private:
    uint8_t _pin;
    uint8_t _state = HIGH;
};

#endif // __LED_H