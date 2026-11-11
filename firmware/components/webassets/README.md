# kaffee/webassets

Compile-time embedding of the admin SPA (`web/dist`) into the firmware image.
The REST/static-file code in `components/api` consumes this component; it never
touches `web/dist` directly.

## Interface

```cpp
#include "kaffee/web_assets.hpp"

const kaffee::web::WebAsset* asset = kaffee::web::find("/");
const kaffee::web::WebAsset* all = kaffee::web::assets();
std::size_t count = kaffee::web::asset_count();
```

`WebAsset` contains `path`, `mime`, gzip `data`, compressed `size`, `gzip`
(true) and `original_size`. `find()` normalises `/`, trailing slashes, query
strings and fragments, and returns `nullptr` for unknown paths. The API should
return `/index.html` (SPA fallback) when `find()` misses and the path has no file
extension.

## Regeneration

```sh
make web          # npm --prefix web run build
make assets       # python3 tools/embed_web.py
```

`make build`, `make check` and CI run both automatically. Generated files live in
`generated/` and are gitignored. On a clean clone without generated assets the
component links `src/web_assets_stub.cpp` (empty table) so the firmware still
builds; the warning tells you to run `make assets`.

## API integration

Add the component to the API's `REQUIRES` list (one line, coordinated with the
API track):

```cmake
idf_component_register(
    SRCS "src/api.cpp"
    INCLUDE_DIRS "include"
    REQUIRES service esp_http_server app_update webassets
)
```

Serve `asset->data` with `Content-Encoding: gzip` when `asset->gzip` is true and
`Content-Length: asset->size`. Do not cache `index.html`.
