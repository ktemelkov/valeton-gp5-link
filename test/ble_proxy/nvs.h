#pragma once
#include <cstdint>
#include <cstring>
#include <vector>

using esp_err_t = int;
using nvs_handle_t = int;
constexpr int ESP_OK = 0, ESP_ERR_NVS_NOT_FOUND = 1, NVS_READONLY = 0, NVS_READWRITE = 1;
inline std::vector<uint8_t> mockStored, mockUncommitted;
inline bool mockStorageFailure = false;
inline const char *esp_err_to_name(esp_err_t) { return "mock NVS error"; }
inline esp_err_t nvs_open(const char*, int mode, nvs_handle_t *handle)
{
    if (mockStorageFailure) return 2;
    if (mode == NVS_READONLY && mockStored.empty()) return ESP_ERR_NVS_NOT_FOUND;
    *handle = 1;
    return ESP_OK;
}
inline esp_err_t nvs_get_blob(nvs_handle_t, const char*, void *data, size_t *size)
{
    if (mockStored.empty()) return ESP_ERR_NVS_NOT_FOUND;
    if (data == nullptr) { *size = mockStored.size(); return ESP_OK; }
    if (*size < mockStored.size()) return 2;
    std::memcpy(data, mockStored.data(), mockStored.size());
    *size = mockStored.size();
    return ESP_OK;
}
inline esp_err_t nvs_set_blob(nvs_handle_t, const char*, const void *data, size_t size)
{
    const auto *bytes = static_cast<const uint8_t*>(data);
    mockUncommitted.assign(bytes, bytes + size);
    return ESP_OK;
}
inline esp_err_t nvs_commit(nvs_handle_t)
{
    if (mockStorageFailure) return 2;
    mockStored = mockUncommitted;
    return ESP_OK;
}
inline void nvs_close(nvs_handle_t) {}
inline esp_err_t nvs_erase_key(nvs_handle_t, const char*)
{
    if (mockStorageFailure) return 2;
    mockUncommitted.clear();
    return ESP_OK;
}
