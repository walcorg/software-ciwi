/**
 * @file    pin_delay.c
 * @brief   Rate-limiting de PIN — WalCorg Fase 6
 * @license GNU GPL v3
 */

#include "pin_delay.h"
#include "storage_hal.h"
#include "display_tft.h"
#include "ui_theme.h"
#include "ui_components.h"
#include "uart_debug.h"
#include "stm32f4xx_hal.h"
#include <stdio.h>

uint32_t pin_delay_calculate_ms(uint8_t attempt_number) {
    /* Intentos 1-2: sin delay */
    if (attempt_number <= 2) return 0;

    /* Intento 3 en adelante: 2^(n-2) segundos
     * Intento 3 → 2^1 = 2s  (Trezor real usa ~1s, redondeamos a potencia de 2)
     * Intento 4 → 2^2 = 4s
     * ...
     * Intento 10 → 2^8 = 256s (~4.3 min) */
    uint8_t exponent = attempt_number - 2;
    if (exponent > 8) exponent = 8;  /* Cap en intento 10 */

    uint32_t delay_s = 1UL << exponent;  /* 2^exponent */
    return delay_s * 1000UL;
}

void pin_delay_enforce(uint8_t attempt_number) {
    uint32_t delay_ms = pin_delay_calculate_ms(attempt_number);

    if (delay_ms == 0) return;  /* Sin delay necesario */

    uint32_t delay_s = delay_ms / 1000;
    DEBUG_PRINTF("[PINDELAY] Intento %d: esperando %lu segundos...\r\n",
        attempt_number, delay_s);

    /* Mostrar cuenta regresiva en pantalla */
    tft_clear(THEME_BG);
    ui_topbar("PIN Bloqueado Temporalmente", '#', THEME_ERROR_DIM);

    tft_draw_string_centered(LAYOUT_CONTENT_Y + 20,
        "Demasiados intentos fallidos.",
        THEME_ERROR, THEME_BG, FONT_BODY);

    tft_draw_string_centered(LAYOUT_CONTENT_Y + 50,
        "Espera antes de reintentar:",
        THEME_TEXT_SECONDARY, THEME_BG, FONT_SMALL);

    uint32_t start = HAL_GetTick();
    uint32_t end   = start + delay_ms;

    while (HAL_GetTick() < end) {
        uint32_t remaining_ms = end - HAL_GetTick();
        uint32_t remaining_s  = (remaining_ms + 999) / 1000;  /* Redondear arriba */

        /* Actualizar el contador grande cada segundo */
        char timer_str[16];
        if (remaining_s >= 60) {
            snprintf(timer_str, sizeof(timer_str), "%lu:%02lu",
                remaining_s / 60, remaining_s % 60);
        } else {
            snprintf(timer_str, sizeof(timer_str), "%lu s", remaining_s);
        }

        tft_fill_rect(0, LAYOUT_CONTENT_Y + 80,
                      tft_get_width(), FONT_H_TITLE + 10, THEME_BG);
        tft_draw_string_centered(LAYOUT_CONTENT_Y + 80,
            timer_str, THEME_ERROR, THEME_BG, FONT_TITLE);

        /* Barra de progreso del countdown */
        uint8_t pct = (uint8_t)(100 - (remaining_ms * 100 / delay_ms));
        ui_progress_bar(LAYOUT_MARGIN_X, LAYOUT_CONTENT_Y + 130,
                        LAYOUT_INNER_W, pct, THEME_ERROR, false);

        HAL_Delay(200);  /* Actualizar UI cada 200ms (suficiente para legibilidad) */
    }

    DEBUG_PRINT("[PINDELAY] Delay completado. Intento permitido.\r\n");
}

uint32_t pin_delay_total_lockout_seconds(void) {
    uint32_t total = 0;
    for (uint8_t i = 1; i <= STORAGE_PIN_MAX_FAILS; i++) {
        total += pin_delay_calculate_ms(i) / 1000;
    }
    return total;
}
