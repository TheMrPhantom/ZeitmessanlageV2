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
#include "Buzzer.h"
#include "freertos/semphr.h"
#include "Keyboard.h"

const char *SEVEN_SEGMENT_TAG = "SevenSegment";

extern QueueHandle_t sevenSegmentQueue;
extern QueueHandle_t resetQueue;
extern QueueHandle_t buzzerQueue;
extern TaskHandle_t buttonTask;

static esp_lcd_touch_handle_t touch = NULL;
static lv_obj_t *avatar;

/* LCD IO and panel */
static esp_lcd_panel_io_handle_t lcd_io = NULL;
static esp_lcd_panel_handle_t lcd_panel = NULL;

/* LVGL display and touch */
static lv_display_t *lvgl_disp = NULL;

/* UI elements */
static lv_obj_t *splash_screen = NULL;
static lv_obj_t *timing_screen = NULL;
static lv_obj_t *top_label = NULL;
static lv_obj_t *bottom_label = NULL;
static lv_obj_t *reset_button = NULL;
static lv_obj_t *fist_image = NULL;
static lv_obj_t *hand_image = NULL;
static lv_obj_t *refusals = NULL;
static lv_obj_t *faults = NULL;
static lv_obj_t *start_label = NULL;
static lv_obj_t *end_label = NULL;
static lv_obj_t *sensor_left = NULL;
static lv_obj_t *sensor_right = NULL;

/* Font and image declarations */
LV_FONT_DECLARE(monospace);
LV_IMG_DECLARE(fist);
LV_IMG_DECLARE(hand);

HistoryEntry history[4];
int history_index = 0;
bool isDis = false;

void Seven_Segment_Task(void *params)
{
    setupSevenSegment();

    bool networkFault = false;

    while (true)
    {
        SevenSegmentDisplay toDisplay;
        if (xQueueReceive(sevenSegmentQueue, &toDisplay, portMAX_DELAY))
        {
            switch (toDisplay.type)
            {
            case SEVEN_SEGMENT_NETWORK_FAULT:
                displayFault(toDisplay.startFault, toDisplay.stopFault);
                break;

            case SEVEN_SEGMENT_COUNTDOWN:
                handleCountdown(toDisplay);
                break;

            case SEVEN_SEGMENT_SET_TIME:
                setMilliseconds(toDisplay.time);
                break;

            case SEVEN_SEGMENT_STORE_TO_HISTORY:
                // Implement storing to history
                setMilliseconds(toDisplay.time);
                add_to_history();
                break;
            case SEVEN_SEGMENT_SENSOR_STATUS:
                lvgl_port_lock(-1);
                draw_sensor_status_single(toDisplay.sensorStatus.sensor, toDisplay.sensorStatus.status, toDisplay.sensorStatus.num_sensors);
                free(toDisplay.sensorStatus.status);
                lvgl_port_unlock();
                break;
            case SEVEN_SEGMENT_INCREASE_FAULT:
                inrease_fault();
                break;
            case SEVEN_SEGMENT_INCREASE_REFUSAL:
                increase_refusals();
                break;
            case SEVEN_SEGMENT_RESET_FAULT_REFUSAL:
                lvgl_port_lock(-1);
                if (faults && refusals)
                {
                    lv_label_set_text(faults, "0");
                    lv_label_set_text(refusals, "0");
                    isDis = false;
                    setMilliseconds(0);
                }
                else
                {
                    ESP_LOGE(SEVEN_SEGMENT_TAG, "Labels not found");
                }
                lvgl_port_unlock();
                break;
            case SEVEN_SEGMENT_DIS:
                lvgl_port_lock(-1);
                isDis = true;
                lv_label_set_text(top_label, "DIS");
                lv_obj_set_style_text_color(top_label, lv_color_hex(0xFF0000), 0);
                lvgl_port_unlock();
                break;
            default:
                ESP_LOGW(SEVEN_SEGMENT_TAG, "Unknown display type");
                break;
            }
        }
    }
}

