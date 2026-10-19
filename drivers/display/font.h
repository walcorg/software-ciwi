/**
 * @file    font.h / font.c
 * @brief   Fuente bitmap 5×7 para el display TFT — WalCorg Fase 2
 *
 * Fuente ASCII completa (32-126) en formato bitmap de 5×7 píxeles.
 * Cada caracter ocupa 5 bytes (1 byte por columna, 7 filas usadas).
 *
 * Con size=1: 5×7 px por caracter (muy pequeño, 80 chars por fila)
 * Con size=2: 10×14 px (legible, 48 chars por fila) ← recomendado UI
 * Con size=3: 15×21 px (grande, 32 chars por fila) ← títulos
 *
 * El formato de los datos:
 *   Cada caracter = 5 bytes, uno por columna (izquierda a derecha)
 *   Cada byte: bit 0 = fila superior, bit 6 = fila inferior
 *   Ejemplo 'A': columna 0 = 0x7E (0111 1110) → encendidos filas 1-6
 *
 * @license GNU GPL v3 — Fuente basada en Adafruit GFX (MIT License)
 */

#ifndef FONT_H
#define FONT_H

#include <stdint.h>
#include "ili9488_regs.h"  /* Para tft_color_t y tft_coord_t */
#include "display_tft.h"

/* Dimensiones de un caracter en la fuente base (antes de scale) */
#define FONT_CHAR_WIDTH   5   /**< Píxeles de ancho por caracter */
#define FONT_CHAR_HEIGHT  7   /**< Píxeles de alto por caracter */
#define FONT_CHAR_SPACING 1   /**< Píxeles de separación entre caracteres */
#define FONT_LINE_SPACING 1   /**< Píxeles de separación entre líneas */

/** Primer caracter del mapa (espacio = ASCII 32) */
#define FONT_FIRST_CHAR   32

/** Último caracter del mapa (tilde = ASCII 126) */
#define FONT_LAST_CHAR    126

/** Número total de caracteres en la fuente */
#define FONT_NUM_CHARS    (FONT_LAST_CHAR - FONT_FIRST_CHAR + 1)

/** Tabla de datos de la fuente — en Flash (const) para ahorrar RAM */
extern const uint8_t font5x7[FONT_NUM_CHARS][FONT_CHAR_WIDTH];

/**
 * @brief Dibuja un caracter en el display.
 *
 * @param x, y  Posición de la esquina superior izquierda
 * @param c     Caracter ASCII (32-126). Fuera de rango → espacio
 * @param fg    Color del caracter
 * @param bg    Color del fondo del caracter
 * @param size  Escala: 1=5×7, 2=10×14, 3=15×21
 */
void font_draw_char(tft_coord_t x, tft_coord_t y,
                    char c, tft_color_t fg, tft_color_t bg,
                    uint8_t size);

#endif /* FONT_H */
