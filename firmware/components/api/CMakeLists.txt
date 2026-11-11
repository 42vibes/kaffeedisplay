idf_component_register(
    SRCS "src/api.cpp"
    INCLUDE_DIRS "include"
    REQUIRES service esp_http_server app_update
)
