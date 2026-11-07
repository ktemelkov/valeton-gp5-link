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
    PresetAction action = presets[currentGroupIndex][currentPresetIndex];

    if (action.action == PRESET_ACTION_SET)
    {
        return setPreset(action.arg);
    }

    if (action.action == PRESET_ACTION_RESET)
    {
        return reset();
    }

    if (action.action == PRESET_ACTION_FIRST)
    {
        return firstPreset();
    }

    BLEMessage msg = {nullptr, 0};
    return msg;
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
BLEMessage PresetManager::extGroup1()
{
    currentPresetIndex = 0;
    currentGroupIndex = 2;
    return apply();
}

/**
 *
 */
BLEMessage PresetManager::extGroup2()
{
    currentPresetIndex = 0;
    currentGroupIndex = 3;
    return apply();
}

/**
 *
 */
BLEMessage PresetManager::setPreset(uint16_t arg)
{
    ESP_LOGI(TAG, "Applying preset: Group %d, Preset %d, Arg %d\r\n", currentGroupIndex, currentPresetIndex, arg);

    BLEMessage msg = {Presets[arg], sizeof(Presets[arg])};
    return msg;
}
