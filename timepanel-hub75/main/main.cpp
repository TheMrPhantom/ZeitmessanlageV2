#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <cstdio>
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

namespace {

constexpr char TAG[] = "timepanel_hub75";

constexpr int SINGLE_PANEL_WIDTH = CONFIG_TIMEPANEL_PANEL_WIDTH;
constexpr int SINGLE_PANEL_HEIGHT = CONFIG_TIMEPANEL_PANEL_HEIGHT;
constexpr int MATRIX_CHAIN_WIDTH = CONFIG_TIMEPANEL_MATRIX_CHAIN_WIDTH;
constexpr int MATRIX_CHAIN_HEIGHT = CONFIG_TIMEPANEL_MATRIX_CHAIN_HEIGHT;
constexpr int DISPLAY_WIDTH = SINGLE_PANEL_WIDTH * MATRIX_CHAIN_WIDTH;
constexpr int DISPLAY_HEIGHT = SINGLE_PANEL_HEIGHT * MATRIX_CHAIN_HEIGHT;
constexpr int PROGRESS_BAR_HEIGHT = 4;
constexpr int FRIZZLES_HEIGHT = DISPLAY_HEIGHT - PROGRESS_BAR_HEIGHT;
constexpr size_t RGB565_BYTES_PER_PIXEL = 2;
constexpr size_t DISPLAY_STRIDE_BYTES =
    ((static_cast<size_t>(DISPLAY_WIDTH) * RGB565_BYTES_PER_PIXEL +
      static_cast<size_t>(LV_DRAW_BUF_STRIDE_ALIGN) - 1) /
     static_cast<size_t>(LV_DRAW_BUF_STRIDE_ALIGN)) *
    static_cast<size_t>(LV_DRAW_BUF_STRIDE_ALIGN);
constexpr int TIMER_SECONDS = CONFIG_TIMEPANEL_PARCOURS_SECONDS;
constexpr int INTRO_SECONDS = CONFIG_TIMEPANEL_PARCOURS_INTRO_SECONDS;
constexpr int START_WHOOSH_DURATION_MS = 1000;
constexpr int WHOOSH_DURATION_MS = 1400;
constexpr int COUNTDOWN_FINISHED_HOLD_MS = 3000;
constexpr int RUNNER_LABEL_SCROLL_SPEED = 20;
constexpr int RUNNER_TIME_OVERLAP = 5;
constexpr int BUTTON_DEBOUNCE_MS = 45;
constexpr int BUTTON_REPEAT_GUARD_MS = 250;
constexpr float TWO_PI = 6.28318530717958647692f;
constexpr uint32_t START_BLUE = 0x1064ff;
constexpr uint32_t START_BLUE_LIGHT = 0x6ed6ff;
constexpr uint32_t END_RED = 0xff1515;
constexpr uint32_t END_RED_LIGHT = 0xff3030;

constexpr uint8_t FHT40_I2C_ADDRESS = 0x44;
constexpr uint8_t FHT40_MEASURE_HIGH_REPEATABILITY = 0xfd;
constexpr int FHT40_SIGNAL_MAX = 65535;
constexpr int FHT40_POWER_UP_DELAY_MS = 2;
constexpr int FHT40_MEASUREMENT_DELAY_MS = 25;
constexpr int FHT40_READ_RETRY_DELAY_MS = 5;
constexpr int FHT40_READ_ATTEMPTS = 3;
constexpr int FHT40_I2C_TIMEOUT_MS = 50;

#if CONFIG_TIMEPANEL_SENSOR_I2C_INTERNAL_PULLUPS
constexpr bool SENSOR_I2C_INTERNAL_PULLUPS = true;
#else
constexpr bool SENSOR_I2C_INTERNAL_PULLUPS = false;
#endif

#if CONFIG_TIMEPANEL_SENSOR_ENABLED
constexpr int SENSOR_LOG_INTERVAL_MS =
    CONFIG_TIMEPANEL_SENSOR_LOG_INTERVAL_SECONDS * 1000;
#endif

static_assert(SINGLE_PANEL_WIDTH > 0 && SINGLE_PANEL_HEIGHT > 0,
              "Panel dimensions must be configured");
static_assert(MATRIX_CHAIN_WIDTH > 0 && MATRIX_CHAIN_HEIGHT > 0,
              "Matrix chain dimensions must be configured");
static_assert(DISPLAY_WIDTH <= 512 && DISPLAY_HEIGHT <= 128,
              "Display buffer size is unexpectedly large for internal RAM");
static_assert(FRIZZLES_HEIGHT > 0,
              "Progress bar leaves no room for the timer background");

constexpr Hub75Pins HUB75_PINS{
    .r1 = 1,
    .g1 = 5,
    .b1 = 6,
    .r2 = 7,
    .g2 = 13,
    .b2 = 9,
    .a = 16,
    .b = 48,
    .c = 47,
    .d = 21,
    .e = 38,
    .lat = 8,
    .oe = 4,
    .clk = 18,
};

enum class ScreenMode : int {
    RunnerPreview = 0,
    ParcoursIntro = 1,
    ParcoursStartWhoosh = 2,
    ParcoursTimer = 3,
    ParcoursWhoosh = 4,
    ParcoursEnded = 5,
};

struct RunnerPreview {
    const char *first_name;
    const char *last_name;
    const char *dog_name;
    const char *time_text;
    int faults;
    int refusals;
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

std::unique_ptr<Hub75Driver> g_hub75;
lv_display_t *g_display = nullptr;
lv_obj_t *g_canvas = nullptr;
lv_obj_t *g_runner_name_label = nullptr;
lv_obj_t *g_runner_dog_label = nullptr;
i2c_master_bus_handle_t g_sensor_i2c_bus = nullptr;
i2c_master_dev_handle_t g_fht40 = nullptr;
SemaphoreHandle_t g_sensor_mutex = nullptr;

std::atomic<int> g_screen_mode{static_cast<int>(ScreenMode::RunnerPreview)};
std::atomic<int64_t> g_screen_started_us{0};
std::atomic_bool g_frizzles_reset_requested{true};

alignas(4) std::array<uint8_t, DISPLAY_STRIDE_BYTES * DISPLAY_HEIGHT>
    g_lvgl_draw_buffer{};
alignas(4) std::array<uint8_t, DISPLAY_STRIDE_BYTES * DISPLAY_HEIGHT>
    g_canvas_buffer{};
std::array<RgbPixel, DISPLAY_WIDTH * FRIZZLES_HEIGHT> g_frizzles_buffer{};
std::array<RgbPixel, DISPLAY_WIDTH * FRIZZLES_HEIGHT> g_frizzles_blur_buffer{};

constexpr RunnerPreview STARTUP_PREVIEW{
    .first_name = "Justin",
    .last_name = "Schiel",
    .dog_name = "Joy",
    .time_text = "37,93",
    .faults = 1,
    .refusals = 0,
};

constexpr char START_TEXT[] = "Parcoursbegehung jetzt";
constexpr char ENDED_TEXT[] = "Parcoursbegehung beendet";

uint16_t rgb565_from_color(lv_color_t color)
{
    return lv_color_to_u16(color);
}

uint16_t rgb565_from_rgb(const RgbPixel &pixel)
{
    return static_cast<uint16_t>(((pixel.r & 0xf8) << 8) |
                                 ((pixel.g & 0xfc) << 3) |
                                 (pixel.b >> 3));
}

RgbPixel rgb_from_rgb565(uint16_t color)
{
    const uint8_t red = static_cast<uint8_t>((color >> 11) & 0x1f);
    const uint8_t green = static_cast<uint8_t>((color >> 5) & 0x3f);
    const uint8_t blue = static_cast<uint8_t>(color & 0x1f);
    return RgbPixel{
        .r = static_cast<uint8_t>((red << 3) | (red >> 2)),
        .g = static_cast<uint8_t>((green << 2) | (green >> 4)),
        .b = static_cast<uint8_t>((blue << 3) | (blue >> 2)),
    };
}

uint16_t blend_rgb565(uint16_t foreground, uint16_t background, lv_opa_t opacity)
{
    if (opacity >= LV_OPA_MAX) {
        return foreground;
    }
    if (opacity <= LV_OPA_MIN) {
        return background;
    }

    const RgbPixel fg = rgb_from_rgb565(foreground);
    const RgbPixel bg = rgb_from_rgb565(background);
    const int alpha = opacity;
    const int inverse_alpha = 255 - alpha;
    const RgbPixel blended{
        .r = static_cast<uint8_t>(
            (static_cast<int>(fg.r) * alpha +
             static_cast<int>(bg.r) * inverse_alpha) /
            255),
        .g = static_cast<uint8_t>(
            (static_cast<int>(fg.g) * alpha +
             static_cast<int>(bg.g) * inverse_alpha) /
            255),
        .b = static_cast<uint8_t>(
            (static_cast<int>(fg.b) * alpha +
             static_cast<int>(bg.b) * inverse_alpha) /
            255),
    };
    return rgb565_from_rgb(blended);
}

uint16_t *canvas_row(int y)
{
    return reinterpret_cast<uint16_t *>(g_canvas_buffer.data() +
                                        static_cast<size_t>(y) *
                                            DISPLAY_STRIDE_BYTES);
}

void canvas_pixel_direct(int x, int y, const RgbPixel &pixel)
{
    if (x < 0 || x >= DISPLAY_WIDTH || y < 0 || y >= DISPLAY_HEIGHT) {
        return;
    }

    canvas_row(y)[x] = rgb565_from_rgb(pixel);
}

void canvas_rect_direct(int x, int y, int width, int height, lv_color_t color,
                        lv_opa_t opacity = LV_OPA_COVER)
{
    if (width <= 0 || height <= 0 || opacity <= LV_OPA_MIN) {
        return;
    }

    const int x1 = std::clamp(x, 0, DISPLAY_WIDTH);
    const int y1 = std::clamp(y, 0, DISPLAY_HEIGHT);
    const int x2 = std::clamp(x + width, 0, DISPLAY_WIDTH);
    const int y2 = std::clamp(y + height, 0, DISPLAY_HEIGHT);
    if (x2 <= x1 || y2 <= y1) {
        return;
    }

    const uint16_t foreground = rgb565_from_color(color);
    for (int row_y = y1; row_y < y2; ++row_y) {
        uint16_t *row = canvas_row(row_y);
        if (opacity >= LV_OPA_MAX) {
            std::fill(row + x1, row + x2, foreground);
        } else {
            for (int col = x1; col < x2; ++col) {
                row[col] = blend_rgb565(foreground, row[col], opacity);
            }
        }
    }
}

void clear_canvas_buffer()
{
    std::fill(g_canvas_buffer.begin(), g_canvas_buffer.end(), 0);
}

void present_canvas_buffer()
{
    if (lv_draw_buf_t *draw_buffer = lv_canvas_get_draw_buf(g_canvas);
        draw_buffer != nullptr) {
        lv_draw_buf_flush_cache(draw_buffer, nullptr);
    }
    lv_obj_invalidate(g_canvas);
    lv_refr_now(g_display);
}

void canvas_rect(lv_layer_t *layer, int x, int y, int width, int height,
                 lv_color_t color, lv_opa_t opacity = LV_OPA_COVER)
{
    if (width <= 0 || height <= 0) {
        return;
    }

    const int x1 = std::clamp(x, 0, DISPLAY_WIDTH);
    const int y1 = std::clamp(y, 0, DISPLAY_HEIGHT);
    const int x2 = std::clamp(x + width, 0, DISPLAY_WIDTH);
    const int y2 = std::clamp(y + height, 0, DISPLAY_HEIGHT);
    if (x2 <= x1 || y2 <= y1) {
        return;
    }

    lv_draw_rect_dsc_t descriptor;
    lv_draw_rect_dsc_init(&descriptor);
    descriptor.bg_color = color;
    descriptor.bg_opa = opacity;
    descriptor.border_width = 0;
    descriptor.radius = 0;

    lv_area_t area{
        .x1 = x1,
        .y1 = y1,
        .x2 = x2 - 1,
        .y2 = y2 - 1,
    };
    lv_draw_rect(layer, &descriptor, &area);
}

lv_point_t measure_text(const char *text, const lv_font_t *font)
{
    lv_point_t size{};
    lv_text_get_size(&size,
                     text,
                     font,
                     0,
                     0,
                     LV_COORD_MAX,
                     LV_TEXT_FLAG_EXPAND);
    return size;
}

void draw_canvas_label(lv_layer_t *layer, int x, int y, const char *text,
                       const lv_font_t *font, lv_color_t color,
                       lv_text_align_t align = LV_TEXT_ALIGN_LEFT,
                       int max_width = LV_COORD_MAX)
{
    if (text == nullptr || text[0] == '\0') {
        return;
    }

    lv_point_t size = measure_text(text, font);
    if (size.x <= 0 || size.y <= 0) {
        return;
    }

    const int width = max_width == LV_COORD_MAX ? size.x : max_width;
    lv_draw_label_dsc_t descriptor;
    lv_draw_label_dsc_init(&descriptor);
    descriptor.text = text;
    descriptor.text_local = 1;
    descriptor.text_size = size;
    descriptor.font = font;
    descriptor.color = color;
    descriptor.opa = LV_OPA_COVER;
    descriptor.align = align;
    descriptor.flag = static_cast<lv_text_flag_t>(LV_TEXT_FLAG_EXPAND |
                                                  LV_TEXT_FLAG_FIT);

    lv_area_t area{
        .x1 = x,
        .y1 = y,
        .x2 = x + width - 1,
        .y2 = y + size.y - 1,
    };
    lv_draw_label(layer, &descriptor, &area);
}

const lv_font_t *font_16()
{
#if LV_FONT_MONTSERRAT_16
    return &lv_font_montserrat_16;
#elif LV_FONT_MONTSERRAT_14
    return &lv_font_montserrat_14;
#else
    return LV_FONT_DEFAULT;
#endif
}

const lv_font_t *font_18()
{
#if LV_FONT_MONTSERRAT_18
    return &lv_font_montserrat_18;
#else
    return font_16();
#endif
}

const lv_font_t *font_14()
{
#if LV_FONT_MONTSERRAT_14
    return &lv_font_montserrat_14;
#else
    return LV_FONT_DEFAULT;
#endif
}

const lv_font_t *font_12()
{
#if LV_FONT_MONTSERRAT_12
    return &lv_font_montserrat_12;
#elif LV_FONT_MONTSERRAT_10
    return &lv_font_montserrat_10;
#else
    return LV_FONT_DEFAULT;
#endif
}

const lv_font_t *status_font_for_text(const char *text)
{
    const lv_font_t *font = font_18();
    if (measure_text(text, font).x <= DISPLAY_WIDTH - 2) {
        return font;
    }
    return font_16();
}

const lv_font_t *font_for_row_height(int max_height)
{
    const lv_font_t *font = font_18();
    if (measure_text("Mg", font).y <= max_height) {
        return font;
    }

    font = font_16();
    if (measure_text("Mg", font).y <= max_height) {
        return font;
    }

    font = font_14();
    if (measure_text("Mg", font).y <= max_height) {
        return font;
    }

    return font_12();
}

std::array<uint8_t, 7> glyph_for(char raw)
{
    const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(raw)));
    switch (c) {
    case '0':
        return {0b01110, 0b10001, 0b10011, 0b10101, 0b11001, 0b10001, 0b01110};
    case '1':
        return {0b00100, 0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110};
    case '2':
        return {0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0b01000, 0b11111};
    case '3':
        return {0b11110, 0b00001, 0b00001, 0b01110, 0b00001, 0b00001, 0b11110};
    case '4':
        return {0b00010, 0b00110, 0b01010, 0b10010, 0b11111, 0b00010, 0b00010};
    case '5':
        return {0b11111, 0b10000, 0b10000, 0b11110, 0b00001, 0b00001, 0b11110};
    case '6':
        return {0b00110, 0b01000, 0b10000, 0b11110, 0b10001, 0b10001, 0b01110};
    case '7':
        return {0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b01000, 0b01000};
    case '8':
        return {0b01110, 0b10001, 0b10001, 0b01110, 0b10001, 0b10001, 0b01110};
    case '9':
        return {0b01110, 0b10001, 0b10001, 0b01111, 0b00001, 0b00010, 0b11100};
    case 'A':
        return {0b01110, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001};
    case 'B':
        return {0b11110, 0b10001, 0b10001, 0b11110, 0b10001, 0b10001, 0b11110};
    case 'C':
        return {0b01110, 0b10001, 0b10000, 0b10000, 0b10000, 0b10001, 0b01110};
    case 'D':
        return {0b11110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b11110};
    case 'E':
        return {0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b11111};
    case 'F':
        return {0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b10000};
    case 'G':
        return {0b01110, 0b10001, 0b10000, 0b10111, 0b10001, 0b10001, 0b01110};
    case 'H':
        return {0b10001, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001};
    case 'I':
        return {0b01110, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110};
    case 'J':
        return {0b00111, 0b00010, 0b00010, 0b00010, 0b10010, 0b10010, 0b01100};
    case 'K':
        return {0b10001, 0b10010, 0b10100, 0b11000, 0b10100, 0b10010, 0b10001};
    case 'L':
        return {0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b11111};
    case 'M':
        return {0b10001, 0b11011, 0b10101, 0b10101, 0b10001, 0b10001, 0b10001};
    case 'N':
        return {0b10001, 0b11001, 0b10101, 0b10011, 0b10001, 0b10001, 0b10001};
    case 'O':
        return {0b01110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110};
    case 'P':
        return {0b11110, 0b10001, 0b10001, 0b11110, 0b10000, 0b10000, 0b10000};
    case 'Q':
        return {0b01110, 0b10001, 0b10001, 0b10001, 0b10101, 0b10010, 0b01101};
    case 'R':
        return {0b11110, 0b10001, 0b10001, 0b11110, 0b10100, 0b10010, 0b10001};
    case 'S':
        return {0b01111, 0b10000, 0b10000, 0b01110, 0b00001, 0b00001, 0b11110};
    case 'T':
        return {0b11111, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100};
    case 'U':
        return {0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110};
    case 'V':
        return {0b10001, 0b10001, 0b10001, 0b10001, 0b01010, 0b01010, 0b00100};
    case 'W':
        return {0b10001, 0b10001, 0b10001, 0b10101, 0b10101, 0b11011, 0b10001};
    case 'X':
        return {0b10001, 0b10001, 0b01010, 0b00100, 0b01010, 0b10001, 0b10001};
    case 'Y':
        return {0b10001, 0b10001, 0b01010, 0b00100, 0b00100, 0b00100, 0b00100};
    case 'Z':
        return {0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b10000, 0b11111};
    case ':':
        return {0b00000, 0b00100, 0b00100, 0b00000, 0b00100, 0b00100, 0b00000};
    case ',':
        return {0b00000, 0b00000, 0b00000, 0b00000, 0b00100, 0b00100, 0b01000};
    case '.':
        return {0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b01100, 0b01100};
    case '!':
        return {0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00000, 0b00100};
    default:
        return {0b00000, 0b00000, 0b00000, 0b01110, 0b00000, 0b00000, 0b00000};
    }
}

