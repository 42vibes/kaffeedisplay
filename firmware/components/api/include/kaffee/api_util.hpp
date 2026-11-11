#pragma once

// Pure helpers for the HTTP API. Nothing in here depends on ESP-IDF so the
// routing, header, CSV and time helpers can be unit-tested on the host.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "kaffee/domain.hpp"

namespace kaffee {
namespace api {

// --- Routing -------------------------------------------------------------

// Route parameters keyed by the placeholder name, e.g. "/users/{id}".
class RouteParams {
  public:
    void set(std::string name, std::string value);
    std::optional<std::string> get(std::string_view name) const;
    const std::vector<std::pair<std::string, std::string>>& values() const {
        return values_;
    }

  private:
    std::vector<std::pair<std::string, std::string>> values_;
};

// Matches one concrete path against a pattern with `{name}` placeholders.
// A placeholder matches exactly one non-empty segment. A single trailing slash
// is ignored on both sides. Returns false and leaves `params` untouched on a
// mismatch.
bool match_path(std::string_view pattern, std::string_view path, RouteParams& params);

// --- Query strings and cookies -------------------------------------------

std::string url_decode(std::string_view input);
std::string url_encode(std::string_view input);

// Returns nullopt when the key is absent, an empty string when it is present
// without a value (`?flag`).
std::optional<std::string> query_param(std::string_view query, std::string_view key);
bool query_flag(std::string_view query, std::string_view key, bool fallback);
std::optional<int64_t> query_int(std::string_view query, std::string_view key);

// Parses a Cookie header ("a=1; b=2") and returns the decoded value of `name`.
std::optional<std::string> cookie_value(std::string_view cookie_header, std::string_view name);

// --- JSON errors ---------------------------------------------------------

// Serialised `{"code": "...", "message": "..."}`.
std::string json_error_body(std::string_view code, std::string_view message);

// HTTP status for a service error code. Unknown codes map to 500 so internal
// details never leak as a false success.
int http_status_for_error(std::string_view code);

bool is_write_method(std::string_view method);

// --- Request ids ---------------------------------------------------------

// `X-Request-Id` must be a short, printable ASCII token (UUIDs included).
bool is_valid_request_id(std::string_view id);

// --- HTTP helpers --------------------------------------------------------

bool accepts_gzip(std::string_view accept_encoding);
// Fingerprinted build artefacts (Vite emits `index-<hash>.js`) are immutable.
bool is_immutable_asset(std::string_view path);
std::string content_disposition(std::string_view filename);
std::string etag_for(std::string_view body);
bool etag_matches(std::string_view if_none_match, std::string_view etag);

// QR payloads used by the admin SPA / device page.
std::string wifi_qr_payload(std::string_view ssid, std::string_view password);
std::string url_qr_payload(std::string_view url);

// --- CSV -----------------------------------------------------------------

// Quotes fields that contain the delimiter, quotes or line breaks, doubles
// embedded quotes and prefixes spreadsheet formula starters with a single
// quote (formula-injection protection). Numeric values stay untouched so
// negative cent amounts keep working.
std::string csv_escape(std::string_view field, char delimiter);
std::string csv_row(const std::vector<std::string>& fields, char delimiter);
std::string csv_header(const std::vector<std::string>& fields, char delimiter);

// --- Time ----------------------------------------------------------------

// Civil date arithmetic (proleptic Gregorian, UTC).
int64_t days_from_civil(int year, unsigned month, unsigned day);
void civil_from_days(int64_t days, int& year, unsigned& month, unsigned& day);

std::string format_timestamp_utc(Timestamp ts);  // YYYY-MM-DD HH:MM:SS
std::string format_date_utc(Timestamp ts);       // YYYY-MM-DD
std::string backup_filename(Timestamp ts);       // kaffeekasse-backup-YYYY-MM-DD.json

// UTC offset of Europe/Berlin at the given instant (3600 or 7200 seconds).
int berlin_utc_offset(Timestamp utc_seconds);

struct TimeRange {
    Timestamp from = 0;  // inclusive
    Timestamp to = 0;    // exclusive
};
std::optional<TimeRange> month_range_utc(int year, int month);
std::optional<TimeRange> parse_month_range(std::string_view yyyy_mm);

// --- Misc ----------------------------------------------------------------

std::string trim(std::string_view value);
std::string lower_ascii(std::string_view value);
std::string hex_lower(const uint8_t* data, size_t size);
bool is_valid_color(std::string_view color);
bool parse_bool_text(std::string_view value, bool& out);

}  // namespace api
}  // namespace kaffee
