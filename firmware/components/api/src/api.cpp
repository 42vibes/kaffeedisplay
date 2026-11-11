#include "api_internal.hpp"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

namespace kaffee {
namespace api {
namespace {

constexpr const char* kTag = "kaffee.api";

// Response helper for the request id echo. The HTTP server task handles one
// request at a time, but thread_local keeps this correct if that ever changes.
thread_local std::string t_response_request_id;

esp_err_t dispatch_entry(httpd_req_t* req) {
    auto* self = static_cast<ApiServerImpl*>(req->user_ctx);
    if (self == nullptr) return ESP_FAIL;

    const std::string path = req_path(req);
    if (path.rfind("/api/", 0) != 0) {
        if (req->method != HTTP_GET && req->method != HTTP_HEAD) {
            return send_error(req, 405, "method_not_allowed", "Methode nicht erlaubt");
        }
        return handle_static(req);
    }

    const std::string query = req_query(req);
    bool path_matched = false;
    for (const Route& route : routes()) {
        RouteParams params;
        if (!match_path(route.pattern, path, params)) continue;
        path_matched = true;
        if (route.method != req->method) continue;

        RequestContext ctx{*self, req, std::move(params), {}, {}, false, {}};

        if (route.auth != AuthPolicy::None) {
            auto cookie = req_header(req, "Cookie");
            auto token = cookie ? cookie_value(*cookie, "kaffee_session") : std::nullopt;
            if (!token || token->empty()) {
                return send_error(req, 401, "unauthorized", "Anmeldung erforderlich");
            }
            Result<Session> session = self->service().validate_session(*token);
            if (!session) {
                return send_error(req, 401, "unauthorized", "Sitzung abgelaufen");
            }
            ctx.token = *token;
            ctx.session = *session.value;
            ctx.authenticated = true;

            if (route.auth == AuthPolicy::SessionCsrf) {
                auto csrf = req_header(req, "X-CSRF-Token");
                if (!csrf || csrf->empty() || !self->service().validate_csrf(*token, *csrf)) {
                    return send_error(req, 403, "csrf_failed",
                                      "CSRF-Token fehlt oder ist ungültig");
                }
            }
        }

        auto request_id = req_header(req, "X-Request-Id");
        std::string trimmed = request_id ? trim(*request_id) : std::string();
        if (!trimmed.empty() && is_valid_request_id(trimmed)) {
            ctx.request_id = trimmed;
        }
        if (route.require_request_id) {
            if (trimmed.empty()) {
                return send_error(req, 400, "missing_request_id",
                                  "Header X-Request-Id ist erforderlich");
            }
            if (!is_valid_request_id(trimmed)) {
                return send_error(req, 400, "invalid_request_id",
                                  "Header X-Request-Id ist ungültig");
            }
        }

        t_response_request_id = ctx.request_id;
        esp_err_t result = ESP_OK;
        try {
            result = route.handler(ctx);
        } catch (...) {
            result = send_error(req, 500, "internal_error", "Interner Fehler");
        }
        t_response_request_id.clear();
        return result;
    }

    if (path_matched) {
        return send_error(req, 405, "method_not_allowed", "Methode nicht erlaubt");
    }
    return send_error(req, 404, "not_found", "Ressource nicht gefunden");
}

void reboot_task(void* arg) {
    const uint32_t delay_ms = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(arg));
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
    esp_restart();
}

}  // namespace

// --- Lifecycle -----------------------------------------------------------

ApiServerImpl::ApiServerImpl(KaffeeService& service, IPlatform& platform)
    : service_(service), platform_(platform) {}

ApiServerImpl::~ApiServerImpl() {
    stop();
}

Result<Unit> ApiServerImpl::start() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (server_ != nullptr) return Result<Unit>::ok(Unit{});

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_open_sockets = 7;
    config.max_uri_handlers = 8;
    config.stack_size = 10240;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 30;
    config.send_wait_timeout = 30;
    config.uri_match_fn = httpd_uri_match_wildcard;

    httpd_handle_t server = nullptr;
    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "httpd_start failed: %s", esp_err_to_name(err));
        return Result<Unit>::fail("unavailable", "HTTP-Server konnte nicht gestartet werden");
    }

    httpd_uri_t catch_all{};
    catch_all.uri = "/*";
    catch_all.method = static_cast<httpd_method_t>(HTTP_ANY);
    catch_all.handler = &dispatch_entry;
    catch_all.user_ctx = this;
    err = httpd_register_uri_handler(server, &catch_all);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "httpd_register_uri_handler failed: %s", esp_err_to_name(err));
        httpd_stop(server);
        return Result<Unit>::fail("unavailable", "HTTP-Routen konnten nicht registriert werden");
    }

    server_ = server;
    log_ring_install();
    ESP_LOGI(kTag, "HTTP API listening on port %d", config.server_port);
    return Result<Unit>::ok(Unit{});
}

