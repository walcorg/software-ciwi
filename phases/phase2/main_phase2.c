/**
 * @file    main_phase2.c
 * @brief   Main integrador Fase 2 — WalCorg Cold Wallet
 *
 * Une todo lo de Fase 1 + el nuevo driver de display TFT.
 *
 * SECUENCIA DE ARRANQUE:
 *   stm32_init()         → FPU, clock 168MHz, UART, MPU
 *   rng_init()           → Hardware RNG (ya validado en Fase 1)
 *   tft_init()           → Display ILI9488 via SPI1
 *   touch_init()         → Touch XPT2046 via SPI1
 *   display_run_all_tests() → Suite de 6 tests visuales
 *   main_loop_phase2()   → Loop con heartbeat + touch polling
 *
 * CRITERIO DE ÉXITO:
 *   ✓ El monitor serial muestra "FASE 2 COMPLETADA"
 *   ✓ Los 6 tests visuales se muestran correctamente en pantalla
 *   ✓ La pantalla final dice "Listo para Fase 3"
 *   ✓ Tocar la pantalla imprime coordenadas por UART
 *
 * @license GNU GPL v3
 */

#include "stm32f4xx_hal.h"
#include <stdio.h>    /* [v0.2] snprintf */
#include "clock_config.h"
#include "uart_debug.h"
#include "rng_test.h"
#include "display_tft.h"
#include "touch_xpt2046.h"
#include "display_test.h"

/* LED de heartbeat */
#define LED_PORT    GPIOC
#define LED_PIN     GPIO_PIN_13
#define LED_TOGGLE() HAL_GPIO_TogglePin(LED_PORT, LED_PIN)

/* Prototipos privados */
extern void stm32_init(void);
static void led_init(void);
static void main_loop_phase2(void);
static void print_banner_phase2(void);


/* =========================================================
 * MAIN
 * ========================================================= */
int main(void) {

    /* 1. Inicialización base (de Fase 1) */
    stm32_init();
    led_init();
    print_banner_phase2();

    /* 2. Verificar RNG (heredado de Fase 1) */
    DEBUG_PRINT("[FASE2] Verificando RNG...\r\n");
    if (!rng_init()) {
        DEBUG_PRINT("[FASE2] ERROR: RNG no disponible.\r\n");
    } else {
        DEBUG_PRINT("[FASE2] RNG OK.\r\n");
    }

    /* 3. Inicializar display TFT
     *    Si falla, el firmware continúa pero sin display.
     *    La UART seguirá funcionando para debug. */
    DEBUG_PRINT("[FASE2] Inicializando display TFT 3.5\" ILI9488...\r\n");
    if (!tft_init(TFT_ROTATION_90)) {  /* Landscape: 480x320 */
        DEBUG_PRINT("[FASE2] ERROR: Display no respondio.\r\n");
        DEBUG_PRINT("[FASE2] Verificar:\r\n");
        DEBUG_PRINT("[FASE2]   - PA5/PA6/PA7 conectados (SPI1)\r\n");
        DEBUG_PRINT("[FASE2]   - PC4=CS, PC5=DC, PB0=RST, PB1=BL\r\n");
        DEBUG_PRINT("[FASE2]   - Alimentacion 3.3V al display\r\n");
        DEBUG_PRINT("[FASE2] Continuando sin display...\r\n");
    } else {
        tft_print_status();

        /* 4. Inicializar touch */
        DEBUG_PRINT("[FASE2] Inicializando touch XPT2046...\r\n");
        touch_init();
        touch_print_status();

        /* 5. Ejecutar suite de tests visuales */
        DEBUG_PRINT("[FASE2] Ejecutando tests visuales...\r\n");
        display_run_all_tests();

        DEBUG_PRINT("\r\n");
        DEBUG_PRINT("############################################\r\n");
        DEBUG_PRINT("##                                        ##\r\n");
        DEBUG_PRINT("##   FASE 2 COMPLETADA EXITOSAMENTE       ##\r\n");
        DEBUG_PRINT("##   Display TFT funcional 480x320        ##\r\n");
        DEBUG_PRINT("##   Listo para Fase 3 (Click-wheel)      ##\r\n");
        DEBUG_PRINT("##                                        ##\r\n");
        DEBUG_PRINT("############################################\r\n\r\n");
    }

    /* 6. Loop principal */
    main_loop_phase2();
    return 0;
}


/* =========================================================
 * LOOP PRINCIPAL FASE 2
 * ========================================================= */
static void main_loop_phase2(void) {

    DEBUG_PRINT("[LOOP2] Entrando al loop principal de Fase 2.\r\n");
    DEBUG_PRINT("[LOOP2] Toca la pantalla para ver las coordenadas.\r\n");

    uint32_t last_heartbeat = 0;
    uint32_t last_status    = 0;
    uint32_t touch_count    = 0;

    while (1) {
        uint32_t now = HAL_GetTick();

        /* LED heartbeat cada 1 segundo */
        if ((now - last_heartbeat) >= 1000) {
            last_heartbeat = now;
            LED_TOGGLE();
        }

        /* Polling del touch — mostrar coordenadas por UART */
        touch_point_t tp;
        if (touch_read(&tp) && tp.valid) {
            touch_count++;
            DEBUG_PRINTF("[TOUCH] Toque #%lu: x=%d y=%d presion=%u\r\n",
                touch_count, tp.x, tp.y, tp.pressure);

            /* Mostrar punto tocado en el display */
            tft_fill_circle(tp.x, tp.y, 8, WALCORG_PRIMARY);
            /* Etiqueta de coordenadas */
            char coord_str[24];
            snprintf(coord_str, sizeof(coord_str), "(%d,%d)", tp.x, tp.y);
            tft_draw_string(tp.x + 10, tp.y - 8,
                            coord_str, WALCORG_TEXT, WALCORG_BG, 1);
        }

        /* Reporte de estado cada 30 segundos */
        if ((now - last_status) >= 30000) {
            last_status = now;
            DEBUG_PRINTF("[STATUS2] Uptime: %lu s | Toques: %lu\r\n",
                now / 1000, touch_count);
        }
    }
}


/* =========================================================
 * UTILIDADES
 * ========================================================= */
static void led_init(void) {
    __HAL_RCC_GPIOC_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin   = LED_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED_PORT, &gpio);
}

static void print_banner_phase2(void) {
    DEBUG_PRINT("\r\n");
    DEBUG_PRINT("============================================\r\n");
    DEBUG_PRINT("  WalCorg Cold Wallet — Fase 2\r\n");
    DEBUG_PRINT("  Driver TFT ILI9488 + Touch XPT2046\r\n");
    DEBUG_PRINTF("  Build: %s %s\r\n", __DATE__, __TIME__);
    DEBUG_PRINT("  STM32F405RGT6 @ 168MHz\r\n");
    DEBUG_PRINT("============================================\r\n\r\n");
}
