idf_component_register(
    SRCS "src/net.cpp"
    INCLUDE_DIRS "include"
    REQUIRES service esp_wifi esp_netif mdns lwip nvs_flash esp_timer
)
