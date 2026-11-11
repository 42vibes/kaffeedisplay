#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace kaffee {

// Host-testable network helpers. This header must never include ESP-IDF or
// FreeRTOS headers so the unit tests can compile it for the host.

inline constexpr int kApDefaultWindowSeconds = 15 * 60;
inline constexpr int kApMaxWindowSeconds = 4 * 60 * 60;
inline constexpr char kCaptivePortalIp[] = "192.168.4.1";
inline constexpr char kDefaultHostname[] = "kaffeekasse";

// Wall-clock bounds. The device has no RTC, but a timestamp outside this range
// is never plausible and must not be written to the ledger.
inline constexpr int64_t kTimeMinEpochSeconds = 1704067200;  // 2024-01-01Z
inline constexpr int64_t kTimeMaxEpochSeconds = 4102444800;  // 2100-01-01Z
inline constexpr int64_t kMaxFutureSkewSeconds = 24 * 60 * 60;

// Bounded AP lifetime. The timer is driven by a monotonic clock supplied by the
// caller (uptime seconds on the device), never by the wall clock.
class ApTimer {
  public:
    explicit ApTimer(int default_window_s = kApDefaultWindowSeconds);

    // Starts the window. A non-positive value uses the default window.
    void start(int64_t now_s, int window_s = 0);
    // Adds time to an active window. Returns false when the AP is not active.
    bool extend(int64_t now_s, int seconds);
    void stop();

    bool active() const {
        return active_;
    }
    int64_t deadline() const {
        return deadline_s_;
    }
    int remaining(int64_t now_s) const;
    // Returns true exactly once when an active window expires.
    bool tick(int64_t now_s);

    void set_default_window(int seconds);
    int default_window() const {
        return default_window_s_;
    }

  private:
    int default_window_s_;
    int64_t deadline_s_ = 0;
    bool active_ = false;
};

// WIFI QR payload as understood by Android/iOS camera apps:
// WIFI:T:WPA;S:<ssid>;P:<password>;;
// Reserved characters are backslash-escaped.
std::string wifi_qr_payload(std::string_view ssid, std::string_view password);

// "http://<host>" or "http://<host>:<port>".
std::string http_url(std::string_view host, uint16_t port = 80);

// True for the well-known OS captive-portal probe paths. The HTTP layer uses
// this to decide whether a request may receive a redirect/204 answer.
bool is_captive_probe_path(std::string_view path);

// 2024..2100 and, when a reference is known, at most 24 h in the future.
// Pass reference_now = 0 when the local clock is not trusted yet.
bool is_time_sane(int64_t epoch_seconds, int64_t reference_now = 0);

// Europe/Berlin offset in seconds (3600 CET, 7200 CEST) for a UTC timestamp.
int berlin_utc_offset_seconds(int64_t epoch_seconds);

}  // namespace kaffee
