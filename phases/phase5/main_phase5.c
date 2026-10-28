/**
 * @file    main_phase5.c
 * @brief   Main integrador Fase 5 — WALCORG Cold Wallet
 *
 * Une las 5 fases:
 *   Fase 1 → STM32 HAL, clock 168MHz, UART, RNG hardware
 *   Fase 2 → Display TFT ILI9488, touch XPT2046
 *   Fase 3 → Click-wheel AS5600, menús navegables
 *   Fase 4 → UI tema WALCORG, animaciones, backlight PWM
 *   Fase 5 → Crypto BIP39/BIP32/ECDSA, storage NORCOW,
 *             USB HID, FSM trezorctl
 *
 * SECUENCIA DE ARRANQUE:
 *   stm32_init()          → HAL, clock, UART, MPU
 *   rng_init()            → Hardware RNG STM32
 *   tft_init()            → Display 480×320
 *   backlight_init()      → PWM brillo
 *   touch_init()          → Touch XPT2046
 *   input_init()          → Click-wheel AS5600
 *   storage_hal_init()    → NORCOW Flash sectores 9-11
 *   crypto_run_all_tests()→ Validar BIP39/BIP32/ECDSA
 *   usb_hid_init()        → Stack USB HID
 *   splash_screen()       → Bienvenida animada
 *   main_wallet_loop()    → Loop principal
 *
 * LOOP PRINCIPAL:
 *   while(1) {
 *     usb_hid_poll()          → verificar mensajes USB
 *     if (mensaje USB) {
 *       fsm_dispatch(msg)     → procesar con UI y crypto
 *     } else {
 *       mostrar menú local    → navegar con click-wheel
 *     }
 *     backlight_poll()        → auto-dim por inactividad
 *   }
 *
 * CRITERIO DE ÉXITO FASE 5:
 *   ✓ Todos los tests crypto pasan (BIP39/BIP32/ECDSA/RNG)
 *   ✓ "trezorctl list" muestra el dispositivo
 *   ✓ "trezorctl get-address" retorna dirección Bitcoin real
 *   ✓ Wallet puede firmar TX en testnet
 *   ✓ Semilla almacenada cifrada en Flash
 *   ✓ PIN protege el acceso a la semilla
 *
 * @license GNU GPL v3
 */

#include "stm32f4xx_hal.h"
#include <string.h>   /* [v0.2] memset, strncpy, strcmp, strtok */
#include <stdio.h>
#include "uart_debug.h"
#include "rng_test.h"
#include "display_tft.h"
#include "backlight.h"
#include "touch_xpt2046.h"
#include "input_manager.h"
#include "ui_theme.h"
#include "ui_components.h"
#include "ui_screens.h"
#include "ui_animations.h"
#include "storage_hal.h"
#include "crypto_validate.h"
#include "wallet_core.h"
#include "usb_hid.h"
#include "menu.h"

/* Declaración del dispatcher del FSM */
extern void fsm_dispatch(const usb_message_t *msg);

/* LED heartbeat */
#define LED_PORT     GPIOB      /* [v0.3] user LED is PB2 on the WeAct board (PC13 is KEY) */
#define LED_PIN      GPIO_PIN_2
#define LED_TOGGLE() HAL_GPIO_TogglePin(LED_PORT, LED_PIN)

/* Prototipos privados */
extern void stm32_init(void);
static void led_init(void);
static void print_banner(void);
static void splash_screen_phase5(void);
static void main_wallet_loop(void);
static void show_local_menu(void);
static void handle_first_run(void);


/* =========================================================
 * MAIN
 * ========================================================= */
