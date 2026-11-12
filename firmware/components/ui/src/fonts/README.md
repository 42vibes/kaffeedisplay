# Device fonts

Montserrat subsets with German umlauts, `ß`, `€`, dashes, quotes, arrows and
`×`. Generated once and committed so the ESP-IDF build does not need Node.

- Family: Montserrat Medium (regular) / Montserrat Bold, SIL OFL 1.1 (`OFL.txt`).
- Generator: `lv_font_conv` 1.5.3, bpp 4, no compression.
- Glyph ranges: `0x20-0x7F`, Latin-1 punctuation needed by German, `€`, arrows.

Regenerate from an LVGL 9.6 checkout (the TTFs are in the LVGL repository):

```sh
RANGE='0x20-0x7F,0xA0,0xA7,0xAB,0xB0,0xB7,0xBB,0xC4,0xD6,0xDC,0xDF,0xE4,0xF6,0xFC,0xD7,0x2013-0x2014,0x201C-0x201E,0x2022,0x2026,0x20AC,0x2190-0x2193'
MED=path/to/lvgl/scripts/generators/built_in_font/Montserrat-Medium.ttf
BOLD=path/to/lvgl/tests/src/test_files/fonts/Montserrat-Bold.ttf
npx --yes lv_font_conv@1.5.3 --font "$MED"  -r "$RANGE" --size 14 --bpp 4 --format lvgl -o kaffee_font_14.c --no-compress
npx --yes lv_font_conv@1.5.3 --font "$MED"  -r "$RANGE" --size 18 --bpp 4 --format lvgl -o kaffee_font_18.c --no-compress
npx --yes lv_font_conv@1.5.3 --font "$MED"  -r "$RANGE" --size 22 --bpp 4 --format lvgl -o kaffee_font_22.c --no-compress
npx --yes lv_font_conv@1.5.3 --font "$BOLD" -r "$RANGE" --size 18 --bpp 4 --format lvgl -o kaffee_font_18_bold.c --no-compress
npx --yes lv_font_conv@1.5.3 --font "$BOLD" -r "$RANGE" --size 24 --bpp 4 --format lvgl -o kaffee_font_24_bold.c --no-compress
npx --yes lv_font_conv@1.5.3 --font "$BOLD" -r "$RANGE" --size 34 --bpp 4 --format lvgl -o kaffee_font_34_bold.c --no-compress
```
