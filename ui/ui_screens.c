/**
 * @file    ui_screens.c
 * @brief   Pantallas completas del wallet — WALCORG Fase 4
 *
 * Cada función dibuja una pantalla completa y maneja la
 * interacción con el usuario via input_manager hasta que
 * el usuario toma una acción (confirmar, cancelar, navegar).
 *
 * @license GNU GPL v3
 */

#include "ui_screens.h"
#include "ui_components.h"
#include "ui_theme.h"
#include "input_manager.h"
#include "menu.h"
#include "display_tft.h"
#include "uart_debug.h"
#include "stm32f4xx_hal.h"
#include <string.h>
#include <stdio.h>

/* =========================================================
 * PANTALLA DE INICIO
 * ========================================================= */

/**
 * screen_home — Pantalla de inicio del wallet
 *
 * Muestra el menú principal con las opciones del wallet.
 * El usuario navega con el click-wheel y selecciona una opción.
 *
 * Diseño:
 *   ┌─────────────────────────────┐
 *   │  [B]  WALCORG Cold Wallet   │  ← topbar con logo Bitcoin
 *   ├─────────────────────────────┤
 *   │ ▸ Recibir Bitcoin           │  ← ítem seleccionado
 *   │   Enviar Bitcoin            │
 *   │   Ver Semilla               │
 *   │   Cambiar PIN               │
 *   │   Configuracion             │
 *   │   USB / trezorctl           │
 *   ├─────────────────────────────┤
 *   │  ↑↓ Girar  ·  ● Seleccionar│  ← ayuda
 *   └─────────────────────────────┘
 */
int screen_home(void) {

    static const menu_item_t home_items[] = {
        { "Recibir Bitcoin",
          "Ver direccion de recepcion",
          MENU_ICON_RECEIVE,  true },
        { "Enviar Bitcoin",
          "Firmar transaccion via USB",
          MENU_ICON_SEND,     true },
        { "Ver Semilla",
          "Mostrar 24 palabras BIP39",
          MENU_ICON_KEY,      true },
        { "Cambiar PIN",
          "Modificar PIN de seguridad",
          MENU_ICON_LOCK,     true },
        { "Configuracion",
          "Ajustes del dispositivo",
          MENU_ICON_SETTINGS, true },
        { "USB / trezorctl",
          "Conectar con el host",
          MENU_ICON_USB,      true },
    };

    menu_t home_menu;
    menu_init(&home_menu, "WALCORG Wallet", home_items, 6);
    menu_set_title_color(&home_menu, THEME_PRIMARY_DIM);

    /* En el home, BACK no sale — vuelve a mostrar el menú */
    while (1) {
        int result = menu_run(&home_menu);
        if (result >= 0) return result;
        /* BACK / HOME en el home → no salir, redibujar */
        menu_invalidate(&home_menu);
    }
}


/* =========================================================
 * PANTALLA DE RECEPCIÓN
 * ========================================================= */

