idf_component_register(SRC_DIRS "src"
                    REQUIRES driver nvs_flash esp_timer
                    INCLUDE_DIRS "include"
                    INCLUDE_DIRS "."
                    REQUIRES lora-network)