#include "kaffee/service.hpp"

#include <algorithm>
#include <cstdio>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "kaffee/auth.hpp"
#include "kaffee/json.hpp"

namespace kaffee {
namespace {

constexpr Timestamp kSessionIdleSeconds = 30 * 60;
constexpr Timestamp kSessionAbsoluteSeconds = 8 * 60 * 60;
constexpr int kLoginMaxFailures = 5;
constexpr Timestamp kLoginBackoffBaseSeconds = 5;
constexpr Timestamp kLoginBackoffMaxSeconds = 300;
constexpr uint32_t kAdminIterations = 50000;
constexpr size_t kAdminSaltBytes = 16;
constexpr size_t kSessionTokenBytes = 16;

std::string make_id(IPlatform& platform, std::string_view prefix) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%016llx",
                  static_cast<unsigned long long>(platform.random_u64()));
    return std::string(prefix) + "_" + buffer;
}

std::string random_hex(IPlatform& platform, size_t bytes) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes * 2);
    while (out.size() < bytes * 2) {
        uint64_t value = platform.random_u64();
        for (int i = 0; i < 16 && out.size() < bytes * 2; ++i) {
            out.push_back(kHex[value & 0x0f]);
            value >>= 4;
        }
    }
    return out;
}

std::string random_password(IPlatform& platform, size_t length) {
    // Unambiguous alphabet for a password that is read from a screen.
    static constexpr char kAlphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    std::string out;
    out.reserve(length);
    while (out.size() < length) {
        uint64_t value = platform.random_u64();
        for (int i = 0; i < 10 && out.size() < length; ++i) {
            out.push_back(kAlphabet[value % (sizeof(kAlphabet) - 1)]);
            value /= (sizeof(kAlphabet) - 1);
        }
    }
    return out;
}

Result<Unit> settings_fail(std::string message) {
    return Result<Unit>::fail("settings", std::move(message));
}

Result<Unit> validate_settings(const Settings& settings) {
    const DisplaySettings& d = settings.display;
    if (d.rotation != 0 && d.rotation != 90 && d.rotation != 180 && d.rotation != 270)
        return settings_fail("invalid rotation");
    if (d.brightness_active < 5 || d.brightness_active > 100)
        return settings_fail("brightness_active out of range");
    if (d.brightness_idle < 1 || d.brightness_idle > 100)
        return settings_fail("brightness_idle out of range");
    if (d.dim_timeout_s < 0 || d.dim_timeout_s > 86400)
        return settings_fail("dim_timeout_s out of range");
    if (d.screen_off_timeout_s < 0 || d.screen_off_timeout_s > 86400)
        return settings_fail("screen_off_timeout_s out of range");
    if (settings.network.ap_window_s < 0 || settings.network.ap_window_s > 86400)
        return settings_fail("ap_window_s out of range");
    if (settings.network.hostname.empty()) return settings_fail("hostname must not be empty");
    if (settings.time.timezone.empty()) return settings_fail("timezone must not be empty");
    if (settings.app.max_open_debt_cents < 0)
        return settings_fail("max_open_debt_cents must not be negative");
    return Result<Unit>::ok(Unit{});
}

}  // namespace

class KaffeeServiceImpl final : public KaffeeService {
  public:
    KaffeeServiceImpl(IEventStore& store, ISettingsStore& settings_store, IPlatform& platform)
        : store_(store), settings_store_(settings_store), platform_(platform) {}

    Result<Unit> init();

    // --- UI-facing ---
    const std::vector<User>& users() const override;
    const std::vector<Product>& products() const override;
    std::optional<User> find_user(const std::string& id) const override;
    std::optional<Product> find_product(const std::string& id) const override;
    Cents balance(const std::string& user_id) const override;
    Summary summary() const override;
    Leaderboard leaderboard(Timestamp from, Timestamp to) const override;
    SystemStats stats() const override;
    TimeInfo time_info() const override;
    NetworkStatus network_status() const override;
    DisplaySettings display_settings() const override;
    AppSettings app_settings() const override;
    Result<Unit> apply_display_settings(const DisplaySettings& settings) override;

