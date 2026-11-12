#include "doctest.h"

#include <algorithm>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "kaffee/api_multipart.hpp"
#include "kaffee/api_routes.hpp"
#include "kaffee/api_util.hpp"

using namespace kaffee;
using namespace kaffee::api;

namespace {

const RouteSpec* find_spec(RouteMethod method, std::string_view pattern) {
    for (const RouteSpec& spec : route_specs()) {
        if (spec.method == method && std::string_view(spec.pattern) == pattern) return &spec;
    }
    return nullptr;
}

}  // namespace

TEST_CASE("route table is unique and namespaced") {
    std::set<std::string> seen;
    for (const RouteSpec& spec : route_specs()) {
        CHECK(std::string_view(spec.pattern).rfind("/api/v1/", 0) == 0);
        const std::string key =
            std::string(route_method_name(spec.method)) + " " + std::string(spec.pattern);
        CHECK(seen.insert(key).second);
    }
}

TEST_CASE("route auth policy matches the OpenAPI contract") {
    const RouteSpec* health = find_spec(RouteMethod::Get, "/api/v1/health");
    REQUIRE(health != nullptr);
    CHECK(health->auth == AuthPolicy::None);

    const RouteSpec* login = find_spec(RouteMethod::Post, "/api/v1/auth/login");
    REQUIRE(login != nullptr);
    CHECK(login->auth == AuthPolicy::None);

    const RouteSpec* me = find_spec(RouteMethod::Get, "/api/v1/auth/me");
    REQUIRE(me != nullptr);
    CHECK(me->auth == AuthPolicy::Session);

    for (const RouteSpec& spec : route_specs()) {
        if (std::string_view(spec.pattern) == "/api/v1/health" ||
            std::string_view(spec.pattern) == "/api/v1/auth/login") {
            continue;
        }
        CHECK(spec.auth != AuthPolicy::None);
        if (spec.method != RouteMethod::Get) {
            CHECK(spec.auth == AuthPolicy::SessionCsrf);
        }
    }
}

TEST_CASE("ledger writes require X-Request-Id") {
    const struct {
        RouteMethod method;
        const char* pattern;
        bool required;
    } kExpected[] = {
        {RouteMethod::Get, "/api/v1/users", false},
        {RouteMethod::Post, "/api/v1/users", true},
        {RouteMethod::Patch, "/api/v1/users/{id}", true},
        {RouteMethod::Post, "/api/v1/users/{id}/archive", true},
        {RouteMethod::Post, "/api/v1/users/{id}/topup", true},
        {RouteMethod::Post, "/api/v1/users/{id}/adjust", true},
        {RouteMethod::Get, "/api/v1/users/{id}/transactions", false},
        {RouteMethod::Post, "/api/v1/products", true},
        {RouteMethod::Patch, "/api/v1/products/{id}", true},
        {RouteMethod::Delete, "/api/v1/products/{id}", true},
        {RouteMethod::Post, "/api/v1/transactions/{id}/void", true},
        {RouteMethod::Post, "/api/v1/expenses", true},
        {RouteMethod::Patch, "/api/v1/expenses/{id}", true},
        {RouteMethod::Post, "/api/v1/expenses/{id}/void", true},
        {RouteMethod::Post, "/api/v1/cash-counts", true},
        {RouteMethod::Put, "/api/v1/settings", true},
        {RouteMethod::Put, "/api/v1/settings/display", true},
        {RouteMethod::Post, "/api/v1/settings/password", true},
        {RouteMethod::Post, "/api/v1/ap/start", true},
        {RouteMethod::Post, "/api/v1/restore", true},
        {RouteMethod::Post, "/api/v1/auth/logout", false},
        {RouteMethod::Post, "/api/v1/time/sync", false},
        {RouteMethod::Post, "/api/v1/ap/stop", false},
        {RouteMethod::Post, "/api/v1/ap/extend", false},
        {RouteMethod::Post, "/api/v1/display/test", false},
        {RouteMethod::Post, "/api/v1/ota", false},
        {RouteMethod::Post, "/api/v1/factory-reset", true},
    };
    for (const auto& expected : kExpected) {
        const RouteSpec* spec = find_spec(expected.method, expected.pattern);
        REQUIRE_MESSAGE(spec != nullptr, expected.pattern);
        CHECK(spec->require_request_id == expected.required);
    }
}

