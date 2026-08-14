#include "timepanel_common.h"

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


void format_timer_text(char *buffer, size_t buffer_size, int remaining_seconds)
{
    remaining_seconds = std::max(0, remaining_seconds);
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

    char timer_text[16]{};
    const int duration_seconds = static_cast<int>(
        std::max<int64_t>(1, g_parcours_duration_ms.load(std::memory_order_acquire) / 1000));
    format_timer_text(timer_text, sizeof(timer_text), duration_seconds);
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

    const int64_t duration_ms =
        std::max<int64_t>(1000, g_parcours_duration_ms.load(std::memory_order_acquire));
    const int duration_seconds =
        static_cast<int>(std::max<int64_t>(1, duration_ms / 1000));
    const int elapsed_seconds =
        static_cast<int>(std::clamp<int64_t>(elapsed_ms / 1000, 0, duration_seconds));
    const int remaining_seconds = std::max(0, duration_seconds - elapsed_seconds);
    char timer_text[16]{};
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
        static_cast<int>((DISPLAY_WIDTH *
                          std::clamp<int64_t>(elapsed_ms, 0, duration_ms)) /
                         duration_ms);
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

    const int64_t duration_ms =
        std::max<int64_t>(1000, g_parcours_duration_ms.load(std::memory_order_acquire));
    draw_frizzles_background(duration_ms + elapsed_ms, progress);

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

