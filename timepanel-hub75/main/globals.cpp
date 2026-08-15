#include "timepanel_common.h"

std::unique_ptr<Hub75Driver> g_hub75;
lv_display_t *g_display = nullptr;
lv_obj_t *g_canvas = nullptr;
lv_obj_t *g_runner_name_label = nullptr;
lv_obj_t *g_runner_dog_label = nullptr;
RunnerLabelCache g_runner_name_label_cache{};
RunnerLabelCache g_runner_dog_label_cache{};
std::array<lv_obj_t *, RUNNER_WHOOSH_BAND_COUNT> g_runner_whoosh_bands{};
i2c_master_bus_handle_t g_sensor_i2c_bus = nullptr;
i2c_master_dev_handle_t g_fht40 = nullptr;
SemaphoreHandle_t g_sensor_mutex = nullptr;
SemaphoreHandle_t g_runner_mutex = nullptr;
RunnerState g_runner_state{};

std::atomic<int> g_screen_mode{static_cast<int>(ScreenMode::StartupSplash)};
std::atomic<int64_t> g_screen_started_us{0};
std::atomic<int64_t> g_last_activity_us{0};
std::atomic<int64_t> g_power_status_icon_until_us{0};
std::atomic<int64_t> g_parcours_duration_ms{TIMER_SECONDS * 1000LL};
std::atomic_bool g_frizzles_reset_requested{true};
std::atomic<uint32_t> g_runner_revision{0};

EXT_RAM_BSS_ATTR alignas(4)
std::array<uint8_t, DISPLAY_STRIDE_BYTES * DISPLAY_HEIGHT>
    g_lvgl_draw_buffer{};
EXT_RAM_BSS_ATTR alignas(4)
std::array<uint8_t, DISPLAY_STRIDE_BYTES * DISPLAY_HEIGHT>
    g_canvas_buffer{};
EXT_RAM_BSS_ATTR std::array<RgbPixel, DISPLAY_WIDTH * FRIZZLES_HEIGHT>
    g_frizzles_buffer{};
EXT_RAM_BSS_ATTR std::array<RgbPixel, DISPLAY_WIDTH * FRIZZLES_HEIGHT>
    g_frizzles_blur_buffer{};