TEST_CASE("only the password-change window allows admin routes") {
    for (const RouteSpec& spec : route_specs()) {
        const bool allowed = route_allowed_when_password_must_change(spec);
        const std::string pattern = spec.pattern;
        const bool expected = spec.auth == AuthPolicy::None || pattern == "/api/v1/auth/me" ||
                              pattern == "/api/v1/auth/logout" ||
                              pattern == "/api/v1/settings/password" ||
                              pattern == "/api/v1/time/sync" ||
                              (pattern == "/api/v1/settings" && spec.method == RouteMethod::Get);
        CHECK_MESSAGE(allowed == expected, pattern.c_str());
    }
    const RouteSpec* users = find_spec(RouteMethod::Get, "/api/v1/users");
    REQUIRE(users != nullptr);
    CHECK_FALSE(route_allowed_when_password_must_change(*users));
}

TEST_CASE("real service codes map to the right status and German text") {
    const struct {
        const char* code;
        const char* message;
        int status;
    } kCases[] = {
        {"auth", "invalid credentials", 401},
        {"auth", "current password is incorrect", 401},
        {"rate_limit", "too many attempts, try again later", 429},
        {"session", "invalid session", 401},
        {"session", "session expired", 401},
        {"password", "new password must have at least 10 characters", 400},
        {"backup", "invalid JSON: parse error", 400},
        {"backup", "backup has no events", 400},
        {"settings", "invalid rotation", 400},
        {"settings", "max_open_debt_cents must not be negative", 400},
        {"void", "event is already voided", 409},
        {"void", "original event not found", 404},
        {"void", "unknown event", 404},
        {"event", "unknown event", 404},
        {"expense", "unknown expense", 404},
        {"expense", "expense is already voided", 409},
        {"expense", "invalid expense", 400},
        {"debt", "maximum open debt exceeded", 409},
        {"duplicate", "event already exists", 409},
        {"amount", "top-up must be positive", 400},
        {"price", "price must not be negative", 400},
        {"user", "unknown user", 404},
        {"user", "user already exists", 409},
        {"user", "user is archived", 409},
        {"product", "unknown product", 404},
        {"product", "product is archived", 409},
        {"time", "invalid epoch", 400},
        {"ap", "duration must be positive", 400},
        {"chain", "hash chain is broken", 500},
        {"something_unexpected", "boom", 500},
    };
    for (const auto& item : kCases) {
        const ErrorMapping mapping = map_service_error(Error{item.code, item.message});
        CHECK_MESSAGE(mapping.status == item.status, item.code);
        CHECK_FALSE(mapping.code.empty());
        // The English service message never reaches the caller.
        CHECK(mapping.message != std::string(item.message));
        CHECK_FALSE(mapping.message.empty());
    }

    CHECK(map_service_error(Error{"auth", "invalid credentials"}).code == "auth");
    CHECK(map_service_error(Error{"rate_limit", "too many"}).code == "rate_limit");
    CHECK(map_service_error(Error{"void", "event is already voided"}).code == "void");
    CHECK(map_service_error(Error{"nonsense", "boom"}).code == "internal_error");
    CHECK_MESSAGE(
        map_service_error(Error{"auth", "invalid credentials"}).message.find("Anmeldung") !=
            std::string::npos,
        "German message expected");
}

TEST_CASE("static cache policy and HEAD handling") {
    StaticPolicy index = static_policy("/", true, "GET");
    CHECK(std::string_view(index.cache_control) == "no-cache");
    CHECK(index.send_body);

    StaticPolicy client_route = static_policy("/users/42", false, "GET");
    CHECK(std::string_view(client_route.cache_control) == "no-cache");

    StaticPolicy hashed = static_policy("/assets/index-a1b2c3d4.js", false, "GET");
    CHECK(std::string_view(hashed.cache_control) == "public, max-age=31536000, immutable");
    CHECK(hashed.immutable);

    StaticPolicy plain = static_policy("/favicon.ico", false, "GET");
    CHECK(std::string_view(plain.cache_control) == "public, max-age=3600");
    CHECK_FALSE(plain.immutable);

    CHECK(static_method_allowed("GET"));
    CHECK_FALSE(static_method_allowed("HEAD"));
    CHECK_FALSE(static_method_allowed("POST"));
    CHECK_FALSE(static_policy("/", true, "HEAD").send_body);
}

TEST_CASE("captive probe paths") {
    CHECK(captive_probe_kind("/generate_204") == CaptiveProbe::Generate204);
    CHECK(captive_probe_kind("/hotspot-detect.html") == CaptiveProbe::Redirect);
    CHECK(captive_probe_kind("/connecttest.txt") == CaptiveProbe::Redirect);
    CHECK(captive_probe_kind("/ncsi.txt") == CaptiveProbe::Redirect);
    CHECK(captive_probe_kind("/") == CaptiveProbe::None);
    CHECK(captive_probe_kind("/api/v1/health") == CaptiveProbe::None);
}

