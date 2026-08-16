#include "timepanel_common.h"

namespace {

const dogdog_ota_progress_t *g_firmware_upgrade_progress = nullptr;

void render_firmware_upgrade_canvas(lv_layer_t *layer)
{
    char text[64];
    if (g_firmware_upgrade_progress != nullptr && g_firmware_upgrade_progress->bytes_received > 0) {
        if (g_firmware_upgrade_progress->total_size > 0 && g_firmware_upgrade_progress->progress_percent >= 0) {
            snprintf(text,
                     sizeof(text),
                     "Firmware Upgrade %d%%\n%zu/%zu bytes",
                     g_firmware_upgrade_progress->progress_percent,
                     g_firmware_upgrade_progress->bytes_received,
                     g_firmware_upgrade_progress->total_size);
        } else {
            snprintf(text,
                     sizeof(text),
                     "Firmware Upgrade\n%zu bytes",
                     g_firmware_upgrade_progress->bytes_received);
        }
    } else {
        snprintf(text, sizeof(text), "Firmware Upgrade");
    }

    const lv_font_t *font = font_18();
    const lv_point_t size = measure_text(text, font);
    const int y = std::max(0, static_cast<int>((DISPLAY_HEIGHT - size.y) / 2));
    draw_canvas_label(layer,
                      0,
                      y,
                      text,
                      font,
                      lv_color_hex(0xffffff),
                      LV_TEXT_ALIGN_CENTER,
                      DISPLAY_WIDTH);
}

} // namespace

void render_startup_splash_screen()
{
    if (!lvgl_port_lock(1000)) {
        ESP_LOGW(TAG, "Timed out waiting for LVGL lock");
        return;
    }

    hide_runner_labels();
    hide_runner_whoosh_bands();
    clear_canvas_buffer();
    draw_startup_splash_logo_direct();

    lv_layer_t layer;
    lv_canvas_init_layer(g_canvas, &layer);
    render_startup_splash_text(&layer);
    draw_power_status_icon(&layer, esp_timer_get_time());
    lv_canvas_finish_layer(g_canvas, &layer);

    present_canvas_buffer();
    lvgl_port_unlock();
}

void render_canvas(void (*draw)(lv_layer_t *))
{
    if (!lvgl_port_lock(1000)) {
        ESP_LOGW(TAG, "Timed out waiting for LVGL lock");
        return;
    }

    hide_runner_labels();
    hide_runner_whoosh_bands();
    lv_canvas_fill_bg(g_canvas, lv_color_hex(0x000000), LV_OPA_COVER);

    lv_layer_t layer;
    lv_canvas_init_layer(g_canvas, &layer);
    draw(&layer);
    draw_power_status_icon(&layer, esp_timer_get_time());
    lv_canvas_finish_layer(g_canvas, &layer);

    lv_obj_invalidate(g_canvas);
    lv_refr_now(g_display);
    lvgl_port_unlock();
}

void render_firmware_upgrade_screen(const dogdog_ota_progress_t *progress)
{
    g_firmware_upgrade_progress = progress;
    render_canvas(render_firmware_upgrade_canvas);
}

void render_firmware_check_screen()
{
    render_canvas([](lv_layer_t *layer) {
        const char text[] = "Checking Firmware";
        const lv_font_t *font = font_18();
        const lv_point_t size = measure_text(text, font);
        const int y = std::max(0, static_cast<int>((DISPLAY_HEIGHT - size.y) / 2));
        draw_canvas_label(layer,
                          0,
                          y,
                          text,
                          font,
                          lv_color_hex(0xffffff),
                          LV_TEXT_ALIGN_CENTER,
                          DISPLAY_WIDTH);
    });
}

void render_runner_preview_screen(int64_t now_us)
{
    const RunnerSnapshot snapshot = runner_snapshot(now_us);

    if (!lvgl_port_lock(1000)) {
        ESP_LOGW(TAG, "Timed out waiting for LVGL lock");
        return;
    }

    lv_canvas_fill_bg(g_canvas, lv_color_hex(0x000000), LV_OPA_COVER);

    lv_layer_t layer;
    lv_canvas_init_layer(g_canvas, &layer);
    draw_runner_preview(&layer, snapshot);
    draw_runner_area_whoosh(&layer, snapshot, now_us);
    draw_power_status_icon(&layer, now_us);
    lv_canvas_finish_layer(g_canvas, &layer);

    show_runner_labels(snapshot);
    update_runner_full_whoosh_bands(snapshot, now_us);
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
    hide_runner_whoosh_bands();
    lv_canvas_fill_bg(g_canvas, lv_color_hex(0x000000), LV_OPA_COVER);
    lv_layer_t layer;
    lv_canvas_init_layer(g_canvas, &layer);
    render_parcours_intro(&layer, elapsed_ms);
    draw_power_status_icon(&layer, esp_timer_get_time());
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
    hide_runner_whoosh_bands();
    lv_canvas_fill_bg(g_canvas, lv_color_hex(0x000000), LV_OPA_COVER);
    lv_layer_t layer;
    lv_canvas_init_layer(g_canvas, &layer);
    render_parcours_start_whoosh(&layer, elapsed_ms);
    draw_power_status_icon(&layer, esp_timer_get_time());
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
    hide_runner_whoosh_bands();
    clear_canvas_buffer();
    render_parcours_timer(elapsed_ms);
    draw_power_status_icon_direct(esp_timer_get_time());
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
    hide_runner_whoosh_bands();
    clear_canvas_buffer();
    render_parcours_whoosh(elapsed_ms);
    draw_power_status_icon_direct(esp_timer_get_time());
    present_canvas_buffer();
    lvgl_port_unlock();
}

void render_ended_screen()
{
    render_canvas([](lv_layer_t *layer) {
        render_parcours_ended(layer);
    });
}

