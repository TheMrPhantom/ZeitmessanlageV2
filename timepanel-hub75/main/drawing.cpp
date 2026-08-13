#include "timepanel_common.h"

constexpr std::array<const char *, SPLASH_LOGO_HEIGHT> DOGDOG_LOGO_BITMAP{{
    "                        wW  w",
    "                        WWWwWW",
    "                        WWWWWWw",
    "                        wWWWWWWw",
    "                        WWWWWWWww",
    "     bBBb BBBBBBBBBBBBbwWWWWWWWwWw",
    "     bbb  bbbbbbbbbbbbbwWWWWWWWwwwWw",
    "          bbb   bbbbbbbWWWWW      WW",
    "  wWWw    BBb   bBBBBBWWWWWw wWWwwWw",
    " wWWW bBBBBBBBBBBBb  WWWWWw  wWWWWw",
    "wWWWW bbbbbbbbbbbbbwWWWWWw   wWWWWw",
    "WWWWWw      wwwwwwwWbbbbWWw   wWwWw",
    "WWWWWWw   wWWWWWWWWbb  bbWW   wW",
    "WWWWWWWWWWWWWWWWWWWb bbbbBWw   W",
    "WWWWWWWWWWWWWWWWWWWb bb  bWw  wW",
    "WWWWWWWWWWWWWWWWWWWb    bBWW  WW",
    "wWWWWWWWWWWWWWWWWWWWbb bbWWWWWWWWWw",
    " wWWWWWWWWWWWWWWWWWWWBBBWWWw    wWWWw",
    "  wWWWWWWWWWWWWWWWWWWWWWWWw   ww wWwWw",
    "    wWWWWWWWWWWWWWWWWWWWWw w WWWwwwwwW",
    "    wWWWWWWWWWWWWWWWWWWWWWWWWWwwWWWWWw",
    "   wWWWWWWWWWWW  bbbbbWWWWww",
    "  wWWWWWWWWWWWw  bBBBBBBb",
    " wWWWwwwWWWWWw",
    "wWwW   WWWWw      b",
    "WwW   WwWw       bBb",
    "WWw  wWww",
    "      ww",
    "",
    "",
}};

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
                        lv_opa_t opacity)
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
                 lv_color_t color, lv_opa_t opacity)
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
                       lv_text_align_t align,
                       int max_width)
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

uint32_t splash_logo_color(char pixel)
{
    switch (pixel) {
    case 'W':
        return 0xffffff;
    case 'w':
        return SPLASH_WHITE_DIM;
    case 'B':
        return DOGDOG_BLUE;
    case 'b':
        return DOGDOG_BLUE_DIM;
    default:
        return 0;
    }
}

void draw_splash_logo_direct(int x, int y)
{
    for (size_t row = 0; row < DOGDOG_LOGO_BITMAP.size(); ++row) {
        const char *pixels = DOGDOG_LOGO_BITMAP[row];
        for (size_t col = 0; pixels[col] != '\0'; ++col) {
            const uint32_t color = splash_logo_color(pixels[col]);
            if (color != 0) {
                canvas_rect_direct(x + static_cast<int>(col),
                                   y + static_cast<int>(row),
                                   1,
                                   1,
                                   lv_color_hex(color));
            }
        }
    }
}

void draw_startup_splash_logo_direct()
{
    const int logo_x = std::max(0, (SINGLE_PANEL_WIDTH - SPLASH_LOGO_WIDTH) / 2);
    const int logo_y = std::max(0, (DISPLAY_HEIGHT - SPLASH_LOGO_HEIGHT) / 2);
    draw_splash_logo_direct(logo_x, logo_y);
}

const lv_font_t *splash_title_font(const char *title, int max_width)
{
    const lv_font_t *font = font_18();
    if (measure_text(title, font).x <= max_width) {
        return font;
    }

    font = font_16();
    if (measure_text(title, font).x <= max_width) {
        return font;
    }

    font = font_14();
    if (measure_text(title, font).x <= max_width) {
        return font;
    }

    return font_12();
}

void render_startup_splash_text(lv_layer_t *layer)
{
    constexpr char title[] = "DogDog Zeitmessung";

    const int text_area_x = SINGLE_PANEL_WIDTH + 3;
    const int text_area_width = std::max(1, DISPLAY_WIDTH - text_area_x - 4);
    const lv_font_t *font = splash_title_font(title, text_area_width);
    const lv_point_t title_size = measure_text(title, font);
    const int title_width = static_cast<int>(title_size.x);
    const int title_height = static_cast<int>(title_size.y);
    const int title_x =
        text_area_x + std::max(0, (text_area_width - title_width) / 2);
    const int title_y = std::max(0, (DISPLAY_HEIGHT - title_height) / 2);

    draw_canvas_label(layer,
                      title_x,
                      title_y,
                      title,
                      font,
                      lv_color_hex(0xffffff),
                      LV_TEXT_ALIGN_LEFT,
                      text_area_width);
}

uint32_t blend_channel(uint32_t a, uint32_t b, int amount, int maximum)
{
    return (a * static_cast<uint32_t>(maximum - amount) +
            b * static_cast<uint32_t>(amount)) /
           static_cast<uint32_t>(maximum);
}

lv_color_t blend_hex(uint32_t from, uint32_t to, int amount, int maximum)
{
    if (maximum <= 0) {
        return lv_color_hex(to);
    }

    amount = std::clamp(amount, 0, maximum);
    const uint32_t red =
        blend_channel((from >> 16) & 0xff, (to >> 16) & 0xff, amount, maximum);
    const uint32_t green =
        blend_channel((from >> 8) & 0xff, (to >> 8) & 0xff, amount, maximum);
    const uint32_t blue =
        blend_channel(from & 0xff, to & 0xff, amount, maximum);

    return lv_color_hex((red << 16) | (green << 8) | blue);
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


int ease_out_cubic_per_mille(int progress)
{
    progress = std::clamp(progress, 0, 1000);
    const int64_t inverse = 1000 - progress;
    return static_cast<int>(1000 - (inverse * inverse * inverse) / 1000000);
}