    Result<Event> purchase(const std::string& user_id, const std::string& product_id,
                           const std::string& request_id) override;
    Result<Event> guest_sale(const std::string& product_id, const std::string& request_id) override;

    // --- Admin operations ---
    Result<Event> create_user(const std::string& name, const std::string& color,
                              const std::string& request_id) override;
    Result<Event> update_user(const std::string& user_id, const std::string& name,
                              const std::string& color, bool active,
                              const std::string& request_id) override;
    Result<Event> archive_user(const std::string& user_id, const std::string& request_id) override;
    Result<Event> create_product(const std::string& name, const std::string& category,
                                 const std::string& icon, const std::string& color,
                                 Cents price_cents, int sort,
                                 const std::string& request_id) override;
    Result<Event> update_product(const std::string& product_id, const std::string& name,
                                 const std::string& category, const std::string& icon,
                                 const std::string& color, Cents price_cents, int sort, bool active,
                                 const std::string& request_id) override;
    Result<Event> archive_product(const std::string& product_id,
                                  const std::string& request_id) override;
    Result<Event> delete_product(const std::string& product_id,
                                 const std::string& request_id) override;
    Result<Event> topup(const std::string& user_id, Cents amount_cents, const std::string& note,
                        const std::string& request_id) override;
    Result<Event> adjust(const std::string& user_id, Cents amount_cents, const std::string& note,
                         const std::string& request_id) override;
    Result<Event> void_event(const std::string& event_id, const std::string& reason,
                             const std::string& request_id) override;
    Result<Event> undo_booking(const std::string& event_id, const std::string& request_id) override;
    Result<Event> add_expense(const std::string& category, const std::string& description,
                              const std::string& vendor, Cents amount_cents,
                              const std::string& note, const std::string& request_id) override;
    Result<Event> update_expense(const std::string& expense_id, const std::string& category,
                                 const std::string& description, const std::string& vendor,
                                 Cents amount_cents, const std::string& note,
                                 const std::string& request_id) override;
    Result<Event> void_expense(const std::string& expense_id, const std::string& reason,
                               const std::string& request_id) override;
    Result<Event> record_cash_count(Cents counted_cents, const std::string& note,
                                    const std::string& request_id) override;

    const std::vector<Event>& events() const override;
    const std::vector<Expense>& expenses() const override;
    const std::vector<CashCount>& cash_counts() const override;

    // --- Auth ---
    Result<Session> login(const std::string& username, const std::string& password,
                          const std::string& client_ip) override;
    Result<Unit> logout(const std::string& token) override;
    Result<Session> validate_session(const std::string& token) override;
    bool validate_csrf(const std::string& token, const std::string& csrf) override;
    Result<Unit> change_admin_password(const std::string& current_password,
                                       const std::string& new_password,
                                       const std::string& request_id) override;
    bool must_change_admin_password() const override;
    std::string setup_password() const override;

    // --- Settings / time / AP ---
    Settings settings() const override;
    Result<Unit> save_settings(const Settings& settings, const std::string& request_id) override;
    Result<Unit> sync_time(Timestamp epoch_seconds, const std::string& source) override;
    Result<Unit> start_ap() override;
    Result<Unit> stop_ap() override;
    Result<Unit> extend_ap(int seconds) override;

    // --- Backup ---
    Result<std::string> export_backup() override;
    Result<Unit> import_backup(const std::string& json, const std::string& request_id) override;

    void set_change_callback(ChangeCallback callback) override;
    void refresh() override;

  private:
    struct SessionState {
        Session session;
        Timestamp last_activity = 0;
    };

    struct LoginState {
        int failures = 0;
        Timestamp blocked_until = 0;
    };