void increase_refusals()
{
    lvgl_port_lock(-1);
    if (refusals)
    {
        char *text = lv_label_get_text(refusals);
        ESP_LOGI(SEVEN_SEGMENT_TAG, "Current refusals: %s", text);
        int refusal_count = atoi(text);
        refusal_count++;
        // figure out string length
        int length = snprintf(NULL, 0, "%d", refusal_count);
        char *new_text = malloc(length + 1);
        snprintf(new_text, length + 1, "%d", refusal_count);
        ESP_LOGI(SEVEN_SEGMENT_TAG, "New refusals: %s", new_text);
        lv_label_set_text(refusals, new_text);
        free(new_text);
    }
    else
    {
        ESP_LOGE(SEVEN_SEGMENT_TAG, "Refusals label not found");
    }
    lvgl_port_unlock();
}

void inrease_fault()
{
    lvgl_port_lock(-1);
    if (faults)
    {
        char *text = lv_label_get_text(faults);
        ESP_LOGI(SEVEN_SEGMENT_TAG, "Current faults: %s", text);
        int fault_count = atoi(text);
        fault_count++;
        // figure out string length
        int length = snprintf(NULL, 0, "%d", fault_count);
        char *new_text = malloc(length + 1);
        snprintf(new_text, length + 1, "%d", fault_count);
        ESP_LOGI(SEVEN_SEGMENT_TAG, "New faults: %s", new_text);
        lv_label_set_text(faults, new_text);
        free(new_text);
    }
    else
    {
        ESP_LOGE(SEVEN_SEGMENT_TAG, "Faults label not found");
    }
    lvgl_port_unlock();
}

esp_err_t app_lcd_init(void)
{
    esp_err_t ret = ESP_OK;

    // Configure LCD backlight GPIO
    gpio_config_t bk_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << LCD_GPIO_BL};
    ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));

    // Initialize SPI bus
    ESP_LOGD(SEVEN_SEGMENT_TAG, "Initialize SPI bus");
    spi_bus_config_t buscfg = {
        .sclk_io_num = LCD_GPIO_SCLK,
        .mosi_io_num = LCD_GPIO_MOSI,
        .miso_io_num = GPIO_NUM_NC,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = LCD_H_RES * LCD_DRAW_BUFF_HEIGHT * sizeof(uint16_t)};
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_SPI_NUM, &buscfg, SPI_DMA_CH_AUTO), SEVEN_SEGMENT_TAG, "SPI init failed");

    // Install panel IO
    ESP_LOGD(SEVEN_SEGMENT_TAG, "Install panel IO");
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = LCD_GPIO_DC,
        .cs_gpio_num = LCD_GPIO_CS,
        .pclk_hz = LCD_PIXEL_CLK_HZ,
        .lcd_cmd_bits = LCD_CMD_BITS,
        .lcd_param_bits = LCD_PARAM_BITS,
        .spi_mode = 0,
        .trans_queue_depth = 10};
    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_NUM, &io_config, &lcd_io), err, SEVEN_SEGMENT_TAG, "New panel IO failed");

    // Install LCD driver
    ESP_LOGD(SEVEN_SEGMENT_TAG, "Install LCD driver");
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_GPIO_RST,
        .color_space = LCD_COLOR_SPACE,
        .bits_per_pixel = LCD_BITS_PER_PIXEL};
    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_st7789(lcd_io, &panel_config, &lcd_panel), err, SEVEN_SEGMENT_TAG, "New panel failed");

    // Initialize LCD panel
    esp_lcd_panel_reset(lcd_panel);
    esp_lcd_panel_init(lcd_panel);
    esp_lcd_panel_mirror(lcd_panel, true, true);
    esp_lcd_panel_disp_on_off(lcd_panel, true);

    // Turn on LCD backlight
    ESP_ERROR_CHECK(gpio_set_level(LCD_GPIO_BL, LCD_BL_ON_LEVEL));

    esp_lcd_panel_set_gap(lcd_panel, 0, 0);
    esp_lcd_panel_invert_color(lcd_panel, false);

    return ret;

err:
    cleanup_lcd_resources();
    return ret;
}

