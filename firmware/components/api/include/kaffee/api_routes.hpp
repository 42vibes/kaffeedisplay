#pragma once

// Route metadata for the admin API. Kept free of ESP-IDF types so the contract
// (methods, auth policy, request-id requirement) can be unit-tested on the host.
// `api_handlers.cpp` binds handlers to these entries in the same order.

#include <string_view>
#include <vector>

namespace kaffee {
namespace api {

enum class AuthPolicy {
    None,         // public route (health, login, captive probes)
    Session,      // valid session cookie
    SessionCsrf,  // valid session + matching X-CSRF-Token
};

enum class RouteMethod { Get, Post, Put, Patch, Delete };

struct RouteSpec {
    RouteMethod method;
    const char* pattern;  // absolute path, e.g. "/api/v1/users/{id}"
    AuthPolicy auth;
    bool require_request_id;
};

const std::vector<RouteSpec>& route_specs();

// True when a session may call the route while the admin password still has to
// be changed. Everything else is rejected with 403 `password_change_required`
// so the SPA can force the password form after first login.
bool route_allowed_when_password_must_change(const RouteSpec& spec);

// Human/machine friendly names used by dispatch and tests.
const char* route_method_name(RouteMethod method);
const char* auth_policy_name(AuthPolicy policy);

}  // namespace api
}  // namespace kaffee
