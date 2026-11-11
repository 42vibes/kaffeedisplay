#include <doctest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <unistd.h>

#include "kaffee/file_store.hpp"
#include "kaffee/json.hpp"

using namespace kaffee;
namespace fs = std::filesystem;

namespace {

constexpr Timestamp kNow = 1'700'000'000;

std::atomic<int> g_temp_counter{0};

struct TempDir {
    fs::path path;

    TempDir() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = fs::temp_directory_path() /
               ("kaffee-" + std::to_string(::getpid()) + "-" + std::to_string(stamp) + "-" +
                std::to_string(++g_temp_counter));
        std::error_code ec;
        fs::remove_all(path, ec);
        fs::create_directories(path);
    }

    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

struct Scenario {
    std::vector<Event> events;
    std::string user_id;
    std::string product_id;
};

Scenario make_scenario(int drinks = 2) {
    int counter = 0;
    Ledger ledger([&counter](std::string_view prefix) {
        return std::string(prefix) + "_" + std::to_string(++counter);
    });
    Scenario scenario;
    ledger.set_commit_hook(CommitHook{[&scenario](const Event& event) {
        scenario.events.push_back(event);
        return Result<Unit>::ok(Unit{});
    }});

    auto user = ledger.create_user("Ada", "#D4A373", "", kNow, TimeSource::Synced);
    REQUIRE(static_cast<bool>(user));
    scenario.user_id = user.value->user_id;

    auto product = ledger.create_product("Kaffee", "Kaffee", "cup", "#D4A373", 50, 0, "", kNow,
                                         TimeSource::Synced);
    REQUIRE(static_cast<bool>(product));
    scenario.product_id = product.value->product_id;

    REQUIRE(static_cast<bool>(
        ledger.topup(scenario.user_id, 1000, "cash", "", kNow, TimeSource::Synced)));
    for (int i = 0; i < drinks; ++i) {
        REQUIRE(static_cast<bool>(
            ledger.purchase(scenario.user_id, scenario.product_id, "", kNow, TimeSource::Synced)));
    }
    return scenario;
}

std::string join_event_lines(const std::vector<Event>& events) {
    std::string out;
    for (const Event& event : events) {
        out += event_to_json(event).dump();
        out.push_back('\n');
    }
    return out;
}

}  // namespace

TEST_CASE("file store replays journal into the ledger") {
    TempDir dir;
    const Scenario scenario = make_scenario(3);

    FileEventStore store((dir.path / "store").string());
    REQUIRE(static_cast<bool>(store.mount()));
    for (const Event& event : scenario.events) {
        REQUIRE(static_cast<bool>(store.append(event)));
    }

    int counter = 0;
    Ledger ledger([&counter](std::string_view prefix) {
        return std::string(prefix) + "_" + std::to_string(++counter);
    });
    auto loaded = store.load(ledger);
    REQUIRE(static_cast<bool>(loaded));
    CHECK(*loaded.value == scenario.events.size());
    CHECK(ledger.events().size() == scenario.events.size());
    CHECK(ledger.head_hash() == scenario.events.back().hash);
    CHECK(ledger.balance(scenario.user_id) == 1000 - 3 * 50);

    const StorageInfo info = store.info();
    CHECK(info.mounted);
    CHECK_FALSE(info.degraded);
    CHECK(info.event_count == scenario.events.size());
    CHECK(info.used_bytes > 0);
    CHECK(info.total_bytes > 0);
}

TEST_CASE("journal survives truncation at every byte") {
    TempDir dir;
    const Scenario scenario = make_scenario(1);
    const std::string journal = join_event_lines(scenario.events);
    REQUIRE(!journal.empty());
    REQUIRE(journal.back() == '\n');

    for (size_t length = 0; length <= journal.size(); ++length) {
        const std::string prefix = journal.substr(0, length);
        TempDir case_dir;

        {
            const fs::path root = case_dir.path / "store";
            fs::create_directories(root);
            std::ofstream out(root / "journal.jsonl", std::ios::binary);
            out.write(prefix.data(), static_cast<std::streamsize>(prefix.size()));
        }

        FileEventStore store((case_dir.path / "store").string());
        REQUIRE(static_cast<bool>(store.mount()));
        int counter = 0;
        Ledger ledger([&counter](std::string_view prefix_id) {
            return std::string(prefix_id) + "_" + std::to_string(++counter);
        });
        auto loaded = store.load(ledger);
        REQUIRE_MESSAGE(static_cast<bool>(loaded), "failed at prefix length ", length, ": ",
                        loaded.error ? loaded.error->message : "");

        const size_t expected = static_cast<size_t>(std::count(prefix.begin(), prefix.end(), '\n'));
        CHECK(ledger.events().size() == expected);
        CHECK(*loaded.value == expected);

        const bool partial_tail = !prefix.empty() && prefix.back() != '\n';
        CHECK(store.last_load_quarantined() == partial_tail);
        if (expected > 0) {
            CHECK(ledger.head_hash() == scenario.events[expected - 1].hash);
        }
    }
}

