#pragma once

#include <string>

#include "kaffee/journal_store.hpp"
#include "kaffee/storage.hpp"

namespace kaffee {

// Host implementation backed by a normal directory.
class FileEventStore final : public JournalEventStore {
  public:
    explicit FileEventStore(std::string directory);

  protected:
    Result<Unit> ensure_root() override;
    Result<Unit> remove_file(const std::string& path) override;
    Result<Unit> rename_file(const std::string& from, const std::string& to) override;
    Result<Unit> sync_dir() override;
    uint64_t total_bytes() const override;
};

// Simple host settings store: one JSON file, secrets included.
class JsonFileSettingsStore final : public ISettingsStore {
  public:
    explicit JsonFileSettingsStore(std::string path);

    Result<Settings> load() override;
    Result<Unit> save(const Settings& settings) override;
    Result<Unit> reset() override;
    Result<std::string> load_setup_password() override;
    Result<Unit> save_setup_password(const std::string& password) override;
    Result<Unit> clear_setup_password() override;

    const std::string& path() const {
        return path_;
    }

  private:
    std::string path_;
};

// In-memory settings store for tests and the simulator.
class MemorySettingsStore final : public ISettingsStore {
  public:
    MemorySettingsStore() = default;
    explicit MemorySettingsStore(Settings settings);

    Result<Settings> load() override;
    Result<Unit> save(const Settings& settings) override;
    Result<Unit> reset() override;
    Result<std::string> load_setup_password() override;
    Result<Unit> save_setup_password(const std::string& password) override;
    Result<Unit> clear_setup_password() override;

    const Settings& stored() const {
        return settings_;
    }
    const std::string& stored_setup_password() const {
        return setup_password_;
    }

  private:
    Settings settings_;
    std::string setup_password_;
};

}  // namespace kaffee
