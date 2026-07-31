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
#include "GPIOPins.h"
#include "LoraNetwork.h"
#include "esp_timer.h"
#include "Startup.h"
#include <stdio.h>
#include <string.h>

const char *SEVEN_SEGMENT_TAG = "SevenSegment";

extern QueueHandle_t sevenSegmentQueue;
extern QueueHandle_t resetQueue;
extern QueueHandle_t buzzerQueue;
extern TaskHandle_t buttonTask;

static lv_obj_t *avatar;

/* LCD IO and panel */
static esp_lcd_panel_io_handle_t lcd_io = NULL;
static esp_lcd_panel_handle_t lcd_panel = NULL;
static bool lcd_bus_initialized = false;

/* LVGL display and touch */
static lv_display_t *lvgl_disp = NULL;

/* UI elements */
static lv_obj_t *splash_screen = NULL;
static lv_obj_t *timing_screen = NULL;
static lv_obj_t *pc_programm_screen = NULL;
static lv_obj_t *top_label = NULL;
static lv_obj_t *reset_button = NULL;
static lv_obj_t *fist_image = NULL;
static lv_obj_t *hand_image = NULL;
static lv_obj_t *refusals = NULL;
static lv_obj_t *faults = NULL;
static lv_obj_t *start_label = NULL;
static lv_obj_t *end_label = NULL;
static lv_obj_t *sensor_left = NULL;
static lv_obj_t *sensor_right = NULL;
static lv_obj_t *vorlaeufig = NULL;
static lv_obj_t *firmware_upgrade_screen = NULL;
static lv_obj_t *firmware_upgrade_label = NULL;

/* Font and image declarations */
LV_FONT_DECLARE(monospace);
LV_IMG_DECLARE(fist);
LV_IMG_DECLARE(hand);

HistoryEntry history[4];
int history_index = 0;
bool isDis = false;
static bool dis_preview_active = false;
static bool dis_preview_previous_state = false;
static bool firmware_upgrade_display_active = false;
static long last_displayed_time_ms = 0;
extern volatile bool sensors_active;

extern char *pc_programm;
extern int controller_id;
extern int start_id;
extern int stop_id;

static bool service_firmware_upgrade_display(bool force)
{
    const EventBits_t bits = xEventGroupGetBits(startupEventGroup);
    if (!force && (bits & DOGDOG_OTA_DISPLAY_REQUEST_BIT) == 0)
    {
        return false;
    }

    xEventGroupClearBits(startupEventGroup, DOGDOG_OTA_DISPLAY_REQUEST_BIT);
    xQueueReset(sevenSegmentQueue);

    const esp_err_t err = display_firmware_upgrade_status("Firmware Upgrade in progress");
    if (err != ESP_OK)
    {
        ESP_LOGE(SEVEN_SEGMENT_TAG, "Could not display firmware-upgrade screen: %s",
                 esp_err_to_name(err));
        return true;
    }

    firmware_upgrade_display_active = true;
    xEventGroupSetBits(startupEventGroup, DOGDOG_OTA_DISPLAY_ACK_BIT);
    return true;
}

