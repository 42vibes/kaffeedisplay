# kaffee/webassets

Compile-time embedding of the admin SPA (`web/dist`) into the firmware image.
The REST/static-file code in `components/api` consumes this component; it never
touches `web/dist` directly.

## Interface

The generated table matches the adapter expected by
`firmware/components/api/src/api_assets.cpp` exactly:

```cpp
namespace kaffee::api {
struct Asset {
    std::string_view path;
    const char* content_type;
    std::string_view body;       // raw bytes
    std::string_view gzip_body;  // gzip bytes, empty only if not generated
    bool immutable;              // true for hashed /assets/* names
};
const Asset* kaffee_web_asset_lookup(std::string_view path);  // nullptr if absent
}
```

`kaffee/web_assets.hpp` also exposes `kaffee::web::find()`, `assets()` and
`asset_count()` for tests and non-API consumers. The lookup normalises `/`,
trailing slashes, query strings and fragments.

`components/main/CMakeLists.txt` already has `webassets` in `REQUIRES`, and
`main.cpp` references `kaffee::web::assets()` so the SPA is linked even before
the API adapter is enabled. `strings firmware.bin` therefore contains
`/assets/index-<hash>.js`.

## API integration

The API branch keeps its adapter behind `KAFFEE_API_WEB_ASSETS`. To enable it:

1. Add `webassets` to the API component's `REQUIRES` (or uncomment the block in
   `components/api/CMakeLists.txt`) and define `KAFFEE_API_WEB_ASSETS`.
2. Move the `kaffee_web_asset_lookup()` declaration in `api_assets.cpp` **out of
   the anonymous namespace** (or include `kaffee/web_assets.hpp`). The generated
   symbol has external `kaffee::api` linkage; a declaration inside an anonymous
   namespace will not resolve.
3. Keep the `Asset` field order/types exactly as above.

Do not copy `web/dist` into the source tree. `tools/embed_web.py` is the only
producer.

## Regeneration

```sh
make web          # npm --prefix web run build
make assets       # python3 tools/embed_web.py
```

`make build`, `make check` and CI run both automatically. Generated files live in
`generated/` and are gitignored. `src/web_assets.cpp` includes
`generated/web_assets_generated.inc` when present and defines an empty table
otherwise, so a clean clone always builds.
