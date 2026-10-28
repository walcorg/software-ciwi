/**
 * @file    main_phase4.c
 * @brief   Main integrador Fase 4 — WALCORG Cold Wallet
 *
 * Une las 4 fases:
 *   Fase 1 → STM32 HAL, clock, UART, RNG
 *   Fase 2 → Display TFT ILI9488, touch XPT2046
 *   Fase 3 → Click-wheel AS5600, menús, buttons.c
 *   Fase 4 → UI tema WALCORG, animaciones, backlight PWM
 *
 * SECUENCIA DE ARRANQUE:
 *   stm32_init()           → HAL, clock 168MHz, UART, MPU
 *   rng_init()             → Hardware RNG
 *   tft_init()             → Display 480×320
 *   touch_init()           → Touch XPT2046
 *   backlight_init()       → PWM TIM3_CH4 con fade
 *   input_init()           → Click-wheel AS5600
 *   splash_screen()        → Pantalla de bienvenida animada
 *   main_wallet_loop()     → Loop principal del wallet
 *
 * CRITERIO DE ÉXITO DE FASE 4:
 *   ✓ Splash screen con animación de fade-in
 *   ✓ Menú principal con tema WALCORG (colores, tipografía)
 *   ✓ Pantallas de recibir, enviar, PIN y semilla funcionando
 *   ✓ Backlight con fade suave al encender
 *   ✓ Auto-dim después de 30s de inactividad
 *   ✓ Animaciones de transición entre pantallas
 *   ✓ Monitor serial: "FASE 4 COMPLETADA"
 *
 * @license GNU GPL v3
 */

#include "stm32f4xx_hal.h"
#include <string.h>   /* [v0.2] memset, strlen */
#include "uart_debug.h"
#include "rng_test.h"
#include "display_tft.h"
#include "touch_xpt2046.h"
#include "backlight.h"
#include "input_manager.h"
#include "ui_theme.h"
#include "ui_components.h"
#include "ui_screens.h"
#include "ui_animations.h"
#include "menu.h"

/* LED heartbeat */
#define LED_PORT     GPIOB      /* [v0.3] user LED is PB2 on the WeAct board (PC13 is KEY) */
#define LED_PIN      GPIO_PIN_2
#define LED_TOGGLE() HAL_GPIO_TogglePin(LED_PORT, LED_PIN)

/* Prototipos privados */
extern void stm32_init(void);
static void led_init(void);
static void print_banner(void);
static void splash_screen(void);
static void main_wallet_loop(void);


/* =========================================================
 * MAIN
 * ========================================================= */
int main(void) {

    /* ---- Fase 1: hardware base ---- */
    stm32_init();
    led_init();
    print_banner();

    /* RNG */
    if (!rng_init()) {
        DEBUG_PRINT("[F4] WARN: RNG no disponible.\r\n");
    }

    /* ---- Fase 2: display ---- */
    DEBUG_PRINT("[F4] Inicializando display TFT...\r\n");
    if (!tft_init(TFT_ROTATION_90)) {
        DEBUG_PRINT("[F4] ERROR FATAL: display no responde.\r\n");
        while (1) { LED_TOGGLE(); HAL_Delay(100); }
    }
    touch_init();

    /* ---- Fase 4 NEW: backlight PWM ---- */
    DEBUG_PRINT("[F4] Inicializando backlight PWM...\r\n");
    backlight_init();   /* Fade de encendido incluido */

    /* ---- Fase 3: click-wheel ---- */
    DEBUG_PRINT("[F4] Inicializando click-wheel...\r\n");
    if (!input_init()) {
        /* Mostrar error en pantalla */
        tft_clear(THEME_BG);
        ui_topbar("Error de Hardware", 0, THEME_ERROR_DIM);
        tft_draw_string_centered(140, "Click-wheel no detectado",
                                 THEME_ERROR, THEME_BG, FONT_BODY);
        tft_draw_string_centered(165, "Verificar AS5600 en PB6/PB7",
                                 THEME_TEXT_SECONDARY, THEME_BG, FONT_SMALL);
        DEBUG_PRINT("[F4] FATAL: click-wheel requerido.\r\n");
        while (1) { LED_TOGGLE(); HAL_Delay(100); }
    }

    /* ---- Fase 4 NEW: pantalla de bienvenida ---- */
    splash_screen();

    /* ---- Resumen de inicialización ---- */
    DEBUG_PRINT("\r\n");
    DEBUG_PRINT("############################################\r\n");
    DEBUG_PRINT("##                                        ##\r\n");
    DEBUG_PRINT("##   FASE 4 COMPLETADA EXITOSAMENTE       ##\r\n");
    DEBUG_PRINT("##   UI WALCORG con tema completo         ##\r\n");
    DEBUG_PRINT("##   Backlight PWM + fade                 ##\r\n");
    DEBUG_PRINT("##   Animaciones de transicion            ##\r\n");
    DEBUG_PRINT("##   Listo para Fase 5 (Crypto/Wallet)    ##\r\n");
    DEBUG_PRINT("##                                        ##\r\n");
    DEBUG_PRINT("############################################\r\n\r\n");

    /* ---- Loop principal ---- */
    main_wallet_loop();
    return 0;
}


