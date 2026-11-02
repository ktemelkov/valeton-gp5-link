#ifndef __PRESET_MANAGER_H__
#define __PRESET_MANAGER_H__

#include <cstddef>
#include <cstdint>

constexpr uint8_t NUM_GROUPS = 4;
constexpr uint8_t NUM_PRESETS_PER_GROUP = 10;

/**
 * Represents a BLE message with data and length.
 */
struct BLEMessage
{
    uint8_t* data;
    uint16_t length;
};


/**
 *
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

protected:
    BLEMessage setRythm();
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

    PresetCallback presets[NUM_GROUPS][NUM_PRESETS_PER_GROUP] = {
        {&PresetManager::setRythm, &PresetManager::setSolo, &PresetManager::firstPreset, 0, 0, 0, 0, 0, 0, 0},
        {&PresetManager::setRythm, &PresetManager::setClean, &PresetManager::setRythm, &PresetManager::setSolo, &PresetManager::setRythm, &PresetManager::setClean, &PresetManager::reset, 0, 0, 0},
        {&PresetManager::setSouthOfIntro, &PresetManager::reset, 0, 0, 0, 0, 0, 0, 0, 0},
        {&PresetManager::setDropRythm, &PresetManager::setDropSolo, &PresetManager::firstPreset, 0, 0, 0, 0, 0, 0, 0}};
};

#endif // __PRESET_MANAGER_H__