int pixel_text_width(const char *text, int scale)
{
    if (text == nullptr || text[0] == '\0') {
        return 0;
    }

    int units = 0;
    for (size_t i = 0; text[i] != '\0'; ++i) {
        units += text[i] == ' ' ? 3 : 5;
        if (text[i + 1] != '\0') {
            units += 1;
        }
    }
    return units * scale;
}

void draw_pixel_text(lv_layer_t *layer, int x, int y, const char *text,
                     int scale, lv_color_t color)
{
    if (scale <= 0 || text == nullptr) {
        return;
    }

    int cursor_x = x;
    for (size_t index = 0; text[index] != '\0'; ++index) {
        if (text[index] == ' ') {
            cursor_x += 4 * scale;
            continue;
        }

        const std::array<uint8_t, 7> rows = glyph_for(text[index]);
        for (int row = 0; row < 7; ++row) {
            for (int col = 0; col < 5; ++col) {
                if ((rows[row] & (1 << (4 - col))) != 0) {
                    canvas_rect(layer,
                                cursor_x + col * scale,
                                y + row * scale,
                                scale,
                                scale,
                                color);
                }
            }
        }
        cursor_x += 6 * scale;
    }
}

void draw_pixel_text_direct(int x, int y, const char *text, int scale,
                            lv_color_t color)
{
    if (scale <= 0 || text == nullptr) {
        return;
    }

    int cursor_x = x;
    for (size_t index = 0; text[index] != '\0'; ++index) {
        if (text[index] == ' ') {
            cursor_x += 4 * scale;
            continue;
        }

        const std::array<uint8_t, 7> rows = glyph_for(text[index]);
        for (int row = 0; row < 7; ++row) {
            for (int col = 0; col < 5; ++col) {
                if ((rows[row] & (1 << (4 - col))) != 0) {
                    canvas_rect_direct(cursor_x + col * scale,
                                       y + row * scale,
                                       scale,
                                       scale,
                                       color);
                }
            }
        }
        cursor_x += 6 * scale;
    }
}

