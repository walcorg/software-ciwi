/**
 * @file    clickwheel_example.c
 * @brief   Ejemplo de integración del driver en el main loop
 *
 * Este archivo muestra cómo integrar el driver en el firmware
 * principal. En el contexto de Trezor legacy, este código va
 * dentro de legacy/firmware/main.c y los eventos se mapean
 * a las llamadas de buttons.c
 *
 * NO es un archivo de producción — es referencia de integración.
 */

#include "clickwheel.h"
#include "stm32f4xx_hal.h"
#include <stdio.h>   /* solo para debug por UART */

/* =========================================================
 * INTEGRACIÓN CON TREZOR LEGACY
 *
 * En trezor/legacy/firmware/buttons.h existe:
 *   #define BTN_YES    (1 << 0)
 *   #define BTN_NO     (1 << 1)
 *   uint16_t button_read(void);
 *
 * Mapeo propuesto:
 *   CW_EVENT_SCROLL_UP    → navegar arriba en menú
 *   CW_EVENT_SCROLL_DOWN  → navegar abajo en menú
 *   CW_EVENT_CLICK        → BTN_YES (confirmar)
 *   CW_EVENT_LONG_PRESS   → BTN_NO  (cancelar / volver)
 *   CW_EVENT_DOUBLE_CLICK → acción especial (ej: home)
 * ========================================================= */

/* Estado global del driver */
static cw_state_t g_cw_state;

/* Variable compartida con buttons.c para inyectar eventos */
static volatile cw_event_t g_pending_event = CW_EVENT_NONE;

/**
 * @brief Inicialización — llamar desde main() después de
 *        SystemClock_Config() y antes del loop principal.
 */
void clickwheel_setup(void) {
    bool ok = cw_init(&g_cw_state);

    if (!ok) {
        /*
         * El AS5600 no respondió. Causas comunes:
         *
         *  1. Pull-ups faltantes:
         *     Coloca 4.7kΩ entre PB6 y 3.3V
         *     Coloca 4.7kΩ entre PB7 y 3.3V
         *
         *  2. Sin imán sobre el AS5600:
         *     El IC requiere un imán diametral sobre el eje.
         *     Sin imán, el registro STATUS.MD = 0 y el IC
         *     puede no responder correctamente.
         *
         *  3. Dirección I2C incorrecta:
         *     AS5600 siempre usa 0x36 (no configurable).
         *     Verificar con i2c_scan() abajo.
         *
         *  4. Cable SDA/SCL invertido.
         */
        /* En producción: mostrar error en pantalla */
        /* En desarrollo: loop infinito para capturar con debugger */
        while (1) { __NOP(); }
    }
}

/**
 * @brief Polling — llamar cada 10ms desde el main loop o un timer.
 *
 * En Trezor legacy, esto va dentro del loop de:
 *   legacy/firmware/main.c  →  for(;;) { ... }
 */
void clickwheel_task(void) {
    cw_event_t event = cw_poll(&g_cw_state);

    if (event == CW_EVENT_NONE) return;

    /* Guardar para que buttons.c lo consuma en el siguiente tick */
    g_pending_event = event;

    /* Debug por UART (remover en PRODUCTION=1) */
    /* printf("[CW] %s\n", cw_event_name(event)); */

    /* Mapeo directo a acciones de UI */
    switch (event) {
        case CW_EVENT_SCROLL_UP:
            /* → ui_navigate(NAV_UP) */
            break;

        case CW_EVENT_SCROLL_DOWN:
            /* → ui_navigate(NAV_DOWN) */
            break;

        case CW_EVENT_CLICK:
            /* → ui_confirm() o BTN_YES en Trezor */
            break;

        case CW_EVENT_LONG_PRESS:
            /* → ui_cancel() o BTN_NO en Trezor */
            break;

        case CW_EVENT_DOUBLE_CLICK:
            /* → ui_home() o acción especial */
            break;

        default:
            break;
    }
}

/**
 * @brief Ejemplo de main loop completo.
 *
 * En el firmware de Trezor legacy, el loop principal está en
 * legacy/firmware/main.c. Este ejemplo muestra el patrón.
 */
void example_main_loop(void) {
    /* 1. Inicialización del sistema (ya existente en Trezor) */
    HAL_Init();
    /* SystemClock_Config();  ← ya existe en stm32.c de Trezor */

    /* 2. Inicializar click-wheel */
    clickwheel_setup();

    /* 3. Loop principal */
    uint32_t last_cw_poll = 0;

    for (;;) {
        uint32_t now = HAL_GetTick();

        /* Polling del click-wheel cada 10ms */
        if ((now - last_cw_poll) >= 10) {
            last_cw_poll = now;
            clickwheel_task();
        }

        /* Aquí va el resto del loop de Trezor:
         *   - USB message handling
         *   - Display refresh
         *   - Timeout de PIN
         *   etc. */
    }
}

/* =========================================================
 * UTILIDAD DE DIAGNÓSTICO — I2C Scanner
 *
 * Llama esta función una vez al boot durante desarrollo
 * para verificar qué dispositivos hay en el bus I2C.
 * Imprime por UART: "Found device at 0x36" para el AS5600.
 *
 * REMOVER en build de producción (PRODUCTION=1).
 * ========================================================= */
#ifndef PRODUCTION
void i2c_scan_debug(I2C_HandleTypeDef *hi2c) {
    /* Esta función requiere que UART esté inicializado */
    for (uint8_t addr = 1; addr < 128; addr++) {
        HAL_StatusTypeDef result = HAL_I2C_IsDeviceReady(
            hi2c,
            (uint16_t)(addr << 1),
            1,
            5
        );
        if (result == HAL_OK) {
            /* printf("Found I2C device at 0x%02X\n", addr); */
            (void)addr; /* silencia warning si printf está comentado */
        }
    }
}
#endif /* PRODUCTION */
