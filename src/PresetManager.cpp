#include "PresetManager.h"

#include "gp5.h"
#include "debug.h"

/**
 *
 */
PresetManager::PresetManager(IBleClient &bleClient) : bleClient(bleClient)
{
    currentPresetIndex = 0;
    currentGroupIndex = 0;
}

/**
 *
 */
void PresetManager::reset()
{
    currentPresetIndex = 0;
    currentGroupIndex = 0;

    apply();
}

/**
 *
 */
void PresetManager::apply()
{
    DEBUG_MSG("Applying preset: Group %d, Preset %d", currentGroupIndex, currentPresetIndex);

    (this->*presets[currentGroupIndex][currentPresetIndex])();
}

/**
 *
 */
void PresetManager::firstPreset()
{
    currentPresetIndex = 0;
    apply();
}

/**
 *
 */
void PresetManager::nextPreset()
{
    currentPresetIndex++;
    apply();
}

/**
 *
 */
void PresetManager::nextGroup()
{
    currentGroupIndex = (currentGroupIndex + 1) % NUM_GROUPS;
    firstPreset();
}

/**
 *
 */
void PresetManager::setRythm()
{
    bleClient.send(Preset_0, sizeof(Preset_0));
}

/**
 *
 */
void PresetManager::setSolo()
{
    bleClient.send(Preset_1, sizeof(Preset_1));
}

/**
 *
 */
void PresetManager::setClean()
{
    bleClient.send(Preset_2, sizeof(Preset_2));
}

/**
 *
 */
void PresetManager::setDropRythm()
{
    bleClient.send(Preset_3, sizeof(Preset_3));
}

/**
 *
 */
void PresetManager::setDropSolo()
{
    bleClient.send(Preset_4, sizeof(Preset_4));
}

/**
 *
 */
void PresetManager::setSouthOfIntro()
{
    bleClient.send(Preset_11, sizeof(Preset_11));
}