screen_result_t screen_receive(const char *address) {

    tft_clear(THEME_BG);
    ui_topbar("Recibir Bitcoin", 'v', THEME_SUCCESS_DIM);

    /* Etiqueta */
    tft_draw_string(LAYOUT_MARGIN_X,
                    LAYOUT_CONTENT_Y + SPACING_MD,
                    "Direccion Bitcoin:",
                    THEME_TEXT_SECONDARY, THEME_BG, FONT_SMALL);

    /* Caja de dirección — componente especializado */
    const char *addr = address ? address :
        "bc1qar0srrr7xfkvy5l643lydnw9re59gtzzwf5mdq";

    ui_address_box(LAYOUT_MARGIN_X,
                   LAYOUT_CONTENT_Y + SPACING_MD + FONT_H_SMALL + 4,
                   LAYOUT_INNER_W, addr);

    /* Advertencia de verificación */
    tft_draw_string_centered(
        LAYOUT_CONTENT_Y + 90,
        "Verifica CADA caracter antes",
        THEME_WARNING, THEME_BG, FONT_SMALL
    );
    tft_draw_string_centered(
        LAYOUT_CONTENT_Y + 104,
        "de compartir esta direccion.",
        THEME_WARNING, THEME_BG, FONT_SMALL
    );

    /* Red activa */
    tft_fill_round_rect(LAYOUT_MARGIN_X,
                        LAYOUT_CONTENT_Y + 125, 100, 20, 4,
                        THEME_SUCCESS_DIM);
    tft_draw_string(LAYOUT_MARGIN_X + SPACING_SM,
                    LAYOUT_CONTENT_Y + 130,
                    "MAINNET", THEME_SUCCESS, THEME_SUCCESS_DIM,
                    FONT_SMALL);

    /* Instrucción de navegación */
    tft_draw_string_centered(
        LAYOUT_CONTENT_Y + 160,
        "Long press para volver al menu",
        THEME_TEXT_DISABLED, THEME_BG, FONT_SMALL
    );

    ui_bottombar("Volver", THEME_ERROR_DIM,
                 "Confirmar", THEME_SUCCESS_DIM);

    /* Esperar acción */
    input_flush();
    input_action_t action = input_wait(0);

    if (action == INPUT_CONFIRM) return SCREEN_RESULT_OK;
    return SCREEN_RESULT_CANCEL;
}


/* =========================================================
 * PANTALLA DE CONFIRMACIÓN DE ENVÍO
 * ========================================================= */

screen_result_t screen_send_confirm(const char *amount,
                                     const char *fee,
                                     const char *to_addr) {
    tft_clear(THEME_BG);
    ui_topbar("Confirmar Envio", '^', THEME_WARNING_DIM);

    int y = LAYOUT_CONTENT_Y + SPACING_SM;

    /* Monto a enviar */
    tft_draw_string(LAYOUT_MARGIN_X, y,
                    "Monto:", THEME_TEXT_SECONDARY, THEME_BG, FONT_SMALL);
    y += FONT_H_SMALL + 2;

    ui_card(LAYOUT_MARGIN_X, y, LAYOUT_INNER_W, 36, THEME_BG_CARD);
    tft_draw_string(LAYOUT_MARGIN_X + CARD_PADDING, y + 10,
                    amount ? amount : "0.00000000",
                    THEME_WARNING, THEME_BG_CARD, FONT_TITLE);
    tft_draw_string(LAYOUT_MARGIN_X + CARD_PADDING + 280, y + 16,
                    "BTC", THEME_TEXT_SECONDARY, THEME_BG_CARD, FONT_BODY);
    y += 36 + SPACING_SM;

    /* Dirección destino */
    tft_draw_string(LAYOUT_MARGIN_X, y,
                    "Destino:", THEME_TEXT_SECONDARY, THEME_BG, FONT_SMALL);
    y += FONT_H_SMALL + 2;

    ui_address_box(LAYOUT_MARGIN_X, y, LAYOUT_INNER_W,
                   to_addr ? to_addr : "bc1q...");
    y += 44 + SPACING_SM;

    /* Comisión */
    tft_draw_string(LAYOUT_MARGIN_X, y,
                    "Comision de red:", THEME_TEXT_SECONDARY,
                    THEME_BG, FONT_SMALL);
    tft_draw_string(LAYOUT_MARGIN_X + 110, y,
                    fee ? fee : "calculando...",
                    THEME_TEXT_PRIMARY, THEME_BG, FONT_SMALL);

    /* Advertencia */
    ui_separator(LAYOUT_CONTENT_Y + 185, THEME_SEPARATOR);
    tft_draw_string_centered(
        LAYOUT_CONTENT_Y + 192,
        "Esta operacion es IRREVERSIBLE.",
        THEME_ERROR, THEME_BG, FONT_SMALL
    );

    /* Botones con foco en SÍ por seguridad visual */
    ui_confirm_buttons("Rechazar", "Firmar y Enviar", false);

    /* El usuario DEBE confirmar explícitamente */
    input_flush();
    bool focus_yes = false;

    while (1) {
        input_action_t action = input_wait(0);

        if (action == INPUT_NAV_UP || action == INPUT_NAV_DOWN) {
            /* Cambiar foco entre botones */
            focus_yes = !focus_yes;
            ui_confirm_buttons("Rechazar", "Firmar y Enviar", focus_yes);
        } else if (action == INPUT_CONFIRM) {
            if (focus_yes) {
                DEBUG_PRINT("[SCREEN] TX confirmada por el usuario\r\n");
                return SCREEN_RESULT_OK;
            } else {
                DEBUG_PRINT("[SCREEN] TX rechazada por el usuario\r\n");
                return SCREEN_RESULT_CANCEL;
            }
        } else if (action == INPUT_BACK || action == INPUT_HOME) {
            return SCREEN_RESULT_CANCEL;
        }
    }
}


