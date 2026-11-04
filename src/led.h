#ifndef __LED_H
#define __LED_H

#include <stdint.h>

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
    void writeLevel(uint8_t pin, uint8_t state);

private:
    uint8_t _pin;
    uint8_t _state = 0;
};

#endif // __LED_H