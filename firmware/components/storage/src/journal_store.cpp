#include "kaffee/journal_store.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <utility>
#include <vector>

#include <unistd.h>

#include "kaffee/json.hpp"
#include "kaffee/sha256.hpp"

namespace kaffee {
namespace {

Result<Unit> storage_fail(std::string message) {
    return Result<Unit>::fail("storage", std::move(message));
}

bool read_file(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

bool file_exists(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) return false;
    std::fclose(f);
    return true;
}

uint64_t file_size(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) return 0;
    if (std::fseek(f, 0, SEEK_END) != 0) {
        std::fclose(f);
        return 0;
    }
    const long size = std::ftell(f);
    std::fclose(f);
    return size < 0 ? 0 : static_cast<uint64_t>(size);
}

bool flush_and_sync(std::FILE* file) {
    if (std::fflush(file) != 0) return false;
    const int fd = fileno(file);
    if (fd < 0) return false;
    return ::fsync(fd) == 0;
}

struct SnapshotData {
    uint64_t last_seq = 0;
    std::string head_hash;
    std::vector<Event> events;
};

Result<SnapshotData> parse_snapshot(const std::string& text) {
    SnapshotData data;
    try {
        const auto json = nlohmann::json::parse(text);
        if (!json.is_object()) return Result<SnapshotData>::fail("snapshot", "not an object");
        if (json.value("version", 0) != 1)
            return Result<SnapshotData>::fail("snapshot", "unsupported snapshot version");
        if (!json.contains("events") || !json["events"].is_array())
            return Result<SnapshotData>::fail("snapshot", "missing events");
        for (const auto& item : json["events"]) {
            data.events.push_back(event_from_json(item));
        }
        data.last_seq = json.value("last_seq", static_cast<uint64_t>(0));
        data.head_hash = json.value("head_hash", std::string{});
        if (data.events.empty()) {
            if (data.last_seq != 0 || !data.head_hash.empty())
                return Result<SnapshotData>::fail("snapshot", "inconsistent empty snapshot");
        } else {
            const Event& last = data.events.back();
            if (last.seq != data.last_seq)
                return Result<SnapshotData>::fail("snapshot", "last_seq mismatch");
            if (last.hash != data.head_hash)
                return Result<SnapshotData>::fail("snapshot", "head_hash mismatch");
        }
    } catch (const std::exception& e) {
        return Result<SnapshotData>::fail("snapshot", e.what());
    }
    return Result<SnapshotData>::ok(std::move(data));
}

}  // namespace

JournalEventStore::JournalEventStore(std::string root) : root_(std::move(root)) {}

JournalEventStore::~JournalEventStore() {
    close_journal();
}

std::string JournalEventStore::journal_path() const {
    return root_ + "/journal.jsonl";
}

std::string JournalEventStore::snapshot_path(char slot) const {
    return root_ + "/snapshot." + slot + ".json";
}

std::string JournalEventStore::corrupt_path() const {
    return journal_path() + ".corrupt";
}

Result<Unit> JournalEventStore::mount() {
    if (mounted_) return Result<Unit>::ok(Unit{});
    return ensure_mounted();
}

Result<Unit> JournalEventStore::ensure_mounted() {
    auto root = ensure_root();
    if (!root) return root;
    auto opened = open_journal_append();
    if (!opened) return opened;
    mounted_ = true;
    return Result<Unit>::ok(Unit{});
}

Result<Unit> JournalEventStore::open_journal_append() {
    close_journal();
    journal_ = std::fopen(journal_path().c_str(), "ab");
    if (journal_ == nullptr) {
        return storage_fail("cannot open journal: " + std::string(std::strerror(errno)));
    }
    return Result<Unit>::ok(Unit{});
}

void JournalEventStore::close_journal() {
    if (journal_ != nullptr) {
        std::fflush(journal_);
        std::fclose(journal_);
        journal_ = nullptr;
    }
}

