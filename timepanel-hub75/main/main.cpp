#include "timepanel_common.h"

extern "C" {
#include "OTA.h"
}

namespace {

void timepanel_ota_status(dogdog_ota_event_t event, void *)
{
    switch (event) {
    case DOGDOG_OTA_EVENT_CHECKING:
    case DOGDOG_OTA_EVENT_WIFI_FOUND:
    case DOGDOG_OTA_EVENT_CONNECTING:
        g_screen_mode.store(static_cast<int>(ScreenMode::FirmwareCheck),
                            std::memory_order_release);
        render_firmware_check_screen();
        break;
    case DOGDOG_OTA_EVENT_UPDATING: {
        static const dogdog_ota_progress_t initial_progress = {
            .progress_percent = 0,
            .bytes_received = 0,
            .total_size = 0,
        };
        g_screen_mode.store(static_cast<int>(ScreenMode::FirmwareUpdate),
                            std::memory_order_release);
        render_firmware_upgrade_screen(&initial_progress);
        break;
    }
    case DOGDOG_OTA_EVENT_RESTARTING_FOR_UPDATE:
        g_screen_mode.store(static_cast<int>(ScreenMode::FirmwareUpdate),
                            std::memory_order_release);
        render_firmware_upgrade_screen(nullptr);
        break;
    default:
        break;
    }
}

void timepanel_ota_progress(const dogdog_ota_progress_t *progress, void *)
{
    if (progress != nullptr) {
        g_screen_mode.store(static_cast<int>(ScreenMode::FirmwareUpdate),
                            std::memory_order_release);
    }
    render_firmware_upgrade_screen(progress);
}

} // namespace

extern "C" void app_main(void)
{
    if (dogdog_ota_update_pending()) {
        const dogdog_ota_config_t pending_ota_config = {
            .device_name = "timepanel-hub75",
            .status_cb = nullptr,
            .progress_cb = nullptr,
            .user_ctx = nullptr,
            .restart_before_update = false,
            .restart_delay_ms = 0,
        };
        ESP_ERROR_CHECK_WITHOUT_ABORT(
            dogdog_ota_check_and_update_in_task(&pending_ota_config, 0));
    }

    const int64_t now_us = esp_timer_get_time();
    g_screen_started_us.store(now_us, std::memory_order_release);
    g_power_status_icon_until_us.store(
        now_us + static_cast<int64_t>(POWER_STATUS_ICON_DURATION_MS) * 1000LL,
        std::memory_order_release);
    mark_timepanel_activity(now_us);
    g_runner_mutex = xSemaphoreCreateMutex();
    if (g_runner_mutex == nullptr) {
        ESP_LOGE(TAG, "Runner state mutex could not be created");
        abort();
    }
    initialize_runner_state();

    initialize_hub75();
    initialize_lvgl();

    const dogdog_ota_config_t ota_config = {
        .device_name = "timepanel-hub75",
        .status_cb = timepanel_ota_status,
        .progress_cb = timepanel_ota_progress,
        .user_ctx = nullptr,
        .restart_before_update = true,
        .restart_delay_ms = 1800,
    };
    ESP_ERROR_CHECK_WITHOUT_ABORT(
        dogdog_ota_check_and_update_in_task(&ota_config, 0));

    initialize_environment_sensor();
    log_environment_temperature(read_environment_sensor());
    start_environment_sensor_log_task();
    initialize_wireless_receiver();

    xTaskCreate(ui_task, "timepanel_ui", 6144, nullptr, 5, nullptr);
    xTaskCreate(button_task, "mode_button", 2048, nullptr, 4, nullptr);

    ESP_LOGI(TAG,
             "Timepanel HUB75 started at %dx%d with brightness %d",
             DISPLAY_WIDTH,
             DISPLAY_HEIGHT,
             hub75_brightness());
}