TEST_CASE("corrupt journal tail is quarantined at the first bad line") {
    TempDir dir;
    const Scenario scenario = make_scenario(2);
    REQUIRE(scenario.events.size() >= 4);

    std::string contents;
    for (size_t i = 0; i < 2; ++i) {
        contents += event_to_json(scenario.events[i]).dump();
        contents.push_back('\n');
    }
    auto tampered = event_to_json(scenario.events[2]);
    tampered["amount_cents"] = 999999;  // hash now does not match the payload
    const std::string tampered_line = tampered.dump() + "\n";
    contents += tampered_line;

    const fs::path root = dir.path / "store";
    fs::create_directories(root);
    {
        std::ofstream out(root / "journal.jsonl", std::ios::binary);
        out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    }

    FileEventStore store(root.string());
    REQUIRE(static_cast<bool>(store.mount()));
    Ledger ledger;
    auto loaded = store.load(ledger);
    REQUIRE(static_cast<bool>(loaded));
    CHECK(*loaded.value == 2);
    CHECK(ledger.events().size() == 2);
    CHECK(store.last_load_quarantined());
    CHECK(store.degraded());
    CHECK(fs::file_size(root / "journal.jsonl") == contents.size() - tampered_line.size());
    CHECK(fs::exists(root / "journal.jsonl.corrupt"));
    CHECK(fs::file_size(root / "journal.jsonl.corrupt") == tampered_line.size());
}

TEST_CASE("snapshots use A/B slots and compaction truncates the journal") {
    TempDir dir;
    const Scenario scenario = make_scenario(4);
    FileEventStore store((dir.path / "store").string());
    REQUIRE(static_cast<bool>(store.mount()));
    for (const Event& event : scenario.events) {
        REQUIRE(static_cast<bool>(store.append(event)));
    }

    Ledger ledger;
    REQUIRE(static_cast<bool>(store.load(ledger)));
    CHECK(ledger.events().size() == scenario.events.size());

    REQUIRE(static_cast<bool>(store.write_snapshot(ledger)));
    REQUIRE(static_cast<bool>(store.write_snapshot(ledger)));
    CHECK(fs::exists(store.snapshot_path('a')));
    CHECK(fs::exists(store.snapshot_path('b')));

    {
        std::ifstream in(store.snapshot_path('b'), std::ios::binary);
        const auto snapshot = nlohmann::json::parse(in);
        CHECK(snapshot["version"] == 1);
        CHECK(snapshot["last_seq"] == scenario.events.back().seq);
        CHECK(snapshot["head_hash"] == scenario.events.back().hash);
        CHECK(snapshot["events"].size() == scenario.events.size());
    }

    REQUIRE(static_cast<bool>(store.compact(ledger)));
    CHECK(fs::file_size(store.journal_path()) == 0);

    Ledger reloaded;
    auto loaded = store.load(reloaded);
    REQUIRE(static_cast<bool>(loaded));
    CHECK(*loaded.value == scenario.events.size());
    CHECK(reloaded.events().size() == scenario.events.size());
    CHECK(reloaded.head_hash() == ledger.head_hash());

    // Appending after compaction still works and replays on top of the snapshot.
    Ledger live;
    REQUIRE(static_cast<bool>(store.load(live)));
    live.set_commit_hook(CommitHook{[&store](const Event& event) { return store.append(event); }});
    auto extra = live.create_user("Bob", "#888888", "", kNow, TimeSource::Synced);
    REQUIRE(static_cast<bool>(extra));

    Ledger after;
    auto after_result = store.load(after);
    REQUIRE(static_cast<bool>(after_result));
    CHECK(after.events().size() == scenario.events.size() + 1);
    CHECK(after.find_user(extra.value->user_id).has_value());
}

