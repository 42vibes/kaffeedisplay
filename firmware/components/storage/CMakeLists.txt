idf_component_register(
    SRCS
        "src/storage.cpp"
        "src/journal_store.cpp"
        "src/littlefs_store.cpp"
        "src/nvs_settings_store.cpp"
    INCLUDE_DIRS "include"
    REQUIRES domain littlefs nvs_flash
)