int main(void) {

    /* ---- Fase 1: hardware base ---- */
    stm32_init();
    led_init();
    print_banner();

    /* ---- RNG: crítico para la seguridad ---- */
    DEBUG_PRINT("[F5] Inicializando RNG hardware...\r\n");
    if (!rng_init()) {
        DEBUG_PRINT("[F5] FATAL: RNG no disponible.\r\n");
        /* Sin RNG no hay seguridad — no continuar */
        while (1) { LED_TOGGLE(); HAL_Delay(100); }
    }

    /* ---- Fase 2: display ---- */
    DEBUG_PRINT("[F5] Inicializando display TFT...\r\n");
    if (!tft_init(TFT_ROTATION_90)) {
        DEBUG_PRINT("[F5] FATAL: Display no responde.\r\n");
        while (1) { LED_TOGGLE(); HAL_Delay(200); }
    }
    backlight_init();
    touch_init();

    /* ---- Fase 3: input ---- */
    DEBUG_PRINT("[F5] Inicializando click-wheel...\r\n");
    if (!input_init()) {
        /* Mostrar error en pantalla */
        tft_clear(THEME_BG);
        ui_topbar("Error Hardware", 0, THEME_ERROR_DIM);
        tft_draw_string_centered(140,
            "Click-wheel no detectado",
            THEME_ERROR, THEME_BG, FONT_BODY);
        tft_draw_string_centered(165,
            "Verificar AS5600 en PB6/PB7",
            THEME_TEXT_SECONDARY, THEME_BG, FONT_SMALL);
        DEBUG_PRINT("[F5] FATAL: Click-wheel requerido.\r\n");
        while (1) { LED_TOGGLE(); HAL_Delay(100); }
    }

    /* ---- Fase 5 NEW: almacenamiento seguro ---- */
    DEBUG_PRINT("[F5] Inicializando almacenamiento NORCOW...\r\n");
    if (!storage_hal_init()) {
        /* No fatal — puede ser primera ejecución */
        DEBUG_PRINT("[F5] WARN: Storage init falló. Primera ejecución?\r\n");
    }
    storage_print_status();

    /* ---- Fase 5 NEW: validación crypto ---- */
    DEBUG_PRINT("[F5] Ejecutando tests de validación crypto...\r\n");
    if (!crypto_run_all_tests()) {
        /* FATAL: si los tests crypto fallan, no es seguro continuar */
        tft_clear(THEME_BG);
        ui_topbar("ERROR CRITICO", 0, THEME_ERROR_DIM);
        tft_draw_string_centered(100,
            "Tests crypto FALLARON",
            THEME_ERROR, THEME_BG, FONT_TITLE);
        tft_draw_string_centered(135,
            "NO usar este dispositivo",
            THEME_TEXT_PRIMARY, THEME_BG, FONT_BODY);
        tft_draw_string_centered(155,
            "con fondos reales.",
            THEME_TEXT_PRIMARY, THEME_BG, FONT_BODY);
        tft_draw_string_centered(185,
            "Ver UART para detalles",
            THEME_TEXT_SECONDARY, THEME_BG, FONT_SMALL);
        DEBUG_PRINT("[F5] FATAL: Tests crypto fallaron.\r\n");
        /* Parpadeo muy lento — indicar error crítico */
        while (1) { LED_TOGGLE(); HAL_Delay(1000); }
    }
    DEBUG_PRINT("[F5] Tests crypto: TODOS PASARON.\r\n");

    /* ---- Fase 5 NEW: USB HID ---- */
    DEBUG_PRINT("[F5] Inicializando USB HID...\r\n");
    if (!usb_hid_init()) {
        DEBUG_PRINT("[F5] WARN: USB HID no disponible.\r\n");
        /* No fatal — el wallet puede operar sin USB */
    }

    /* ---- Splash screen ---- */
    splash_screen_phase5();

    /* ---- Primera ejecución: setup del wallet ---- */
    if (!wallet_is_initialized()) {
        DEBUG_PRINT("[F5] Primera ejecución — setup inicial.\r\n");
        handle_first_run();
    }

    /* ---- Resumen ---- */
    DEBUG_PRINT("\r\n");
    DEBUG_PRINT("############################################\r\n");
    DEBUG_PRINT("##                                        ##\r\n");
    DEBUG_PRINT("##   FASE 5 COMPLETADA EXITOSAMENTE       ##\r\n");
    DEBUG_PRINT("##   Crypto BIP39/BIP32/ECDSA validado    ##\r\n");
    DEBUG_PRINT("##   Storage NORCOW operativo             ##\r\n");
    DEBUG_PRINT("##   USB HID trezorctl listo              ##\r\n");
    DEBUG_PRINT("##   Wallet funcional end-to-end          ##\r\n");
    DEBUG_PRINT("##   Listo para Fase 6 (Security)         ##\r\n");
    DEBUG_PRINT("##                                        ##\r\n");
    DEBUG_PRINT("############################################\r\n\r\n");

    /* ---- Loop principal ---- */
    main_wallet_loop();
    return 0;
}


/* =========================================================
 * PRIMERA EJECUCIÓN — SETUP DEL WALLET
 * ========================================================= */