/* =========================================================
 * SPLASH SCREEN
 *
 * Pantalla de bienvenida con:
 *   1. Logo WALCORG (texto estilizado)
 *   2. Barra de progreso de inicialización
 *   3. Fade-in animado
 *   4. Check mark de éxito
 * ========================================================= */
static void splash_screen(void) {
    DEBUG_PRINT("[F4] Mostrando splash screen...\r\n");

    /* Dibujar la pantalla antes del fade-in */
    tft_clear(THEME_BG);

    /* Fondo degradado simulado con franjas */
    for (int y = 0; y < 320; y += 4) {
        uint8_t intensity = (uint8_t)(y * 8 / 320);
        tft_color_t stripe = RGB565(intensity, intensity, intensity + 10);
        tft_fill_rect(0, y, 480, 4, stripe);
    }
    tft_fill_rect(0, 0, 480, 320, THEME_BG);  /* Cubrir con bg */

    /* Logo principal */
    tft_draw_string_centered(70, "WALCORG", THEME_BITCOIN, THEME_BG, FONT_TITLE);
    tft_draw_string_centered(100, "Cold Wallet", THEME_TEXT_SECONDARY, THEME_BG, FONT_BODY);

    /* Línea decorativa */
    tft_draw_hline(120, 128, 240, THEME_SEPARATOR);

    /* Versión */
    tft_draw_string_centered(138, "Fase 4 — UI Completa",
                             THEME_TEXT_DISABLED, THEME_BG, FONT_SMALL);

    /* Crédito */
    tft_draw_string_centered(155, "Basado en Trezor firmware",
                             THEME_TEXT_DISABLED, THEME_BG, FONT_SMALL);
    tft_draw_string_centered(167, "STM32F405 @ 168MHz",
                             THEME_TEXT_DISABLED, THEME_BG, FONT_SMALL);

    /* Barra de progreso de inicialización */
    tft_draw_string_centered(200, "Inicializando...",
                             THEME_TEXT_SECONDARY, THEME_BG, FONT_SMALL);

    /* Simular progreso de inicialización */
    uint8_t stages[] = { 15, 35, 55, 75, 90, 100 };
    const char *stage_labels[] = {
        "HAL + Clock...",
        "RNG...",
        "Display TFT...",
        "Backlight PWM...",
        "Click-wheel...",
        "Listo!"
    };

    for (int i = 0; i < 6; i++) {
        /* Actualizar label */
        tft_fill_rect(0, 210, 480, FONT_H_SMALL + 4, THEME_BG);
        tft_draw_string_centered(212, stage_labels[i],
                                 THEME_TEXT_SECONDARY, THEME_BG, FONT_SMALL);

        /* Actualizar barra */
        ui_progress_bar(LAYOUT_MARGIN_X, 228,
                        LAYOUT_INNER_W, stages[i],
                        THEME_PRIMARY, false);
        HAL_Delay(120);
    }

    /* Animación de éxito */
    HAL_Delay(200);
    anim_success_check(tft_get_width() / 2, 260, THEME_SUCCESS);
    HAL_Delay(400);

    /* Fade out antes de la pantalla principal */
    anim_fade_out(250);

    DEBUG_PRINT("[F4] Splash screen completado.\r\n");
}


/* =========================================================
 * LOOP PRINCIPAL DEL WALLET
 *
 * Implementa el flujo completo de navegación del wallet:
 *
 *   Pantalla de inicio (home)
 *       │
 *       ├── [0] Recibir   → screen_receive()
 *       ├── [1] Enviar    → screen_send_confirm() (vía trezorctl)
 *       ├── [2] Semilla   → screen_seed_view()
 *       ├── [3] PIN       → screen_pin_entry()
 *       ├── [4] Config    → screen_settings()
 *       └── [5] USB       → screen_usb_ready()
 * ========================================================= */
