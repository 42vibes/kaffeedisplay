#include <cstdio>
#include <string>
#include <string_view>

#include "api_internal.hpp"
#include "esp_log.h"

#if __has_include("kaffee/web_assets.hpp")
#include "kaffee/web_assets.hpp"
#else
// Safe fallback for builds without the generated `webassets` component
// (T-007). The shape matches kaffee::api::Asset so the serving code below is
// identical in both modes.
namespace kaffee {
namespace api {

struct Asset {
    std::string_view path;
    const char* content_type;
    std::string_view body;
    std::string_view gzip_body;
    bool immutable;
};

const Asset* kaffee_web_asset_lookup(std::string_view) {
    return nullptr;
}

}  // namespace api
}  // namespace kaffee
#endif

namespace kaffee {
namespace api {
namespace {

constexpr const char kFallbackIndex[] = R"HTML(<!doctype html>
<html lang="de">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Kaffeekasse</title>
<style>
  :root { color-scheme: dark; }
  body { margin: 0; min-height: 100vh; display: grid; place-items: center;
         background: #1a1410; color: #f3e6d8;
         font: 16px/1.5 system-ui, -apple-system, "Segoe UI", sans-serif; }
  main { max-width: 32rem; padding: 2rem; text-align: center; }
  h1 { font-size: 1.6rem; margin: 0 0 .75rem; color: #d4a373; }
  code { background: #2b211a; padding: .15rem .35rem; border-radius: .25rem; }
  p { margin: .6rem 0; color: #d9c7b6; }
</style>
</head>
<body>
<main>
  <h1>Kaffeekasse</h1>
  <p>Die Admin-Oberfläche ist in dieser Firmware nicht eingebettet.</p>
  <p>Bitte die Firmware mit <code>make build</code> neu bauen, damit die
     Web-Assets aus <code>web/dist</code> enthalten sind.</p>
  <p>Die Gerätebuchung funktioniert unabhängig davon.</p>
</main>
</body>
</html>
)HTML";

constexpr Asset kFallbackAssets[] = {
    {"/index.html", "text/html; charset=utf-8", kFallbackIndex, {}, false},
};

// Generated table first; the fallback page keeps clean clones usable when the
// generator has not run yet (empty table).
const Asset* find_asset(std::string_view path) {
    if (const Asset* asset = kaffee_web_asset_lookup(path)) return asset;
    for (const Asset& asset : kFallbackAssets) {
        if (asset.path == path) return &asset;
    }
    return nullptr;
}

const char* content_type_for(std::string_view path) {
    auto ends_with = [path](std::string_view suffix) {
        return path.size() >= suffix.size() &&
               path.compare(path.size() - suffix.size(), suffix.size(), suffix) == 0;
    };
    if (ends_with(".html")) return "text/html; charset=utf-8";
    if (ends_with(".js") || ends_with(".mjs")) return "text/javascript; charset=utf-8";
    if (ends_with(".css")) return "text/css; charset=utf-8";
    if (ends_with(".json") || ends_with(".webmanifest")) return "application/json; charset=utf-8";
    if (ends_with(".svg")) return "image/svg+xml";
    if (ends_with(".png")) return "image/png";
    if (ends_with(".jpg") || ends_with(".jpeg")) return "image/jpeg";
    if (ends_with(".webp")) return "image/webp";
    if (ends_with(".ico")) return "image/x-icon";
    if (ends_with(".woff2")) return "font/woff2";
    if (ends_with(".woff")) return "font/woff";
    if (ends_with(".txt") || ends_with(".map")) return "text/plain; charset=utf-8";
    return "application/octet-stream";
}

esp_err_t send_not_found(httpd_req_t* req) {
    httpd_resp_set_status(req, status_text(404));
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    set_security_headers(req);
    return httpd_resp_sendstr(req, "Nicht gefunden");
}

}  // namespace

esp_err_t handle_static(httpd_req_t* req) {
    std::string path = req_path(req);

    const std::string_view lookup =
        path == "/" ? std::string_view("/index.html") : std::string_view(path);
    const Asset* asset = find_asset(lookup);
    bool is_index = asset != nullptr && asset->path == "/index.html";
    if (asset == nullptr && is_spa_fallback_path(path)) {
        // Client-side route such as /users/abc.
        asset = find_asset("/index.html");
        path = "/index.html";
        is_index = asset != nullptr;
    }
    if (asset == nullptr) return send_not_found(req);

    const StaticPolicy policy = static_policy(path, is_index, "GET");
    const bool gzip = accepts_gzip(req_header(req, "Accept-Encoding").value_or(""));
    std::string_view body = asset->body;
    bool encoded = false;
    if (gzip && !asset->gzip_body.empty()) {
        body = asset->gzip_body;
        encoded = true;
    }
    const std::string etag = etag_for(asset->body);

    httpd_resp_set_hdr(req, "ETag", etag.c_str());
    httpd_resp_set_hdr(req, "Cache-Control", policy.cache_control);
    if (is_index) {
        // Defense in depth for the admin surface; no external origins.
        httpd_resp_set_hdr(req, "Content-Security-Policy",
                           "default-src 'self'; img-src 'self' data:; "
                           "style-src 'self' 'unsafe-inline'; script-src 'self'; "
                           "connect-src 'self'; base-uri 'none'; frame-ancestors 'none'");
    }

    auto if_none_match = req_header(req, "If-None-Match");
    if (if_none_match && etag_matches(*if_none_match, etag)) {
        httpd_resp_set_status(req, status_text(304));
        set_security_headers(req);
        return httpd_resp_send(req, nullptr, 0);
    }

    httpd_resp_set_status(req, status_text(200));
    httpd_resp_set_type(
        req, asset->content_type != nullptr ? asset->content_type : content_type_for(path));
    if (encoded) httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(req, "Vary", "Accept-Encoding");
    set_security_headers(req);
    // Only GET is routed here, so the body is always produced. HEAD is rejected
    // in the dispatcher because esp_http_server manages Content-Length itself.
    return httpd_resp_send(req, body.data(), body.size());
}

}  // namespace api
}  // namespace kaffee
