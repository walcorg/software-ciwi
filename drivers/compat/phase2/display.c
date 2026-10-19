/**
 * @file    display.c  (legacy/firmware/display.c — versión MODIFICADA)
 * @brief   Implementación de wrappers oledXxx → tft_xxx — WalCorg Fase 2
 *
 * Este archivo implementa la capa de compatibilidad entre el
 * código existente de Trezor (que llama oledXxx) y nuestro
 * driver TFT ILI9488 (que expone tft_xxx).
 *
 * ARCHIVO ORIGINAL: legacy/firmware/oled.c del repo trezor-firmware
 * REEMPLAZA:  oled.c completo — el código del SSD1306 ya no aplica.
 *
 * DECISIONES DE DISEÑO:
 *
 *   1. Sin buffer de pantalla completo
 *      El OLED usaba un buffer de 1KB en SRAM que se enviaba
 *      completo al IC en cada oledRefresh(). El TFT no puede
 *      tener un buffer de 307KB en SRAM (solo tenemos 128KB).
 *      Escribimos directamente al TFT en cada llamada.
 *
 *   2. oledRefresh() es un NO-OP
 *      Layout.c llama oledRefresh() después de componer la pantalla.
 *      En el OLED esto enviaba el buffer acumulado.
 *      En el TFT cada función ya escribe directamente, así que
 *      refresh no tiene nada que hacer.
 *
 *   3. Coordenadas escaladas
 *      El OLED era 128×64. El TFT es 480×320.
 *      Ratio: 480/128 ≈ 3.75 en X,  320/64 = 5.0 en Y.
 *      Las llamadas con coordenadas hardcodeadas de Trezor
 *      (ej: oledDrawString(0, 0, ...)) se verán en la
 *      esquina superior izquierda — posición correcta.
 *      Las coordenadas que dependen de OLED_WIDTH/OLED_HEIGHT
 *      automáticamente usan 480/320 al recompilar.
 *
 *   4. Fuente escalada
 *      La fuente original de Trezor (6×8 px) resulta muy pequeña
 *      en 480×320. Usamos un factor de escala x2 por defecto
 *      para que el texto sea legible. Layout.c puede cambiarlo.
 *
 * @license GNU GPL v3
 */

#include "display.h"
#include "display_tft.h"
#include "font.h"
#include "uart_debug.h"

/* =========================================================
 * ESTADO GLOBAL DE COLORES
 * ========================================================= */

/** Color de primer plano activo (texto, formas)
 *  Por defecto: blanco del tema WalCorg */
tft_color_t g_fg_color = WALCORG_TEXT;

/** Color de fondo activo
 *  Por defecto: negro oscuro del tema WalCorg */
tft_color_t g_bg_color = WALCORG_BG;

/**
 * Factor de escala por defecto para la fuente.
 * El texto de Trezor fue diseñado para fuente 6×8 en 128×64.
 * En 480×320 usamos escala ×2 (12×16 px) para mejor legibilidad.
 * Layout.c puede cambiar esto con oledDrawString(..., size).
 */
#define DEFAULT_FONT_SIZE   2


/* =========================================================
 * INICIALIZACIÓN
 * ========================================================= */

/**
 * oledInit — Inicializar el display TFT
 *
 * Reemplaza la inicialización del SSD1306 OLED del Trezor One.
 * Llama a tft_init() con orientación landscape (480×320).
 *
 * [MOD-D4] El original configuraba:
 *   GPIO para RST, CS, DC del SSD1306
 *   SPI a ~5MHz para el SSD1306
 *   Secuencia de 26 comandos de init del SSD1306
 *
 * Ahora llamamos a tft_init() que hace todo eso para el ILI9488.
 */
void oledInit(void) {
    DEBUG_PRINT("[DISP] oledInit() → tft_init(landscape)\r\n");
    tft_init(TFT_ROTATION_90);  /* 480×320 landscape */
    oledResetColors();
}

/**
 * oledClear — Limpiar la pantalla con el color de fondo
 *
 * [MOD-D4] Original: memset(_oledbuffer, 0, OLED_BUFSIZE)
 * Nuevo: tft_clear(g_bg_color)
 */
void oledClear(void) {
    tft_clear(g_bg_color);
}

/**
 * oledRefresh — Enviar buffer al display
 *
 * [MOD-D4] Original: enviaba _oledbuffer[] al SSD1306 via SPI
 * Nuevo: NO-OP — el TFT escribe directamente sin buffer.
 *
 * Se mantiene la función por compatibilidad con layout.c,
 * protect.c y otros archivos de Trezor que la llaman.
 * En el TFT cada operación ya escribe directamente al IC.
 */
void oledRefresh(void) {
    /* NO-OP intencional — ver comentario en el header */
    /* En debug podríamos contar los refreshes para FPS: */
    /* g_tft.fps_count++; */
}


/* =========================================================
 * PRIMITIVAS DE PIXEL
 * ========================================================= */

