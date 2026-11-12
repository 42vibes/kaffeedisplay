#include "doctest.h"

#include <cstdint>
#include <string>

#include "kaffee/api_util.hpp"
#include "kaffee/sha256.hpp"
#include "sha256_stream.hpp"

using namespace kaffee;
using namespace kaffee::api;

TEST_CASE("match_path handles literals and parameters") {
    RouteParams params;
    CHECK(match_path("/api/v1/health", "/api/v1/health", params));
    CHECK(params.values().empty());

    CHECK(match_path("/api/v1/users/{id}/topup", "/api/v1/users/u-123/topup", params));
    CHECK(params.get("id") == std::optional<std::string>("u-123"));

    CHECK(match_path("/api/v1/reports/{kind}", "/api/v1/reports/consumption", params));
    CHECK(params.get("kind") == std::optional<std::string>("consumption"));

    CHECK_FALSE(match_path("/api/v1/users/{id}", "/api/v1/users/u-1/archive", params));
    CHECK_FALSE(match_path("/api/v1/users/{id}", "/api/v1/users", params));
    CHECK_FALSE(match_path("/api/v1/users/{id}", "/api/v1/products/u-1", params));
    CHECK(match_path("/api/v1/users/{id}", "/api/v1/users/u-1/", params));
    CHECK(params.get("id") == std::optional<std::string>("u-1"));

    // Empty segments do not satisfy a placeholder.
    CHECK_FALSE(match_path("/api/v1/users/{id}", "/api/v1/users/", params));
}

TEST_CASE("match_path decodes percent escapes in parameters") {
    RouteParams params;
    CHECK(match_path("/users/{id}", "/users/a%2Fb", params));
    CHECK(params.get("id") == std::optional<std::string>("a/b"));
}

TEST_CASE("query parsing") {
    const std::string query = "limit=25&type=purchase&flag&name=Kaffee%20Bohne&empty=";
    CHECK(query_param(query, "limit") == std::optional<std::string>("25"));
    CHECK(query_param(query, "name") == std::optional<std::string>("Kaffee Bohne"));
    CHECK(query_param(query, "flag") == std::optional<std::string>(""));
    CHECK(query_param(query, "empty") == std::optional<std::string>(""));
    CHECK_FALSE(query_param(query, "missing").has_value());
    CHECK(query_int(query, "limit") == std::optional<int64_t>(25));
    CHECK_FALSE(query_int(query, "name").has_value());
    CHECK(query_flag("include_archived=true", "include_archived", false));
    CHECK_FALSE(query_flag("include_archived=false", "include_archived", true));
    CHECK(query_flag("", "include_archived", true));
}

TEST_CASE("url_decode and url_encode") {
    CHECK(url_decode("a+b%20c%2Fd") == "a b c/d");
    CHECK(url_decode("100%") == "100%");
    CHECK(url_encode("a b/c") == "a%20b%2Fc");
}

TEST_CASE("cookie parsing") {
    const std::string header = "theme=dark; kaffee_session=abc123; other=\"x; y\"";
    CHECK(cookie_value(header, "kaffee_session") == std::optional<std::string>("abc123"));
    CHECK(cookie_value(header, "theme") == std::optional<std::string>("dark"));
    CHECK(cookie_value("kaffee_session=abc123", "kaffee_session") ==
          std::optional<std::string>("abc123"));
    CHECK_FALSE(cookie_value("", "kaffee_session").has_value());
    CHECK_FALSE(cookie_value("a=1; b=2", "kaffee_session").has_value());
}

TEST_CASE("json error body is escaped") {
    CHECK(json_error_body("not_found", "Nicht gefunden") ==
          "{\"code\":\"not_found\",\"message\":\"Nicht gefunden\"}");
    CHECK(json_error_body("invalid_input", "Zeile \"1\"\nneu") ==
          "{\"code\":\"invalid_input\",\"message\":\"Zeile \\\"1\\\"\\nneu\"}");
}

TEST_CASE("service error codes map to HTTP status") {
    CHECK(http_status_for_error("invalid_input") == 400);
    CHECK(http_status_for_error("not_found") == 404);
    CHECK(http_status_for_error("already_voided") == 409);
    CHECK(http_status_for_error("rate_limited") == 429);
    CHECK(http_status_for_error("unauthorized") == 401);
    CHECK(http_status_for_error("csrf_failed") == 403);
    CHECK(http_status_for_error("not_implemented") == 501);
    CHECK(http_status_for_error("something_else") == 500);
    CHECK(http_status_for_error("INVALID_INPUT") == 400);
}