void Seven_Segment_Task(void *params)
{
    (void)params;

    const esp_err_t setup_err = setupSevenSegment();
    if (setup_err != ESP_OK)
    {
        ESP_LOGE(SEVEN_SEGMENT_TAG, "Display initialization failed: %s",
                 esp_err_to_name(setup_err));
        dogdog_startup_signal_failure();
        vTaskDelete(NULL);
        return;
    }

    while (true)
    {
        if (service_firmware_upgrade_display(false))
        {
            continue;
        }
        if (firmware_upgrade_display_active)
        {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

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
                remove_vorlaeufig();

                if (IS_THS_MODE)
                {
                    sendKey(HID_KEY_S);
                }
                break;
            case SEVEN_SEGMENT_TEMP_TIME:
                // Implement temporary time display
                setMilliseconds(toDisplay.time);
                add_vorlaeufig();
                break;
            case SEVEN_SEGMENT_SENSOR_STATUS:
                lvgl_port_lock(-1);
                draw_sensor_status_single(toDisplay.sensorStatus.sensor, toDisplay.sensorStatus.status, toDisplay.sensorStatus.num_sensors, toDisplay.sensorStatus.is_trigger);
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
                remove_vorlaeufig();
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
                if (IS_SIMPLE_AGILITY_MODE || IS_THS_MODE)
                {
                    isDis = !isDis;
                }
                else
                {
                    isDis = true;
                }
                lv_label_set_text(top_label, "DIS");
                lv_obj_set_style_text_color(top_label, lv_color_hex(0xFF0000), 0);
                lvgl_port_unlock();
                break;
            case SEVEN_SEGMENT_DIS_PREVIEW:
                lvgl_port_lock(-1);
                if (!dis_preview_active)
                {
                    dis_preview_previous_state = isDis;
                    dis_preview_active = true;
                }
                if (IS_SIMPLE_AGILITY_MODE || IS_THS_MODE)
                {
                    isDis = !dis_preview_previous_state;
                }
                else
                {
                    isDis = true;
                }
                lv_label_set_text(top_label, "DIS");
                lv_obj_set_style_text_color(top_label, lv_color_hex(0xFF0000), 0);
                lvgl_port_unlock();
                break;
            case SEVEN_SEGMENT_DIS_PREVIEW_REVERT:
                if (dis_preview_active)
                {
                    isDis = dis_preview_previous_state;
                    dis_preview_active = false;
                    setMilliseconds(last_displayed_time_ms);
                }
                break;
            case SEVEN_SEGMENT_DIS_PREVIEW_CONFIRM:
                dis_preview_active = false;
                break;
            case SEVEN_SEGMENT_FIRMWARE_UPGRADE:
                service_firmware_upgrade_display(true);
                break;
            default:
                ESP_LOGW(SEVEN_SEGMENT_TAG, "Unknown display type");
                break;
            }
        }
    }
}

