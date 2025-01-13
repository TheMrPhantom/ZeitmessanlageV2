#include "SevenSegment.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <rom/ets_sys.h>
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include "KeyValue.h"

const int sevenSegmentPins[] = {26, 27, 14, 12, 19};
const char *SEVEN_SEGMENT_TAG = "SevenSegment";

extern QueueHandle_t sevenSegmentQueue;

void Seven_Segment_Task(void *params)
{
    setupSevenSegment();

    bool networkFault = false;

    if (gpio_get_level(17) == 0)
    {
        ESP_LOGI("7Segment", "Reset pressed on startup");
        setMilliseconds(8888);
        vTaskDelay(pdMS_TO_TICKS(5000));
        setMilliseconds(getValue("startups"));
        vTaskDelay(pdMS_TO_TICKS(5000));
        setMilliseconds(getValue("triggers"));
        vTaskDelay(pdMS_TO_TICKS(5000));
    }

    while (true)
    {
        SevenSegmentDisplay toDisplay;
        if (xQueueReceive(sevenSegmentQueue, &toDisplay, portMAX_DELAY))
        {
            if (toDisplay.type == SEVEN_SEGMENT_NETWORK_FAULT)
            {
                networkFault = toDisplay.startFault || toDisplay.stopFault;
                ESP_LOGI(SEVEN_SEGMENT_TAG, "Received Fault: Start -> %i, Stop -> %i", toDisplay.startFault, toDisplay.stopFault);
                if (networkFault)
                {
                    displayFault(toDisplay.startFault, toDisplay.stopFault, pdTICKS_TO_MS(xTaskGetTickCount()) % 1000 > 500);
                }
                else
                {
                    setMilliseconds(0);
                }
            }
            else if (toDisplay.type == SEVEN_SEGMENT_COUNTDOWN)
            {

                int startTime = (int)pdTICKS_TO_MS(xTaskGetTickCount());
                int endTime = startTime + toDisplay.time;
                int remainingTime = endTime - (int)pdTICKS_TO_MS(xTaskGetTickCount());

                setSeconds(remainingTime / 1000);
                vTaskDelay(pdMS_TO_TICKS(2000));

                startTime = (int)pdTICKS_TO_MS(xTaskGetTickCount());
                endTime = startTime + toDisplay.time;
                remainingTime = endTime - (int)pdTICKS_TO_MS(xTaskGetTickCount());

                int skip_zero = 0;

                while (remainingTime > 0)
                {
                    setSeconds(remainingTime / 1000);
                    remainingTime = endTime - (int)pdTICKS_TO_MS(xTaskGetTickCount());

                    if (xQueueReceive(sevenSegmentQueue, &toDisplay, pdMS_TO_TICKS(200)))
                    {
                        if (toDisplay.type == SEVEN_SEGMENT_COUNTDOWN_RESET)
                        {
                            skip_zero = 1;
                            break;
                        }
                        else if (toDisplay.type == SEVEN_SEGMENT_COUNTDOWN)
                        {
                            startTime = (int)pdTICKS_TO_MS(xTaskGetTickCount());
                            endTime = startTime + toDisplay.time * 1000;
                            remainingTime = endTime - (int)pdTICKS_TO_MS(xTaskGetTickCount());
                        }
                        else if (toDisplay.type == SEVEN_SEGMENT_NETWORK_FAULT)
                        {
                            networkFault = toDisplay.startFault || toDisplay.stopFault;
                        }
                    }
                }
                if (skip_zero == 0)
                {
                    for (int i = 0; i < 4 && skip_zero == 0; i++)
                    {
                        vTaskDelay(pdMS_TO_TICKS(500));
                        clearSevenSegment();
                        setSevenSegment();
                        vTaskDelay(pdMS_TO_TICKS(500));
                        setSeconds(0);
                    }
                    vTaskDelay(pdMS_TO_TICKS(2000));
                }
            }
            if (!networkFault)
            {
                if (toDisplay.type == SEVEN_SEGMENT_SET_TIME)
                {
                    setMilliseconds(toDisplay.time);
                }
            }
        }
    }
}