Result<Unit> ApiServerImpl::stop() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (server_ == nullptr) return Result<Unit>::ok(Unit{});
    httpd_handle_t server = server_;
    server_ = nullptr;
    esp_err_t err = httpd_stop(server);
    log_ring_uninstall();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(kTag, "httpd_stop failed: %s", esp_err_to_name(err));
        return Result<Unit>::fail("unavailable", "HTTP-Server konnte nicht gestoppt werden");
    }
    ESP_LOGI(kTag, "HTTP API stopped");
    return Result<Unit>::ok(Unit{});
}

bool ApiServerImpl::running() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return server_ != nullptr;
}

void ApiServerImpl::schedule_reboot(uint32_t delay_ms) {
    xTaskCreate(&reboot_task, "api_reboot", 2048, reinterpret_cast<void*>(delay_ms), 5, nullptr);
}

// --- Responses -----------------------------------------------------------

const char* status_text(int status) {
    switch (status) {
        case 200:
            return "200 OK";
        case 201:
            return "201 Created";
        case 204:
            return "204 No Content";
        case 304:
            return "304 Not Modified";
        case 400:
            return "400 Bad Request";
        case 401:
            return "401 Unauthorized";
        case 403:
            return "403 Forbidden";
        case 404:
            return "404 Not Found";
        case 405:
            return "405 Method Not Allowed";
        case 409:
            return "409 Conflict";
        case 411:
            return "411 Length Required";
        case 413:
            return "413 Payload Too Large";
        case 415:
            return "415 Unsupported Media Type";
        case 429:
            return "429 Too Many Requests";
        case 500:
            return "500 Internal Server Error";
        case 501:
            return "501 Not Implemented";
        case 503:
            return "503 Service Unavailable";
        default:
            return "500 Internal Server Error";
    }
}

void set_security_headers(httpd_req_t* req) {
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(req, "X-Frame-Options", "DENY");
    httpd_resp_set_hdr(req, "Referrer-Policy", "no-referrer");
    if (!t_response_request_id.empty()) {
        httpd_resp_set_hdr(req, "X-Request-Id", t_response_request_id.c_str());
    }
}

esp_err_t send_json_text(httpd_req_t* req, int status, std::string_view body) {
    httpd_resp_set_status(req, status_text(status));
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    set_security_headers(req);
    return httpd_resp_send(req, body.data(), body.size());
}

esp_err_t send_json(httpd_req_t* req, int status, const nlohmann::json& body) {
    const std::string text = body.dump();
    return send_json_text(req, status, text);
}

esp_err_t send_error(httpd_req_t* req, int status, std::string_view code,
                     std::string_view message) {
    return send_json_text(req, status, json_error_body(code, message));
}

esp_err_t send_service_error(httpd_req_t* req, const Error& error) {
    const std::string code = error.code.empty() ? "internal_error" : error.code;
    const std::string message = error.message.empty() ? "Unerwarteter Fehler" : error.message;
    return send_error(req, http_status_for_error(code), code, message);
}

esp_err_t send_no_content(httpd_req_t* req) {
    httpd_resp_set_status(req, status_text(204));
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    set_security_headers(req);
    return httpd_resp_send(req, nullptr, 0);
}

esp_err_t send_chunked_text(httpd_req_t* req, int status, const char* content_type,
                            std::string_view body, const char* content_disposition) {
    httpd_resp_set_status(req, status_text(status));
    httpd_resp_set_type(req, content_type);
    if (content_disposition != nullptr) {
        httpd_resp_set_hdr(req, "Content-Disposition", content_disposition);
    }
    set_security_headers(req);

    constexpr size_t kSlice = 4096;
    size_t offset = 0;
    while (offset < body.size()) {
        size_t len = std::min(kSlice, body.size() - offset);
        esp_err_t err = httpd_resp_send_chunk(req, body.data() + offset, len);
        if (err != ESP_OK) return err;
        offset += len;
    }
    return httpd_resp_send_chunk(req, nullptr, 0);
}

// --- Requests ------------------------------------------------------------

std::optional<std::string> req_header(httpd_req_t* req, const char* name) {
    size_t len = httpd_req_get_hdr_value_len(req, name);
    if (len == 0) return std::nullopt;
    std::string value(len + 1, '\0');
    if (httpd_req_get_hdr_value_str(req, name, value.data(), value.size()) != ESP_OK) {
        return std::nullopt;
    }
    value.resize(len);
    return value;
}

std::string req_path(httpd_req_t* req) {
    std::string uri(req->uri);
    size_t query = uri.find('?');
    if (query != std::string::npos) uri.resize(query);
    return uri;
}

std::string req_query(httpd_req_t* req) {
    std::string uri(req->uri);
    size_t query = uri.find('?');
    if (query == std::string::npos) return {};
    return uri.substr(query + 1);
}

std::string client_ip(httpd_req_t* req) {
    int fd = httpd_req_to_sockfd(req);
    if (fd < 0) return "0.0.0.0";
    struct sockaddr_in addr{};
    socklen_t len = sizeof(addr);
    if (getpeername(fd, reinterpret_cast<struct sockaddr*>(&addr), &len) != 0) {
        return "0.0.0.0";
    }
    const uint32_t host = ntohl(addr.sin_addr.s_addr);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u", static_cast<unsigned>((host >> 24) & 0xff),
                  static_cast<unsigned>((host >> 16) & 0xff),
                  static_cast<unsigned>((host >> 8) & 0xff), static_cast<unsigned>(host & 0xff));
    return buf;
}