/**
 * oledDrawPixel — Encender un píxel en (x, y)
 *
 * [MOD-D4] Original: _oledbuffer[OLED_OFFSET(x,y)] |= OLED_MASK(x,y)
 * Nuevo: tft_draw_pixel(x, y, g_fg_color)
 *
 * NOTA: En el OLED no había concepto de color — el píxel
 * era blanco (encendido) o negro (apagado). En el TFT
 * usamos el color de primer plano activo.
 */
void oledDrawPixel(int x, int y) {
    if (!OLED_IN_BOUNDS(x, y)) return;
    tft_draw_pixel((tft_coord_t)x, (tft_coord_t)y, g_fg_color);
}

/**
 * oledClearPixel — Apagar un píxel en (x, y)
 *
 * [MOD-D4] Original: _oledbuffer[OLED_OFFSET(x,y)] &= ~OLED_MASK(x,y)
 * Nuevo: dibuja con el color de fondo
 */
void oledClearPixel(int x, int y) {
    if (!OLED_IN_BOUNDS(x, y)) return;
    tft_draw_pixel((tft_coord_t)x, (tft_coord_t)y, g_bg_color);
}

/**
 * oledInvertPixel — Invertir el color de un píxel
 *
 * [MOD-D4] Original: XOR del bit en el buffer
 * Nuevo: No podemos leer el color actual del TFT eficientemente.
 * Simplificación: alternar entre fg y bg basado en una bandera.
 * En la práctica, Trezor usa esto principalmente para el cursor
 * de texto animado — en el TFT lo implementamos como fg color.
 */
void oledInvertPixel(int x, int y) {
    /* Simplificación: dibujar siempre con fg_color inverso */
    if (!OLED_IN_BOUNDS(x, y)) return;
    /* Swap temporal de colores para el pixel */
    tft_draw_pixel((tft_coord_t)x, (tft_coord_t)y,
                   (g_fg_color == g_bg_color) ? WALCORG_TEXT : g_bg_color);
}


/* =========================================================
 * FORMAS Y ÁREAS
 * ========================================================= */

/**
 * oledDrawBitmap — Dibujar bitmap monocromático
 *
 * [MOD-D4] El OLED recibía bitmaps de 1 bit por píxel
 * (formato SSD1306: columnas de 8 píxeles por byte).
 *
 * Trezor usa bitmaps para:
 *   - El logo de inicio (bootloader)
 *   - Íconos en la UI (confirmación, advertencia, BTC logo)
 *   - Imágenes de confirmación de transacciones
 *
 * Aquí convertimos el formato 1bpp del OLED al formato
 * RGB565 del TFT en tiempo real.
 *
 * Formato de los datos (igual que el OLED original):
 *   data[y*cols + x] donde cada byte = columna de 8 píxeles
 *   bit 0 = píxel superior, bit 7 = píxel inferior
 *   cols = ceil(w/8)
 */
void oledDrawBitmap(int x, int y, const uint8_t *data, int w, int h) {
    if (!data || w <= 0 || h <= 0) return;

    int cols = (w + 7) / 8;  /* Bytes por fila */

    tft_set_window((tft_coord_t)x, (tft_coord_t)y,
                   (tft_coord_t)w, (tft_coord_t)h);

    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) {
            /* Extraer el bit correspondiente */
            int byte_idx = row * cols + (col / 8);
            int bit_idx  = 7 - (col % 8);   /* MSB primero */
            bool pixel_on = (data[byte_idx] >> bit_idx) & 1;

            tft_write_pixel(pixel_on ? g_fg_color : g_bg_color);
        }
    }
    tft_end_write();
}

/**
 * oledFillRect — Rellenar un rectángulo con el color de primer plano
 *
 * [MOD-D4] Original: ponía a 1 todos los bits del área en el buffer
 * Nuevo: tft_fill_rect con g_fg_color
 */
void oledFillRect(int x, int y, int w, int h) {
    tft_fill_rect((tft_coord_t)x, (tft_coord_t)y,
                  (tft_coord_t)w, (tft_coord_t)h,
                  g_fg_color);
}

/**
 * oledDrawRect — Dibujar el borde de un rectángulo
 */
void oledDrawRect(int x, int y, int w, int h) {
    tft_draw_rect((tft_coord_t)x, (tft_coord_t)y,
                  (tft_coord_t)w, (tft_coord_t)h,
                  g_fg_color);
}

/**
 * oledHLine — Línea horizontal
 */
void oledHLine(int x, int y, int len) {
    tft_draw_hline((tft_coord_t)x, (tft_coord_t)y,
                   (tft_coord_t)len, g_fg_color);
}

/**
 * oledVLine — Línea vertical
 */
void oledVLine(int x, int y, int len) {
    tft_draw_vline((tft_coord_t)x, (tft_coord_t)y,
                   (tft_coord_t)len, g_fg_color);
}

/**
 * oledInvert — Invertir los colores de un área rectangular
 *
 * [MOD-D4] Original: XOR de todos los bits del área en el buffer.
 * Trezor usa esto para resaltar el elemento de menú seleccionado
 * (texto negro sobre fondo blanco en vez del normal).
 *
 * Nuevo: intercambiar temporalmente fg/bg y redibujar el área.
 * LIMITACIÓN: no podemos leer el contenido actual del TFT,
 * así que usamos un swap de colores predecible.
 * En layout.c debemos llamar oledSetFgColor/oledSetBgColor
 * explícitamente para el elemento seleccionado en vez de usar invert.
 */
