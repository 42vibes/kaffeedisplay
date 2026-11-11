#pragma once

#ifdef ESP_PLATFORM

#include <string>

#include "kaffee/journal_store.hpp"

namespace kaffee {

// LittleFS backend for the journal store. The partition must already be
// mounted (see esp_vfs_littlefs_register in main).
class LittleFsEventStore final : public JournalEventStore {
  public:
    explicit LittleFsEventStore(std::string mount_point = "/littlefs");

    Result<Unit> mount() override;
    Result<Unit> factory_reset() override;

  protected:
    Result<Unit> ensure_root() override;
    Result<Unit> remove_file(const std::string& path) override;
    Result<Unit> rename_file(const std::string& from, const std::string& to) override;
    Result<Unit> sync_dir() override;
    uint64_t total_bytes() const override;
};

}  // namespace kaffee

#endif  // ESP_PLATFORM
