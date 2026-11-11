idf_component_register(
    SRCS "src/net_manager.cpp" "src/net_pure.cpp"
    INCLUDE_DIRS "include"
    REQUIRES service esp_common esp_wifi esp_netif esp_event esp_hw_support freertos mdns lwip nvs_flash esp_timer
)
