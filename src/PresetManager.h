#ifndef __PRESET_MANAGER_H__
#define __PRESET_MANAGER_H__

#include "IBleClient.h"

constexpr uint8_t NUM_GROUPS = 4;
constexpr uint8_t NUM_PRESETS_PER_GROUP = 10;

/**
 *
 */
class PresetManager
{
public:
    using PresetCallback = void (PresetManager::*)(void);

public:
    PresetManager(IBleClient &bleClient);

    void reset();
    void nextPreset();
    void nextGroup();

protected:
    void setRythm();
    void setSolo();
    void setClean();
    void setDropRythm();
    void setDropSolo();
    void setSouthOfIntro();
    void firstPreset();

    void apply();

private:
    uint8_t currentPresetIndex;
    uint8_t currentGroupIndex;
    PresetCallback presets[NUM_GROUPS][NUM_PRESETS_PER_GROUP] = {
        {&PresetManager::setRythm, &PresetManager::setSolo, &PresetManager::firstPreset, 0, 0, 0, 0, 0, 0, 0},
        {&PresetManager::setRythm, &PresetManager::setClean, &PresetManager::setRythm, &PresetManager::setSolo, &PresetManager::setRythm, &PresetManager::setClean, &PresetManager::reset, 0, 0, 0},
        {&PresetManager::setSouthOfIntro, &PresetManager::reset, 0, 0, 0, 0, 0, 0, 0, 0},
        {&PresetManager::setDropRythm, &PresetManager::setDropSolo, &PresetManager::firstPreset, 0, 0, 0, 0, 0, 0, 0}};

    IBleClient &bleClient;
};

#endif // __PRESET_MANAGER_H__