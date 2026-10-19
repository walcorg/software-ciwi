/**
 * @file    display.h  (legacy/firmware/display.h — versión MODIFICADA)
 * @brief   Abstracción del display para WalCorg Cold Wallet
 *
 * ARCHIVO ORIGINAL:  legacy/firmware/display.h  del repo trezor-firmware
 * MODIFICADO PARA:   Display TFT ILI9488 3.5" 480×320 en lugar del
 *                    OLED SSD1306 128×64 del Trezor One original.
 *
 * RESUMEN DE CAMBIOS:
 * ┌─────────────────────┬──────────────────┬──────────────────────┐
 * │ Aspecto             │ Original (OLED)  │ WalCorg (TFT)        │
 * ├─────────────────────┼──────────────────┼──────────────────────┤
 * │ Ancho               │ 128 px           │ 480 px               │
 * │ Alto                │  64 px           │ 320 px               │
 * │ Profundidad color   │ 1 bit (mono)     │ 16 bits (RGB565)     │
 * │ Tamaño buffer       │ 1,024 bytes      │ 307,200 bytes*       │
 * │ Interfaz            │ SPI (SSD1306)    │ SPI (ILI9488)        │
 * │ Tipo funciones      │ oledXxx()        │ tft_xxx() wrapper    │
 * └─────────────────────┴──────────────────┴──────────────────────┘
 *
 * * El buffer completo de 307KB no cabe en SRAM (128KB total).
 *   En su lugar usamos escritura directa al display por ventanas
 *   (set_window → write_pixels) sin buffer intermedio.
 *
 * ESTRATEGIA DE COMPATIBILIDAD:
 *   El resto del firmware de Trezor (layout.c, protect.c, etc.)
 *   llama a funciones como oledDrawPixel(), oledFillRect(),
 *   oledDrawString(). En vez de reescribir todos esos archivos,
 *   redefinimos esas funciones como wrappers que llaman al
 *   driver TFT. Así el código de Trezor no cambia.
 *
 * MARCADORES DE CAMBIO:
 *   [MOD-D1] Dimensiones: 128×64 → 480×320
 *   [MOD-D2] Buffer: eliminado (escritura directa al TFT)
 *   [MOD-D3] Color: monocromático → RGB565
 *   [MOD-D4] API: wrappers oledXxx → tft_xxx
 *   [MOD-D5] Nuevas funciones específicas del TFT
 *
 * @license GNU GPL v3
 */

#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>
#include <stdbool.h>

/* Incluir el driver TFT real */
#include "display_tft.h"
#include "ili9488_regs.h"

/* =========================================================
 * [MOD-D1] DIMENSIONES DEL DISPLAY
 *
 * CAMBIO CRÍTICO: de 128×64 (OLED) a 480×320 (TFT landscape)
 *
 * Todo el código de Trezor que usa OLED_WIDTH y OLED_HEIGHT
 * automáticamente usará las nuevas dimensiones al recompilar.
 * ========================================================= */

/** [MOD-D1] Ancho del display en píxeles
 *  Original: #define OLED_WIDTH  128
 *  Nuevo:    480 (landscape ILI9488) */
#define OLED_WIDTH      480

/** [MOD-D1] Alto del display en píxeles
 *  Original: #define OLED_HEIGHT  64
 *  Nuevo:    320 (landscape ILI9488) */
#define OLED_HEIGHT     320

/** [MOD-D2] Tamaño del buffer OLED — ELIMINADO para TFT
 *  Original: #define OLED_BUFSIZE (OLED_WIDTH * OLED_HEIGHT / 8)
 *  = 1,024 bytes para OLED monocromático (1 bit por píxel)
 *
 *  Para el TFT en RGB565 sería 480 × 320 × 2 = 307,200 bytes
 *  que no cabe en SRAM. Usamos escritura directa por ventanas.
 *  Mantenemos la macro por compatibilidad pero con valor 0. */
