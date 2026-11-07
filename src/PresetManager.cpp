#include "PresetManager.h"

#include "esp_log.h"
#include "gp5.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <cstring>

static constexpr const char *TAG = "PresetManager";

#define PRESET_RYTHM 0
#define PRESET_SOLO 1
#define PRESET_CLEAN 2
#define PRESET_DROP_RYTHM 3
#define PRESET_DROP_SOLO 4
#define PRESET_SOUTH_OF_INTRO 11
#define PRESET_RYTHM_ABYSS 12

#define GET_ACTION(entry) ((entry) & 0b10000000)
#define GET_ARG(entry) ((entry) & 0b01111111)
#define GET_GROUP_ARG(entry) ((entry) & 0b00001111)
#define GET_PRESET_ARG(entry) (((entry) & 0b01110000) >> 4)

#define MAP_SET_ENTRY(gpPresetNo) (PRESET_ACTION_SET | ((gpPresetNo) & 0b01111111))
#define MAP_NAV_ENTRY(presetNo, groupNo) (((presetNo) << 4) | ((groupNo) & 0b00001111))

static constexpr uint8_t defaultNormalGroups = 2;

static PresetMapEntry defaultConfig[MAX_GROUPS][NUM_PRESETS_PER_GROUP] = {
    {MAP_SET_ENTRY(PRESET_RYTHM), MAP_SET_ENTRY(PRESET_SOLO), MAP_NAV_ENTRY(0,0), 0, 0, 0, 0, 0},
    {MAP_SET_ENTRY(PRESET_DROP_RYTHM), MAP_SET_ENTRY(PRESET_DROP_SOLO), MAP_NAV_ENTRY(0,1), 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0},
    {MAP_SET_ENTRY(PRESET_RYTHM_ABYSS), MAP_SET_ENTRY(PRESET_CLEAN), MAP_SET_ENTRY(PRESET_RYTHM_ABYSS), MAP_SET_ENTRY(PRESET_SOLO), MAP_SET_ENTRY(PRESET_RYTHM_ABYSS), MAP_SET_ENTRY(PRESET_CLEAN), MAP_NAV_ENTRY(0,0)},
    {MAP_SET_ENTRY(PRESET_SOUTH_OF_INTRO), MAP_NAV_ENTRY(0,0), 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0}
};

/**
 * Constructs a PresetManager instance and initializes the configuration.
 */
PresetManager::PresetManager()
{
    currentPresetIndex = 0;
    currentGroupIndex = 0;

    std::memcpy(configuration.presetMap, defaultConfig, sizeof(defaultConfig));
}

/**
 * Applies the current preset based on the configuration and updates the current group and preset indices if necessary. 
 * If the current preset is a navigation entry, it will update the group and preset indices accordingly before applying the preset.
 * If a navigation entry leads to another navigation entry, no further navigation will be performed and the preset will not be applied.
 * @return BLEMessage indicating the result of applying the current preset.
 */
BLEMessage PresetManager::apply()
{
    PresetMapEntry entry = configuration.presetMap[currentGroupIndex][currentPresetIndex];

    if (GET_ACTION(entry) == PRESET_ACTION_NAV)
    {
        currentGroupIndex = GET_GROUP_ARG(entry);
        currentPresetIndex = GET_PRESET_ARG(entry);

        entry = configuration.presetMap[currentGroupIndex][currentPresetIndex];
    }

    if (GET_ACTION(entry) == PRESET_ACTION_SET)
        return setPreset(GET_ARG(entry));

    BLEMessage msg = {nullptr, 0};
    return msg;
}

/**
 *
 */
BLEMessage PresetManager::nextPreset()
{
    std::lock_guard<std::mutex> guard(mutex);
    currentPresetIndex = (currentPresetIndex + 1) % NUM_PRESETS_PER_GROUP;

    return apply();
}

/**
 *
 */
BLEMessage PresetManager::nextGroup()
{
    std::lock_guard<std::mutex> guard(mutex);
    currentPresetIndex = 0;

    if (currentGroupIndex >= MAX_NORMAL_GROUPS)
        currentGroupIndex = 0;
    else
        currentGroupIndex = (currentGroupIndex + 1) % MAX_NORMAL_GROUPS;

    return apply();
}

/**
 *
 */
BLEMessage PresetManager::extGroup(uint16_t index)
{
    std::lock_guard<std::mutex> guard(mutex);
    currentPresetIndex = 0;
    currentGroupIndex = MAX_NORMAL_GROUPS + index;
    return apply();
}

/**
 *
 */
BLEMessage PresetManager::setPreset(uint16_t arg)
{
    if (arg >= sizeof(Presets) / sizeof(Presets[0]))
    {
        ESP_LOGE(TAG, "Invalid preset index: %u", arg);
        return {nullptr, 0};
    }

    ESP_LOGI(TAG, "Applying preset: Group %d, Preset %d, Arg %d\r\n", currentGroupIndex, currentPresetIndex, arg);

    BLEMessage msg = {Presets[arg], sizeof(Presets[arg])};
    return msg;
}

/**
 * Checks if the given configuration is valid according to the current version and group/preset constraints.
 * @param value The configuration to validate.
 * @return True if the configuration is valid, false otherwise.
 */
