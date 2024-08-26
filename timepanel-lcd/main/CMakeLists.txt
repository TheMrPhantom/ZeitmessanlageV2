set(COMPONENT_REQUIRES "Adafruit_GFX")
set(COMPONENT_REQUIRES "Adafruit_ST7735")

idf_component_register(SRCS "Display.cpp" "KeyValue.cpp" "NetworkFault.cpp" "Timer.cpp" "main.cpp" "Network.cpp" "Sensor.cpp"
                    INCLUDE_DIRS ".")
