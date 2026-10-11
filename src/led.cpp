#include "led.h"

/**
 * 
 */
Led::Led(uint8_t pin)
    : _pin(pin)
{
    pinMode(_pin, OUTPUT);
    digitalWrite(_pin, HIGH);
}

/**
 * 
 */
void Led::on()
{ 
    digitalWrite(_pin, _state = LOW);
}

/**
 * 
 */
void Led::off()
{
    digitalWrite(_pin, _state = HIGH);
}

/**
 * 
 */
void Led::toggle()
{
    digitalWrite(_pin, _state = !_state);
}