void setupSevenSegment()
{

    for (int i = 0; i < sizeof(sevenSegmentPins) / sizeof(int); i++)
    {
        ESP_LOGI(SEVEN_SEGMENT_TAG, "Configuring IO Pin %i", sevenSegmentPins[i]);
        esp_rom_gpio_pad_select_gpio(sevenSegmentPins[i]);
        gpio_set_direction(sevenSegmentPins[i], GPIO_MODE_OUTPUT);
    }

    ESP_LOGI(SEVEN_SEGMENT_TAG, "Done configuring IO");

    gpio_set_level(26, 1);
    gpio_set_level(19, 1);
}

void shiftSevenSegmentNumber(int a, int b, int c, int d, int e, int f, int g, int dot)
{
    gpio_set_level(12, dot);
    ets_delay_us(5);
    gpio_set_level(27, 1);
    ets_delay_us(5);
    gpio_set_level(27, 0);
    ets_delay_us(5);

    gpio_set_level(12, e);
    ets_delay_us(5);
    gpio_set_level(27, 1);
    ets_delay_us(5);
    gpio_set_level(27, 0);
    ets_delay_us(5);

    gpio_set_level(12, d);
    ets_delay_us(5);
    gpio_set_level(27, 1);
    ets_delay_us(5);
    gpio_set_level(27, 0);
    ets_delay_us(5);

    gpio_set_level(12, c);
    ets_delay_us(5);
    gpio_set_level(27, 1);
    ets_delay_us(5);
    gpio_set_level(27, 0);
    ets_delay_us(5);

    gpio_set_level(12, b);
    ets_delay_us(5);
    gpio_set_level(27, 1);
    ets_delay_us(5);
    gpio_set_level(27, 0);
    ets_delay_us(5);

    gpio_set_level(12, a);
    ets_delay_us(5);
    gpio_set_level(27, 1);
    ets_delay_us(5);
    gpio_set_level(27, 0);
    ets_delay_us(5);

    gpio_set_level(12, g);
    ets_delay_us(5);
    gpio_set_level(27, 1);
    ets_delay_us(5);
    gpio_set_level(27, 0);
    ets_delay_us(5);

    gpio_set_level(12, f);
    ets_delay_us(5);
    gpio_set_level(27, 1);
    ets_delay_us(5);
    gpio_set_level(27, 0);
    ets_delay_us(5);
}

void setSevenSegment()
{
    gpio_set_level(14, 1);
    ets_delay_us(5);
    gpio_set_level(14, 0);
}

void setZero(int dot)
{
    shiftSevenSegmentNumber(1, 1, 1, 1, 1, 1, 0, dot);
}

void setOne(int dot)
{
    shiftSevenSegmentNumber(0, 1, 1, 0, 0, 0, 0, dot);
}
void setTwo(int dot)
{
    shiftSevenSegmentNumber(1, 1, 0, 1, 1, 0, 1, dot);
}
void setThree(int dot)
{
    shiftSevenSegmentNumber(1, 1, 1, 1, 0, 0, 1, dot);
}
void setFour(int dot)
{
    shiftSevenSegmentNumber(0, 1, 1, 0, 0, 1, 1, dot);
}
void setFive(int dot)
{
    shiftSevenSegmentNumber(1, 0, 1, 1, 0, 1, 1, dot);
}
void setSix(int dot)
{
    shiftSevenSegmentNumber(1, 0, 1, 1, 1, 1, 1, dot);
}
void setSeven(int dot)
{
    shiftSevenSegmentNumber(1, 1, 1, 0, 0, 0, 0, dot);
}
void setEight(int dot)
{
    shiftSevenSegmentNumber(1, 1, 1, 1, 1, 1, 1, dot);
}
void setNine(int dot)
{
    shiftSevenSegmentNumber(1, 1, 1, 1, 0, 1, 1, dot);
}

