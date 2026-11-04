#ifndef __PRESET_MANAGER_H__
#define __PRESET_MANAGER_H__

#include <cstdint>
#include <cstddef>

constexpr uint8_t NUM_GROUPS = 2;
constexpr uint8_t NUM_EXT_GROUPS = 2;
constexpr uint8_t NUM_PRESETS_PER_GROUP = 10;

/**
 * Represents a BLE message with data and length.
 */
struct BLEMessage
{
    uint8_t* data;
    size_t length;
};


/**
 * Manages presets and groups for the BLE device.
 */
class PresetManager
{
public:
    using PresetCallback = BLEMessage (PresetManager::*)(void);

public:
    PresetManager();

    BLEMessage reset();
    BLEMessage nextPreset();
    BLEMessage nextGroup();

    BLEMessage extGroup1();
    BLEMessage extGroup2();

protected:
    BLEMessage setRythm();
    BLEMessage setRythmAbyss();
    BLEMessage setSolo();
    BLEMessage setClean();
    BLEMessage setDropRythm();
    BLEMessage setDropSolo();
    BLEMessage setSouthOfIntro();
    BLEMessage firstPreset();

    BLEMessage apply();

private:
    uint8_t currentPresetIndex;
    uint8_t currentGroupIndex;

    PresetCallback presets[NUM_GROUPS + NUM_EXT_GROUPS][NUM_PRESETS_PER_GROUP] = {
        {&PresetManager::setRythm, &PresetManager::setSolo, &PresetManager::firstPreset, 0, 0, 0, 0, 0, 0, 0},
        {&PresetManager::setDropRythm, &PresetManager::setDropSolo, &PresetManager::firstPreset, 0, 0, 0, 0, 0, 0, 0},
        {&PresetManager::setRythmAbyss, &PresetManager::setClean, &PresetManager::setRythmAbyss, &PresetManager::setSolo, &PresetManager::setRythmAbyss, &PresetManager::setClean, &PresetManager::reset, 0, 0, 0},
        {&PresetManager::setSouthOfIntro, &PresetManager::reset, 0, 0, 0, 0, 0, 0, 0, 0},
    };
};

#endif // __PRESET_MANAGER_H__