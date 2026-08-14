#include "timepanel_common.h"

extern "C" void app_main(void)
{
    const int64_t now_us = esp_timer_get_time();
    g_screen_started_us.store(now_us, std::memory_order_release);
    mark_timepanel_activity(now_us);
    g_runner_mutex = xSemaphoreCreateMutex();
    if (g_runner_mutex == nullptr) {
        ESP_LOGE(TAG, "Runner state mutex could not be created");
        abort();
    }
    initialize_runner_state();

    initialize_hub75();
    initialize_lvgl();
    initialize_environment_sensor();
    log_environment_temperature(read_environment_sensor());
    start_environment_sensor_log_task();
    initialize_wireless_receiver();

    xTaskCreate(ui_task, "timepanel_ui", 6144, nullptr, 5, nullptr);
    xTaskCreate(button_task, "mode_button", 2048, nullptr, 4, nullptr);
    start_runner_demo_preview_task();

    ESP_LOGI(TAG,
             "Timepanel HUB75 started at %dx%d with brightness %d",
             DISPLAY_WIDTH,
             DISPLAY_HEIGHT,
             CONFIG_TIMEPANEL_PANEL_BRIGHTNESS);
}
