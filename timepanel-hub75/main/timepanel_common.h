#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_lvgl_port.h"
#include "hub75.h"
#include "lvgl.h"

inline constexpr char TAG[] = "timepanel_hub75";

inline constexpr int SINGLE_PANEL_WIDTH = CONFIG_TIMEPANEL_PANEL_WIDTH;
inline constexpr int SINGLE_PANEL_HEIGHT = CONFIG_TIMEPANEL_PANEL_HEIGHT;
inline constexpr int MATRIX_CHAIN_WIDTH = CONFIG_TIMEPANEL_MATRIX_CHAIN_WIDTH;
inline constexpr int MATRIX_CHAIN_HEIGHT = CONFIG_TIMEPANEL_MATRIX_CHAIN_HEIGHT;
inline constexpr int DISPLAY_WIDTH = SINGLE_PANEL_WIDTH * MATRIX_CHAIN_WIDTH;
inline constexpr int DISPLAY_HEIGHT = SINGLE_PANEL_HEIGHT * MATRIX_CHAIN_HEIGHT;
inline constexpr int PROGRESS_BAR_HEIGHT = 4;
inline constexpr int FRIZZLES_HEIGHT = DISPLAY_HEIGHT - PROGRESS_BAR_HEIGHT;
inline constexpr size_t RGB565_BYTES_PER_PIXEL = 2;
inline constexpr size_t DISPLAY_STRIDE_BYTES =
    ((static_cast<size_t>(DISPLAY_WIDTH) * RGB565_BYTES_PER_PIXEL +
      static_cast<size_t>(LV_DRAW_BUF_STRIDE_ALIGN) - 1) /
     static_cast<size_t>(LV_DRAW_BUF_STRIDE_ALIGN)) *
    static_cast<size_t>(LV_DRAW_BUF_STRIDE_ALIGN);

inline constexpr int TIMER_SECONDS = CONFIG_TIMEPANEL_PARCOURS_SECONDS;
inline constexpr int INTRO_SECONDS = CONFIG_TIMEPANEL_PARCOURS_INTRO_SECONDS;
inline constexpr int STARTUP_SPLASH_DURATION_MS = 7000;
#ifdef CONFIG_TIMEPANEL_IDLE_SPLASH_SECONDS
inline constexpr int IDLE_SPLASH_SECONDS = CONFIG_TIMEPANEL_IDLE_SPLASH_SECONDS;
#else
inline constexpr int IDLE_SPLASH_SECONDS = 300;
#endif
inline constexpr int START_WHOOSH_DURATION_MS = 1000;
inline constexpr int WHOOSH_DURATION_MS = 1400;
inline constexpr int COUNTDOWN_FINISHED_HOLD_MS = 3000;
inline constexpr int RUNNER_LABEL_SCROLL_SPEED = 20;
inline constexpr int RUNNER_TIME_OVERLAP = 5;
inline constexpr int RUNNER_WHOOSH_DURATION_MS = 760;
#ifdef CONFIG_TIMEPANEL_DEMO_PREVIEW_ENABLED
inline constexpr bool RUNNER_DEMO_PREVIEW_ENABLED = true;
#else
inline constexpr bool RUNNER_DEMO_PREVIEW_ENABLED = false;
#endif
inline constexpr int BUTTON_DEBOUNCE_MS = 45;
inline constexpr int BUTTON_REPEAT_GUARD_MS = 250;
inline constexpr float TWO_PI = 6.28318530717958647692f;

inline constexpr uint32_t START_BLUE = 0x1064ff;
inline constexpr uint32_t START_BLUE_LIGHT = 0x6ed6ff;
inline constexpr uint32_t END_RED = 0xff1515;
inline constexpr uint32_t END_RED_LIGHT = 0xff3030;
inline constexpr uint32_t DOGDOG_BLUE = 0x396689;
inline constexpr uint32_t DOGDOG_BLUE_DIM = 0x1d3446;
inline constexpr uint32_t SPLASH_WHITE_DIM = 0x9bb5c2;
inline constexpr uint32_t RUN_WHITE = 0xffffff;
inline constexpr uint32_t RUN_GREEN = 0x2dff68;
inline constexpr uint32_t RUN_ORANGE = 0xff8a00;
inline constexpr uint32_t RUN_RED = 0xff1515;

inline constexpr size_t RUNNER_NAME_TEXT_SIZE = 32;
inline constexpr size_t RUNNER_FULL_NAME_TEXT_SIZE = 64;
inline constexpr size_t RUNNER_DOG_TEXT_SIZE = 32;
inline constexpr size_t RUNNER_TIME_TEXT_SIZE = 16;
inline constexpr size_t RUNNER_WHOOSH_BAND_COUNT = 12;
inline constexpr int SPLASH_LOGO_WIDTH = 38;
inline constexpr int SPLASH_LOGO_HEIGHT = 30;

inline constexpr char START_TEXT[] = "Parcoursbegehung jetzt";
inline constexpr char ENDED_TEXT[] = "Parcoursbegehung beendet";