void lvgl_touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    esp_lcd_touch_handle_t touch = (esp_lcd_touch_handle_t)drv->user_data;
    assert(touch);

    uint16_t tp_x, tp_y;
    uint8_t tp_cnt = 0;

    // Read touch data
    esp_lcd_touch_read_data(touch);
    bool tp_pressed = esp_lcd_touch_get_coordinates(touch, &tp_x, &tp_y, NULL, &tp_cnt, 1);

    if (tp_pressed && tp_cnt > 0)
    {
        data->point.x = tp_x;
        data->point.y = tp_y;
        data->state = LV_INDEV_STATE_PRESSED;
        ESP_LOGD(SEVEN_SEGMENT_TAG, "Touch position: %d,%d", tp_x, tp_y);
    }
    else
    {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

esp_err_t app_lvgl_init(void)
{
    // Initialize LVGL
    lvgl_port_cfg_t lvgl_cfg = {
        .task_priority = 4,
        .task_stack = 16096,
        .task_affinity = -1,
        .task_max_sleep_ms = 8,
        .timer_period_ms = 1};
    ESP_RETURN_ON_ERROR(lvgl_port_init(&lvgl_cfg), SEVEN_SEGMENT_TAG, "LVGL port initialization failed");

    // Add LCD screen
    ESP_LOGD(SEVEN_SEGMENT_TAG, "Add LCD screen");
    lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = lcd_io,
        .panel_handle = lcd_panel,
        .buffer_size = LCD_H_RES * LCD_DRAW_BUFF_HEIGHT * sizeof(uint16_t),
        .double_buffer = LCD_DRAW_BUFF_DOUBLE,
        .hres = LCD_H_RES,
        .vres = LCD_V_RES,
        .monochrome = false,
        .rotation = {
            .swap_xy = true,
            .mirror_x = false,
            .mirror_y = false},
        .flags = {.buff_dma = true}};
    lvgl_disp = lvgl_port_add_disp(&disp_cfg);

    return ESP_OK;
}

void setupSevenSegment()
{
    /* LCD HW initialization */
    ESP_ERROR_CHECK(app_lcd_init());

    ESP_LOGI(SEVEN_SEGMENT_TAG, "Initialize touch controller");
    // esp_lcd_touch_new_i2c_cst816s(tp_io_handle, &tp_cfg, &touch);

    /* LVGL initialization */
    ESP_ERROR_CHECK(app_lvgl_init());

    lvgl_port_lock(-1);
    setup_splashscreen();

    setup_timing_screen();
    xTaskNotifyGive(buttonTask);

    // Buzz for startup
    xQueueSend(buzzerQueue, &(int){BUZZER_STARTUP}, 0);

    lv_scr_load_anim(timing_screen, LV_SCR_LOAD_ANIM_MOVE_TOP, 500, 0, false);
    lvgl_port_unlock();
}