    template <typename Fn>
    Result<Event> mutate(Fn fn) {
        Result<Event> result;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            result = fn();
        }
        if (result) notify_changed();
        return result;
    }

    template <typename Fn>
    Result<Unit> mutate_unit(Fn fn) {
        Result<Unit> result;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            result = fn();
        }
        if (result) notify_changed();
        return result;
    }

    Timestamp now() const {
        return platform_.time_info().now;
    }
    TimeSource time_source() const {
        return platform_.time_info().source;
    }
    void notify_changed();
    void record_login_failure(const std::string& client_ip, Timestamp ts);
    bool session_expired(const SessionState& state, Timestamp ts) const;
    Timestamp session_expiry(const SessionState& state) const;
    std::string hash_password(const std::string& password) const;
    Result<Unit> audit_locked(const std::string& kind, const std::string& meta,
                              const std::string& request_id, const std::string& actor = "admin");
    bool has_password_change_audit() const;

    IEventStore& store_;
    ISettingsStore& settings_store_;
    IPlatform& platform_;

    mutable std::mutex mutex_;
    Ledger ledger_;
    Settings settings_;
    std::map<std::string, SessionState> sessions_;
    std::map<std::string, LoginState> login_states_;
    ChangeCallback change_callback_;
    std::string setup_password_;
    bool must_change_password_ = false;
    Timestamp last_backup_at_ = 0;
};

// ---------------------------------------------------------------------------
// Construction

Result<Unit> KaffeeServiceImpl::init() {
    auto mounted = store_.mount();
    if (!mounted) return mounted;

    ledger_ = Ledger([this](std::string_view prefix) { return make_id(platform_, prefix); });
    auto loaded = store_.load(ledger_);
    if (!loaded) return Result<Unit>::fail(loaded.error->code, loaded.error->message);

    auto stored = settings_store_.load();
    if (stored) {
        settings_ = *stored.value;
    } else {
        settings_ = Settings{};
    }
    if (settings_.admin.username.empty()) settings_.admin.username = "admin";

    bool dirty = false;
    const bool has_credentials =
        !settings_.admin.password_hash.empty() && !settings_.admin.salt.empty();
    if (!has_credentials) {
        const std::string initial = random_password(platform_, 16);
        settings_.admin.salt = random_hex(platform_, kAdminSaltBytes);
        settings_.admin.iterations = kAdminIterations;
        settings_.admin.password_hash =
            pbkdf2_sha256_hex(initial, settings_.admin.salt, settings_.admin.iterations);
        setup_password_ = initial;
        must_change_password_ = true;
        dirty = true;
    } else {
        must_change_password_ = !has_password_change_audit();
    }

    if (settings_.network.ap_password.empty()) {
        settings_.network.ap_password = random_password(platform_, 12);
        dirty = true;
    }
    if (dirty) {
        auto saved = settings_store_.save(settings_);
        if (!saved) return saved;
    }

    ledger_.set_commit_hook(
        CommitHook{[this](const Event& event) { return store_.append(event); }});
    return Result<Unit>::ok(Unit{});
}

bool KaffeeServiceImpl::has_password_change_audit() const {
    return std::any_of(ledger_.events().begin(), ledger_.events().end(), [](const Event& event) {
        return event.type == EventType::Audit && event.note == "admin_password_changed";
    });
}

void KaffeeServiceImpl::notify_changed() {
    ChangeCallback callback;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        callback = change_callback_;
    }
    platform_.notify_ledger_changed();
    if (callback) callback();
}

std::string KaffeeServiceImpl::hash_password(const std::string& password) const {
    return pbkdf2_sha256_hex(password, settings_.admin.salt, settings_.admin.iterations);
}

Result<Unit> KaffeeServiceImpl::audit_locked(const std::string& kind, const std::string& meta,
                                             const std::string& request_id,
                                             const std::string& actor) {
    auto result = ledger_.audit(kind, meta, actor, request_id, now(), time_source());
    if (!result) return Result<Unit>::fail(result.error->code, result.error->message);
    return Result<Unit>::ok(Unit{});
}

// ---------------------------------------------------------------------------
// Queries

const std::vector<User>& KaffeeServiceImpl::users() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return ledger_.users();
}

const std::vector<Product>& KaffeeServiceImpl::products() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return ledger_.products();
}

