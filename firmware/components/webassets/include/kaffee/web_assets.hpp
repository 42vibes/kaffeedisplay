#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace kaffee::web {

// One file of the embedded admin SPA. The table is generated at build time by
// `tools/embed_web.py` from `web/dist`; it is never checked in.
struct WebAsset {
    // Absolute request path with a leading slash, e.g. "/index.html".
    const char* path;
    // Value for the HTTP Content-Type header.
    const char* mime;

    // gzip-compressed payload. Valid for the lifetime of the application.
    const std::uint8_t* data;
    // Compressed payload size in bytes (HTTP Content-Length).
    std::size_t size;
    // Always true for generated assets; present so the server can decide.
    bool gzip;
    // Uncompressed size, useful for diagnostics and size accounting.
    std::size_t original_size;
};

// Sorted-by-path asset table. Returns nullptr when no assets were embedded.
const WebAsset* assets();

// Number of entries in `assets()`; 0 for the stub build.
std::size_t asset_count();

// Looks up an asset by request path. Accepts "/", "/index.html", "index.html",
// directory paths with a trailing slash and ignores query/fragment suffixes.
// Returns nullptr when the path is not bundled (the caller may serve the SPA
// fallback, i.e. "/index.html").
const WebAsset* find(std::string_view path);

}  // namespace kaffee::web
