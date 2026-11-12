#pragma once

#include <cstddef>
#include <string_view>

// Embedded admin SPA table.
//
// The table is generated at build time by `tools/embed_web.py` from `web/dist`;
// it is never checked in. The `kaffee::api::Asset` layout and the
// `kaffee_web_asset_lookup()` symbol are the contract expected by
// `firmware/components/api/src/api_assets.cpp`.
namespace kaffee::api {

struct Asset {
    std::string_view path;
    const char* content_type;
    std::string_view body;
    std::string_view gzip_body;
    bool immutable;
};

// Returns nullptr for unknown paths. The returned pointers stay valid for the
// lifetime of the application.
const Asset* kaffee_web_asset_lookup(std::string_view path);

}  // namespace kaffee::api

namespace kaffee::web {

using Asset = kaffee::api::Asset;

// Convenience helpers for tests and non-API consumers.
const Asset* assets();
std::size_t asset_count();
const Asset* find(std::string_view path);

}  // namespace kaffee::web
