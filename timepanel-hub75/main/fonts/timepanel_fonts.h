#pragma once

#include <algorithm>

#include "lvgl.h"

extern "C" {
LV_FONT_DECLARE(timepanel_font_german_10);
LV_FONT_DECLARE(timepanel_font_german_12);
LV_FONT_DECLARE(timepanel_font_german_14);
LV_FONT_DECLARE(timepanel_font_german_16);
LV_FONT_DECLARE(timepanel_font_german_18);
}

template <const lv_font_t &BaseFont, const lv_font_t &GermanFont>
const lv_font_t *font_with_german_glyphs()
{
    // Keep ASCII kerning and allow enough room above the baseline for the dots.
    static const lv_font_t font = [] {
        lv_font_t extended = BaseFont;
        extended.fallback = &GermanFont;
        extended.base_line = std::max(BaseFont.base_line, GermanFont.base_line);
        extended.line_height =
            std::max(BaseFont.line_height - BaseFont.base_line,
                     GermanFont.line_height - GermanFont.base_line) +
            extended.base_line;
        return extended;
    }();
    return &font;
}
