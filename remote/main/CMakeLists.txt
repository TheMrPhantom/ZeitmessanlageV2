idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS ""
    REQUIRES driver dd-ota esp_event esp_hw_support esp_netif esp_timer esp_wifi nvs_flash
)
