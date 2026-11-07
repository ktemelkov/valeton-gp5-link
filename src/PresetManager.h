#ifndef __PRESET_MANAGER_H__
#define __PRESET_MANAGER_H__

#include <cstdint>
#include <cstddef>
#include <mutex>
#include "BleEndpoint.h"

constexpr uint8_t MAX_NORMAL_GROUPS = 12;
constexpr uint8_t NUM_EXT_GROUPS = 4;
constexpr uint8_t MAX_GROUPS = MAX_NORMAL_GROUPS + NUM_EXT_GROUPS;
constexpr uint8_t NUM_PRESETS_PER_GROUP = 8;

/**
 * Represents a BLE message with data and length.
 */
struct BLEMessage
{
    uint8_t *data;
    size_t length;
};

/**
 * Defines the possible actions that can be performed on a preset.
 */
enum PresetAction : uint8_t
{
    PRESET_ACTION_NAV = 0b00000000,
    PRESET_ACTION_SET = 0b10000000,
};

/**
 * Represents a preset action with an action type and an argument.
 */
typedef uint8_t PresetMapEntry;

/**
 * Represents the configuration of presets and groups.
 */
struct Configuration
{
    PresetMapEntry presetMap[MAX_GROUPS][NUM_PRESETS_PER_GROUP];
};


/**
 * Manages presets and groups for the BLE device.
 * Provides functionality to navigate, apply, and edit preset configurations.
 */
class PresetManager
{
public:
    PresetManager();
 
    bool setup();
    bool handleApi(const uint8_t *data, size_t length, BleEndpoint &reply);

    BLEMessage nextPreset();
    BLEMessage nextGroup();

    BLEMessage extGroup1() { return extGroup(0); }
    BLEMessage extGroup2() { return extGroup(1); }
    BLEMessage extGroup3() { return extGroup(2); }
    BLEMessage extGroup4() { return extGroup(3); }

protected:
    BLEMessage extGroup(uint16_t index);
    BLEMessage setPreset(uint16_t arg);
    BLEMessage apply();

private:
    bool valid(const Configuration &value) const;
    bool persist(const Configuration &value);

private:
    uint8_t currentPresetIndex;
    uint8_t currentGroupIndex;

    Configuration configuration{};
    std::mutex mutex;
};

#endif // __PRESET_MANAGER_H__