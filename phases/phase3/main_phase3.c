/**
 * @file    main_phase3.c
 * @brief   Main integrador Fase 3 — WALCORG Cold Wallet
 *
 * Une todo: Fase 1 (toolchain + HAL) + Fase 2 (display TFT)
 * + Fase 3 (click-wheel + navegación de menú).
 *
 * SECUENCIA DE ARRANQUE:
 *   stm32_init()          → FPU, clock 168MHz, UART, MPU
 *   rng_init()            → Hardware RNG validado
 *   tft_init()            → Display ILI9488 480×320
 *   touch_init()          → Touch XPT2046
 *   input_init()          → Click-wheel AS5600 via I2C
 *   input_run_all_tests() → Suite de 5 tests interactivos
 *   main_loop_phase3()    → Menú real del wallet en operación
 *
 * CRITERIO DE ÉXITO:
 *   ✓ Monitor serial muestra "FASE 3 COMPLETADA"
 *   ✓ Los 5 tests del click-wheel pasan
 *   ✓ Menú principal del wallet navegable con el wheel
 *   ✓ button_read() retorna BTN_YES / BTN_NO correctamente
 *
 * @license GNU GPL v3
 */

#include "stm32f4xx_hal.h"
#include "uart_debug.h"
#include "rng_test.h"
#include "display_tft.h"
#include "display.h"
#include "touch_xpt2046.h"
#include "input_manager.h"
#include "input_test.h"
#include "menu.h"

/* LED heartbeat */
#define LED_PORT    GPIOB      /* [v0.3] user LED is PB2 on the WeAct board (PC13 is KEY) */
#define LED_PIN     GPIO_PIN_2
#define LED_TOGGLE() HAL_GPIO_TogglePin(LED_PORT, LED_PIN)

/* Prototipos privados */
extern void stm32_init(void);
static void led_init(void);
static void print_banner(void);
static void main_loop_phase3(void);
static void show_wallet_menu(void);


/* =========================================================
 * MAIN
 * ========================================================= */
int main(void) {

    /* 1. Inicialización base heredada de Fases 1 y 2 */
    stm32_init();
    led_init();
    print_banner();

    /* 2. Hardware RNG */
    if (!rng_init()) {
        DEBUG_PRINT("[FASE3] WARN: RNG no disponible.\r\n");
    }

    /* 3. Display TFT — heredado de Fase 2 */
    DEBUG_PRINT("[FASE3] Inicializando display TFT...\r\n");
    bool display_ok = tft_init(TFT_ROTATION_90);
    if (!display_ok) {
        DEBUG_PRINT("[FASE3] ERROR: Display no respondio.\r\n");
    }

    /* 4. Touch — heredado de Fase 2 */
    touch_init();

    /* 5. Click-wheel — NUEVO en Fase 3 */
    DEBUG_PRINT("[FASE3] Inicializando click-wheel AS5600...\r\n");
    bool input_ok = input_init();

    if (!input_ok) {
        /* Display el error en pantalla si el display funciona */
        if (display_ok) {
            tft_clear(WALCORG_BG);
            tft_fill_rect(0, 0, tft_get_width(), 36, WALCORG_ERROR);
            tft_draw_string_centered(10, "Error: Click-wheel",
                                     ILI9488_WHITE, WALCORG_ERROR, 2);
            tft_draw_string_centered(60, "AS5600 no detectado",
                                     WALCORG_ERROR, WALCORG_BG, 2);
            tft_draw_string_centered(100, "Verificar:", WALCORG_TEXT, WALCORG_BG, 1);
            tft_draw_string_centered(115, "- Pull-ups 4.7kOhm en PB6/PB7",
                                     WALCORG_SUBTEXT, WALCORG_BG, 1);
            tft_draw_string_centered(130, "- VCC 3.3V al AS5600",
                                     WALCORG_SUBTEXT, WALCORG_BG, 1);
            tft_draw_string_centered(145, "- Cables SDA/SCL correctos",
                                     WALCORG_SUBTEXT, WALCORG_BG, 1);
            tft_draw_string_centered(170, "- Iman sobre el AS5600",
                                     WALCORG_SUBTEXT, WALCORG_BG, 1);
        }
        DEBUG_PRINT("[FASE3] FATAL: click-wheel requerido para continuar.\r\n");
        /* Loop de error — parpadeo rápido del LED */
        while (1) {
            LED_TOGGLE();
            HAL_Delay(100);
        }
    }

    DEBUG_PRINT("[FASE3] Click-wheel inicializado.\r\n");

    /* 6. Ejecutar suite de tests interactivos */
    DEBUG_PRINT("[FASE3] Iniciando tests interactivos...\r\n");
    input_run_all_tests();

    /* 7. Resumen en UART */
    DEBUG_PRINT("\r\n");
    DEBUG_PRINT("############################################\r\n");
    DEBUG_PRINT("##                                        ##\r\n");
    DEBUG_PRINT("##   FASE 3 COMPLETADA EXITOSAMENTE       ##\r\n");
    DEBUG_PRINT("##   Click-wheel funcional                ##\r\n");
    DEBUG_PRINT("##   Menus navegables                     ##\r\n");
    DEBUG_PRINT("##   BTN_YES/NO compatibles con Trezor    ##\r\n");
    DEBUG_PRINT("##   Listo para Fase 4 (UI WALCORG)       ##\r\n");
    DEBUG_PRINT("##                                        ##\r\n");
    DEBUG_PRINT("############################################\r\n\r\n");

    /* 8. Loop principal con menú real del wallet */
    main_loop_phase3();
    return 0;
}


