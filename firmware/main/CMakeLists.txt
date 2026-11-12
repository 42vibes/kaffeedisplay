idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES board domain storage service ui net api webassets nvs_flash esp_event lvgl
)
