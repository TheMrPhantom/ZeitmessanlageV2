#include "timepanel_common.h"

namespace {

bool idle_splash_due(ScreenMode mode, int64_t now_us, int64_t screen_started_us)
{
    if (IDLE_SPLASH_SECONDS <= 0 || mode == ScreenMode::StartupSplash ||
        mode == ScreenMode::IdleSplash || mode == ScreenMode::ParcoursIntro ||
        mode == ScreenMode::ParcoursStartWhoosh ||
        mode == ScreenMode::ParcoursTimer ||
        mode == ScreenMode::ParcoursWhoosh) {
        return false;
    }

    if (mode == ScreenMode::RunnerPreview && runner_timer_running()) {
        return false;
    }

    const int64_t last_activity_us =
        g_last_activity_us.load(std::memory_order_acquire);
    const int64_t idle_baseline_us = std::max(last_activity_us, screen_started_us);
    return idle_baseline_us > 0 &&
           now_us - idle_baseline_us >=
               static_cast<int64_t>(IDLE_SPLASH_SECONDS) * 1000000LL;
}

} // namespace

void mark_timepanel_activity(int64_t now_us)
{
    g_last_activity_us.store(now_us, std::memory_order_release);
}

void mark_timepanel_activity()
{
    mark_timepanel_activity(esp_timer_get_time());
}

void switch_to_runner_preview()
{
    const int64_t now_us = esp_timer_get_time();
    mark_timepanel_activity(now_us);
    g_frizzles_reset_requested.store(true, std::memory_order_release);
    g_screen_started_us.store(now_us, std::memory_order_release);
    g_screen_mode.store(static_cast<int>(ScreenMode::RunnerPreview),
                        std::memory_order_release);
}

void switch_to_parcours_intro()
{
    switch_to_parcours_intro(TIMER_SECONDS * 1000U);
}

void switch_to_parcours_intro(uint32_t duration_ms)
{
    const int64_t now_us = esp_timer_get_time();
    mark_timepanel_activity(now_us);
    g_parcours_duration_ms.store(std::max<uint32_t>(1000, duration_ms),
                                 std::memory_order_release);
    g_frizzles_reset_requested.store(true, std::memory_order_release);
    g_screen_started_us.store(now_us, std::memory_order_release);
    g_screen_mode.store(static_cast<int>(ScreenMode::ParcoursIntro),
                        std::memory_order_release);
}

void apply_parcours_command_start(uint32_t duration_ms)
{
    switch_to_parcours_intro(duration_ms);
}

