#pragma once

#include <stddef.h>
#include <stdint.h>

namespace panel_brightness {

inline constexpr uint8_t FULL_BRIGHTNESS = 255;
inline constexpr uint8_t MIN_BRIGHTNESS = 217; // At most about 15% dimmer.
// Load is the RGB channel sum, relative to an entirely white display.
inline constexpr unsigned DIMMING_START_PERCENT = 10;
inline constexpr unsigned MAX_DIMMING_PERCENT = 35;

constexpr uint8_t brightness_for_load(uint32_t load, size_t pixel_count)
{
    const uint64_t full_load = static_cast<uint64_t>(pixel_count) * 3 * 255;
    const uint64_t start = full_load * DIMMING_START_PERCENT / 100;
    const uint64_t end = full_load * MAX_DIMMING_PERCENT / 100;
    if (load <= start) {
        return FULL_BRIGHTNESS;
    }
    if (load >= end) {
        return MIN_BRIGHTNESS;
    }
    return static_cast<uint8_t>(
        FULL_BRIGHTNESS -
        (FULL_BRIGHTNESS - MIN_BRIGHTNESS) * (load - start) / (end - start));
}

inline uint8_t brightness_for_frame(const uint8_t *pixels,
                                    size_t width,
                                    size_t height,
                                    size_t stride)
{
    uint32_t load = 0;
    for (size_t y = 0; y < height; ++y) {
        const uint8_t *row = pixels + y * stride;
        for (size_t x = 0; x < width; ++x) {
            // LVGL and the HUB75 driver use little-endian RGB565.
            const uint16_t pixel = row[x * 2] | (uint16_t{row[x * 2 + 1]} << 8);
            const uint8_t red = (pixel >> 11) & 0x1f;
            const uint8_t green = (pixel >> 5) & 0x3f;
            const uint8_t blue = pixel & 0x1f;
            // Expand each channel to 8 bits so green has the same weight.
            load += ((red << 3) | (red >> 2)) +
                    ((green << 2) | (green >> 4)) +
                    ((blue << 3) | (blue >> 2));
        }
    }
    return brightness_for_load(load, width * height);
}

} // namespace panel_brightness
