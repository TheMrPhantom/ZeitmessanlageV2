#include "Clock.h"

int64_t rtc_time = 0;
QueueHandle_t timePrintQueue;

// Define the ISR handler for RTC_SQW pin
static void IRAM_ATTR rtc_sqw_isr_handler(void *arg)
{
    rtc_time += 1000000;
    // Notify task to perform time sync and logging
    xQueueSendFromISR(timePrintQueue, &rtc_time, 0);
}

BaseType_t init_external_clock()
{
    timePrintQueue = xQueueCreate(10, sizeof(int64_t));

    i2c_master_bus_handle_t *bus_handle =
        (i2c_master_bus_handle_t *)malloc(sizeof(i2c_master_bus_handle_t));
    if (!bus_handle)
    {
        ESP_LOGE("CLOCK", "Failed to allocate memory for i2c_master_bus_handle_t");
        return pdFALSE;
    }
    i2c_master_bus_config_t i2c_mst_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = -1,
        .scl_io_num = RTC_SCL,
        .sda_io_num = RTC_SDA,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_new_master_bus(&i2c_mst_config, bus_handle);
    rtc_handle_t *rtc_handle = ds3231_init(bus_handle);
    if (!rtc_handle)
    {
        ESP_LOGE("CLOCK", "No RTC Clock found! Relying on internal clock.");
        //free(bus_handle);
        return pdFALSE;
    }

    // Enable the square wave output on the DS3231
    // 1. Set the square wave frequency (e.g., 1000Hz)
    ds3231_square_wave_freq_set(rtc_handle, RTC_SQUARE_WAVE_FREQ_1000HZ);
    // 2. Set the output to square wave mode (not interrupt)
    ds3231_interrupt_square_wave_control_flag_set(rtc_handle, 0); // 0 = Square Wave
    // 3. (Optional) Enable the oscillator if not already enabled
    ds3231_enable_oscillator_flag_set(rtc_handle, true);
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

    gpio_config(&interrupt_pin_enable);

    gpio_isr_handler_add(RTC_SQW, rtc_sqw_isr_handler, (void *)rtc_handle);
    return pdTRUE;
}

void ClockTask(void *arg)
{
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

            settimeofday(&now, NULL);

            ESP_LOGI(pcTaskGetName(NULL), "Time difference: %lld", time_since_initial);
        }
    }
}