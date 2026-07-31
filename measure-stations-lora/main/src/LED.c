#include "LED.h"

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "esp_err.h"
#include "esp_log.h"
#include "led_strip.h"

#define LED_STRIP_GPIO_PIN 48
#define LED_STRIP_RMT_RES_HZ (10 * 1000 * 1000)

static const char *TAG = "LED";

static led_strip_handle_t led_handle;
static SemaphoreHandle_t led_mutex;
static int number_of_leds;
static bool is_initialized;

static bool lock_leds(void)
{
    if (!is_initialized || led_mutex == NULL)
    {
        return false;
    }
    if (xSemaphoreTake(led_mutex, pdMS_TO_TICKS(100)) != pdTRUE)
    {
        ESP_LOGW(TAG, "Timed out waiting for LED strip");
        return false;
    }
    return true;
}

void init_led(int num_leds)
{
    if (num_leds <= 0)
    {
        ESP_LOGE(TAG, "Cannot initialize LED strip with %d LEDs", num_leds);
        return;
    }

    led_mutex = xSemaphoreCreateMutex();
    if (led_mutex == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate LED mutex; LEDs disabled");
        return;
    }

    const led_strip_config_t strip_config = {
        .strip_gpio_num = LED_STRIP_GPIO_PIN,
        .max_leds = num_leds,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags = {
            .invert_out = false,
        },
    };

    const led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = LED_STRIP_RMT_RES_HZ,
        .mem_block_symbols = 64,
        .flags = {
            .with_dma = false,
        },
    };

    esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &led_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "LED strip initialization failed: %s", esp_err_to_name(err));
        vSemaphoreDelete(led_mutex);
        led_mutex = NULL;
        return;
    }

    number_of_leds = num_leds;
    is_initialized = true;
}

void set_led(uint8_t led, uint8_t red, uint8_t green, uint8_t blue)
{
    if (led >= number_of_leds)
    {
        ESP_LOGW(TAG, "Ignoring out-of-range LED index %u", led);
        return;
    }
    if (!lock_leds())
    {
        return;
    }

    esp_err_t err = led_strip_set_pixel(led_handle, led, red, green, blue);
    if (err == ESP_OK)
    {
        err = led_strip_refresh(led_handle);
    }
    xSemaphoreGive(led_mutex);

    if (err != ESP_OK)
    {
        // LEDs are diagnostic only; a transient RMT failure must not reboot the
        // timing station.
        ESP_LOGE(TAG, "Failed to update LED %u: %s", led, esp_err_to_name(err));
    }
}

void set_all_leds(uint8_t red, uint8_t green, uint8_t blue)
{
    if (!lock_leds())
    {
        return;
    }

    esp_err_t err = ESP_OK;
    for (int i = 0; i < number_of_leds; i++)
    {
        err = led_strip_set_pixel(led_handle, i, red, green, blue);
        if (err != ESP_OK)
        {
            break;
        }
    }
    if (err == ESP_OK)
    {
        err = led_strip_refresh(led_handle);
    }
    xSemaphoreGive(led_mutex);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to update LED strip: %s", esp_err_to_name(err));
    }
}