uint32_t blend_channel(uint32_t a, uint32_t b, int amount, int maximum)
{
    return (a * static_cast<uint32_t>(maximum - amount) +
            b * static_cast<uint32_t>(amount)) /
           static_cast<uint32_t>(maximum);
}

lv_color_t blend_hex(uint32_t from, uint32_t to, int amount, int maximum)
{
    amount = std::clamp(amount, 0, maximum);
    const uint32_t r = blend_channel((from >> 16) & 0xff,
                                     (to >> 16) & 0xff,
                                     amount,
                                     maximum);
    const uint32_t g = blend_channel((from >> 8) & 0xff,
                                     (to >> 8) & 0xff,
                                     amount,
                                     maximum);
    const uint32_t b = blend_channel(from & 0xff, to & 0xff, amount, maximum);
    return lv_color_hex((r << 16) | (g << 8) | b);
}

void format_runner_full_name(const RunnerPreview &preview, char *buffer,
                             size_t buffer_size)
{
    std::snprintf(buffer,
                  buffer_size,
                  "%s %s",
                  preview.first_name,
                  preview.last_name);
}

void hide_runner_labels()
{
    if (g_runner_name_label != nullptr) {
        lv_obj_add_flag(g_runner_name_label, LV_OBJ_FLAG_HIDDEN);
    }
    if (g_runner_dog_label != nullptr) {
        lv_obj_add_flag(g_runner_dog_label, LV_OBJ_FLAG_HIDDEN);
    }
}