static_assert(SINGLE_PANEL_WIDTH > 0 && SINGLE_PANEL_HEIGHT > 0,
              "Panel dimensions must be configured");
static_assert(MATRIX_CHAIN_WIDTH > 0 && MATRIX_CHAIN_HEIGHT > 0,
              "Matrix chain dimensions must be configured");
static_assert(DISPLAY_WIDTH <= 512 && DISPLAY_HEIGHT <= 128,
              "Display buffer size is unexpectedly large for internal RAM");
static_assert(FRIZZLES_HEIGHT > 0,
              "Progress bar leaves no room for the timer background");

enum class ScreenMode : int {
    StartupSplash = 0,
    RunnerPreview = 1,
    ParcoursIntro = 2,
    ParcoursStartWhoosh = 3,
    ParcoursTimer = 4,
    ParcoursWhoosh = 5,
    ParcoursEnded = 6,
    IdleSplash = 7,
};

enum class RunnerWhooshType : uint8_t {
    None,
    FullWhite,
    FullGreen,
    FaultOrange,
    RefusalOrange,
    FullRed,
};

struct RunnerWhoosh {
    RunnerWhooshType type = RunnerWhooshType::None;
    int64_t started_us = 0;
    int duration_ms = RUNNER_WHOOSH_DURATION_MS;
};

struct RunnerState {
    std::array<char, RUNNER_NAME_TEXT_SIZE> first_name{};
    std::array<char, RUNNER_NAME_TEXT_SIZE> last_name{};
    std::array<char, RUNNER_DOG_TEXT_SIZE> dog_name{};
    std::array<char, RUNNER_NAME_TEXT_SIZE> pending_first_name{};
    std::array<char, RUNNER_NAME_TEXT_SIZE> pending_last_name{};
    std::array<char, RUNNER_DOG_TEXT_SIZE> pending_dog_name{};
    std::array<char, RUNNER_TIME_TEXT_SIZE> time_text{};
    int faults = 0;
    int refusals = 0;
    bool running = false;
    bool disqualified = false;
    int64_t timer_started_us = 0;
    uint32_t value_color = RUN_WHITE;
    RunnerWhoosh whoosh{};
};

struct RunnerSnapshot {
    std::array<char, RUNNER_NAME_TEXT_SIZE> first_name{};
    std::array<char, RUNNER_NAME_TEXT_SIZE> last_name{};
    std::array<char, RUNNER_DOG_TEXT_SIZE> dog_name{};
    std::array<char, RUNNER_TIME_TEXT_SIZE> time_text{};
    int faults = 0;
    int refusals = 0;
    bool disqualified = false;
    uint32_t value_color = RUN_WHITE;
    RunnerWhoosh whoosh{};
};

struct RunnerLayout {
    int time_x = 0;
    int time_y = 0;
    int time_width = 0;
    int time_scale = 1;
    int stats_x = 0;
    int fault_y = 0;
    int refusal_y = 0;
    int stat_height = 0;
    int stat_scale = 1;
};

struct RunnerLabelCache {
    std::array<char, RUNNER_FULL_NAME_TEXT_SIZE> text{};
    const lv_font_t *font = nullptr;
    int x = -1;
    int y = -1;
    int width = -1;
    int height = -1;
    bool valid = false;
};

struct EnvironmentReading {
    bool valid = false;
    int temperature_c_tenths = 0;
    int humidity_percent = 0;
};

struct RgbPixel {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
};

template <size_t Size>
void copy_text(std::array<char, Size> &target, const char *source)
{
    if (source == nullptr) {
        source = "";
    }
    std::snprintf(target.data(), target.size(), "%s", source);
}

extern std::unique_ptr<Hub75Driver> g_hub75;
extern lv_display_t *g_display;
extern lv_obj_t *g_canvas;
extern lv_obj_t *g_runner_name_label;
extern lv_obj_t *g_runner_dog_label;
extern RunnerLabelCache g_runner_name_label_cache;
extern RunnerLabelCache g_runner_dog_label_cache;
extern std::array<lv_obj_t *, RUNNER_WHOOSH_BAND_COUNT> g_runner_whoosh_bands;
extern i2c_master_bus_handle_t g_sensor_i2c_bus;
extern i2c_master_dev_handle_t g_fht40;
extern SemaphoreHandle_t g_sensor_mutex;
extern SemaphoreHandle_t g_runner_mutex;
extern RunnerState g_runner_state;
extern std::atomic<int> g_screen_mode;
extern std::atomic<int64_t> g_screen_started_us;
extern std::atomic<int64_t> g_last_activity_us;
extern std::atomic<int64_t> g_parcours_duration_ms;
extern std::atomic_bool g_frizzles_reset_requested;
extern std::atomic<uint32_t> g_runner_revision;
extern std::array<uint8_t, DISPLAY_STRIDE_BYTES * DISPLAY_HEIGHT>
    g_lvgl_draw_buffer;
extern std::array<uint8_t, DISPLAY_STRIDE_BYTES * DISPLAY_HEIGHT>
    g_canvas_buffer;
