#include "PresetManager.h"

#include "esp_log.h"
#include "gp5.h"

static constexpr const char *TAG = "PresetManager";

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

BLEMessage PresetManager::extGroup1()
{
    currentPresetIndex = 0;
    currentGroupIndex = 2;
    return apply();
}

BLEMessage PresetManager::extGroup2()
{
    currentPresetIndex = 0;
    currentGroupIndex = 3;
    return apply();
}
/**
 *
 */
BLEMessage PresetManager::setRythm()
{
    ESP_LOGI(TAG, "Applying preset: Group %d, Preset %d\r\n", currentGroupIndex, currentPresetIndex);

    BLEMessage msg = {Presets[0], sizeof(Presets[0])};
    return msg;
}

/**
 *
 */
BLEMessage PresetManager::setRythmAbyss()
{
    ESP_LOGI(TAG, "Applying preset: Group %d, Preset %d\r\n", currentGroupIndex, currentPresetIndex);

    BLEMessage msg = {Presets[12], sizeof(Presets[12])};
    return msg;
}

/**
 *
 */
BLEMessage PresetManager::setSolo()
{
    ESP_LOGI(TAG, "Applying preset: Group %d, Preset %d\r\n", currentGroupIndex, currentPresetIndex);

    BLEMessage msg = {Presets[1], sizeof(Presets[1])};
    return msg;
}

/**
 *
 */
BLEMessage PresetManager::setClean()
{
    ESP_LOGI(TAG, "Applying preset: Group %d, Preset %d\r\n", currentGroupIndex, currentPresetIndex);

    BLEMessage msg = {Presets[2], sizeof(Presets[2])};
    return msg;
}

/**
 *
 */
BLEMessage PresetManager::setDropRythm()
{
    ESP_LOGI(TAG, "Applying preset: Group %d, Preset %d\r\n", currentGroupIndex, currentPresetIndex);

    BLEMessage msg = {Presets[3], sizeof(Presets[3])};
    return msg;
}

/**
 *
 */
BLEMessage PresetManager::setDropSolo()
{
    ESP_LOGI(TAG, "Applying preset: Group %d, Preset %d\r\n", currentGroupIndex, currentPresetIndex);

    BLEMessage msg = {Presets[4], sizeof(Presets[4])};
    return msg;
}

/**
 *
 */
BLEMessage PresetManager::setSouthOfIntro()
{
    ESP_LOGI(TAG, "Applying preset: Group %d, Preset %d\r\n", currentGroupIndex, currentPresetIndex);

    BLEMessage msg = {Presets[11], sizeof(Presets[11])};
    return msg;
}
