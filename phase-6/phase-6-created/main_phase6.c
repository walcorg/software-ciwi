/**
 * @file    main_phase6.c
 * @brief   Main integrador Fase 6 — WalCorg Cold Wallet (FINAL)
 *
 * Último main del roadmap. Une las 6 fases completas:
 *   Fase 1 → STM32 HAL, clock, RNG hardware
 *   Fase 2 → Display TFT, touch
 *   Fase 3 → Click-wheel, menús
 *   Fase 4 → UI WalCorg, backlight PWM
 *   Fase 5 → Crypto, storage cifrado, USB HID
 *   Fase 6 → RDP, anti-tamper, MPU hardening, rate-limiting,
 *             security audit
 *
 * DECISIÓN DE PRODUCCIÓN:
 *   Este firmware NO aplica RDP automáticamente — eso requiere
 *   una decisión explícita del fabricante, normalmente como parte
 *   del proceso de fabricación en planta, después de un test
 *   funcional completo. flash_protect_enable_rdp1() debe llamarse
 *   desde una herramienta de producción separada.
 *
 * @license GNU GPL v3
 */

#include "stm32f4xx_hal.h"
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

#include "flash_protect.h"
#include "tamper_detect.h"
#include "pin_delay.h"
#include "mpu_hardening.h"
#include "security_audit.h"

#include <string.h>
#include <stdio.h>

extern void fsm_dispatch(const usb_message_t *msg);

#define LED_PORT     GPIOC
#define LED_PIN      GPIO_PIN_13
#define LED_TOGGLE() HAL_GPIO_TogglePin(LED_PORT, LED_PIN)

extern void stm32_init(void);
static void led_init(void);
static void print_banner(void);
static void splash_screen_final(void);
static void main_wallet_loop_secure(void);
static void show_local_menu_secure(void);


/* =========================================================
 * MAIN
 * ========================================================= */
int main(void) {

    /* ---- Fases 1-5: toda la base del wallet ---- */
    stm32_init();
    led_init();
    print_banner();

    if (!rng_init()) {
        while (1) { LED_TOGGLE(); HAL_Delay(100); }
    }

    if (!tft_init(TFT_ROTATION_90)) {
        while (1) { LED_TOGGLE(); HAL_Delay(200); }
    }
    backlight_init();
    touch_init();

    if (!input_init()) {
        tft_clear(THEME_BG);
        ui_topbar("Error Hardware", 0, THEME_ERROR_DIM);
        tft_draw_string_centered(140, "Click-wheel no detectado",
                                 THEME_ERROR, THEME_BG, FONT_BODY);
        while (1) { LED_TOGGLE(); HAL_Delay(100); }
    }

    storage_hal_init();

    /* ---- Fase 6 NEW: hardening de seguridad ---- */
    DEBUG_PRINT("[F6] === Aplicando hardening de Fase 6 ===\r\n");

    DEBUG_PRINT("[F6] Aplicando MPU hardening...\r\n");
    mpu_hardening_apply();

#if (PRODUCTION == 1)
    tamper_init(TAMPER_POLICY_WIPE);
#else
    tamper_init(TAMPER_POLICY_LOG);
    DEBUG_PRINT("[F6] NOTA: tamper policy=LOG en build de desarrollo.\r\n");
    DEBUG_PRINT("[F6]       En PRODUCTION=1 sera WIPE automático.\r\n");
#endif

    flash_protect_print_status();

    DEBUG_PRINT("[F6] Re-validando crypto antes de continuar...\r\n");
    if (!crypto_run_all_tests()) {
        tft_clear(THEME_BG);
        ui_topbar("ERROR CRITICO", 0, THEME_ERROR_DIM);
        tft_draw_string_centered(120, "Tests crypto FALLARON",
                                 THEME_ERROR, THEME_BG, FONT_TITLE);
        tft_draw_string_centered(160, "Dispositivo bloqueado",
                                 THEME_TEXT_PRIMARY, THEME_BG, FONT_BODY);
        while (1) { LED_TOGGLE(); HAL_Delay(1000); }
    }

    usb_hid_init();

    /* ---- Security audit completo ---- */
    DEBUG_PRINT("[F6] Ejecutando auditoría de seguridad final...\r\n");
    security_audit_result_t audit = {0};
    bool ready = security_audit_run(&audit);
    security_audit_print_report(&audit);

    tft_clear(THEME_BG);
    ui_topbar("Security Audit", 0,
              ready ? THEME_SUCCESS_DIM : THEME_WARNING_DIM);

    char score_str[16];
    snprintf(score_str, sizeof(score_str), "%d/100", audit.score);
    tft_draw_string_centered(80, score_str,
        ready ? THEME_SUCCESS : THEME_WARNING, THEME_BG, FONT_TITLE);

    tft_draw_string_centered(115,
        ready ? "Listo para produccion" : "Requiere atencion",
        THEME_TEXT_PRIMARY, THEME_BG, FONT_BODY);

    if (!ready) {
        tft_draw_string_centered(145,
            "Ver UART para detalles.", THEME_TEXT_SECONDARY, THEME_BG, FONT_SMALL);
        tft_draw_string_centered(160,
            "Continuando en modo desarrollo...",
            THEME_WARNING, THEME_BG, FONT_SMALL);
    }
    HAL_Delay(2500);

    splash_screen_final();

    DEBUG_PRINT("\r\n");
    DEBUG_PRINT("############################################\r\n");
    DEBUG_PRINT("##                                        ##\r\n");
    DEBUG_PRINT("##   FASE 6 COMPLETADA — ROADMAP FINAL    ##\r\n");
    DEBUG_PRINT("##   MPU hardening: 8 regiones            ##\r\n");
    DEBUG_PRINT("##   Anti-tamper activo                   ##\r\n");
    DEBUG_PRINT("##   Rate-limiting de PIN configurado     ##\r\n");
    DEBUG_PRINTF("##   Security audit: %3d/100               ##\r\n", audit.score);
    DEBUG_PRINT("##                                        ##\r\n");
    DEBUG_PRINT("############################################\r\n\r\n");

    main_wallet_loop_secure();
    return 0;
}


