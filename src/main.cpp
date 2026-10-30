#include <Arduino.h>
#include "esp_pm.h"
#include "esp_sleep.h"
#include "esp_err.h"

#include "buttons.h"
#include "BleScannerIdf.h"
#include "BleClientIdf.h"
#include "BleAdvertisedDevice.h"
#include "PowerManager.h"
#include "PresetManager.h"
#include "led.h"

#include "debug.h"

#define BUTTON1_PIN GPIO_NUM_5
#define BUTTON2_PIN GPIO_NUM_4

#define LED_PIN GPIO_NUM_8


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

  BleScannerIdf bleScanner;
  BleClientIdf bleClient;

  PowerManager powerManager(buttons, bleClient, bleScanner);
  PresetManager presetManager(bleClient);

  while (true)
  {
    powerManager.update();

    if (!bleClient.isConnected())
    {
      if (!bleScanner.isScanning())
      {
        bleScanner.StartScanning();
        delay(50);
        continue;
      }

      const BleAdvertisedDevice *advDevice = nullptr;
      size_t index = 0;

      while ((advDevice = bleScanner.getDevice(index++)) != nullptr)
      {
        if (!bleClient.isAllowedDevice(advDevice))
        {
          continue;
        }

        DEBUG_MSG("Found allowed device: %s\n", advDevice->getName().c_str());
        bleScanner.StopScanning();

        delay(50);
       
        if (bleClient.connect(advDevice))
        {
          presetManager.reset();
          break;
        }
      }

      delay(5);
      continue;
    }

    if (buttons.getEvent(BUTTON1_PIN) == PRESSED)
    {
      presetManager.nextPreset();
      led.toggle();
    }

    if (buttons.getEvent(BUTTON2_PIN) == PRESSED)
    {
      presetManager.nextGroup();
      led.toggle();
    }

    delay(20);
  }
}
