#include "Buzzer.h"

#include <stdbool.h>
#include <inttypes.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"

#include "GPIOPins.h"

#define SOUND_A 440
#define SOUND_D_HIGH 587
#define SOUND_E 329

#define LEDC_TIMER LEDC_TIMER_0
#define LEDC_MODE LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL LEDC_CHANNEL_0
#define LEDC_DUTY_RES LEDC_TIMER_13_BIT
#define LEDC_DUTY 4095
#define LEDC_INITIAL_FREQUENCY 2700

static const char *TAG = "BUZZER";

extern QueueHandle_t buzzerQueue;

static esp_err_t init_buzzer(void)
{
    const ledc_timer_config_t timer_config = {
        .speed_mode = LEDC_MODE,
        .timer_num = LEDC_TIMER,
        .duty_resolution = LEDC_DUTY_RES,
        .freq_hz = LEDC_INITIAL_FREQUENCY,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer_config);
    if (err != ESP_OK)
    {
        return err;
    }

    const ledc_channel_config_t channel_config = {
        .speed_mode = LEDC_MODE,
        .channel = LEDC_CHANNEL,
        .timer_sel = LEDC_TIMER,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = BUZZER_GPIO,
        .duty = 0,
        .hpoint = 0,
    };
    return ledc_channel_config(&channel_config);
}

static bool set_buzzer(uint32_t frequency, uint32_t duty)
{
    if (frequency > 0 && ledc_set_freq(LEDC_MODE, LEDC_TIMER, frequency) == 0)
    {
        ESP_LOGE(TAG, "Could not set buzzer frequency to %" PRIu32 " Hz", frequency);
        return false;
    }

    esp_err_t err = ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty);
    if (err == ESP_OK)
    {
        err = ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
    }
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Could not update buzzer output: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

static void sound(uint32_t frequency, uint32_t duration_ms)
{
    if (!set_buzzer(frequency, LEDC_DUTY))
    {
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    set_buzzer(0, 0);
}

void Buzzer_Task(void *params)
{
    (void)params;

    const esp_err_t init_err = init_buzzer();
    const bool buzzer_available = init_err == ESP_OK;
    if (!buzzer_available)
    {
        // The buzzer is non-critical; do not reboot the measurement station if
        // its peripheral cannot initialize. Keep draining its queue so producers
        // never stall behind an unavailable diagnostic device.
        ESP_LOGE(TAG, "Buzzer initialization failed: %s", esp_err_to_name(init_err));
    }

    bool error_active = false;

    while (true)
    {
        int input;
        if (xQueueReceive(buzzerQueue, &input, portMAX_DELAY) != pdPASS)
        {
            continue;
        }
        if (!buzzer_available)
        {
            continue;
        }

        if (input == BUZZER_STARTUP)
        {
            if (error_active)
            {
                continue;
            }
            for (int i = 0; i < 3; i++)
            {
                sound(SOUND_A, 100);
                if (i != 2)
                {
                    vTaskDelay(pdMS_TO_TICKS(100));
                }
            }
        }
        else if (input == Buzzer_TRIGGER)
        {
            if (!error_active)
            {
                sound(SOUND_D_HIGH, 300);
            }
        }
        else if (input == Buzzer_ERROR_START)
        {
            error_active = true;
            set_buzzer(SOUND_E, LEDC_DUTY);
        }
        else if (input == Buzzer_ERROR_STOP)
        {
            error_active = false;
            set_buzzer(0, 0);
        }
        else if (input == Buzzer_INDICATE_OTA)
        {
            for (int i = 0; i < 5; i++)
            {
                sound(SOUND_A, 1000);
                if (i != 4)
                {
                    vTaskDelay(pdMS_TO_TICKS(500));
                }
            }
            if (error_active)
            {
                set_buzzer(SOUND_E, LEDC_DUTY);
            }
        }
        else
        {
            ESP_LOGW(TAG, "Ignoring unknown buzzer event %d", input);
        }
    }
}