void setNumber(int i, int dot)
{
    // ESP_LOGI(SEVEN_SEGMENT_TAG, "Setting Number: %i and dot:%i", i, dot);

    if (i == 0)
    {
        setZero(dot);
    }
    else if (i == 1)
    {
        setOne(dot);
    }
    else if (i == 2)
    {
        setTwo(dot);
    }
    else if (i == 3)
    {
        setThree(dot);
    }
    else if (i == 4)
    {
        setFour(dot);
    }
    else if (i == 5)
    {
        setFive(dot);
    }
    else if (i == 6)
    {
        setSix(dot);
    }
    else if (i == 7)
    {
        setSeven(dot);
    }
    else if (i == 8)
    {
        setEight(dot);
    }
    else if (i == 9)
    {
        setNine(dot);
    }
}

void clearSevenSegment()
{
    gpio_set_level(26, 0);
    ets_delay_us(4);
    gpio_set_level(26, 1);
}

void setMilliseconds(long timeToSet)
{
    float sec = timeToSet / 1000.0;
    char numberString[6];
    numberString[5] = 0x00;

    int len = snprintf(NULL, 0, "%f", sec);
    char *longResult = malloc(len + 1);
    snprintf(longResult, len + 1, "%f", sec);

    strncpy(numberString, longResult, 5);

    free(longResult);

    for (int i = 0; i < 5; i++)
    {
        int dot = 0;
        if (numberString[i + 1] == '.')
        {
            dot = 1;
        }
        if (numberString[i] == '.')
        {
            continue;
        }

        const char toConvert[2] = {numberString[i], 0x00};
        // ESP_LOGI(SEVEN_SEGMENT_TAG, "Characters: %s Dot:%i", toConvert, atoi(toConvert));
        setNumber(atoi(toConvert), dot);
    }
    setSevenSegment();
}

/*
    Will be displayed as minute:seconds
*/
void setSeconds(long timeToSet)
{

    int minutes = timeToSet / 60;
    int seconds = timeToSet % 60;
    char numberString[6];
    numberString[5] = 0x00;

    int len = snprintf(NULL, 0, "%02d.%02d", minutes, seconds);
    char *longResult = malloc(len + 1);
    snprintf(longResult, len + 1, "%02d.%02d", minutes, seconds);

    strncpy(numberString, longResult, 5);
    ESP_LOGI(SEVEN_SEGMENT_TAG, "Setting time: %s, %s", numberString, longResult);
    free(longResult);

    for (int i = 0; i < 5; i++)
    {
        int dot = 0;
        if (numberString[i + 1] == '.')
        {
            dot = 1;
        }
        if (numberString[i] == '.')
        {
            continue;
        }
        if (numberString[0] == '0' && i == 0)
        {
            shiftSevenSegmentNumber(0, 0, 0, 0, 0, 0, 0, 0);
            continue;
        }

        const char toConvert[2] = {numberString[i], 0x00};
        // ESP_LOGI(SEVEN_SEGMENT_TAG, "Characters: %s Dot:%i", toConvert, atoi(toConvert));
        setNumber(atoi(toConvert), dot);
    }
    setSevenSegment();
}

void resetCountdown()
{
    SevenSegmentDisplay reset;
    reset.type = SEVEN_SEGMENT_COUNTDOWN_RESET;
    xQueueSend(sevenSegmentQueue, &reset, 0);
}

void displayFault(int start, int stop, int dots)
{
    int dotStart = start && dots;
    int dotStop = stop && dots;

    shiftSevenSegmentNumber(1, 0, 0, 1, 1, 1, 0, 0);
    shiftSevenSegmentNumber(1, 1, 1, 0, 1, 1, 1, dotStart);
    shiftSevenSegmentNumber(1, 0, 0, 1, 1, 1, 1, dotStop);
    shiftSevenSegmentNumber(1, 1, 1, 1, 0, 0, 0, 0);

    setSevenSegment();
}