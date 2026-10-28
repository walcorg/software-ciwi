/**
 * @file    display_phase4.c  → extiende display.c de Fase 2
 * @brief   Extensiones del display para Fase 4 — WALCORG
 *
 * DIFERENCIAS RESPECTO A display.c DE FASE 2:
 *
 *   [MOD-D6] oledSetBrightness() ahora llama a backlight_set()
 *            real via PWM (TIM3_CH4) en vez de GPIO ON/OFF.
 *
 *   [MOD-D7] oledInvert() mejorado: guarda y restaura colores
 *            correctamente para el ítem seleccionado del menú.
 *
 *   [MOD-D8] Añadida oledDrawStringColor() — permite especificar
 *            color directamente sin cambiar los globales g_fg/g_bg.
 *
 *   [MOD-D9] oledRefresh() ahora también llama backlight_activity()
 *            para que cualquier actualización de pantalla resetee
 *            el timer de auto-dim.
 *
 * ESTRATEGIA:
 *   Este archivo NO reemplaza display.c — lo complementa.
 *   display.c implementa los wrappers básicos oledXxx → tftXxx.
 *   display_phase4.c reemplaza/extiende las funciones que necesitan
 *   comportamiento más sofisticado en Fase 4.
 *
 *   En el Makefile, display_phase4.c se compila en lugar de display.c
 *   para las funciones que sobreescribe, usando el mismo nombre de
 *   símbolo pero con __attribute__((alias)) o simplemente definiendo
 *   la función en este archivo y eliminándola de display.c.
 *
 * IMPLEMENTACIÓN PRÁCTICA:
 *   Para simplicidad, este archivo define las funciones extendidas
 *   con un sufijo _v2, y display.h usa #define para redirigir.
 *   O bien: compilar SOLO display_phase4.c y NO display.c en Fase 4.
 *
 * @license GNU GPL v3
 */

#include "display.h"
#include "display_tft.h"
#include "backlight.h"
#include "ui_theme.h"
#include "uart_debug.h"
#include <string.h>

/* =========================================================
 * ESTADO DE COLORES (redefinido aquí para Fase 4)
 *
 * En Fase 4 los colores por defecto vienen de ui_theme.h
 * en vez de los valores hardcodeados de Fase 2.
 * ========================================================= */

tft_color_t g_fg_color = THEME_TEXT_PRIMARY;
tft_color_t g_bg_color = THEME_BG;

/* =========================================================
 * [MOD-D6] CONTROL DE BRILLO — PWM REAL
 * ========================================================= */

/**
 * oledSetBrightness — Control de brillo via PWM
 *
 * [MOD-D6] FASE 2: controlaba el pin GPIO BL directamente (ON/OFF).
 *          FASE 4: llama backlight_set() que usa TIM3_CH4 PWM.
 *                  Permite 256 niveles de brillo en vez de 2.
 *
 * @param brightness  0=apagado, 255=máximo
 */
void oledSetBrightness(uint8_t brightness) {
    /* [MOD-D6] En Fase 2 esto era: TFT_BL_ON() / TFT_BL_OFF()
     * Ahora usamos el PWM real del backlight */
    backlight_set(brightness);
}

/**
 * oledSetVisible — Encender/apagar display con fade
 *
 * [MOD-D6] En Fase 2: controlaba GPIO BL directamente.
 *          En Fase 4: usa backlight_on() / backlight_off()
 *                     que hacen un fade suave (300ms).
 */
void oledSetVisible(bool visible) {
    if (visible) {
        backlight_on();
    } else {
        backlight_off();
    }
}

/* =========================================================
 * [MOD-D9] oledRefresh — Notificar actividad al backlight
 * ========================================================= */

/**
 * oledRefresh — Actualizar el display y notificar actividad
 *
 * [MOD-D9] En Fase 2: NO-OP (el TFT escribe directamente).
 *          En Fase 4: sigue siendo NO-OP para escritura al TFT,
 *          PERO ahora también notifica actividad al backlight.
 *          Esto asegura que cualquier actualización de pantalla
 *          resetea el timer de auto-dim — el backlight no se
 *          apagará mientras la pantalla esté activa.
 */
void oledRefresh(void) {
    /* NO-OP para escritura TFT (el TFT escribe directamente) */
    /* [MOD-D9] Notificar actividad al backlight */
    backlight_activity();
}

/* =========================================================
 * [MOD-D7] oledInvert — Inversión mejorada de colores
 * ========================================================= */