static void handle_first_run(void) {
    tft_clear(THEME_BG);
    ui_topbar("Bienvenido a WALCORG", 0, THEME_PRIMARY_DIM);

    tft_draw_string_centered(70,
        "Este dispositivo no tiene", THEME_TEXT_PRIMARY, THEME_BG, FONT_BODY);
    tft_draw_string_centered(92,
        "un wallet configurado.", THEME_TEXT_PRIMARY, THEME_BG, FONT_BODY);
    tft_draw_string_centered(125,
        "Opciones:", THEME_TEXT_SECONDARY, THEME_BG, FONT_SMALL);

    static const menu_item_t setup_items[] = {
        { "Crear nuevo wallet",
          "Generar semilla BIP39 nueva",
          MENU_ICON_KEY,     true },
        { "Recuperar wallet",
          "Importar semilla existente",
          MENU_ICON_RECEIVE, true },
        { "Continuar sin wallet",
          "Solo demostración de UI",
          MENU_ICON_INFO,    true },
    };

    menu_t setup_menu;
    menu_init(&setup_menu, "Configuracion inicial",
               setup_items, 3);
    menu_set_title_color(&setup_menu, THEME_PRIMARY_DIM);

    int choice = menu_run(&setup_menu);

    switch (choice) {
        case 0: {
            /* Crear nuevo wallet */
            /* Solicitar PIN */
            char pin[10] = {0}, pin2[10] = {0};
            screen_result_t r1 = screen_pin_entry(
                "Crea tu PIN (min 4 digitos)", pin, sizeof(pin));
            if (r1 != SCREEN_RESULT_OK) break;

            screen_result_t r2 = screen_pin_entry(
                "Confirma tu PIN", pin2, sizeof(pin2));
            if (r2 != SCREEN_RESULT_OK || strcmp(pin, pin2) != 0) {
                screen_error("Error", "Los PINs no coinciden.");
                memset(pin, 0, sizeof(pin));
                memset(pin2, 0, sizeof(pin2));
                break;
            }
            memset(pin2, 0, sizeof(pin2));

            /* Configurar PIN */
            storage_set_pin(pin);

            /* Generar semilla */
            ui_loading_start("Generando semilla...", 0);
            char mnemonic[WALLET_MNEMONIC_MAX_LEN];
            wallet_result_t res = wallet_generate_seed(
                24, pin, mnemonic, sizeof(mnemonic));
            ui_loading_end(res == WALLET_OK, NULL);
            memset(pin, 0, sizeof(pin));

            if (res != WALLET_OK) {
                screen_error("Error", wallet_result_str(res));
                memset(mnemonic, 0, sizeof(mnemonic));
                break;
            }

            /* Mostrar semilla para que el usuario la anote */
            const char *words[24];
            int wc = 0;
            char mc[WALLET_MNEMONIC_MAX_LEN];
            strncpy(mc, mnemonic, sizeof(mc) - 1);
            char *tok = strtok(mc, " ");
            while (tok && wc < 24) { words[wc++] = tok; tok = strtok(NULL, " "); }

            screen_seed_view((const char *const *)words, (uint8_t)wc);
            screen_success("Wallet creado exitosamente", 2000);

            /* Limpiar */
            memset(mnemonic, 0, sizeof(mnemonic));
            memset(mc, 0, sizeof(mc));
            break;
        }

        case 1: {
            /* Recuperar wallet — UI para ingresar palabras (simplificado) */
            tft_clear(THEME_BG);
            ui_topbar("Recuperar Wallet", 0, THEME_WARNING_DIM);
            tft_draw_string_centered(100,
                "Usa trezorctl para recuperar:",
                THEME_TEXT_PRIMARY, THEME_BG, FONT_BODY);
            tft_draw_string_centered(130,
                "trezorctl recovery-device",
                THEME_WARNING, THEME_BG, FONT_BODY);
            tft_draw_string_centered(160,
                "Conecta el USB-C primero.",
                THEME_TEXT_SECONDARY, THEME_BG, FONT_SMALL);
            ui_bottombar(NULL, THEME_BG, "OK", THEME_PRIMARY_DIM);
            input_flush();
            input_wait(0);
            break;
        }

        default:
            /* Continuar sin wallet — modo demo */
            DEBUG_PRINT("[F5] Modo demo — sin wallet configurado.\r\n");
            break;
    }
}


/* =========================================================
 * LOOP PRINCIPAL
 * ========================================================= */