TEST_CASE("a corrupt newest snapshot falls back to the other slot") {
    TempDir dir;
    const Scenario scenario = make_scenario(2);
    FileEventStore store((dir.path / "store").string());
    REQUIRE(static_cast<bool>(store.mount()));
    for (const Event& event : scenario.events) {
        REQUIRE(static_cast<bool>(store.append(event)));
    }
    Ledger ledger;
    REQUIRE(static_cast<bool>(store.load(ledger)));
    REQUIRE(static_cast<bool>(store.write_snapshot(ledger)));
    REQUIRE(static_cast<bool>(store.write_snapshot(ledger)));

    {
        std::ofstream out(store.snapshot_path('b'), std::ios::binary);
        out << "not json";
    }

    Ledger reloaded;
    auto loaded = store.load(reloaded);
    REQUIRE(static_cast<bool>(loaded));
    CHECK(reloaded.events().size() == scenario.events.size());
    CHECK(store.last_load_quarantined() == false);
    CHECK(store.degraded());
}

TEST_CASE("factory reset clears journal and snapshots") {
    TempDir dir;
    const Scenario scenario = make_scenario(1);
    FileEventStore store((dir.path / "store").string());
    REQUIRE(static_cast<bool>(store.mount()));
    for (const Event& event : scenario.events) {
        REQUIRE(static_cast<bool>(store.append(event)));
    }
    Ledger ledger;
    REQUIRE(static_cast<bool>(store.load(ledger)));
    REQUIRE(static_cast<bool>(store.write_snapshot(ledger)));

    REQUIRE(static_cast<bool>(store.factory_reset()));
    CHECK_FALSE(fs::exists(store.snapshot_path('a')));
    CHECK_FALSE(fs::exists(store.snapshot_path('b')));

    Ledger empty;
    auto loaded = store.load(empty);
    REQUIRE(static_cast<bool>(loaded));
    CHECK(*loaded.value == 0);
    CHECK(empty.events().empty());
    CHECK(store.info().event_count == 0);
}

TEST_CASE("mount reports a filesystem error instead of crashing") {
    TempDir dir;
    const fs::path blocker = dir.path / "not-a-directory";
    {
        std::ofstream out(blocker, std::ios::binary);
        out << "x";
    }
    FileEventStore store(blocker.string());
    auto mounted = store.mount();
    REQUIRE_FALSE(static_cast<bool>(mounted));
    CHECK(mounted.error->code == "storage");
    CHECK_FALSE(store.info().mounted);
}

TEST_CASE("json settings store round-trips secrets") {
    TempDir dir;
    const std::string path = (dir.path / "settings.json").string();
    JsonFileSettingsStore store(path);

    auto defaults = store.load();
    REQUIRE(static_cast<bool>(defaults));
    CHECK(defaults.value->network.ap_password.empty());

    Settings settings;
    settings.display.rotation = 90;
    settings.network.ap_ssid = "Kaffeekasse-Test";
    settings.network.ap_password = "ap-secret";
    settings.network.sta_password = "sta-secret";
    settings.app.max_open_debt_cents = 500;
    settings.admin.password_hash = "deadbeef";
    settings.admin.salt = "cafe";
    REQUIRE(static_cast<bool>(store.save(settings)));

    auto loaded = store.load();
    REQUIRE(static_cast<bool>(loaded));
    CHECK(loaded.value->display.rotation == 90);
    CHECK(loaded.value->network.ap_ssid == "Kaffeekasse-Test");
    CHECK(loaded.value->network.ap_password == "ap-secret");
    CHECK(loaded.value->network.sta_password == "sta-secret");
    CHECK(loaded.value->app.max_open_debt_cents == 500);
    CHECK(loaded.value->admin.password_hash == "deadbeef");

    REQUIRE(static_cast<bool>(store.reset()));
    auto after_reset = store.load();
    REQUIRE(static_cast<bool>(after_reset));
    CHECK(after_reset.value->network.ap_password.empty());
    CHECK(after_reset.value->admin.password_hash.empty());
}

TEST_CASE("memory settings store keeps a copy") {
    MemorySettingsStore store;
    Settings settings;
    settings.app.guest_enabled = false;
    REQUIRE(static_cast<bool>(store.save(settings)));
    CHECK_FALSE(store.stored().app.guest_enabled);
    REQUIRE(static_cast<bool>(store.reset()));
    CHECK(store.stored().app.guest_enabled);
}