/* =========================================================
 * PANTALLA DE ENTRADA DE PIN
 * ========================================================= */

screen_result_t screen_pin_entry(const char *prompt,
                                  char *pin_out, uint8_t max_len) {
    if (!pin_out || max_len == 0) return SCREEN_RESULT_CANCEL;

    tft_clear(THEME_BG);
    ui_topbar("PIN de Seguridad", '#', THEME_PRIMARY_DIM);

    /* Prompt */
    tft_draw_string_centered(
        LAYOUT_CONTENT_Y + SPACING_MD,
        prompt ? prompt : "Ingresa tu PIN:",
        THEME_TEXT_PRIMARY, THEME_BG, FONT_BODY
    );

    /* PIN state */
    uint8_t pin_len     = 0;
    uint8_t cur_digit   = 0;
    bool    done        = false;
    bool    cancelled   = false;

    memset(pin_out, 0, max_len);
    input_flush();

    while (!done && !cancelled) {

        /* Dibujar dígito actual grande en el centro */
        tft_fill_rect(LAYOUT_MARGIN_X,
                      LAYOUT_CONTENT_Y + 55,
                      LAYOUT_INNER_W, FONT_H_TITLE + 8, THEME_BG);

        char digit_str[4];
        snprintf(digit_str, sizeof(digit_str), "%d", cur_digit);
        tft_draw_string_centered(
            LAYOUT_CONTENT_Y + 58,
            digit_str, THEME_PRIMARY, THEME_BG, FONT_TITLE
        );

        /* Puntos de PIN */
        ui_pin_dots(tft_get_width() / 2,
                    LAYOUT_CONTENT_Y + 110,
                    pin_len, (max_len < 9 ? max_len : 9),
                    pin_len);

        /* Instrucciones */
        tft_fill_rect(0, LAYOUT_CONTENT_Y + 140,
                      tft_get_width(), FONT_H_SMALL * 3 + 4, THEME_BG);
        tft_draw_string_centered(LAYOUT_CONTENT_Y + 142,
            "Girar: cambiar digito",
            THEME_TEXT_SECONDARY, THEME_BG, FONT_SMALL);
        tft_draw_string_centered(LAYOUT_CONTENT_Y + 156,
            "Click: confirmar digito",
            THEME_TEXT_SECONDARY, THEME_BG, FONT_SMALL);
        tft_draw_string_centered(LAYOUT_CONTENT_Y + 170,
            "Long press: borrar / aceptar",
            THEME_TEXT_SECONDARY, THEME_BG, FONT_SMALL);

        ui_bottombar("Cancelar", THEME_ERROR_DIM,
                     "OK", THEME_SUCCESS_DIM);

        /* Esperar acción */
        input_action_t action = input_wait(30000); /* 30s timeout */

        switch (action) {
            case INPUT_NAV_UP:
                cur_digit = (cur_digit + 1) % 10;
                break;

            case INPUT_NAV_DOWN:
                cur_digit = (cur_digit == 0) ? 9 : cur_digit - 1;
                break;

            case INPUT_CONFIRM:
                if (pin_len < max_len - 1) {
                    pin_out[pin_len++] = '0' + cur_digit;
                    pin_out[pin_len]   = '\0';
                    cur_digit = 0;
                }
                break;

            case INPUT_BACK:
                if (pin_len > 0) {
                    /* Borrar último dígito */
                    pin_out[--pin_len] = '\0';
                    cur_digit = 0;
                } else if (pin_len == 0) {
                    /* Long press sin dígitos = cancelar */
                    cancelled = true;
                }
                /* Long press con dígitos = aceptar el PIN */
                if (pin_len >= 4 && !cancelled) done = true;
                break;

            case INPUT_HOME:
            case INPUT_NONE:
                cancelled = true;
                break;

            default:
                break;
        }
    }

    if (cancelled) {
        memset(pin_out, 0, max_len);
        return SCREEN_RESULT_CANCEL;
    }

    return SCREEN_RESULT_OK;
}


