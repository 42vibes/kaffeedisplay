#include "kaffee/web_assets.hpp"

// `tools/embed_web.py` writes this include file (gitignored). Keeping the
// generated payload in a `.inc` included from a stable source file means a
// clean clone builds without running the generator, and no CMake reconfigure is
// needed when assets are regenerated later.
#if __has_include("../generated/web_assets_generated.inc")
#include "../generated/web_assets_generated.inc"
#else
namespace kaffee::api {

const Asset* kaffee_web_asset_lookup(std::string_view) {
    return nullptr;
}

}  // namespace kaffee::api

namespace kaffee::web {

const Asset* assets() {
    return nullptr;
}

std::size_t asset_count() {
    return 0;
}

const Asset* find(std::string_view) {
    return nullptr;
}

}  // namespace kaffee::web
#endif