std::optional<User> KaffeeServiceImpl::find_user(const std::string& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return ledger_.find_user(id);
}

std::optional<Product> KaffeeServiceImpl::find_product(const std::string& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return ledger_.find_product(id);
}

Cents KaffeeServiceImpl::balance(const std::string& user_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return ledger_.balance(user_id);
}

Summary KaffeeServiceImpl::summary() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return ledger_.summary();
}

Leaderboard KaffeeServiceImpl::leaderboard(Timestamp from, Timestamp to) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return ledger_.leaderboard(from, to);
}

SystemStats KaffeeServiceImpl::stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    SystemStats out = platform_.stats();
    out.storage = store_.info();
    out.time = platform_.time_info();
    out.last_backup_at = last_backup_at_;

    Timestamp ts = out.time.now;
    if (ts > 0) {
        const Timestamp day_start = ts - (ts % 86400);
        int drinks = 0;
        for (const Event& event : ledger_.events()) {
            if (event.ts < day_start || event.ts >= day_start + 86400) continue;
            if (event.type == EventType::Purchase || event.type == EventType::GuestSale) ++drinks;
        }
        out.drinks_today = drinks;
    }
    return out;
}

TimeInfo KaffeeServiceImpl::time_info() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return platform_.time_info();
}

NetworkStatus KaffeeServiceImpl::network_status() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return platform_.network_status();
}

DisplaySettings KaffeeServiceImpl::display_settings() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return settings_.display;
}

AppSettings KaffeeServiceImpl::app_settings() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return settings_.app;
}

Result<Unit> KaffeeServiceImpl::apply_display_settings(const DisplaySettings& display) {
    return mutate_unit([&]() -> Result<Unit> {
        Settings candidate = settings_;
        candidate.display = display;
        auto valid = validate_settings(candidate);
        if (!valid) return valid;
        auto applied = platform_.apply_display_settings(display);
        if (!applied) return applied;
        settings_.display = display;
        return settings_store_.save(settings_);
    });
}

// ---------------------------------------------------------------------------
// Mutations

Result<Event> KaffeeServiceImpl::purchase(const std::string& user_id, const std::string& product_id,
                                          const std::string& request_id) {
    return mutate([&]() -> Result<Event> {
        const auto product = ledger_.find_product(product_id);
        const auto user = ledger_.find_user(user_id);
        if (product && user) {
            if (settings_.app.max_open_debt_cents > 0 && product->active && user->active) {
                const Cents after = ledger_.balance(user_id) - product->price_cents;
                if (after < -static_cast<Cents>(settings_.app.max_open_debt_cents)) {
                    return Result<Event>::fail("debt", "maximum open debt exceeded");
                }
            }
        }
        return ledger_.purchase(user_id, product_id, request_id, now(), time_source());
    });
}

Result<Event> KaffeeServiceImpl::guest_sale(const std::string& product_id,
                                            const std::string& request_id) {
    return mutate(
        [&]() { return ledger_.guest_sale(product_id, request_id, now(), time_source()); });
}

Result<Event> KaffeeServiceImpl::create_user(const std::string& name, const std::string& color,
                                             const std::string& request_id) {
    return mutate(
        [&]() { return ledger_.create_user(name, color, request_id, now(), time_source()); });
}

Result<Event> KaffeeServiceImpl::update_user(const std::string& user_id, const std::string& name,
                                             const std::string& color, bool active,
                                             const std::string& request_id) {
    return mutate([&]() {
        return ledger_.update_user(user_id, name, color, active, request_id, now(), time_source());
    });
}

Result<Event> KaffeeServiceImpl::archive_user(const std::string& user_id,
                                              const std::string& request_id) {
    return mutate(
        [&]() { return ledger_.archive_user(user_id, request_id, now(), time_source()); });
}

Result<Event> KaffeeServiceImpl::create_product(const std::string& name,
                                                const std::string& category,
                                                const std::string& icon, const std::string& color,
                                                Cents price_cents, int sort,
                                                const std::string& request_id) {
    return mutate([&]() {
        return ledger_.create_product(name, category, icon, color, price_cents, sort, request_id,
                                      now(), time_source());
    });
}