void setup_splashscreen()
{
    splash_screen = lv_scr_act();
    lv_obj_set_style_bg_color(splash_screen, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(splash_screen, LV_OPA_COVER, 0);

    LV_IMG_DECLARE(Logo_Black);
    avatar = lv_img_create(splash_screen);
    lv_img_set_src(avatar, &Logo_Black);

    lv_obj_align(avatar, LV_ALIGN_CENTER, 0, 0);

    lvgl_port_unlock();
    vTaskDelay(pdMS_TO_TICKS(5000));
    lvgl_port_lock(-1);
}

void setup_timing_screen()
{
    timing_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(timing_screen, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(timing_screen, LV_OPA_COVER, 0);

    draw_vertical_line(115);
    draw_vertical_line(480 - 115);

    // on the left side of the line print Sensoren
    lv_obj_t *label = lv_label_create(timing_screen);
    lv_label_set_text(label, "Sensoren");
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 20, 5);

    lv_obj_t *history_label = lv_label_create(timing_screen);
    lv_label_set_text(history_label, "History");
    lv_obj_align(history_label, LV_ALIGN_TOP_RIGHT, -35, 5);

    top_label = lv_label_create(timing_screen);
    lv_obj_set_style_text_align(top_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(top_label, &monospace, 0);
    lv_obj_align(top_label, LV_ALIGN_CENTER, 0, -55);
    lv_obj_set_style_text_color(top_label, lv_color_hex(0x000000), 0);
    lv_label_set_text(top_label, "0.00");

    hand_image = lv_img_create(timing_screen);
    lv_img_set_src(hand_image, &hand);
    // position left under top label
    lv_obj_align(hand_image, LV_ALIGN_CENTER, -30, 25);

    fist_image = lv_img_create(timing_screen);
    lv_img_set_src(fist_image, &fist);
    // position right under top label
    lv_obj_align(fist_image, LV_ALIGN_CENTER, 70, 25);

    refusals = lv_label_create(timing_screen);
    lv_obj_set_style_text_font(refusals, &lv_font_montserrat_44, 0);
    lv_obj_align(refusals, LV_ALIGN_CENTER, 30, 25);
    lv_obj_set_style_text_color(refusals, lv_color_hex(0x000000), 0);
    // set to 0
    lv_label_set_text(refusals, "0");

    faults = lv_label_create(timing_screen);
    lv_obj_set_style_text_font(faults, &lv_font_montserrat_44, 0);
    lv_obj_align(faults, LV_ALIGN_CENTER, -70, 25);
    lv_obj_set_style_text_color(faults, lv_color_hex(0x000000), 0);
    // set to 0
    lv_label_set_text(faults, "0");

    // add_reset_button();

    const int NUM_SENSORS_LEFT = 10, NUM_SENSORS_RIGHT = 10;
    bool sensor_connected_left[10] = {0};
    bool sensor_connected_right[10] = {0};

    draw_sensor_status(sensor_connected_left, sensor_connected_right, NUM_SENSORS_LEFT, NUM_SENSORS_RIGHT);
    draw_connection_status(true, false);
}

void setMilliseconds(long timeToSet)
{
    timeToSet = timeToSet - timeToSet % 10; // Round down to nearest 10 ms
    float sec = timeToSet / 1000.0f;
    char numberString[8]; // Enough for "9999.99\0"
    numberString[7] = 0x00;

    // Always show two decimals, regardless of value
    snprintf(numberString, sizeof(numberString), "%.2f", sec);

    // TODO: Show milliseconds
    lvgl_port_lock(-1);
    if (isDis)
    {
        lv_label_set_text(top_label, "DIS");
        lv_obj_set_style_text_color(top_label, lv_color_hex(0xFF0000), 0);
    }
    else
    {
        lv_label_set_text(top_label, numberString);
        lv_obj_set_style_text_color(top_label, lv_color_hex(0x000000), 0);
    }
    lvgl_port_unlock();
}

void setSeconds(long timeToSet)
{

    int minutes = timeToSet / 60;
    int seconds = timeToSet % 60;
    char numberString[6];
    numberString[5] = 0x00;

    int len = snprintf(NULL, 0, "%02d:%02d", minutes, seconds);
    char *longResult = malloc(len + 1);
    snprintf(longResult, len + 1, "%02d:%02d", minutes, seconds);

    strncpy(numberString, longResult, 5);
    ESP_LOGI(SEVEN_SEGMENT_TAG, "Setting time: %s, %s", numberString, longResult);
    free(longResult);

    lvgl_port_lock(-1);
    // add_reset_button();

    lv_obj_align(top_label, LV_ALIGN_CENTER, 0, -50);
    lv_obj_set_style_text_color(top_label, lv_color_hex(0x00FF00), 0);
    lv_label_set_text(top_label, numberString);
    lvgl_port_unlock();
}

void add_to_history()
{
    lvgl_port_lock(-1);
    // get time text from top label
    char *time_text = lv_label_get_text(top_label);
    // get faults and refusal from their labels
    char *fault_text = lv_label_get_text(faults);
    char *refusal_text = lv_label_get_text(refusals);

    ESP_LOGI(SEVEN_SEGMENT_TAG, "History Entry - Time: %s, Fault: %s, Refusal: %s", time_text, fault_text, refusal_text);

    // send time text to keyboard
    if (!isDis)
    {
        sendKey(HID_KEY_TAB);
        sendText(time_text);
        sendKey(HID_KEY_ENTER);
    }
    else
    {
        sendKey(HID_KEY_TAB);
        sendText("15.00");
        sendKey(HID_KEY_ENTER);
    }

    // Add entry to history
    HistoryEntry entry;
    entry.box = lv_obj_create(timing_screen);
    entry.time = lv_label_create(entry.box);
    entry.fault = lv_label_create(entry.box);
    entry.refusal = lv_label_create(entry.box);
    entry.fault_image = lv_img_create(entry.box);
    entry.refusal_image = lv_img_create(entry.box);

    lv_img_set_src(entry.fault_image, &hand);
    lv_img_set_src(entry.refusal_image, &fist);
    lv_label_set_text(entry.time, time_text);
    lv_label_set_text(entry.fault, fault_text);
    lv_label_set_text(entry.refusal, refusal_text);

    if (history_index >= 4)
    {
        // delete oldest entry
        lv_obj_del(history[3].box);
    }
    else
    {
        history_index++;
    }

    for (int i = 3; i > 0; i--)
    {
        history[i] = history[i - 1];
    }

    history[0] = entry;

    for (int i = 0; i < history_index; i++)
    {
        draw_history_element(&history[i], i);
    }

    lvgl_port_unlock();
}

void draw_history_element(HistoryEntry *entry, int index)
{
    // make box grey
    lv_obj_set_style_bg_color(entry->box, lv_color_hex(0xEEEEEE), 0);
    lv_obj_set_style_bg_opa(entry->box, LV_OPA_COVER, 0);
    lv_obj_set_size(entry->box, 105, 60);
    // set no padding
    lv_obj_set_style_pad_all(entry->box, 0, 0);
    lv_obj_align(entry->box, LV_ALIGN_TOP_RIGHT, -5, 35 + index * 65);

    // Draw the history entry at the specified index
    lv_obj_set_style_text_align(entry->time, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_font(entry->time, &lv_font_montserrat_16, 0);
    lv_obj_align(entry->time, LV_ALIGN_TOP_MID, 0, 3);

    lv_obj_set_style_text_align(entry->fault, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_font(entry->fault, &lv_font_montserrat_16, 0);
    lv_obj_align(entry->fault, LV_ALIGN_TOP_LEFT, 10, 28);

    lv_obj_align(entry->fault_image, LV_ALIGN_TOP_LEFT, 15, 15);
    lv_img_set_zoom(entry->fault_image, 128);

    lv_obj_set_style_text_align(entry->refusal, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_font(entry->refusal, &lv_font_montserrat_16, 0);
    lv_obj_align(entry->refusal, LV_ALIGN_TOP_LEFT, 55, 28);

    lv_obj_align(entry->refusal_image, LV_ALIGN_TOP_LEFT, 60, 15);
    lv_img_set_zoom(entry->refusal_image, 128);
}

void add_reset_button()
{
    lvgl_port_lock(-1);
    if (reset_button == NULL)
    {
        reset_button = lv_btn_create(timing_screen);
        lv_obj_set_size(reset_button, 200, 50);
        lv_obj_align(reset_button, LV_ALIGN_BOTTOM_MID, 0, -25);

        lv_obj_t *reset_label = lv_label_create(reset_button);
        lv_label_set_text(reset_label, "Reset");
        lv_obj_set_style_text_align(reset_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(reset_label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(reset_label, &lv_font_montserrat_20, 0);

        // Add callback to reset button
        lv_obj_add_event_cb(reset_button, reset_btn_event_cb,
                            LV_EVENT_CLICKED, NULL);
    }
    lvgl_port_unlock();
}

void del_reset_button()
{
    lvgl_port_lock(-1);
    if (reset_button != NULL)
    {
        lv_obj_del(reset_button);
        reset_button = NULL;
    }
    lvgl_port_unlock();
}

void handleCountdown(SevenSegmentDisplay toDisplay)
{
    int startTime = (int)pdTICKS_TO_MS(xTaskGetTickCount());
    int endTime = startTime + toDisplay.time;
    int remainingTime = endTime - (int)pdTICKS_TO_MS(xTaskGetTickCount());

    setSeconds(remainingTime / 1000);
    xQueueSend(buzzerQueue, &(int){BUZZER_7_MINUTE_TIMER_START}, 0);
    vTaskDelay(pdMS_TO_TICKS(2000));

    startTime = (int)pdTICKS_TO_MS(xTaskGetTickCount());
    endTime = startTime + toDisplay.time;
    remainingTime = endTime - (int)pdTICKS_TO_MS(xTaskGetTickCount());

    while (remainingTime > 0)
    {
        setSeconds(remainingTime / 1000);
        remainingTime = endTime - (int)pdTICKS_TO_MS(xTaskGetTickCount());

        if (xQueueReceive(sevenSegmentQueue, &toDisplay, pdMS_TO_TICKS(200)))
        {
            if (toDisplay.type == SEVEN_SEGMENT_COUNTDOWN_RESET)
            {
                setMilliseconds(0);
                return;
            }
            else if (toDisplay.type == SEVEN_SEGMENT_COUNTDOWN)
            {
                startTime = (int)pdTICKS_TO_MS(xTaskGetTickCount());
                endTime = startTime + toDisplay.time * 1000;
                remainingTime = endTime - (int)pdTICKS_TO_MS(xTaskGetTickCount());
            }
            else if (toDisplay.type == SEVEN_SEGMENT_SENSOR_STATUS)
            {
                lvgl_port_lock(-1);
                draw_sensor_status_single(toDisplay.sensorStatus.sensor, toDisplay.sensorStatus.status, toDisplay.sensorStatus.num_sensors);
                free(toDisplay.sensorStatus.status);
                lvgl_port_unlock();
            }
            else if (toDisplay.type == SEVEN_SEGMENT_NETWORK_FAULT)
            {
                displayFault(toDisplay.startFault, toDisplay.stopFault);
            }
        }
    }

    finalizeCountdown();
}

void finalizeCountdown()
{
    for (int i = 0; i < 4; i++)
    {
        vTaskDelay(pdMS_TO_TICKS(500));
        lvgl_port_lock(-1);
        lv_obj_set_style_text_color(top_label, lv_color_hex(0xFF0000), 0);
        lvgl_port_unlock();
        vTaskDelay(pdMS_TO_TICKS(500));
        setSeconds(0);
        xQueueSend(buzzerQueue, &(int){BUZZER_7_MINUTE_TIMER_END}, 0);
    }
    vTaskDelay(pdMS_TO_TICKS(2000));
}

void cleanup_lcd_resources()
{
    if (lcd_panel)
    {
        esp_lcd_panel_del(lcd_panel);
    }
    if (lcd_io)
    {
        esp_lcd_panel_io_del(lcd_io);
    }
    spi_bus_free(LCD_SPI_NUM);
}

void draw_connection_status(bool start_alive, bool end_alive)
{
    if (start_label == NULL)
    {
        start_label = lv_label_create(timing_screen);
        lv_obj_align(start_label, LV_ALIGN_TOP_LEFT, 25, 33);
    }

    if (end_label == NULL)
    {
        end_label = lv_label_create(timing_screen);
        lv_obj_align(end_label, LV_ALIGN_TOP_LEFT, 70, 33);
    }

    if (!start_alive)
    {
        draw_sensor_status_single(SENSOR_START, NULL, -1);
    }
    if (!end_alive)
    {
        draw_sensor_status_single(SENSOR_STOP, NULL, -1);
    }

    lv_label_set_text(start_label, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(start_label, start_alive ? lv_color_hex(0x00FF00) : lv_color_hex(0xFF0000), 0);

    lv_label_set_text(end_label, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(end_label, end_alive ? lv_color_hex(0x00FF00) : lv_color_hex(0xFF0000), 0);
}

void draw_sensor_status(bool *sensor_connected_left, bool *sensor_connected_right, int num_sensors_left, int num_sensors_right)
{
    draw_sensor_status_single(SENSOR_START, sensor_connected_left, num_sensors_left);
    draw_sensor_status_single(SENSOR_STOP, sensor_connected_right, num_sensors_right);
}

void draw_sensor_status_single(int sensor, bool *status, int num)
{
    lv_obj_t *sensor_box = NULL;
    if (sensor == SENSOR_START)
    {
        if (sensor_left != NULL)
        {
            lv_obj_del(sensor_left);
        }
        sensor_left = lv_obj_create(timing_screen);
        sensor_box = sensor_left;
        lv_obj_align(sensor_left, LV_ALIGN_TOP_LEFT, 22, 57);
        // set size
        lv_obj_set_size(sensor_left, 25, num * 25 + 5);
        lv_obj_set_style_pad_all(sensor_left, 0, 0);
    }
    else
    {
        if (sensor_right != NULL)
        {
            lv_obj_del(sensor_right);
        }
        sensor_right = lv_obj_create(timing_screen);
        sensor_box = sensor_right;
        lv_obj_align(sensor_right, LV_ALIGN_TOP_LEFT, 67, 57);
        // set size
        lv_obj_set_size(sensor_right, 25, num * 25 + 3);
        lv_obj_set_style_pad_all(sensor_right, 0, 0);
    }
    for (int i = 0; i < num; i++)
    {
        lv_obj_t *text = lv_label_create(sensor_box);
        if (status[i])
        {
            lv_label_set_text(text, LV_SYMBOL_OK);
            lv_obj_set_style_text_color(text, lv_color_hex(0x00FF00), 0); // green
        }
        else
        {
            lv_label_set_text(text, LV_SYMBOL_CLOSE);
            lv_obj_set_style_text_color(text, lv_color_hex(0xFF0000), 0); // red
        }
        lv_obj_set_style_text_font(text, &lv_font_montserrat_20, 0);
        lv_obj_align(text, LV_ALIGN_TOP_MID, 0, i * 25);
    }
}

void draw_vertical_line(int x_pos)
{
    static lv_point_t line_points[2];
    line_points[0].x = 0;
    line_points[0].y = 4;
    line_points[1].x = 0;
    line_points[1].y = 315;

    lv_obj_t *line = lv_line_create(timing_screen);
    lv_line_set_points(line, line_points, 2);

    /* Style */
    lv_obj_set_style_line_color(line, lv_color_hex(0xb0b0b0), 0); // grey
    lv_obj_set_style_line_width(line, 2, 0);
    lv_obj_set_style_line_rounded(line, false, 0);

    /* Position on screen */
    lv_obj_set_pos(line, x_pos, 0); // Screen coordinate (x_pos,0)
}

void draw_line(int x1, int y1, int x2, int y2)
{
    lv_point_t *line_points = malloc(sizeof(lv_point_t) * 2);
    if (line_points == NULL)
    {
        ESP_LOGE(SEVEN_SEGMENT_TAG, "Failed to allocate memory for line_points");
        return;
    }
    line_points[0].x = x1;
    line_points[0].y = y1;
    line_points[1].x = x2;
    line_points[1].y = y2;

    lv_obj_t *line = lv_line_create(timing_screen);
    lv_line_set_points(line, line_points, 2);

    /* Style */
    lv_obj_set_style_line_color(line, lv_color_hex(0xb0b0b0), 0); // grey
    lv_obj_set_style_line_width(line, 2, 0);
    lv_obj_set_style_line_rounded(line, false, 0);

    lv_obj_set_pos(line, 0, 0);
}

void reset_btn_event_cb(lv_event_t *e)
{
    int toSend = 0;
    xQueueSend(resetQueue, &toSend, 0);
    resetCountdown();
}

void resetCountdown()
{
    SevenSegmentDisplay reset;
    reset.type = SEVEN_SEGMENT_COUNTDOWN_RESET;
    xQueueSend(sevenSegmentQueue, &reset, 0);
}

void displayFault(int start, int stop)
{
    lvgl_port_lock(-1);
    draw_connection_status(!start, !stop);
    lvgl_port_unlock();
}