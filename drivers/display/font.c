/**
 * @file    font.c
 * @brief   Fuente bitmap 5x7 y función de dibujo — WALCORG Fase 2
 * @license GNU GPL v3 — Tabla de fuente basada en Adafruit GFX (MIT)
 */

#include "font.h"
#include "display_tft.h"

/* =========================================================
 * TABLA DE LA FUENTE 5x7
 * 95 caracteres ASCII del espacio (32) a la tilde (126).
 * 5 bytes por caracter = 5 columnas de 7 pixeles cada una.
 * Bit 0 = fila superior, Bit 6 = fila inferior.
 * Almacenada en Flash (const) para no consumir RAM.
 * ========================================================= */
/* [v0.4] The table font5x7[] now lives in font5x7.c (no hardware dependency). */


/* =========================================================
 * FUNCION DE DIBUJO DE CARACTER
 * ========================================================= */

/**
 * font_draw_char — Dibuja un caracter escalable en el display
 *
 * Para cada columna (0..4) y cada fila (0..6) del caracter:
 *   Si el bit [fila] de la columna esta encendido → color fg
 *   Si no → color bg
 *
 * Con size > 1, cada "pixel" de la fuente se convierte en un
 * cuadrado de size×size pixeles reales usando fill_rect.
 */
void font_draw_char(tft_coord_t x, tft_coord_t y,
                    char c, tft_color_t fg, tft_color_t bg,
                    uint8_t size) {

    /* Validar y normalizar el caracter */
    if (c < FONT_FIRST_CHAR || c > FONT_LAST_CHAR) c = ' ';
    if (size < 1) size = 1;

    uint8_t idx = (uint8_t)(c - FONT_FIRST_CHAR);

    /* Para cada columna del caracter (5 columnas) */
    for (uint8_t col = 0; col < FONT_CHAR_WIDTH; col++) {
        uint8_t col_data = font5x7[idx][col];

        /* Para cada fila (7 filas) */
        for (uint8_t row = 0; row < FONT_CHAR_HEIGHT; row++) {

            /* Bit [row] determina si el pixel esta encendido */
            tft_color_t pixel_color = (col_data & (1 << row)) ? fg : bg;

            if (size == 1) {
                /* Un solo pixel — mas eficiente que fill_rect 1x1 */
                tft_draw_pixel(
                    x + (tft_coord_t)col,
                    y + (tft_coord_t)row,
                    pixel_color
                );
            } else {
                /* Cuadrado de size×size pixels */
                tft_fill_rect(
                    x + (tft_coord_t)(col * size),
                    y + (tft_coord_t)(row * size),
                    (tft_coord_t)size,
                    (tft_coord_t)size,
                    pixel_color
                );
            }
        }
    }

    /* Columna de separacion entre caracteres */
    if (size == 1) {
        for (uint8_t row = 0; row < FONT_CHAR_HEIGHT; row++) {
            tft_draw_pixel(x + FONT_CHAR_WIDTH, y + (tft_coord_t)row, bg);
        }
    } else {
        tft_fill_rect(
            x + (tft_coord_t)(FONT_CHAR_WIDTH * size), y,
            (tft_coord_t)size, (tft_coord_t)(FONT_CHAR_HEIGHT * size), bg
        );
    }
}
