#include "Clock.h"

#include <errno.h>
#include <string.h>

#define DS3231_I2C_ADDRESS 0x68

static int64_t rtc_time = 0;
static QueueHandle_t timePrintQueue;
static i2c_master_bus_handle_t rtc_bus_handle;
static rtc_handle_t rtc_device_handle;
static bool rtc_isr_installed;

static void cleanup_external_clock(void)
{
    if (rtc_isr_installed)
    {
        ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_isr_handler_remove(RTC_SQW));
        rtc_isr_installed = false;
    }
    if (rtc_device_handle)
    {
        ESP_ERROR_CHECK_WITHOUT_ABORT(i2c_master_bus_rm_device(rtc_device_handle));
        rtc_device_handle = NULL;
    }
    if (rtc_bus_handle)
    {
        ESP_ERROR_CHECK_WITHOUT_ABORT(i2c_del_master_bus(rtc_bus_handle));
        rtc_bus_handle = NULL;
    }
    if (timePrintQueue)
    {
        vQueueDelete(timePrintQueue);
        timePrintQueue = NULL;
    }
}

void deinit_external_clock(void)
{
    cleanup_external_clock();
}

// Define the ISR handler for RTC_SQW pin
static void IRAM_ATTR rtc_sqw_isr_handler(void *arg)
{
    rtc_time += 1000000;
    // Notify task to perform time sync and logging
    BaseType_t higher_priority_task_woken = pdFALSE;
    xQueueOverwriteFromISR(timePrintQueue, &rtc_time, &higher_priority_task_woken);
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

BaseType_t init_external_clock()
{
    i2c_master_bus_config_t i2c_mst_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = -1,
        .scl_io_num = RTC_SCL,
        .sda_io_num = RTC_SDA,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t err = i2c_new_master_bus(&i2c_mst_config, &rtc_bus_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE("CLOCK", "Failed to create RTC I2C bus: %s", esp_err_to_name(err));
        cleanup_external_clock();
        return pdFALSE;
    }

    err = i2c_master_probe(rtc_bus_handle, DS3231_I2C_ADDRESS, 100);
    if (err != ESP_OK)
    {
        ESP_LOGW("CLOCK", "No RTC clock found; relying on internal clock: %s", esp_err_to_name(err));
        cleanup_external_clock();
        return pdFALSE;
    }

    i2c_device_config_t rtc_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = DS3231_I2C_ADDRESS,
        .scl_speed_hz = 200000,
    };
    err = i2c_master_bus_add_device(rtc_bus_handle, &rtc_config, &rtc_device_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE("CLOCK", "Failed to add RTC I2C device: %s", esp_err_to_name(err));
        cleanup_external_clock();
        return pdFALSE;
    }

    // Enable the square wave output on the DS3231
    // The library names register value 0 "1000HZ", but the DS3231 hardware
    // value is 1 Hz. One interrupt therefore represents one elapsed second.
    err = ds3231_square_wave_freq_set(&rtc_device_handle, RTC_SQUARE_WAVE_FREQ_1000HZ);
    // 2. Set the output to square wave mode (not interrupt)
    if (err == ESP_OK)
    {
        err = ds3231_interrupt_square_wave_control_flag_set(&rtc_device_handle, 0);
    }
    // 3. (Optional) Enable the oscillator if not already enabled
    if (err == ESP_OK)
    {
        err = ds3231_enable_oscillator_flag_set(&rtc_device_handle, true);
    }
    if (err != ESP_OK)
    {
        ESP_LOGE("CLOCK", "Failed to configure RTC square wave: %s", esp_err_to_name(err));
        cleanup_external_clock();
        return pdFALSE;
    }
    // 4. (Optional) Enable 32kHz output if needed (not required for SQW pin)
    // ds3231_32kHz_out_enable_flag_set(rtc_handle, true);

    // init pin RTC_SQW as interrupt
    gpio_config_t interrupt_pin_enable = {
        .pin_bit_mask = (1ULL << RTC_SQW),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_POSEDGE, // Trigger on rising edge
    };

    err = gpio_config(&interrupt_pin_enable);
    if (err != ESP_OK)
    {
        ESP_LOGE("CLOCK", "Failed to configure RTC interrupt GPIO: %s", esp_err_to_name(err));
        cleanup_external_clock();
        return pdFALSE;
    }

    timePrintQueue = xQueueCreate(1, sizeof(int64_t));
    if (!timePrintQueue)
    {
        ESP_LOGE("CLOCK", "Failed to create RTC event queue");
        cleanup_external_clock();
        return pdFALSE;
    }

    timeval_t current;
    gettimeofday(&current, NULL);
    rtc_time = TIME_US(current);

    err = gpio_isr_handler_add(RTC_SQW, rtc_sqw_isr_handler, NULL);
    if (err != ESP_OK)
    {
        ESP_LOGE("CLOCK", "Failed to install RTC interrupt handler: %s", esp_err_to_name(err));
        cleanup_external_clock();
        return pdFALSE;
    }
    rtc_isr_installed = true;
    return pdTRUE;
}

void ClockTask(void *arg)
{
    ESP_LOGI(pcTaskGetName(NULL), "Clock Task started");
    while (true)
    {
        int64_t new_rtc_time = 0;
        if (xQueueReceive(timePrintQueue, &new_rtc_time, portMAX_DELAY))
        {
            // Synchronize time and log difference

            timeval_t current;
            gettimeofday(&current, NULL);

            int64_t time_since_initial = TIME_US(current) - new_rtc_time;

            timeval_t now;
            now.tv_sec = new_rtc_time / 1000000;
            now.tv_usec = new_rtc_time % 1000000;

            if (settimeofday(&now, NULL) != 0)
            {
                ESP_LOGE(pcTaskGetName(NULL), "settimeofday failed: %s", strerror(errno));
            }

            ESP_LOGI(pcTaskGetName(NULL), "Time difference: %lld", time_since_initial);
        }
    }
}
