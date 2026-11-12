idf_component_register(
    SRCS "src/board.cpp"
    INCLUDE_DIRS "include"
    REQUIRES domain driver esp_lcd esp_lcd_st7701 esp_lcd_touch_gt911 esp_lvgl_port lvgl
)