Result<Unit> JournalEventStore::write_file_atomic(const std::string& path,
                                                  const std::string& contents) {
    const std::string tmp = path + ".tmp";
    std::FILE* file = std::fopen(tmp.c_str(), "wb");
    if (file == nullptr) return storage_fail("cannot write " + tmp);
    const bool written = std::fwrite(contents.data(), 1, contents.size(), file) == contents.size();
    const bool synced = written && flush_and_sync(file);
    std::fclose(file);
    if (!written || !synced) {
        remove_file(tmp);
        return storage_fail("cannot flush " + tmp);
    }
    auto renamed = rename_file(tmp, path);
    if (!renamed) {
        remove_file(tmp);
        return renamed;
    }
    return sync_dir();
}

Result<uint32_t> JournalEventStore::load(Ledger& ledger) {
    close_journal();
    auto mounted = ensure_mounted();
    if (!mounted) return Result<uint32_t>::fail(mounted.error->code, mounted.error->message);

    ledger.reset();
    degraded_ = false;
    quarantined_ = false;
    active_slot_ = 'b';

    // Pick the newest valid snapshot; fall back to the other slot if it is
    // corrupt. A failed snapshot leaves `degraded` set for diagnostics.
    std::vector<std::pair<char, SnapshotData>> candidates;
    for (char slot : {'a', 'b'}) {
        std::string text;
        if (!read_file(snapshot_path(slot), text)) continue;
        auto parsed = parse_snapshot(text);
        if (!parsed) {
            degraded_ = true;
            continue;
        }
        candidates.emplace_back(slot, std::move(*parsed.value));
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const auto& a, const auto& b) { return a.second.last_seq > b.second.last_seq; });

    bool snapshot_loaded = false;
    uint32_t snapshot_events = 0;
    for (auto& candidate : candidates) {
        ledger.reset();
        bool applied = true;
        for (const Event& event : candidate.second.events) {
            if (!ledger.apply(event)) {
                applied = false;
                break;
            }
        }
        if (applied) {
            active_slot_ = candidate.first;
            snapshot_loaded = true;
            snapshot_events = static_cast<uint32_t>(candidate.second.events.size());
            break;
        }
        degraded_ = true;
    }
    if (!snapshot_loaded) {
        ledger.reset();
        active_slot_ = 'b';
        if (!candidates.empty()) degraded_ = true;
    }

    auto replayed = replay_journal(ledger);
    if (!replayed) return Result<uint32_t>::fail(replayed.error->code, replayed.error->message);

    event_count_ = static_cast<uint32_t>(ledger.events().size());
    auto reopened = open_journal_append();
    if (!reopened) return Result<uint32_t>::fail(reopened.error->code, reopened.error->message);
    return Result<uint32_t>::ok(snapshot_events + *replayed.value);
}

Result<uint32_t> JournalEventStore::replay_journal(Ledger& ledger) {
    std::string data;
    if (!read_file(journal_path(), data)) {
        return Result<uint32_t>::ok(0);
    }

    uint32_t applied = 0;
    size_t pos = 0;
    while (pos < data.size()) {
        const size_t newline = data.find('\n', pos);
        if (newline == std::string::npos) {
            // A torn append without its terminating newline.
            auto quarantined = quarantine_from(data, pos);
            if (!quarantined)
                return Result<uint32_t>::fail(quarantined.error->code, quarantined.error->message);
            break;
        }
        std::string line = data.substr(pos, newline - pos);
        const size_t next = newline + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) {
            pos = next;
            continue;
        }

        bool bad = false;
        try {
            const auto json = nlohmann::json::parse(line);
            const Event event = event_from_json(json);
            if (event.seq < ledger.next_seq()) {
                // Already covered by the snapshot (journal was not rotated yet).
            } else if (event.seq != ledger.next_seq()) {
                bad = true;
            } else {
                auto result = ledger.apply(event);
                if (!result) {
                    bad = true;
                } else {
                    ++applied;
                }
            }
        } catch (const std::exception&) {
            bad = true;
        }

        if (bad) {
            auto quarantined = quarantine_from(data, pos);
            if (!quarantined)
                return Result<uint32_t>::fail(quarantined.error->code, quarantined.error->message);
            break;
        }
        pos = next;
    }
    return Result<uint32_t>::ok(applied);
}