static void main_wallet_loop(void) {
    DEBUG_PRINT("[F4] Entrando al loop principal del wallet.\r\n");

    uint32_t last_heartbeat = 0;

    /* Dirección de ejemplo para demostración */
    const char *demo_address =
        "bc1qar0srrr7xfkvy5l643lydnw9re59gtzzwf5mdq";

    /* Palabras de semilla de demostración (NO usar en producción) */
    const char *demo_words[12] = {
        "abandon", "ability", "able", "about",
        "above", "absent", "absorb", "abstract",
        "absurd", "abuse", "access", "accident"
    };

    while (1) {

        /* ---- Heartbeat LED ---- */
        uint32_t now = HAL_GetTick();
        if ((now - last_heartbeat) >= 2000) {
            last_heartbeat = now;
            LED_TOGGLE();
        }

        /* ---- Auto-dim del backlight ---- */
        backlight_poll();

        /* ---- Mostrar pantalla de inicio y esperar selección ---- */
        /* Fade-in al entrar al home */
        tft_clear(THEME_BG);
        anim_fade_in(200);

        int selection = screen_home();

        /* Notificar actividad al hacer selección */
        backlight_activity();

        DEBUG_PRINTF("[F4] Opcion seleccionada: %d\r\n", selection);

        /* ---- Manejar la opción seleccionada ---- */
        switch (selection) {

            case 0: {
                /* RECIBIR BITCOIN */
                DEBUG_PRINT("[F4] → Recibir Bitcoin\r\n");
                anim_slide_in_right(150);
                screen_result_t r = screen_receive(demo_address);
                if (r == SCREEN_RESULT_OK) {
                    screen_success("Direccion copiada / verificada",
                                  1500);
                }
                anim_slide_in_left(150);
                break;
            }

            case 1: {
                /* ENVIAR BITCOIN — normalmente viene de trezorctl */
                DEBUG_PRINT("[F4] → Enviar Bitcoin\r\n");
                anim_slide_in_right(150);

                /* En producción, los datos vienen de trezorctl via USB.
                 * Para demo, usamos valores ejemplo. */
                screen_result_t r = screen_send_confirm(
                    "0.00500000",    /* monto */
                    "0.00001500",    /* comisión */
                    demo_address     /* destino */
                );

                if (r == SCREEN_RESULT_OK) {
                    /* TX confirmada — en producción: signing.c firma la TX */
                    screen_success("Transaccion firmada", 1500);
                    DEBUG_PRINT("[F4] TX confirmada por el usuario\r\n");
                } else {
                    DEBUG_PRINT("[F4] TX rechazada por el usuario\r\n");
                }
                anim_slide_in_left(150);
                break;
            }

            case 2: {
                /* VER SEMILLA */
                DEBUG_PRINT("[F4] → Ver Semilla\r\n");
                anim_slide_in_right(150);

                /* Pedir confirmación de PIN antes de mostrar semilla */
                char pin_buf[10] = {0};
                screen_result_t pin_r = screen_pin_entry(
                    "Ingresa PIN para ver semilla",
                    pin_buf, sizeof(pin_buf)
                );

                if (pin_r == SCREEN_RESULT_OK) {
                    /* PIN ingresado — en producción verificar con storage */
                    /* Para demo: cualquier PIN de 4+ dígitos funciona */
                    DEBUG_PRINTF("[F4] PIN ingresado (%zu dígitos)\r\n",
                        strlen(pin_buf));
                    memset(pin_buf, 0, sizeof(pin_buf)); /* Limpiar PIN */

                    screen_result_t seed_r = screen_seed_view(
                        demo_words, 12
                    );
                    if (seed_r == SCREEN_RESULT_OK) {
                        screen_success("Semilla mostrada", 1000);
                    }
                }
                anim_slide_in_left(150);
                break;
            }

            case 3: {
                /* CAMBIAR PIN */
                DEBUG_PRINT("[F4] → Cambiar PIN\r\n");
                anim_slide_in_right(150);

                char old_pin[10] = {0};
                screen_result_t r1 = screen_pin_entry(
                    "Ingresa PIN actual", old_pin, sizeof(old_pin)
                );

                if (r1 == SCREEN_RESULT_OK) {
                    char new_pin[10] = {0};
                    screen_result_t r2 = screen_pin_entry(
                        "Nuevo PIN (min. 4 digitos)", new_pin, sizeof(new_pin)
                    );

                    if (r2 == SCREEN_RESULT_OK) {
                        /* En producción: guardar hash en Flash via storage */
                        memset(old_pin, 0, sizeof(old_pin));
                        memset(new_pin, 0, sizeof(new_pin));
                        screen_success("PIN actualizado", 1500);
                        DEBUG_PRINT("[F4] PIN cambiado (demo)\r\n");
                    }
                }
                memset(old_pin, 0, sizeof(old_pin));
                anim_slide_in_left(150);
                break;
            }

            case 4: {
                /* CONFIGURACIÓN */
                DEBUG_PRINT("[F4] → Configuracion\r\n");
                anim_slide_in_right(150);
                screen_settings();
                anim_slide_in_left(150);
                break;
            }

            case 5: {
                /* USB / TREZORCTL */
                DEBUG_PRINT("[F4] → USB / trezorctl\r\n");
                anim_slide_in_right(150);
                screen_usb_ready();
                anim_slide_in_left(150);
                break;
            }

            default:
                break;
        }
    }
}


/* =========================================================
 * UTILIDADES
 * ========================================================= */
static void led_init(void) {
    __HAL_RCC_GPIOB_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin   = LED_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED_PORT, &gpio);
}

static void print_banner(void) {
    DEBUG_PRINT("\r\n");
    DEBUG_PRINT("============================================\r\n");
    DEBUG_PRINT("  WALCORG Cold Wallet — Fase 4\r\n");
    DEBUG_PRINT("  UI Completa + Tema WALCORG\r\n");
    DEBUG_PRINT("  Backlight PWM + Animaciones\r\n");
    DEBUG_PRINTF("  Build: %s %s\r\n", __DATE__, __TIME__);
    DEBUG_PRINT("  STM32F405RGT6 @ 168MHz\r\n");
    DEBUG_PRINT("============================================\r\n\r\n");
}
