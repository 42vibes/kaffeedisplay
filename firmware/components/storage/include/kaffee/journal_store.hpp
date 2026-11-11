#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

#include "kaffee/storage.hpp"

namespace kaffee {

// Portable append-only journal + A/B snapshots. The filesystem specifics (mkdir,
// rename, capacity, directory fsync) are supplied by a platform backend.
//
// Layout under `root`:
//   journal.jsonl        one JSON event per line, fsynced after every append
//   snapshot.a.json      {version,last_seq,head_hash,events[]}
//   snapshot.b.json      alternate snapshot slot
//   journal.jsonl.corrupt quarantined corrupt tail from the last load
class JournalEventStore : public IEventStore {
  public:
    explicit JournalEventStore(std::string root);
    ~JournalEventStore() override;

    Result<Unit> mount() override;
    Result<uint32_t> load(Ledger& ledger) override;
    Result<Unit> append(const Event& event) override;
    Result<Unit> write_snapshot(const Ledger& ledger) override;
    Result<Unit> compact(const Ledger& ledger) override;
    Result<Unit> factory_reset() override;
    StorageInfo info() const override;

    const std::string& root() const {
        return root_;
    }
    std::string journal_path() const;
    std::string snapshot_path(char slot) const;
    std::string corrupt_path() const;
    bool last_load_quarantined() const {
        return quarantined_;
    }
    bool degraded() const {
        return degraded_;
    }

  protected:
    virtual Result<Unit> ensure_root() = 0;
    virtual Result<Unit> remove_file(const std::string& path) = 0;
    virtual Result<Unit> rename_file(const std::string& from, const std::string& to) = 0;
    virtual Result<Unit> sync_dir() = 0;
    virtual uint64_t total_bytes() const = 0;

  private:
    Result<Unit> ensure_mounted();
    Result<Unit> open_journal_append();
    void close_journal();
    Result<Unit> write_file_atomic(const std::string& path, const std::string& contents);
    Result<uint32_t> replay_journal(Ledger& ledger);
    Result<Unit> quarantine_from(const std::string& journal, size_t offset);

    std::string root_;
    std::FILE* journal_ = nullptr;
    char active_slot_ = 'b';  // slot of the newest valid snapshot
    bool mounted_ = false;
    bool degraded_ = false;
    bool quarantined_ = false;
    uint32_t event_count_ = 0;
};

}  // namespace kaffee
