idf_component_register(
    SRCS "src/board.cpp"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES
        domain
        esp_lcd
        esp_driver_i2c
        esp_driver_tsens
        esp_lcd_touch
        esp_lcd_touch_gt911
        lvgl
    PRIV_REQUIRES
        driver
        esp_lcd_st7701
        esp_lcd_panel_io_additions
        esp_lvgl_port
        esp_mm
        esp_timer
)