/* =========================================================
 * PANTALLA DE VISUALIZACIÓN DE SEMILLA
 * ========================================================= */

screen_result_t screen_seed_view(const char *const *words,
                                  uint8_t count) {
    if (!words || count == 0) return SCREEN_RESULT_CANCEL;

    /* Advertencia antes de mostrar */
    tft_clear(THEME_BG);
    ui_topbar("! Advertencia !", 0, THEME_ERROR_DIM);

    tft_draw_string_centered(LAYOUT_CONTENT_Y + 20,
        "Estas a punto de ver tu semilla.", THEME_ERROR, THEME_BG, FONT_BODY);
    tft_draw_string_centered(LAYOUT_CONTENT_Y + 46,
        "NUNCA fotografies ni compartas", THEME_TEXT_PRIMARY, THEME_BG, FONT_SMALL);
    tft_draw_string_centered(LAYOUT_CONTENT_Y + 60,
        "estas palabras con nadie.", THEME_TEXT_PRIMARY, THEME_BG, FONT_SMALL);
    tft_draw_string_centered(LAYOUT_CONTENT_Y + 82,
        "Quien tenga tu semilla tiene", THEME_TEXT_PRIMARY, THEME_BG, FONT_SMALL);
    tft_draw_string_centered(LAYOUT_CONTENT_Y + 96,
        "acceso TOTAL a tus fondos.", THEME_ERROR, THEME_BG, FONT_SMALL);
    tft_draw_string_centered(LAYOUT_CONTENT_Y + 118,
        "Asegurate de estar solo y en", THEME_TEXT_SECONDARY, THEME_BG, FONT_SMALL);
    tft_draw_string_centered(LAYOUT_CONTENT_Y + 132,
        "un lugar privado.", THEME_TEXT_SECONDARY, THEME_BG, FONT_SMALL);

    ui_confirm_buttons("Cancelar", "Entiendo el riesgo", false);

    input_flush();
    input_action_t warn_action = input_wait(0);
    if (warn_action != INPUT_CONFIRM) return SCREEN_RESULT_CANCEL;

    /* Mostrar semilla en páginas de 6 palabras */
    uint8_t words_per_page = 6;
    uint8_t total_pages    = (count + words_per_page - 1) / words_per_page;
    uint8_t current_page   = 0;

    while (current_page < total_pages) {
        tft_clear(THEME_BG);

        char title[32];
        snprintf(title, sizeof(title), "Semilla — Pag %d/%d",
                 current_page + 1, total_pages);
        ui_topbar(title, 0, THEME_WARNING_DIM);

        /* Mostrar 6 palabras en 2 columnas de 3 */
        int base_idx = current_page * words_per_page;
        for (int i = 0; i < words_per_page && (base_idx + i) < count; i++) {
            int col = i / 3;  /* 0=izquierda, 1=derecha */
            int row = i % 3;
            int x   = LAYOUT_MARGIN_X + col * (LAYOUT_INNER_W / 2 + 4);
            int y   = LAYOUT_CONTENT_Y + SPACING_SM + row * 58;

            ui_word_chip(x, y, base_idx + i + 1,
                         words[base_idx + i], true);
        }

        /* Navegación entre páginas */
        bool is_last = (current_page == total_pages - 1);
        ui_bottombar(current_page > 0 ? "Anterior" : NULL,
                     THEME_PRIMARY_DIM,
                     is_last ? "Finalizar" : "Siguiente",
                     is_last ? THEME_SUCCESS_DIM : THEME_PRIMARY_DIM);

        input_flush();
        input_action_t page_action = input_wait(0);

        if (page_action == INPUT_CONFIRM) {
            if (is_last) break;
            else current_page++;
        } else if (page_action == INPUT_BACK) {
            if (current_page > 0) current_page--;
            else return SCREEN_RESULT_CANCEL;
        } else if (page_action == INPUT_HOME) {
            return SCREEN_RESULT_CANCEL;
        }
    }

    return SCREEN_RESULT_OK;
}