TEST_CASE("request ids must be printable and short") {
    CHECK(is_valid_request_id("550e8400-e29b-41d4-a716-446655440000"));
    CHECK(is_valid_request_id("abc"));
    CHECK_FALSE(is_valid_request_id(""));
    CHECK_FALSE(is_valid_request_id("with space"));
    CHECK_FALSE(is_valid_request_id(std::string(65, 'a')));
    CHECK_FALSE(is_valid_request_id("tab\there"));
}

TEST_CASE("gzip negotiation") {
    CHECK(accepts_gzip("gzip"));
    CHECK(accepts_gzip("deflate, gzip;q=0.8"));
    CHECK(accepts_gzip("*"));
    CHECK_FALSE(accepts_gzip("gzip;q=0"));
    CHECK_FALSE(accepts_gzip("deflate, br"));
    CHECK_FALSE(accepts_gzip(""));
}

TEST_CASE("immutable asset detection") {
    CHECK(is_immutable_asset("/assets/index-a1b2c3d4.js"));
    CHECK(is_immutable_asset("/assets/inter-1a2b3c4d.woff2"));
    CHECK_FALSE(is_immutable_asset("/assets/index.js"));
    CHECK_FALSE(is_immutable_asset("/index.html"));
    CHECK_FALSE(is_immutable_asset("/assets/index-xyz.js"));
}

TEST_CASE("content disposition and etags") {
    CHECK(content_disposition("kaffee.json") == "attachment; filename=\"kaffee.json\"");
    CHECK(content_disposition("a\"b.json") == "attachment; filename=\"a_b.json\"");
    const std::string etag = etag_for("index");
    CHECK(etag == etag_for("index"));
    CHECK(etag != etag_for("other"));
    CHECK(etag_matches(etag, etag));
    CHECK(etag_matches("W/" + etag, etag));
    CHECK(etag_matches("\"x\", " + etag, etag));
    CHECK(etag_matches("*", etag));
    CHECK_FALSE(etag_matches("\"other\"", etag));
}

TEST_CASE("wifi QR payload escapes specials") {
    CHECK(wifi_qr_payload("Kaffeekasse-1234", "secret123") ==
          "WIFI:T:WPA;S:Kaffeekasse-1234;P:secret123;;");
    CHECK(wifi_qr_payload("Cafe;Bar", "a:b") == "WIFI:T:WPA;S:Cafe\\;Bar;P:a\\:b;;");
    CHECK(url_qr_payload("http://192.168.4.1/") == "http://192.168.4.1/");
}

TEST_CASE("CSV escaping") {
    CHECK(csv_escape("Kaffee", ';') == "Kaffee");
    CHECK(csv_escape("Kaffee, schwarz", ',') == "\"Kaffee, schwarz\"");
    CHECK(csv_escape("Kaffee, schwarz", ';') == "Kaffee, schwarz");
    CHECK(csv_escape("say \"hi\"", ';') == "\"say \"\"hi\"\"\"");
    CHECK(csv_escape("line\nbreak", ';') == "\"line\nbreak\"");
    // Formula injection protection; numbers keep their sign.
    CHECK(csv_escape("=SUM(A1)", ';') == "'=SUM(A1)");
    CHECK(csv_escape("@cmd", ';') == "'@cmd");
    CHECK(csv_escape("-42", ';') == "-42");
    CHECK(csv_escape("-SUM(A1)", ';') == "'-SUM(A1)");

    CHECK(csv_row({"a", "b;c"}, ';') == "a;\"b;c\"\r\n");
    CHECK(csv_header({"Betrag", "Notiz"}, ';') == "Betrag;Notiz\r\n");
}

TEST_CASE("civil date conversion round-trips") {
    for (int day = 0; day < 4000; day += 97) {
        int year = 0;
        unsigned month = 0;
        unsigned dom = 0;
        civil_from_days(day, year, month, dom);
        CHECK(days_from_civil(year, month, dom) == day);
    }
    CHECK(days_from_civil(1970, 1, 1) == 0);
    CHECK(format_date_utc(0) == "1970-01-01");
    CHECK(format_timestamp_utc(1704063600) == "2023-12-31 23:00:00");
    CHECK(backup_filename(1719784800) == "kaffeekasse-backup-2024-06-30.json");
}