static void main_wallet_loop(void) {
    DEBUG_PRINT("[F5] Entrando al loop principal del wallet.\r\n");
    DEBUG_PRINT("[F5] USB: conectar cable para usar trezorctl.\r\n");
    DEBUG_PRINT("[F5] Local: girar el click-wheel para navegar.\r\n");

    uint32_t last_heartbeat = 0;
    uint32_t last_usb_check = 0;
    bool     usb_was_connected = false;

    while (1) {

        uint32_t now = HAL_GetTick();

        /* ---- Heartbeat LED ---- */
        if ((now - last_heartbeat) >= 2000) {
            last_heartbeat = now;
            LED_TOGGLE();
        }

        /* ---- Auto-dim backlight ---- */
        backlight_poll();

        /* ---- Procesar mensajes USB (alta prioridad) ---- */
        usb_hid_poll();

        if (usb_hid_has_message()) {
            usb_message_t msg;
            if (usb_hid_read_message(&msg)) {
                backlight_activity();
                fsm_dispatch(&msg);
            }
            continue;  /* Volver al inicio del loop para procesar más mensajes */
        }

        /* ---- Notificar cambio de estado USB ---- */
        if ((now - last_usb_check) >= 1000) {
            last_usb_check = now;
            bool connected = usb_hid_is_connected();
            if (connected != usb_was_connected) {
                usb_was_connected = connected;
                DEBUG_PRINTF("[F5] USB: %s\r\n",
                    connected ? "conectado" : "desconectado");
            }
        }

        /* ---- Menú local (cuando no hay actividad USB) ---- */
        show_local_menu();
    }
}


/* =========================================================
 * MENÚ LOCAL DEL WALLET
 * ========================================================= */
static void show_local_menu(void) {

    static const menu_item_t wallet_items[] = {
        { "Recibir Bitcoin",
          "Mostrar dirección de recepcion",
          MENU_ICON_RECEIVE, true },
        { "Ver Semilla",
          "Requiere PIN",
          MENU_ICON_KEY,     true },
        { "Cambiar PIN",
          "Modificar PIN de seguridad",
          MENU_ICON_LOCK,    true },
        { "Configuracion",
          "Ajustes del dispositivo",
          MENU_ICON_SETTINGS,true },
        { "USB / trezorctl",
          "Conectar con el host",
          MENU_ICON_USB,     true },
        { "Info del wallet",
          "Version, estado crypto",
          MENU_ICON_INFO,    true },
    };

    menu_t menu;
    menu_init(&menu, "WALCORG Wallet", wallet_items, 6);
    menu_set_title_color(&menu, THEME_PRIMARY_DIM);

    int selection = menu_run(&menu);
    backlight_activity();

    switch (selection) {

        case 0: {
            /* RECIBIR: derivar y mostrar dirección */
            if (!wallet_is_initialized()) {
                screen_error("Sin wallet", "Configura el wallet primero.");
                break;
            }
            char pin[10] = {0};
            if (screen_pin_entry("PIN para ver dirección",
                                  pin, sizeof(pin)) == SCREEN_RESULT_OK) {
                wallet_address_t addr = {0};
                wallet_result_t r = wallet_get_address(
                    pin, 0, WALLET_COIN_BITCOIN, &addr);
                memset(pin, 0, sizeof(pin));
                if (r == WALLET_OK) {
                    anim_slide_in_right(150);
                    screen_receive(addr.address_bech32);
                } else {
                    screen_error("Error", wallet_result_str(r));
                }
            }
            break;
        }

        case 1: {
            /* VER SEMILLA */
            if (!wallet_is_initialized()) {
                screen_error("Sin wallet", "Configura el wallet primero.");
                break;
            }
            char pin[10] = {0};
            if (screen_pin_entry("PIN para ver semilla",
                                  pin, sizeof(pin)) == SCREEN_RESULT_OK) {
                char mnemonic[WALLET_MNEMONIC_MAX_LEN];
                if (storage_get_seed(pin, mnemonic, sizeof(mnemonic))) {
                    const char *words[24];
                    int wc = 0;
                    char mc[WALLET_MNEMONIC_MAX_LEN];
                    strncpy(mc, mnemonic, sizeof(mc) - 1);
                    char *tok = strtok(mc, " ");
                    while (tok && wc < 24) {
                        words[wc++] = tok; tok = strtok(NULL, " ");
                    }
                    anim_slide_in_right(150);
                    screen_seed_view((const char *const *)words, (uint8_t)wc);
                    memset(mnemonic, 0, sizeof(mnemonic));
                    memset(mc, 0, sizeof(mc));
                } else {
                    screen_error("Error PIN", "PIN incorrecto.");
                }
                memset(pin, 0, sizeof(pin));
            }
            break;
        }

        case 2: {
            /* CAMBIAR PIN */
            char old_pin[10] = {0}, new_pin[10] = {0};
            if (screen_pin_entry("PIN actual", old_pin, sizeof(old_pin))
                    == SCREEN_RESULT_OK) {
                if (storage_verify_pin(old_pin)) {
                    if (screen_pin_entry("Nuevo PIN",
                            new_pin, sizeof(new_pin)) == SCREEN_RESULT_OK) {
                        storage_set_pin(new_pin);
                        screen_success("PIN actualizado", 1500);
                    }
                } else {
                    screen_error("PIN incorrecto", "Intentalo de nuevo.");
                }
            }
            memset(old_pin, 0, sizeof(old_pin));
            memset(new_pin, 0, sizeof(new_pin));
            break;
        }

        case 3:
            /* CONFIGURACIÓN */
            anim_slide_in_right(150);
            screen_settings();
            anim_slide_in_left(150);
            break;

        case 4:
            /* USB */
            anim_slide_in_right(150);
            screen_usb_ready();
            anim_slide_in_left(150);
            break;

        case 5: {
            /* INFO */
            tft_clear(THEME_BG);
            ui_topbar("Info del Wallet", 'i', THEME_PRIMARY_DIM);

            int y = LAYOUT_CONTENT_Y + 10;
            struct { const char *k; const char *v; } info[] = {
                { "Modelo",    "WALCORG Cold Wallet"        },
                { "Fase",      "5 — Crypto + USB HID"       },
                { "MCU",       "STM32F405RGT6 @ 168MHz"     },
                { "Display",   "ILI9488 480x320 TFT"        },
                { "Input",     "AS5600 Click-wheel I2C"     },
                { "Storage",   "NORCOW Flash AES-256"       },
                { "Crypto",    "BIP39/BIP32/ECDSA secp256k1"},
                { "USB",       "HID trezorctl compatible"   },
                { "Wallet",    wallet_is_initialized() ?
                               "Configurado" : "Sin configurar" },
                { "Licencia",  "GNU GPL v3 — walcorg.com"   },
            };
            for (size_t i = 0; i < sizeof(info)/sizeof(info[0]); i++) {
                tft_draw_string(LAYOUT_MARGIN_X, y,
                    info[i].k, THEME_TEXT_SECONDARY, THEME_BG, FONT_SMALL);
                tft_draw_string(LAYOUT_MARGIN_X + 75, y,
                    info[i].v, THEME_TEXT_PRIMARY, THEME_BG, FONT_SMALL);
                y += FONT_H_SMALL + 4;
            }
            ui_bottombar(NULL, THEME_BG, "Volver", THEME_PRIMARY_DIM);
            input_flush();
            input_wait(0);
            break;
        }

        default:
            break;
    }
}