extern std::array<RgbPixel, DISPLAY_WIDTH * FRIZZLES_HEIGHT> g_frizzles_buffer;
extern std::array<RgbPixel, DISPLAY_WIDTH * FRIZZLES_HEIGHT>
    g_frizzles_blur_buffer;

void initialize_runner_state();
RunnerSnapshot runner_snapshot(int64_t now_us);
bool runner_whoosh_active(const RunnerWhoosh &whoosh, int64_t now_us);
bool runner_timer_running();
void apply_runner_command_reset(const char *first_name,
                                const char *last_name,
                                const char *dog_name);
void apply_runner_command_competitor(const char *first_name,
                                     const char *last_name,
                                     const char *dog_name);
void apply_runner_command_start(int offset_ms);
void apply_runner_command_stop(int elapsed_ms);
void apply_runner_command_fault(int faults);
void apply_runner_command_refusal(int refusals);
void apply_runner_command_dis();
void apply_parcours_command_start(uint32_t duration_ms);

uint16_t rgb565_from_rgb(const RgbPixel &pixel);
RgbPixel make_rgb(uint32_t color);
uint32_t rgb_to_hex(const RgbPixel &pixel);
uint8_t scale_channel(uint8_t value, int scale, int maximum);
RgbPixel scale_rgb(const RgbPixel &pixel, int scale, int maximum);
RgbPixel blend_rgb(const RgbPixel &from,
                   const RgbPixel &to,
                   int amount,
                   int maximum);
lv_color_t blend_hex(uint32_t from, uint32_t to, int amount, int maximum);
int ease_out_cubic_per_mille(int progress);
void canvas_pixel_direct(int x, int y, const RgbPixel &pixel);
void canvas_rect_direct(int x,
                        int y,
                        int width,
                        int height,
                        lv_color_t color,
                        lv_opa_t opacity = LV_OPA_COVER);
void clear_canvas_buffer();
void present_canvas_buffer();
void canvas_rect(lv_layer_t *layer,
                 int x,
                 int y,
                 int width,
                 int height,
                 lv_color_t color,
                 lv_opa_t opacity = LV_OPA_COVER);
lv_point_t measure_text(const char *text, const lv_font_t *font);
void draw_canvas_label(lv_layer_t *layer,
                       int x,
                       int y,
                       const char *text,
                       const lv_font_t *font,
                       lv_color_t color,
                       lv_text_align_t align = LV_TEXT_ALIGN_LEFT,
                       int max_width = LV_COORD_MAX);
const lv_font_t *font_16();
const lv_font_t *font_18();
const lv_font_t *font_14();
const lv_font_t *font_12();
const lv_font_t *status_font_for_text(const char *text);
const lv_font_t *font_for_row_height(int max_height);
int pixel_text_width(const char *text, int scale);
void draw_pixel_text(lv_layer_t *layer,
                     int x,
                     int y,
                     const char *text,
                     int scale,
                     lv_color_t color);
void draw_pixel_text_direct(int x,
                            int y,
                            const char *text,
                            int scale,
                            lv_color_t color);
void draw_startup_splash_logo_direct();
void render_startup_splash_text(lv_layer_t *layer);

void hide_runner_labels();
void reset_runner_label_cache(RunnerLabelCache &cache);
void initialize_runner_label(lv_obj_t *label);
void show_runner_labels(const RunnerSnapshot &preview);
void hide_runner_whoosh_bands();
void initialize_runner_whoosh_band(lv_obj_t *band);
void update_runner_full_whoosh_bands(const RunnerSnapshot &snapshot,
                                     int64_t now_us);
void draw_runner_preview(lv_layer_t *layer, const RunnerSnapshot &preview);
void draw_runner_area_whoosh(lv_layer_t *layer,
                             const RunnerSnapshot &snapshot,
                             int64_t now_us);

void render_parcours_intro(lv_layer_t *layer, int64_t elapsed_ms);
void render_parcours_start_whoosh(lv_layer_t *layer, int64_t elapsed_ms);
void render_parcours_timer(int64_t elapsed_ms);
void render_parcours_whoosh(int64_t elapsed_ms);
void render_parcours_ended(lv_layer_t *layer);

void render_startup_splash_screen();
void render_runner_preview_screen(int64_t now_us);
void render_intro_screen(int64_t elapsed_ms);
void render_start_whoosh_screen(int64_t elapsed_ms);
void render_timer_screen(int64_t elapsed_ms);
void render_whoosh_screen(int64_t elapsed_ms);
void render_ended_screen();

void initialize_hub75();
void initialize_lvgl();
void initialize_environment_sensor();
EnvironmentReading read_environment_sensor();
void log_environment_temperature(const EnvironmentReading &reading);
void start_environment_sensor_log_task();
void initialize_wireless_receiver();

void mark_timepanel_activity();
void mark_timepanel_activity(int64_t now_us);
void switch_to_runner_preview();
void switch_to_parcours_intro();
void switch_to_parcours_intro(uint32_t duration_ms);
void button_task(void *);
void start_runner_demo_preview_task();
void ui_task(void *);
