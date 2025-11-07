#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "esp_system.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "Buzzer.h"

#define sound_c 261
#define sound_d 294
#define sound_e 329
#define sound_f 349
#define sound_g 391
#define sound_gS 415
#define sound_a 440
#define sound_aS 455
#define sound_b 466
#define sound_cH 523
#define sound_cSH 554
#define sound_dH 587
#define sound_dSH 622
#define sound_eH 659
#define sound_fH 698
#define sound_fSH 740
#define sound_gH 784
#define sound_gSH 830
#define sound_aH 880

#include <stdio.h>
#include "driver/ledc.h"
#include "esp_err.h"
#include "GPIOPins.h"

#define LEDC_TIMER LEDC_TIMER_0
#define LEDC_MODE LEDC_LOW_SPEED_MODE
#define LEDC_OUTPUT_IO (BUZZER_GPIO) // Define the output GPIO
#define LEDC_CHANNEL LEDC_CHANNEL_0
#define LEDC_DUTY_RES LEDC_TIMER_13_BIT // Set duty resolution to 13 bits
#define LEDC_DUTY (4095)                // Set duty to 50%. ((2 ** 13) - 1) * 50% = 4095
#define LEDC_FREQUENCY (5000)           // Frequency in Hertz. Set frequency at 5 kHz

#define TAG "BUZZER"

extern QueueHandle_t buzzerQueue;

void init_buzzer()
{
    // Prepare and then apply the LEDC PWM timer configuration
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_MODE,
        .timer_num = LEDC_TIMER,
        .duty_resolution = LEDC_DUTY_RES,
        .freq_hz = LEDC_FREQUENCY, // Set output frequency at 5 kHz
        .clk_cfg = LEDC_AUTO_CLK};
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    // Prepare and then apply the LEDC PWM channel configuration
    ledc_channel_config_t ledc_channel = {
        .speed_mode = LEDC_MODE,
        .channel = LEDC_CHANNEL,
        .timer_sel = LEDC_TIMER,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = LEDC_OUTPUT_IO,
        .duty = 0, // Set duty to 0%
        .hpoint = 0};
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
}

void sound(uint32_t freq, uint32_t duration)
{
    // start
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_0, LEDC_DUTY); // 12% duty - play here for your speaker or buzzer
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_0);
    ESP_LOGI(TAG, "Buzzing start");
    vTaskDelay(pdMS_TO_TICKS(duration));
    // stop
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_0, 0);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_0);
    ESP_LOGI(TAG, "Buzzing end");
}

void startSound(uint32_t freq)
{
    // start
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_0, LEDC_DUTY); // 12% duty - play here for your speaker or buzzer
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_0);
    ESP_LOGI(TAG, "Buzzing start");
}

void stopSound()
{
    // stop
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_0, 0);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_0);
    ESP_LOGI(TAG, "Buzzing end");
}

void Buzzer_Task(void *params)
{
    init_buzzer();
    while (true)
    {
        int input = 0;
        if (xQueueReceive(buzzerQueue, &input, portMAX_DELAY))
        {
            if (input == BUZZER_STARTUP)
            {
                sound(sound_a, 100);
                vTaskDelay(pdMS_TO_TICKS(100));
                sound(sound_a, 100);
                vTaskDelay(pdMS_TO_TICKS(100));
                sound(sound_a, 100);
            }
            else if (input == BUZZER_TRIGGER)
            {
                sound(sound_dH, 300);
            }
            else if (input == BUZZER_INDICATE_ERROR)
            {
                input = 0;
                startSound(sound_e);
                while (input != BUZZER_INDICATE_ERROR)
                {
                    xQueueReceive(buzzerQueue, &input, portMAX_DELAY);
                }
                stopSound();
            }
            else if (input == BUZZER_7_MINUTE_TIMER_START)
            {
                sound(sound_cH, 100);
                vTaskDelay(pdMS_TO_TICKS(100));
                sound(sound_cH, 100);
            }
            else if (input == BUZZER_7_MINUTE_TIMER_END)
            {
                sound(sound_cH, 600);
            }
            else if (input == BUZZER_BUTTON_PRESS)
            {
                sound(sound_g, 100);
            }
        }
    }
}