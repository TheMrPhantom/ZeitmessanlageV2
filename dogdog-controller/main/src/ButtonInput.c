#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <stdint.h>
#include <string.h>
#include "sdkconfig.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "ButtonInput.h"
#include "Keyboard.h"
#include "Button.h"
#include "GPIOPins.h"
#include "SevenSegment.h"
#include "Buzzer.h"
#include "Timer.h"
#include "esp_timer.h"
#include "OTA.h"
#include "Startup.h"

#define OTA_CHORD_STABLE_US 250000LL
#define OTA_CHORD_ASSEMBLY_US 500000LL
#define OTA_RELEASE_STABLE_US 100000LL
#define OTA_DISPLAY_ACK_TIMEOUT_MS 7000
#define OTA_GESTURE_MONITOR_STACK_SIZE 2048
#define OTA_GESTURE_MONITOR_PRIORITY 12

#if CONFIG_START
#define STATION_TYPE 0
#elif CONFIG_STOP
#define STATION_TYPE 1
#endif

extern QueueHandle_t buttonInterruptQueue;
extern QueueHandle_t buttonQueue;
extern QueueHandle_t resetQueue;
extern QueueHandle_t sevenSegmentQueue;
extern QueueHandle_t buzzerQueue;
extern TaskHandle_t sevenSegmentTask;
extern QueueHandle_t loraSendQueue;
extern int64_t otaGestureDeadlineUs;

extern int stop_id;
static const char *TAG = "BUTTON_INPUT";
const int sensorButtonPins[] = {BUTTON_INPUT_GPIO_TYPE_ACTIVATE,
                                BUTTON_INPUT_GPIO_TYPE_DIS,
                                BUTTON_INPUT_GPIO_TYPE_FAULT,
                                BUTTON_INPUT_GPIO_TYPE_REFUSAL,
                                BUTTON_INPUT_GPIO_TYPE_RESET};

volatile bool sensors_active = false;
extern char *pc_programm;

static bool any_controller_button_pressed(void)
{
    for (size_t i = 0; i < sizeof(sensorButtonPins) / sizeof(sensorButtonPins[0]); i++)
    {
        if (gpio_get_level(sensorButtonPins[i]) == 0)
        {
            return true;
        }
    }
    return false;
}

static bool all_controller_buttons_pressed(void)
{
    for (size_t i = 0; i < sizeof(sensorButtonPins) / sizeof(sensorButtonPins[0]); i++)
    {
        if (gpio_get_level(sensorButtonPins[i]) != 0)
        {
            return false;
        }
    }
    return true;
}

