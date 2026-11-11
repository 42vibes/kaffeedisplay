#include "kaffee/web_assets.hpp"

namespace kaffee::web {

// Used when `make assets` / `tools/embed_web.py` has not run yet. The firmware
// still builds, but the admin SPA is unavailable and the API should return 404.
const WebAsset* assets() {
    return nullptr;
}

std::size_t asset_count() {
    return 0;
}

const WebAsset* find(std::string_view) {
    return nullptr;
}

}  // namespace kaffee::web