/* =========================================================
 * PANTALLA DE CONFIGURACIÓN
 * ========================================================= */

screen_result_t screen_settings(void) {
    static const menu_item_t settings_items[] = {
        { "Acerca de",   "Version y diagnostico",   MENU_ICON_INFO,    true  },
        { "USB / Host",  "Estado de conexion",       MENU_ICON_USB,     true  },
        { "Brillo",      "Ajustar backlight",        MENU_ICON_NONE,    true  },
        { "Formato",     "Unidades BTC/sat",         MENU_ICON_NONE,    true  },
        { "Restablecer", "Borrar todos los datos",   MENU_ICON_WARNING, false }, /* Bloqueado */
    };

    menu_t cfg_menu;
    menu_init(&cfg_menu, "Configuracion", settings_items, 5);
    menu_set_title_color(&cfg_menu, THEME_PRIMARY_DIM);

    int result = menu_run(&cfg_menu);

    switch (result) {
        case 0: screen_about();    break;
        case 1: screen_usb_ready(); break;
        default: break;
    }

    return (result == MENU_RESULT_BACK ||
            result == MENU_RESULT_HOME) ?
           SCREEN_RESULT_CANCEL : SCREEN_RESULT_OK;
}


/* =========================================================
 * PANTALLA ACERCA DE
 * ========================================================= */

void screen_about(void) {
    tft_clear(THEME_BG);
    ui_topbar("Acerca de WALCORG", 'i', THEME_PRIMARY_DIM);

    int y = LAYOUT_CONTENT_Y + SPACING_MD;

    /* Logo textual */
    tft_draw_string_centered(y, "WALCORG", THEME_BITCOIN, THEME_BG, FONT_TITLE);
    y += FONT_H_TITLE + SPACING_SM;
    tft_draw_string_centered(y, "Cold Wallet", THEME_TEXT_SECONDARY, THEME_BG, FONT_BODY);
    y += FONT_H_BODY + SPACING_LG;

    /* Información del dispositivo */
    struct { const char *k; const char *v; } info[] = {
        { "Hardware",  "STM32F405RGT6 @ 168MHz" },
        { "Display",   "ILI9488 TFT 480x320"     },
        { "Input",     "AS5600 I2C Click-wheel"  },
        { "Firmware",  "WALCORG Fase 4"          },
        { "Base",      "Trezor firmware (LGPL)"  },
        { "Licencia",  "GNU GPL v3"              },
        { "Proyecto",  "walcorg.com"             },
    };

    for (size_t i = 0; i < sizeof(info)/sizeof(info[0]); i++) {
        tft_draw_string(LAYOUT_MARGIN_X, y,
                        info[i].k, THEME_TEXT_SECONDARY, THEME_BG, FONT_SMALL);
        tft_draw_string(LAYOUT_MARGIN_X + 80, y,
                        info[i].v, THEME_TEXT_PRIMARY, THEME_BG, FONT_SMALL);
        y += FONT_H_SMALL + 4;
    }

    ui_bottombar(NULL, THEME_BG, "Volver", THEME_PRIMARY_DIM);
    input_flush();
    input_wait(0);
}


/* =========================================================
 * PANTALLA USB LISTO
 * ========================================================= */

