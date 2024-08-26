# Adafruit GFX Library
# https://github.com/adafruit/Adafruit-GFX-Library
# BSD License

cmake_minimum_required(VERSION 3.5)

idf_component_register(SRCS "Adafruit_ST7789.cpp" "Adafruit_ST7735.cpp" "Adafruit_ST7735.cpp" "Adafruit_ST77xx.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES "arduino-esp32 Adafruit_BusIO Adafruit_GFX")

project(Adafruit_ST7735)
