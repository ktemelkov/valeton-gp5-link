#ifndef DEBUG_H
#define DEBUG_H

#include <Arduino.h>

// #define ENABLE_DEBUG_MESSAGES


#ifdef ENABLE_DEBUG_MESSAGES
#define DEBUG_MSG(format, ...) Serial.printf(format, __VA_ARGS__)
#define DEBUG_BUFFER(buffer, len) printHexBuffer(buffer, len)

/**
 * @brief Print a buffer in hexadecimal format.
 * @param buffer The buffer to print.
 * @param len The length of the buffer.
 */
void printHexBuffer(const uint8_t *buffer, int len);


#else
#define DEBUG_MSG(format, ...)
#define DEBUG_BUFFER(buffer, len)
#endif

#endif