Result<Event> KaffeeServiceImpl::update_product(const std::string& product_id,
                                                const std::string& name,
                                                const std::string& category,
                                                const std::string& icon, const std::string& color,
                                                Cents price_cents, int sort, bool active,
                                                const std::string& request_id) {
    return mutate([&]() {
        return ledger_.update_product(product_id, name, category, icon, color, price_cents, sort,
                                      active, request_id, now(), time_source());
    });
}

Result<Event> KaffeeServiceImpl::archive_product(const std::string& product_id,
                                                 const std::string& request_id) {
    return mutate(
        [&]() { return ledger_.archive_product(product_id, request_id, now(), time_source()); });
}

Result<Event> KaffeeServiceImpl::delete_product(const std::string& product_id,
                                                const std::string& request_id) {
    return mutate(
        [&]() { return ledger_.delete_product(product_id, request_id, now(), time_source()); });
}

Result<Event> KaffeeServiceImpl::topup(const std::string& user_id, Cents amount_cents,
                                       const std::string& note, const std::string& request_id) {
    return mutate([&]() {
        return ledger_.topup(user_id, amount_cents, note, request_id, now(), time_source());
    });
}

Result<Event> KaffeeServiceImpl::adjust(const std::string& user_id, Cents amount_cents,
                                        const std::string& note, const std::string& request_id) {
    return mutate([&]() -> Result<Event> {
        if (amount_cents < 0 && settings_.app.max_open_debt_cents > 0 &&
            ledger_.find_user(user_id)) {
            const Cents after = ledger_.balance(user_id) + amount_cents;
            if (after < -static_cast<Cents>(settings_.app.max_open_debt_cents)) {
                return Result<Event>::fail("debt", "maximum open debt exceeded");
            }
        }
        return ledger_.adjust(user_id, amount_cents, note, request_id, now(), time_source());
    });
}

Result<Event> KaffeeServiceImpl::void_event(const std::string& event_id, const std::string& reason,
                                            const std::string& request_id) {
    return mutate([&]() {
        return ledger_.void_event(event_id, reason, request_id, now(), time_source(), "admin");
    });
}

Result<Event> KaffeeServiceImpl::undo_booking(const std::string& event_id,
                                              const std::string& request_id) {
    return mutate([&]() -> Result<Event> {
        const auto original = ledger_.find_event(event_id);
        if (!original) return Result<Event>::fail("event", "unknown event");
        if (original->type != EventType::Purchase)
            return Result<Event>::fail("void", "only member bookings can be undone");
        return ledger_.void_event(event_id, "undo", request_id, now(), time_source(), "ui");
    });
}

Result<Event> KaffeeServiceImpl::add_expense(const std::string& category,
                                             const std::string& description,
                                             const std::string& vendor, Cents amount_cents,
                                             const std::string& note,
                                             const std::string& request_id) {
    return mutate([&]() {
        return ledger_.add_expense(category, description, vendor, amount_cents, note, request_id,
                                   now(), time_source());
    });
}

Result<Event> KaffeeServiceImpl::update_expense(const std::string& expense_id,
                                                const std::string& category,
                                                const std::string& description,
                                                const std::string& vendor, Cents amount_cents,
                                                const std::string& note,
                                                const std::string& request_id) {
    return mutate([&]() {
        return ledger_.update_expense(expense_id, category, description, vendor, amount_cents, note,
                                      request_id, now(), time_source());
    });
}

Result<Event> KaffeeServiceImpl::void_expense(const std::string& expense_id,
                                              const std::string& reason,
                                              const std::string& request_id) {
    return mutate([&]() {
        return ledger_.void_expense(expense_id, reason, request_id, now(), time_source());
    });
}

Result<Event> KaffeeServiceImpl::record_cash_count(Cents counted_cents, const std::string& note,
                                                   const std::string& request_id) {
    return mutate([&]() {
        return ledger_.record_cash_count(counted_cents, note, request_id, now(), time_source());
    });
}