void initialize_runner_label(lv_obj_t *label)
{
    lv_obj_remove_style_all(label);
    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(label, 0, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_opa(label, LV_OPA_COVER, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_anim_duration(label,
                                   lv_anim_speed_clamped(
                                       RUNNER_LABEL_SCROLL_SPEED, 300, 10000),
                                   LV_PART_MAIN);
    lv_obj_remove_flag(label, LV_OBJ_FLAG_SCROLLABLE);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
}

void configure_runner_label(lv_obj_t *label, int row_y, const char *text,
                            const lv_font_t *font)
{
    if (label == nullptr) {
        return;
    }

    constexpr int left_padding = 4;
    const int row_height = DISPLAY_HEIGHT / 2;
    const int label_width =
        std::max(1, SINGLE_PANEL_WIDTH * 2 - left_padding * 2 -
                        RUNNER_TIME_OVERLAP);
    const int text_height = static_cast<int>(measure_text("Mg", font).y);
    const int label_y = row_y + std::max(0, (row_height - text_height) / 2);

    lv_obj_set_pos(label, left_padding, label_y);
    lv_obj_set_size(label, label_width, text_height);
    lv_obj_set_style_text_font(label, font, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_label_set_text(label, text);
    lv_obj_remove_flag(label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(label);
}

void show_runner_labels(const RunnerPreview &preview)
{
    char full_name[64]{};
    format_runner_full_name(preview, full_name, sizeof(full_name));

    const lv_font_t *runner_font = font_for_row_height(DISPLAY_HEIGHT / 2);
    configure_runner_label(g_runner_name_label, 0, full_name, runner_font);
    configure_runner_label(g_runner_dog_label,
                           DISPLAY_HEIGHT / 2,
                           preview.dog_name,
                           runner_font);
}

void draw_runner_preview(lv_layer_t *layer, const RunnerPreview &preview)
{
    constexpr int split_x = SINGLE_PANEL_WIDTH * 2;
    constexpr int right_padding = 1;
    constexpr int stat_gap = 2;
    const lv_color_t white = lv_color_hex(0xffffff);

    char faults_text[8]{};
    char refusals_text[8]{};
    char time_text[16]{};
    std::snprintf(time_text, sizeof(time_text), "%s", preview.time_text);
    const size_t time_length = std::strlen(time_text);
    if (time_length > 0 &&
        (time_text[time_length - 1] == 's' || time_text[time_length - 1] == 'S')) {
        time_text[time_length - 1] = '\0';
    }
    std::snprintf(faults_text, sizeof(faults_text), "F:%d", preview.faults);
    std::snprintf(refusals_text, sizeof(refusals_text), "V:%d", preview.refusals);

    const int stat_scale = 2;
    const int stat_line_gap = 2;
    const int stat_width =
        std::max(pixel_text_width(faults_text, stat_scale),
                 pixel_text_width(refusals_text, stat_scale));
    const int right_width = std::max(1, DISPLAY_WIDTH - split_x +
                                            RUNNER_TIME_OVERLAP);
    const int time_available =
        right_width - right_padding * 2 - stat_gap - stat_width;
    int time_scale = DISPLAY_WIDTH >= 240 ? 3 : 2;
    while (time_scale > 1 &&
           pixel_text_width(time_text, time_scale) > time_available) {
        --time_scale;
    }

    const int time_width = pixel_text_width(time_text, time_scale);
    const int time_x = split_x + right_padding - 1 - RUNNER_TIME_OVERLAP;
    const int time_y = (DISPLAY_HEIGHT - 7 * time_scale) / 2;
    draw_pixel_text(layer,
                    time_x,
                    time_y,
                    time_text,
                    time_scale,
                    white);

    const int stats_x = time_x + time_width + stat_gap + 5 +
                        RUNNER_TIME_OVERLAP;
    const int stat_height = 7 * stat_scale;
    const int stats_group_height = stat_height * 2 + stat_line_gap;
    const int stats_y = (DISPLAY_HEIGHT - stats_group_height) / 2;
    draw_pixel_text(layer, stats_x, stats_y, faults_text, stat_scale, white);
    draw_pixel_text(layer,
                    stats_x,
                    stats_y + stat_height + stat_line_gap,
                    refusals_text,
                    stat_scale,
                    white);
}

RgbPixel make_rgb(uint32_t color)
{
    return RgbPixel{
        .r = static_cast<uint8_t>((color >> 16) & 0xff),
        .g = static_cast<uint8_t>((color >> 8) & 0xff),
        .b = static_cast<uint8_t>(color & 0xff),
    };
}

uint32_t rgb_to_hex(const RgbPixel &pixel)
{
    return (static_cast<uint32_t>(pixel.r) << 16) |
           (static_cast<uint32_t>(pixel.g) << 8) |
           static_cast<uint32_t>(pixel.b);
}

uint8_t scale_channel(uint8_t value, int scale, int maximum)
{
    return static_cast<uint8_t>((static_cast<int>(value) * scale) / maximum);
}

RgbPixel scale_rgb(const RgbPixel &pixel, int scale, int maximum)
{
    return RgbPixel{
        .r = scale_channel(pixel.r, scale, maximum),
        .g = scale_channel(pixel.g, scale, maximum),
        .b = scale_channel(pixel.b, scale, maximum),
    };
}

uint8_t blend_channel_u8(uint8_t from, uint8_t to, int amount, int maximum)
{
    return static_cast<uint8_t>(
        (static_cast<int>(from) * (maximum - amount) +
         static_cast<int>(to) * amount) /
        maximum);
}

RgbPixel blend_rgb(const RgbPixel &from,
                   const RgbPixel &to,
                   int amount,
                   int maximum)
{
    amount = std::clamp(amount, 0, maximum);
    return RgbPixel{
        .r = blend_channel_u8(from.r, to.r, amount, maximum),
        .g = blend_channel_u8(from.g, to.g, amount, maximum),
        .b = blend_channel_u8(from.b, to.b, amount, maximum),
    };
}

uint8_t saturating_add(uint8_t left, uint8_t right)
{
    const int sum = static_cast<int>(left) + static_cast<int>(right);
    return static_cast<uint8_t>(std::min(sum, 255));
}

size_t frizzles_index(int x, int y)
{
    return static_cast<size_t>(y * DISPLAY_WIDTH + x);
}

void reset_frizzles_background()
{
    std::fill(g_frizzles_buffer.begin(), g_frizzles_buffer.end(), RgbPixel{});
    std::fill(g_frizzles_blur_buffer.begin(),
              g_frizzles_blur_buffer.end(),
              RgbPixel{});
}

int beat_sin_coord(int64_t elapsed_ms,
                   float beats_per_minute,
                   int minimum,
                   int maximum,
                   float phase)
{
    if (maximum <= minimum) {
        return minimum;
    }

    const float angle =
        (static_cast<float>(elapsed_ms) * beats_per_minute / 60000.0f) *
            TWO_PI +
        phase;
    const float normalized = (std::sin(angle) + 1.0f) * 0.5f;
    return minimum + static_cast<int>(normalized * (maximum - minimum) + 0.5f);
}

uint8_t beat_sin8(int64_t elapsed_ms, float beats_per_minute, float phase)
{
    return static_cast<uint8_t>(
        beat_sin_coord(elapsed_ms, beats_per_minute, 0, 255, phase));
}

RgbPixel frizzles_palette(uint8_t index)
{
    if (index < 85) {
        return blend_rgb(make_rgb(0x083dff),
                         make_rgb(0x00d8ff),
                         index,
                         84);
    }
    if (index < 170) {
        return blend_rgb(make_rgb(0x00d8ff),
                         make_rgb(0xeaf7ff),
                         index - 85,
                         84);
    }
    return blend_rgb(make_rgb(0xeaf7ff),
                     make_rgb(0x1064ff),
                     index - 170,
                     85);
}

RgbPixel red_tinted_frizzle(const RgbPixel &pixel, int red_mix)
{
    const uint8_t brightness = std::max(pixel.r, std::max(pixel.g, pixel.b));
    const RgbPixel red_target{
        .r = brightness,
        .g = static_cast<uint8_t>(brightness / 12),
        .b = static_cast<uint8_t>(brightness / 18),
    };
    return blend_rgb(pixel, red_target, red_mix, 1000);
}

void fade_frizzles_background()
{
    constexpr int fade_by = 22;
    constexpr int keep = 255 - fade_by;
    for (RgbPixel &pixel : g_frizzles_buffer) {
        pixel.r = scale_channel(pixel.r, keep, 255);
        pixel.g = scale_channel(pixel.g, keep, 255);
        pixel.b = scale_channel(pixel.b, keep, 255);
    }
}

void add_frizzles_pixel(int x, int y, const RgbPixel &color, int strength)
{
    if (x < 0 || x >= DISPLAY_WIDTH || y < 0 || y >= FRIZZLES_HEIGHT ||
        strength <= 0) {
        return;
    }

    RgbPixel &pixel = g_frizzles_buffer[frizzles_index(x, y)];
    const RgbPixel scaled = scale_rgb(color, std::min(strength, 255), 255);
    pixel.r = saturating_add(pixel.r, scaled.r);
    pixel.g = saturating_add(pixel.g, scaled.g);
    pixel.b = saturating_add(pixel.b, scaled.b);
}

void add_frizzles_spark(int x, int y, const RgbPixel &color)
{
    add_frizzles_pixel(x, y, color, 255);
    add_frizzles_pixel(x - 1, y, color, 92);
    add_frizzles_pixel(x + 1, y, color, 92);
    add_frizzles_pixel(x, y - 1, color, 72);
    add_frizzles_pixel(x, y + 1, color, 72);
}

void blur_frizzles_background()
{
    constexpr int blur_amount = 86;
    for (int y = 0; y < FRIZZLES_HEIGHT; ++y) {
        for (int x = 0; x < DISPLAY_WIDTH; ++x) {
            const RgbPixel center = g_frizzles_buffer[frizzles_index(x, y)];
            int red_sum = center.r;
            int green_sum = center.g;
            int blue_sum = center.b;
            int samples = 1;

            if (x > 0) {
                const RgbPixel neighbor = g_frizzles_buffer[frizzles_index(x - 1, y)];
                red_sum += neighbor.r;
                green_sum += neighbor.g;
                blue_sum += neighbor.b;
                ++samples;
            }
            if (x + 1 < DISPLAY_WIDTH) {
                const RgbPixel neighbor = g_frizzles_buffer[frizzles_index(x + 1, y)];
                red_sum += neighbor.r;
                green_sum += neighbor.g;
                blue_sum += neighbor.b;
                ++samples;
            }
            if (y > 0) {
                const RgbPixel neighbor = g_frizzles_buffer[frizzles_index(x, y - 1)];
                red_sum += neighbor.r;
                green_sum += neighbor.g;
                blue_sum += neighbor.b;
                ++samples;
            }
            if (y + 1 < FRIZZLES_HEIGHT) {
                const RgbPixel neighbor = g_frizzles_buffer[frizzles_index(x, y + 1)];
                red_sum += neighbor.r;
                green_sum += neighbor.g;
                blue_sum += neighbor.b;
                ++samples;
            }

            const RgbPixel average{
                .r = static_cast<uint8_t>(red_sum / samples),
                .g = static_cast<uint8_t>(green_sum / samples),
                .b = static_cast<uint8_t>(blue_sum / samples),
            };
            g_frizzles_blur_buffer[frizzles_index(x, y)] =
                blend_rgb(center, average, blur_amount, 255);
        }
    }

    g_frizzles_buffer = g_frizzles_blur_buffer;
}

void update_frizzles_background(int64_t elapsed_ms)
{
    if (g_frizzles_reset_requested.exchange(false, std::memory_order_acq_rel)) {
        reset_frizzles_background();
    }

    fade_frizzles_background();

    for (int i = 8; i > 0; --i) {
        const float phase = static_cast<float>(i) * 0.83f;
        const int x = beat_sin_coord(elapsed_ms,
                                     24.0f + static_cast<float>(i),
                                     0,
                                     DISPLAY_WIDTH - 1,
                                     phase);
        const int y = beat_sin_coord(elapsed_ms,
                                     18.0f + static_cast<float>(8 - i),
                                     0,
                                     FRIZZLES_HEIGHT - 1,
                                     phase * 1.37f);
        const uint8_t palette_index =
            beat_sin8(elapsed_ms, 12.0f, phase * 0.7f);
        add_frizzles_spark(x, y, frizzles_palette(palette_index));
    }

    blur_frizzles_background();
}

void draw_frizzles_background(int64_t elapsed_ms, int red_mix)
{
    update_frizzles_background(elapsed_ms);

    for (int y = 0; y < FRIZZLES_HEIGHT; ++y) {
        for (int x = 0; x < DISPLAY_WIDTH; ++x) {
            RgbPixel pixel = g_frizzles_buffer[frizzles_index(x, y)];
            if (red_mix > 0) {
                pixel = red_tinted_frizzle(pixel, red_mix);
            }
            if (pixel.r + pixel.g + pixel.b < 10) {
                continue;
            }
            canvas_pixel_direct(x, y, pixel);
        }
    }
}

void draw_status_message(lv_layer_t *layer,
                         const char *text,
                         uint32_t line_color,
                         uint32_t text_color)
{
    const lv_font_t *font = status_font_for_text(text);
    const lv_point_t text_size = measure_text(text, font);
    const int x = (DISPLAY_WIDTH - text_size.x) / 2;
    const int y = (DISPLAY_HEIGHT - text_size.y) / 2;

    canvas_rect(layer, 0, 0, DISPLAY_WIDTH, 1, lv_color_hex(line_color));
    canvas_rect(layer,
                0,
                DISPLAY_HEIGHT - 1,
                DISPLAY_WIDTH,
                1,
                lv_color_hex(line_color));
    draw_canvas_label(layer, x, y, text, font, lv_color_hex(text_color));
}

void render_parcours_intro(lv_layer_t *layer, int64_t elapsed_ms)
{
    (void)elapsed_ms;
    draw_status_message(layer, START_TEXT, START_BLUE, START_BLUE_LIGHT);
}

int ease_out_cubic_per_mille(int progress)
{
    progress = std::clamp(progress, 0, 1000);
    const int64_t inverse = 1000 - progress;
    return static_cast<int>(1000 - (inverse * inverse * inverse) / 1000000);
}

void format_timer_text(char *buffer, size_t buffer_size, int remaining_seconds)
{
    const int minutes = remaining_seconds / 60;
    const int seconds = remaining_seconds % 60;
    std::snprintf(buffer, buffer_size, "%d:%02d", minutes, seconds);
}

void render_parcours_start_whoosh(lv_layer_t *layer, int64_t elapsed_ms)
{
    const int progress =
        static_cast<int>(std::clamp<int64_t>(
            elapsed_ms * 1000 / START_WHOOSH_DURATION_MS, 0, 1000));
    const int eased = ease_out_cubic_per_mille(progress);

    const lv_font_t *font = status_font_for_text(START_TEXT);
    const lv_point_t source_size = measure_text(START_TEXT, font);
    const int source_x = (DISPLAY_WIDTH - source_size.x) / 2;
    const int source_y = (DISPLAY_HEIGHT - source_size.y) / 2;
    const int source_mid_y = source_y + source_size.y / 2;

    char timer_text[8]{};
    format_timer_text(timer_text, sizeof(timer_text), TIMER_SECONDS);
    const int scale = DISPLAY_WIDTH >= 240 ? 3 : 2;
    const int counter_width = pixel_text_width(timer_text, scale);
    const int target_x = (DISPLAY_WIDTH - counter_width) / 2;
    const int target_y = 5;
    const int target_mid_y = target_y + (7 * scale) / 2;

    const int text_fade = std::clamp(1000 - std::max(0, progress - 120) * 2, 0, 1000);
    if (text_fade > 0) {
        draw_canvas_label(layer,
                          source_x,
                          source_y,
                          START_TEXT,
                          font,
                          blend_hex(0x061426, START_BLUE_LIGHT, text_fade, 1000));
    }

    const int wipe_width = 54;
    const int wipe_x =
        -wipe_width + ((DISPLAY_WIDTH + wipe_width * 2) * eased) / 1000;
    const int sweep_y =
        source_mid_y + ((target_mid_y - source_mid_y) * eased) / 1000;
    for (int streak = 0; streak < 6; ++streak) {
        const int tail = wipe_x - 48 - streak * 7;
        const int head = wipe_x - streak * 3;
        const int y = sweep_y + streak - 3;
        const lv_color_t color =
            blend_hex(START_BLUE_LIGHT, START_BLUE, streak * 115, 1000);
        canvas_rect(layer,
                    tail,
                    y,
                    head - tail,
                    streak == 2 ? 2 : 1,
                    color,
                    static_cast<lv_opa_t>(std::clamp(230 - streak * 28, 55, 230)));
    }
    canvas_rect(layer,
                wipe_x,
                sweep_y - 4,
                3,
                9,
                lv_color_hex(0xbff8ff),
                static_cast<lv_opa_t>(190));

    const int counter_progress = std::clamp((progress - 520) * 1000 / 480, 0, 1000);
    if (counter_progress > 0) {
        draw_pixel_text(layer,
                        target_x,
                        target_y,
                        timer_text,
                        scale,
                        blend_hex(START_BLUE, 0xffffff, counter_progress, 1000));
        canvas_rect(layer,
                    target_x,
                    target_y + 7 * scale + 1,
                    (counter_width * counter_progress) / 1000,
                    1,
                    lv_color_hex(START_BLUE_LIGHT),
                    static_cast<lv_opa_t>(160));
    }
}

void draw_timer_background(int64_t elapsed_ms)
{
    draw_frizzles_background(elapsed_ms, 0);
}

void render_parcours_timer(int64_t elapsed_ms)
{
    draw_timer_background(elapsed_ms);

    const int elapsed_seconds =
        static_cast<int>(std::clamp<int64_t>(elapsed_ms / 1000, 0, TIMER_SECONDS));
    const int remaining_seconds = std::max(0, TIMER_SECONDS - elapsed_seconds);
    char timer_text[8]{};
    format_timer_text(timer_text, sizeof(timer_text), remaining_seconds);

    const int scale = DISPLAY_WIDTH >= 240 ? 3 : 2;
    const int text_width = pixel_text_width(timer_text, scale);
    const int x = (DISPLAY_WIDTH - text_width) / 2;
    const int y = 5;
    canvas_rect_direct(x - 5, y - 2, text_width + 10, 25, lv_color_hex(0x020407));
    draw_pixel_text_direct(x, y, timer_text, scale, lv_color_hex(0xffffff));

    const int bar_y = DISPLAY_HEIGHT - PROGRESS_BAR_HEIGHT;
    canvas_rect_direct(0,
                       bar_y,
                       DISPLAY_WIDTH,
                       PROGRESS_BAR_HEIGHT,
                       lv_color_hex(0x061426));
    const int progress_width =
        TIMER_SECONDS <= 0
            ? DISPLAY_WIDTH
            : (DISPLAY_WIDTH * elapsed_seconds) / TIMER_SECONDS;
    canvas_rect_direct(0,
                       bar_y,
                       progress_width,
                       PROGRESS_BAR_HEIGHT,
                       lv_color_hex(0x1064ff));
    if (progress_width > 2 && progress_width < DISPLAY_WIDTH) {
        canvas_rect_direct(progress_width - 2,
                           bar_y,
                           2,
                           PROGRESS_BAR_HEIGHT,
                           lv_color_hex(0x6ed6ff));
    }
}

void render_parcours_whoosh(int64_t elapsed_ms)
{
    const int progress =
        static_cast<int>(std::clamp<int64_t>(
            elapsed_ms * 1000 / WHOOSH_DURATION_MS, 0, 1000));
    const int eased = ease_out_cubic_per_mille(progress);

    draw_frizzles_background(TIMER_SECONDS * 1000LL + elapsed_ms, progress);

    constexpr char done_time[] = "0:00";
    const int scale = DISPLAY_WIDTH >= 240 ? 3 : 2;
    const int text_width = pixel_text_width(done_time, scale);
    const int start_x = (DISPLAY_WIDTH - text_width) / 2;
    const int end_x = DISPLAY_WIDTH + 12;
    const int text_x = start_x + ((end_x - start_x) * eased) / 1000;
    const int text_y = 5;

    const int bar_y = DISPLAY_HEIGHT - PROGRESS_BAR_HEIGHT;
    canvas_rect_direct(0,
                       bar_y,
                       DISPLAY_WIDTH,
                       PROGRESS_BAR_HEIGHT,
                       blend_hex(0x1064ff, 0xff1515, progress, 1000));

    const int streak_head = text_x + text_width / 2;
    for (int i = 0; i < 8; ++i) {
        const int tail_strength = 1000 - i * 105;
        const int streak_width = 18 + i * 15 + eased / 18;
        const int streak_y = 7 + (i % 4) * 4;
        const lv_color_t color =
            blend_hex(0x1064ff, 0xff1515, std::min(1000, progress + i * 70), 1000);
        canvas_rect_direct(streak_head - streak_width,
                           streak_y,
                           streak_width,
                           2,
                           color,
                           static_cast<lv_opa_t>(
                               std::clamp(tail_strength / 5, 32, 210)));
    }

    for (int trail = 5; trail > 0; --trail) {
        const int trail_x = text_x - trail * (5 + eased / 180);
        const int brightness = std::max(130, 680 - trail * 90);
        const RgbPixel trail_color =
            scale_rgb(make_rgb(0xff1515), brightness, 1000);
        draw_pixel_text_direct(trail_x,
                               text_y,
                               done_time,
                               scale,
                               lv_color_hex(rgb_to_hex(trail_color)));
    }

    draw_pixel_text_direct(text_x,
                           text_y,
                           done_time,
                           scale,
                           blend_hex(0xffffff, 0xff3030, progress, 1000));
}

void render_parcours_ended(lv_layer_t *layer)
{
    draw_status_message(layer, ENDED_TEXT, END_RED, END_RED_LIGHT);
}

void render_canvas(void (*draw)(lv_layer_t *))
{
    if (!lvgl_port_lock(1000)) {
        ESP_LOGW(TAG, "Timed out waiting for LVGL lock");
        return;
    }

    hide_runner_labels();
    lv_canvas_fill_bg(g_canvas, lv_color_hex(0x000000), LV_OPA_COVER);

    lv_layer_t layer;
    lv_canvas_init_layer(g_canvas, &layer);
    draw(&layer);
    lv_canvas_finish_layer(g_canvas, &layer);

    lv_obj_invalidate(g_canvas);
    lv_refr_now(g_display);
    lvgl_port_unlock();
}

void render_runner_preview_screen()
{
    if (!lvgl_port_lock(1000)) {
        ESP_LOGW(TAG, "Timed out waiting for LVGL lock");
        return;
    }

    lv_canvas_fill_bg(g_canvas, lv_color_hex(0x000000), LV_OPA_COVER);

    lv_layer_t layer;
    lv_canvas_init_layer(g_canvas, &layer);
    draw_runner_preview(&layer, STARTUP_PREVIEW);
    lv_canvas_finish_layer(g_canvas, &layer);

    show_runner_labels(STARTUP_PREVIEW);
    lv_obj_invalidate(g_canvas);
    lv_refr_now(g_display);
    lvgl_port_unlock();
}

void render_intro_screen(int64_t elapsed_ms)
{
    if (!lvgl_port_lock(1000)) {
        ESP_LOGW(TAG, "Timed out waiting for LVGL lock");
        return;
    }

    hide_runner_labels();
    lv_canvas_fill_bg(g_canvas, lv_color_hex(0x000000), LV_OPA_COVER);
    lv_layer_t layer;
    lv_canvas_init_layer(g_canvas, &layer);
    render_parcours_intro(&layer, elapsed_ms);
    lv_canvas_finish_layer(g_canvas, &layer);
    lv_obj_invalidate(g_canvas);
    lv_refr_now(g_display);
    lvgl_port_unlock();
}

void render_start_whoosh_screen(int64_t elapsed_ms)
{
    if (!lvgl_port_lock(1000)) {
        ESP_LOGW(TAG, "Timed out waiting for LVGL lock");
        return;
    }

    hide_runner_labels();
    lv_canvas_fill_bg(g_canvas, lv_color_hex(0x000000), LV_OPA_COVER);
    lv_layer_t layer;
    lv_canvas_init_layer(g_canvas, &layer);
    render_parcours_start_whoosh(&layer, elapsed_ms);
    lv_canvas_finish_layer(g_canvas, &layer);
    lv_obj_invalidate(g_canvas);
    lv_refr_now(g_display);
    lvgl_port_unlock();
}

void render_timer_screen(int64_t elapsed_ms)
{
    if (!lvgl_port_lock(1000)) {
        ESP_LOGW(TAG, "Timed out waiting for LVGL lock");
        return;
    }

    hide_runner_labels();
    clear_canvas_buffer();
    render_parcours_timer(elapsed_ms);
    present_canvas_buffer();
    lvgl_port_unlock();
}

void render_whoosh_screen(int64_t elapsed_ms)
{
    if (!lvgl_port_lock(1000)) {
        ESP_LOGW(TAG, "Timed out waiting for LVGL lock");
        return;
    }

    hide_runner_labels();
    clear_canvas_buffer();
    render_parcours_whoosh(elapsed_ms);
    present_canvas_buffer();
    lvgl_port_unlock();
}

void render_ended_screen()
{
    render_canvas([](lv_layer_t *layer) {
        render_parcours_ended(layer);
    });
}

void hub75_flush_callback(lv_display_t *display,
                          const lv_area_t *area,
                          uint8_t *pixel_map)
{
    const auto width = static_cast<uint16_t>(area->x2 - area->x1 + 1);
    const auto height = static_cast<uint16_t>(area->y2 - area->y1 + 1);

    g_hub75->draw_pixels(static_cast<uint16_t>(area->x1),
                         static_cast<uint16_t>(area->y1),
                         width,
                         height,
                         pixel_map,
                         Hub75PixelFormat::RGB565,
                         Hub75ColorOrder::RGB,
                         false);

    if (lv_display_flush_is_last(display)) {
        g_hub75->flip_buffer();
    }

    lv_display_flush_ready(display);
}

void initialize_hub75()
{
    Hub75Config config{};
    config.panel_width = SINGLE_PANEL_WIDTH;
    config.panel_height = SINGLE_PANEL_HEIGHT;
    config.scan_wiring = Hub75ScanWiring::STANDARD_TWO_SCAN;
    config.shift_driver = Hub75ShiftDriver::GENERIC;
    config.layout_rows = MATRIX_CHAIN_HEIGHT;
    config.layout_cols = MATRIX_CHAIN_WIDTH;
    config.layout = Hub75PanelLayout::HORIZONTAL;
    config.rotation = Hub75Rotation::ROTATE_0;
    config.pins = HUB75_PINS;
    config.output_clock_speed = Hub75ClockSpeed::HZ_20M;
    config.min_refresh_rate = 80;
    config.double_buffer = true;
    config.brightness = CONFIG_TIMEPANEL_PANEL_BRIGHTNESS;

    g_hub75 = std::make_unique<Hub75Driver>(config);
    if (!g_hub75->begin()) {
        ESP_LOGE(TAG, "HUB75 initialization failed");
        abort();
    }
    g_hub75->clear();
    g_hub75->flip_buffer();
}

void initialize_lvgl()
{
    const lvgl_port_cfg_t port_config = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&port_config));

    lvgl_port_lock(0);

    g_display = lv_display_create(DISPLAY_WIDTH, DISPLAY_HEIGHT);
    lv_display_set_color_format(g_display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(g_display,
                           g_lvgl_draw_buffer.data(),
                           nullptr,
                           g_lvgl_draw_buffer.size(),
                           LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(g_display, hub75_flush_callback);

    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    g_canvas = lv_canvas_create(screen);
    lv_canvas_set_buffer(g_canvas,
                         g_canvas_buffer.data(),
                         DISPLAY_WIDTH,
                         DISPLAY_HEIGHT,
                         LV_COLOR_FORMAT_RGB565);
    lv_obj_remove_style_all(g_canvas);
    lv_obj_set_pos(g_canvas, 0, 0);
    lv_obj_set_size(g_canvas, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    lv_canvas_fill_bg(g_canvas, lv_color_hex(0x000000), LV_OPA_COVER);
    lv_obj_invalidate(g_canvas);

    g_runner_name_label = lv_label_create(screen);
    initialize_runner_label(g_runner_name_label);
    g_runner_dog_label = lv_label_create(screen);
    initialize_runner_label(g_runner_dog_label);

    lvgl_port_unlock();
}

constexpr uint8_t fht40_crc8_word(uint8_t msb, uint8_t lsb)
{
    uint8_t crc = 0xff;
    for (int byte_index = 0; byte_index < 2; ++byte_index) {
        crc ^= byte_index == 0 ? msb : lsb;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x80) != 0
                      ? static_cast<uint8_t>((crc << 1) ^ 0x31)
                      : static_cast<uint8_t>(crc << 1);
        }
    }
    return crc;
}

static_assert(fht40_crc8_word(0xbe, 0xef) == 0x92,
              "FHT40 CRC implementation does not match the datasheet");

int div_round_nearest(int64_t numerator, int64_t denominator)
{
    if (numerator >= 0) {
        return static_cast<int>((numerator + denominator / 2) / denominator);
    }
    return -static_cast<int>((-numerator + denominator / 2) / denominator);
}

int temperature_celsius_tenths_from_fht40_raw(uint16_t raw_temperature)
{
    return div_round_nearest(-450LL * FHT40_SIGNAL_MAX +
                                 1750LL * raw_temperature,
                             FHT40_SIGNAL_MAX);
}

int humidity_percent_from_fht40_raw(uint16_t raw_humidity)
{
    const int humidity = div_round_nearest(-6LL * FHT40_SIGNAL_MAX +
                                               125LL * raw_humidity,
                                           FHT40_SIGNAL_MAX);
    return std::clamp(humidity, 0, 100);
}

bool fht40_word_crc_ok(const std::array<uint8_t, 6> &data, size_t offset)
{
    return fht40_crc8_word(data[offset], data[offset + 1]) == data[offset + 2];
}

struct SensorMutexLock {
    SensorMutexLock()
    {
        if (g_sensor_mutex != nullptr) {
            locked = xSemaphoreTake(g_sensor_mutex, portMAX_DELAY) == pdTRUE;
        }
    }

    ~SensorMutexLock()
    {
        if (locked) {
            xSemaphoreGive(g_sensor_mutex);
        }
    }

    bool locked = false;
};

void initialize_environment_sensor()
{
#if CONFIG_TIMEPANEL_SENSOR_ENABLED
    if (g_sensor_mutex == nullptr) {
        g_sensor_mutex = xSemaphoreCreateMutex();
        if (g_sensor_mutex == nullptr) {
            ESP_LOGW(TAG, "FHT40 I2C mutex allocation failed");
            return;
        }
    }

    const i2c_master_bus_config_t bus_config{
        .i2c_port = static_cast<i2c_port_num_t>(CONFIG_TIMEPANEL_SENSOR_I2C_PORT),
        .sda_io_num = static_cast<gpio_num_t>(CONFIG_TIMEPANEL_SENSOR_I2C_SDA_GPIO),
        .scl_io_num = static_cast<gpio_num_t>(CONFIG_TIMEPANEL_SENSOR_I2C_SCL_GPIO),
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags = {
            .enable_internal_pullup = SENSOR_I2C_INTERNAL_PULLUPS,
            .allow_pd = false,
        },
    };

    esp_err_t error = i2c_new_master_bus(&bus_config, &g_sensor_i2c_bus);
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "FHT40 I2C bus initialization failed: %s",
                 esp_err_to_name(error));
        return;
    }

    const i2c_device_config_t device_config{
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = FHT40_I2C_ADDRESS,
        .scl_speed_hz = CONFIG_TIMEPANEL_SENSOR_I2C_FREQUENCY_HZ,
        .scl_wait_us = 0,
        .flags = {
            .disable_ack_check = false,
        },
    };

    error = i2c_master_bus_add_device(g_sensor_i2c_bus,
                                      &device_config,
                                      &g_fht40);
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "FHT40 I2C device setup failed: %s",
                 esp_err_to_name(error));
        g_fht40 = nullptr;
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(FHT40_POWER_UP_DELAY_MS));

    error = i2c_master_probe(g_sensor_i2c_bus,
                             FHT40_I2C_ADDRESS,
                             FHT40_I2C_TIMEOUT_MS);
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "FHT40 not detected at I2C address 0x%02x: %s",
                 FHT40_I2C_ADDRESS,
                 esp_err_to_name(error));
        g_fht40 = nullptr;
    }