void oledInvert(int x, int y, int w, int h) {
    /* Dibujar rectángulo con colores intercambiados */
    tft_fill_rect((tft_coord_t)x, (tft_coord_t)y,
                  (tft_coord_t)w, (tft_coord_t)h,
                  g_bg_color);   /* Fondo donde estaba el primer plano */

    /* Nota: el texto encima no se reinvierte automáticamente.
     * layout.c debe redibujar el texto con colores invertidos
     * después de llamar oledInvert(). */
}


/* =========================================================
 * TEXTO
 * ========================================================= */

/**
 * oledDrawString — Dibujar string en posición (x, y)
 *
 * [MOD-D4] Original: usaba una fuente bitmap fija de 6×8 px
 * del archivo font.h del Trezor. Escribía directamente al buffer.
 *
 * Nuevo: llama a tft_draw_string() con nuestra fuente 5×7
 * escalada según el parámetro size.
 *
 * COORDENADAS: las coordenadas de layout.c están calculadas
 * para 128×64. En 480×320 el texto aparecerá en proporciones
 * similares porque OLED_WIDTH y OLED_HEIGHT ahora valen 480/320.
 */
void oledDrawString(int x, int y, const char *str, uint8_t size) {
    if (!str) return;
    if (size == 0) size = DEFAULT_FONT_SIZE;
    tft_draw_string((tft_coord_t)x, (tft_coord_t)y,
                    str, g_fg_color, g_bg_color, size);
}

/**
 * oledDrawStringCenter — String centrado horizontalmente
 *
 * [MOD-D4] Original: calculaba (OLED_WIDTH - ancho_str) / 2
 * Nuevo: tft_draw_string_centered() que hace lo mismo para 480px
 */
void oledDrawStringCenter(int y, const char *str, uint8_t size) {
    if (!str) return;
    if (size == 0) size = DEFAULT_FONT_SIZE;
    tft_draw_string_centered((tft_coord_t)y, str,
                              g_fg_color, g_bg_color, size);
}

/**
 * oledDrawStringRight — String alineado a la derecha
 *
 * El string termina en la coordenada x dada.
 */
void oledDrawStringRight(int x, int y, const char *str, uint8_t size) {
    if (!str) return;
    if (size == 0) size = DEFAULT_FONT_SIZE;

    /* Calcular posición X para que el string termine en 'x' */
    uint16_t str_w = tft_string_width(str, size);
    int start_x = x - (int)str_w;
    if (start_x < 0) start_x = 0;

    tft_draw_string((tft_coord_t)start_x, (tft_coord_t)y,
                    str, g_fg_color, g_bg_color, size);
}

/**
 * oledStringWidth — Ancho en píxeles de un string
 *
 * [MOD-D4] Original: contaba los caracteres × 6 (fuente 6px)
 * Nuevo: tft_string_width() que usa nuestra fuente 5×7 × size
 */
int oledStringWidth(const char *str, uint8_t size) {
    if (!str) return 0;
    if (size == 0) size = DEFAULT_FONT_SIZE;
    return (int)tft_string_width(str, size);
}


/* =========================================================
 * CONTROL DEL DISPLAY
 * ========================================================= */

/**
 * oledSetVisible — Encender/apagar el display
 *
 * [MOD-D4] Original: comando SSD1306 Display ON (0xAF) / OFF (0xAE)
 * Nuevo: controla el backlight del TFT (pin BL = PB1)
 */
void oledSetVisible(bool visible) {
    tft_set_brightness(visible ? 255 : 0);
}

/**
 * oledSetBrightness — Control de brillo
 * [MOD-D5] Nueva función sin equivalente en el OLED monocromático
 */
void oledSetBrightness(uint8_t brightness) {
    tft_set_brightness(brightness);
}


/* =========================================================
 * CONTROL DE COLORES
 * ========================================================= */

/**
 * oledSetFgColor — Cambiar color de primer plano
 * [MOD-D5] Nueva función — el OLED no tenía color
 *
 * Uso típico en layout.c:
 *   oledSetFgColor(WALCORG_SUCCESS);
 *   oledDrawString(0, 0, "Confirmado", 2);
 *   oledResetColors();
 */
void oledSetFgColor(tft_color_t color) {
    g_fg_color = color;
}

/**
 * oledSetBgColor — Cambiar color de fondo
 * [MOD-D5] Nueva función
 */
void oledSetBgColor(tft_color_t color) {
    g_bg_color = color;
}

/**
 * oledResetColors — Restaurar colores por defecto
 *
 * Colores del tema WalCorg:
 *   Primer plano: blanco claro (WALCORG_TEXT = RGB565(230,230,230))
 *   Fondo: negro oscuro (WALCORG_BG = RGB565(18,18,18))
 */
void oledResetColors(void) {
    g_fg_color = WALCORG_TEXT;
    g_bg_color = WALCORG_BG;
}
