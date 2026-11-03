#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "led_strip.h"
#include "esp_log.h"
#include "esp_err.h"
#include "LED.h"

// GPIO assignment
#define LED_STRIP_GPIO_PIN 48
// 10MHz resolution, 1 tick = 0.1us (led strip needs a high resolution)
#define LED_STRIP_RMT_RES_HZ (10 * 1000 * 1000)

static const char *TAG = "LED";
led_strip_handle_t led_handle;
bool led_on_off = false;

int number_of_leds = 0;
float brightness = 1;
bool is_initialized = false;
static SemaphoreHandle_t led_mutex;
static TaskHandle_t ota_pulse_task_handle;
static SemaphoreHandle_t ota_pulse_stopped;

void init_led(int num_leds)
{
    ESP_LOGI(TAG, "Initializing LED strip");
    led_mutex = xSemaphoreCreateMutex();
    ota_pulse_stopped = xSemaphoreCreateBinary();
    ESP_ERROR_CHECK(led_mutex != NULL && ota_pulse_stopped != NULL ? ESP_OK : ESP_ERR_NO_MEM);
    number_of_leds = num_leds;
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
    is_initialized = true;
    set_all_leds(0, 0, 0);
}

void set_led(uint8_t led, uint8_t r, uint8_t g, uint8_t b)
{

    // ESP_LOGI(TAG, "Start blinking LED strip");
    if (!is_initialized || led >= number_of_leds)
    {
        ESP_LOGW(TAG, "LED strip not initialized");
        return;
    }
    /* Set the LED pixel using RGB from 0 (0%) to 255 (100%) for each color */
    xSemaphoreTake(led_mutex, portMAX_DELAY);
    ESP_LOGI(TAG, "Set LED %d to color R:%d G:%d B:%d", led, r, g, b);
    ESP_ERROR_CHECK_WITHOUT_ABORT(led_strip_set_pixel(led_handle, led, r * brightness, g * brightness, b * brightness));
    /* Refresh the strip to send data */
    ESP_ERROR_CHECK_WITHOUT_ABORT(led_strip_refresh(led_handle));
    xSemaphoreGive(led_mutex);
}

void set_all_leds(uint8_t r, uint8_t g, uint8_t b)
{
    if (!is_initialized)
    {
        ESP_LOGW(TAG, "LED strip not initialized");
        return;
    }
    xSemaphoreTake(led_mutex, portMAX_DELAY);
    for (int i = 0; i < number_of_leds; i++)
    {
        ESP_ERROR_CHECK_WITHOUT_ABORT(led_strip_set_pixel(led_handle, i, r, g, b));
    }
    /* Refresh the strip to send data */
    ESP_ERROR_CHECK_WITHOUT_ABORT(led_strip_refresh(led_handle));
    xSemaphoreGive(led_mutex);
}

static void ota_led_pulse_task(void *params)
{
    (void)params;
    int level = 16;
    int step = 6;

    do
    {
        // Fade turquoise (64, 224, 208) up and down over about 1.6 seconds.
        set_all_leds(64 * level / 255, 224 * level / 255, 208 * level / 255);
        level += step;
        if (level >= 255)
        {
            level = 255;
            step = -6;
        }
        else if (level <= 16)
        {
            level = 16;
            step = 6;
        }
    } while (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(20)) == 0);

    set_all_leds(0, 0, 0);
    xSemaphoreGive(ota_pulse_stopped);
    vTaskDelete(NULL);
}

void start_ota_led_pulse(void)
{
    if (!is_initialized || ota_pulse_task_handle != NULL)
    {
        return;
    }

    ESP_ERROR_CHECK_WITHOUT_ABORT(xTaskCreate(ota_led_pulse_task, "OTA_LED", 3072,
                                            NULL, 2, &ota_pulse_task_handle) == pdPASS
                                     ? ESP_OK
                                     : ESP_ERR_NO_MEM);
}

void stop_ota_led_pulse(void)
{
    if (ota_pulse_task_handle == NULL)
    {
        return;
    }

    xTaskNotifyGive(ota_pulse_task_handle);
    // Wait for the last LED write before normal sensor colors can take over.
    xSemaphoreTake(ota_pulse_stopped, portMAX_DELAY);
    ota_pulse_task_handle = NULL;
}