TEST_CASE("Europe/Berlin DST offset") {
    CHECK(berlin_utc_offset(1711846799) == 3600);  // 2024-03-31 00:59:59Z
    CHECK(berlin_utc_offset(1711846800) == 7200);  // 2024-03-31 01:00:00Z
    CHECK(berlin_utc_offset(1729990799) == 7200);  // 2024-10-27 00:59:59Z
    CHECK(berlin_utc_offset(1729990800) == 3600);  // 2024-10-27 01:00:00Z
}

TEST_CASE("month range uses Berlin midnight") {
    auto summer = parse_month_range("2024-07");
    REQUIRE(summer.has_value());
    CHECK(summer->from == 1719784800);  // 2024-07-01 00:00 CEST
    CHECK(summer->to == 1722463200);    // 2024-08-01 00:00 CEST

    auto winter = parse_month_range("2024-01");
    REQUIRE(winter.has_value());
    CHECK(winter->from == 1704063600);  // 2024-01-01 00:00 CET
    CHECK(winter->to == 1706742000);    // 2024-02-01 00:00 CET

    CHECK_FALSE(parse_month_range("2024-13").has_value());
    CHECK_FALSE(parse_month_range("2024-1").has_value());
    CHECK_FALSE(parse_month_range("abcd-01").has_value());
}

TEST_CASE("misc helpers") {
    CHECK(trim("  x \t\n") == "x");
    CHECK(lower_ascii("AbC") == "abc");
    CHECK(is_valid_color("#D4A373"));
    CHECK_FALSE(is_valid_color("D4A373"));
    CHECK_FALSE(is_valid_color("#D4A37"));
    bool flag = false;
    CHECK(parse_bool_text("Ja", flag));
    CHECK(flag);
    CHECK(parse_bool_text("off", flag));
    CHECK_FALSE(flag);
    CHECK_FALSE(parse_bool_text("vielleicht", flag));
}

TEST_CASE("device status payload includes service-owned statistics") {
    SystemStats stats;
    stats.uptime_s = 42;
    stats.storage.used_bytes = 12345;
    stats.storage.total_bytes = 9 * 1024 * 1024;
    stats.storage.mounted = true;
    stats.storage.degraded = false;
    stats.last_backup_at = 1'794'477'000;
    stats.drinks_today = 7;
    stats.network.ap_active = true;
    stats.network.ap_ssid = "Kaffeekasse-ABCD";
    stats.network.ap_ip = "192.168.4.1";
    stats.time.now = 1'794'477'600;
    stats.time.source = TimeSource::Synced;
    stats.time.last_source = "browser";
    stats.time.trusted = true;

    const auto payload = device_status_json(stats);
    CHECK(payload["storage_used"] == 12345);
    CHECK(payload["storage_total"] == 9 * 1024 * 1024);
    CHECK(payload["storage_mounted"] == true);
    CHECK(payload["storage_degraded"] == false);
    CHECK(payload["last_backup_at"] == 1'794'477'000);
    CHECK(payload["drinks_today"] == 7);
    CHECK(payload["network"]["ap_active"] == true);
    CHECK(payload["network"]["ap_ssid"] == "Kaffeekasse-ABCD");
    CHECK(payload["time"]["source"] == "synced");
    CHECK(payload["time"]["trusted"] == true);
}

TEST_CASE("streaming SHA-256 matches the one-shot helper") {
    std::string data;
    data.reserve(5000);
    for (size_t i = 0; i < 5000; ++i) {
        data.push_back(static_cast<char>((i * 31 + 7) % 251));
    }
    const std::string expected = sha256_hex(data);
    for (size_t chunk :
         {size_t{1}, size_t{3}, size_t{7}, size_t{64}, size_t{65}, size_t{1000}, size_t{5000}}) {
        Sha256Stream stream;
        for (size_t offset = 0; offset < data.size(); offset += chunk) {
            size_t take = std::min(chunk, data.size() - offset);
            stream.update(reinterpret_cast<const uint8_t*>(data.data() + offset), take);
        }
        CHECK(stream.hex() == expected);
    }
    Sha256Stream empty;
    CHECK(empty.hex() == sha256_hex(""));
}
