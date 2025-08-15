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
#include "Timer.h"

const char *SEVEN_SEGMENT_TAG = "SevenSegment";

extern QueueHandle_t sevenSegmentQueue;
extern QueueHandle_t resetQueue;
extern QueueHandle_t buzzerQueue;

esp_lcd_touch_handle_t touch = NULL;

static lv_obj_t *avatar;

/* LCD IO and panel */
static esp_lcd_panel_io_handle_t lcd_io = NULL;
static esp_lcd_panel_handle_t lcd_panel = NULL;

/* LVGL display and touch */
static lv_display_t *lvgl_disp = NULL;

lv_obj_t *top_label = NULL;
lv_obj_t *bottom_label = NULL;
lv_obj_t *reset_buton = NULL;
lv_obj_t *trigger_reason = NULL;
lv_timer_t *trigger_timer = NULL;

void Seven_Segment_Task(void *params)
{
    setupSevenSegment();

    bool networkFault = false;

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
                xQueueSend(buzzerQueue, &(int){BUZZER_7_MINUTE_TIMER_START}, 0);
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
                        vTaskDelay(pdMS_TO_TICKS(500));
                        setSeconds(0);
                        xQueueSend(buzzerQueue, &(int){BUZZER_7_MINUTE_TIMER_END}, 0);
                    }
                    vTaskDelay(pdMS_TO_TICKS(2000));
                }
            }
            if (!networkFault)
            {
                if (toDisplay.type == SEVEN_SEGMENT_SET_TIME)
                {
                    setMilliseconds(toDisplay.time);
                    if (toDisplay.triggerStation != -1)
                    {
                        showTriggerStation(toDisplay.triggerStation);
                    }
                }
            }
        }
    }
}

void clearSevenSegment()
{
    lvgl_port_lock(-1);
    if (top_label != NULL)
    {
        lv_obj_del(top_label);
        top_label = NULL;
    }
    if (bottom_label != NULL)
    {
        lv_obj_del(bottom_label);
        bottom_label = NULL;
    }
    del_reset_button();
    lvgl_port_unlock();
}

esp_err_t app_lcd_init(void)
{
    esp_err_t ret = ESP_OK;

    /* LCD backlight */
    gpio_config_t bk_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << LCD_GPIO_BL};
    ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));

    /* LCD initialization */
    ESP_LOGD(SEVEN_SEGMENT_TAG, "Initialize SPI bus");
    const spi_bus_config_t buscfg = {
        .sclk_io_num = LCD_GPIO_SCLK,
        .mosi_io_num = LCD_GPIO_MOSI,
        .miso_io_num = GPIO_NUM_NC,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = LCD_H_RES * LCD_DRAW_BUFF_HEIGHT * sizeof(uint16_t),
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_SPI_NUM, &buscfg, SPI_DMA_CH_AUTO), SEVEN_SEGMENT_TAG, "SPI init failed");

    ESP_LOGD(SEVEN_SEGMENT_TAG, "Install panel IO");
    const esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = LCD_GPIO_DC,
        .cs_gpio_num = LCD_GPIO_CS,
        .pclk_hz = LCD_PIXEL_CLK_HZ,
        .lcd_cmd_bits = LCD_CMD_BITS,
        .lcd_param_bits = LCD_PARAM_BITS,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_NUM, &io_config, &lcd_io), err, SEVEN_SEGMENT_TAG, "New panel IO failed");

    ESP_LOGD(SEVEN_SEGMENT_TAG, "Install LCD driver");
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_GPIO_RST,
        .color_space = LCD_COLOR_SPACE,
        .bits_per_pixel = LCD_BITS_PER_PIXEL,
    };
    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_st7789(lcd_io, &panel_config, &lcd_panel), err, SEVEN_SEGMENT_TAG, "New panel failed");

    esp_lcd_panel_reset(lcd_panel);
    esp_lcd_panel_init(lcd_panel);
    esp_lcd_panel_mirror(lcd_panel, true, true);
    esp_lcd_panel_disp_on_off(lcd_panel, true);

    /* LCD backlight on */
    ESP_ERROR_CHECK(gpio_set_level(LCD_GPIO_BL, LCD_BL_ON_LEVEL));

    esp_lcd_panel_set_gap(lcd_panel, 0, 20);
    esp_lcd_panel_invert_color(lcd_panel, true);

    return ret;

err:
    if (lcd_panel)
    {
        esp_lcd_panel_del(lcd_panel);
    }
    if (lcd_io)
    {
        esp_lcd_panel_io_del(lcd_io);
    }
    spi_bus_free(LCD_SPI_NUM);
    return ret;
}

