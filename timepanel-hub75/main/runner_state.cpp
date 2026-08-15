#include "timepanel_common.h"

void format_run_time(int elapsed_ms, char *buffer, size_t buffer_size)
{
    elapsed_ms = std::max(0, elapsed_ms);
    const int seconds = elapsed_ms / 1000;
    const int centiseconds = (elapsed_ms % 1000) / 10;
    std::snprintf(buffer, buffer_size, "%d,%02d", seconds, centiseconds);
}

void normalize_run_time_text(std::array<char, RUNNER_TIME_TEXT_SIZE> &target)
{
    const size_t length = std::strlen(target.data());
    if (length > 0 &&
        (target[length - 1] == 's' || target[length - 1] == 'S')) {
        target[length - 1] = '\0';
    }
}

void set_runner_whoosh(RunnerState &state, RunnerWhooshType type, int64_t now_us)
{
    state.whoosh = RunnerWhoosh{
        .type = type,
        .started_us = now_us,
        .duration_ms = RUNNER_WHOOSH_DURATION_MS,
    };
}

bool runner_whoosh_active(const RunnerWhoosh &whoosh, int64_t now_us)
{
    if (whoosh.type == RunnerWhooshType::None) {
        return false;
    }
    const int64_t elapsed_ms = (now_us - whoosh.started_us) / 1000;
    return elapsed_ms >= 0 && elapsed_ms < whoosh.duration_ms;
}

bool runner_timer_running()
{
    bool running = false;
    bool locked = false;
    if (g_runner_mutex != nullptr) {
        locked = xSemaphoreTake(g_runner_mutex, pdMS_TO_TICKS(20)) == pdTRUE;
    }
    running = g_runner_state.running;
    if (locked) {
        xSemaphoreGive(g_runner_mutex);
    }
    return running;
}

void mark_runner_changed(int64_t now_us)
{
    mark_timepanel_activity(now_us);
    g_screen_started_us.store(now_us, std::memory_order_release);
    g_screen_mode.store(static_cast<int>(ScreenMode::RunnerPreview),
                        std::memory_order_release);
    g_runner_revision.fetch_add(1, std::memory_order_acq_rel);
}

void initialize_runner_state()
{
    g_runner_state.first_name.fill('\0');
    g_runner_state.last_name.fill('\0');
    g_runner_state.dog_name.fill('\0');
    g_runner_state.pending_first_name.fill('\0');
    g_runner_state.pending_last_name.fill('\0');
    g_runner_state.pending_dog_name.fill('\0');
    format_run_time(0, g_runner_state.time_text.data(),
                    g_runner_state.time_text.size());
    g_runner_state.faults = 0;
    g_runner_state.refusals = 0;
    g_runner_state.running = false;
    g_runner_state.disqualified = false;
    g_runner_state.value_color = RUN_WHITE;
    g_runner_state.whoosh.type = RunnerWhooshType::None;
}

RunnerSnapshot runner_snapshot(int64_t now_us)
{
    RunnerSnapshot snapshot{};
    bool locked = false;
    if (g_runner_mutex != nullptr) {
        locked = xSemaphoreTake(g_runner_mutex, pdMS_TO_TICKS(20)) == pdTRUE;
    }

    snapshot.first_name = g_runner_state.first_name;
    snapshot.last_name = g_runner_state.last_name;
    snapshot.dog_name = g_runner_state.dog_name;
    snapshot.time_text = g_runner_state.time_text;
    if (g_runner_state.running) {
        const int elapsed_ms =
            static_cast<int>(std::max<int64_t>(
                0, (now_us - g_runner_state.timer_started_us) / 1000));
        format_run_time(elapsed_ms,
                        snapshot.time_text.data(),
                        snapshot.time_text.size());
    }
    normalize_run_time_text(snapshot.time_text);
    snapshot.faults = g_runner_state.faults;
    snapshot.refusals = g_runner_state.refusals;
    snapshot.disqualified = g_runner_state.disqualified;
    snapshot.value_color = g_runner_state.value_color;
    snapshot.whoosh = g_runner_state.whoosh;

    if (locked) {
        xSemaphoreGive(g_runner_mutex);
    }
    return snapshot;
}

[[maybe_unused]] void apply_runner_command_reset(const char *first_name,
                                                const char *last_name,
                                                const char *dog_name)
{
    const int64_t now_us = esp_timer_get_time();
    if (g_runner_mutex != nullptr) {
        xSemaphoreTake(g_runner_mutex, portMAX_DELAY);
    }
    copy_text(g_runner_state.first_name, first_name);
    copy_text(g_runner_state.last_name, last_name);
    copy_text(g_runner_state.dog_name, dog_name);
    copy_text(g_runner_state.pending_first_name, first_name);
    copy_text(g_runner_state.pending_last_name, last_name);
    copy_text(g_runner_state.pending_dog_name, dog_name);
    format_run_time(0, g_runner_state.time_text.data(),
                    g_runner_state.time_text.size());
    g_runner_state.faults = 0;
    g_runner_state.refusals = 0;
    g_runner_state.running = false;
    g_runner_state.disqualified = false;
    g_runner_state.value_color = RUN_WHITE;
    set_runner_whoosh(g_runner_state, RunnerWhooshType::FullWhite, now_us);
    if (g_runner_mutex != nullptr) {
        xSemaphoreGive(g_runner_mutex);
    }
    mark_runner_changed(now_us);
}

