#include "debug.h"

/**
 *
 */
void printHexBuffer(const uint8_t *buffer, int len)
{
  Serial.print("Data: ");

  for (int i = 0; i < len; i++)
  {
    Serial.printf("%02X ", buffer[i]);
  }

  Serial.println();
}
