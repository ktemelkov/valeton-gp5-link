#include "PresetManager.h"

#include "esp_log.h"
#include "gp5.h"


static const char *TAG = "PresetManager";

/**
 *
 */
PresetManager::PresetManager()
{
    currentPresetIndex = 0;
    currentGroupIndex = 0;
}

/**
 *
 */
BLEMessage PresetManager::reset()
{
    currentPresetIndex = 0;
    currentGroupIndex = 0;

    return apply();
}

/**
 *
 */
BLEMessage PresetManager::apply()
{
    return (this->*presets[currentGroupIndex][currentPresetIndex])();
}

/**
 *
 */
BLEMessage PresetManager::firstPreset()
{
    currentPresetIndex = 0;
    return apply();
}

/**
 *
 */
BLEMessage PresetManager::nextPreset()
{
    currentPresetIndex++;
    return apply();
}

/**
 *
 */
BLEMessage PresetManager::nextGroup()
{
    currentGroupIndex = (currentGroupIndex + 1) % NUM_GROUPS;
    return firstPreset();
}

/**
 *
 */
BLEMessage PresetManager::setRythm()
{
    ESP_LOGI(TAG, "Applying preset: Group %d, Preset %d\r\n", currentGroupIndex, currentPresetIndex);

    BLEMessage msg = { Preset_0, sizeof(Preset_0) };
    return msg;
}

/**
 *
 */
BLEMessage PresetManager::setSolo()
{
    ESP_LOGI(TAG, "Applying preset: Group %d, Preset %d\r\n", currentGroupIndex, currentPresetIndex);

    BLEMessage msg = { Preset_1, sizeof(Preset_1) };
    return msg;
}

/**
 *
 */
BLEMessage PresetManager::setClean()
{
    ESP_LOGI(TAG, "Applying preset: Group %d, Preset %d\r\n", currentGroupIndex, currentPresetIndex);
    BLEMessage msg = { Preset_2, sizeof(Preset_2) };
    return msg;
}

/**
 *
 */
BLEMessage PresetManager::setDropRythm()
{
    ESP_LOGI(TAG, "Applying preset: Group %d, Preset %d\r\n", currentGroupIndex, currentPresetIndex);

    BLEMessage msg = { Preset_3, sizeof(Preset_3) };
    return msg;
}

/**
 *
 */
BLEMessage PresetManager::setDropSolo()
{
    ESP_LOGI(TAG, "Applying preset: Group %d, Preset %d\r\n", currentGroupIndex, currentPresetIndex);

    BLEMessage msg = { Preset_4, sizeof(Preset_4)};
    return msg;
}

/**
 *
 */
BLEMessage PresetManager::setSouthOfIntro()
{
    ESP_LOGI(TAG, "Applying preset: Group %d, Preset %d\r\n", currentGroupIndex, currentPresetIndex);
    
    BLEMessage msg = { Preset_11, sizeof(Preset_11) };
    return msg;
}