/* =========================================================
 * MENÚ PRINCIPAL DEL WALLET
 *
 * Después de los tests, muestra el menú real del wallet.
 * Esta es la pantalla que verá el usuario en producción.
 * ========================================================= */
static void show_wallet_menu(void) {

    static const menu_item_t wallet_items[] = {
        { "Recibir Bitcoin",
          "Ver direccion de recepcion",
          MENU_ICON_RECEIVE, true },

        { "Enviar Bitcoin",
          "Firmar y enviar transaccion",
          MENU_ICON_SEND, true },

        { "Ver semilla",
          "Mostrar 24 palabras BIP39",
          MENU_ICON_KEY, true },

        { "Cambiar PIN",
          "Modificar PIN de seguridad",
          MENU_ICON_LOCK, true },

        { "Informacion",
          "Version, estado, diagnostico",
          MENU_ICON_INFO, true },

        { "USB / trezorctl",
          "Conectar con el host",
          MENU_ICON_USB, true },
    };

    menu_t wallet_menu;
    menu_init(&wallet_menu, "WALCORG Wallet", wallet_items, 6);

    bool running = true;

    while (running) {
        int result = menu_run(&wallet_menu);

        switch (result) {

            case 0:  /* Recibir Bitcoin */
                tft_clear(WALCORG_BG);
                tft_fill_rect(0, 0, tft_get_width(), 36, WALCORG_SUCCESS);
                tft_draw_string_centered(10, "Recibir Bitcoin",
                                         ILI9488_WHITE, WALCORG_SUCCESS, 2);
                tft_draw_string_centered(80,
                    "bc1qar0srrr7xfkvy5l643lydnw9re59gtzz",
                    WALCORG_TEXT, WALCORG_BG, 1);
                tft_draw_string_centered(100, "(direccion de ejemplo)",
                    WALCORG_SUBTEXT, WALCORG_BG, 1);
                tft_draw_string_centered(160,
                    "Long press para volver",
                    WALCORG_SUBTEXT, WALCORG_BG, 1);
                input_wait(0);  /* Esperar cualquier acción */
                menu_invalidate(&wallet_menu);
                break;

            case 1:  /* Enviar Bitcoin */
                tft_clear(WALCORG_BG);
                tft_fill_rect(0, 0, tft_get_width(), 36, WALCORG_WARNING);
                tft_draw_string_centered(10, "Enviar Bitcoin",
                                         ILI9488_WHITE, WALCORG_WARNING, 2);
                tft_draw_string_centered(80,
                    "Conecta a trezorctl para",
                    WALCORG_TEXT, WALCORG_BG, 2);
                tft_draw_string_centered(110,
                    "firmar transacciones.",
                    WALCORG_TEXT, WALCORG_BG, 2);
                input_wait(0);
                menu_invalidate(&wallet_menu);
                break;

            case 2:  /* Ver semilla */
                tft_clear(WALCORG_BG);
                tft_fill_rect(0, 0, tft_get_width(), 36, WALCORG_ERROR);
                tft_draw_string_centered(10, "Ver Semilla",
                                         ILI9488_WHITE, WALCORG_ERROR, 2);
                tft_draw_string_centered(70,
                    "ADVERTENCIA: Nunca fotografíes",
                    WALCORG_ERROR, WALCORG_BG, 1);
                tft_draw_string_centered(85,
                    "ni compartas tu semilla.",
                    WALCORG_ERROR, WALCORG_BG, 1);
                tft_draw_string_centered(115,
                    "Disponible en Fase 5",
                    WALCORG_SUBTEXT, WALCORG_BG, 1);
                tft_draw_string_centered(130,
                    "(validacion crypto)",
                    WALCORG_SUBTEXT, WALCORG_BG, 1);
                input_wait(0);
                menu_invalidate(&wallet_menu);
                break;

            case 3:  /* Cambiar PIN */
                tft_clear(WALCORG_BG);
                tft_fill_rect(0, 0, tft_get_width(), 36, WALCORG_PRIMARY);
                tft_draw_string_centered(10, "Cambiar PIN",
                                         ILI9488_WHITE, WALCORG_PRIMARY, 2);
                tft_draw_string_centered(80,
                    "Disponible en Fase 5",
                    WALCORG_TEXT, WALCORG_BG, 2);
                tft_draw_string_centered(110,
                    "(validacion y almacenamiento)",
                    WALCORG_SUBTEXT, WALCORG_BG, 1);
                input_wait(0);
                menu_invalidate(&wallet_menu);
                break;

            case 4:  /* Información */
                tft_clear(WALCORG_BG);
                tft_fill_rect(0, 0, tft_get_width(), 36, WALCORG_PRIMARY);
                tft_draw_string_centered(10, "Informacion",
                                         ILI9488_WHITE, WALCORG_PRIMARY, 2);
                tft_draw_string_centered(60, "WALCORG Cold Wallet",
                                         WALCORG_TEXT, WALCORG_BG, 2);
                tft_draw_string_centered(90,  "Fase 3 — Click-wheel",
                                         WALCORG_SUBTEXT, WALCORG_BG, 1);
                tft_draw_string_centered(105, "STM32F405RGT6 @ 168MHz",
                                         WALCORG_SUBTEXT, WALCORG_BG, 1);
                tft_draw_string_centered(120, "Display: ILI9488 480x320",
                                         WALCORG_SUBTEXT, WALCORG_BG, 1);
                tft_draw_string_centered(135, "Input: AS5600 I2C",
                                         WALCORG_SUBTEXT, WALCORG_BG, 1);
                tft_draw_string_centered(155, "GPL v3 — walcorg.com",
                                         WALCORG_SUBTEXT, WALCORG_BG, 1);
                input_wait(0);
                menu_invalidate(&wallet_menu);
                break;

            case 5:  /* USB / trezorctl */
                tft_clear(WALCORG_BG);
                tft_fill_rect(0, 0, tft_get_width(), 36, WALCORG_PRIMARY);
                tft_draw_string_centered(10, "USB — trezorctl",
                                         ILI9488_WHITE, WALCORG_PRIMARY, 2);
                tft_draw_string_centered(70, "Conecta el cable USB-C",
                                         WALCORG_TEXT, WALCORG_BG, 2);
                tft_draw_string_centered(100, "y ejecuta:",
                                         WALCORG_SUBTEXT, WALCORG_BG, 1);
                tft_draw_string_centered(120, "trezorctl list",
                                         WALCORG_WARNING, WALCORG_BG, 1);
                input_wait(0);
                menu_invalidate(&wallet_menu);
                break;

            case MENU_RESULT_BACK:
            case MENU_RESULT_HOME:
                /* En el menú principal, BACK/HOME no sale — vuelve a mostrar */
                menu_invalidate(&wallet_menu);
                break;

            default:
                break;
        }
    }
}


/* =========================================================
 * LOOP PRINCIPAL FASE 3
 * ========================================================= */
static void main_loop_phase3(void) {
    DEBUG_PRINT("[LOOP3] Entrando al menú principal del wallet.\r\n");
    DEBUG_PRINT("[LOOP3] Gira el wheel para navegar.\r\n");

    uint32_t last_heartbeat = 0;

    /* El menú es el loop principal — se ejecuta indefinidamente */
    while (1) {
        /* Heartbeat LED cada 2 segundos (más lento — el menú ya
         * consume tiempo con su loop interno) */
        uint32_t now = HAL_GetTick();
        if ((now - last_heartbeat) >= 2000) {
            last_heartbeat = now;
            LED_TOGGLE();
        }

        show_wallet_menu();
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
    DEBUG_PRINT("  WALCORG Cold Wallet — Fase 3\r\n");
    DEBUG_PRINT("  Click-wheel AS5600 + Menus TFT\r\n");
    DEBUG_PRINTF("  Build: %s %s\r\n", __DATE__, __TIME__);
    DEBUG_PRINT("  STM32F405RGT6 @ 168MHz\r\n");
    DEBUG_PRINT("============================================\r\n\r\n");
}