#define OLED_BUFSIZE    0

/** [MOD-D3] Colores — de monocromático a RGB565
 *  El OLED original solo tenía blanco (1) y negro (0).
 *  El TFT soporta 65,536 colores en RGB565. */

/** Color de primer plano (texto, formas) — blanco en TFT */
#define OLED_COLOR_WHITE    ILI9488_WHITE

/** Color de fondo — negro oscuro del tema WalCorg */
#define OLED_COLOR_BLACK    WALCORG_BG

/** Color de acento — azul primario WalCorg */
#define OLED_COLOR_ACCENT   WALCORG_PRIMARY

/** Colores de estado */
#define OLED_COLOR_SUCCESS  WALCORG_SUCCESS
#define OLED_COLOR_WARNING  WALCORG_WARNING
#define OLED_COLOR_ERROR    WALCORG_ERROR

/* Colores actuales de texto y fondo (pueden cambiar con el contexto) */
extern tft_color_t g_fg_color;   /**< Color de primer plano activo */
extern tft_color_t g_bg_color;   /**< Color de fondo activo */


/* =========================================================
 * [MOD-D4] API COMPATIBLE CON TREZOR — WRAPPERS
 *
 * Todas las funciones oledXxx() que usa el resto del firmware
 * de Trezor (layout.c, protect.c, etc.) se redefinen aquí
 * como wrappers que llaman al driver TFT.
 *
 * El resto del código de Trezor NO necesita cambiar.
 * ========================================================= */

/**
 * @brief Inicializa el display TFT.
 * Wrapper de tft_init() en landscape.
 * Llamar en stm32_init() antes del firmware principal.
 */
void oledInit(void);

/**
 * @brief Limpia toda la pantalla con el color de fondo.
 * Original: llenaba el buffer OLED con ceros.
 * Nuevo: tft_clear(g_bg_color)
 */
void oledClear(void);

/**
 * @brief Refresca el display.
 * Original: enviaba el buffer OLED al IC via SPI.
 * Nuevo: NO-OP. El TFT escribe directamente, sin buffer.
 *        Se mantiene por compatibilidad con layout.c.
 */
void oledRefresh(void);

/**
 * @brief Dibuja un píxel en coordenadas (x, y).
 * @param x  Coordenada X (0 = izquierda)
 * @param y  Coordenada Y (0 = arriba)
 *
 * [MOD-D4] Usa g_fg_color en vez de "encender el bit"
 */
void oledDrawPixel(int x, int y);

/**
 * @brief Limpia (apaga) un píxel en coordenadas (x, y).
 * [MOD-D4] Dibuja el píxel con g_bg_color
 */
void oledClearPixel(int x, int y);

/**
 * @brief Invierte el color de un píxel.
 * Original: hacía XOR del bit en el buffer.
 * Nuevo: alterna entre fg y bg color en el TFT.
 */
void oledInvertPixel(int x, int y);

/**
 * @brief Dibuja una imagen bitmap monocromática.
 * @param x, y    Posición de la esquina superior izquierda
 * @param w, h    Dimensiones del bitmap
 * @param data    Datos del bitmap (1 bit por píxel, MSB primero)
 *
 * [MOD-D4] Convierte de 1bpp a RGB565 en el vuelo.
 *           Bits encendidos → g_fg_color, apagados → g_bg_color
 */
void oledDrawBitmap(int x, int y, const uint8_t *data, int w, int h);

/**
 * @brief Rellena un rectángulo.
 * @param x, y  Esquina superior izquierda
 * @param w, h  Dimensiones
 *
 * [MOD-D4] Wrapper de tft_fill_rect(x, y, w, h, g_fg_color)
 */
void oledFillRect(int x, int y, int w, int h);

/**
 * @brief Dibuja el borde de un rectángulo (sin relleno).
 */
void oledDrawRect(int x, int y, int w, int h);

/**
 * @brief Dibuja una línea horizontal.
 */
