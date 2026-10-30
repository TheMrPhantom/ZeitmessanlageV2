These 4-bpp Montserrat Medium subsets add U+00C4, U+00D6, U+00DC, U+00DF,
U+00E4, U+00F6 and U+00FC (uppercase/lowercase umlauts and sharp s). They are
fallbacks for the built-in LVGL fonts, preserving ASCII kerning and baselines.
The 12 px font needs one extra pixel of line height to avoid clipping uppercase
umlaut dots; the other sizes keep their original line heights. The source font
uses the SIL Open Font License in OFL.txt. Generated with lv_font_conv 1.5.3.

To regenerate from the repository root with lv_font_conv installed:

```powershell
foreach ($size in 10, 12, 14, 16, 18) {
    lv_font_conv --size $size --bpp 4 --no-compress --no-prefilter --no-kerning `
        --format lvgl --lv-include lvgl.h `
        --font managed_components/lvgl__lvgl/scripts/built_in_font/Montserrat-Medium.ttf `
        -r 0xC4,0xD6,0xDC,0xDF,0xE4,0xF6,0xFC `
        -o main/fonts/timepanel_font_german_$size.c
}
```
