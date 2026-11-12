#pragma once

#ifdef ESP_PLATFORM

#include <string>

#include "kaffee/storage.hpp"

namespace kaffee {

// Settings (including admin/AP/STA secrets) in the NVS partition. Secrets never
// leave the device: backups are produced from Settings with secrets excluded.
class NvsSettingsStore final : public ISettingsStore {
  public:
    explicit NvsSettingsStore(std::string nvs_namespace = "kaffee_settings");

    Result<Settings> load() override;
    Result<Unit> save(const Settings& settings) override;
    Result<Unit> reset() override;
    Result<std::string> load_setup_password() override;
    Result<Unit> save_setup_password(const std::string& password) override;
    Result<Unit> clear_setup_password() override;

  private:
    std::string ns_;
};

}  // namespace kaffee

#endif  // ESP_PLATFORM
