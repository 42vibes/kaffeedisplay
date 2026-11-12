#include "kaffee/api_util.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace kaffee {
namespace api {
namespace {

bool is_unreserved(unsigned char c) {
    return std::isalnum(c) != 0 || c == '-' || c == '.' || c == '_' || c == '~';
}

int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string_view strip_trailing_slash(std::string_view path) {
    if (path.size() > 1 && path.back() == '/') path.remove_suffix(1);
    return path;
}

std::vector<std::string_view> split_segments(std::string_view path) {
    std::vector<std::string_view> out;
    size_t start = 0;
    while (start <= path.size()) {
        size_t next = path.find('/', start);
        if (next == std::string_view::npos) next = path.size();
        out.push_back(path.substr(start, next - start));
        start = next + 1;
    }
    return out;
}

bool looks_numeric(std::string_view value) {
    if (value.empty()) return false;
    size_t i = 0;
    if (value[0] == '-' || value[0] == '+') i = 1;
    if (i >= value.size()) return false;
    bool digits = false;
    for (; i < value.size(); ++i) {
        if (std::isdigit(static_cast<unsigned char>(value[i])) != 0) {
            digits = true;
        } else if (value[i] != '.' && value[i] != ',') {
            return false;
        }
    }
    return digits;
}

bool needs_quotes(std::string_view value, char delimiter) {
    return value.find(delimiter) != std::string_view::npos ||
           value.find('"') != std::string_view::npos ||
           value.find('\n') != std::string_view::npos || value.find('\r') != std::string_view::npos;
}

int64_t floor_div(int64_t value, int64_t divisor) {
    int64_t q = value / divisor;
    if ((value % divisor != 0) && ((value < 0) != (divisor < 0))) --q;
    return q;
}

int last_sunday(int year, unsigned month) {
    static constexpr unsigned kLastDay[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    unsigned last = kLastDay[month - 1];
    if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) last = 29;
    int64_t days = days_from_civil(year, month, last);
    // 1970-01-01 was a Thursday; 0 == Sunday.
    int weekday = static_cast<int>(((days % 7) + 7 + 4) % 7);
    return static_cast<int>(last) - weekday;
}

}  // namespace

// --- RouteParams ---------------------------------------------------------

void RouteParams::set(std::string name, std::string value) {
    values_.emplace_back(std::move(name), std::move(value));
}

std::optional<std::string> RouteParams::get(std::string_view name) const {
    for (const auto& entry : values_) {
        if (entry.first == name) return entry.second;
    }
    return std::nullopt;
}

bool match_path(std::string_view pattern, std::string_view path, RouteParams& params) {
    pattern = strip_trailing_slash(pattern);
    path = strip_trailing_slash(path);
    auto pattern_segments = split_segments(pattern);
    auto path_segments = split_segments(path);
    if (pattern_segments.size() != path_segments.size()) return false;

    RouteParams parsed;
    for (size_t i = 0; i < pattern_segments.size(); ++i) {
        std::string_view expected = pattern_segments[i];
        std::string_view actual = path_segments[i];
        if (expected.size() >= 2 && expected.front() == '{' && expected.back() == '}') {
            if (actual.empty()) return false;
            std::string name(expected.substr(1, expected.size() - 2));
            parsed.set(std::move(name), url_decode(actual));
            continue;
        }
        if (expected != actual) return false;
    }
    params = std::move(parsed);
    return true;
}

// --- Query strings and cookies -------------------------------------------

std::string url_decode(std::string_view input) {
    std::string out;
    out.reserve(input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        char c = input[i];
        if (c == '+') {
            out.push_back(' ');
        } else if (c == '%' && i + 2 < input.size()) {
            int hi = hex_value(input[i + 1]);
            int lo = hex_value(input[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
            } else {
                out.push_back(c);
            }
        } else {
            out.push_back(c);
        }
    }
    return out;
}

std::string url_encode(std::string_view input) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(input.size());
    for (unsigned char c : input) {
        if (is_unreserved(c)) {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(kHex[c >> 4]);
            out.push_back(kHex[c & 0x0f]);
        }
    }
    return out;
}

std::optional<std::string> query_param(std::string_view query, std::string_view key) {
    size_t pos = 0;
    while (pos <= query.size()) {
        size_t amp = query.find('&', pos);
        if (amp == std::string_view::npos) amp = query.size();
        std::string_view pair = query.substr(pos, amp - pos);
        size_t eq = pair.find('=');
        std::string_view raw_key = eq == std::string_view::npos ? pair : pair.substr(0, eq);
        if (url_decode(raw_key) == key) {
            if (eq == std::string_view::npos) return std::string();
            return url_decode(pair.substr(eq + 1));
        }
        pos = amp + 1;
    }
    return std::nullopt;
}

bool parse_bool_text(std::string_view value, bool& out) {
    std::string lower = lower_ascii(value);
    if (lower.empty()) return false;
    if (lower == "1" || lower == "true" || lower == "yes" || lower == "on" || lower == "ja") {
        out = true;
        return true;
    }
    if (lower == "0" || lower == "false" || lower == "no" || lower == "off" || lower == "nein") {
        out = false;
        return true;
    }
    return false;
}

bool query_flag(std::string_view query, std::string_view key, bool fallback) {
    auto value = query_param(query, key);
    if (!value) return fallback;
    bool parsed = fallback;
    if (parse_bool_text(*value, parsed)) return parsed;
    return fallback;
}

std::optional<int64_t> query_int(std::string_view query, std::string_view key) {
    auto value = query_param(query, key);
    if (!value || value->empty()) return std::nullopt;
    try {
        size_t consumed = 0;
        long long parsed = std::stoll(*value, &consumed);
        if (consumed != value->size()) return std::nullopt;
        return static_cast<int64_t>(parsed);
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::string> cookie_value(std::string_view cookie_header, std::string_view name) {
    size_t pos = 0;
    while (pos <= cookie_header.size()) {
        size_t semi = cookie_header.find(';', pos);
        if (semi == std::string_view::npos) semi = cookie_header.size();
        std::string pair = trim(cookie_header.substr(pos, semi - pos));
        size_t eq = pair.find('=');
        if (eq != std::string_view::npos) {
            std::string key = trim(pair.substr(0, eq));
            if (key == name) {
                std::string value = trim(pair.substr(eq + 1));
                if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
                    value = value.substr(1, value.size() - 2);
                }
                return std::string(value);
            }
        }
        pos = semi + 1;
    }
    return std::nullopt;
}

// --- JSON errors ---------------------------------------------------------

std::string json_error_body(std::string_view code, std::string_view message) {
    auto escape = [](std::string_view text) {
        std::string out;
        out.reserve(text.size() + 8);
        for (unsigned char c : text) {
            switch (c) {
                case '"':
                    out += "\\\"";
                    break;
                case '\\':
                    out += "\\\\";
                    break;
                case '\n':
                    out += "\\n";
                    break;
                case '\r':
                    out += "\\r";
                    break;
                case '\t':
                    out += "\\t";
                    break;
                default:
                    if (c < 0x20) {
                        char buf[8];
                        std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                        out += buf;
                    } else {
                        out.push_back(static_cast<char>(c));
                    }
                    break;
            }
        }
        return out;
    };
    std::string body = "{\"code\":\"";
    body += escape(code);
    body += "\",\"message\":\"";
    body += escape(message);
    body += "\"}";
    return body;
}

int http_status_for_error(std::string_view code) {
    std::string key = lower_ascii(code);
    if (key == "invalid_credentials" || key == "unauthorized" || key == "not_authenticated" ||
        key == "session_expired" || key == "invalid_session") {
        return 401;
    }
    if (key == "forbidden" || key == "csrf_failed" || key == "csrf_invalid" ||
        key == "password_change_required") {
        return 403;
    }
    if (key == "not_found") return 404;
    if (key == "conflict" || key == "already_exists" || key == "already_voided" ||
        key == "duplicate" || key == "request_id_conflict" || key == "ap_already_active") {
        return 409;
    }
    if (key == "payload_too_large" || key == "body_too_large") return 413;
    if (key == "method_not_allowed") return 405;
    if (key == "unsupported_media_type") return 415;
    if (key == "invalid_image" || key == "checksum_mismatch" || key == "missing_request_id" ||
        key == "invalid_request_id" || key == "invalid_json" || key == "checksum_invalid") {
        return 400;
    }
    if (key == "rate_limited" || key == "too_many_requests" || key == "locked_out") return 429;
    if (key == "not_implemented") return 501;
    if (key == "unavailable" || key == "network_unavailable" || key == "storage_unavailable") {
        return 503;
    }
    if (key == "invalid_argument" || key == "invalid_input" || key == "bad_request" ||
        key == "validation_failed" || key == "out_of_range" || key == "missing_field") {
        return 400;
    }
    return 500;
}

bool is_write_method(std::string_view method) {
    return method == "POST" || method == "PUT" || method == "PATCH" || method == "DELETE";
}

ErrorMapping map_service_error(const Error& error) {
    const std::string code = lower_ascii(error.code);
    const std::string text = lower_ascii(error.message);
    auto says = [&text](std::string_view needle) { return text.find(needle) != std::string::npos; };

    // API-generated errors already carry a German message (validation, CSRF,
    // limits). Keep it; service codes are mapped below.
    static constexpr std::string_view kApiCodes[] = {
        "invalid_input",
        "missing_request_id",
        "invalid_request_id",
        "csrf_failed",
        "payload_too_large",
        "not_implemented",
        "method_not_allowed",
        "unsupported_media_type",
        "invalid_image",
        "checksum_mismatch",
        "password_change_required",
        "unauthorized",
        "forbidden",
        "not_found",
        "conflict",
    };
    for (std::string_view api_code : kApiCodes) {
        if (code == api_code) {
            ErrorMapping mapping;
            mapping.status = http_status_for_error(code);
            mapping.code = code;
            mapping.message = error.message.empty() ? "Eingabe ist ungültig" : error.message;
            return mapping;
        }
    }

    // Real KaffeeService/ledger codes. The service message is only inspected to
    // disambiguate overloaded codes; it is never forwarded to the client.
    if (code == "auth") return {401, "auth", "Anmeldung fehlgeschlagen"};
    if (code == "rate_limit" || code == "rate_limited") {
        return {429, "rate_limit", "Zu viele Versuche, bitte später erneut versuchen"};
    }
    if (code == "session") return {401, "session", "Sitzung abgelaufen"};
    if (code == "password") {
        return {400, "password", "Passwort erfüllt die Anforderungen nicht"};
    }
    if (code == "backup") return {400, "backup", "Sicherung ist ungültig"};
    if (code == "settings") return {400, "settings", "Einstellungen sind ungültig"};
    if (code == "debt") return {409, "debt", "Kreditlimit überschritten"};
    if (code == "duplicate") return {409, "duplicate", "Vorgang wurde bereits gebucht"};
    if (code == "amount" || code == "price") {
        return {400, code, "Betrag ist ungültig"};
    }
    if (code == "time") return {400, "time", "Zeitangabe ist ungültig"};
    if (code == "ap") return {400, "ap", "Zugangspunkt-Anfrage ist ungültig"};
    if (code == "void") {
        if (says("not found") || says("unknown")) {
            return {404, "void", "Buchung nicht gefunden"};
        }
        return {409, "void", "Buchung kann nicht storniert werden"};
    }
    if (code == "event") return {404, "event", "Buchung nicht gefunden"};
    if (code == "expense") {
        if (says("unknown") || says("not found")) {
            return {404, "expense", "Ausgabe nicht gefunden"};
        }
        if (says("already") || says("voided")) {
            return {409, "expense", "Ausgabe kann nicht geändert werden"};
        }
        return {400, "expense", "Ausgabe ist ungültig"};
    }
    if (code == "user") {
        if (says("unknown") || says("not found")) {
            return {404, "user", "Benutzer nicht gefunden"};
        }
        if (says("already") || says("exists")) {
            return {409, "user", "Benutzer existiert bereits"};
        }
        if (says("archived")) return {409, "user", "Benutzer ist archiviert"};
        return {400, "user", "Benutzerangaben sind ungültig"};
    }
    if (code == "product") {
        if (says("unknown") || says("not found")) {
            return {404, "product", "Produkt nicht gefunden"};
        }
        if (says("already") || says("exists")) {
            return {409, "product", "Produkt existiert bereits"};
        }
        if (says("archived")) return {409, "product", "Produkt ist archiviert"};
        return {400, "product", "Produktangaben sind ungültig"};
    }
    if (code == "name" || code == "type" || code == "seq" || code == "id") {
        return {400, code, "Eingabe ist ungültig"};
    }
    if (code == "chain" || code == "hash") {
        return {500, "internal_error", "Interner Datenfehler"};
    }

    const int status = http_status_for_error(code);
    ErrorMapping mapping;
    mapping.status = status;
    mapping.code = status == 500 || code.empty() ? "internal_error" : code;
    switch (status) {
        case 400:
            mapping.message = "Eingabe ist ungültig";
            break;
        case 401:
            mapping.message = "Anmeldung erforderlich";
            break;
        case 403:
            mapping.message = "Zugriff verweigert";
            break;
        case 404:
            mapping.message = "Ressource nicht gefunden";
            break;
        case 409:
            mapping.message = "Konflikt";
            break;
        case 413:
            mapping.message = "Anfrage ist zu groß";
            break;
        case 429:
            mapping.message = "Zu viele Anfragen, bitte später erneut versuchen";
            break;
        case 501:
            mapping.message = "Nicht verfügbar";
            break;
        case 503:
            mapping.message = "Vorübergehend nicht verfügbar";
            break;
        default:
            mapping.message = "Interner Fehler";
            break;
    }
    return mapping;
}

// --- Request ids ---------------------------------------------------------

bool is_valid_request_id(std::string_view id) {
    if (id.empty() || id.size() > 64) return false;
    for (unsigned char c : id) {
        if (c < 0x21 || c > 0x7e) return false;
    }
    return true;
}

// --- HTTP helpers --------------------------------------------------------

bool accepts_gzip(std::string_view accept_encoding) {
    size_t pos = 0;
    while (pos < accept_encoding.size()) {
        size_t comma = accept_encoding.find(',', pos);
        if (comma == std::string_view::npos) comma = accept_encoding.size();
        std::string token = lower_ascii(trim(accept_encoding.substr(pos, comma - pos)));
        pos = comma + 1;
        if (token.empty()) continue;
        bool disabled = false;
        size_t semi = token.find(';');
        if (semi != std::string::npos) {
            std::string params = token.substr(semi + 1);
            token = trim(token.substr(0, semi));
            size_t q = params.find("q=");
            if (q != std::string::npos) {
                double quality = 1.0;
                try {
                    quality = std::stod(params.substr(q + 2));
                } catch (...) {
                    quality = 1.0;
                }
                disabled = quality <= 0.0;
            }
        }
        if ((token == "gzip" || token == "*") && !disabled) return true;
    }
    return false;
}

bool is_immutable_asset(std::string_view path) {
    if (path.rfind("/assets/", 0) != 0) return false;
    size_t slash = path.find_last_of('/');
    std::string_view name = slash == std::string_view::npos ? path : path.substr(slash + 1);
    size_t dot = name.find_last_of('.');
    std::string_view stem = dot == std::string_view::npos ? name : name.substr(0, dot);
    size_t dash = stem.find_last_of('-');
    if (dash == std::string_view::npos) return false;
    std::string_view hash = stem.substr(dash + 1);
    if (hash.size() < 8) return false;
    for (char c : hash) {
        if (std::isalnum(static_cast<unsigned char>(c)) == 0) return false;
    }
    return true;
}

bool is_spa_fallback_path(std::string_view path) {
    if (path.empty() || path == "/") return true;
    size_t slash = path.find_last_of('/');
    std::string_view name = slash == std::string_view::npos ? path : path.substr(slash + 1);
    return name.find('.') == std::string_view::npos;
}

bool static_method_allowed(std::string_view method) {
    return method == "GET";
}

StaticPolicy static_policy(std::string_view request_path, bool is_index_asset,
                           std::string_view method) {
    StaticPolicy policy;
    policy.send_body = method == "GET";
    if (is_index_asset || is_spa_fallback_path(request_path)) {
        policy.cache_control = "no-cache";
        policy.immutable = false;
        return policy;
    }
    if (is_immutable_asset(request_path)) {
        policy.cache_control = "public, max-age=31536000, immutable";
        policy.immutable = true;
        return policy;
    }
    policy.cache_control = "public, max-age=3600";
    return policy;
}

CaptiveProbe captive_probe_kind(std::string_view path) {
    if (path == "/generate_204") return CaptiveProbe::Generate204;
    if (path == "/hotspot-detect.html" || path == "/connecttest.txt" || path == "/ncsi.txt") {
        return CaptiveProbe::Redirect;
    }
    return CaptiveProbe::None;
}

std::string content_disposition(std::string_view filename) {
    std::string out = "attachment; filename=\"";
    for (char c : filename) {
        if (c == '"' || c == '\\')
            out.push_back('_');
        else if (static_cast<unsigned char>(c) < 0x20)
            out.push_back('_');
        else
            out.push_back(c);
    }
    out += "\"";
    return out;
}

std::string etag_for(std::string_view body) {
    uint64_t hash = 1469598103934665603ULL;
    for (unsigned char c : body) {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "\"%016llx\"", static_cast<unsigned long long>(hash));
    return buf;
}

bool etag_matches(std::string_view if_none_match, std::string_view etag) {
    std::string value = trim(if_none_match);
    if (value == "*") return true;
    size_t pos = 0;
    while (pos < value.size()) {
        size_t comma = value.find(',', pos);
        if (comma == std::string_view::npos) comma = value.size();
        std::string candidate = trim(value.substr(pos, comma - pos));
        if (candidate.rfind("W/", 0) == 0) candidate = candidate.substr(2);
        if (candidate == etag) return true;
        pos = comma + 1;
    }
    return false;
}

std::string wifi_qr_payload(std::string_view ssid, std::string_view password) {
    auto escape = [](std::string_view value) {
        std::string out;
        out.reserve(value.size() + 4);
        for (char c : value) {
            if (c == '\\' || c == ';' || c == ',' || c == ':' || c == '"') out.push_back('\\');
            out.push_back(c);
        }
        return out;
    };
    std::string out = "WIFI:T:WPA;S:";
    out += escape(ssid);
    out += ";P:";
    out += escape(password);
    out += ";;";
    return out;
}

std::string url_qr_payload(std::string_view url) {
    return std::string(url);
}

// --- CSV -----------------------------------------------------------------

std::string csv_escape(std::string_view field, char delimiter) {
    std::string value(field);
    if (!value.empty()) {
        char first = value.front();
        bool formula =
            first == '=' || first == '+' || first == '@' || first == '\t' || first == '\r';
        if (first == '-') formula = !looks_numeric(value);
        if (formula) value.insert(value.begin(), '\'');
    }
    if (!needs_quotes(value, delimiter)) return value;
    std::string out;
    out.reserve(value.size() + 4);
    out.push_back('"');
    for (char c : value) {
        if (c == '"') out.push_back('"');
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

std::string csv_row(const std::vector<std::string>& fields, char delimiter) {
    std::string out;
    for (size_t i = 0; i < fields.size(); ++i) {
        if (i > 0) out.push_back(delimiter);
        out += csv_escape(fields[i], delimiter);
    }
    out += "\r\n";
    return out;
}

std::string csv_header(const std::vector<std::string>& fields, char delimiter) {
    return csv_row(fields, delimiter);
}

// --- Time ----------------------------------------------------------------

int64_t days_from_civil(int year, unsigned month, unsigned day) {
    int64_t y = year;
    y -= month <= 2 ? 1 : 0;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153u * (month + (month > 2 ? -3u : 9u)) + 2u) / 5u + day - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

void civil_from_days(int64_t days, int& year, unsigned& month, unsigned& day) {
    days += 719468;
    const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(days - era * 146097);
    const unsigned yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
    const int64_t y = static_cast<int64_t>(yoe) + era * 400;
    const unsigned doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    const unsigned mp = (5u * doy + 2u) / 153u;
    const unsigned d = doy - (153u * mp + 2u) / 5u + 1u;
    const unsigned m = mp + (mp < 10u ? 3u : -9u);
    year = static_cast<int>(y + (m <= 2u ? 1 : 0));
    month = m;
    day = d;
}

std::string format_timestamp_utc(Timestamp ts) {
    int64_t days = floor_div(ts, 86400);
    int64_t rem = ts - days * 86400;
    int year = 0;
    unsigned month = 0;
    unsigned day = 0;
    civil_from_days(days, year, month, day);
    int hours = static_cast<int>(rem / 3600);
    int minutes = static_cast<int>((rem % 3600) / 60);
    int seconds = static_cast<int>(rem % 60);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02u-%02u %02d:%02d:%02d", year, month, day, hours,
                  minutes, seconds);
    return buf;
}

std::string format_date_utc(Timestamp ts) {
    int year = 0;
    unsigned month = 0;
    unsigned day = 0;
    civil_from_days(floor_div(ts, 86400), year, month, day);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04d-%02u-%02u", year, month, day);
    return buf;
}

std::string backup_filename(Timestamp ts) {
    return "kaffeekasse-backup-" + format_date_utc(ts) + ".json";
}

int berlin_utc_offset(Timestamp utc_seconds) {
    int year = 0;
    unsigned month = 0;
    unsigned day = 0;
    civil_from_days(floor_div(utc_seconds, 86400), year, month, day);
    int start_day = last_sunday(year, 3);
    int end_day = last_sunday(year, 10);
    Timestamp dst_start = days_from_civil(year, 3, static_cast<unsigned>(start_day)) * 86400 + 3600;
    Timestamp dst_end = days_from_civil(year, 10, static_cast<unsigned>(end_day)) * 86400 + 3600;
    if (utc_seconds >= dst_start && utc_seconds < dst_end) return 7200;
    return 3600;
}

std::optional<TimeRange> month_range_utc(int year, int month) {
    if (year < 1970 || year > 2200 || month < 1 || month > 12) return std::nullopt;
    int next_year = year;
    int next_month = month + 1;
    if (next_month > 12) {
        next_month = 1;
        ++next_year;
    }
    const int64_t start_local = days_from_civil(year, static_cast<unsigned>(month), 1) * 86400;
    const int64_t end_local =
        days_from_civil(next_year, static_cast<unsigned>(next_month), 1) * 86400;
    // Local midnight is always on the same side of the 01:00 UTC DST switch.
    TimeRange range;
    range.from = start_local - berlin_utc_offset(start_local - 3600);
    range.to = end_local - berlin_utc_offset(end_local - 3600);
    return range;
}

std::optional<TimeRange> parse_month_range(std::string_view yyyy_mm) {
    if (yyyy_mm.size() != 7 || yyyy_mm[4] != '-') return std::nullopt;
    for (size_t i = 0; i < yyyy_mm.size(); ++i) {
        if (i == 4) continue;
        if (std::isdigit(static_cast<unsigned char>(yyyy_mm[i])) == 0) return std::nullopt;
    }
    int year = 0;
    int month = 0;
    for (int i = 0; i < 4; ++i) year = year * 10 + (yyyy_mm[i] - '0');
    month = (yyyy_mm[5] - '0') * 10 + (yyyy_mm[6] - '0');
    return month_range_utc(year, month);
}

// --- Misc ----------------------------------------------------------------

std::string trim(std::string_view value) {
    size_t begin = 0;
    size_t end = value.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(value[begin])) != 0) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) --end;
    return std::string(value.substr(begin, end - begin));
}

