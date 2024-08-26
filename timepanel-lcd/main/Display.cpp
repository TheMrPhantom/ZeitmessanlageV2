#include "Display.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <rom/ets_sys.h>
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include "KeyValue.h"
#include <Adafruit_GFX.h>    // Core graphics library
#include <Adafruit_ST7735.h> // Hardware-specific library for ST7735
#include <SPI.h>

#define TFT_CS 23
#define TFT_RST 21 // Or set to -1 and connect to Arduino RESET pin
#define TFT_DC 22

#define TFT_MOSI 13 // Data out
#define TFT_SCLK 14 // Clock out

Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);

const char *DISPLAY_TAG = "Display";

extern QueueHandle_t displayQueue;

bool newFault = false;
bool startFault = false;
bool stopFault = false;

void Seven_Segment_Task(void *params)
{
    setupSevenSegment();

    bool networkFault = false;

    /*
        if (gpio_get_level((gpio_num_t)17) == 0)
        {
            ESP_LOGI("7Segment", "Reset pressed on startup");
            setMilliseconds(8888);
            vTaskDelay(pdMS_TO_TICKS(5000));
            setMilliseconds(getValue("startups"));
            vTaskDelay(pdMS_TO_TICKS(5000));
            setMilliseconds(getValue("triggers"));
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
    */

    while (true)
    {
        LCDDisplay toDisplay;
        if (xQueueReceive(displayQueue, &toDisplay, portMAX_DELAY))
        {
            if (toDisplay.type == SEVEN_SEGMENT_NETWORK_FAULT)
            {
                networkFault = toDisplay.startFault || toDisplay.stopFault;
                ESP_LOGI(DISPLAY_TAG, "Received Fault: Start -> %i, Stop -> %i", toDisplay.startFault, toDisplay.stopFault);
                if (networkFault)
                {
                    displayFault(toDisplay.startFault, toDisplay.stopFault, pdTICKS_TO_MS(xTaskGetTickCount()) % 1000 > 500);
                }
                else
                {
                    tft.fillScreen(ST77XX_BLACK);
                    tft.setTextColor(ST77XX_WHITE);
                    tft.setTextSize(1);
                    printCentered("Aktuelle Zeit:", 20);
                    setMilliseconds(0);
                    newFault = true;
                }
            }
            if (!networkFault)
            {
                if (!newFault)
                {
                    tft.setTextColor(ST77XX_WHITE);
                    tft.setTextSize(1);
                    printCentered("Aktuelle Zeit:", 20);
                    tft.fillScreen(ST77XX_BLACK);
                    setMilliseconds(0);
                }
                newFault = true;

                if (toDisplay.type == SEVEN_SEGMENT_SET_TIME)
                {
                    setMilliseconds(toDisplay.time);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void printCentered(const char *text, int h)
{
    int16_t x1;
    int16_t y1;
    uint16_t width;
    uint16_t height;

    tft.getTextBounds(text, 0, 0, &x1, &y1, &width, &height);

    tft.setCursor((128 - width) / 2, h);
    tft.println(text);
}

void setupSevenSegment()
{

    tft.initR(INITR_GREENTAB); // Init ST7735S chip, green tab
    tft.setRotation(0);

    tft.fillScreen(ST77XX_BLACK);
    tft.setTextColor(ST77XX_WHITE);
    tft.setTextSize(2);
    printCentered("DogDog", 15);
    tft.setTextSize(1);
    printCentered("Zeitmessanlage", 40);

    tft.drawBitmap(2, 60, epd_bitmap_Logo_Simple, 120, 120, ST77XX_WHITE);
}

void setMilliseconds(long timeToSet)
{
    float sec = timeToSet / 1000.0;
    char numberString[7];
    numberString[5] = 's';
    numberString[6] = 0x00;

    int len = snprintf(NULL, 0, "%f", sec);
    char *longResult = (char *)malloc(len + 1);
    snprintf(longResult, len + 1, "%f", sec);

    strncpy(numberString, longResult, 5);

    free(longResult);

    tft.fillRect(0, 80, 128, 16, ST77XX_BLACK);
    tft.setTextColor(ST77XX_WHITE);

    tft.setTextSize(2);
    printCentered(numberString, 80);
}

void displayFault(int start, int stop, int dots)
{

    if (newFault || start != startFault || stop != stopFault)
    {

        tft.fillScreen(ST77XX_BLACK);

        tft.setTextSize(1);
        tft.setTextColor(ST77XX_WHITE);
        printCentered("Verbindungsfehler", 15);
        tft.setTextSize(2);

        if (start)
        {
            tft.setTextColor(ST77XX_BLUE);
            printCentered("Start", 60);
        }
        else
        {
            tft.setTextColor(ST77XX_GREEN);
            printCentered("Start", 60);
        }
        if (stop)
        {
            tft.setTextColor(ST77XX_BLUE);
            printCentered("Stop", 120);
        }
        else
        {
            tft.setTextColor(ST77XX_GREEN);
            printCentered("Stop", 120);
        }
    }
    newFault = false;
    startFault = start;
    stopFault = stop;
}