/**
 * oledInvert — Invertir colores de un área
 *
 * [MOD-D7] FASE 2: simplemente rellenaba el área con g_bg_color.
 *          FASE 4: gestiona correctamente los colores para que
 *                  el texto dentro del área aparezca con colores
 *                  invertidos (texto oscuro sobre fondo claro,
 *                  o texto del tema sobre fondo de selección).
 *
 * Trezor usa oledInvert() principalmente para:
 *   a) Resaltar el ítem seleccionado en menús
 *   b) Indicar el campo activo en la entrada de PIN
 *
 * Estrategia: guardar colores actuales, intercambiarlos,
 * y restaurarlos después del área invertida.
 */
void oledInvert(int x, int y, int w, int h) {
    /* Guardar colores actuales */
    tft_color_t saved_fg = g_fg_color;
    tft_color_t saved_bg = g_bg_color;

    /* Intercambiar fg y bg para el área */
    g_fg_color = THEME_SELECTED_BG;
    g_bg_color = THEME_TEXT_PRIMARY;

    /* Rellenar el área con el color de selección */
    tft_fill_rect((tft_coord_t)x, (tft_coord_t)y,
                  (tft_coord_t)w, (tft_coord_t)h,
                  THEME_SELECTED_BG);

    /* Barra lateral indicadora */
    tft_fill_rect((tft_coord_t)x, (tft_coord_t)y,
                  4, (tft_coord_t)h, THEME_SELECTED_BAR);

    /* Restaurar colores */
    g_fg_color = saved_fg;
    g_bg_color = saved_bg;
}

/* =========================================================
 * [MOD-D8] NUEVAS FUNCIONES DE COLOR
 * ========================================================= */

/**
 * oledDrawStringColor — Dibujar string con color específico
 *
 * [MOD-D8] Nueva función — no existe en el display.c original.
 * Permite especificar el color directamente sin modificar
 * los globales g_fg_color / g_bg_color.
 *
 * Más seguro que oledSetFgColor() + oledDrawString() + oledResetColors()
 * porque es atómico — no hay estado intermedio.
 *
 * @param x, y   Posición
 * @param str    String a dibujar
 * @param fg     Color del texto
 * @param bg     Color del fondo
 * @param size   Tamaño de fuente
 */
void oledDrawStringColor(int x, int y, const char *str,
                          tft_color_t fg, tft_color_t bg,
                          uint8_t size) {
    if (!str) return;
    if (size == 0) size = FONT_BODY;
    tft_draw_string((tft_coord_t)x, (tft_coord_t)y,
                    str, fg, bg, size);
}

/**
 * oledDrawStringCenterColor — String centrado con color específico
 * [MOD-D8] Nueva función
 */
void oledDrawStringCenterColor(int y, const char *str,
                                 tft_color_t fg, tft_color_t bg,
                                 uint8_t size) {
    if (!str) return;
    if (size == 0) size = FONT_BODY;
    tft_draw_string_centered((tft_coord_t)y, str, fg, bg, size);
}

/* =========================================================
 * FUNCIONES HEREDADAS DE display.c (Fase 2)
 * Redefinidas aquí con el tema actualizado de Fase 4.
 * ========================================================= */

/**
 * oledInit — Inicializar display con tema Fase 4
 *
 * [MOD-D6] Ahora también inicializa el backlight PWM.
 */
void oledInit(void) {
    DEBUG_PRINT("[DISP-F4] oledInit() con tema Fase 4 + backlight PWM\r\n");

    /* Inicializar TFT en landscape */
    tft_init(TFT_ROTATION_90);

    /* [MOD-D6] Inicializar backlight PWM (en Fase 2 era solo GPIO) */
    backlight_init();

    /* Aplicar colores del tema Fase 4 (más oscuros que Fase 2) */
    g_fg_color = THEME_TEXT_PRIMARY;
    g_bg_color = THEME_BG;
}

/**
 * oledClear — Limpiar con color de fondo del tema
 */
void oledClear(void) {
    tft_clear(g_bg_color);
}

/* Las demás funciones (oledDrawPixel, oledFillRect, etc.)
 * se heredan sin cambios de display.c de Fase 2.
 * Solo las funciones listadas arriba cambian en Fase 4. */

/**
 * oledResetColors — Restaurar colores del tema Fase 4
 *
 * [MOD-D6] Usa los colores de THEME_* en vez de los
 * valores hardcodeados de Fase 2.
 */
void oledResetColors(void) {
    g_fg_color = THEME_TEXT_PRIMARY;  /* Blanco suave del tema */
    g_bg_color = THEME_BG;            /* Negro azulado del tema */
}