std::string lower_ascii(std::string_view value) {
    std::string out(value);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::string hex_lower(const uint8_t* data, size_t size) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(size * 2);
    for (size_t i = 0; i < size; ++i) {
        out.push_back(kHex[data[i] >> 4]);
        out.push_back(kHex[data[i] & 0x0f]);
    }
    return out;
}

bool is_valid_color(std::string_view color) {
    if (color.size() != 7 || color.front() != '#') return false;
    for (size_t i = 1; i < color.size(); ++i) {
        if (std::isxdigit(static_cast<unsigned char>(color[i])) == 0) return false;
    }
    return true;
}

bool contains_secret_marker(std::string_view line) {
    // Allocation-free: this runs inside the log hook.
    static constexpr std::string_view kMarkers[] = {
        "password",      "passwort", "secret", "token",       "csrf",
        "authorization", "cookie",   "psk=",   "ap_password", "sta_password",
    };
    for (std::string_view marker : kMarkers) {
        if (marker.size() > line.size()) continue;
        for (size_t pos = 0; pos + marker.size() <= line.size(); ++pos) {
            bool match = true;
            for (size_t i = 0; i < marker.size(); ++i) {
                const char c =
                    static_cast<char>(std::tolower(static_cast<unsigned char>(line[pos + i])));
                if (c != marker[i]) {
                    match = false;
                    break;
                }
            }
            if (match) return true;
        }
    }
    return false;
}

