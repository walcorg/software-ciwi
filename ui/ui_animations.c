/**
 * @file    ui_animations.c
 * @brief   Implementación de animaciones — WALCORG Fase 4
 *
 * Las animaciones usan exclusivamente operaciones de fill_rect
 * y draw_circle que ya están optimizadas en display_tft.c.
 * No se requiere framebuffer adicional.
 *
 * @license GNU GPL v3
 */

#include "ui_animations.h"
#include "display_tft.h"
#include "stm32f4xx_hal.h"

/* =========================================================
 * FADE IN / OUT
 *
 * Simulamos fade con una serie de rectángulos de color de
 * fondo con alpha creciente o decreciente. Dado que el TFT
 * no soporta transparencia real, usamos un truco:
 *
 * FADE IN:
 *   Dibujamos el contenido ANTES de llamar anim_fade_in().
 *   La animación superpone franjas del color de fondo que
 *   se van "levantando" de abajo hacia arriba, revelando
 *   el contenido debajo. Simula una apertura de cortina.
 *
 * FADE OUT:
 *   Cubrimos progresivamente la pantalla con el color de fondo
 *   usando franjas horizontales que se expanden desde el centro.
 * ========================================================= */

/**
 * anim_fade_in — Revelar contenido con efecto de cortina
 *
 * Barre franjas horizontales del color de fondo de abajo hacia
 * arriba, revelando el contenido previamente dibujado.
 *
 * Steps: más pasos = más suave pero más lento.
 * Con 16 franjas y 300ms → 18ms por franja.
 */
void anim_fade_in(uint32_t duration_ms) {
    int steps = 16;
    int delay = (int)(duration_ms / steps);
    if (delay < 1) delay = 1;

    int screen_h = (int)tft_get_height();
    int screen_w = (int)tft_get_width();
    int strip_h  = screen_h / steps;

    /* Cubrir toda la pantalla con el color de fondo inicialmente */
    /* (el contenido ya está dibujado debajo) */
    /* Animar levantando la "cortina" de abajo hacia arriba */
    for (int i = steps; i >= 0; i--) {
        int cover_h = i * strip_h;
        if (cover_h > 0) {
            tft_fill_rect(0, screen_h - cover_h,
                          screen_w, cover_h, THEME_BG);
        }
        if (delay > 0) HAL_Delay((uint32_t)delay);
    }
}

/**
 * anim_fade_out — Cubrir pantalla con efecto de cortina
 *
 * Barre franjas del color de fondo de arriba hacia abajo,
 * cubriendo el contenido actual.
 */
void anim_fade_out(uint32_t duration_ms) {
    int steps    = 16;
    int delay    = (int)(duration_ms / steps);
    if (delay < 1) delay = 1;

    int screen_h = (int)tft_get_height();
    int screen_w = (int)tft_get_width();
    int strip_h  = screen_h / steps + 1;

    for (int i = 1; i <= steps; i++) {
        int cover_h = i * strip_h;
        if (cover_h > screen_h) cover_h = screen_h;
        tft_fill_rect(0, 0, screen_w, cover_h, THEME_BG);
        if (delay > 0) HAL_Delay((uint32_t)delay);
    }
}


/* =========================================================
 * SLIDE HORIZONTAL
 *
 * Simula el deslizamiento entre pantallas usando franjas
 * verticales. No requiere buffer de la pantalla anterior.
 *
 * Para SLIDE IN RIGHT (entrar desde la derecha):
 *   Barre franjas verticales de derecha a izquierda,
 *   limpiando la pantalla para que el llamador la redibuje.
 *
 * Esto es efectivo porque el llamador ya tiene dibujada
 * la nueva pantalla ANTES de llamar la animación, o bien
 * dibuja mientras la animación avanza.
 * ========================================================= */

void anim_slide_in_right(uint32_t duration_ms) {
    int steps    = 12;
    int delay    = (int)(duration_ms / steps);
    if (delay < 1) delay = 1;

    int screen_w = (int)tft_get_width();
    int screen_h = (int)tft_get_height();
    int strip_w  = screen_w / steps + 1;

    /* Barrer de derecha a izquierda limpiando */
    for (int i = steps; i >= 0; i--) {
        int x = i * strip_w;
        int w = screen_w - x;
        if (w > 0) {
            tft_fill_rect(x, 0, w, screen_h, THEME_BG);
        }
        if (delay > 0) HAL_Delay((uint32_t)delay);
    }
}