#else
    ESP_LOGI(TAG, "FHT40 sensor support is disabled");
#endif
}

EnvironmentReading read_environment_sensor()
{
    EnvironmentReading reading{};

#if CONFIG_TIMEPANEL_SENSOR_ENABLED
    if (g_fht40 == nullptr) {
        return reading;
    }

    SensorMutexLock lock;
    if (!lock.locked) {
        ESP_LOGW(TAG, "FHT40 measurement skipped; I2C mutex unavailable");
        return reading;
    }

    const uint8_t command = FHT40_MEASURE_HIGH_REPEATABILITY;
    esp_err_t error = i2c_master_transmit(g_fht40,
                                          &command,
                                          sizeof(command),
                                          FHT40_I2C_TIMEOUT_MS);
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "FHT40 measurement command failed: %s",
                 esp_err_to_name(error));
        return reading;
    }

    vTaskDelay(pdMS_TO_TICKS(FHT40_MEASUREMENT_DELAY_MS));

    std::array<uint8_t, 6> data{};
    for (int attempt = 1; attempt <= FHT40_READ_ATTEMPTS; ++attempt) {
        error = i2c_master_receive(g_fht40,
                                   data.data(),
                                   data.size(),
                                   FHT40_I2C_TIMEOUT_MS);
        if (error == ESP_OK || attempt == FHT40_READ_ATTEMPTS) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(FHT40_READ_RETRY_DELAY_MS));
    }
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "FHT40 measurement read failed after %d attempts: %s",
                 FHT40_READ_ATTEMPTS,
                 esp_err_to_name(error));
        return reading;
    }

    if (!fht40_word_crc_ok(data, 0) || !fht40_word_crc_ok(data, 3)) {
        ESP_LOGW(TAG, "FHT40 measurement failed CRC check");
        return reading;
    }

    const uint16_t raw_temperature =
        static_cast<uint16_t>((data[0] << 8) | data[1]);
    const uint16_t raw_humidity =
        static_cast<uint16_t>((data[3] << 8) | data[4]);

    reading.temperature_c_tenths =
        temperature_celsius_tenths_from_fht40_raw(raw_temperature);
    reading.humidity_percent = humidity_percent_from_fht40_raw(raw_humidity);
    reading.valid = true;