void oledHLine(int x, int y, int len);

/**
 * @brief Dibuja una línea vertical.
 */
void oledVLine(int x, int y, int len);

/**
 * @brief Dibuja un string en la posición dada.
 * @param x, y   Posición de la esquina superior izquierda
 * @param str    String terminado en '\0'
 * @param size   Tamaño de la fuente (1=pequeño, 2=normal, 3=grande)
 *
 * [MOD-D4] Wrapper de tft_draw_string() con g_fg/bg_color
 * [MOD-D5] Añadido parámetro size (el OLED siempre era size=1)
 */
void oledDrawString(int x, int y, const char *str, uint8_t size);

/**
 * @brief Dibuja un string centrado horizontalmente.
 * @param y     Posición vertical
 * @param str   String a centrar
 * @param size  Tamaño de la fuente
 */
void oledDrawStringCenter(int y, const char *str, uint8_t size);

/**
 * @brief Dibuja un string alineado a la derecha.
 * @param x     Borde derecho (el string termina aquí)
 * @param y     Posición vertical
 * @param str   String a alinear
 * @param size  Tamaño de la fuente
 */
void oledDrawStringRight(int x, int y, const char *str, uint8_t size);

/**
 * @brief Retorna el ancho en píxeles de un string.
 * @param str   String a medir
 * @param size  Tamaño de la fuente
 * @return      Ancho en píxeles
 */
int oledStringWidth(const char *str, uint8_t size);

/**
 * @brief Invierte los colores de un área rectangular.
 * Original: hacía XOR de los bits en el buffer OLED.
 * Nuevo: intercambia temporalmente fg y bg en esa región.
 * Usado en Trezor para resaltar el elemento seleccionado.
 */
void oledInvert(int x, int y, int w, int h);

/**
 * @brief Activa/desactiva el display completo.
 * @param visible  true=display encendido, false=apagado
 * [MOD-D4] Controla el backlight del TFT (BL pin)
 */
void oledSetVisible(bool visible);

/**
 * @brief Controla el brillo del backlight.
 * [MOD-D5] Nueva función — no existía en el OLED monocromático
 * @param brightness  0=apagado, 255=máximo
 */
void oledSetBrightness(uint8_t brightness);


/* =========================================================
 * [MOD-D4] CONTROL DE COLORES
 *
 * El OLED original era monocromático — no había concepto de
 * "color". El TFT soporta RGB565. Añadimos estas funciones
 * para que layout.c pueda cambiar los colores según el contexto:
 *   - Color normal: texto blanco sobre fondo oscuro
 *   - Confirmación: texto verde
 *   - Error/advertencia: texto rojo o naranja
 * ========================================================= */

/**
 * @brief Establece el color de primer plano (texto, formas).
 * [MOD-D5] Nueva función para el TFT con color
 */
void oledSetFgColor(tft_color_t color);

/**
 * @brief Establece el color de fondo.
 * [MOD-D5] Nueva función para el TFT con color
 */
void oledSetBgColor(tft_color_t color);

/**
 * @brief Restaura los colores al tema por defecto.
 * (Blanco sobre fondo oscuro WalCorg)
 */
void oledResetColors(void);


/* =========================================================
 * MACROS DE COMPATIBILIDAD
 *
 * Algunas partes del código de Trezor usan estas macros
 * directamente. Las redefinimos para el TFT.
 * ========================================================= */

/** Macro para dibujar un pixel — compatible con código legacy */
#define OLED_DRAW_PIXEL(x, y)   oledDrawPixel(x, y)

/** Macro para limpiar un pixel */
#define OLED_CLEAR_PIXEL(x, y)  oledClearPixel(x, y)

/** Verificar si las coordenadas están dentro del display */
#define OLED_IN_BOUNDS(x, y) \
    ((x) >= 0 && (x) < OLED_WIDTH && (y) >= 0 && (y) < OLED_HEIGHT)

#endif /* DISPLAY_H */
