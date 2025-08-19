#ifndef __SEGMENT_H
#define __SEGMENT_H
#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lvgl_port.h"
#include "lv_conf.h"
#include "esp_lcd_touch_cst816s.h"
#include <esp_system.h>
#include "soc/soc.h"

typedef struct SensorStatus
{
    int sensor;
    bool *status;
    int num_sensors;
} SensorStatus;

typedef struct SevenSegmentDisplay
{
    int time;
    int type;
    int startFault;            // 1 = Fault
    int stopFault;             // 1 = Fault
    SensorStatus sensorStatus; // Used for SEVEN_SEGMENT_SENSOR_STATUS
} SevenSegmentDisplay;

typedef struct HistoryEntry
{
    lv_obj_t *time;
    lv_obj_t *fault;
    lv_obj_t *fault_image;
    lv_obj_t *refusal;
    lv_obj_t *refusal_image;
    lv_obj_t *box;
} HistoryEntry;

void Seven_Segment_Task(void *params);

void increase_refusals();

void inrease_fault();

void setupSevenSegment();

void setup_timing_screen();

void setup_splashscreen();
void finalizeCountdown();
void cleanup_lcd_resources();
void handleCountdown(SevenSegmentDisplay toDisplay);
void draw_vertical_line(int x_pos);
void draw_sensor_status(bool *sensor_connected_left, bool *sensor_connected_right, int num_sensors_left, int num_sensors_right);
void draw_sensor_status_single(int sensor, bool *status, int num);
void draw_line(int x1, int y1, int x2, int y2);
void draw_connection_status(bool start_alive, bool end_alive);
void add_to_history();
void draw_history_element(HistoryEntry *entry, int index);
/* Adapt to LCD */
void setMilliseconds(long timeToSet);
void setSeconds(long timeToSet);
void displayFault(int start, int stop);
void add_reset_button();
void del_reset_button();

/* End of Adaption */

void resetCountdown();
void reset_btn_event_cb(lv_event_t *e);

#define SEVEN_SEGMENT_SET_TIME 0
#define SEVEN_SEGMENT_NETWORK_FAULT 1
#define SEVEN_SEGMENT_COUNTDOWN 2
#define SEVEN_SEGMENT_COUNTDOWN_RESET 3
#define SEVEN_SEGMENT_STORE_TO_HISTORY 4
#define SEVEN_SEGMENT_SENSOR_STATUS 5
#define SEVEN_SEGMENT_INCREASE_FAULT 6
#define SEVEN_SEGMENT_INCREASE_REFUSAL 7
#define SEVEN_SEGMENT_RESET_FAULT_REFUSAL 8
#define SEVEN_SEGMENT_DIS 9

#define SENSOR_START 0
#define SENSOR_STOP 1

/* LCD size */
#define LCD_H_RES (480)
#define LCD_V_RES (320)

/* LCD settings */
#define LCD_SPI_NUM (SPI2_HOST)
#define LCD_PIXEL_CLK_HZ (40 * 1000 * 1000)
#define LCD_CMD_BITS (8)
#define LCD_PARAM_BITS (8)
#define LCD_COLOR_SPACE (ESP_LCD_COLOR_SPACE_BGR)
#define LCD_BITS_PER_PIXEL (16)
#define LCD_DRAW_BUFF_DOUBLE (1)
#define LCD_DRAW_BUFF_HEIGHT (20)
#define LCD_BL_ON_LEVEL (1)

/* LCD pins */
#define LCD_GPIO_SCLK (GPIO_NUM_36)
#define LCD_GPIO_MOSI (GPIO_NUM_35)
#define LCD_GPIO_RST (GPIO_NUM_38)
#define LCD_GPIO_DC (GPIO_NUM_40)
#define LCD_GPIO_CS (GPIO_NUM_39)
#define LCD_GPIO_BL (GPIO_NUM_21)

#define USE_TOUCH 0

#define TOUCH_HOST I2C_NUM_0

#define PIN_NUM_TOUCH_SCL (GPIO_NUM_10)
#define PIN_NUM_TOUCH_SDA (GPIO_NUM_11)
#define PIN_NUM_TOUCH_RST (GPIO_NUM_13)
#define PIN_NUM_TOUCH_INT (GPIO_NUM_14)

esp_err_t app_lcd_init(void);
void lvgl_touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data);
esp_err_t app_lvgl_init(void);

#endif