Result<Unit> JournalEventStore::quarantine_from(const std::string& journal, size_t offset) {
    quarantined_ = true;
    degraded_ = true;
    const std::string tail = journal.substr(offset);
    auto corrupt = write_file_atomic(corrupt_path(), tail);
    if (!corrupt) return corrupt;
    return write_file_atomic(journal_path(), journal.substr(0, offset));
}

Result<Unit> JournalEventStore::append(const Event& event) {
    if (!mounted_) {
        auto mounted = ensure_mounted();
        if (!mounted) return mounted;
    }
    if (journal_ == nullptr) {
        auto opened = open_journal_append();
        if (!opened) return opened;
    }

    std::string line = event_to_json(event).dump();
    line.push_back('\n');
    const size_t written = std::fwrite(line.data(), 1, line.size(), journal_);
    if (written != line.size()) return storage_fail("short journal write");
    if (!flush_and_sync(journal_)) return storage_fail("journal fsync failed");
    ++event_count_;
    return Result<Unit>::ok(Unit{});
}

Result<Unit> JournalEventStore::write_snapshot(const Ledger& ledger) {
    if (!mounted_) {
        auto mounted = ensure_mounted();
        if (!mounted) return mounted;
    }

    nlohmann::json events = nlohmann::json::array();
    for (const Event& event : ledger.events()) {
        events.push_back(event_to_json(event));
    }
    const nlohmann::json snapshot{
        {"version", 1},
        {"last_seq",
         ledger.events().empty() ? static_cast<uint64_t>(0) : ledger.events().back().seq},
        {"head_hash", ledger.head_hash()},
        {"events", std::move(events)}};

    const char slot = active_slot_ == 'a' ? 'b' : 'a';
    auto written = write_file_atomic(snapshot_path(slot), snapshot.dump());
    if (!written) return written;
    active_slot_ = slot;
    return Result<Unit>::ok(Unit{});
}

Result<Unit> JournalEventStore::compact(const Ledger& ledger) {
    auto snapshot = write_snapshot(ledger);
    if (!snapshot) return snapshot;

    close_journal();
    auto truncated = write_file_atomic(journal_path(), std::string{});
    if (!truncated) return truncated;
    auto reopened = open_journal_append();
    if (!reopened) return reopened;
    event_count_ = static_cast<uint32_t>(ledger.events().size());
    return Result<Unit>::ok(Unit{});
}

Result<Unit> JournalEventStore::factory_reset() {
    close_journal();
    const std::string files[] = {
        journal_path(),     journal_path() + ".tmp",     snapshot_path('a'),
        snapshot_path('b'), snapshot_path('a') + ".tmp", snapshot_path('b') + ".tmp",
        corrupt_path()};
    for (const std::string& file : files) {
        if (!file_exists(file)) continue;
        auto removed = remove_file(file);
        if (!removed) return removed;
    }
    active_slot_ = 'b';
    degraded_ = false;
    quarantined_ = false;
    event_count_ = 0;
    auto opened = open_journal_append();
    if (!opened) return opened;
    sync_dir();
    return Result<Unit>::ok(Unit{});
}

StorageInfo JournalEventStore::info() const {
    StorageInfo out;
    out.mounted = mounted_;
    out.degraded = degraded_ || quarantined_;
    out.event_count = event_count_;
    out.used_bytes = file_size(journal_path()) + file_size(snapshot_path('a')) +
                     file_size(snapshot_path('b')) + file_size(corrupt_path());
    out.total_bytes = total_bytes();
    return out;
}

}  // namespace kaffee