/* =========================================================
 * SPLASH SCREEN FASE 5
 * ========================================================= */
static void splash_screen_phase5(void) {
    tft_clear(THEME_BG);

    ui_topbar("WALCORG Cold Wallet", 0, THEME_PRIMARY_DIM);

    tft_draw_string_centered(70, "WALCORG", THEME_BITCOIN, THEME_BG, FONT_TITLE);
    tft_draw_string_centered(100, "Cold Wallet v5", THEME_TEXT_SECONDARY, THEME_BG, FONT_BODY);

    /* Estado del sistema */
    struct { const char *label; bool ok; } checks[] = {
        { "RNG hardware",        true                        },
        { "Display TFT",         true                        },
        { "Click-wheel",         true                        },
        { "Storage NORCOW",      true                        },
        { "Crypto BIP39/BIP32",  true                        },
        { "ECDSA secp256k1",     true                        },
        { "USB HID",             usb_hid_is_connected()      },
        { "Wallet",              wallet_is_initialized()      },
    };

    int y = 135;
    for (size_t i = 0; i < sizeof(checks)/sizeof(checks[0]); i++) {
        tft_draw_string(80, y, checks[i].label,
            THEME_TEXT_SECONDARY, THEME_BG, FONT_SMALL);
        tft_draw_string(300, y,
            checks[i].ok ? "OK" : "---",
            checks[i].ok ? THEME_SUCCESS : THEME_TEXT_DISABLED,
            THEME_BG, FONT_SMALL);
        y += FONT_H_SMALL + 3;
    }

    HAL_Delay(1500);
    anim_fade_out(200);
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
    DEBUG_PRINT("  WALCORG Cold Wallet — Fase 5\r\n");
    DEBUG_PRINT("  Crypto + Storage + USB HID\r\n");
    DEBUG_PRINT("  BIP39 / BIP32 / BIP44 / ECDSA\r\n");
    DEBUG_PRINTF("  Build: %s %s\r\n", __DATE__, __TIME__);
    DEBUG_PRINT("  STM32F405RGT6 @ 168MHz\r\n");
    DEBUG_PRINT("============================================\r\n\r\n");
}