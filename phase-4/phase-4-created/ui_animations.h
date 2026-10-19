/**
 * @file    ui_animations.h / ui_animations.c
 * @brief   Animaciones de UI — WalCorg Fase 4
 *
 * Animaciones simples que hacen la interfaz más fluida:
 *   - Fade in/out de pantalla completa
 *   - Slide horizontal entre pantallas
 *   - Efecto de aparición de elementos
 *
 * Todas las animaciones son opcionales y degradan
 * graciosamente si se omiten — la funcionalidad es idéntica.
 *
 * RENDIMIENTO:
 *   A 10.5MHz SPI, limpiar la pantalla completa (480×320×2 bytes)
 *   toma ~29ms. Las animaciones de fade usan fill_rect parcial
 *   para ser más rápidas (no requieren limpiar toda la pantalla).
 *
 * @license GNU GPL v3
 */

#ifndef UI_ANIMATIONS_H
#define UI_ANIMATIONS_H

#include <stdint.h>
#include "ui_theme.h"
#include "display_tft.h"

/**
 * @brief Efecto de fade-in: aparece desde el color de fondo.
 *
 * Dibuja rectángulos de color de fondo con opacidad decreciente
 * sobre el contenido ya dibujado. Simula un fade-in.
 *
 * @param duration_ms  Duración total del efecto en ms
 */
void anim_fade_in(uint32_t duration_ms);

/**
 * @brief Efecto de fade-out: desaparece hacia el color de fondo.
 *
 * @param duration_ms  Duración total del efecto en ms
 */
void anim_fade_out(uint32_t duration_ms);

/**
 * @brief Slide horizontal de entrada desde la derecha.
 *
 * Útil para navegar hacia adelante en una jerarquía de menús.
 * La pantalla actual se mueve hacia la izquierda y la nueva
 * entra desde la derecha.
 *
 * Implementación simplificada: barre franjas verticales de
 * izquierda a derecha mostrando el color de fondo.
 *
 * @param duration_ms  Duración en ms
 */
void anim_slide_in_right(uint32_t duration_ms);

/**
 * @brief Slide horizontal de entrada desde la izquierda.
 *
 * Útil para navegar hacia atrás (volver).
 *
 * @param duration_ms  Duración en ms
 */
void anim_slide_in_left(uint32_t duration_ms);

/**
 * @brief Efecto de "pop" para un elemento específico.
 *
 * Hace que el elemento crezca brevemente y vuelva a su tamaño.
 * Útil para confirmar una selección.
 *
 * @param cx, cy  Centro del elemento
 * @param r       Radio inicial del efecto
 * @param color   Color del efecto
 */
void anim_pop(tft_coord_t cx, tft_coord_t cy,
              tft_coord_t r, tft_color_t color);

/**
 * @brief Efecto de parpadeo de un área.
 *
 * Hace parpadear un área con color de destaque.
 * Útil para errores o alertas.
 *
 * @param x, y, w, h  Área a hacer parpadear
 * @param color        Color del parpadeo
 * @param times        Número de parpadeos
 */
void anim_blink_area(tft_coord_t x, tft_coord_t y,
                     tft_coord_t w, tft_coord_t h,
                     tft_color_t color, uint8_t times);

/**
 * @brief Animación de "check" exitoso.
 *
 * Dibuja un check mark animado en el centro de la pantalla.
 * Usado en screen_success().
 *
 * @param cx, cy  Centro del check
 * @param color   Color del check (THEME_SUCCESS por defecto)
 */
void anim_success_check(tft_coord_t cx, tft_coord_t cy,
                        tft_color_t color);

#endif /* UI_ANIMATIONS_H */
