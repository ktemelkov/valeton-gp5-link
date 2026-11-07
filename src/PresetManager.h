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
    uint8_t *data;
    size_t length;
};

struct PresetAction
{
    uint16_t action;
    uint16_t arg;
};

#define PRESET_ACTION_NONE 0
#define PRESET_ACTION_SET 1
#define PRESET_ACTION_RESET 2
#define PRESET_ACTION_FIRST 3

#define PRESET_RYTHM 0
#define PRESET_SOLO 1
#define PRESET_CLEAN 2
#define PRESET_DROP_RYTHM 3
#define PRESET_DROP_SOLO 4
#define PRESET_SOUTH_OF_INTRO 11
#define PRESET_RYTHM_ABYSS 12

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

    BLEMessage setPreset(uint16_t arg);
    BLEMessage firstPreset();

    BLEMessage apply();

private:
    uint8_t currentPresetIndex;
    uint8_t currentGroupIndex;

    PresetAction presets[NUM_GROUPS + NUM_EXT_GROUPS][NUM_PRESETS_PER_GROUP] = {
        {{PRESET_ACTION_SET, PRESET_RYTHM}, {PRESET_ACTION_SET, PRESET_SOLO}, {PRESET_ACTION_FIRST, 0}, {PRESET_ACTION_NONE, 0}, {PRESET_ACTION_NONE, 0}, {PRESET_ACTION_NONE, 0}, {PRESET_ACTION_NONE, 0}, {PRESET_ACTION_NONE, 0}, {PRESET_ACTION_NONE, 0}, {PRESET_ACTION_NONE, 0}},
        {{PRESET_ACTION_SET, PRESET_DROP_RYTHM}, {PRESET_ACTION_SET, PRESET_DROP_SOLO}, {PRESET_ACTION_FIRST, 0}, {PRESET_ACTION_NONE, 0}, {PRESET_ACTION_NONE, 0}, {PRESET_ACTION_NONE, 0}, {PRESET_ACTION_NONE, 0}, {PRESET_ACTION_NONE, 0}, {PRESET_ACTION_NONE, 0}, {PRESET_ACTION_NONE, 0}},
        {{PRESET_ACTION_SET, PRESET_RYTHM_ABYSS}, {PRESET_ACTION_SET, PRESET_CLEAN}, {PRESET_ACTION_SET, PRESET_RYTHM_ABYSS}, {PRESET_ACTION_SET, PRESET_SOLO}, {PRESET_ACTION_SET, PRESET_RYTHM_ABYSS}, {PRESET_ACTION_SET, PRESET_CLEAN}, {PRESET_ACTION_RESET, 0}, {PRESET_ACTION_NONE, 0}, {PRESET_ACTION_NONE, 0}, {PRESET_ACTION_NONE, 0}},
        {{PRESET_ACTION_SET, PRESET_SOUTH_OF_INTRO}, {PRESET_ACTION_RESET, 0}, {PRESET_ACTION_FIRST, 0}, {PRESET_ACTION_NONE, 0}, {PRESET_ACTION_NONE, 0}, {PRESET_ACTION_NONE, 0}, {PRESET_ACTION_NONE, 0}, {PRESET_ACTION_NONE, 0}, {PRESET_ACTION_NONE, 0}, {PRESET_ACTION_NONE, 0}},
    };
};

#endif // __PRESET_MANAGER_H__