const std::vector<Event>& KaffeeServiceImpl::events() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return ledger_.events();
}

const std::vector<Expense>& KaffeeServiceImpl::expenses() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return ledger_.expenses();
}

const std::vector<CashCount>& KaffeeServiceImpl::cash_counts() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return ledger_.cash_counts();
}

// ---------------------------------------------------------------------------
// Auth

void KaffeeServiceImpl::record_login_failure(const std::string& client_ip, Timestamp ts) {
    LoginState& state = login_states_[client_ip];
    ++state.failures;
    if (state.failures >= kLoginMaxFailures) {
        const int exponent = std::min(state.failures - kLoginMaxFailures, 6);
        Timestamp delay = kLoginBackoffBaseSeconds << exponent;
        if (delay > kLoginBackoffMaxSeconds) delay = kLoginBackoffMaxSeconds;
        state.blocked_until = ts > 0 ? ts + delay : 0;
    }
}

Timestamp KaffeeServiceImpl::session_expiry(const SessionState& state) const {
    Timestamp expiry = state.session.created_at + kSessionAbsoluteSeconds;
    if (state.last_activity > 0) {
        expiry = std::min(expiry, state.last_activity + kSessionIdleSeconds);
    }
    return expiry;
}

bool KaffeeServiceImpl::session_expired(const SessionState& state, Timestamp ts) const {
    if (ts <= 0) return false;
    const Timestamp expiry = session_expiry(state);
    return expiry > 0 && ts > expiry;
}

Result<Session> KaffeeServiceImpl::login(const std::string& username, const std::string& password,
                                         const std::string& client_ip) {
    std::lock_guard<std::mutex> lock(mutex_);
    const Timestamp ts = now();

    auto& state = login_states_[client_ip];
    if (state.blocked_until > 0 && ts < state.blocked_until) {
        return Result<Session>::fail("rate_limit", "too many attempts, try again later");
    }

    const bool user_ok = constant_time_equal(username, settings_.admin.username);
    const bool password_ok =
        constant_time_equal(hash_password(password), settings_.admin.password_hash);
    if (!user_ok || !password_ok) {
        record_login_failure(client_ip, ts);
        return Result<Session>::fail("auth", "invalid credentials");
    }

    login_states_.erase(client_ip);
    Session session;
    session.token = random_hex(platform_, kSessionTokenBytes);
    session.csrf_token = random_hex(platform_, kSessionTokenBytes);
    session.created_at = ts;
    session.expires_at = ts + kSessionIdleSeconds;
    sessions_[session.token] = SessionState{session, ts};
    return Result<Session>::ok(session);
}

Result<Unit> KaffeeServiceImpl::logout(const std::string& token) {
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_.erase(token);
    return Result<Unit>::ok(Unit{});
}

Result<Session> KaffeeServiceImpl::validate_session(const std::string& token) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(token);
    if (it == sessions_.end()) return Result<Session>::fail("session", "invalid session");
    const Timestamp ts = now();
    if (session_expired(it->second, ts)) {
        sessions_.erase(it);
        return Result<Session>::fail("session", "session expired");
    }
    it->second.last_activity = ts;
    it->second.session.expires_at = session_expiry(it->second);
    return Result<Session>::ok(it->second.session);
}

bool KaffeeServiceImpl::validate_csrf(const std::string& token, const std::string& csrf) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(token);
    if (it == sessions_.end()) return false;
    if (session_expired(it->second, now())) {
        sessions_.erase(it);
        return false;
    }
    return constant_time_equal(csrf, it->second.session.csrf_token);
}

