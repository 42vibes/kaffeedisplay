idf_component_register(
    SRCS "main.cpp" "platform_adapter.cpp" "api_controller.cpp"
    INCLUDE_DIRS "."
    REQUIRES board domain storage service ui net api reports webassets nvs_flash littlefs
             esp_app_format esp_event lvgl
)