void add_vorlaeufig()
{
    lvgl_port_lock(-1);
    if (!vorlaeufig)
    {
        vorlaeufig = lv_label_create(timing_screen);
        lv_label_set_text(vorlaeufig, "Verifizierung\nausstehend");
        lv_obj_set_style_text_font(vorlaeufig, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(vorlaeufig, lv_color_hex(0xFF0000), 0);
        lv_obj_align(vorlaeufig, LV_ALIGN_TOP_MID, 0, 10);
        lv_obj_set_style_text_color(top_label, lv_color_hex(0xFF0000), 0);
    }
    lvgl_port_unlock();
}

void remove_vorlaeufig()
{
    lvgl_port_lock(-1);
    if (vorlaeufig)
    {
        lv_obj_del(vorlaeufig);
        vorlaeufig = NULL;
        lv_obj_set_style_text_color(top_label, lv_color_hex(0x000000), 0);
    }
    lvgl_port_unlock();
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
        if (!new_text)
        {
            ESP_LOGE(SEVEN_SEGMENT_TAG, "Failed to allocate memory for new_text");
            lvgl_port_unlock();
            return;
        }
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
        if (!new_text)
        {
            ESP_LOGE(SEVEN_SEGMENT_TAG, "Failed to allocate memory for new_text");
            lvgl_port_unlock();
            return;
        }
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
    ESP_RETURN_ON_ERROR(gpio_config(&bk_gpio_config), SEVEN_SEGMENT_TAG, "Backlight GPIO init failed");

    // Initialize SPI bus
    ESP_LOGD(SEVEN_SEGMENT_TAG, "Initialize SPI bus");
    spi_bus_config_t buscfg = {
        .sclk_io_num = LCD_GPIO_SCLK,
        .mosi_io_num = LCD_GPIO_MOSI,
        .miso_io_num = GPIO_NUM_NC,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = LCD_H_RES * LCD_DRAW_BUFF_HEIGHT * sizeof(uint16_t)};
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO), SEVEN_SEGMENT_TAG, "SPI init failed");
    lcd_bus_initialized = true;

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
    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST, &io_config, &lcd_io), err, SEVEN_SEGMENT_TAG, "New panel IO failed");

    // Install LCD driver
    ESP_LOGD(SEVEN_SEGMENT_TAG, "Install LCD driver");
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_GPIO_RST,
        .color_space = LCD_COLOR_SPACE,
        .bits_per_pixel = LCD_BITS_PER_PIXEL};
    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_st7789(lcd_io, &panel_config, &lcd_panel), err, SEVEN_SEGMENT_TAG, "New panel failed");

    // Initialize LCD panel
    ESP_GOTO_ON_ERROR(esp_lcd_panel_reset(lcd_panel), err, SEVEN_SEGMENT_TAG, "Panel reset failed");
    ESP_GOTO_ON_ERROR(esp_lcd_panel_init(lcd_panel), err, SEVEN_SEGMENT_TAG, "Panel init failed");
    ESP_GOTO_ON_ERROR(esp_lcd_panel_mirror(lcd_panel, true, true), err, SEVEN_SEGMENT_TAG, "Panel mirror setup failed");
    ESP_GOTO_ON_ERROR(esp_lcd_panel_disp_on_off(lcd_panel, true), err, SEVEN_SEGMENT_TAG, "Panel enable failed");

    // Turn on LCD backlight
    ESP_GOTO_ON_ERROR(gpio_set_level(LCD_GPIO_BL, LCD_BL_ON_LEVEL), err, SEVEN_SEGMENT_TAG, "Backlight enable failed");

    ESP_GOTO_ON_ERROR(esp_lcd_panel_set_gap(lcd_panel, 0, 0), err, SEVEN_SEGMENT_TAG, "Panel gap setup failed");
    ESP_GOTO_ON_ERROR(esp_lcd_panel_invert_color(lcd_panel, false), err, SEVEN_SEGMENT_TAG, "Panel color setup failed");

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
        .buffer_size = LCD_H_RES * LCD_DRAW_BUFF_HEIGHT,
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
    if (!lvgl_disp)
    {
        ESP_LOGE(SEVEN_SEGMENT_TAG, "Failed to allocate LVGL display");
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t setupSevenSegment(void)
{
    /* LCD HW initialization */
    esp_err_t err = app_lcd_init();
    if (err != ESP_OK)
    {
        return err;
    }

    ESP_LOGI(SEVEN_SEGMENT_TAG, "Initialize touch controller");
    // esp_lcd_touch_new_i2c_cst816s(tp_io_handle, &tp_cfg, &touch);

    /* LVGL initialization */
    err = app_lvgl_init();
    if (err != ESP_OK)
    {
        cleanup_lcd_resources();
        return err;
    }

    if (!lvgl_port_lock(-1))
    {
        cleanup_lcd_resources();
        return ESP_ERR_TIMEOUT;
    }
    setup_splashscreen();
    setup_timing_screen();
    setup_pc_programm_screen();

    dogdog_startup_signal_ready(DOGDOG_STARTUP_DISPLAY_READY_BIT);

    xTaskNotifyGive(buttonTask);

    lv_scr_load_anim(pc_programm_screen, LV_SCR_LOAD_ANIM_MOVE_TOP, 500, 0, false);
    lvgl_port_unlock();

    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    xTaskNotifyGive(buttonTask);
    if ((xEventGroupGetBits(startupEventGroup) & DOGDOG_OTA_DISPLAY_REQUEST_BIT) == 0)
    {
        lvgl_port_lock(-1);
        // Buzz for startup
        xQueueSend(buzzerQueue, &(int){BUZZER_STARTUP}, 0);

        lv_scr_load_anim(timing_screen, LV_SCR_LOAD_ANIM_MOVE_TOP, 500, 0, false);
        lvgl_port_unlock();
    }
    return ESP_OK;
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

    char id_text[48];
    snprintf(id_text, sizeof(id_text), "Station:%d Start:%d Stop:%d", controller_id, start_id, stop_id);
    lv_obj_t *id_label = lv_label_create(splash_screen);
    lv_label_set_text(id_label, id_text);
    lv_obj_set_style_text_font(id_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(id_label, lv_color_hex(0x666666), 0);
    lv_obj_align(id_label, LV_ALIGN_BOTTOM_LEFT, 8, -6);

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
    draw_connection_status(2, 2);
}

void setup_pc_programm_screen()
{
    pc_programm_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(pc_programm_screen, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(pc_programm_screen, LV_OPA_COVER, 0);
    draw_vertical_line(115);
    draw_vertical_line(480 - 115);

    lv_obj_t *title_label = lv_label_create(pc_programm_screen);
    lv_label_set_text(title_label, "Welcher \nAuswertungsmodus?");
    // make the text center aligned
    lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_30, 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 10, 10);

    // Create two boxes a green and blue one with text Webmelden and Simple Agility
    lv_obj_t *webmelden_box = lv_obj_create(pc_programm_screen);
    lv_obj_set_style_bg_color(webmelden_box, lv_color_hex(0x00FF00), 0);
    lv_obj_set_size(webmelden_box, 150, 100);
    lv_obj_align(webmelden_box, LV_ALIGN_LEFT_MID, 20, 0);
    lv_obj_t *webmelden_label = lv_label_create(webmelden_box);
    lv_label_set_text(webmelden_label, "Webmelden");
    lv_obj_set_style_text_font(webmelden_label, &lv_font_montserrat_16, 0);
    lv_obj_align(webmelden_label, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *simple_agility_box = lv_obj_create(pc_programm_screen);
    lv_obj_set_style_bg_color(simple_agility_box, lv_color_hex(0x0000FF), 0);
    lv_obj_set_size(simple_agility_box, 100, 100);
    lv_obj_align(simple_agility_box, LV_ALIGN_LEFT_MID, 190, 0);
    lv_obj_t *simple_agility_label = lv_label_create(simple_agility_box);
    lv_label_set_text(simple_agility_label, "Simple\nAgility");
    lv_obj_set_style_text_font(simple_agility_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(simple_agility_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(simple_agility_label, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *ths_box = lv_obj_create(pc_programm_screen);
    lv_obj_set_style_bg_color(ths_box, lv_color_hex(0xFFFF00), 0);
    lv_obj_set_size(ths_box, 100, 100);
    lv_obj_align(ths_box, LV_ALIGN_LEFT_MID, 310, 0);
    lv_obj_t *ths_label = lv_label_create(ths_box);
    lv_label_set_text(ths_label, "THS");
    lv_obj_set_style_text_font(ths_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(ths_label, lv_color_hex(0x000000), 0);
    lv_obj_align(ths_label, LV_ALIGN_CENTER, 0, 0);
}

void setMilliseconds(long timeToSet)
{
    timeToSet = timeToSet - timeToSet % 10; // Round down to nearest 10 ms
    last_displayed_time_ms = timeToSet;
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
    if (!longResult)
    {
        ESP_LOGE(SEVEN_SEGMENT_TAG, "Failed to allocate memory for longResult");
        return;
    }
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

    if (sensors_active)
    {
        if (IS_SIMPLE_AGILITY_MODE)
        {
            if (!isDis)
            {
                // not dis
                // the programm is simple-agility
                // output the message: 'e00024,65\n' (the time 24,65s padded to 5 digits with leading zeros)
                // time text is the time in seconds with 2 decimals and comma as decimal separator
                char padded_time[9]; // 8 digits + null terminator
                // fill padded time with zeros
                memset(padded_time, '0', 8);
                size_t length = strlen(time_text);
                const char *time_suffix = time_text;
                if (length > 8)
                {
                    time_suffix += length - 8;
                    length = 8;
                }
                // copy time text to padded time from the end
                memcpy(padded_time + 8 - length, time_suffix, length);
                padded_time[8] = 0x00; // null terminator

                printf("e%s\n", padded_time);
            }
            else
            {
                // dis
                // send e00000,00
                printf("e00000,00\n");
            }
        }
        else if (strcmp(pc_programm, "webmelden") == 0)
        {
            // the programm is webmelden
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
                // sendText("15.00");
                sendKey(HID_KEY_ENTER);
            }
        }
    }
    else if (IS_THS_MODE)
    {
        if (!isDis)
        {
            // not dis
            // the programm is simple-agility
            // output the message: 'e00024,65\n' (the time 24,65s padded to 5 digits with leading zeros)
            // time text is the time in seconds with 2 decimals and comma as decimal separator
            char padded_time[9]; // 8 digits + null terminator
            // fill padded time with zeros
            memset(padded_time, '0', 8);
            size_t length = strlen(time_text);
            const char *time_suffix = time_text;
            if (length > 8)
            {
                time_suffix += length - 8;
                length = 8;
            }
            // copy time text to padded time from the end
            memcpy(padded_time + 8 - length, time_suffix, length);
            padded_time[8] = 0x00; // null terminator

            printf("e%s\n", padded_time);
        }
        else
        {
            // dis
            // send e00000,00
            printf("e00000,00\n");
        }
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
    int64_t end_time_us = esp_timer_get_time() + (int64_t)toDisplay.time * 1000;
    int64_t remaining_time_ms = (end_time_us - esp_timer_get_time()) / 1000;

    setSeconds((long)(remaining_time_ms / 1000));
    xQueueSend(buzzerQueue, &(int){BUZZER_7_MINUTE_TIMER_START}, 0);
    vTaskDelay(pdMS_TO_TICKS(2000));

    end_time_us = esp_timer_get_time() + (int64_t)toDisplay.time * 1000;
    remaining_time_ms = (end_time_us - esp_timer_get_time()) / 1000;

    while (remaining_time_ms > 0)
    {
        if (service_firmware_upgrade_display(false))
        {
            return;
        }

        setSeconds((long)(remaining_time_ms / 1000));
        remaining_time_ms = (end_time_us - esp_timer_get_time()) / 1000;

        if (xQueueReceive(sevenSegmentQueue, &toDisplay, pdMS_TO_TICKS(200)))
        {
            if (toDisplay.type == SEVEN_SEGMENT_COUNTDOWN_RESET)
            {
                setMilliseconds(0);
                return;
            }
            else if (toDisplay.type == SEVEN_SEGMENT_COUNTDOWN)
            {
                end_time_us = esp_timer_get_time() + (int64_t)toDisplay.time * 1000;
                remaining_time_ms = (end_time_us - esp_timer_get_time()) / 1000;
            }
            else if (toDisplay.type == SEVEN_SEGMENT_SENSOR_STATUS)
            {
                lvgl_port_lock(-1);
                draw_sensor_status_single(toDisplay.sensorStatus.sensor, toDisplay.sensorStatus.status, toDisplay.sensorStatus.num_sensors, toDisplay.sensorStatus.is_trigger);
                lvgl_port_unlock();
            }
            else if (toDisplay.type == SEVEN_SEGMENT_NETWORK_FAULT)
            {
                displayFault(toDisplay.startFault, toDisplay.stopFault);
            }
            else if (toDisplay.type == SEVEN_SEGMENT_FIRMWARE_UPGRADE)
            {
                service_firmware_upgrade_display(true);
                return;
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
        lcd_panel = NULL;
    }
    if (lcd_io)
    {
        esp_lcd_panel_io_del(lcd_io);
        lcd_io = NULL;
    }
    if (lcd_bus_initialized)
    {
        spi_bus_free(LCD_SPI_NUM);
        lcd_bus_initialized = false;
    }
}

void draw_connection_status(int start_alive, int end_alive)
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

    if (start_alive == 2)
    {
        draw_sensor_status_single(SENSOR_START, NULL, -1, false);
    }
    if (end_alive == 2)
    {
        draw_sensor_status_single(SENSOR_STOP, NULL, -1, false);
    }

    lv_color_t start_color;
    lv_color_t end_color;

    if (start_alive == 0)
    {
        start_color = lv_color_hex(0x00FF00); // green
    }
    else if (start_alive == 1)
    {
        start_color = lv_color_hex(0xFFFF00); // orange
    }
    else if (start_alive == 2)
    {
        start_color = lv_color_hex(0xFF0000); // red
    }

    if (end_alive == 0)
    {
        end_color = lv_color_hex(0x00FF00); // green
    }
    else if (end_alive == 1)
    {
        end_color = lv_color_hex(0xFFFF00); // orange
    }
    else if (end_alive == 2)
    {
        end_color = lv_color_hex(0xFF0000); // red
    }

    lv_label_set_text(start_label, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(start_label, start_color, 0);

    lv_label_set_text(end_label, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(end_label, end_color, 0);
}

void draw_sensor_status(bool *sensor_connected_left, bool *sensor_connected_right, int num_sensors_left, int num_sensors_right)
{
    draw_sensor_status_single(SENSOR_START, sensor_connected_left, num_sensors_left, false);
    draw_sensor_status_single(SENSOR_STOP, sensor_connected_right, num_sensors_right, false);
}

void draw_sensor_status_single(int sensor, bool *status, int num, bool is_trigger)
{
    if (num < 0)
    {
        num = 0;
    }
    if (num > 10)
    {
        ESP_LOGW(SEVEN_SEGMENT_TAG, "Only the first 10 of %d sensors fit on screen", num);
        num = 10;
    }
    if (num > 0 && !status)
    {
        ESP_LOGE(SEVEN_SEGMENT_TAG, "Missing sensor status array");
        num = 0;
    }

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

            if (is_trigger)
            {
                lv_label_set_text(text, LV_SYMBOL_SHUFFLE);
                lv_obj_set_style_text_color(text, lv_color_hex(0x00aaFF), 0); // light blue
            }
            else
            {
                lv_label_set_text(text, LV_SYMBOL_CLOSE);
                lv_obj_set_style_text_color(text, lv_color_hex(0xFF0000), 0); // red
            }
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

static void create_firmware_upgrade_screen_locked(const char *message)
{
    if (firmware_upgrade_screen == NULL)
    {
        firmware_upgrade_screen = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(firmware_upgrade_screen, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(firmware_upgrade_screen, LV_OPA_COVER, 0);

        firmware_upgrade_label = lv_label_create(firmware_upgrade_screen);
        lv_obj_set_width(firmware_upgrade_label, LCD_H_RES - 40);
        lv_obj_set_style_text_align(firmware_upgrade_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(firmware_upgrade_label, &lv_font_montserrat_30, 0);
        lv_obj_set_style_text_color(firmware_upgrade_label, lv_color_hex(0x000000), 0);
        lv_obj_center(firmware_upgrade_label);
    }

    lv_label_set_text(firmware_upgrade_label, message);
    lv_scr_load(firmware_upgrade_screen);
}

esp_err_t init_firmware_upgrade_screen(void)
{
    esp_err_t err = app_lcd_init();
    if (err != ESP_OK)
    {
        return err;
    }

    err = app_lvgl_init();
    if (err != ESP_OK)
    {
        cleanup_lcd_resources();
        return err;
    }

    if (!lvgl_port_lock(-1))
    {
        return ESP_ERR_TIMEOUT;
    }
    create_firmware_upgrade_screen_locked("Firmware Upgrade in progress");
    lvgl_port_unlock();
    return ESP_OK;
}

esp_err_t display_firmware_upgrade_status(const char *message)
{
    if (message == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (lvgl_disp == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    if (!lvgl_port_lock(pdMS_TO_TICKS(1000)))
    {
        ESP_LOGW(SEVEN_SEGMENT_TAG, "Timed out updating firmware upgrade screen");
        return ESP_ERR_TIMEOUT;
    }
    create_firmware_upgrade_screen_locked(message);
    lvgl_port_unlock();
    return ESP_OK;
}

static void free_line_points(lv_event_t *event)
{
    free(lv_event_get_user_data(event));
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
    if (!line)
    {
        free(line_points);
        ESP_LOGE(SEVEN_SEGMENT_TAG, "Failed to create line object");
        return;
    }
    lv_line_set_points(line, line_points, 2);
    lv_obj_add_event_cb(line, free_line_points, LV_EVENT_DELETE, line_points);

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
    draw_connection_status(start, stop);
    lvgl_port_unlock();
}