void anim_slide_in_left(uint32_t duration_ms) {
    int steps    = 12;
    int delay    = (int)(duration_ms / steps);
    if (delay < 1) delay = 1;

    int screen_w = (int)tft_get_width();
    int screen_h = (int)tft_get_height();
    int strip_w  = screen_w / steps + 1;

    /* Barrer de izquierda a derecha limpiando */
    for (int i = 0; i <= steps; i++) {
        int w = i * strip_w;
        if (w > screen_w) w = screen_w;
        if (w > 0) {
            tft_fill_rect(0, 0, w, screen_h, THEME_BG);
        }
        if (delay > 0) HAL_Delay((uint32_t)delay);
    }
}


/* =========================================================
 * EFECTOS PUNTUALES
 * ========================================================= */

/**
 * anim_pop — Efecto de confirmación visual
 *
 * Dibuja círculos concéntricos que se expanden desde el centro,
 * simulando una onda de confirmación.
 */
void anim_pop(tft_coord_t cx, tft_coord_t cy,
              tft_coord_t r, tft_color_t color) {
    int steps = 8;
    int delay = 25;   /* 25ms × 8 = 200ms total */

    for (int i = 0; i < steps; i++) {
        tft_coord_t cur_r = (tft_coord_t)(r * (i + 1) / steps);

        /* Dibujar anillo (círculo exterior menos interior) */
        tft_draw_circle(cx, cy, cur_r, color);

        /* Atenuar el color del ring anterior */
        if (i > 0) {
            tft_coord_t prev_r = (tft_coord_t)(r * i / steps);
            tft_draw_circle(cx, cy, prev_r, THEME_BG);
        }

        HAL_Delay((uint32_t)delay);
    }

    /* Limpiar el último ring */
    tft_draw_circle(cx, cy, r, THEME_BG);
}

/**
 * anim_blink_area — Parpadeo de área para alertas
 *
 * Alterna el color del área entre el color de destaque y
 * el color de fondo, 'times' veces.
 */
void anim_blink_area(tft_coord_t x, tft_coord_t y,
                     tft_coord_t w, tft_coord_t h,
                     tft_color_t color, uint8_t times) {
    for (int i = 0; i < times; i++) {
        /* Encender */
        tft_fill_rect(x, y, w, h, color);
        HAL_Delay(120);
        /* Apagar */
        tft_fill_rect(x, y, w, h, THEME_BG);
        HAL_Delay(120);
    }
}

/**
 * anim_success_check — Check mark animado
 *
 * Dibuja un check mark (✓) de forma animada:
 *   1. Círculo de fondo que crece
 *   2. Trazo del check de izquierda a derecha
 */
void anim_success_check(tft_coord_t cx, tft_coord_t cy,
                        tft_color_t color) {
    int r_max = 35;

    /* 1. Círculo de fondo que crece */
    for (int r = 4; r <= r_max; r += 4) {
        tft_fill_circle(cx, cy, (tft_coord_t)r, THEME_SUCCESS_DIM);
        HAL_Delay(15);
    }
    tft_fill_circle(cx, cy, (tft_coord_t)r_max, THEME_SUCCESS_DIM);

    /* 2. Dibujar el check mark en 2 trazos:
     *    Trazo 1: pata izquierda del check (↘)
     *    Trazo 2: pata derecha del check (↗ larga) */

    /* Coordenadas del check dentro del círculo de r=35:
     *   Inicio:    (cx-18, cy)
     *   Punto medio: (cx-6,  cy+12)
     *   Final:     (cx+16, cy-10) */

    /* Trazo 1: inicio → punto medio */
    int x0 = cx - 18, y0 = cy;
    int x1 = cx - 6,  y1 = cy + 12;
    int steps1 = 10;
    for (int i = 0; i <= steps1; i++) {
        int px = x0 + (x1 - x0) * i / steps1;
        int py = y0 + (y1 - y0) * i / steps1;
        /* Trazo grueso: dibujar 3 píxeles de ancho */
        for (int d = -1; d <= 1; d++) {
            tft_draw_pixel((tft_coord_t)px, (tft_coord_t)(py + d), color);
            tft_draw_pixel((tft_coord_t)(px + d), (tft_coord_t)py, color);
        }
        HAL_Delay(12);
    }

    /* Trazo 2: punto medio → final */
    int x2 = cx + 16, y2 = cy - 10;
    int steps2 = 16;
    for (int i = 0; i <= steps2; i++) {
        int px = x1 + (x2 - x1) * i / steps2;
        int py = y1 + (y2 - y1) * i / steps2;
        for (int d = -1; d <= 1; d++) {
            tft_draw_pixel((tft_coord_t)px, (tft_coord_t)(py + d), color);
            tft_draw_pixel((tft_coord_t)(px + d), (tft_coord_t)py, color);
        }
        HAL_Delay(10);
    }
}