#endif

    return reading;
}

void log_environment_temperature(const EnvironmentReading &reading)
{
    if (reading.valid) {
        const bool negative = reading.temperature_c_tenths < 0;
        const int magnitude = negative ? -reading.temperature_c_tenths
                                       : reading.temperature_c_tenths;
        ESP_LOGI(TAG,
                 "FHT40 temperature: %s%d.%d C, humidity: %d%%",
                 negative ? "-" : "",
                 magnitude / 10,
                 magnitude % 10,
                 reading.humidity_percent);
    } else {
        ESP_LOGW(TAG, "FHT40 temperature unavailable");
    }
}

void environment_sensor_log_task(void *)
{
#if CONFIG_TIMEPANEL_SENSOR_ENABLED
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(SENSOR_LOG_INTERVAL_MS));
        log_environment_temperature(read_environment_sensor());
    }
#endif
}

void start_environment_sensor_log_task()
{
#if CONFIG_TIMEPANEL_SENSOR_ENABLED
    if (g_fht40 == nullptr) {
        ESP_LOGW(TAG, "FHT40 temperature logging not started; sensor is not initialized");
        return;
    }

    const BaseType_t result = xTaskCreate(environment_sensor_log_task,
                                         "fht40_log",
                                         3072,
                                         nullptr,
                                         4,
                                         nullptr);
    if (result != pdPASS) {
        ESP_LOGW(TAG, "FHT40 temperature logging task could not be started");
    }
#endif
}

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
        case ScreenMode::RunnerPreview:
            if (last_static_mode != static_cast<int>(ScreenMode::RunnerPreview)) {
                render_runner_preview_screen();
                last_static_mode = static_cast<int>(ScreenMode::RunnerPreview);
            }
            vTaskDelay(pdMS_TO_TICKS(100));
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

} // namespace

extern "C" void app_main(void)
{
    g_screen_started_us.store(esp_timer_get_time(), std::memory_order_release);

    initialize_hub75();
    initialize_lvgl();
    initialize_environment_sensor();
    log_environment_temperature(read_environment_sensor());
    start_environment_sensor_log_task();

    xTaskCreate(ui_task, "timepanel_ui", 6144, nullptr, 5, nullptr);
    xTaskCreate(button_task, "mode_button", 2048, nullptr, 4, nullptr);

    ESP_LOGI(TAG,
             "Timepanel HUB75 started at %dx%d with brightness %d",
             DISPLAY_WIDTH,
             DISPLAY_HEIGHT,
             CONFIG_TIMEPANEL_PANEL_BRIGHTNESS);
}