void lvgl_touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    esp_lcd_touch_handle_t touch = (esp_lcd_touch_handle_t)drv->user_data;
    assert(touch);

    uint16_t tp_x;
    uint16_t tp_y;
    uint8_t tp_cnt = 0;
    /* Read data from touch controller into memory */
    esp_lcd_touch_read_data(touch);
    /* Read data from touch controller */
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
    /* Initialize LVGL */
    const lvgl_port_cfg_t lvgl_cfg = {
        .task_priority = 4,       /* LVGL task priority */
        .task_stack = 4096,       /* LVGL task stack size */
        .task_affinity = -1,      /* LVGL task pinned to core (-1 is no affinity) */
        .task_max_sleep_ms = 500, /* Maximum sleep in LVGL task */
        .timer_period_ms = 5      /* LVGL timer tick period in ms */
    };
    ESP_RETURN_ON_ERROR(lvgl_port_init(&lvgl_cfg), SEVEN_SEGMENT_TAG, "LVGL port initialization failed");

    /* Add LCD screen */
    ESP_LOGD(SEVEN_SEGMENT_TAG, "Add LCD screen");
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = lcd_io,
        .panel_handle = lcd_panel,
        .buffer_size = LCD_H_RES * LCD_DRAW_BUFF_HEIGHT * sizeof(uint16_t),
        .double_buffer = LCD_DRAW_BUFF_DOUBLE,
        .hres = LCD_H_RES,
        .vres = LCD_V_RES,
        .monochrome = false,
        /* Rotation values must be same as used in esp_lcd for initial settings of the screen */
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .flags = {
            .buff_dma = true,
        }};
    lvgl_disp = lvgl_port_add_disp(&disp_cfg);

    return ESP_OK;
}

void add_reset_button()
{
    lvgl_port_lock(-1);
    if (reset_buton == NULL)
    {
        reset_buton = lv_btn_create(lv_scr_act());
        lv_obj_set_size(reset_buton, 200, 50);
        lv_obj_align(reset_buton, LV_ALIGN_BOTTOM_MID, 0, -25);

        lv_obj_t *reset_label = lv_label_create(reset_buton);
        lv_label_set_text(reset_label, "Reset");
        lv_obj_set_style_text_align(reset_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(reset_label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(reset_label, &lv_font_montserrat_20, 0);

        // Add callback to reset button
        lv_obj_add_event_cb(reset_buton, reset_btn_event_cb,
                            LV_EVENT_CLICKED, NULL);
    }
    lvgl_port_unlock();
}

void del_reset_button()
{
    lvgl_port_lock(-1);
    if (reset_buton != NULL)
    {
        lv_obj_del(reset_buton);
        reset_buton = NULL;
    }
    lvgl_port_unlock();
}

void showTriggerStation(int station)
{
    lvgl_port_lock(-1);
    if (trigger_timer != NULL)
    {
        lv_timer_del(trigger_timer);
        trigger_timer = NULL;
    }
    if (trigger_reason != NULL)
    {
        lv_obj_del(trigger_reason);
        trigger_reason = NULL;
    }

    // create a new label
    trigger_reason = lv_label_create(lv_scr_act());
    if (station == TRIGGER_START)
    {
        lv_label_set_text(trigger_reason, "Start Triggered");
    }
    else if (station == TRIGGER_STOP)
    {
        lv_label_set_text(trigger_reason, "Stop Triggered");
    }

    ESP_LOGI(SEVEN_SEGMENT_TAG, "Show trigger reason: %s", lv_label_get_text(trigger_reason));

    lv_obj_set_style_text_align(trigger_reason, LV_TEXT_ALIGN_CENTER, 0);
    // Orange color
    lv_obj_set_style_text_color(trigger_reason, lv_color_hex(0xFFA500), 0);
    lv_obj_set_style_text_font(trigger_reason, &lv_font_montserrat_20, 0);
    lv_obj_align(trigger_reason, LV_ALIGN_BOTTOM_MID, 0, -125);

    // make lvgl timer to delete the label after 3 seconds
    trigger_timer = lv_timer_create(trigger_reason_timer_cb, 5000, NULL);

    lvgl_port_unlock();
}

void trigger_reason_timer_cb(lv_timer_t *timer)
{
    if (trigger_reason != NULL)
    {
        lv_obj_del(trigger_reason);
        trigger_reason = NULL;
    }
    if (trigger_timer != NULL)
    {
        lv_timer_del(trigger_timer);
        trigger_timer = NULL;
    }
}

void setupSevenSegment()
{
    /* LCD HW initialization */
    ESP_ERROR_CHECK(app_lcd_init());

    ESP_LOGI(SEVEN_SEGMENT_TAG, "Initialize I2C bus");
    esp_log_level_set("lcd_panel.io.i2c", ESP_LOG_NONE);
    esp_log_level_set("CST816S", ESP_LOG_NONE);
    const i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = PIN_NUM_TOUCH_SDA,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = PIN_NUM_TOUCH_SCL,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100 * 1000,
    };
    i2c_param_config(TOUCH_HOST, &i2c_conf);

    i2c_driver_install(TOUCH_HOST, i2c_conf.mode, 0, 0, 0);

    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    const esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_CST816S_CONFIG();
    // Attach the TOUCH to the I2C bus
    esp_lcd_new_panel_io_i2c((esp_lcd_i2c_bus_handle_t)TOUCH_HOST, &tp_io_config, &tp_io_handle);

    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = LCD_H_RES,
        .y_max = LCD_V_RES,
        .rst_gpio_num = PIN_NUM_TOUCH_RST,
        .int_gpio_num = PIN_NUM_TOUCH_INT,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };

    ESP_LOGI(SEVEN_SEGMENT_TAG, "Initialize touch controller");
    esp_lcd_touch_new_i2c_cst816s(tp_io_handle, &tp_cfg, &touch);

    /* LVGL initialization */
    ESP_ERROR_CHECK(app_lvgl_init());

    static lv_indev_drv_t indev_drv; // Input device driver (Touch)
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.disp = lvgl_disp;
    indev_drv.read_cb = lvgl_touch_cb;
    indev_drv.user_data = touch;
    lv_indev_drv_register(&indev_drv);

    lvgl_port_lock(-1);
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    LV_IMG_DECLARE(Logo);
    avatar = lv_img_create(scr);
    lv_img_set_src(avatar, &Logo);

    lvgl_port_unlock();
    vTaskDelay(pdMS_TO_TICKS(5000));
    lvgl_port_lock(-1);

    // Delete the logo after 5 seconds
    if (avatar != NULL)
    {
        lv_obj_del(avatar);
        avatar = NULL;
    }

    lvgl_port_unlock();
}

