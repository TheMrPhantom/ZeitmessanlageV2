idf_component_register(SRC_DIRS "src"
                    REQUIRES driver esp_timer nvs_flash
                    INCLUDE_DIRS "include"
                    INCLUDE_DIRS "."
                    REQUIRES lora-network)