static void wait_for_controller_button_release(void)
{
    int64_t released_since_us = -1;
    while (true)
    {
        const int64_t now_us = esp_timer_get_time();
        if (!any_controller_button_pressed())
        {
            if (released_since_us < 0)
            {
                released_since_us = now_us;
            }
            else if (now_us - released_since_us >= OTA_RELEASE_STABLE_US)
            {
                return;
            }
        }
        else
        {
            released_since_us = -1;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static esp_err_t configure_controller_button_inputs(gpio_int_type_t interrupt_type)
{
    uint64_t pin_mask = 0;
    for (size_t i = 0; i < sizeof(sensorButtonPins) / sizeof(sensorButtonPins[0]); i++)
    {
        pin_mask |= 1ULL << sensorButtonPins[i];
    }

    const gpio_config_t input_config = {
        .pin_bit_mask = pin_mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = interrupt_type,
    };
    return gpio_config(&input_config);
}

static void ota_gesture_monitor_task(void *params)
{
    (void)params;

    int64_t all_pressed_since_us = 0;
    while (true)
    {
        const int64_t now_us = esp_timer_get_time();
        if (now_us >= otaGestureDeadlineUs)
        {
            break;
        }

        if (all_controller_buttons_pressed())
        {
            if (all_pressed_since_us == 0)
            {
                all_pressed_since_us = now_us;
            }
            else if (now_us - all_pressed_since_us >= OTA_CHORD_STABLE_US)
            {
                ESP_LOGI(TAG, "Firmware-upgrade button chord latched");
                xEventGroupSetBits(startupEventGroup, DOGDOG_OTA_GESTURE_LATCHED_BIT);
                break;
            }
        }
        else
        {
            all_pressed_since_us = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }

    vTaskDelete(NULL);
}

esp_err_t start_ota_gesture_monitor(void)
{
    esp_err_t err = configure_controller_button_inputs(GPIO_INTR_DISABLE);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to configure early firmware-upgrade buttons: %s",
                 esp_err_to_name(err));
        return err;
    }

    if (xTaskCreate(ota_gesture_monitor_task, "OTA_Gesture", OTA_GESTURE_MONITOR_STACK_SIZE,
                    NULL, OTA_GESTURE_MONITOR_PRIORITY, NULL) != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create early firmware-upgrade monitor");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static bool wait_for_latched_ota_gesture(void)
{
    if ((xEventGroupGetBits(startupEventGroup) & DOGDOG_OTA_GESTURE_LATCHED_BIT) != 0)
    {
        return true;
    }

    int64_t now_us = esp_timer_get_time();
    if (!any_controller_button_pressed() || now_us >= otaGestureDeadlineUs)
    {
        return false;
    }

    int64_t wait_until_us = now_us + OTA_CHORD_ASSEMBLY_US + OTA_CHORD_STABLE_US;
    if (wait_until_us > otaGestureDeadlineUs)
    {
        wait_until_us = otaGestureDeadlineUs;
    }

    while ((now_us = esp_timer_get_time()) < wait_until_us)
    {
        if ((xEventGroupGetBits(startupEventGroup) & DOGDOG_OTA_GESTURE_LATCHED_BIT) != 0)
        {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    return (xEventGroupGetBits(startupEventGroup) & DOGDOG_OTA_GESTURE_LATCHED_BIT) != 0;
}

static void request_ota_from_button_chord(bool release_display_startup)
{
    if (!wait_for_latched_ota_gesture())
    {
        return;
    }

    ESP_LOGI(TAG, "All controller buttons held; requesting firmware upgrade");
    xEventGroupClearBits(startupEventGroup, DOGDOG_OTA_DISPLAY_ACK_BIT);
    xEventGroupSetBits(startupEventGroup, DOGDOG_OTA_DISPLAY_REQUEST_BIT);

    if (release_display_startup)
    {
        // The display startup waits for the program-selection task. Let it
        // finish initialization so it can acknowledge the OTA screen.
        xTaskNotifyGive(sevenSegmentTask);
    }

    SevenSegmentDisplay display_request = {.type = SEVEN_SEGMENT_FIRMWARE_UPGRADE};
    xQueueReset(sevenSegmentQueue);
    if (xQueueSendToFront(sevenSegmentQueue, &display_request, pdMS_TO_TICKS(500)) != pdTRUE)
    {
        ESP_LOGE(TAG, "Could not prioritize the firmware-upgrade display request");
    }

    const EventBits_t display_bits = xEventGroupWaitBits(
        startupEventGroup, DOGDOG_OTA_DISPLAY_ACK_BIT, pdFALSE, pdTRUE,
        pdMS_TO_TICKS(OTA_DISPLAY_ACK_TIMEOUT_MS));
    if ((display_bits & DOGDOG_OTA_DISPLAY_ACK_BIT) == 0)
    {
        ESP_LOGE(TAG,
                 "Firmware-upgrade screen unavailable; continuing with headless OTA fallback");

        wait_for_controller_button_release();
        dd_ota_request_reboot();
    }

    // Never restart while a strap or gesture button is still held. This also
    // prevents the new firmware from immediately requesting another update.
    wait_for_controller_button_release();

    dd_ota_request_reboot();
}

static void free_dogdog_packet(DogDogPacket *packet)
{
    if (packet)
    {
        free(packet->payload);
        free(packet);
    }
}

static void show_dis_press_feedback()
{
    if (sensors_active)
    {
        SevenSegmentDisplay toSendSevenSegment;
        toSendSevenSegment.type = SEVEN_SEGMENT_DIS_PREVIEW;
        xQueueSend(sevenSegmentQueue, &toSendSevenSegment, pdMS_TO_TICKS(500));
        xQueueSend(buzzerQueue, &(int){BUZZER_BUTTON_PRESS}, 0);
    }
}

static void confirm_dis_press_feedback()
{
    SevenSegmentDisplay toSendSevenSegment;
    toSendSevenSegment.type = SEVEN_SEGMENT_DIS_PREVIEW_CONFIRM;
    xQueueSend(sevenSegmentQueue, &toSendSevenSegment, pdMS_TO_TICKS(500));
}

static void revert_dis_press_feedback()
{
    SevenSegmentDisplay toSendSevenSegment;
    toSendSevenSegment.type = SEVEN_SEGMENT_DIS_PREVIEW_REVERT;
    xQueueSend(sevenSegmentQueue, &toSendSevenSegment, pdMS_TO_TICKS(500));
}

static void send_dis_key_to_pc()
{
    if (sensors_active)
    {
        BaseType_t result = sendKey(HID_KEY_D);
        ESP_LOGI(TAG, "Result of sending key: %i", result);
    }
    else
    {
        if (IS_SIMPLE_AGILITY_MODE || IS_THS_MODE)
        {
            sendKey(HID_KEY_N);
        }
        ESP_LOGI(TAG, "Sensors are not active");
    }
}

static void IRAM_ATTR gpio_interrupt_handler(void *args)
{
    int pinNumber = (int)(intptr_t)args;
    int edge = gpio_get_level(pinNumber) == 0 ? GPIO_INTR_NEGEDGE : GPIO_INTR_POSEDGE;
    sensor_interrupt_t sensor_interrupt;
    sensor_interrupt.pinNumber = pinNumber;
    sensor_interrupt.edge = edge;
    BaseType_t higher_priority_task_woken = pdFALSE;
    xQueueSendFromISR(buttonInterruptQueue, &sensor_interrupt, &higher_priority_task_woken);
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

esp_err_t init_button_pins(void)
{
    ESP_LOGI(TAG, "Configuring IO");
    esp_err_t err = configure_controller_button_inputs(GPIO_INTR_ANYEDGE);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to configure controller buttons: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Done configuring IO");

    const size_t button_count = sizeof(sensorButtonPins) / sizeof(sensorButtonPins[0]);
    for (size_t i = 0; i < button_count; i++)
    {
        ESP_LOGI(TAG, "Configuring ISR for Pin %i", sensorButtonPins[i]);
        err = gpio_isr_handler_add(sensorButtonPins[i], gpio_interrupt_handler,
                                   (void *)(intptr_t)sensorButtonPins[i]);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to install button ISR for pin %d: %s",
                     sensorButtonPins[i], esp_err_to_name(err));
            for (size_t installed = 0; installed < i; installed++)
            {
                ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_isr_handler_remove(sensorButtonPins[installed]));
            }
            for (size_t pin = 0; pin < button_count; pin++)
            {
                ESP_ERROR_CHECK_WITHOUT_ABORT(
                    gpio_set_intr_type(sensorButtonPins[pin], GPIO_INTR_DISABLE));
            }
            return err;
        }
    }

    ESP_LOGI(TAG, "Done configuring ISR");
    return ESP_OK;
}

void Button_Input_Task(void *params)
{
    ESP_LOGI(TAG, "Setting up Buttons");
    esp_err_t button_init_err = init_button_pins();
    if (button_init_err != ESP_OK)
    {
        dogdog_startup_signal_failure();
        vTaskDelete(NULL);
        return;
    }
    dogdog_startup_signal_ready(DOGDOG_STARTUP_BUTTON_READY_BIT);

    sensor_interrupt_t sensor_interrupt;

    int64_t last_button_interrupt_us = esp_timer_get_time();
    int64_t reset_pressed_us = last_button_interrupt_us;
    int64_t dis_pressed_us = last_button_interrupt_us;
    int countdown_sent = 1;
    bool reset_ignore_until_release = false;
    bool dis_press_pending = false;
    bool dis_long_press_sent = false;
    bool dis_feedback_shown = false;

    // Also catch a chord that was already being held when the ISR handlers
    // were installed and therefore produced no new edge.
    request_ota_from_button_chord(true);

    bool canContinue = false;
    while (!canContinue)
    {
        xQueueReceive(buttonInterruptQueue, &sensor_interrupt, portMAX_DELAY);
        request_ota_from_button_chord(true);
        canContinue = true;
        if (sensor_interrupt.pinNumber == BUTTON_INPUT_GPIO_TYPE_ACTIVATE)
        {
            pc_programm = "webmelden";
            ESP_LOGI(TAG, "Program set to webmelden");
        }
        else if (sensor_interrupt.pinNumber == BUTTON_INPUT_GPIO_TYPE_FAULT)
        {
            pc_programm = "simple-agility";
            ESP_LOGI(TAG, "Program set to simple-agility");
        }
        else if (sensor_interrupt.pinNumber == BUTTON_INPUT_GPIO_TYPE_REFUSAL)
        {
            pc_programm = "ths";
            ESP_LOGI(TAG, "Program set to ths");
        }
        else
        {
            canContinue = false;
        }
    }

    xTaskNotifyGive(sevenSegmentTask);

    while (true)
    {
        if (xQueueReceive(buttonInterruptQueue, &sensor_interrupt, pdMS_TO_TICKS(500)))
        {
            request_ota_from_button_chord(false);
            ESP_LOGI(TAG, "Checking interrupt of Pin: %i with state %i", sensor_interrupt.pinNumber, sensor_interrupt.edge);

            vTaskDelay(pdMS_TO_TICKS(3));
            int64_t now_us = esp_timer_get_time();

            if (gpio_get_level(sensor_interrupt.pinNumber) == 1 && sensor_interrupt.pinNumber == BUTTON_INPUT_GPIO_TYPE_RESET && reset_ignore_until_release)
            {
                reset_ignore_until_release = false;
                last_button_interrupt_us = now_us;
                ESP_LOGI(TAG, "Reset release after countdown");
            }
            else if (gpio_get_level(sensor_interrupt.pinNumber) == 1 && sensor_interrupt.pinNumber == BUTTON_INPUT_GPIO_TYPE_DIS && dis_press_pending)
            {
                if (!dis_long_press_sent)
                {
                    send_dis_key_to_pc();
                    if (dis_feedback_shown)
                    {
                        confirm_dis_press_feedback();
                    }
                }
                dis_press_pending = false;
                dis_feedback_shown = false;
            }
            else if (gpio_get_level(sensor_interrupt.pinNumber) == 0 &&
                     (now_us - last_button_interrupt_us > 300000))
            {
                last_button_interrupt_us = now_us;

                ESP_LOGI(TAG, "Confirmed interrupt of Pin: %i", sensor_interrupt.pinNumber);

                if (sensor_interrupt.pinNumber == BUTTON_INPUT_GPIO_TYPE_ACTIVATE)
                {
                    if (IS_THS_MODE && sensors_active)
                    {
                        DogDogPacket *request_final_time = create_dogdog_packet_from_request_final_time_information(stop_id);
                        if (!request_final_time)
                        {
                            ESP_LOGE(TAG, "Failed to allocate final-time request");
                        }
                        else if (!loraSendQueue ||
                                 xQueueSend(loraSendQueue, &request_final_time, pdMS_TO_TICKS(100)) != pdTRUE)
                        {
                            ESP_LOGW(TAG, "LoRa send queue unavailable; dropping final-time request");
                            free_dogdog_packet(request_final_time);
                        }

                        glow_state_t glow_state;
                        glow_state.state = 1;
                        glow_state.pinNumber = BUTTON_GLOW_GPIO_TYPE_RESET;
                        xQueueSend(buttonQueue, &glow_state, pdMS_TO_TICKS(50));
                    }
                    if (IS_THS_MODE && !sensors_active)
                    {
                        sendKey(HID_KEY_S);
                    }

                    sensors_active = !sensors_active;

                    glow_state_t glow_state;
                    if (sensors_active)
                    {
                        glow_state.state = 1;
                        glow_state.pinNumber = BUTTON_GLOW_GPIO_TYPE_ACTIVATE;
                        xQueueSend(buttonQueue, &glow_state, pdMS_TO_TICKS(50));
                    }
                    else
                    {
                        glow_state.state = 0;
                    }

                    if (IS_SIMPLE_AGILITY_MODE)
                    {
                        sendKey(HID_KEY_S);
                    }

                    ESP_LOGI(TAG, "Sensors are now %s", sensors_active ? "active" : "inactive");

                    glow_state.pinNumber = BUTTON_GLOW_GPIO_TYPE_FAULT;
                    xQueueSend(buttonQueue, &glow_state, pdMS_TO_TICKS(50));

                    glow_state.pinNumber = BUTTON_GLOW_GPIO_TYPE_REFUSAL;
                    xQueueSend(buttonQueue, &glow_state, pdMS_TO_TICKS(50));

                    glow_state.pinNumber = BUTTON_GLOW_GPIO_TYPE_DIS;
                    xQueueSend(buttonQueue, &glow_state, pdMS_TO_TICKS(50));
                }
                else if (sensor_interrupt.pinNumber == BUTTON_INPUT_GPIO_TYPE_RESET)
                {
                    if (reset_ignore_until_release)
                    {
                        ESP_LOGI(TAG, "Ignoring reset press while waiting for release after countdown");
                        continue;
                    }

                    glow_state_t glow_state;
                    glow_state.state = 0;
                    glow_state.pinNumber = BUTTON_GLOW_GPIO_TYPE_RESET;

                    reset_pressed_us = now_us;
                    countdown_sent = 0;

                    xQueueSend(buttonQueue, &glow_state, pdMS_TO_TICKS(50));
                    int toSend = 0;
                    xQueueSend(resetQueue, &toSend, 0);

                    SevenSegmentDisplay toSendSevenSegment;
                    toSendSevenSegment.type = SEVEN_SEGMENT_COUNTDOWN_RESET;
                    xQueueSend(sevenSegmentQueue, &toSendSevenSegment, 0);

                    toSendSevenSegment.type = SEVEN_SEGMENT_RESET_FAULT_REFUSAL;
                    xQueueSend(sevenSegmentQueue, &toSendSevenSegment, pdMS_TO_TICKS(500));

                    if ((IS_SIMPLE_AGILITY_MODE || IS_THS_MODE) && !sensors_active)
                    {
                        sendKey(HID_KEY_N);
                    }
                    if ((IS_SIMPLE_AGILITY_MODE || IS_THS_MODE) && sensors_active)
                    {
                        printf("e00000,00\n");
                    }
                }
                else
                {
                    if (sensors_active)
                    {
                        if (sensor_interrupt.pinNumber == BUTTON_INPUT_GPIO_TYPE_FAULT)
                        {
                            BaseType_t result = sendKey(HID_KEY_F);
                            SevenSegmentDisplay toSendSevenSegment;
                            toSendSevenSegment.type = SEVEN_SEGMENT_INCREASE_FAULT;
                            xQueueSend(sevenSegmentQueue, &toSendSevenSegment, 0);
                            xQueueSend(buzzerQueue, &(int){BUZZER_BUTTON_PRESS}, 0);
                            ESP_LOGI(TAG, "Result of sending key: %i", result);
                        }
                        else if (sensor_interrupt.pinNumber == BUTTON_INPUT_GPIO_TYPE_REFUSAL)
                        {
                            BaseType_t result = pdFALSE;
                            if (IS_SIMPLE_AGILITY_MODE || IS_THS_MODE)
                            {
                                result = sendKey(HID_KEY_V);
                            }
                            else if (strcmp(pc_programm, "webmelden") == 0)
                            {
                                result = sendKey(HID_KEY_R);
                            }

                            SevenSegmentDisplay toSendSevenSegment;
                            toSendSevenSegment.type = SEVEN_SEGMENT_INCREASE_REFUSAL;
                            xQueueSend(sevenSegmentQueue, &toSendSevenSegment, 0);
                            xQueueSend(buzzerQueue, &(int){BUZZER_BUTTON_PRESS}, 0);
                            ESP_LOGI(TAG, "Result of sending key: %i", result);
                        }
                        else if (sensor_interrupt.pinNumber == BUTTON_INPUT_GPIO_TYPE_DIS)
                        {
                            dis_pressed_us = now_us;
                            dis_press_pending = true;
                            dis_long_press_sent = false;
                            show_dis_press_feedback();
                            dis_feedback_shown = sensors_active;
                        }
                    }
                    else
                    {
                        if (sensor_interrupt.pinNumber == BUTTON_INPUT_GPIO_TYPE_DIS)
                        {
                            dis_pressed_us = now_us;
                            dis_press_pending = true;
                            dis_long_press_sent = false;
                            dis_feedback_shown = false;
                        }
                        else
                        {
                            ESP_LOGI(TAG, "Sensors are not active");
                        }
                    }
                }
            }
        }

        int64_t now_us = esp_timer_get_time();

        if (gpio_get_level(BUTTON_INPUT_GPIO_TYPE_RESET) == 0 &&
            (now_us - reset_pressed_us > 1000000) && countdown_sent == 0)
        {
            countdown_sent = 1;
            reset_ignore_until_release = true;
            SevenSegmentDisplay toSend;
            toSend.type = SEVEN_SEGMENT_COUNTDOWN;
            toSend.time = 60 * 7 * 1000;
            xQueueSend(sevenSegmentQueue, &toSend, 0);
            reset_pressed_us = now_us;
            ESP_LOGI(TAG, "Countdown started");
        }

        if (gpio_get_level(BUTTON_INPUT_GPIO_TYPE_DIS) == 0 && dis_press_pending &&
            !dis_long_press_sent && (now_us - dis_pressed_us > 1500000))
        {
            dis_long_press_sent = true;
            if (dis_feedback_shown)
            {
                revert_dis_press_feedback();
                dis_feedback_shown = false;
            }

            if (restartTimerFromLastTrigger())
            {
                ESP_LOGI(TAG, "Queued timer restart from last trigger timestamp");
            }
            else
            {
                ESP_LOGW(TAG, "DIS long press ignored because no trigger timestamp or queue slot is available");
            }
        }
    }
}
