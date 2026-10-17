idf_component_register(
    SRCS "main.cpp" "board.c" "st7701_init.c" "walltime.cpp" "storage.cpp" "radio.cpp" "wifiscan.cpp"
    INCLUDE_DIRS "."
    REQUIRES sim scene esp_lcd esp_driver_i2c esp_driver_ledc esp_driver_gpio esp_timer esp_wifi esp_netif
             esp_event nvs_flash)
