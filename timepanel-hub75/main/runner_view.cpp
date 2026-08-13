#include "timepanel_common.h"

void format_runner_full_name(const RunnerSnapshot &preview, char *buffer,
                             size_t buffer_size)
{
    std::snprintf(buffer,
                  buffer_size,
                  "%s %s",
                  preview.first_name.data(),
                  preview.last_name.data());
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

void reset_runner_label_cache(RunnerLabelCache &cache)
{
    cache = RunnerLabelCache{};
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

void configure_runner_label(lv_obj_t *label, RunnerLabelCache &cache,
                            int row_y, const char *text, const lv_font_t *font)
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

    const bool changed =
        !cache.valid || cache.font != font || cache.x != left_padding ||
        cache.y != label_y || cache.width != label_width ||
        cache.height != text_height ||
        std::strcmp(cache.text.data(), text) != 0;

    if (changed) {
        lv_obj_set_pos(label, left_padding, label_y);
        lv_obj_set_size(label, label_width, text_height);
        lv_obj_set_style_text_font(label, font, 0);
        lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
        lv_label_set_text(label, text);

        copy_text(cache.text, text);
        cache.font = font;
        cache.x = left_padding;
        cache.y = label_y;
        cache.width = label_width;
        cache.height = text_height;
        cache.valid = true;
    }

    lv_obj_remove_flag(label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(label);
}

void show_runner_labels(const RunnerSnapshot &preview)
{
    char full_name[RUNNER_FULL_NAME_TEXT_SIZE]{};
    format_runner_full_name(preview, full_name, sizeof(full_name));

    const lv_font_t *runner_font = font_for_row_height(DISPLAY_HEIGHT / 2);
    configure_runner_label(g_runner_name_label,
                           g_runner_name_label_cache,
                           0,
                           full_name,
                           runner_font);
    configure_runner_label(g_runner_dog_label,
                           g_runner_dog_label_cache,
                           DISPLAY_HEIGHT / 2,
                           preview.dog_name.data(),
                           runner_font);
}

RunnerLayout calculate_runner_layout(const RunnerSnapshot &preview,
                                     const char *faults_text,
                                     const char *refusals_text)
{
    RunnerLayout layout{};
    constexpr int split_x = SINGLE_PANEL_WIDTH * 2;
    constexpr int right_padding = 1;
    constexpr int stat_gap = 2;

    layout.stat_scale = 2;
    const int stat_line_gap = 2;
    const int stat_width =
        std::max(pixel_text_width(faults_text, layout.stat_scale),
                 pixel_text_width(refusals_text, layout.stat_scale));
    const int min_time_x = split_x + right_padding - 1 - RUNNER_TIME_OVERLAP;
    const int time_stat_gap = stat_gap + 5 + RUNNER_TIME_OVERLAP;
    layout.stats_x = DISPLAY_WIDTH - stat_width;
    const int time_right_x = layout.stats_x - time_stat_gap;
    const int time_available =
        std::max(1, time_right_x - min_time_x);
    layout.time_scale = DISPLAY_WIDTH >= 240 ? 3 : 2;
    while (layout.time_scale > 1 &&
           pixel_text_width(preview.time_text.data(), layout.time_scale) >
               time_available) {
        --layout.time_scale;
    }

    layout.time_width =
        pixel_text_width(preview.time_text.data(), layout.time_scale);
    layout.time_x = time_right_x - layout.time_width;
    layout.time_y = (DISPLAY_HEIGHT - 7 * layout.time_scale) / 2;
    layout.stat_height = 7 * layout.stat_scale;
    const int stats_group_height = layout.stat_height * 2 + stat_line_gap;
    layout.fault_y = (DISPLAY_HEIGHT - stats_group_height) / 2;
    layout.refusal_y = layout.fault_y + layout.stat_height + stat_line_gap;
    return layout;
}

void draw_runner_preview(lv_layer_t *layer, const RunnerSnapshot &preview)
{
    constexpr int split_x = SINGLE_PANEL_WIDTH * 2;

    if (preview.disqualified) {
        constexpr char dis_text[] = "DIS";
        const int dis_scale = DISPLAY_HEIGHT >= 32 ? 4 : 3;
        const int dis_width = pixel_text_width(dis_text, dis_scale);
        const int dis_x =
            split_x - RUNNER_TIME_OVERLAP +
            (DISPLAY_WIDTH - (split_x - RUNNER_TIME_OVERLAP) - dis_width) / 2;
        const int dis_y = (DISPLAY_HEIGHT - 7 * dis_scale) / 2;
        draw_pixel_text(layer,
                        dis_x,
                        dis_y,
                        dis_text,
                        dis_scale,
                        lv_color_hex(RUN_RED));
        return;
    }

    char faults_text[8]{};
    char refusals_text[8]{};
    std::snprintf(faults_text, sizeof(faults_text), "F:%d", preview.faults);
    std::snprintf(refusals_text, sizeof(refusals_text), "V:%d", preview.refusals);

    const RunnerLayout layout =
        calculate_runner_layout(preview, faults_text, refusals_text);
    const lv_color_t value_color = lv_color_hex(preview.value_color);
    draw_pixel_text(layer,
                    layout.time_x,
                    layout.time_y,
                    preview.time_text.data(),
                    layout.time_scale,
                    value_color);
    draw_pixel_text(layer,
                    layout.stats_x,
                    layout.fault_y,
                    faults_text,
                    layout.stat_scale,
                    value_color);
    draw_pixel_text(layer,
                    layout.stats_x,
                    layout.refusal_y,
                    refusals_text,
                    layout.stat_scale,
                    value_color);
}

int ease_out_cubic_per_mille(int progress);

void hide_runner_whoosh_bands()
{
    for (lv_obj_t *band : g_runner_whoosh_bands) {
        if (band != nullptr) {
            lv_obj_add_flag(band, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void initialize_runner_whoosh_band(lv_obj_t *band)
{
    lv_obj_remove_style_all(band);
    lv_obj_set_style_bg_opa(band, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(band, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(band, LV_OBJ_FLAG_HIDDEN);
}

uint32_t runner_whoosh_color(RunnerWhooshType type)
{
    switch (type) {
    case RunnerWhooshType::FullGreen:
        return RUN_GREEN;
    case RunnerWhooshType::FaultOrange:
    case RunnerWhooshType::RefusalOrange:
        return RUN_ORANGE;
    case RunnerWhooshType::FullRed:
        return RUN_RED;
    case RunnerWhooshType::FullWhite:
    case RunnerWhooshType::None:
    default:
        return RUN_WHITE;
    }
}

uint32_t runner_whoosh_glint_color(RunnerWhooshType type)
{
    switch (type) {
    case RunnerWhooshType::FullGreen:
        return 0xc8ffd8;
    case RunnerWhooshType::FaultOrange:
    case RunnerWhooshType::RefusalOrange:
        return 0xfff0a0;
    case RunnerWhooshType::FullRed:
        return 0xff9a9a;
    case RunnerWhooshType::FullWhite:
    case RunnerWhooshType::None:
    default:
        return 0xffffff;
    }
}

uint32_t runner_whoosh_shadow_color(RunnerWhooshType type)
{
    switch (type) {
    case RunnerWhooshType::FullGreen:
        return 0x063418;
    case RunnerWhooshType::FaultOrange:
    case RunnerWhooshType::RefusalOrange:
        return 0x3a1600;
    case RunnerWhooshType::FullRed:
        return 0x360000;
    case RunnerWhooshType::FullWhite:
    case RunnerWhooshType::None:
    default:
        return 0x283244;
    }
}

bool runner_whoosh_is_full(RunnerWhooshType type)
{
    return type == RunnerWhooshType::FullWhite ||
           type == RunnerWhooshType::FullGreen ||
           type == RunnerWhooshType::FullRed;
}

int runner_whoosh_progress(const RunnerWhoosh &whoosh, int64_t now_us)
{
    const int64_t elapsed_ms = (now_us - whoosh.started_us) / 1000;
    return static_cast<int>(std::clamp<int64_t>(
        elapsed_ms * 1000 / std::max(1, whoosh.duration_ms), 0, 1000));
}

void update_runner_full_whoosh_bands(const RunnerSnapshot &snapshot,
                                     int64_t now_us)
{
    if (!runner_whoosh_active(snapshot.whoosh, now_us) ||
        !runner_whoosh_is_full(snapshot.whoosh.type)) {
        hide_runner_whoosh_bands();
        return;
    }

    const int progress = runner_whoosh_progress(snapshot.whoosh, now_us);
    const int eased = ease_out_cubic_per_mille(progress);
    const uint32_t color = runner_whoosh_color(snapshot.whoosh.type);
    const uint32_t glint = runner_whoosh_glint_color(snapshot.whoosh.type);
    const uint32_t shadow = runner_whoosh_shadow_color(snapshot.whoosh.type);
    const lv_opa_t fade_opacity =
        static_cast<lv_opa_t>(progress < 860
                                  ? 255
                                  : std::max(0, (1000 - progress) * 255 / 140));
    constexpr int head_width = 16;
    const int head_x =
        -head_width + ((DISPLAY_WIDTH + head_width * 2) * eased) / 1000;

    for (size_t index = 0; index < g_runner_whoosh_bands.size(); ++index) {
        lv_obj_t *band = g_runner_whoosh_bands[index];
        if (band == nullptr) {
            continue;
        }

        int x = head_x;
        int y = 0;
        int width = 1;
        int height = DISPLAY_HEIGHT;
        uint32_t band_color = color;
        int opacity = 180;

        if (index == 0) {
            x = head_x - 2;
            width = 3;
            band_color = glint;
            opacity = 245;
        } else if (index == 1) {
            x = head_x - 11;
            width = 9;
            band_color = color;
            opacity = 210;
        } else if (index == 2) {
            x = head_x - 30;
            width = 22;
            band_color = color;
            opacity = 100;
        } else {
            const int streak = static_cast<int>(index) - 3;
            width = 18 + streak * 8;
            height = streak % 3 == 0 ? 2 : 1;
            x = head_x - 22 - streak * 18 - (streak % 2) * 8;
            y = (streak * 7 + progress / 35) %
                std::max(1, DISPLAY_HEIGHT - height + 1);
            band_color = streak % 4 == 0 ? glint
                         : streak % 3 == 0 ? shadow
                                           : color;
            opacity = std::clamp(170 - streak * 12, 45, 170);
        }

        opacity = (opacity * fade_opacity) / 255;
        lv_obj_set_pos(band, x, y);
        lv_obj_set_size(band, width, height);
        lv_obj_set_style_bg_color(band, lv_color_hex(band_color), 0);
        lv_obj_set_style_bg_opa(
            band,
            static_cast<lv_opa_t>(std::clamp(opacity, 0, 255)),
            0);
        lv_obj_remove_flag(band, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(band);
    }
}

void canvas_rect_clipped_to_area(lv_layer_t *layer, int x, int y, int width,
                                 int height, int area_x, int area_y,
                                 int area_width, int area_height,
                                 lv_color_t color, lv_opa_t opacity)
{
    const int clipped_x1 = std::max(x, area_x);
    const int clipped_y1 = std::max(y, area_y);
    const int clipped_x2 = std::min(x + width, area_x + area_width);
    const int clipped_y2 = std::min(y + height, area_y + area_height);
    canvas_rect(layer,
                clipped_x1,
                clipped_y1,
                clipped_x2 - clipped_x1,
                clipped_y2 - clipped_y1,
                color,
                opacity);
}

void draw_runner_area_whoosh(lv_layer_t *layer, const RunnerSnapshot &snapshot,
                             int64_t now_us)
{
    if (!runner_whoosh_active(snapshot.whoosh, now_us) ||
        (snapshot.whoosh.type != RunnerWhooshType::FaultOrange &&
         snapshot.whoosh.type != RunnerWhooshType::RefusalOrange)) {
        return;
    }

    char faults_text[8]{};
    char refusals_text[8]{};
    std::snprintf(faults_text, sizeof(faults_text), "F:%d", snapshot.faults);
    std::snprintf(refusals_text, sizeof(refusals_text), "V:%d", snapshot.refusals);
    const RunnerLayout layout =
        calculate_runner_layout(snapshot, faults_text, refusals_text);

    const int area_x = std::max(0, layout.stats_x - 2);
    const int area_y =
        snapshot.whoosh.type == RunnerWhooshType::FaultOrange
            ? layout.fault_y
            : layout.refusal_y;
    const int area_width = DISPLAY_WIDTH - area_x;
    const int area_height =
        std::min(DISPLAY_HEIGHT - area_y, layout.stat_height);
    const int progress = runner_whoosh_progress(snapshot.whoosh, now_us);
    const int eased = ease_out_cubic_per_mille(progress);
    const int head_width = 16;
    const int head_x =
        area_x - head_width + ((area_width + head_width * 2) * eased) / 1000;
    const lv_color_t orange = lv_color_hex(RUN_ORANGE);
    const lv_color_t glint = lv_color_hex(runner_whoosh_glint_color(snapshot.whoosh.type));
    const lv_color_t shadow =
        lv_color_hex(runner_whoosh_shadow_color(snapshot.whoosh.type));

    for (int index = 0; index < 8; ++index) {
        const int band_width = index == 0 ? 9 : 14 + index * 7;
        const int x = head_x - index * 9 - band_width;
        const int height = index == 0 ? std::max(5, area_height - 3)
                                      : (index % 2 == 0 ? 3 : 2);
        const int y = index == 0 ? area_y + (area_height - height) / 2
                                 : area_y + 1 + (index % std::max(1, area_height - 2));
        const lv_color_t band_color = index == 0 ? glint
                                      : index == 3 ? shadow
                                                   : orange;
        const lv_opa_t opacity =
            static_cast<lv_opa_t>(std::clamp(235 - index * 25, 58, 235));
        canvas_rect_clipped_to_area(layer,
                                    x,
                                    y,
                                    band_width,
                                    height,
                                    area_x,
                                    area_y,
                                    area_width,
                                    area_height,
                                    band_color,
                                    opacity);
    }

    for (int spark = 0; spark < 5; ++spark) {
        const int spark_progress =
            std::clamp(progress - spark * 80 + (spark % 2) * 45, 0, 1000);
        const int spark_x = area_x + (area_width * spark_progress) / 1000;
        const int spark_y =
            area_y + (spark * 5 + progress / 90) % std::max(1, area_height);
        canvas_rect_clipped_to_area(layer,
                                    spark_x,
                                    spark_y,
                                    spark % 2 == 0 ? 2 : 1,
                                    1,
                                    area_x,
                                    area_y,
                                    area_width,
                                    area_height,
                                    glint,
                                    static_cast<lv_opa_t>(210));
    }
}