Result<Unit> KaffeeServiceImpl::change_admin_password(const std::string& current_password,
                                                      const std::string& new_password,
                                                      const std::string& request_id) {
    return mutate_unit([&]() -> Result<Unit> {
        if (!constant_time_equal(hash_password(current_password), settings_.admin.password_hash)) {
            return Result<Unit>::fail("auth", "current password is incorrect");
        }
        if (new_password.size() < 10)
            return Result<Unit>::fail("password", "new password must have at least 10 characters");
        if (new_password.size() > 256)
            return Result<Unit>::fail("password", "new password is too long");
        if (constant_time_equal(new_password, current_password))
            return Result<Unit>::fail("password", "new password must differ from the current one");

        Settings updated = settings_;
        updated.admin.username = "admin";
        updated.admin.salt = random_hex(platform_, kAdminSaltBytes);
        updated.admin.iterations = kAdminIterations;
        updated.admin.password_hash =
            pbkdf2_sha256_hex(new_password, updated.admin.salt, updated.admin.iterations);
        auto saved = settings_store_.save(updated);
        if (!saved) return saved;

        settings_ = updated;
        setup_password_.clear();
        must_change_password_ = false;
        return audit_locked("admin_password_changed", "{}", request_id, "admin");
    });
}

bool KaffeeServiceImpl::must_change_admin_password() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return must_change_password_;
}

std::string KaffeeServiceImpl::setup_password() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return setup_password_;
}

// ---------------------------------------------------------------------------
// Settings / time / AP

Settings KaffeeServiceImpl::settings() const {
    std::lock_guard<std::mutex> lock(mutex_);
    Settings out = settings_;
    out.admin.password_hash.clear();
    out.admin.salt.clear();
    out.network.ap_password.clear();
    out.network.sta_password.clear();
    return out;
}

Result<Unit> KaffeeServiceImpl::save_settings(const Settings& input,
                                              const std::string& request_id) {
    return mutate_unit([&]() -> Result<Unit> {
        Settings merged = settings_;
        merged.display = input.display;
        merged.network.ap_ssid = input.network.ap_ssid;
        merged.network.ap_window_s = input.network.ap_window_s;
        merged.network.hostname = input.network.hostname;
        merged.network.sta_enabled = input.network.sta_enabled;
        merged.network.sta_ssid = input.network.sta_ssid;
        merged.network.napt_enabled = input.network.napt_enabled;
        if (!input.network.ap_password.empty())
            merged.network.ap_password = input.network.ap_password;
        if (!input.network.sta_password.empty())
            merged.network.sta_password = input.network.sta_password;
        merged.time = input.time;
        merged.app = input.app;
        // Admin credentials are only changed through change_admin_password().

        auto valid = validate_settings(merged);
        if (!valid) return valid;

        auto applied = platform_.apply_display_settings(merged.display);
        if (!applied) return applied;
        auto saved = settings_store_.save(merged);
        if (!saved) return saved;

        settings_ = merged;
        const std::string meta = settings_to_json(settings_, false).dump();
        return audit_locked("settings_updated", meta, request_id, "admin");
    });
}

Result<Unit> KaffeeServiceImpl::sync_time(Timestamp epoch_seconds, const std::string& source) {
    if (epoch_seconds <= 0) return Result<Unit>::fail("time", "invalid epoch");
    Result<Unit> result;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        result = platform_.sync_time(epoch_seconds, source);
    }
    if (result) notify_changed();
    return result;
}

Result<Unit> KaffeeServiceImpl::start_ap() {
    std::lock_guard<std::mutex> lock(mutex_);
    return platform_.start_ap();
}

Result<Unit> KaffeeServiceImpl::stop_ap() {
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_.clear();
    login_states_.clear();
    return platform_.stop_ap();
}

Result<Unit> KaffeeServiceImpl::extend_ap(int seconds) {
    if (seconds <= 0) return Result<Unit>::fail("ap", "duration must be positive");
    std::lock_guard<std::mutex> lock(mutex_);
    return platform_.extend_ap(seconds);
}

// ---------------------------------------------------------------------------
// Backup

