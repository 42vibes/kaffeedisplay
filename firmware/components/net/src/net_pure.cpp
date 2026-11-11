#include "kaffee/net_pure.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace kaffee {
namespace {

int clamp_window(int seconds) {
    if (seconds <= 0) {
        return kApDefaultWindowSeconds;
    }
    return std::min(seconds, kApMaxWindowSeconds);
}

// Howard Hinnant's civil calendar algorithms, valid for the whole int64 range.
int64_t days_from_civil(int year, unsigned month, unsigned day) {
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(year - era * 400);
    const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
}

void civil_from_days(int64_t days, int& year, unsigned& month, unsigned& day) {
    days += 719468;
    const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(days - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    year = static_cast<int>(yoe) + static_cast<int>(era) * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    day = doy - (153 * mp + 2) / 5 + 1;
    month = mp + (mp < 10 ? 3 : -9);
    year += month <= 2;
}

bool is_leap_year(int year) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

unsigned days_in_month(int year, unsigned month) {
    static constexpr std::array<unsigned, 12> kDays{31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && is_leap_year(year)) {
        return 29;
    }
    return kDays[month - 1];
}

// 0 = Sunday.
int weekday_from_days(int64_t days) {
    return static_cast<int>(((days + 4) % 7 + 7) % 7);
}

int64_t last_sunday_of_month(int year, unsigned month) {
    const int64_t last_day = days_from_civil(year, month, days_in_month(year, month));
    return last_day - weekday_from_days(last_day);
}

std::string escape_wifi_field(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (char c : value) {
        if (c == '\\' || c == ';' || c == ',' || c == ':' || c == '"') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    return out;
}

}  // namespace

ApTimer::ApTimer(int default_window_s) : default_window_s_(clamp_window(default_window_s)) {}

void ApTimer::start(int64_t now_s, int window_s) {
    const int window = clamp_window(window_s > 0 ? window_s : default_window_s_);
    deadline_s_ = now_s + window;
    active_ = true;
}

bool ApTimer::extend(int64_t now_s, int seconds) {
    if (!active_ || seconds <= 0) {
        return false;
    }
    const int64_t addition = std::min<int64_t>(seconds, kApMaxWindowSeconds);
    const int64_t maximum_deadline = now_s + kApMaxWindowSeconds;
    const int64_t current_deadline = std::max(now_s, deadline_s_);
    deadline_s_ = std::min(maximum_deadline, current_deadline + addition);
    return true;
}

void ApTimer::stop() {
    active_ = false;
    deadline_s_ = 0;
}

int ApTimer::remaining(int64_t now_s) const {
    if (!active_) {
        return 0;
    }
    const int64_t left = deadline_s_ - now_s;
    if (left <= 0) {
        return 0;
    }
    return static_cast<int>(std::min<int64_t>(left, std::numeric_limits<int>::max()));
}

bool ApTimer::tick(int64_t now_s) {
    if (!active_) {
        return false;
    }
    if (now_s < deadline_s_) {
        return false;
    }
    stop();
    return true;
}

void ApTimer::set_default_window(int seconds) {
    default_window_s_ = clamp_window(seconds);
}

std::string wifi_qr_payload(std::string_view ssid, std::string_view password) {
    std::string out;
    out.reserve(ssid.size() + password.size() + 22);
    out += "WIFI:T:WPA;S:";
    out += escape_wifi_field(ssid);
    out += ";P:";
    out += escape_wifi_field(password);
    out += ";;";
    return out;
}

std::string http_url(std::string_view host, uint16_t port) {
    if (host.empty()) {
        return {};
    }
    std::string out = "http://";
    out += host;
    if (port != 80) {
        out.push_back(':');
        out += std::to_string(port);
    }
    return out;
}

bool is_captive_probe_path(std::string_view path) {
    const auto separator = path.find_first_of("?#");
    if (separator != std::string_view::npos) {
        path = path.substr(0, separator);
    }
    static constexpr std::array<std::string_view, 12> kProbePaths{
        "/generate_204",
        "/gen_204",
        "/hotspot-detect.html",
        "/library/test/success.html",
        "/connecttest.txt",
        "/redirect",
        "/ncsi.txt",
        "/success.txt",
        "/canonical.html",
        "/captiveportal.html",
        "/connectivity-check.html",
        "/check_network_status.txt",
    };
    return std::find(kProbePaths.begin(), kProbePaths.end(), path) != kProbePaths.end();
}

bool is_time_sane(int64_t epoch_seconds, int64_t reference_now) {
    if (epoch_seconds < kTimeMinEpochSeconds || epoch_seconds >= kTimeMaxEpochSeconds) {
        return false;
    }
    if (reference_now > 0 && epoch_seconds > reference_now + kMaxFutureSkewSeconds) {
        return false;
    }
    return true;
}

int berlin_utc_offset_seconds(int64_t epoch_seconds) {
    const int64_t days = epoch_seconds / 86400;
    int year = 1970;
    unsigned month = 1;
    unsigned day = 1;
    civil_from_days(days, year, month, day);

    const int64_t start = last_sunday_of_month(year, 3) * 86400 + 3600;
    const int64_t end = last_sunday_of_month(year, 10) * 86400 + 3600;
    return (epoch_seconds >= start && epoch_seconds < end) ? 7200 : 3600;
}

}  // namespace kaffee
