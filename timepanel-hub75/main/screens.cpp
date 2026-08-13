#include "timepanel_common.h"

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
    lv_canvas_finish_layer(g_canvas, &layer);

    lv_obj_invalidate(g_canvas);
    lv_refr_now(g_display);
    lvgl_port_unlock();
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
    present_canvas_buffer();
    lvgl_port_unlock();
}

void render_ended_screen()
{
    render_canvas([](lv_layer_t *layer) {
        render_parcours_ended(layer);
    });
}

