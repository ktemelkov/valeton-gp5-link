
#include "esp_pm.h"
#include "esp_sleep.h"
#include "esp_err.h"

#include "led.h"
#include "buttons.h"
#include "BleScanner.h"
#include "BleClient.h"

#include "PowerManager.h"
#include "PresetManager.h"
#include "BleApiService.h"


#define BUTTON1_PIN GPIO_NUM_5
#define BUTTON2_PIN GPIO_NUM_4

#define LED_PIN GPIO_NUM_8

/**
 *
 */
extern "C" void app_main()
{
  Led led(LED_PIN);

  Buttons &buttons = Buttons::instance();
  buttons.begin((const uint8_t[]){BUTTON1_PIN, BUTTON2_PIN}, 2);

  BleScanner bleScanner;
  BleClient bleClient;

  PowerManager powerManager;
  PresetManager presetManager;

  BleApiService bleApiService;
  bleClient.setup(&bleApiService);
  bleApiService.setup(&bleClient);

  while (true)
  {
    vTaskDelay(pdMS_TO_TICKS(20));

    if (powerManager.shouldSleep())
    {
      bleClient.Disconnect();
      bleScanner.StopScanning();

      powerManager.doSleep(buttons.getPins(), buttons.getButtonCount());
    }

    if (!bleClient.IsConnected())
    {
      if (!bleScanner.IsScanning())
      {
        bleScanner.StartScanning();
        continue;
      }

      NimBLEAddress deviceAddress = bleScanner.getDeviceAddress();

      if (deviceAddress.isNull())
      {
        continue;
      }

      bleScanner.StopScanning();
      bleClient.Connect(deviceAddress);
      continue;
    }

    auto event1 = buttons.getEvent(BUTTON1_PIN);
    auto event2 = buttons.getEvent(BUTTON2_PIN);

    if (event1 == PRESSED)
    {
      led.toggle();
      powerManager.triggerWatchdog();

      BLEMessage msg = presetManager.nextPreset();
      bleClient.Send(msg.data, msg.length);
    }

    if (event2 == PRESSED)
    {
      led.toggle();
      powerManager.triggerWatchdog();

      BLEMessage msg = presetManager.nextGroup();
      bleClient.Send(msg.data, msg.length);
    }

    if (event1 == HOLD)
    {
      led.toggle();
      powerManager.triggerWatchdog();

      BLEMessage msg = presetManager.extGroup2();
      bleClient.Send(msg.data, msg.length);
    }

    if (event2 == HOLD)
    {
      led.toggle();
      powerManager.triggerWatchdog();

      BLEMessage msg = presetManager.extGroup1();
      bleClient.Send(msg.data, msg.length);
    }
  }
}