bool PresetManager::valid(const Configuration &value) const
{
    for (uint8_t group = 0; group < MAX_GROUPS; ++group)
    {
        for (uint8_t preset = 0; preset < NUM_PRESETS_PER_GROUP; ++preset)
        {
            const auto &entry = value.presetMap[group][preset];

            if (GET_ACTION(entry) == PRESET_ACTION_SET && (GET_ARG(entry) >= (sizeof(Presets) / sizeof(Presets[0]))))
            {
                ESP_LOGE(TAG, "Invalid GP-5 preset number: %d", GET_ARG(entry));
                return false;
            }
        }
    }

    return true;
}

/**
 * Sets up the preset manager by initializing NVS and loading the saved configuration if available.
 */
bool PresetManager::setup()
{
    std::lock_guard<std::mutex> guard(mutex);

    const esp_err_t init = nvs_flash_init();
    if (init != ESP_OK)
    {
        ESP_LOGE(TAG, "NVS initialization failed: %s", esp_err_to_name(init));
        return false;
    }

    nvs_handle_t handle;
    esp_err_t result = nvs_open("gp5-presets", NVS_READWRITE, &handle);

    if (result == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGI(TAG, "Preset storage not found, using default configuration.");
        return true;
    }

    if (result != ESP_OK)
    {
        ESP_LOGE(TAG, "Cannot open preset storage: %s", esp_err_to_name(result));
        return false;
    }

    Configuration saved{};
    size_t size = sizeof(saved);

    result = nvs_get_blob(handle, "config", nullptr, &size);

    if (result == ESP_OK)
    {
        if (size != sizeof(saved))
        {
            ESP_LOGE(TAG, "Incomplete preset configuration: size = %zu, should be %zu", size, sizeof(saved));

            result = nvs_erase_key(handle, "config");

            if (result != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to erase incomplete preset configuration: %s", esp_err_to_name(result));
                nvs_close(handle);
                return false;
            }

            result = nvs_commit(handle);

            if (result != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to commit erased preset configuration: %s", esp_err_to_name(result));
            }

            nvs_close(handle);
            return result == ESP_OK;
        }

        result = nvs_get_blob(handle, "config", &saved, &size);
    }

    nvs_close(handle);

    if (result == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGI(TAG, "Preset configuration not found, using default configuration.");
        return true;
    }

    if (result != ESP_OK || size != sizeof(saved) || !valid(saved))
    {
        ESP_LOGE(TAG, "Invalid saved preset configuration: %s", esp_err_to_name(result));
        return false;
    }

    configuration = saved;
    return true;
}

/**
 * Persists the given configuration to NVS if it is valid.
 */
bool PresetManager::persist(const Configuration &value)
{
    if (!valid(value))
        return false;

    nvs_handle_t handle;
    esp_err_t result = nvs_open("gp5-presets", NVS_READWRITE, &handle);

    if (result == ESP_OK)
    {
        result = nvs_set_blob(handle, "config", &value, sizeof(value));

        if (result == ESP_OK)
            result = nvs_commit(handle);

        nvs_close(handle);
    }

    if (result != ESP_OK)
    {
        ESP_LOGE(TAG, "Cannot save presets: %s", esp_err_to_name(result));
    }

    return result == ESP_OK;
}

/**
 * Handle incoming API messages over BLE.
 * Responds on the dedicated configuration characteristic, including errors for rejected requests.
 */
bool PresetManager::handleApi(const uint8_t *data, size_t length, BleEndpoint &reply)
{
    constexpr size_t configurationSize = MAX_GROUPS * NUM_PRESETS_PER_GROUP;
    uint8_t response[4 + configurationSize] = {1, 0, 0, 1};
    size_t responseLength = 4;
    uint8_t error = 1;

    std::lock_guard<std::mutex> guard(mutex);

    if (data != nullptr && length >= 3)
    {
        const uint8_t command = data[1];
        response[1] = command;
        response[2] = data[2];
        const size_t args = length - 3;
        const uint8_t *p = data + 3;
        if (data[0] == 1 && command == 1 && args == 0)
        {
            for (size_t group = 0; group < MAX_GROUPS; ++group)
            {
                for (size_t preset = 0; preset < NUM_PRESETS_PER_GROUP; ++preset)
                {
                    response[responseLength++] = configuration.presetMap[group][preset];
                }
            }
            error = 0;
        }
        else if (data[0] == 1 && command == 4 && args == configurationSize)
        {
            Configuration value{};

            for (size_t group = 0; group < MAX_GROUPS; ++group)
            {
                for (size_t preset = 0; preset < NUM_PRESETS_PER_GROUP; ++preset)
                {
                    value.presetMap[group][preset] = *p++;
                }
            }

            if (!valid(value))
                error = 2;
            else if (!persist(value))
                error = 3;
            else
            {
                configuration = value;
                currentGroupIndex = currentPresetIndex = 0;
                error = 0;
            }
        }
    }

    if (error != 0)
    {
        ESP_LOGE(TAG, "Preset API request rejected: %u", error);
    }
    response[3] = error;

    if (!reply.Send(response, responseLength))
    {
        ESP_LOGE(TAG, "Cannot acknowledge preset API request.");
    }

    return true;
}
