idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        driver
        esp_driver_i2c
        esp_timer
        esp_lvgl_port
        esphome__esp-hub75
        lvgl
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
