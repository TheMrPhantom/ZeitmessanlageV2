idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS ""
    REQUIRES driver esp_driver_rmt esp_http_server esp_timer esp_wifi esp_netif nvs_flash lwip
)
