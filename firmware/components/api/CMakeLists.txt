idf_component_register(
    SRCS
        "src/api.cpp"
        "src/api_assets.cpp"
        "src/api_handlers.cpp"
        "src/api_ota.cpp"
        "src/api_util.cpp"
    INCLUDE_DIRS "include"
    REQUIRES service esp_http_server app_update
    PRIV_REQUIRES esp_common esp_system log lwip
)

# The tooling track (T-007) embeds the admin SPA. When that component exists,
# add it to REQUIRES, extend the include path and define KAFFEE_API_WEB_ASSETS;
# `api_assets.cpp` is the only file that has to be adapted.
#
# find_path(WEB_ASSETS_INCLUDE kaffee/web_assets.hpp PATHS
#           ${CMAKE_CURRENT_LIST_DIR}/../web_assets/include NO_DEFAULT_PATH)
# if(WEB_ASSETS_INCLUDE)
#   target_include_directories(${COMPONENT_LIB} PRIVATE ${WEB_ASSETS_INCLUDE})
#   target_compile_definitions(${COMPONENT_LIB} PRIVATE KAFFEE_API_WEB_ASSETS)
# endif()