[[maybe_unused]] void apply_runner_command_competitor(const char *first_name,
                                                     const char *last_name,
                                                     const char *dog_name)
{
    const int64_t now_us = esp_timer_get_time();
    if (g_runner_mutex != nullptr) {
        xSemaphoreTake(g_runner_mutex, portMAX_DELAY);
    }
    copy_text(g_runner_state.pending_first_name, first_name);
    copy_text(g_runner_state.pending_last_name, last_name);
    copy_text(g_runner_state.pending_dog_name, dog_name);
    if (g_runner_mutex != nullptr) {
        xSemaphoreGive(g_runner_mutex);
    }
    mark_timepanel_activity(now_us);
    g_runner_revision.fetch_add(1, std::memory_order_acq_rel);
}

[[maybe_unused]] void apply_runner_command_start(int offset_ms)
{
    const int64_t now_us = esp_timer_get_time();
    offset_ms = std::max(0, offset_ms);
    if (g_runner_mutex != nullptr) {
        xSemaphoreTake(g_runner_mutex, portMAX_DELAY);
    }
    g_runner_state.first_name = g_runner_state.pending_first_name;
    g_runner_state.last_name = g_runner_state.pending_last_name;
    g_runner_state.dog_name = g_runner_state.pending_dog_name;
    g_runner_state.timer_started_us =
        now_us - static_cast<int64_t>(offset_ms) * 1000;
    g_runner_state.faults = 0;
    g_runner_state.refusals = 0;
    g_runner_state.running = true;
    g_runner_state.disqualified = false;
    g_runner_state.value_color = RUN_WHITE;
    g_runner_state.whoosh.type = RunnerWhooshType::None;
    if (g_runner_mutex != nullptr) {
        xSemaphoreGive(g_runner_mutex);
    }
    mark_runner_changed(now_us);
}

[[maybe_unused]] void apply_runner_command_stop(int elapsed_ms)
{
    const int64_t now_us = esp_timer_get_time();
    if (g_runner_mutex != nullptr) {
        xSemaphoreTake(g_runner_mutex, portMAX_DELAY);
    }
    format_run_time(elapsed_ms,
                    g_runner_state.time_text.data(),
                    g_runner_state.time_text.size());
    g_runner_state.running = false;
    g_runner_state.disqualified = false;
    if (g_runner_state.faults == 0 && g_runner_state.refusals == 0) {
        g_runner_state.value_color = RUN_GREEN;
        set_runner_whoosh(g_runner_state, RunnerWhooshType::FullGreen, now_us);
    } else {
        g_runner_state.value_color = RUN_WHITE;
        g_runner_state.whoosh.type = RunnerWhooshType::None;
    }
    if (g_runner_mutex != nullptr) {
        xSemaphoreGive(g_runner_mutex);
    }
    mark_runner_changed(now_us);
}

[[maybe_unused]] void apply_runner_command_fault(int faults)
{
    const int64_t now_us = esp_timer_get_time();
    if (g_runner_mutex != nullptr) {
        xSemaphoreTake(g_runner_mutex, portMAX_DELAY);
    }
    g_runner_state.faults = std::max(0, faults);
    g_runner_state.value_color = RUN_WHITE;
    set_runner_whoosh(g_runner_state, RunnerWhooshType::FaultOrange, now_us);
    if (g_runner_mutex != nullptr) {
        xSemaphoreGive(g_runner_mutex);
    }
    mark_runner_changed(now_us);
}

[[maybe_unused]] void apply_runner_command_refusal(int refusals)
{
    const int64_t now_us = esp_timer_get_time();
    if (g_runner_mutex != nullptr) {
        xSemaphoreTake(g_runner_mutex, portMAX_DELAY);
    }
    g_runner_state.refusals = std::max(0, refusals);
    g_runner_state.value_color = RUN_WHITE;
    set_runner_whoosh(g_runner_state, RunnerWhooshType::RefusalOrange, now_us);
    if (g_runner_mutex != nullptr) {
        xSemaphoreGive(g_runner_mutex);
    }
    mark_runner_changed(now_us);
}

[[maybe_unused]] void apply_runner_command_dis()
{
    const int64_t now_us = esp_timer_get_time();
    if (g_runner_mutex != nullptr) {
        xSemaphoreTake(g_runner_mutex, portMAX_DELAY);
    }
    g_runner_state.running = false;
    g_runner_state.disqualified = true;
    g_runner_state.value_color = RUN_RED;
    set_runner_whoosh(g_runner_state, RunnerWhooshType::FullRed, now_us);
    if (g_runner_mutex != nullptr) {
        xSemaphoreGive(g_runner_mutex);
    }
    mark_runner_changed(now_us);
}

