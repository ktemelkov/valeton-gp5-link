
#include "esp_pm.h"
#include "esp_sleep.h"
#include "esp_err.h"
#include "esp_log.h"

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
  esp_log_level_set("*", ESP_LOG_ERROR);

  Led led(LED_PIN);

  Buttons &buttons = Buttons::instance();
  buttons.begin((const uint8_t[]){BUTTON1_PIN, BUTTON2_PIN}, 2);

  BleScanner bleScanner;
  BleClient bleClient;

  PowerManager powerManager;
  PresetManager presetManager;
  presetManager.setup();

  BleApiService bleApiService;
  bleClient.setup(&bleApiService);
  bleApiService.setup(&bleClient, &presetManager);

  while (true)
  {
    vTaskDelay(pdMS_TO_TICKS(30));
    bleApiService.processNotifications();

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

    /*
     * Release volume button alone - next preset
     * Release tone button alone - next group
     * Hold tone button for 2 sec - ext group 1
     * Hold volume button for 2 sec - ext group 2
     * Press tone button and next press+release volume button - ext group 3
     * Press volume button and next press tone button - ext group 4
     */
    auto event1 = buttons.getEvent(BUTTON1_PIN); // button 1 is the top button
    auto state1 = buttons.getState(BUTTON1_PIN);
    auto event2 = buttons.getEvent(BUTTON2_PIN); // button 2 is the bottom button
    auto state2 = buttons.getState(BUTTON2_PIN);

    if (event1 == RELEASED)
    {
      led.toggle();
      powerManager.triggerWatchdog();

      if (state2 != RELEASED_STATE) // hold bottom and press top button
      {
        buttons.setState(BUTTON2_PIN, LONG_PRESSED_STATE);

        BLEMessage msg = presetManager.extGroup3();
        bleClient.Send(msg.data, msg.length);
      }
      else // press top button alone
      {
        BLEMessage msg = presetManager.nextPreset();
        bleClient.Send(msg.data, msg.length);
      }
    }

    if (event2 == RELEASED)
    {
      led.toggle();
      powerManager.triggerWatchdog();

      if (state1 != RELEASED_STATE) // hold top and press bottom button
      {
        buttons.setState(BUTTON1_PIN, LONG_PRESSED_STATE);

        BLEMessage msg = presetManager.extGroup4();
        bleClient.Send(msg.data, msg.length);
      }
      else // press bottom button alone
      {
        BLEMessage msg = presetManager.nextGroup();
        bleClient.Send(msg.data, msg.length);
      }
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
