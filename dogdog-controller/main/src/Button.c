#include "Button.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <sys/time.h>
#include <freertos/semphr.h>
#include "esp_log.h"
#include "GPIOPins.h"

static const char *TAG = "BUTTON";

const int sensorGlowPins[] = {BUTTON_GLOW_GPIO_TYPE_ACTIVATE,
                              BUTTON_GLOW_GPIO_TYPE_DIS,
                              BUTTON_GLOW_GPIO_TYPE_FAULT,
                              BUTTON_GLOW_GPIO_TYPE_REFUSAL,
                              BUTTON_GLOW_GPIO_TYPE_RESET};

extern QueueHandle_t buttonQueue;
extern volatile bool sensors_active;
bool active_glowing = false;
static TickType_t last_glow_tick;

void init_glow_pins()
{
    ESP_LOGI(TAG, "Configuring IO");
    for (int i = 0; i < sizeof(sensorGlowPins) / sizeof(int); i++)
    {

        gpio_config_t io_conf = {0};
        io_conf.pin_bit_mask = 1ULL << sensorGlowPins[i]; // select pin
        io_conf.mode = GPIO_MODE_OUTPUT;                  // input mode
        io_conf.intr_type = GPIO_INTR_DISABLE;
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&io_conf));
        ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_set_level(sensorGlowPins[i], 0));
    }

    ESP_LOGI(TAG, "Done configuring IO");
}

void Button_Task(void *params)
{
    last_glow_tick = xTaskGetTickCount();

    gpio_set_level(BUTTON_GLOW_GPIO_TYPE_ACTIVATE, 1);
    gpio_set_level(BUTTON_GLOW_GPIO_TYPE_RESET, 1);
    vTaskDelay(pdMS_TO_TICKS(400));

    gpio_set_level(BUTTON_GLOW_GPIO_TYPE_FAULT, 1);
    gpio_set_level(BUTTON_GLOW_GPIO_TYPE_DIS, 1);
    vTaskDelay(pdMS_TO_TICKS(400));

    gpio_set_level(BUTTON_GLOW_GPIO_TYPE_REFUSAL, 1);
    vTaskDelay(pdMS_TO_TICKS(400));

    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    // Screen is ready, turn off all glows

    gpio_set_level(BUTTON_GLOW_GPIO_TYPE_REFUSAL, 0);
    vTaskDelay(pdMS_TO_TICKS(400));

    gpio_set_level(BUTTON_GLOW_GPIO_TYPE_FAULT, 0);
    gpio_set_level(BUTTON_GLOW_GPIO_TYPE_DIS, 0);
    vTaskDelay(pdMS_TO_TICKS(400));

    gpio_set_level(BUTTON_GLOW_GPIO_TYPE_ACTIVATE, 0);
    gpio_set_level(BUTTON_GLOW_GPIO_TYPE_RESET, 0);
    vTaskDelay(pdMS_TO_TICKS(400));

    // Wait for pc program to be set
    gpio_set_level(BUTTON_GLOW_GPIO_TYPE_ACTIVATE, 1);
    gpio_set_level(BUTTON_GLOW_GPIO_TYPE_FAULT, 1);
    gpio_set_level(BUTTON_GLOW_GPIO_TYPE_REFUSAL, 1);

    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    gpio_set_level(BUTTON_GLOW_GPIO_TYPE_ACTIVATE, 0);
    gpio_set_level(BUTTON_GLOW_GPIO_TYPE_FAULT, 0);
    gpio_set_level(BUTTON_GLOW_GPIO_TYPE_REFUSAL, 0);

    while (1)
    {
        glow_state_t glow_state;
        if (xQueueReceive(buttonQueue, &glow_state, pdMS_TO_TICKS(50)))
        {
            ESP_LOGI(TAG, "Change button glow %i is %i", glow_state.pinNumber, glow_state.state);
            gpio_set_level(glow_state.pinNumber, glow_state.state);
        }

        if (!sensors_active)
        {
            TickType_t now_tick = xTaskGetTickCount();
            if ((now_tick - last_glow_tick) > pdMS_TO_TICKS(1000))
            {

                if (active_glowing)
                {
                    glow_state.pinNumber = BUTTON_GLOW_GPIO_TYPE_ACTIVATE;
                    glow_state.state = 0;
                    xQueueSend(buttonQueue, &glow_state, pdMS_TO_TICKS(50));
                    active_glowing = false;
                }
                else
                {
                    glow_state.pinNumber = BUTTON_GLOW_GPIO_TYPE_ACTIVATE;
                    glow_state.state = 1;
                    xQueueSend(buttonQueue, &glow_state, pdMS_TO_TICKS(50));
                    active_glowing = true;
                }
                last_glow_tick = now_tick;
            }
        }
    }
}