nlohmann::json time_info_json(const TimeInfo& time) {
    return nlohmann::json{{"now", time.now},
                          {"source", Ledger::time_source_name(time.source)},
                          {"last_sync", time.last_sync},
                          {"last_source", time.last_source},
                          {"trusted", time.trusted}};
}

nlohmann::json device_status_json(const SystemStats& stats) {
    nlohmann::json network{{"ap_active", stats.network.ap_active},
                           {"ap_starting", stats.network.ap_starting},
                           {"ap_remaining_s", stats.network.ap_remaining_s},
                           {"ap_ssid", stats.network.ap_ssid},
                           {"ap_ip", stats.network.ap_ip},
                           {"sta_connected", stats.network.sta_connected},
                           {"sta_ip", stats.network.sta_ip},
                           {"sta_rssi", stats.network.sta_rssi}};
    return nlohmann::json{{"firmware_version", stats.firmware_version},
                          {"build_date", stats.build_date},
                          {"uptime_s", stats.uptime_s},
                          {"free_heap", stats.free_heap},
                          {"min_free_heap", stats.min_free_heap},
                          {"free_psram", stats.free_psram},
                          {"storage_used", stats.storage.used_bytes},
                          {"storage_total", stats.storage.total_bytes},
                          {"storage_mounted", stats.storage.mounted},
                          {"storage_degraded", stats.storage.degraded},
                          {"temperature_c", stats.chip_temperature_c},
                          {"temperature_valid", stats.temperature_valid},
                          {"mac", stats.mac},
                          {"network", std::move(network)},
                          {"time", time_info_json(stats.time)},
                          {"last_backup_at", stats.last_backup_at},
                          {"drinks_today", stats.drinks_today}};
}

}  // namespace api
}  // namespace kaffee
