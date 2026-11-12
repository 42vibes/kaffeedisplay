#pragma once

// Internal API component plumbing. Not installed as a public header.

#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "esp_http_server.h"
#include "kaffee/api.hpp"
#include "kaffee/api_routes.hpp"
#include "kaffee/api_util.hpp"
#include "kaffee/domain.hpp"
#include "kaffee/json.hpp"

namespace kaffee {
namespace api {

// Request body limits. OTA is streamed and only bounded by the partition size.
constexpr size_t kMaxJsonBody = 32u * 1024u;
constexpr size_t kMaxRestoreBody = 4u * 1024u * 1024u;
constexpr size_t kMaxOtaBytes = 3u * 1024u * 1024u;
constexpr const char* kApiPrefix = "/api/v1";

class ApiServerImpl;

struct RequestContext {
    ApiServerImpl& server;
    httpd_req_t* req;
    RouteParams params;
    std::string token;  // session token when authenticated
    Session session;
    bool authenticated = false;
    std::string request_id;  // validated X-Request-Id for ledger writes
};

using RouteHandler = esp_err_t (*)(RequestContext&);

// Handlers in the same order as `route_specs()` (checked at dispatch time).
const std::vector<RouteHandler>& route_handlers();

class ApiServerImpl final : public ApiServer {
  public:
    ApiServerImpl(KaffeeService& service, IPlatform& platform);
    ~ApiServerImpl() override;

    Result<Unit> start() override;
    Result<Unit> stop() override;
    bool running() const override;

    KaffeeService& service() {
        return service_;
    }
    IPlatform& platform() {
        return platform_;
    }

    // Used by the OTA handler: reboot only after the response has been sent.
    void schedule_reboot(uint32_t delay_ms);
    // Used by POST /factory-reset: answer first, then erase storage/settings/net
    // credentials and reboot from a helper task.
    void schedule_factory_reset();

  private:
    Result<Unit> stop_locked();
    KaffeeService& service_;
    IPlatform& platform_;
    httpd_handle_t server_ = nullptr;
    mutable std::mutex state_mutex_;
    std::atomic<bool> stop_pending_{false};
};

// --- Responses -----------------------------------------------------------

const char* status_text(int status);
void set_security_headers(httpd_req_t* req);
esp_err_t send_json_text(httpd_req_t* req, int status, std::string_view body);
esp_err_t send_json(httpd_req_t* req, int status, const nlohmann::json& body);
esp_err_t send_error(httpd_req_t* req, int status, std::string_view code, std::string_view message);
esp_err_t send_service_error(httpd_req_t* req, const Error& error);
esp_err_t send_no_content(httpd_req_t* req);
// Streams a potentially large body with httpd_resp_send_chunk in fixed slices.
esp_err_t send_chunked_text(httpd_req_t* req, int status, const char* content_type,
                            std::string_view body, const char* content_disposition = nullptr);

// --- Requests ------------------------------------------------------------

std::optional<std::string> req_header(httpd_req_t* req, const char* name);
std::string req_path(httpd_req_t* req);   // URI without the query string
std::string req_query(httpd_req_t* req);  // query string without '?'
std::string client_ip(httpd_req_t* req);
Result<std::string> read_body(httpd_req_t* req, size_t limit);
Result<nlohmann::json> read_json_body(httpd_req_t* req, size_t limit);
// Same as read_json_body but rejects non-object roots with 400.
Result<nlohmann::json> read_json_object(httpd_req_t* req, size_t limit);
// Re-issues the session cookie (sliding expiry); used by /auth/me and login.
void set_session_cookie(httpd_req_t* req, std::string_view token, Timestamp expires_at,
                        Timestamp now);
void clear_session_cookie(httpd_req_t* req);

// Copies the error out of a failed Result<T>.
template <typename T>
Error result_error(const Result<T>& result) {
    return result.error.value_or(Error{"internal_error", "Unerwarteter Fehler"});
}

// --- Handlers ------------------------------------------------------------

esp_err_t handle_static(httpd_req_t* req);
esp_err_t handle_ota(RequestContext& ctx);

// --- Log ring (Installed on start, exposed by GET /logs) -----------------

void log_ring_install();
void log_ring_uninstall();
std::vector<std::string> log_ring_lines(size_t limit);

}  // namespace api
}  // namespace kaffee
