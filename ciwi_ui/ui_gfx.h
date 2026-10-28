/**
 * @file    ui_gfx.h
 * @brief   Drawing for the CiWi interface (portrait 320x480, RGB565).
 *
 * The backend only has to provide two functions (ui_gfx_backend_*): fill a rectangle and copy a
 * block of pixels. Everything else (text, rounded boxes, circles, progress bars) is built here,
 * so the same screens run on the ILI9488 (ui_gfx_tft.c) and in the PC test (tests/ui).
 *
 * Text is UTF-8. The 5x7 font covers ASCII plus á é í ó ú ü ñ ¿ ¡ (upper-case accented letters are
 * drawn without the accent) and a few symbols: ● ◉ ‹ › ✓ … · ↻.
 */
#ifndef UI_GFX_H
#define UI_GFX_H

#include <stdbool.h>
#include <stdint.h>

#define UI_W 320
#define UI_H 480

typedef uint16_t ui_color_t;

#define UI_RGB(r, g, b) \
    ((ui_color_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | (((b) & 0xF8) >> 3)))

/* ---- palette (same values as ui/ui_theme.h) */
#define UI_C_BG         UI_RGB(12, 12, 18)
#define UI_C_BAR        UI_RGB(22, 24, 35)
#define UI_C_ROW        UI_RGB(22, 24, 35)
#define UI_C_BORDER     UI_RGB(45, 50, 72)
#define UI_C_TEXT       UI_RGB(235, 237, 245)
#define UI_C_TEXT2      UI_RGB(140, 145, 170)
#define UI_C_DISABLED   UI_RGB(70, 73, 90)
#define UI_C_ACCENT     UI_RGB(64, 156, 255)
#define UI_C_SELECTED   UI_RGB(25, 55, 100)
#define UI_C_GOOD       UI_RGB(50, 210, 120)
#define UI_C_WARN       UI_RGB(255, 185, 50)
#define UI_C_BAD        UI_RGB(230, 65, 65)
#define UI_C_BTC        UI_RGB(242, 169, 0)

/* ---- backend (one implementation per platform) */
void ui_gfx_backend_fill(int16_t x, int16_t y, int16_t w, int16_t h, ui_color_t c);
void ui_gfx_backend_blit(int16_t x, int16_t y, int16_t w, int16_t h, const ui_color_t *px);

/* ---- primitives */
void ui_fill(int16_t x, int16_t y, int16_t w, int16_t h, ui_color_t c);
void ui_frame(int16_t x, int16_t y, int16_t w, int16_t h, ui_color_t c);
void ui_round(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, ui_color_t c);
/** Rounded outline of thickness thick, filled with inside. */
void ui_round_frame(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, ui_color_t c,
                    int16_t thick, ui_color_t inside);
void ui_disc(int16_t cx, int16_t cy, int16_t r, ui_color_t c);
void ui_ring(int16_t cx, int16_t cy, int16_t r, int16_t thick, ui_color_t c);

/* ---- text: size = scale of the 5x7 font (2 => 12x16 px per character cell) */
int16_t ui_text_width(const char *utf8, uint8_t size);
/** Draws on a solid background bg; returns the x after the last character. */
int16_t ui_text(int16_t x, int16_t y, const char *utf8, uint8_t size, ui_color_t fg,
                ui_color_t bg);
void ui_text_center(int16_t cx, int16_t y, const char *utf8, uint8_t size, ui_color_t fg,
                    ui_color_t bg);
void ui_text_right(int16_t xr, int16_t y, const char *utf8, uint8_t size, ui_color_t fg,
                   ui_color_t bg);
/** Number of characters (code points) in a UTF-8 string. */
int ui_utf8_len(const char *s);

#endif /* UI_GFX_H */