TEST_CASE("log ring filters secret markers") {
    CHECK(contains_secret_marker("POST /auth/login password=geheim"));
    CHECK(contains_secret_marker("Set-Cookie: kaffee_session=abc"));
    CHECK(contains_secret_marker("authorization: bearer xyz"));
    CHECK(contains_secret_marker("csrf_token=abc"));
    CHECK_FALSE(contains_secret_marker("HTTP API listening on port 80"));
    CHECK_FALSE(contains_secret_marker("booked 2 items for user-1"));
}

TEST_CASE("multipart boundary parsing") {
    CHECK(parse_multipart_boundary("multipart/form-data; boundary=abc123") == "abc123");
    CHECK(parse_multipart_boundary("multipart/form-data; boundary=\"a b c\"") == "a b c");
    CHECK(parse_multipart_boundary("multipart/form-data; charset=utf-8; boundary=xyz") == "xyz");
    CHECK(parse_multipart_boundary("MULTIPART/FORM-DATA; BOUNDARY=UP") == "UP");
    CHECK(parse_multipart_boundary("multipart/mixed; boundary=abc").empty());
    CHECK(parse_multipart_boundary("multipart/form-data").empty());
    CHECK(parse_multipart_boundary("multipart/form-data; boundary=").empty());
    CHECK(parse_multipart_boundary("multipart/form-data; boundary=a\r\nb").empty());
}

TEST_CASE("multipart part name parsing") {
    CHECK(multipart_is_firmware_part("form-data; name=\"firmware\"; filename=\"fw.bin\""));
    CHECK(multipart_is_firmware_part("form-data; name=firmware"));
    CHECK(multipart_is_firmware_part("form-data; filename=\"fw.bin\"; name=\"firmware\""));
    CHECK_FALSE(multipart_is_firmware_part("form-data; filename=\"firmware\""));
    CHECK_FALSE(multipart_is_firmware_part("form-data; name=\"other\"; filename=\"fw.bin\""));
    CHECK_FALSE(multipart_is_firmware_part(""));
    CHECK(multipart_parameter("form-data; name=\"firmware\"; filename=\"a.bin\"", "filename") ==
          std::optional<std::string>("a.bin"));
}

TEST_CASE("multipart scanner handles every chunk split") {
    std::string payload;
    for (int i = 0; i < 500; ++i) payload.push_back(static_cast<char>('a' + (i % 26)));
    // Include CR/LF and the boundary prefix inside the payload; only the full
    // delimiter sequence terminates the part.
    payload += "\r\n--NOT-THE-BOUNDARY\r\n";
    const std::string boundary = "BOUNDARY123";
    const std::string body = payload + "\r\n--" + boundary + "--\r\n";

    for (size_t split = 0; split <= body.size(); ++split) {
        MultipartScanner scanner(boundary);
        std::string out;
        scanner.feed(std::string_view(body).substr(0, split), out);
        scanner.feed(std::string_view(body).substr(split), out);
        CHECK(scanner.complete());
        CHECK(out == payload);
        // Bytes after the delimiter may arrive in a later chunk; whatever the
        // scanner retained must be a prefix of the closing sequence.
        CHECK(std::string_view("--\r\n").substr(0, scanner.tail().size()) == scanner.tail());
    }

    MultipartScanner whole(boundary);
    std::string whole_out;
    whole.feed(body, whole_out);
    CHECK(whole.complete());
    CHECK(whole_out == payload);
    CHECK(whole.tail().substr(0, 2) == "--");
}

TEST_CASE("multipart scanner detects byte-wise feeds") {
    const std::string boundary = "xyz";
    const std::string payload = "firmware-bytes";
    const std::string body = payload + "\r\n--xyz--\r\n";
    MultipartScanner scanner(boundary);
    std::string out;
    for (char c : body) {
        const char byte = c;
        scanner.feed(std::string_view(&byte, 1), out);
    }
    CHECK(scanner.complete());
    CHECK(out == payload);
}

TEST_CASE("multipart scanner without a boundary never completes") {
    MultipartScanner scanner("BOUNDARY123");
    std::string out;
    const std::string data = "no delimiter in this body at all";
    CHECK_FALSE(scanner.feed(data, out));
    CHECK_FALSE(scanner.complete());
    // The last delimiter.size()-1 bytes stay buffered for the next chunk.
    const size_t keep = scanner.delimiter().size() - 1;
    REQUIRE(data.size() > keep);
    CHECK(out.size() == data.size() - keep);
    CHECK(out == data.substr(0, data.size() - keep));
}
