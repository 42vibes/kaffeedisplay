#pragma once

#include <cstdint>
#include <string>

#include "kaffee/domain.hpp"
#include "kaffee/settings.hpp"

namespace kaffee {

struct StorageInfo {
    uint64_t used_bytes = 0;
    uint64_t total_bytes = 0;
    uint32_t event_count = 0;
    // Events currently stored in the journal (not yet folded into a snapshot).
    // Used to schedule compaction.
    uint32_t journal_events = 0;
    bool mounted = false;
    bool degraded = false;
};

// Persistence boundary. Implemented by LittleFS on the device and by a
// file-backed store on the host. All methods are called through KaffeeService
// with its write mutex held.
class IEventStore {
  public:
    virtual ~IEventStore() = default;
    virtual Result<Unit> mount() = 0;
    // Replays snapshot + journal into the ledger. Returns the number of events
    // applied and whether the journal tail had to be quarantined.
    virtual Result<uint32_t> load(Ledger& ledger) = 0;
    virtual Result<Unit> append(const Event& event) = 0;
    virtual Result<Unit> write_snapshot(const Ledger& ledger) = 0;
    virtual Result<Unit> compact(const Ledger& ledger) = 0;
    virtual Result<Unit> factory_reset() = 0;
    virtual StorageInfo info() const = 0;
};

class ISettingsStore {
  public:
    virtual ~ISettingsStore() = default;
    virtual Result<Settings> load() = 0;
    virtual Result<Unit> save(const Settings& settings) = 0;
    virtual Result<Unit> reset() = 0;
};

}  // namespace kaffee
