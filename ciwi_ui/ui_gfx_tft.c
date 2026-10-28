/**
 * @file    ui_gfx_tft.c
 * @brief   ui_gfx backend for the ILI9488 (drivers/display). The display must be initialised in
 *          portrait: tft_init(TFT_ROTATION_0) gives 320x480.
 */
#include "display_tft.h"
#include "ui_gfx.h"

void ui_gfx_backend_fill(int16_t x, int16_t y, int16_t w, int16_t h, ui_color_t c)
{
    tft_fill_rect(x, y, w, h, c);
}

void ui_gfx_backend_blit(int16_t x, int16_t y, int16_t w, int16_t h, const ui_color_t *px)
{
    tft_set_window(x, y, w, h);
    tft_write_buffer(px, (uint32_t)w * (uint32_t)h);
    tft_end_write();
}