Result<std::string> read_body(httpd_req_t* req, size_t limit) {
    if (req->content_len == 0) return Result<std::string>::ok(std::string());
    if (req->content_len > limit) {
        return Result<std::string>::fail("payload_too_large", "Anfrage ist zu groß");
    }
    std::string body(req->content_len, '\0');
    size_t received = 0;
    int timeouts = 0;
    while (received < body.size()) {
        int got = httpd_req_recv(req, body.data() + received, body.size() - received);
        if (got == HTTPD_SOCK_ERR_TIMEOUT) {
            if (++timeouts > 3) {
                return Result<std::string>::fail("bad_request", "Zeitüberschreitung beim Lesen");
            }
            continue;
        }
        if (got <= 0) {
            return Result<std::string>::fail("bad_request", "Anfrage konnte nicht gelesen werden");
        }
        received += static_cast<size_t>(got);
    }
    return Result<std::string>::ok(std::move(body));
}

Result<nlohmann::json> read_json_body(httpd_req_t* req, size_t limit) {
    Result<std::string> body = read_body(req, limit);
    if (!body)
        return Result<nlohmann::json>::fail(result_error(body).code, result_error(body).message);
    if (body.value->empty()) {
        return Result<nlohmann::json>::fail("invalid_input", "JSON-Körper fehlt");
    }
    nlohmann::json parsed = nlohmann::json::parse(*body.value, nullptr, false);
    if (parsed.is_discarded()) {
        return Result<nlohmann::json>::fail("invalid_input", "JSON ist ungültig");
    }
    return Result<nlohmann::json>::ok(std::move(parsed));
}

// --- Log ring ------------------------------------------------------------

namespace {

constexpr size_t kLogLines = 48;
constexpr size_t kLogLineLength = 160;

struct LogRing {
    char lines[kLogLines][kLogLineLength];
    size_t next = 0;
    size_t count = 0;
    bool installed = false;
    vprintf_like_t previous = nullptr;
};

LogRing g_log_ring;
portMUX_TYPE g_log_lock = portMUX_INITIALIZER_UNLOCKED;

int log_hook(const char* format, va_list args) {
    char line[kLogLineLength];
    va_list copy;
    va_copy(copy, args);
    int written = std::vsnprintf(line, sizeof(line), format, copy);
    va_end(copy);
    if (written > 0) {
        size_t length = std::strlen(line);
        while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r')) {
            line[--length] = '\0';
        }
        portENTER_CRITICAL(&g_log_lock);
        std::memcpy(g_log_ring.lines[g_log_ring.next], line, length + 1);
        g_log_ring.next = (g_log_ring.next + 1) % kLogLines;
        if (g_log_ring.count < kLogLines) ++g_log_ring.count;
        portEXIT_CRITICAL(&g_log_lock);
    }
    vprintf_like_t previous = nullptr;
    portENTER_CRITICAL(&g_log_lock);
    previous = g_log_ring.previous;
    portEXIT_CRITICAL(&g_log_lock);
    if (previous != nullptr) return previous(format, args);
    return written;
}

}  // namespace

void log_ring_install() {
    portENTER_CRITICAL(&g_log_lock);
    if (!g_log_ring.installed) {
        g_log_ring.previous = esp_log_set_vprintf(&log_hook);
        g_log_ring.installed = true;
    }
    portEXIT_CRITICAL(&g_log_lock);
}

void log_ring_uninstall() {
    portENTER_CRITICAL(&g_log_lock);
    if (g_log_ring.installed) {
        esp_log_set_vprintf(g_log_ring.previous);
        g_log_ring.installed = false;
        g_log_ring.previous = nullptr;
    }
    portEXIT_CRITICAL(&g_log_lock);
}

std::vector<std::string> log_ring_lines(size_t limit) {
    std::vector<std::string> out;
    if (limit == 0) return out;
    if (limit > kLogLines) limit = kLogLines;
    portENTER_CRITICAL(&g_log_lock);
    const size_t available = g_log_ring.count < limit ? g_log_ring.count : limit;
    const size_t first = (g_log_ring.next + kLogLines - available) % kLogLines;
    for (size_t i = 0; i < available; ++i) {
        out.emplace_back(g_log_ring.lines[(first + i) % kLogLines]);
    }
    portEXIT_CRITICAL(&g_log_lock);
    return out;
}

}  // namespace api
}  // namespace kaffee

namespace kaffee {

Result<std::unique_ptr<ApiServer>> ApiServer::create(KaffeeService& service, IPlatform& platform) {
    return Result<std::unique_ptr<ApiServer>>::ok(
        std::make_unique<api::ApiServerImpl>(service, platform));
}

}  // namespace kaffee
