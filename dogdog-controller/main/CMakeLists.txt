idf_component_register(SRC_DIRS "src"
                    REQUIRES nvs_flash driver esp_timer esp_event esp_netif esp_wifi lora-network spi_flash timepanel-protocol dd-ota
                    INCLUDE_DIRS "include"
                    INCLUDE_DIRS ".")