Result<std::string> KaffeeServiceImpl::export_backup() {
    std::lock_guard<std::mutex> lock(mutex_);

    nlohmann::json users = nlohmann::json::array();
    for (const User& user : ledger_.users()) users.push_back(user_to_json(user));
    nlohmann::json products = nlohmann::json::array();
    for (const Product& product : ledger_.products()) products.push_back(product_to_json(product));
    nlohmann::json events = nlohmann::json::array();
    for (const Event& event : ledger_.events()) events.push_back(event_to_json(event));
    nlohmann::json expenses_json = nlohmann::json::array();
    for (const Expense& expense : ledger_.expenses())
        expenses_json.push_back(expense_to_json(expense));
    nlohmann::json cash_counts = nlohmann::json::array();
    for (const CashCount& count : ledger_.cash_counts())
        cash_counts.push_back(cash_count_to_json(count));

    const nlohmann::json backup{{"format", "kaffeekasse-backup"},
                                {"version", 1},
                                {"created_at", now()},
                                {"settings", settings_to_json(settings_, false)},
                                {"users", std::move(users)},
                                {"products", std::move(products)},
                                {"events", std::move(events)},
                                {"expenses", std::move(expenses_json)},
                                {"cash_counts", std::move(cash_counts)}};

    last_backup_at_ = now();
    return Result<std::string>::ok(backup.dump(2));
}

Result<Unit> KaffeeServiceImpl::import_backup(const std::string& json,
                                              const std::string& request_id) {
    return mutate_unit([&]() -> Result<Unit> {
        if (!request_id.empty() && ledger_.find_request(request_id)) {
            return Result<Unit>::ok(Unit{});
        }

        nlohmann::json parsed;
        try {
            parsed = nlohmann::json::parse(json);
        } catch (const std::exception& e) {
            return Result<Unit>::fail("backup", std::string("invalid JSON: ") + e.what());
        }
        if (!parsed.is_object()) return Result<Unit>::fail("backup", "backup is not an object");
        const int version = parsed.value("version", 0);
        if (version < 1 || version > 1)
            return Result<Unit>::fail("backup", "unsupported backup version");
        if (!parsed.contains("events") || !parsed["events"].is_array())
            return Result<Unit>::fail("backup", "backup has no events");

        std::vector<Event> events;
        try {
            events.reserve(parsed["events"].size());
            for (const auto& item : parsed["events"]) events.push_back(event_from_json(item));
        } catch (const std::exception& e) {
            return Result<Unit>::fail("backup", std::string("invalid event: ") + e.what());
        }

        ledger_.reset();
        for (const Event& event : events) {
            auto applied = ledger_.apply(event);
            if (!applied) {
                ledger_.reset();
                store_.load(ledger_);
                return Result<Unit>::fail("backup",
                                          "invalid event chain: " + applied.error->message);
            }
        }

        Settings restored = settings_;
        if (parsed.contains("settings") && parsed["settings"].is_object()) {
            restored = settings_from_json(parsed["settings"], settings_);
            auto valid = validate_settings(restored);
            if (!valid) {
                ledger_.reset();
                store_.load(ledger_);
                return valid;
            }
        }
        restored.admin = settings_.admin;  // never restore admin credentials from a backup

        auto compacted = store_.compact(ledger_);
        if (!compacted) {
            ledger_.reset();
            store_.load(ledger_);
            return compacted;
        }
        auto saved = settings_store_.save(restored);
        if (!saved) return saved;
        settings_ = restored;
        platform_.apply_display_settings(settings_.display);

        return audit_locked("backup_restored", "{}", request_id, "admin");
    });
}

void KaffeeServiceImpl::set_change_callback(ChangeCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    change_callback_ = std::move(callback);
}

void KaffeeServiceImpl::refresh() {
    notify_changed();
}

// ---------------------------------------------------------------------------
// Factory

Result<std::unique_ptr<KaffeeService>> KaffeeService::create(IEventStore& store,
                                                             ISettingsStore& settings_store,
                                                             IPlatform& platform) {
    auto service =
        std::unique_ptr<KaffeeServiceImpl>(new KaffeeServiceImpl(store, settings_store, platform));
    auto initialized = service->init();
    if (!initialized) {
        return Result<std::unique_ptr<KaffeeService>>::fail(initialized.error->code,
                                                            initialized.error->message);
    }
    return Result<std::unique_ptr<KaffeeService>>::ok(std::move(service));
}

}  // namespace kaffee