/* =========================================================
 * LOOP PRINCIPAL CON SEGURIDAD ACTIVA
 * ========================================================= */
static void main_wallet_loop_secure(void) {
    DEBUG_PRINT("[F6] Loop principal — seguridad activa.\r\n");

    uint32_t last_heartbeat = 0;

    while (1) {
        uint32_t now = HAL_GetTick();

        if ((now - last_heartbeat) >= 2000) {
            last_heartbeat = now;
            LED_TOGGLE();
        }

        tamper_poll();
        backlight_poll();
        usb_hid_poll();

        if (usb_hid_has_message()) {
            usb_message_t msg;
            if (usb_hid_read_message(&msg)) {
                backlight_activity();

                if (!tamper_check_safe_to_proceed()) {
                    usb_hid_send_failure(99, "Security check failed");
                    continue;
                }
                fsm_dispatch(&msg);
            }
            continue;
        }

        show_local_menu_secure();
    }
}

/* =========================================================
 * MENÚ LOCAL CON RATE-LIMITING DE PIN
 * ========================================================= */
static void show_local_menu_secure(void) {
    static const menu_item_t wallet_items[] = {
        { "Recibir Bitcoin",  "Mostrar dirección",         MENU_ICON_RECEIVE, true },
        { "Ver Semilla",      "Requiere PIN",              MENU_ICON_KEY,     true },
        { "Cambiar PIN",      "Modificar PIN",             MENU_ICON_LOCK,    true },
        { "Seguridad",        "Estado de proteccion",      MENU_ICON_LOCK,    true },
        { "Configuracion",    "Ajustes",                   MENU_ICON_SETTINGS,true },
        { "USB / trezorctl",  "Conectar con el host",      MENU_ICON_USB,     true },
    };

    menu_t menu;
    menu_init(&menu, "WalCorg Wallet", wallet_items, 6);
    menu_set_title_color(&menu, THEME_PRIMARY_DIM);

    int selection = menu_run(&menu);
    backlight_activity();

    char pin[10] = {0};
    bool pin_ok = false;

    switch (selection) {
        case 0:
        case 1: {
            uint8_t fails = storage_get_fail_count();
            if (fails > 0) {
                pin_delay_enforce(fails + 1);
            }
            const char *prompt = (selection == 0) ?
                "PIN para ver dirección" : "PIN para ver semilla";
            if (screen_pin_entry(prompt, pin, sizeof(pin)) == SCREEN_RESULT_OK) {
                pin_ok = storage_verify_pin(pin);
            }

            if (pin_ok) {
                if (selection == 0) {
                    wallet_address_t addr = {0};
                    if (wallet_get_address(pin, 0, WALLET_COIN_BITCOIN, &addr)
                            == WALLET_OK) {
                        screen_receive(addr.address_bech32);
                    }
                } else {
                    char mnemonic[WALLET_MNEMONIC_MAX_LEN];
                    if (storage_get_seed(pin, mnemonic, sizeof(mnemonic))) {
                        const char *words[24]; int wc = 0;
                        char mc[WALLET_MNEMONIC_MAX_LEN];
                        strncpy(mc, mnemonic, sizeof(mc)-1);
                        char *tok = strtok(mc, " ");
                        while (tok && wc < 24) { words[wc++]=tok; tok=strtok(NULL," "); }
                        screen_seed_view((const char *const *)words, (uint8_t)wc);
                        memset(mnemonic, 0, sizeof(mnemonic));
                        memset(mc, 0, sizeof(mc));
                    }
                }
            } else if (pin[0]) {
                screen_error("PIN incorrecto",
                    "Intento registrado. Delay aplicado.");
            }
            memset(pin, 0, sizeof(pin));
            break;
        }

        case 2: {
            uint8_t fails = storage_get_fail_count();
            if (fails > 0) pin_delay_enforce(fails + 1);

            char old_pin[10] = {0}, new_pin[10] = {0};
            if (screen_pin_entry("PIN actual", old_pin, sizeof(old_pin))
                    == SCREEN_RESULT_OK && storage_verify_pin(old_pin)) {
                if (screen_pin_entry("Nuevo PIN", new_pin, sizeof(new_pin))
                        == SCREEN_RESULT_OK) {
                    storage_set_pin(new_pin);
                    screen_success("PIN actualizado", 1500);
                }
            } else {
                screen_error("PIN incorrecto", "No se cambio el PIN.");
            }
            memset(old_pin, 0, sizeof(old_pin));
            memset(new_pin, 0, sizeof(new_pin));
            break;
        }

        case 3: {
            tft_clear(THEME_BG);
            ui_topbar("Estado de Seguridad", '#', THEME_PRIMARY_DIM);

            int y = LAYOUT_CONTENT_Y + 10;
            uint8_t rdp = flash_protect_get_rdp_level();
            char rdp_str[24];
            snprintf(rdp_str, sizeof(rdp_str), "Nivel %s",
                rdp == RDP_LEVEL_0 ? "0 (sin proteger)" : "1 (protegido)");

            char fails_str[16];
            snprintf(fails_str, sizeof(fails_str), "%d/%d",
                storage_get_fail_count(), STORAGE_PIN_MAX_FAILS);

            struct { const char *k; const char *v; tft_color_t c; } items[] = {
                { "Flash RDP", rdp_str,
                  rdp == RDP_LEVEL_0 ? THEME_WARNING : THEME_SUCCESS },
                { "MPU", "8 regiones activas", THEME_SUCCESS },
                { "Anti-tamper", "Activo", THEME_SUCCESS },
                { "Intentos PIN", fails_str, THEME_TEXT_PRIMARY },
                { "Debugger", tamper_is_debugger_attached() ?
                  "DETECTADO" : "No detectado",
                  tamper_is_debugger_attached() ? THEME_ERROR : THEME_SUCCESS },
            };

            for (size_t i = 0; i < sizeof(items)/sizeof(items[0]); i++) {
                tft_draw_string(LAYOUT_MARGIN_X, y, items[i].k,
                    THEME_TEXT_SECONDARY, THEME_BG, FONT_SMALL);
                tft_draw_string(LAYOUT_MARGIN_X + 110, y, items[i].v,
                    items[i].c, THEME_BG, FONT_SMALL);
                y += FONT_H_SMALL + 6;
            }

            ui_bottombar(NULL, THEME_BG, "Volver", THEME_PRIMARY_DIM);
            input_flush();
            input_wait(0);
            break;
        }

        case 4:
            anim_slide_in_right(150);
            screen_settings();
            anim_slide_in_left(150);
            break;

        case 5:
            anim_slide_in_right(150);
            screen_usb_ready();
            anim_slide_in_left(150);
            break;

        default:
            break;
    }
}


