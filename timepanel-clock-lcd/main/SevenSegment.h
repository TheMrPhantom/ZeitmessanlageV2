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
void Seven_Segment_Task(void *params);

void setupSevenSegment();

/* Adapt to LCD */
void clearSevenSegment();
void setMilliseconds(long timeToSet);
void setSeconds(long timeToSet);
void displayFault(int start, int stop, int dots);
void add_reset_button();
void del_reset_button();

/* End of Adaption */

void resetCountdown();
void reset_btn_event_cb(lv_event_t *e);

typedef struct SevenSegmentDisplay
{
    int time;
    int type;
    int startFault; // 1 = Fault
    int stopFault;  // 1 = Fault
} SevenSegmentDisplay;

#define SEVEN_SEGMENT_SET_TIME 0
#define SEVEN_SEGMENT_NETWORK_FAULT 1
#define SEVEN_SEGMENT_COUNTDOWN 2
#define SEVEN_SEGMENT_COUNTDOWN_RESET 3

/* LCD size */
#define LCD_H_RES (240)
#define LCD_V_RES (280)

/* LCD settings */
#define LCD_SPI_NUM (SPI2_HOST)
#define LCD_PIXEL_CLK_HZ (40 * 1000 * 1000)
#define LCD_CMD_BITS (8)
#define LCD_PARAM_BITS (8)
#define LCD_COLOR_SPACE (ESP_LCD_COLOR_SPACE_RGB)
#define LCD_BITS_PER_PIXEL (16)
#define LCD_DRAW_BUFF_DOUBLE (1)
#define LCD_DRAW_BUFF_HEIGHT (50)
#define LCD_BL_ON_LEVEL (1)

/* LCD pins */
#define LCD_GPIO_SCLK (GPIO_NUM_6)
#define LCD_GPIO_MOSI (GPIO_NUM_7)
#define LCD_GPIO_RST (GPIO_NUM_8)
#define LCD_GPIO_DC (GPIO_NUM_4)
#define LCD_GPIO_CS (GPIO_NUM_5)
#define LCD_GPIO_BL (GPIO_NUM_15)

#define USE_TOUCH 1

#define TOUCH_HOST I2C_NUM_0

#define PIN_NUM_TOUCH_SCL (GPIO_NUM_10)
#define PIN_NUM_TOUCH_SDA (GPIO_NUM_11)
#define PIN_NUM_TOUCH_RST (GPIO_NUM_13)
#define PIN_NUM_TOUCH_INT (GPIO_NUM_14)

esp_err_t app_lcd_init(void);
void lvgl_touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data);
esp_err_t app_lvgl_init(void);

#endif