set(srcs "main.cpp" "walltime.cpp" "storage.cpp" "radio.cpp" "wifiscan.cpp" "power.cpp" "audio.cpp")
if(CONFIG_SQUATCH_BOARD_AMOLED_18)
    list(APPEND srcs "board_amoled18.c")
else()
    list(APPEND srcs "board.c" "st7701_init.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "."
    REQUIRES sim scene esp_lcd esp_driver_i2c esp_driver_ledc esp_driver_gpio esp_driver_i2s esp_timer esp_wifi esp_netif
             esp_event nvs_flash)