void screen_usb_ready(void) {
    tft_clear(THEME_BG);
    ui_topbar("USB / trezorctl", 'U', THEME_PRIMARY_DIM);

    /* Ícono de conexión */
    tft_fill_circle(tft_get_width() / 2,
                    LAYOUT_CONTENT_Y + 55, 30, THEME_PRIMARY_DIM);
    tft_draw_string_centered(LAYOUT_CONTENT_Y + 42,
                             "USB", THEME_PRIMARY, THEME_PRIMARY_DIM, FONT_BODY);

    tft_draw_string_centered(LAYOUT_CONTENT_Y + 100,
        "Conecta el cable USB-C", THEME_TEXT_PRIMARY, THEME_BG, FONT_BODY);
    tft_draw_string_centered(LAYOUT_CONTENT_Y + 122,
        "y ejecuta en tu computadora:", THEME_TEXT_SECONDARY, THEME_BG, FONT_SMALL);

    /* Comando */
    tft_fill_round_rect(LAYOUT_MARGIN_X, LAYOUT_CONTENT_Y + 138,
                        LAYOUT_INNER_W, 24, 4, THEME_BG_INPUT);
    tft_draw_string_centered(LAYOUT_CONTENT_Y + 144,
        "trezorctl list", THEME_SUCCESS, THEME_BG_INPUT, FONT_BODY);

    tft_draw_string_centered(LAYOUT_CONTENT_Y + 175,
        "El dispositivo debe aparecer", THEME_TEXT_SECONDARY, THEME_BG, FONT_SMALL);
    tft_draw_string_centered(LAYOUT_CONTENT_Y + 189,
        "como 'WALCORG Cold Wallet'.", THEME_TEXT_SECONDARY, THEME_BG, FONT_SMALL);

    ui_bottombar(NULL, THEME_BG, "Volver", THEME_PRIMARY_DIM);
    input_flush();
    input_wait(0);
}


/* =========================================================
 * PANTALLAS DE ESTADO
 * ========================================================= */

void screen_error(const char *title, const char *message) {
    tft_clear(THEME_BG);
    ui_topbar(title ? title : "Error", '!', THEME_ERROR_DIM);

    /* Ícono de error */
    tft_fill_circle(tft_get_width() / 2,
                    LAYOUT_CONTENT_Y + 60, 35, THEME_ERROR_DIM);
    tft_draw_string_centered(LAYOUT_CONTENT_Y + 45,
                             "!", THEME_ERROR, THEME_ERROR_DIM, FONT_TITLE);

    if (message) {
        tft_draw_string_centered(LAYOUT_CONTENT_Y + 110,
                                 message, THEME_TEXT_PRIMARY, THEME_BG, FONT_BODY);
    }

    ui_bottombar(NULL, THEME_BG, "OK", THEME_ERROR_DIM);
    DEBUG_PRINTF("[SCREEN] Error: %s — %s\r\n",
        title ? title : "", message ? message : "");

    input_flush();
    input_wait(0);
}

void screen_success(const char *message, uint32_t duration_ms) {
    tft_clear(THEME_BG);
    ui_topbar("Completado", 0, THEME_SUCCESS_DIM);

    /* Ícono de éxito */
    tft_fill_circle(tft_get_width() / 2,
                    LAYOUT_CONTENT_Y + 60, 35, THEME_SUCCESS_DIM);
    tft_draw_string_centered(LAYOUT_CONTENT_Y + 45,
                             "OK", THEME_SUCCESS, THEME_SUCCESS_DIM, FONT_TITLE);

    if (message) {
        tft_draw_string_centered(LAYOUT_CONTENT_Y + 110,
                                 message, THEME_TEXT_PRIMARY, THEME_BG, FONT_BODY);
    }

    if (duration_ms > 0) {
        HAL_Delay(duration_ms);
    } else {
        ui_bottombar(NULL, THEME_BG, "OK", THEME_SUCCESS_DIM);
        input_flush();
        input_wait(0);
    }
}
