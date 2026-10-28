#include <Arduino.h>
#include <NimBLEDevice.h>
#include "esp_pm.h"
#include "esp_sleep.h"
#include "esp_err.h"

#include "buttons.h"
#include "BleScanner.h"
#include "BleClient.h"
#include "PowerManager.h"
#include "led.h"

#include "debug.h"

#define BUTTON1_PIN GPIO_NUM_5
#define BUTTON2_PIN GPIO_NUM_4

#define LED_PIN GPIO_NUM_8

#define MIDI_MSG_BUTTON1 0
#define MIDI_MSG_BUTTON2 1

uint8_t MidiMessages[][5] = {
    {0x80, 0x80, 0xB0, 0x01, 0x00}, // Control Change message with controller number set to 1 and value 0
    {0x80, 0x80, 0xB0, 0x02, 0x00}  // Control Change message with controller number set to 2 and value 0
};

/**
 *
 */
extern "C" void app_main()
{
  initArduino();

#ifdef ENABLE_DEBUG_MESSAGES
  Serial.begin(115200);
  delay(1000);
#endif

  Led led(LED_PIN);

  Buttons &buttons = Buttons::instance();
  buttons.begin((const uint8_t[]){BUTTON1_PIN, BUTTON2_PIN}, 2);

  BleScanner bleScanner;
  BleClient bleClient;

  PowerManager powerManager(buttons, bleClient, bleScanner);

  while (true)
  {
    powerManager.update();

    if (!bleClient.isConnected())
    {
      if (!bleScanner.isScanning())
      {
        bleScanner.StartScanning();
        continue;
      }

      const NimBLEAdvertisedDevice *advDevice = nullptr;
      size_t index = 0;

      while ((advDevice = bleScanner.getDevice(index++)) != nullptr)
      {
        if (!bleClient.isAllowedDevice(advDevice))
        {
          continue;
        }

        bleScanner.StopScanning();
       
        if (bleClient.connect(advDevice))
        {
          break;
        }
      }

      delay(5);
      continue;
    }

    if (buttons.getEvent(BUTTON1_PIN) == PRESSED)
    {
      bleClient.send(MidiMessages[MIDI_MSG_BUTTON1], sizeof(MidiMessages[MIDI_MSG_BUTTON1]));
      led.toggle();
    }

    if (buttons.getEvent(BUTTON2_PIN) == PRESSED)
    {
      bleClient.send(MidiMessages[MIDI_MSG_BUTTON2], sizeof(MidiMessages[MIDI_MSG_BUTTON2]));
      led.toggle();
    }

    delay(20);
  }
}
