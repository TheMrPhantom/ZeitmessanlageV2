#include "timepanel_common.h"

void switch_to_runner_preview()
{
    g_frizzles_reset_requested.store(true, std::memory_order_release);
    g_screen_started_us.store(esp_timer_get_time(), std::memory_order_release);
    g_screen_mode.store(static_cast<int>(ScreenMode::RunnerPreview),
                        std::memory_order_release);
}

void switch_to_parcours_intro()
{
    g_frizzles_reset_requested.store(true, std::memory_order_release);
    g_screen_started_us.store(esp_timer_get_time(), std::memory_order_release);
    g_screen_mode.store(static_cast<int>(ScreenMode::ParcoursIntro),
                        std::memory_order_release);
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

void runner_demo_preview_task(void *)
{
    vTaskDelay(pdMS_TO_TICKS(STARTUP_SPLASH_DURATION_MS + 500));

    apply_runner_command_reset("Justin", "Schiel", "Joy");
    vTaskDelay(pdMS_TO_TICKS(2000));

    apply_runner_command_start(400);
    vTaskDelay(pdMS_TO_TICKS(2800));

    apply_runner_command_refusal(1);
    vTaskDelay(pdMS_TO_TICKS(2600));

    apply_runner_command_fault(1);
    vTaskDelay(pdMS_TO_TICKS(2600));

    apply_runner_command_dis();
    vTaskDelay(pdMS_TO_TICKS(3200));

    apply_runner_command_reset("Max", "Mustermann", "Kira");
    vTaskDelay(pdMS_TO_TICKS(2000));

    apply_runner_command_start(0);
    vTaskDelay(pdMS_TO_TICKS(14500));

    apply_runner_command_stop(14500);
    vTaskDelete(nullptr);
}

void start_runner_demo_preview_task()
{
    if (!RUNNER_DEMO_PREVIEW_ENABLED) {
        return;
    }

    const BaseType_t result = xTaskCreate(runner_demo_preview_task,
                                         "runner_demo",
                                         3072,
                                         nullptr,
                                         3,
                                         nullptr);
    if (result != pdPASS) {
        ESP_LOGW(TAG, "Runner demo preview task could not be started");
    }
}

void ui_task(void *)
{
    int last_static_mode = -1;

    while (true) {
        const ScreenMode mode = static_cast<ScreenMode>(
            g_screen_mode.load(std::memory_order_acquire));
        const int64_t now_us = esp_timer_get_time();
        const int64_t started_us =
            g_screen_started_us.load(std::memory_order_acquire);
        const int64_t elapsed_ms = (now_us - started_us) / 1000;

        switch (mode) {
        case ScreenMode::StartupSplash:
            if (elapsed_ms >= STARTUP_SPLASH_DURATION_MS) {
                switch_to_runner_preview();
                last_static_mode = -1;
            } else {
                if (last_static_mode !=
                    static_cast<int>(ScreenMode::StartupSplash)) {
                    render_startup_splash_screen();
                    last_static_mode =
                        static_cast<int>(ScreenMode::StartupSplash);
                }
                vTaskDelay(pdMS_TO_TICKS(100));
            }
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
                TIMER_SECONDS * 1000LL + COUNTDOWN_FINISHED_HOLD_MS) {
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
            if (last_static_mode != static_cast<int>(ScreenMode::ParcoursEnded)) {
                render_ended_screen();
                last_static_mode = static_cast<int>(ScreenMode::ParcoursEnded);
            }
            vTaskDelay(pdMS_TO_TICKS(100));
            break;
        }
    }
}