void reset_btn_event_cb(lv_event_t *e)
{
    int toSend = 0;
    xQueueSend(resetQueue, &toSend, 0);
    resetCountdown();
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

    // TODO: Show milliseconds
    lvgl_port_lock(-1);
    add_reset_button();
    if (top_label == NULL)
    {
        top_label = lv_label_create(lv_scr_act());
        lv_obj_set_style_text_font(top_label, &lv_font_montserrat_38, 0);
        lv_obj_set_style_text_color(top_label, lv_color_hex(0x00FF00), 0);
        lv_obj_set_style_text_align(top_label, LV_TEXT_ALIGN_CENTER, 0);
    }
    if (bottom_label != NULL)
    {
        // delete bottom label
        lv_obj_del(bottom_label);
        bottom_label = NULL;
    }
    lv_obj_align(top_label, LV_ALIGN_CENTER, 0, -50);
    lv_obj_set_style_text_color(top_label, lv_color_hex(0x00FF00), 0);
    lv_label_set_text(top_label, numberString);
    lvgl_port_unlock();
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

    lvgl_port_lock(-1);
    add_reset_button();
    // TODO Show seconds
    if (top_label == NULL)
    {
        top_label = lv_label_create(lv_scr_act());
        lv_obj_set_style_text_font(top_label, &lv_font_montserrat_38, 0);
        lv_obj_set_style_text_color(top_label, lv_color_hex(0x00FF00), 0);
        lv_obj_set_style_text_align(top_label, LV_TEXT_ALIGN_CENTER, 0);
    }
    if (bottom_label != NULL)
    {
        // delete bottom label
        lv_obj_del(bottom_label);
        bottom_label = NULL;
    }
    lv_obj_align(top_label, LV_ALIGN_CENTER, 0, -50);
    lv_obj_set_style_text_color(top_label, lv_color_hex(0x00FF00), 0);
    lv_label_set_text(top_label, numberString);
    lvgl_port_unlock();
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
    lvgl_port_lock(-1);
    del_reset_button();
    // TODO Show Fault
    if (top_label == NULL)
    {
        top_label = lv_label_create(lv_scr_act());

        lv_obj_set_style_text_color(top_label, lv_color_hex(0xF00FFF), 0);
        lv_obj_set_style_text_align(top_label, LV_TEXT_ALIGN_CENTER, 0);
    }
    if (bottom_label == NULL)
    {
        bottom_label = lv_label_create(lv_scr_act());

        lv_obj_set_style_text_color(bottom_label, lv_color_hex(0xF00FFF), 0);
        lv_obj_set_style_text_align(bottom_label, LV_TEXT_ALIGN_CENTER, 0);
    }

    // Top label is for start, bottom label is for stop
    // Show label in red if fault and green if no fault
    lv_obj_align(bottom_label, LV_ALIGN_CENTER, 0, 30);
    lv_obj_align(top_label, LV_ALIGN_CENTER, 0, -30);

    lv_obj_set_style_text_font(bottom_label, &lv_font_montserrat_38, 0);
    lv_obj_set_style_text_font(top_label, &lv_font_montserrat_38, 0);

    if (start)
    {
        lv_obj_set_style_text_color(top_label, lv_color_hex(0xFF0000), 0);
    }
    else
    {
        lv_obj_set_style_text_color(top_label, lv_color_hex(0x00FF00), 0);
    }
    if (stop)
    {
        lv_obj_set_style_text_color(bottom_label, lv_color_hex(0xFF0000), 0);
    }
    else
    {
        lv_obj_set_style_text_color(bottom_label, lv_color_hex(0x00FF00), 0);
    }

    if (dotStart)
    {
        lv_label_set_text(top_label, "!! START !!");
    }
    else
    {
        lv_label_set_text(top_label, "START");
    }
    if (dotStop)
    {
        lv_label_set_text(bottom_label, "!! ZIEL !!");
    }
    else
    {
        lv_label_set_text(bottom_label, "ZIEL");
    }
    lvgl_port_unlock();
}