/* =========================================================
 * SPLASH FINAL
 * ========================================================= */
static void splash_screen_final(void) {
    tft_clear(THEME_BG);
    ui_topbar("WalCorg Cold Wallet", 0, THEME_SUCCESS_DIM);

    anim_success_check(tft_get_width() / 2, 100, THEME_SUCCESS);

    tft_draw_string_centered(150, "WalCorg v1.0", THEME_BITCOIN, THEME_BG, FONT_TITLE);
    tft_draw_string_centered(180, "Firmware completo", THEME_TEXT_SECONDARY, THEME_BG, FONT_BODY);
    tft_draw_string_centered(205, "Fases 1-6 integradas", THEME_TEXT_DISABLED, THEME_BG, FONT_SMALL);
    tft_draw_string_centered(220, "walcorg.com — GNU GPL v3", THEME_TEXT_DISABLED, THEME_BG, FONT_SMALL);

    HAL_Delay(2000);
    anim_fade_out(250);
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

static void print_banner(void) {
    DEBUG_PRINT("\r\n");
    DEBUG_PRINT("============================================\r\n");
    DEBUG_PRINT("  WalCorg Cold Wallet — Fase 6 (FINAL)\r\n");
    DEBUG_PRINT("  Security Hardening Completo\r\n");
    DEBUG_PRINTF("  Build: %s %s\r\n", __DATE__, __TIME__);
    DEBUG_PRINTF("  PRODUCTION=%d\r\n", PRODUCTION);
    DEBUG_PRINT("  STM32F405RGT6 @ 168MHz\r\n");
    DEBUG_PRINT("============================================\r\n\r\n");
}
