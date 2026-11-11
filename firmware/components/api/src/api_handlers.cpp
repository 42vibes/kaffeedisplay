#include <algorithm>
#include <cstdio>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "api_internal.hpp"
#include "esp_log.h"

namespace kaffee {
namespace api {
namespace {

using nlohmann::json;

constexpr const char* kTag = "kaffee.api";
constexpr const char* kSessionCookie = "kaffee_session";
constexpr size_t kMaxNameLength = 40;
constexpr size_t kMaxNoteLength = 300;
constexpr Timestamp kMinTrustedTime = 1577836800;  // 2020-01-01T00:00:00Z
constexpr Timestamp kMaxTrustedTime = 4102444800;  // 2100-01-01T00:00:00Z

// --- Validated JSON field access ----------------------------------------

Result<std::string> get_string(const json& body, const char* key, bool required, size_t max_len) {
    if (!body.contains(key) || body[key].is_null()) {
        if (required) {
            return Result<std::string>::fail("invalid_input",
                                             std::string("Feld '") + key + "' fehlt");
        }
        return Result<std::string>::ok(std::string());
    }
    if (!body[key].is_string()) {
        return Result<std::string>::fail("invalid_input",
                                         std::string("Feld '") + key + "' muss Text sein");
    }
    std::string value = body[key].get<std::string>();
    if (value.size() > max_len) {
        return Result<std::string>::fail("invalid_input",
                                         std::string("Feld '") + key + "' ist zu lang");
    }
    return Result<std::string>::ok(std::move(value));
}

Result<Cents> get_cents(const json& body, const char* key, bool required, Cents min, Cents max) {
    if (!body.contains(key) || body[key].is_null()) {
        if (required) {
            return Result<Cents>::fail("invalid_input", std::string("Feld '") + key + "' fehlt");
        }
        return Result<Cents>::ok(static_cast<Cents>(0));
    }
    if (!body[key].is_number_integer() && !body[key].is_number_unsigned()) {
        return Result<Cents>::fail("invalid_input",
                                   std::string("Feld '") + key + "' muss eine Ganzzahl sein");
    }
    Cents value = body[key].get<Cents>();
    if (value < min || value > max) {
        return Result<Cents>::fail("invalid_input",
                                   std::string("Feld '") + key + "' liegt außerhalb des Bereichs");
    }
    return Result<Cents>::ok(value);
}

Result<bool> get_bool(const json& body, const char* key, bool fallback) {
    if (!body.contains(key) || body[key].is_null()) return Result<bool>::ok(fallback);
    if (!body[key].is_boolean()) {
        return Result<bool>::fail("invalid_input",
                                  std::string("Feld '") + key + "' muss bool sein");
    }
    return Result<bool>::ok(body[key].get<bool>());
}

bool validate_display(const DisplaySettings& display, Error& error) {
    if (display.rotation != 0 && display.rotation != 90 && display.rotation != 180 &&
        display.rotation != 270) {
        error = Error{"invalid_input", "Rotation muss 0, 90, 180 oder 270 sein"};
        return false;
    }
    if (display.brightness_active < 5 || display.brightness_active > 100) {
        error = Error{"invalid_input", "Aktive Helligkeit muss zwischen 5 und 100 liegen"};
        return false;
    }
    if (display.brightness_idle < 1 || display.brightness_idle > 100) {
        error = Error{"invalid_input", "Ruhehelligkeit muss zwischen 1 und 100 liegen"};
        return false;
    }
    if (display.dim_timeout_s < 0 || display.dim_timeout_s > 86400 ||
        display.screen_off_timeout_s < 0 || display.screen_off_timeout_s > 86400) {
        error = Error{"invalid_input", "Zeitüberschreitungen müssen zwischen 0 und 86400 liegen"};
        return false;
    }
    return true;
}

// --- Response builders ---------------------------------------------------

json time_info_json(const TimeInfo& time) {
    return json{{"now", time.now},
                {"source", Ledger::time_source_name(time.source)},
                {"last_sync", time.last_sync},
                {"last_source", time.last_source},
                {"trusted", time.trusted}};
}

json ap_status_json(const NetworkStatus& network) {
    const std::string ip =
        network.ap_ip.empty() ? (network.ap_active ? "192.168.4.1" : "") : network.ap_ip;
    json status{{"active", network.ap_active},
                {"starting", network.ap_starting},
                {"remaining_seconds", network.ap_remaining_s},
                {"ssid", network.ap_active ? network.ap_ssid : std::string()},
                {"password", network.ap_active ? network.ap_password : std::string()},
                {"ip", ip},
                {"sta_connected", network.sta_connected},
                {"sta_ip", network.sta_ip},
                {"sta_rssi", network.sta_rssi}};
    if (network.ap_active && !ip.empty()) {
        const std::string url = "http://" + ip + "/";
        status["url"] = url;
        status["wifi_qr"] = wifi_qr_payload(network.ap_ssid, network.ap_password);
        status["url_qr"] = url_qr_payload(url);
    } else {
        status["url"] = "";
        status["wifi_qr"] = "";
        status["url_qr"] = "";
    }
    return status;
}

json device_status_json(const SystemStats& stats) {
    json network{{"ap_active", stats.network.ap_active},
                 {"ap_starting", stats.network.ap_starting},
                 {"ap_remaining_s", stats.network.ap_remaining_s},
                 {"ap_ssid", stats.network.ap_ssid},
                 {"ap_ip", stats.network.ap_ip},
                 {"sta_connected", stats.network.sta_connected},
                 {"sta_ip", stats.network.sta_ip},
                 {"sta_rssi", stats.network.sta_rssi}};
    return json{{"firmware_version", stats.firmware_version},
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

// Serialises events with the derived fields the contract exposes: `voided`
// and the balance after each user event. One pass over the ledger keeps this
// O(events) per request instead of O(events * page).
json events_json(KaffeeService& service, const std::vector<const Event*>& list) {
    std::set<std::string> voided;
    for (const Event& event : service.events()) {
        if (event.type == EventType::Void && !event.reverses.empty()) voided.insert(event.reverses);
    }

    std::set<std::string> targets;
    for (const Event* event : list) targets.insert(event->id);

    std::map<std::string, Cents> balance_after;
    if (!targets.empty()) {
        std::map<std::string, Cents> balances;
        for (const Event& event : service.events()) {
            if (event.user_id.empty()) continue;
            balances[event.user_id] += event.amount_cents;
            if (targets.count(event.id) != 0) balance_after[event.id] = balances[event.user_id];
        }
    }

    json array = json::array();
    for (const Event* event : list) {
        json item = event_to_json(*event);
        item["voided"] = voided.count(event->id) != 0;
        auto found = balance_after.find(event->id);
        if (!event->user_id.empty() && found != balance_after.end()) {
            item["balance_after_cents"] = found->second;
        }
        array.push_back(std::move(item));
    }
    return array;
}

json event_json(KaffeeService& service, const Event& event) {
    json array = events_json(service, {&event});
    return array.at(0);
}

const Expense* find_expense(KaffeeService& service, const std::string& id) {
    for (const Expense& expense : service.expenses()) {
        if (expense.id == id) return &expense;
    }
    return nullptr;
}

json expense_or_event(KaffeeService& service, const Event& event) {
    if (const Expense* expense = find_expense(service, event.expense_id)) {
        return expense_to_json(*expense);
    }
    json fallback = event_to_json(event);
    fallback["id"] = event.expense_id;
    return fallback;
}

json cash_count_from_event(const Event& event) {
    return json{{"id", event.id},
                {"ts", event.ts},
                {"counted_cents", event.counted_cents},
                {"expected_cents", event.expected_cents},
                {"difference_cents", event.counted_cents - event.expected_cents},
                {"note", event.note}};
}

std::string request_id_or_generated(RequestContext& ctx, const char* prefix) {
    if (!ctx.request_id.empty()) return ctx.request_id;
    if (auto header = req_header(ctx.req, "X-Request-Id")) {
        std::string candidate = trim(*header);
        if (is_valid_request_id(candidate)) return candidate;
    }
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "%s-%016llx", prefix,
                  static_cast<unsigned long long>(ctx.server.platform().random_u64()));
    return buffer;
}

// --- Auth ----------------------------------------------------------------

esp_err_t handle_health(RequestContext& ctx) {
    SystemStats stats = ctx.server.platform().stats();
    return send_json(ctx.req, 200, json{{"status", "ok"}, {"version", stats.firmware_version}});
}

json session_json(KaffeeService& service, const Session& session) {
    return json{
        {"username", service.settings().admin.username.empty() ? std::string("admin")
                                                               : service.settings().admin.username},
        {"csrf_token", session.csrf_token},
        {"expires_at", session.expires_at}};
}

esp_err_t handle_login(RequestContext& ctx) {
    Result<json> body = read_json_body(ctx.req, kMaxJsonBody);
    if (!body) return send_service_error(ctx.req, result_error(body));

    Result<std::string> username = get_string(*body.value, "username", true, 64);
    if (!username) return send_service_error(ctx.req, result_error(username));
    Result<std::string> password = get_string(*body.value, "password", true, 256);
    if (!password) return send_service_error(ctx.req, result_error(password));

    const std::string ip = client_ip(ctx.req);
    Result<Session> session = ctx.server.service().login(*username.value, *password.value, ip);
    if (!session) {
        ESP_LOGW(kTag, "login failed from %s: %s", ip.c_str(), result_error(session).code.c_str());
        return send_service_error(ctx.req, result_error(session));
    }

    const Timestamp now = ctx.server.platform().time_info().now;
    int64_t max_age = session.value->expires_at - now;
    if (max_age < 60) max_age = 60;
    if (max_age > 8 * 3600) max_age = 8 * 3600;
    char cookie[160];
    std::snprintf(cookie, sizeof(cookie), "%s=%s; Path=/; HttpOnly; SameSite=Strict; Max-Age=%lld",
                  kSessionCookie, session.value->token.c_str(), static_cast<long long>(max_age));
    httpd_resp_set_hdr(ctx.req, "Set-Cookie", cookie);

    return send_json(ctx.req, 200, session_json(ctx.server.service(), *session.value));
}

esp_err_t handle_logout(RequestContext& ctx) {
    Result<Unit> result = ctx.server.service().logout(ctx.token);
    if (!result) return send_service_error(ctx.req, result_error(result));
    httpd_resp_set_hdr(ctx.req, "Set-Cookie",
                       "kaffee_session=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0");
    return send_no_content(ctx.req);
}

esp_err_t handle_me(RequestContext& ctx) {
    return send_json(ctx.req, 200, session_json(ctx.server.service(), ctx.session));
}

// --- Users ---------------------------------------------------------------

esp_err_t handle_users_list(RequestContext& ctx) {
    KaffeeService& service = ctx.server.service();
    std::map<std::string, Timestamp> last_booking;
    std::map<std::string, Cents> balances;
    for (const Event& event : service.events()) {
        if (event.user_id.empty()) continue;
        balances[event.user_id] += event.amount_cents;
        if (event.type == EventType::Purchase) {
            auto& value = last_booking[event.user_id];
            if (event.ts > value) value = event.ts;
        }
    }

    json array = json::array();
    for (const User& user : service.users()) {
        json item = user_to_json(user);
        auto balance = balances.find(user.id);
        item["balance_cents"] = balance == balances.end() ? 0 : balance->second;
        auto found = last_booking.find(user.id);
        if (found != last_booking.end()) item["last_booking_at"] = found->second;
        array.push_back(std::move(item));
    }
    return send_json(ctx.req, 200, array);
}

esp_err_t handle_users_create(RequestContext& ctx) {
    Result<json> body = read_json_body(ctx.req, kMaxJsonBody);
    if (!body) return send_service_error(ctx.req, result_error(body));
    Result<std::string> name = get_string(*body.value, "name", true, kMaxNameLength);
    if (!name) return send_service_error(ctx.req, result_error(name));
    std::string trimmed_name = trim(*name.value);
    if (trimmed_name.empty()) {
        return send_error(ctx.req, 400, "invalid_input", "Name darf nicht leer sein");
    }
    Result<std::string> color = get_string(*body.value, "color", false, 7);
    if (!color) return send_service_error(ctx.req, result_error(color));
    std::string user_color = color.value->empty() ? "#D4A373" : *color.value;
    if (!is_valid_color(user_color)) {
        return send_error(ctx.req, 400, "invalid_input", "Farbe muss das Format #RRGGBB haben");
    }

    Result<Event> event =
        ctx.server.service().create_user(trimmed_name, user_color, ctx.request_id);
    if (!event) return send_service_error(ctx.req, result_error(event));
    auto user = ctx.server.service().find_user(event.value->user_id);
    if (!user) return send_error(ctx.req, 500, "internal_error", "Benutzer nicht gefunden");
    return send_json(ctx.req, 201, user_to_json(*user));
}

esp_err_t handle_users_update(RequestContext& ctx) {
    KaffeeService& service = ctx.server.service();
    const std::string id = ctx.params.get("id").value_or("");
    auto existing = service.find_user(id);
    if (!existing) return send_error(ctx.req, 404, "not_found", "Benutzer nicht gefunden");

    Result<json> body = read_json_body(ctx.req, kMaxJsonBody);
    if (!body) return send_service_error(ctx.req, result_error(body));
    Result<std::string> name = get_string(*body.value, "name", false, kMaxNameLength);
    if (!name) return send_service_error(ctx.req, result_error(name));
    Result<std::string> color = get_string(*body.value, "color", false, 7);
    if (!color) return send_service_error(ctx.req, result_error(color));
    Result<bool> active = get_bool(*body.value, "active", existing->active);
    if (!active) return send_service_error(ctx.req, result_error(active));

    std::string new_name = name.value->empty() ? existing->name : trim(*name.value);
    if (new_name.empty()) {
        return send_error(ctx.req, 400, "invalid_input", "Name darf nicht leer sein");
    }
    std::string new_color = color.value->empty() ? existing->color : *color.value;
    if (!is_valid_color(new_color)) {
        return send_error(ctx.req, 400, "invalid_input", "Farbe muss das Format #RRGGBB haben");
    }

    Result<Event> event =
        service.update_user(id, new_name, new_color, *active.value, ctx.request_id);
    if (!event) return send_service_error(ctx.req, result_error(event));
    auto updated = service.find_user(id);
    if (!updated) return send_error(ctx.req, 404, "not_found", "Benutzer nicht gefunden");
    return send_json(ctx.req, 200, user_to_json(*updated));
}

esp_err_t handle_users_archive(RequestContext& ctx) {
    KaffeeService& service = ctx.server.service();
    const std::string id = ctx.params.get("id").value_or("");
    if (!service.find_user(id)) {
        return send_error(ctx.req, 404, "not_found", "Benutzer nicht gefunden");
    }
    Result<Event> event = service.archive_user(id, ctx.request_id);
    if (!event) return send_service_error(ctx.req, result_error(event));
    auto archived = service.find_user(id);
    if (!archived) return send_error(ctx.req, 404, "not_found", "Benutzer nicht gefunden");
    return send_json(ctx.req, 200, user_to_json(*archived));
}

esp_err_t handle_users_topup(RequestContext& ctx) {
    KaffeeService& service = ctx.server.service();
    const std::string id = ctx.params.get("id").value_or("");
    if (!service.find_user(id)) {
        return send_error(ctx.req, 404, "not_found", "Benutzer nicht gefunden");
    }
    Result<json> body = read_json_body(ctx.req, kMaxJsonBody);
    if (!body) return send_service_error(ctx.req, result_error(body));
    Result<Cents> amount = get_cents(*body.value, "amount_cents", true, 1, 1000000);
    if (!amount) return send_service_error(ctx.req, result_error(amount));
    Result<std::string> note = get_string(*body.value, "note", false, 200);
    if (!note) return send_service_error(ctx.req, result_error(note));

    Result<Event> event = service.topup(id, *amount.value, *note.value, ctx.request_id);
    if (!event) return send_service_error(ctx.req, result_error(event));
    return send_json(ctx.req, 200, event_json(service, *event.value));
}

esp_err_t handle_users_adjust(RequestContext& ctx) {
    KaffeeService& service = ctx.server.service();
    const std::string id = ctx.params.get("id").value_or("");
    if (!service.find_user(id)) {
        return send_error(ctx.req, 404, "not_found", "Benutzer nicht gefunden");
    }
    Result<json> body = read_json_body(ctx.req, kMaxJsonBody);
    if (!body) return send_service_error(ctx.req, result_error(body));
    Result<Cents> amount = get_cents(*body.value, "amount_cents", true, -1000000, 1000000);
    if (!amount) return send_service_error(ctx.req, result_error(amount));
    Result<std::string> note = get_string(*body.value, "note", true, 200);
    if (!note) return send_service_error(ctx.req, result_error(note));
    if (trim(*note.value).size() < 3) {
        return send_error(ctx.req, 400, "invalid_input", "Notiz muss mindestens 3 Zeichen haben");
    }

    Result<Event> event = service.adjust(id, *amount.value, *note.value, ctx.request_id);
    if (!event) return send_service_error(ctx.req, result_error(event));
    return send_json(ctx.req, 200, event_json(service, *event.value));
}

esp_err_t handle_users_transactions(RequestContext& ctx) {
    KaffeeService& service = ctx.server.service();
    const std::string id = ctx.params.get("id").value_or("");
    if (!service.find_user(id)) {
        return send_error(ctx.req, 404, "not_found", "Benutzer nicht gefunden");
    }
    const std::string query = req_query(ctx.req);
    int64_t limit = query_int(query, "limit").value_or(50);
    if (limit < 1) limit = 1;
    if (limit > 200) limit = 200;
    auto before = query_int(query, "before");
    if (before && *before < 0) before.reset();

    std::vector<const Event*> selected;
    const auto& events = service.events();
    for (auto it = events.rbegin();
         it != events.rend() && selected.size() < static_cast<size_t>(limit); ++it) {
        if (it->user_id != id) continue;
        if (before && it->seq >= static_cast<uint64_t>(*before)) continue;
        selected.push_back(&*it);
    }
    return send_json(ctx.req, 200, events_json(service, selected));
}

// --- Products ------------------------------------------------------------

esp_err_t handle_products_list(RequestContext& ctx) {
    const bool include_archived = query_flag(req_query(ctx.req), "include_archived", false);
    json array = json::array();
    for (const Product& product : ctx.server.service().products()) {
        if (!include_archived && !product.active) continue;
        array.push_back(product_to_json(product));
    }
    return send_json(ctx.req, 200, array);
}

struct ProductInput {
    std::string name;
    std::string category;
    std::string icon;
    std::string color;
    Cents price_cents = 0;
    int sort = 0;
    bool active = true;
};

Result<ProductInput> parse_product_input(const json& body, const Product* base) {
    Result<std::string> name = get_string(body, "name", base == nullptr, kMaxNameLength);
    if (!name)
        return Result<ProductInput>::fail(result_error(name).code, result_error(name).message);
    Result<std::string> category = get_string(body, "category", base == nullptr, kMaxNameLength);
    if (!category) {
        return Result<ProductInput>::fail(result_error(category).code,
                                          result_error(category).message);
    }
    Result<std::string> icon = get_string(body, "icon", false, kMaxNameLength);
    if (!icon)
        return Result<ProductInput>::fail(result_error(icon).code, result_error(icon).message);
    Result<std::string> color = get_string(body, "color", false, 7);
    if (!color)
        return Result<ProductInput>::fail(result_error(color).code, result_error(color).message);
    Result<Cents> price = get_cents(body, "price_cents", base == nullptr, 0, 100000);
    if (!price)
        return Result<ProductInput>::fail(result_error(price).code, result_error(price).message);
    Result<Cents> sort = get_cents(body, "sort", false, -100000, 100000);
    if (!sort)
        return Result<ProductInput>::fail(result_error(sort).code, result_error(sort).message);
    Result<bool> active = get_bool(body, "active", base == nullptr ? true : base->active);
    if (!active)
        return Result<ProductInput>::fail(result_error(active).code, result_error(active).message);

    ProductInput input;
    input.name = base ? base->name : "";
    input.category = base ? base->category : "";
    input.icon = base ? base->icon : "";
    input.color = base ? base->color : "#8B5E3C";
    input.sort = base ? base->sort : 0;
    input.price_cents = base ? base->price_cents : 0;
    input.active = *active.value;

    if (!name.value->empty()) input.name = trim(*name.value);
    if (!category.value->empty()) input.category = *category.value;
    if (!icon.value->empty()) input.icon = *icon.value;
    if (!color.value->empty()) input.color = *color.value;
    if (body.contains("price_cents")) input.price_cents = *price.value;
    if (body.contains("sort")) input.sort = static_cast<int>(*sort.value);

    if (input.name.empty()) {
        return Result<ProductInput>::fail("invalid_input", "Name darf nicht leer sein");
    }
    if (input.category.empty()) {
        return Result<ProductInput>::fail("invalid_input", "Kategorie darf nicht leer sein");
    }
    if (!is_valid_color(input.color)) {
        return Result<ProductInput>::fail("invalid_input", "Farbe muss das Format #RRGGBB haben");
    }
    return Result<ProductInput>::ok(std::move(input));
}

esp_err_t handle_products_create(RequestContext& ctx) {
    Result<json> body = read_json_body(ctx.req, kMaxJsonBody);
    if (!body) return send_service_error(ctx.req, result_error(body));
    Result<ProductInput> input = parse_product_input(*body.value, nullptr);
    if (!input) return send_service_error(ctx.req, result_error(input));

    Result<Event> event = ctx.server.service().create_product(
        input.value->name, input.value->category, input.value->icon, input.value->color,
        input.value->price_cents, input.value->sort, ctx.request_id);
    if (!event) return send_service_error(ctx.req, result_error(event));
    auto product = ctx.server.service().find_product(event.value->product_id);
    if (!product) return send_error(ctx.req, 500, "internal_error", "Produkt nicht gefunden");
    return send_json(ctx.req, 201, product_to_json(*product));
}

esp_err_t handle_products_update(RequestContext& ctx) {
    KaffeeService& service = ctx.server.service();
    const std::string id = ctx.params.get("id").value_or("");
    auto existing = service.find_product(id);
    if (!existing) return send_error(ctx.req, 404, "not_found", "Produkt nicht gefunden");

    Result<json> body = read_json_body(ctx.req, kMaxJsonBody);
    if (!body) return send_service_error(ctx.req, result_error(body));
    Result<ProductInput> input = parse_product_input(*body.value, &*existing);
    if (!input) return send_service_error(ctx.req, result_error(input));

    Result<Event> event = service.update_product(
        id, input.value->name, input.value->category, input.value->icon, input.value->color,
        input.value->price_cents, input.value->sort, input.value->active, ctx.request_id);
    if (!event) return send_service_error(ctx.req, result_error(event));
    auto product = service.find_product(id);
    if (!product) return send_error(ctx.req, 404, "not_found", "Produkt nicht gefunden");
    return send_json(ctx.req, 200, product_to_json(*product));
}

esp_err_t handle_products_delete(RequestContext& ctx) {
    KaffeeService& service = ctx.server.service();
    const std::string id = ctx.params.get("id").value_or("");
    if (!service.find_product(id)) {
        return send_error(ctx.req, 404, "not_found", "Produkt nicht gefunden");
    }
    Result<Event> event = service.delete_product(id, ctx.request_id);
    if (!event) return send_service_error(ctx.req, result_error(event));
    return send_no_content(ctx.req);
}

// --- Transactions --------------------------------------------------------

esp_err_t handle_transactions_list(RequestContext& ctx) {
    KaffeeService& service = ctx.server.service();
    const std::string query = req_query(ctx.req);
    const std::string user_id = query_param(query, "user_id").value_or("");
    const std::string type_name = query_param(query, "type").value_or("");
    auto from = query_int(query, "from");
    auto to = query_int(query, "to");
    int64_t limit = query_int(query, "limit").value_or(100);
    int64_t offset = query_int(query, "offset").value_or(0);
    if (limit < 1) limit = 1;
    if (limit > 500) limit = 500;
    if (offset < 0) offset = 0;

    EventType type = EventType::Unknown;
    const bool filter_type = !type_name.empty();
    if (filter_type) {
        type = Ledger::event_type_from_name(type_name);
        if (type == EventType::Unknown && lower_ascii(type_name) != "unknown") {
            return send_error(ctx.req, 400, "invalid_input", "Unbekannter Buchungstyp");
        }
    }

    std::vector<const Event*> selected;
    const auto& events = service.events();
    size_t skipped = 0;
    for (auto it = events.rbegin(); it != events.rend(); ++it) {
        if (!user_id.empty() && it->user_id != user_id) continue;
        if (filter_type && it->type != type) continue;
        if (from && it->ts < *from) continue;
        if (to && it->ts >= *to) continue;
        if (skipped < static_cast<size_t>(offset)) {
            ++skipped;
            continue;
        }
        if (selected.size() >= static_cast<size_t>(limit)) break;
        selected.push_back(&*it);
    }
    return send_json(ctx.req, 200, events_json(service, selected));
}

esp_err_t handle_transactions_void(RequestContext& ctx) {
    KaffeeService& service = ctx.server.service();
    const std::string id = ctx.params.get("id").value_or("");
    Result<json> body = read_json_body(ctx.req, kMaxJsonBody);
    if (!body) return send_service_error(ctx.req, result_error(body));
    Result<std::string> reason = get_string(*body.value, "reason", true, 200);
    if (!reason) return send_service_error(ctx.req, result_error(reason));
    if (trim(*reason.value).size() < 3) {
        return send_error(ctx.req, 400, "invalid_input", "Grund muss mindestens 3 Zeichen haben");
    }
    Result<Event> event = service.void_event(id, trim(*reason.value), ctx.request_id);
    if (!event) return send_service_error(ctx.req, result_error(event));
    return send_json(ctx.req, 200, event_json(service, *event.value));
}

// --- Expenses ------------------------------------------------------------

struct ExpenseInput {
    std::string category;
    std::string description;
    std::string vendor;
    std::string note;
    Cents amount_cents = 0;
};

Result<ExpenseInput> parse_expense_input(const json& body, const Expense* base) {
    Result<std::string> category = get_string(body, "category", base == nullptr, kMaxNameLength);
    if (!category) {
        return Result<ExpenseInput>::fail(result_error(category).code,
                                          result_error(category).message);
    }
    Result<std::string> description = get_string(body, "description", base == nullptr, 120);
    if (!description) {
        return Result<ExpenseInput>::fail(result_error(description).code,
                                          result_error(description).message);
    }
    Result<std::string> vendor = get_string(body, "vendor", false, 80);
    if (!vendor) {
        return Result<ExpenseInput>::fail(result_error(vendor).code, result_error(vendor).message);
    }
    Result<std::string> note = get_string(body, "note", false, kMaxNoteLength);
    if (!note)
        return Result<ExpenseInput>::fail(result_error(note).code, result_error(note).message);
    Result<Cents> amount = get_cents(body, "amount_cents", base == nullptr, 1, 10000000);
    if (!amount)
        return Result<ExpenseInput>::fail(result_error(amount).code, result_error(amount).message);

    ExpenseInput input;
    if (base != nullptr) {
        input.category = base->category;
        input.description = base->description;
        input.vendor = base->vendor;
        input.note = base->note;
        input.amount_cents = base->amount_cents;
    }
    if (!category.value->empty()) input.category = *category.value;
    if (!description.value->empty()) input.description = *description.value;
    if (!vendor.value->empty()) input.vendor = *vendor.value;
    if (body.contains("note")) input.note = *note.value;
    if (body.contains("amount_cents")) input.amount_cents = *amount.value;

    if (input.category.empty()) {
        return Result<ExpenseInput>::fail("invalid_input", "Kategorie darf nicht leer sein");
    }
    if (input.description.empty()) {
        return Result<ExpenseInput>::fail("invalid_input", "Beschreibung darf nicht leer sein");
    }
    return Result<ExpenseInput>::ok(std::move(input));
}

esp_err_t handle_expenses_list(RequestContext& ctx) {
    KaffeeService& service = ctx.server.service();
    const std::string query = req_query(ctx.req);
    auto from = query_int(query, "from");
    auto to = query_int(query, "to");
    const bool include_voided = query_flag(query, "include_voided", false);

    json array = json::array();
    const auto& expenses = service.expenses();
    for (auto it = expenses.rbegin(); it != expenses.rend(); ++it) {
        if (!include_voided && it->voided) continue;
        if (from && it->ts < *from) continue;
        if (to && it->ts >= *to) continue;
        array.push_back(expense_to_json(*it));
    }
    return send_json(ctx.req, 200, array);
}

esp_err_t handle_expenses_create(RequestContext& ctx) {
    Result<json> body = read_json_body(ctx.req, kMaxJsonBody);
    if (!body) return send_service_error(ctx.req, result_error(body));
    Result<ExpenseInput> input = parse_expense_input(*body.value, nullptr);
    if (!input) return send_service_error(ctx.req, result_error(input));

    Result<Event> event = ctx.server.service().add_expense(
        input.value->category, input.value->description, input.value->vendor,
        input.value->amount_cents, input.value->note, ctx.request_id);
    if (!event) return send_service_error(ctx.req, result_error(event));
    return send_json(ctx.req, 201, expense_or_event(ctx.server.service(), *event.value));
}

esp_err_t handle_expenses_update(RequestContext& ctx) {
    KaffeeService& service = ctx.server.service();
    const std::string id = ctx.params.get("id").value_or("");
    const Expense* existing = find_expense(service, id);
    if (existing == nullptr) return send_error(ctx.req, 404, "not_found", "Ausgabe nicht gefunden");

    Result<json> body = read_json_body(ctx.req, kMaxJsonBody);
    if (!body) return send_service_error(ctx.req, result_error(body));
    Result<ExpenseInput> input = parse_expense_input(*body.value, existing);
    if (!input) return send_service_error(ctx.req, result_error(input));

    Result<Event> event = service.update_expense(
        id, input.value->category, input.value->description, input.value->vendor,
        input.value->amount_cents, input.value->note, ctx.request_id);
    if (!event) return send_service_error(ctx.req, result_error(event));
    if (const Expense* updated = find_expense(service, id)) {
        return send_json(ctx.req, 200, expense_to_json(*updated));
    }
    return send_json(ctx.req, 200, event_json(service, *event.value));
}

esp_err_t handle_expenses_void(RequestContext& ctx) {
    KaffeeService& service = ctx.server.service();
    const std::string id = ctx.params.get("id").value_or("");
    if (find_expense(service, id) == nullptr) {
        return send_error(ctx.req, 404, "not_found", "Ausgabe nicht gefunden");
    }
    Result<json> body = read_json_body(ctx.req, kMaxJsonBody);
    if (!body) return send_service_error(ctx.req, result_error(body));
    Result<std::string> reason = get_string(*body.value, "reason", true, 200);
    if (!reason) return send_service_error(ctx.req, result_error(reason));
    if (trim(*reason.value).size() < 3) {
        return send_error(ctx.req, 400, "invalid_input", "Grund muss mindestens 3 Zeichen haben");
    }
    Result<Event> event = service.void_expense(id, trim(*reason.value), ctx.request_id);
    if (!event) return send_service_error(ctx.req, result_error(event));
    return send_json(ctx.req, 200, event_json(service, *event.value));
}

// --- Cash counts and stats ----------------------------------------------

esp_err_t handle_cash_counts_list(RequestContext& ctx) {
    json array = json::array();
    const auto& counts = ctx.server.service().cash_counts();
    for (auto it = counts.rbegin(); it != counts.rend(); ++it) {
        array.push_back(cash_count_to_json(*it));
    }
    return send_json(ctx.req, 200, array);
}

esp_err_t handle_cash_counts_create(RequestContext& ctx) {
    Result<json> body = read_json_body(ctx.req, kMaxJsonBody);
    if (!body) return send_service_error(ctx.req, result_error(body));
    Result<Cents> counted = get_cents(*body.value, "counted_cents", true, 0, 100000000);
    if (!counted) return send_service_error(ctx.req, result_error(counted));
    Result<std::string> note = get_string(*body.value, "note", false, 200);
    if (!note) return send_service_error(ctx.req, result_error(note));

    Result<Event> event =
        ctx.server.service().record_cash_count(*counted.value, *note.value, ctx.request_id);
    if (!event) return send_service_error(ctx.req, result_error(event));
    return send_json(ctx.req, 201, cash_count_from_event(*event.value));
}

esp_err_t handle_summary(RequestContext& ctx) {
    return send_json(ctx.req, 200, summary_to_json(ctx.server.service().summary()));
}

esp_err_t handle_leaderboard(RequestContext& ctx) {
    KaffeeService& service = ctx.server.service();
    const std::string query = req_query(ctx.req);
    TimeRange range;
    if (auto month = query_param(query, "month")) {
        auto parsed = parse_month_range(*month);
        if (!parsed) return send_error(ctx.req, 400, "invalid_input", "Monat muss YYYY-MM sein");
        range = *parsed;
    } else {
        const Timestamp now = ctx.server.platform().time_info().now;
        if (now <= 0) {
            range.from = 0;
            range.to = std::numeric_limits<Timestamp>::max();
        } else {
            int year = 0;
            unsigned month = 0;
            unsigned day = 0;
            civil_from_days((now + berlin_utc_offset(now)) / 86400, year, month, day);
            auto parsed = month_range_utc(year, static_cast<int>(month));
            range = parsed.value_or(TimeRange{now - 30 * 86400, now + 1});
        }
    }
    return send_json(ctx.req, 200, leaderboard_to_json(service.leaderboard(range.from, range.to)));
}

esp_err_t handle_reports(RequestContext& ctx) {
    static const std::set<std::string> kKinds = {
        "overview", "consumption", "deposits", "expenses", "cashflow", "balances", "transactions"};
    const std::string kind = ctx.params.get("kind").value_or("");
    if (kKinds.count(kind) == 0) {
        return send_error(ctx.req, 404, "not_found", "Unbekannter Bericht");
    }
    // TODO(T-005): call kaffee::reports once components/reports lands. The
    // report kinds, period handling (Europe/Berlin/DST) and the streaming CSV
    // writer are owned by T-005. Until then the endpoint is explicit about it
    // instead of returning misleading empty data.
    ESP_LOGW(kTag, "reports/%s requested but the reports component is not linked yet",
             kind.c_str());
    return send_error(ctx.req, 501, "not_implemented", "Berichte sind noch nicht verfügbar");
}

// --- Settings ------------------------------------------------------------

esp_err_t handle_settings_get(RequestContext& ctx) {
    return send_json(ctx.req, 200, settings_to_json(ctx.server.service().settings(), false));
}

esp_err_t handle_settings_put(RequestContext& ctx) {
    KaffeeService& service = ctx.server.service();
    Result<json> body = read_json_body(ctx.req, kMaxJsonBody);
    if (!body) return send_service_error(ctx.req, result_error(body));

    Settings base = service.settings();
    Settings next = settings_from_json(*body.value, base);

    // Empty write-only secrets keep the stored value (contract).
    if (body.value->contains("network")) {
        const json& network = (*body.value)["network"];
        if (network.contains("ap_password") && network["ap_password"].is_string() &&
            network["ap_password"].get<std::string>().empty()) {
            next.network.ap_password = base.network.ap_password;
        }
        if (network.contains("sta_password") && network["sta_password"].is_string() &&
            network["sta_password"].get<std::string>().empty()) {
            next.network.sta_password = base.network.sta_password;
        }
    }

    Error error;
    if (!validate_display(next.display, error)) return send_service_error(ctx.req, error);
    if (next.network.ap_window_s < 60 || next.network.ap_window_s > 3600) {
        return send_error(ctx.req, 400, "invalid_input",
                          "AP-Fenster muss zwischen 60 und 3600 Sekunden liegen");
    }
    if (next.network.hostname.empty() || next.network.hostname.size() > 32) {
        return send_error(ctx.req, 400, "invalid_input", "Hostname ist ungültig");
    }
    if (next.time.timezone.empty()) {
        return send_error(ctx.req, 400, "invalid_input", "Zeitzone darf nicht leer sein");
    }
    if (next.app.currency.empty() || next.app.locale.empty()) {
        return send_error(ctx.req, 400, "invalid_input",
                          "Währung und Sprache dürfen nicht leer sein");
    }

    Result<Unit> saved = service.save_settings(next, ctx.request_id);
    if (!saved) return send_service_error(ctx.req, result_error(saved));
    return send_json(ctx.req, 200, settings_to_json(service.settings(), false));
}

esp_err_t handle_display_get(RequestContext& ctx) {
    return send_json(ctx.req, 200,
                     display_settings_to_json(ctx.server.service().display_settings()));
}

esp_err_t handle_display_put(RequestContext& ctx) {
    KaffeeService& service = ctx.server.service();
    Result<json> body = read_json_body(ctx.req, kMaxJsonBody);
    if (!body) return send_service_error(ctx.req, result_error(body));

    DisplaySettings next = service.display_settings();
    if (body.value->contains("rotation")) {
        Result<Cents> rotation = get_cents(*body.value, "rotation", false, 0, 270);
        if (!rotation) return send_service_error(ctx.req, result_error(rotation));
        next.rotation = static_cast<int>(*rotation.value);
    }
    if (body.value->contains("brightness_active")) {
        Result<Cents> value = get_cents(*body.value, "brightness_active", false, 5, 100);
        if (!value) return send_service_error(ctx.req, result_error(value));
        next.brightness_active = static_cast<int>(*value.value);
    }
    if (body.value->contains("brightness_idle")) {
        Result<Cents> value = get_cents(*body.value, "brightness_idle", false, 1, 100);
        if (!value) return send_service_error(ctx.req, result_error(value));
        next.brightness_idle = static_cast<int>(*value.value);
    }
    if (body.value->contains("dim_timeout_s")) {
        Result<Cents> value = get_cents(*body.value, "dim_timeout_s", false, 0, 86400);
        if (!value) return send_service_error(ctx.req, result_error(value));
        next.dim_timeout_s = static_cast<int>(*value.value);
    }
    if (body.value->contains("screen_off_timeout_s")) {
        Result<Cents> value = get_cents(*body.value, "screen_off_timeout_s", false, 0, 86400);
        if (!value) return send_service_error(ctx.req, result_error(value));
        next.screen_off_timeout_s = static_cast<int>(*value.value);
    }

    Error error;
    if (!validate_display(next, error)) return send_service_error(ctx.req, error);

    Result<Unit> applied = service.apply_display_settings(next);
    if (!applied) return send_service_error(ctx.req, result_error(applied));
    return send_json(ctx.req, 200, display_settings_to_json(service.display_settings()));
}

esp_err_t handle_password_change(RequestContext& ctx) {
    Result<json> body = read_json_body(ctx.req, kMaxJsonBody);
    if (!body) return send_service_error(ctx.req, result_error(body));
    Result<std::string> current = get_string(*body.value, "current_password", true, 256);
    if (!current) return send_service_error(ctx.req, result_error(current));
    Result<std::string> next = get_string(*body.value, "new_password", true, 256);
    if (!next) return send_service_error(ctx.req, result_error(next));
    if (next.value->size() < 10) {
        return send_error(ctx.req, 400, "invalid_input",
                          "Neues Passwort muss mindestens 10 Zeichen haben");
    }
    Result<Unit> changed = ctx.server.service().change_admin_password(
        *current.value, *next.value, request_id_or_generated(ctx, "pw"));
    if (!changed) return send_service_error(ctx.req, result_error(changed));
    return send_no_content(ctx.req);
}

// --- Time ----------------------------------------------------------------

esp_err_t handle_time_get(RequestContext& ctx) {
    return send_json(ctx.req, 200, time_info_json(ctx.server.platform().time_info()));
}

esp_err_t handle_time_sync(RequestContext& ctx) {
    Result<json> body = read_json_body(ctx.req, kMaxJsonBody);
    if (!body) return send_service_error(ctx.req, result_error(body));
    Result<Cents> epoch_ms =
        get_cents(*body.value, "epoch_ms", true, std::numeric_limits<int64_t>::min(),
                  std::numeric_limits<int64_t>::max());
    if (!epoch_ms) return send_service_error(ctx.req, result_error(epoch_ms));

    const Timestamp seconds = *epoch_ms.value / 1000;
    if (seconds < kMinTrustedTime || seconds > kMaxTrustedTime) {
        return send_error(ctx.req, 400, "invalid_input",
                          "Zeitstempel liegt außerhalb des Bereichs");
    }
    Result<Unit> synced = ctx.server.service().sync_time(seconds, "browser");
    if (!synced) return send_service_error(ctx.req, result_error(synced));
    return send_json(ctx.req, 200, time_info_json(ctx.server.platform().time_info()));
}

// --- AP ------------------------------------------------------------------

esp_err_t handle_ap_status(RequestContext& ctx) {
    return send_json(ctx.req, 200, ap_status_json(ctx.server.service().network_status()));
}

esp_err_t handle_ap_start(RequestContext& ctx) {
    Result<Unit> started = ctx.server.service().start_ap();
    if (!started) return send_service_error(ctx.req, result_error(started));
    return send_json(ctx.req, 200, ap_status_json(ctx.server.service().network_status()));
}

esp_err_t handle_ap_stop(RequestContext& ctx) {
    Result<Unit> stopped = ctx.server.service().stop_ap();
    if (!stopped) return send_service_error(ctx.req, result_error(stopped));
    return send_no_content(ctx.req);
}

esp_err_t handle_ap_extend(RequestContext& ctx) {
    Cents seconds = 600;
    if (ctx.req->content_len > 0) {
        Result<json> body = read_json_body(ctx.req, kMaxJsonBody);
        if (!body) return send_service_error(ctx.req, result_error(body));
        if (!body.value->is_object()) {
            return send_error(ctx.req, 400, "invalid_input", "JSON-Objekt erwartet");
        }
        if (body.value->contains("seconds")) {
            Result<Cents> parsed = get_cents(*body.value, "seconds", false, 60, 3600);
            if (!parsed) return send_service_error(ctx.req, result_error(parsed));
            seconds = *parsed.value;
        }
    }
    Result<Unit> extended = ctx.server.service().extend_ap(static_cast<int>(seconds));
    if (!extended) return send_service_error(ctx.req, result_error(extended));
    return send_json(ctx.req, 200, ap_status_json(ctx.server.service().network_status()));
}

// --- System --------------------------------------------------------------

esp_err_t handle_device(RequestContext& ctx) {
    return send_json(ctx.req, 200, device_status_json(ctx.server.platform().stats()));
}

esp_err_t handle_display_test(RequestContext& ctx) {
    Result<Unit> shown = ctx.server.platform().show_display_test();
    if (!shown) return send_service_error(ctx.req, result_error(shown));
    return send_no_content(ctx.req);
}

esp_err_t handle_logs(RequestContext& ctx) {
    int64_t limit = query_int(req_query(ctx.req), "limit").value_or(200);
    if (limit < 1) limit = 1;
    if (limit > 500) limit = 500;
    json lines = json::array();
    for (const std::string& line : log_ring_lines(static_cast<size_t>(limit))) {
        lines.push_back(line);
    }
    return send_json(ctx.req, 200, json{{"lines", std::move(lines)}});
}

esp_err_t handle_backup(RequestContext& ctx) {
    Result<std::string> data = ctx.server.service().export_backup();
    if (!data) return send_service_error(ctx.req, result_error(data));
    const Timestamp now = ctx.server.platform().time_info().now;
    const std::string disposition = content_disposition(backup_filename(now));
    return send_chunked_text(ctx.req, 200, "application/json; charset=utf-8", *data.value,
                             disposition.c_str());
}

esp_err_t handle_restore(RequestContext& ctx) {
    Result<std::string> body = read_body(ctx.req, kMaxRestoreBody);
    if (!body) return send_service_error(ctx.req, result_error(body));
    if (body.value->empty()) {
        return send_error(ctx.req, 400, "invalid_input", "Sicherung fehlt");
    }
    Result<Unit> restored =
        ctx.server.service().import_backup(*body.value, request_id_or_generated(ctx, "restore"));
    if (!restored) return send_service_error(ctx.req, result_error(restored));
    return send_no_content(ctx.req);
}

}  // namespace

// Route table. Order matters only for readability; paths are exact per segment.
const std::vector<Route>& routes() {
    static const std::vector<Route> kRoutes = {
        {HTTP_GET, "/api/v1/health", AuthPolicy::None, &handle_health, false},
        {HTTP_POST, "/api/v1/auth/login", AuthPolicy::None, &handle_login, false},
        {HTTP_POST, "/api/v1/auth/logout", AuthPolicy::SessionCsrf, &handle_logout, false},
        {HTTP_GET, "/api/v1/auth/me", AuthPolicy::Session, &handle_me, false},

        {HTTP_GET, "/api/v1/users", AuthPolicy::Session, &handle_users_list, false},
        {HTTP_POST, "/api/v1/users", AuthPolicy::SessionCsrf, &handle_users_create, true},
        {HTTP_PATCH, "/api/v1/users/{id}", AuthPolicy::SessionCsrf, &handle_users_update, true},
        {HTTP_POST, "/api/v1/users/{id}/archive", AuthPolicy::SessionCsrf, &handle_users_archive,
         true},
        {HTTP_POST, "/api/v1/users/{id}/topup", AuthPolicy::SessionCsrf, &handle_users_topup, true},
        {HTTP_POST, "/api/v1/users/{id}/adjust", AuthPolicy::SessionCsrf, &handle_users_adjust,
         true},
        {HTTP_GET, "/api/v1/users/{id}/transactions", AuthPolicy::Session,
         &handle_users_transactions, false},

        {HTTP_GET, "/api/v1/products", AuthPolicy::Session, &handle_products_list, false},
        {HTTP_POST, "/api/v1/products", AuthPolicy::SessionCsrf, &handle_products_create, true},
        {HTTP_PATCH, "/api/v1/products/{id}", AuthPolicy::SessionCsrf, &handle_products_update,
         true},
        {HTTP_DELETE, "/api/v1/products/{id}", AuthPolicy::SessionCsrf, &handle_products_delete,
         true},

        {HTTP_GET, "/api/v1/transactions", AuthPolicy::Session, &handle_transactions_list, false},
        {HTTP_POST, "/api/v1/transactions/{id}/void", AuthPolicy::SessionCsrf,
         &handle_transactions_void, true},

        {HTTP_GET, "/api/v1/expenses", AuthPolicy::Session, &handle_expenses_list, false},
        {HTTP_POST, "/api/v1/expenses", AuthPolicy::SessionCsrf, &handle_expenses_create, true},
        {HTTP_PATCH, "/api/v1/expenses/{id}", AuthPolicy::SessionCsrf, &handle_expenses_update,
         true},
        {HTTP_POST, "/api/v1/expenses/{id}/void", AuthPolicy::SessionCsrf, &handle_expenses_void,
         true},

        {HTTP_GET, "/api/v1/cash-counts", AuthPolicy::Session, &handle_cash_counts_list, false},
        {HTTP_POST, "/api/v1/cash-counts", AuthPolicy::SessionCsrf, &handle_cash_counts_create,
         true},

        {HTTP_GET, "/api/v1/stats/summary", AuthPolicy::Session, &handle_summary, false},
        {HTTP_GET, "/api/v1/stats/leaderboard", AuthPolicy::Session, &handle_leaderboard, false},
        {HTTP_GET, "/api/v1/reports/{kind}", AuthPolicy::Session, &handle_reports, false},

        {HTTP_GET, "/api/v1/settings", AuthPolicy::Session, &handle_settings_get, false},
        {HTTP_PUT, "/api/v1/settings", AuthPolicy::SessionCsrf, &handle_settings_put, true},
        {HTTP_GET, "/api/v1/settings/display", AuthPolicy::Session, &handle_display_get, false},
        {HTTP_PUT, "/api/v1/settings/display", AuthPolicy::SessionCsrf, &handle_display_put, true},
        {HTTP_POST, "/api/v1/settings/password", AuthPolicy::SessionCsrf, &handle_password_change,
         false},

        {HTTP_GET, "/api/v1/time", AuthPolicy::Session, &handle_time_get, false},
        {HTTP_POST, "/api/v1/time/sync", AuthPolicy::SessionCsrf, &handle_time_sync, false},

        {HTTP_GET, "/api/v1/ap", AuthPolicy::Session, &handle_ap_status, false},
        {HTTP_POST, "/api/v1/ap/start", AuthPolicy::SessionCsrf, &handle_ap_start, true},
        {HTTP_POST, "/api/v1/ap/stop", AuthPolicy::SessionCsrf, &handle_ap_stop, false},
        {HTTP_POST, "/api/v1/ap/extend", AuthPolicy::SessionCsrf, &handle_ap_extend, false},

        {HTTP_GET, "/api/v1/device", AuthPolicy::Session, &handle_device, false},
        {HTTP_POST, "/api/v1/display/test", AuthPolicy::SessionCsrf, &handle_display_test, false},
        {HTTP_GET, "/api/v1/logs", AuthPolicy::Session, &handle_logs, false},
        {HTTP_GET, "/api/v1/backup", AuthPolicy::Session, &handle_backup, false},
        {HTTP_POST, "/api/v1/restore", AuthPolicy::SessionCsrf, &handle_restore, false},
        {HTTP_POST, "/api/v1/ota", AuthPolicy::SessionCsrf, &handle_ota, false},
    };
    return kRoutes;
}

}  // namespace api
}  // namespace kaffee
