idf_component_register(SRCS "bist_hmi.c" "tab5_display.c"
                       INCLUDE_DIRS "."
                       REQUIRES m5stack_tab5 espressif__esp_lcd_st7121 esp_lcd esp_driver_i2c
                                espressif__esp_lcd_touch modbus_bist_master)
