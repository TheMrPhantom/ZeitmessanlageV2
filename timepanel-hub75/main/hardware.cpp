#include "timepanel_common.h"

namespace {

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

} // namespace

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
    reset_runner_label_cache(g_runner_name_label_cache);
    g_runner_dog_label = lv_label_create(screen);
    initialize_runner_label(g_runner_dog_label);
    reset_runner_label_cache(g_runner_dog_label_cache);
    for (lv_obj_t *&band : g_runner_whoosh_bands) {
        band = lv_obj_create(screen);
        initialize_runner_whoosh_band(band);
    }

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

