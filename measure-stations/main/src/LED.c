#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "esp_log.h"
#include "esp_err.h"
#include "LED.h"

// GPIO assignment
#define LED_STRIP_GPIO_PIN 17
// 10MHz resolution, 1 tick = 0.1us (led strip needs a high resolution)
#define LED_STRIP_RMT_RES_HZ (10 * 1000 * 1000)

static const char *TAG = "LED";
led_strip_handle_t led_handle;
bool led_on_off = false;

extern TaskHandle_t sensorTask;

void init_led(int num_leds)
{
    // LED strip general initialization, according to your led board design
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_STRIP_GPIO_PIN,                        // The GPIO that connected to the LED strip's data line
        .max_leds = num_leds,                                        // The number of LEDs in the strip,
        .led_model = LED_MODEL_WS2812,                               // LED strip model
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB, // The color order of the strip: GRB
        .flags = {
            .invert_out = false, // don't invert the output signal
        }};

    // LED strip backend configuration: RMT
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,        // different clock source can lead to different power consumption
        .resolution_hz = LED_STRIP_RMT_RES_HZ, // RMT counter clock frequency
        .mem_block_symbols = 64,               // the memory size of each RMT channel, in words (4 bytes)
        .flags = {
            .with_dma = false, // DMA feature is available on chips like ESP32-S3/P4
        }};

    // LED Strip object handle
    led_strip_handle_t led_strip;
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    ESP_LOGI(TAG, "Created LED strip object with RMT backend");
    led_handle = led_strip;
}

void set_led(uint8_t led, uint8_t r, uint8_t g, uint8_t b)
{

    ESP_LOGI(TAG, "Start blinking LED strip");

    /* Set the LED pixel using RGB from 0 (0%) to 255 (100%) for each color */

    ESP_ERROR_CHECK(led_strip_set_pixel(led_handle, led, r, g, b));

    /* Refresh the strip to send data */
    ESP_ERROR_CHECK(led_strip_refresh(led_handle));
}

void LED_Task(void *params)
{
    int num_leds = *((int *)params);
    init_led(num_leds);
    xTaskNotifyGive(sensorTask);
    vTaskDelete(NULL);
}