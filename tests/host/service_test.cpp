#include <doctest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>

#include <unistd.h>

#include "kaffee/auth.hpp"
#include "kaffee/file_store.hpp"
#include "kaffee/json.hpp"
#include "kaffee/service.hpp"

using namespace kaffee;
namespace fs = std::filesystem;

namespace {

constexpr Timestamp kNow = 1'700'000'000;
constexpr const char* kPassword = "correct horse battery";

std::atomic<int> g_temp_counter{0};

struct TempDir {
    fs::path path;

    TempDir() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = fs::temp_directory_path() /
               ("kaffee-svc-" + std::to_string(::getpid()) + "-" + std::to_string(stamp) + "-" +
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

class FakePlatform : public IPlatform {
  public:
    SystemStats stats() const override {
        return stats_;
    }
    TimeInfo time_info() const override {
        return time;
    }
    uint64_t random_u64() override {
        rng += 0x9e3779b97f4a7c15ull;
        uint64_t z = rng;
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
        return z ^ (z >> 31);
    }
    Result<Unit> sync_time(Timestamp epoch_seconds, const std::string& source) override {
        time.now = epoch_seconds;
        time.source = TimeSource::Synced;
        time.last_sync = epoch_seconds;
        time.last_source = source;
        time.trusted = true;
        synced_epoch = epoch_seconds;
        return Result<Unit>::ok(Unit{});
    }
    NetworkStatus network_status() const override {
        return network;
    }
    Result<Unit> start_ap() override {
        network.ap_active = true;
        ++ap_starts;
        return Result<Unit>::ok(Unit{});
    }
    Result<Unit> stop_ap() override {
        network.ap_active = false;
        ++ap_stops;
        return Result<Unit>::ok(Unit{});
    }
    Result<Unit> extend_ap(int seconds) override {
        last_extend = seconds;
        return Result<Unit>::ok(Unit{});
    }
    DisplaySettings display_settings() const override {
        return display;
    }
    Result<Unit> apply_display_settings(const DisplaySettings& settings) override {
        display = settings;
        return Result<Unit>::ok(Unit{});
    }
    Result<Unit> show_display_test() override {
        ++display_tests;
        return Result<Unit>::ok(Unit{});
    }
    void notify_ledger_changed() override {
        ++notifications;
    }

    TimeInfo time;
    NetworkStatus network;
    DisplaySettings display;
    SystemStats stats_;
    uint64_t rng = 0x123456789abcdef0ull;
    Timestamp synced_epoch = 0;
    int ap_starts = 0;
    int ap_stops = 0;
    int last_extend = 0;
    int display_tests = 0;
    int notifications = 0;
};

Event must_event(Result<Event> result) {
    REQUIRE(static_cast<bool>(result));
    return *result.value;
}

struct ServiceFixture {
    TempDir dir;
    FileEventStore store;
    MemorySettingsStore settings_store;
    FakePlatform platform;
    std::unique_ptr<KaffeeService> service;

    explicit ServiceFixture(bool seed_known_password = false)
        : store((dir.path / "events").string()) {
        platform.time.now = kNow;
        platform.time.source = TimeSource::Synced;
        platform.time.last_sync = kNow;
        platform.time.trusted = true;
        if (seed_known_password) {
            Settings settings;
            settings.admin.username = "admin";
            settings.admin.salt = "00112233445566778899aabbccddeeff";
            settings.admin.iterations = 1000;
            settings.admin.password_hash =
                pbkdf2_sha256_hex(kPassword, settings.admin.salt, settings.admin.iterations);
            settings.network.ap_password = "ap-secret";
            REQUIRE(static_cast<bool>(settings_store.save(settings)));
        }
        auto created = KaffeeService::create(store, settings_store, platform);
        REQUIRE(static_cast<bool>(created));
        service = std::move(*created.value);
    }

    std::string user(const std::string& name, const std::string& request_id = "") {
        return must_event(service->create_user(name, "#D4A373", request_id)).user_id;
    }
    std::string product(const std::string& name, Cents price, const std::string& request_id = "") {
        return must_event(
                   service->create_product(name, "Kaffee", "cup", "#D4A373", price, 0, request_id))
            .product_id;
    }
};

}  // namespace

TEST_CASE("first boot generates a setup password") {
    ServiceFixture fixture;
    CHECK(fixture.service->must_change_admin_password());
    const std::string initial = fixture.service->setup_password();
    CHECK(initial.size() == 16);

    CHECK_FALSE(static_cast<bool>(fixture.service->login("admin", "wrong", "10.0.0.1")));
    auto session = fixture.service->login("admin", initial, "10.0.0.2");
    REQUIRE(static_cast<bool>(session));
    CHECK(session.value->token.size() == 32);
    CHECK(session.value->csrf_token.size() == 32);
    CHECK(session.value->token != session.value->csrf_token);
}

TEST_CASE("sessions, csrf and expiry") {
    ServiceFixture fixture(true);
    auto session = fixture.service->login("admin", kPassword, "10.0.0.5");
    REQUIRE(static_cast<bool>(session));
    const std::string token = session.value->token;

    auto validated = fixture.service->validate_session(token);
    REQUIRE(static_cast<bool>(validated));
    CHECK(validated.value->csrf_token == session.value->csrf_token);
    CHECK(fixture.service->validate_csrf(token, session.value->csrf_token));
    CHECK_FALSE(fixture.service->validate_csrf(token, "not-the-token"));
    CHECK_FALSE(fixture.service->validate_csrf("unknown", session.value->csrf_token));

    REQUIRE(static_cast<bool>(fixture.service->logout(token)));
    CHECK_FALSE(static_cast<bool>(fixture.service->validate_session(token)));

    // Idle timeout (30 min).
    auto idle = fixture.service->login("admin", kPassword, "10.0.0.6");
    REQUIRE(static_cast<bool>(idle));
    fixture.platform.time.now = kNow + 30 * 60 + 1;
    CHECK_FALSE(static_cast<bool>(fixture.service->validate_session(idle.value->token)));

    // Absolute timeout (8 h), with activity every 29 min.
    fixture.platform.time.now = kNow;
    auto absolute = fixture.service->login("admin", kPassword, "10.0.0.7");
    REQUIRE(static_cast<bool>(absolute));
    const std::string absolute_token = absolute.value->token;
    for (int i = 0; i < 16; ++i) {
        fixture.platform.time.now += 29 * 60;
        REQUIRE(static_cast<bool>(fixture.service->validate_session(absolute_token)));
    }
    fixture.platform.time.now += 29 * 60;
    CHECK_FALSE(static_cast<bool>(fixture.service->validate_session(absolute_token)));
}

TEST_CASE("login rate limit backs off and resets on success") {
    ServiceFixture fixture(true);
    const std::string ip = "192.168.4.20";
    for (int i = 0; i < 5; ++i) {
        auto failed = fixture.service->login("admin", "wrong", ip);
        REQUIRE_FALSE(static_cast<bool>(failed));
        CHECK(failed.error->code == "auth");
    }
    auto blocked = fixture.service->login("admin", kPassword, ip);
    REQUIRE_FALSE(static_cast<bool>(blocked));
    CHECK(blocked.error->code == "rate_limit");

    fixture.platform.time.now += 6;
    REQUIRE(static_cast<bool>(fixture.service->login("admin", kPassword, ip)));

    // A successful login clears the failure counter.
    for (int i = 0; i < 4; ++i) {
        CHECK_FALSE(static_cast<bool>(fixture.service->login("admin", "wrong", ip)));
    }
    CHECK(static_cast<bool>(fixture.service->login("admin", kPassword, ip)));
}

TEST_CASE("admin password change persists and is audited") {
    ServiceFixture fixture(true);
    auto bad = fixture.service->change_admin_password("wrong", "another-password-1", "chg-0");
    REQUIRE_FALSE(static_cast<bool>(bad));
    CHECK(bad.error->code == "auth");

    auto changed = fixture.service->change_admin_password(kPassword, "another-password-1", "chg-1");
    REQUIRE(static_cast<bool>(changed));
    CHECK_FALSE(fixture.service->must_change_admin_password());
    CHECK(fixture.service->setup_password().empty());
    CHECK_FALSE(static_cast<bool>(fixture.service->login("admin", kPassword, "10.0.1.1")));
    CHECK(static_cast<bool>(fixture.service->login("admin", "another-password-1", "10.0.1.2")));

    const auto& events = fixture.service->events();
    REQUIRE(!events.empty());
    CHECK(events.back().type == EventType::Audit);
    CHECK(events.back().note == "admin_password_changed");
    CHECK(fixture.settings_store.stored().admin.password_hash !=
          pbkdf2_sha256_hex(kPassword, fixture.settings_store.stored().admin.salt, 1000));
}

TEST_CASE("mutations are idempotent and enforce the debt limit") {
    ServiceFixture fixture(true);
    const std::string user_id = fixture.user("Ada", "req-user");
    const std::string duplicate_user = fixture.user("Ada", "req-user");
    CHECK(user_id == duplicate_user);
    CHECK(fixture.service->users().size() == 1);

    const std::string product_id = fixture.product("Kaffee", 60, "req-product");
    auto sale = fixture.service->purchase(user_id, product_id, "req-buy");
    REQUIRE(static_cast<bool>(sale));
    auto retry = fixture.service->purchase(user_id, product_id, "req-buy");
    REQUIRE(static_cast<bool>(retry));
    CHECK(retry.value->id == sale.value->id);
    CHECK(fixture.service->balance(user_id) == -60);
    CHECK(fixture.service->events().size() == 3);

    Settings settings = fixture.service->settings();
    settings.app.max_open_debt_cents = 100;
    REQUIRE(static_cast<bool>(fixture.service->save_settings(settings, "req-settings")));

    auto over_limit = fixture.service->purchase(user_id, product_id, "req-buy-2");
    REQUIRE_FALSE(static_cast<bool>(over_limit));
    CHECK(over_limit.error->code == "debt");
    CHECK(fixture.service->balance(user_id) == -60);

    settings.app.max_open_debt_cents = 0;  // unlimited
    REQUIRE(static_cast<bool>(fixture.service->save_settings(settings, "req-settings-2")));
    CHECK(static_cast<bool>(fixture.service->purchase(user_id, product_id, "req-buy-3")));
    CHECK(fixture.service->balance(user_id) == -120);
}

TEST_CASE("undo booking creates a ui void") {
    ServiceFixture fixture(true);
    const std::string user_id = fixture.user("Ada");
    const std::string product_id = fixture.product("Kaffee", 50);
    auto sale = fixture.service->purchase(user_id, product_id, "req-buy");
    REQUIRE(static_cast<bool>(sale));
    CHECK(fixture.service->balance(user_id) == -50);

    auto undo = fixture.service->undo_booking(sale.value->id, "req-undo");
    REQUIRE(static_cast<bool>(undo));
    CHECK(undo.value->type == EventType::Void);
    CHECK(undo.value->actor == "ui");
    CHECK(undo.value->reverses == sale.value->id);
    CHECK(fixture.service->balance(user_id) == 0);

    auto topup = fixture.service->topup(user_id, 100, "cash", "req-topup");
    REQUIRE(static_cast<bool>(topup));
    auto bad_undo = fixture.service->undo_booking(topup.value->id, "req-undo-2");
    REQUIRE_FALSE(static_cast<bool>(bad_undo));
}

TEST_CASE("settings never expose secrets and keep them when omitted") {
    ServiceFixture fixture(true);
    const Settings initial = fixture.settings_store.stored();
    REQUIRE_FALSE(initial.network.ap_password.empty());

    const Settings visible = fixture.service->settings();
    CHECK(visible.network.ap_password.empty());
    CHECK(visible.network.sta_password.empty());
    CHECK(visible.admin.password_hash.empty());
    CHECK(visible.admin.salt.empty());

    Settings input = visible;
    input.network.ap_ssid = "Kaffeekasse-Test";
    input.app.guest_enabled = false;
    REQUIRE(static_cast<bool>(fixture.service->save_settings(input, "set-1")));

    const Settings stored = fixture.settings_store.stored();
    CHECK(stored.network.ap_password == initial.network.ap_password);
    CHECK(stored.admin.password_hash == initial.admin.password_hash);
    CHECK_FALSE(stored.app.guest_enabled);
    CHECK(stored.network.ap_ssid == "Kaffeekasse-Test");

    const auto& events = fixture.service->events();
    REQUIRE(!events.empty());
    CHECK(events.back().note == "settings_updated");
    CHECK(events.back().meta_json.find("ap-secret") == std::string::npos);
    CHECK(events.back().meta_json.find("password_hash") == std::string::npos);

    input.network.ap_password = "brand-new-secret";
    REQUIRE(static_cast<bool>(fixture.service->save_settings(input, "set-2")));
    CHECK(fixture.settings_store.stored().network.ap_password == "brand-new-secret");
}

TEST_CASE("pretty display names are applied and delegated") {
    ServiceFixture fixture(true);
    DisplaySettings display;
    display.rotation = 90;
    display.brightness_active = 70;
    REQUIRE(static_cast<bool>(fixture.service->apply_display_settings(display)));
    CHECK(fixture.platform.display.rotation == 90);
    CHECK(fixture.service->display_settings().rotation == 90);

    display.rotation = 45;
    CHECK_FALSE(static_cast<bool>(fixture.service->apply_display_settings(display)));
}

TEST_CASE("time quality follows the platform and browser sync") {
    ServiceFixture fixture(true);
    const std::string user_id = fixture.user("Ada");
    const std::string product_id = fixture.product("Kaffee", 50);

    fixture.platform.time.source = TimeSource::Restored;
    auto restored = fixture.service->purchase(user_id, product_id, "t-1");
    REQUIRE(static_cast<bool>(restored));
    CHECK(restored.value->time_source == TimeSource::Restored);
    CHECK(restored.value->ts == kNow);

    REQUIRE(static_cast<bool>(fixture.service->sync_time(kNow + 120, "browser")));
    CHECK(fixture.platform.synced_epoch == kNow + 120);
    CHECK(fixture.service->time_info().source == TimeSource::Synced);

    auto synced = fixture.service->purchase(user_id, product_id, "t-2");
    REQUIRE(static_cast<bool>(synced));
    CHECK(synced.value->time_source == TimeSource::Synced);
    CHECK(synced.value->ts == kNow + 120);
}

TEST_CASE("ap delegation clears sessions on stop") {
    ServiceFixture fixture(true);
    auto session = fixture.service->login("admin", kPassword, "10.0.0.9");
    REQUIRE(static_cast<bool>(session));

    REQUIRE(static_cast<bool>(fixture.service->start_ap()));
    CHECK(fixture.platform.ap_starts == 1);
    REQUIRE(static_cast<bool>(fixture.service->extend_ap(60)));
    CHECK(fixture.platform.last_extend == 60);
    REQUIRE(static_cast<bool>(fixture.service->stop_ap()));
    CHECK(fixture.platform.ap_stops == 1);
    CHECK_FALSE(static_cast<bool>(fixture.service->validate_session(session.value->token)));
}

TEST_CASE("change callback and platform notification fire once per mutation") {
    ServiceFixture fixture(true);
    int callback_count = 0;
    fixture.service->set_change_callback([&callback_count]() { ++callback_count; });

    fixture.user("Ada");
    CHECK(callback_count == 1);
    CHECK(fixture.platform.notifications == 1);

    fixture.service->refresh();
    CHECK(callback_count == 2);
}

TEST_CASE("backup excludes secrets and restores state") {
    ServiceFixture fixture(true);
    const std::string user_id = fixture.user("Ada");
    const std::string product_id = fixture.product("Kaffee", 50);
    REQUIRE(static_cast<bool>(fixture.service->topup(user_id, 1000, "cash", "b-1")));
    REQUIRE(static_cast<bool>(fixture.service->purchase(user_id, product_id, "b-2")));
    REQUIRE(static_cast<bool>(
        fixture.service->add_expense("Maschine", "Entkalkung", "Shop", 300, "", "b-3")));

    auto backup = fixture.service->export_backup();
    REQUIRE(static_cast<bool>(backup));
    CHECK(backup.value->find("ap-secret") == std::string::npos);
    CHECK(backup.value->find("password_hash") == std::string::npos);

    const auto parsed = nlohmann::json::parse(*backup.value);
    CHECK(parsed["format"] == "kaffeekasse-backup");
    CHECK(parsed["version"] == 1);
    CHECK_FALSE(parsed["settings"].contains("admin"));
    CHECK_FALSE(parsed["settings"]["network"].contains("ap_password"));
    CHECK_FALSE(parsed["settings"]["network"].contains("sta_password"));
    CHECK(parsed["events"].size() == fixture.service->events().size());
    CHECK(fixture.service->stats().last_backup_at == kNow);

    TempDir other_dir;
    FileEventStore other_store((other_dir.path / "events").string());
    MemorySettingsStore other_settings;
    FakePlatform other_platform;
    other_platform.time.now = kNow + 500;
    other_platform.time.source = TimeSource::Synced;
    auto created = KaffeeService::create(other_store, other_settings, other_platform);
    REQUIRE(static_cast<bool>(created));
    std::unique_ptr<KaffeeService> other = std::move(*created.value);

    auto restored = other->import_backup(*backup.value, "restore-1");
    REQUIRE(static_cast<bool>(restored));
    CHECK(other->users().size() == fixture.service->users().size());
    CHECK(other->products().size() == fixture.service->products().size());
    CHECK(other->events().size() == fixture.service->events().size() + 1);  // restore audit
    CHECK(other->balance(user_id) == fixture.service->balance(user_id));
    CHECK(other->summary().cash_expected == fixture.service->summary().cash_expected);
    CHECK(other->summary().debts == fixture.service->summary().debts);
    CHECK(other->summary().expenses == fixture.service->summary().expenses);
    CHECK(other->events().back().note == "backup_restored");

    // The restore target keeps its own generated secrets.
    CHECK_FALSE(other_settings.stored().network.ap_password.empty());
    CHECK_FALSE(other_settings.stored().admin.password_hash.empty());
    CHECK_FALSE(other_settings.stored().admin.salt.empty());

    const size_t before = other->events().size();
    REQUIRE(static_cast<bool>(other->import_backup(*backup.value, "restore-1")));
    CHECK(other->events().size() == before);
}

TEST_CASE("service reloads the ledger and admin credentials from storage") {
    TempDir dir;
    FileEventStore store((dir.path / "events").string());
    MemorySettingsStore settings_store;
    FakePlatform platform;
    platform.time.now = kNow;
    platform.time.source = TimeSource::Synced;

    {
        auto created = KaffeeService::create(store, settings_store, platform);
        REQUIRE(static_cast<bool>(created));
        auto service = std::move(*created.value);
        must_event(service->create_user("Ada", "#fff", "reload-1"));
        REQUIRE(static_cast<bool>(service->login("admin", service->setup_password(), "ip")));
        REQUIRE(static_cast<bool>(service->change_admin_password(service->setup_password(),
                                                                 "reloaded-password", "reload-2")));
    }

    auto created = KaffeeService::create(store, settings_store, platform);
    REQUIRE(static_cast<bool>(created));
    auto service = std::move(*created.value);
    CHECK(service->users().size() == 1);
    CHECK_FALSE(service->must_change_admin_password());
    CHECK(static_cast<bool>(service->login("admin", "reloaded-password", "ip-2")));
}