void button_task(void *)
{
    gpio_config_t io_config{};
    io_config.pin_bit_mask = 1ULL << CONFIG_TIMEPANEL_BUTTON_GPIO;
    io_config.mode = GPIO_MODE_INPUT;
    io_config.pull_up_en = GPIO_PULLUP_ENABLE;
    io_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_config.intr_type = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&io_config));

    int stable_level = gpio_get_level(static_cast<gpio_num_t>(CONFIG_TIMEPANEL_BUTTON_GPIO));
    int last_level = stable_level;
    int64_t changed_at_us = esp_timer_get_time();
    int64_t last_press_us = 0;

    while (true) {
        const int current_level =
            gpio_get_level(static_cast<gpio_num_t>(CONFIG_TIMEPANEL_BUTTON_GPIO));
        const int64_t now_us = esp_timer_get_time();

        if (current_level != last_level) {
            last_level = current_level;
            changed_at_us = now_us;
        }

        const bool debounced =
            (now_us - changed_at_us) >= BUTTON_DEBOUNCE_MS * 1000LL;
        if (debounced && current_level != stable_level) {
            stable_level = current_level;
            if (stable_level == 0 &&
                (now_us - last_press_us) >= BUTTON_REPEAT_GUARD_MS * 1000LL) {
                last_press_us = now_us;
                mark_timepanel_activity(now_us);
                const ScreenMode mode = static_cast<ScreenMode>(
                    g_screen_mode.load(std::memory_order_acquire));
                if (mode == ScreenMode::RunnerPreview) {
                    switch_to_parcours_intro();
                } else {
                    switch_to_runner_preview();
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void ui_task(void *)
{
    int last_static_mode = -1;
    bool last_power_status_icon_visible = false;

    while (true) {
        const ScreenMode mode = static_cast<ScreenMode>(
            g_screen_mode.load(std::memory_order_acquire));
        const int64_t now_us = esp_timer_get_time();
        const int64_t started_us =
            g_screen_started_us.load(std::memory_order_acquire);
        const int64_t elapsed_ms = (now_us - started_us) / 1000;
        const bool status_icon_visible = power_status_icon_visible(now_us);

        if (idle_splash_due(mode, now_us, started_us)) {
            g_screen_started_us.store(now_us, std::memory_order_release);
            g_screen_mode.store(static_cast<int>(ScreenMode::IdleSplash),
                                std::memory_order_release);
            last_static_mode = -1;
            vTaskDelay(pdMS_TO_TICKS(40));
            continue;
        }

        switch (mode) {
        case ScreenMode::StartupSplash:
            if (last_static_mode !=
                    static_cast<int>(ScreenMode::StartupSplash) ||
                last_power_status_icon_visible != status_icon_visible) {
                render_startup_splash_screen();
                last_static_mode =
                    static_cast<int>(ScreenMode::StartupSplash);
                last_power_status_icon_visible = status_icon_visible;
            }
            vTaskDelay(pdMS_TO_TICKS(status_icon_visible ? 100 : 250));
            break;

        case ScreenMode::RunnerPreview:
            render_runner_preview_screen(now_us);
            last_static_mode = static_cast<int>(ScreenMode::RunnerPreview);
            vTaskDelay(pdMS_TO_TICKS(40));
            break;

        case ScreenMode::ParcoursIntro:
            last_static_mode = -1;
            if (elapsed_ms >= INTRO_SECONDS * 1000LL) {
                g_screen_started_us.store(now_us, std::memory_order_release);
                g_screen_mode.store(static_cast<int>(ScreenMode::ParcoursStartWhoosh),
                                    std::memory_order_release);
            } else {
                render_intro_screen(elapsed_ms);
                vTaskDelay(pdMS_TO_TICKS(50));
            }
            break;

        case ScreenMode::ParcoursStartWhoosh:
            last_static_mode = -1;
            if (elapsed_ms >= START_WHOOSH_DURATION_MS) {
                g_frizzles_reset_requested.store(true, std::memory_order_release);
                g_screen_started_us.store(now_us, std::memory_order_release);
                g_screen_mode.store(static_cast<int>(ScreenMode::ParcoursTimer),
                                    std::memory_order_release);
            } else {
                render_start_whoosh_screen(elapsed_ms);
                vTaskDelay(pdMS_TO_TICKS(35));
            }
            break;

        case ScreenMode::ParcoursTimer:
            last_static_mode = -1;
            if (elapsed_ms >=
                g_parcours_duration_ms.load(std::memory_order_acquire) +
                    COUNTDOWN_FINISHED_HOLD_MS) {
                g_screen_started_us.store(now_us, std::memory_order_release);
                g_screen_mode.store(static_cast<int>(ScreenMode::ParcoursWhoosh),
                                    std::memory_order_release);
            } else {
                render_timer_screen(elapsed_ms);
                vTaskDelay(pdMS_TO_TICKS(50));
            }
            break;

        case ScreenMode::ParcoursWhoosh:
            last_static_mode = -1;
            if (elapsed_ms >= WHOOSH_DURATION_MS) {
                g_screen_started_us.store(now_us, std::memory_order_release);
                g_screen_mode.store(static_cast<int>(ScreenMode::ParcoursEnded),
                                    std::memory_order_release);
            } else {
                render_whoosh_screen(elapsed_ms);
                vTaskDelay(pdMS_TO_TICKS(35));
            }
            break;

        case ScreenMode::ParcoursEnded:
            if (last_static_mode != static_cast<int>(ScreenMode::ParcoursEnded) ||
                last_power_status_icon_visible != status_icon_visible) {
                render_ended_screen();
                last_static_mode = static_cast<int>(ScreenMode::ParcoursEnded);
                last_power_status_icon_visible = status_icon_visible;
            }
            vTaskDelay(pdMS_TO_TICKS(status_icon_visible ? 100 : 250));
            break;

        case ScreenMode::IdleSplash:
            if (last_static_mode != static_cast<int>(ScreenMode::IdleSplash) ||
                last_power_status_icon_visible != status_icon_visible) {
                render_startup_splash_screen();
                last_static_mode = static_cast<int>(ScreenMode::IdleSplash);
                last_power_status_icon_visible = status_icon_visible;
            }
            vTaskDelay(pdMS_TO_TICKS(status_icon_visible ? 100 : 250));
            break;
        }
    }
}

