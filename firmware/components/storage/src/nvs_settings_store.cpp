#include "kaffee/nvs_settings_store.hpp"

#ifdef ESP_PLATFORM

#include <string>
#include <utility>

#include <esp_log.h>
#include <nvs.h>

#include "kaffee/json.hpp"

namespace kaffee {
namespace {

constexpr const char* kTag = "kaffee-settings";
constexpr const char* kKey = "json";
constexpr const char* kSetupPasswordKey = "setup_password";

Result<Unit> nvs_fail(const std::string& what, esp_err_t err) {
    return Result<Unit>::fail("settings", what + ": " + esp_err_to_name(err));
}

}  // namespace

NvsSettingsStore::NvsSettingsStore(std::string nvs_namespace) : ns_(std::move(nvs_namespace)) {}

Result<Settings> NvsSettingsStore::load() {
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(ns_.c_str(), NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return Result<Settings>::ok(Settings{});
    if (err != ESP_OK) return Result<Settings>::fail("settings", esp_err_to_name(err));

    size_t length = 0;
    err = nvs_get_blob(handle, kKey, nullptr, &length);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return Result<Settings>::ok(Settings{});
    }
    if (err != ESP_OK) {
        nvs_close(handle);
        return Result<Settings>::fail("settings", esp_err_to_name(err));
    }

    std::string blob(length, '\0');
    err = nvs_get_blob(handle, kKey, blob.data(), &length);
    nvs_close(handle);
    if (err != ESP_OK) return Result<Settings>::fail("settings", esp_err_to_name(err));

    try {
        return Result<Settings>::ok(settings_from_json(nlohmann::json::parse(blob)));
    } catch (const std::exception& e) {
        ESP_LOGE(kTag, "corrupt settings blob: %s", e.what());
        return Result<Settings>::fail("settings", e.what());
    }
}

Result<Unit> NvsSettingsStore::save(const Settings& settings) {
    const std::string blob = settings_to_json(settings, true).dump();

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(ns_.c_str(), NVS_READWRITE, &handle);
    if (err != ESP_OK) return nvs_fail("open namespace", err);

    err = nvs_set_blob(handle, kKey, blob.data(), blob.size());
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "save failed: %s", esp_err_to_name(err));
        return nvs_fail("save", err);
    }
    return Result<Unit>::ok(Unit{});
}

Result<Unit> NvsSettingsStore::reset() {
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(ns_.c_str(), NVS_READWRITE, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return Result<Unit>::ok(Unit{});
    if (err != ESP_OK) return nvs_fail("open namespace", err);

    err = nvs_erase_key(handle, kKey);
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    if (err == ESP_OK) {
        err = nvs_erase_key(handle, kSetupPasswordKey);
        if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    }
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err != ESP_OK) return nvs_fail("reset", err);
    return Result<Unit>::ok(Unit{});
}

Result<std::string> NvsSettingsStore::load_setup_password() {
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(ns_.c_str(), NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return Result<std::string>::ok(std::string());
    if (err != ESP_OK) return Result<std::string>::fail("settings", esp_err_to_name(err));

    size_t length = 0;
    err = nvs_get_str(handle, kSetupPasswordKey, nullptr, &length);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return Result<std::string>::ok(std::string());
    }
    if (err != ESP_OK) {
        nvs_close(handle);
        return Result<std::string>::fail("settings", esp_err_to_name(err));
    }

    std::string value(length, '\0');
    err = nvs_get_str(handle, kSetupPasswordKey, value.data(), &length);
    nvs_close(handle);
    if (err != ESP_OK) return Result<std::string>::fail("settings", esp_err_to_name(err));
    if (!value.empty() && value.back() == '\0') value.pop_back();
    return Result<std::string>::ok(std::move(value));
}

Result<Unit> NvsSettingsStore::save_setup_password(const std::string& password) {
    if (password.empty()) return clear_setup_password();
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(ns_.c_str(), NVS_READWRITE, &handle);
    if (err != ESP_OK) return nvs_fail("open namespace", err);
    err = nvs_set_str(handle, kSetupPasswordKey, password.c_str());
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err != ESP_OK) return nvs_fail("save setup password", err);
    return Result<Unit>::ok(Unit{});
}

Result<Unit> NvsSettingsStore::clear_setup_password() {
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(ns_.c_str(), NVS_READWRITE, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return Result<Unit>::ok(Unit{});
    if (err != ESP_OK) return nvs_fail("open namespace", err);
    err = nvs_erase_key(handle, kSetupPasswordKey);
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err != ESP_OK) return nvs_fail("clear setup password", err);
    return Result<Unit>::ok(Unit{});
}

}  // namespace kaffee

#endif  // ESP_PLATFORM
