# The generated SPA component (T-007) is optional: a clean checkout without it
# still builds and serves the German fallback page. `__has_include` in
# api_assets.cpp selects the real table when the header is on the include path.
set(webassets_requires "")
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../webassets/include/kaffee/web_assets.hpp")
    set(webassets_requires webassets)
endif()

idf_component_register(
    SRCS
        "src/api.cpp"
        "src/api_assets.cpp"
        "src/api_handlers.cpp"
        "src/api_multipart.cpp"
        "src/api_ota.cpp"
        "src/api_routes.cpp"
        "src/api_util.cpp"
    INCLUDE_DIRS "include"
    REQUIRES service reports esp_http_server app_update ${webassets_requires}
    PRIV_REQUIRES esp_common esp_system log lwip
)
