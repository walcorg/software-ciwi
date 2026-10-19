/**
 * @file    layout_phase4.c  → reemplaza layout.c de Fase 2
 * @brief   Capa de presentación extendida — WalCorg Fase 4
 *
 * CAMBIOS RESPECTO A FASE 2 (layout.c):
 *
 *   [MOD-L6] Las funciones layoutXxx() ahora llaman a las pantallas
 *            completas de ui_screens.c en vez de dibujar directamente
 *            con primitivas oledXxx. Esto separa completamente la
 *            lógica de presentación del motor de Trezor.
 *
 *   [MOD-L7] Añadidas las funciones que faltaban en Fase 2:
 *            layoutProgressUpdate(), layoutVerifyAddress(),
 *            layoutSignMessage(), layoutRequireConfirmation()
 *
 *   [MOD-L8] Integración con backlight: cada pantalla notifica
 *            actividad a backlight_activity() para resetear
 *            el timer de auto-dim.
 *
 *   [MOD-L9] Integración con animaciones: transiciones suaves
 *            entre pantallas con anim_slide_in_right/left().
 *
 * TODAS las funciones mantienen las firmas originales de Trezor
 * para que fsm.c, protect.c y signing.c no cambien.
 *
 * @license GNU GPL v3
 */

#include "layout.h"
#include "ui_screens.h"
#include "ui_components.h"
#include "ui_animations.h"
#include "ui_theme.h"
#include "backlight.h"
#include "input_manager.h"
#include "display_tft.h"
#include "uart_debug.h"
#include "stm32f4xx_hal.h"
#include <stdio.h>
#include <string.h>

/* =========================================================
 * ESTADO INTERNO DEL LAYOUT
 * ========================================================= */

/** Última pantalla mostrada — para decidir dirección de animación */
static enum {
    SCREEN_NONE = 0,
    SCREEN_HOME,
    SCREEN_CONFIRM,
    SCREEN_ADDRESS,
    SCREEN_PIN,
    SCREEN_SEED,
    SCREEN_ERROR,
} _last_screen = SCREEN_NONE;

/** Notificar actividad al backlight en cada cambio de pantalla */
static void _notify_activity(void) {
    backlight_activity();
}

/** Decidir dirección de animación según el flujo de pantallas */
static void _transition_forward(void) {
    anim_slide_in_right(180);   /* [MOD-L9] Navegar hacia adelante */
}

static void _transition_back(void) {
    anim_slide_in_left(180);    /* [MOD-L9] Volver hacia atrás */
}


/* =========================================================
 * PANTALLA DE INICIO
 * ========================================================= */

/**
 * layoutHome — Pantalla de inicio del wallet
 *
 * [MOD-L6] En Fase 2 dibujaba directamente con oledXxx.
 * En Fase 4 delega a screen_home() de ui_screens.c,
 * que usa los componentes completos del tema WalCorg.
 */
void layoutHome(void) {
    _notify_activity();

    if (_last_screen != SCREEN_HOME && _last_screen != SCREEN_NONE) {
        _transition_back();
    }
    _last_screen = SCREEN_HOME;

    /* screen_home() es bloqueante y retorna la opción seleccionada.
     * El llamador (fsm.c) maneja el resultado via button_read().
     * Aquí solo lanzamos la pantalla — el flujo lo controla fsm.c. */
    DEBUG_PRINT("[LAYOUT] layoutHome()\r\n");

    /* Dibujar la pantalla de inicio — no bloqueante en este contexto.
     * fsm.c llama layoutHome() y luego entra en su loop de eventos. */
    tft_clear(THEME_BG);
    ui_topbar("WalCorg Wallet", 'B', THEME_PRIMARY_DIM);

    /* Logo / estado central */
    tft_fill_circle(tft_get_width() / 2, 130, 45, THEME_PRIMARY_DIM);
    tft_draw_string_centered(112, "B", THEME_BITCOIN, THEME_PRIMARY_DIM, 3);

    tft_draw_string_centered(195, "WalCorg Cold Wallet",
                             THEME_TEXT_PRIMARY, THEME_BG, 2);
    tft_draw_string_centered(218, "Conecta al USB o gira el wheel",
                             THEME_TEXT_SECONDARY, THEME_BG, 1);

    ui_bottombar(NULL, THEME_BG, "Menu", THEME_PRIMARY_DIM);
}

/**
 * layoutLabel — Mostrar el nombre/label del dispositivo
 * [MOD-L6] Dibuja sobre la pantalla de inicio existente
 */
void layoutLabel(const char *label) {
    if (!label || !label[0]) return;
    _notify_activity();

    tft_fill_rect(0, 175, tft_get_width(), 20, THEME_BG);
    tft_draw_string_centered(178, label,
                             THEME_TEXT_SECONDARY, THEME_BG, 1);
}


/* =========================================================
 * DIÁLOGOS DE CONFIRMACIÓN
 * ========================================================= */

/**
 * layoutDialogSwipe — Diálogo genérico de confirmación
 *
 * [MOD-L6] Delega a ui_components para el render y
 * a input_manager para la interacción.
 * [MOD-L8] Notifica actividad al backlight.
 */
void layoutDialogSwipe(const BITMAP *icon,
                        const char *btnNo, const char *btnYes,
                        const char *desc1, const char *desc2,
                        const char *desc3, const char *desc4,
                        const char *desc5, const char *desc6) {
    _notify_activity();
    _transition_forward();
    _last_screen = SCREEN_CONFIRM;

    DEBUG_PRINT("[LAYOUT] layoutDialogSwipe()\r\n");

    tft_clear(THEME_BG);
    ui_topbar("Confirmar", 0, THEME_PRIMARY_DIM);

    int y = LAYOUT_CONTENT_Y + 8;

    /* Mostrar líneas de descripción con estilo alternado */
    const char *descs[] = {desc1, desc2, desc3, desc4, desc5, desc6};
    for (int i = 0; i < 6; i++) {
        if (!descs[i]) continue;
        tft_color_t color = (i % 2 == 0) ?
                            THEME_TEXT_PRIMARY : THEME_TEXT_SECONDARY;
        tft_draw_string(LAYOUT_MARGIN_X, y, descs[i],
                        color, THEME_BG, (i == 0) ? FONT_BODY : FONT_SMALL);
        y += (i == 0) ? (FONT_H_BODY + 4) : (FONT_H_SMALL + 3);
    }

    /* Botones */
    ui_confirm_buttons(btnNo ? btnNo : "No",
                       btnYes ? btnYes : "Si", false);

    (void)icon;  /* No usamos bitmap en la versión TFT */
}

/**
 * layoutAddress — Mostrar dirección Bitcoin para verificación
 * [MOD-L6] Delega a screen_receive() de ui_screens.c
 */
void layoutAddress(const char *address) {
    _notify_activity();
    _transition_forward();
    _last_screen = SCREEN_ADDRESS;

    DEBUG_PRINTF("[LAYOUT] layoutAddress(%s)\r\n",
        address ? address : "NULL");

    screen_receive(address);
}

/**
 * layoutConfirmTx — Confirmación de transacción Bitcoin
 * [MOD-L6] Delega a screen_send_confirm() de ui_screens.c
 */
void layoutConfirmTx(const char *amount, const char *fee) {
    _notify_activity();
    _transition_forward();
    _last_screen = SCREEN_CONFIRM;

    DEBUG_PRINTF("[LAYOUT] layoutConfirmTx(%s, fee=%s)\r\n",
        amount ? amount : "?", fee ? fee : "?");

    screen_send_confirm(amount, fee, NULL);
}

/**
 * layoutFeeOverThreshold — Advertencia de comisión alta
 */
void layoutFeeOverThreshold(const char *fee) {
    _notify_activity();
    _last_screen = SCREEN_CONFIRM;

    tft_clear(THEME_BG);
    ui_topbar("! Comision Alta !", 0, THEME_ERROR_DIM);

    tft_draw_string_centered(LAYOUT_CONTENT_Y + 20,
        "La comision de red es", THEME_ERROR, THEME_BG, FONT_BODY);
    tft_draw_string_centered(LAYOUT_CONTENT_Y + 42,
        "inusualmente alta:", THEME_ERROR, THEME_BG, FONT_BODY);

    if (fee) {
        tft_fill_round_rect(LAYOUT_MARGIN_X, LAYOUT_CONTENT_Y + 68,
                            LAYOUT_INNER_W, 36, 6, THEME_BG_CARD);
        tft_draw_string_centered(LAYOUT_CONTENT_Y + 78,
            fee, THEME_WARNING, THEME_BG_CARD, FONT_TITLE);
    }

    tft_draw_string_centered(LAYOUT_CONTENT_Y + 120,
        "Verifica antes de continuar.", THEME_TEXT_SECONDARY, THEME_BG, FONT_SMALL);

    ui_confirm_buttons("Cancelar", "Continuar", false);
}

/**
 * layoutSignMessage — Confirmar firma de mensaje
 */
void layoutSignMessage(const char *msg) {
    _notify_activity();
    _last_screen = SCREEN_CONFIRM;

    tft_clear(THEME_BG);
    ui_topbar("Firmar Mensaje", 0, THEME_WARNING_DIM);

    tft_draw_string(LAYOUT_MARGIN_X, LAYOUT_CONTENT_Y + 10,
        "Mensaje a firmar:", THEME_TEXT_SECONDARY, THEME_BG, FONT_SMALL);

    ui_card(LAYOUT_MARGIN_X, LAYOUT_CONTENT_Y + 26,
            LAYOUT_INNER_W, 80, THEME_BG_CARD);

    if (msg) {
        tft_draw_string(LAYOUT_MARGIN_X + CARD_PADDING,
                        LAYOUT_CONTENT_Y + 34,
                        msg, THEME_TEXT_PRIMARY, THEME_BG_CARD, FONT_SMALL);
    }

    tft_draw_string_centered(LAYOUT_CONTENT_Y + 118,
        "Esta firma no mueve fondos.", THEME_TEXT_SECONDARY, THEME_BG, FONT_SMALL);

    ui_confirm_buttons("Rechazar", "Firmar", false);
}

/**
 * layoutVerifyAddress — Pedir al usuario que verifique la dirección
 * [MOD-L7] Función nueva no presente en Fase 2
 */
void layoutVerifyAddress(const char *address) {
    _notify_activity();
    _last_screen = SCREEN_ADDRESS;

    tft_clear(THEME_BG);
    ui_topbar("Verificar Direccion", 0, THEME_SUCCESS_DIM);

    tft_draw_string_centered(LAYOUT_CONTENT_Y + 10,
        "Confirma que esta es tu", THEME_TEXT_PRIMARY, THEME_BG, FONT_BODY);
    tft_draw_string_centered(LAYOUT_CONTENT_Y + 30,
        "direccion de recepcion:", THEME_TEXT_SECONDARY, THEME_BG, FONT_SMALL);

    ui_address_box(LAYOUT_MARGIN_X, LAYOUT_CONTENT_Y + 50,
                   LAYOUT_INNER_W, address ? address : "");

    tft_draw_string_centered(LAYOUT_CONTENT_Y + 110,
        "Compara CADA caracter", THEME_WARNING, THEME_BG, FONT_SMALL);
    tft_draw_string_centered(LAYOUT_CONTENT_Y + 124,
        "con tu dispositivo origen.", THEME_WARNING, THEME_BG, FONT_SMALL);

    ui_confirm_buttons("No coincide", "Confirmar", false);
}


/* =========================================================
 * PIN Y SEGURIDAD
 * ========================================================= */

/**
 * layoutEnterPIN — Solicitar PIN al usuario
 * [MOD-L6] Delega a screen_pin_entry() de ui_screens.c
 * [MOD-L8] Notifica actividad al backlight para encender si estaba dim
 */
void layoutEnterPIN(const char *desc, const char *pin) {
    _notify_activity();
    _last_screen = SCREEN_PIN;

    DEBUG_PRINTF("[LAYOUT] layoutEnterPIN: %s\r\n", desc ? desc : "");

    /* layoutEnterPIN en Trezor solo DIBUJA la pantalla de PIN,
     * no la maneja. La interacción la hace protect.c via button_read().
     * Aquí mostramos la pantalla con el estado actual del PIN. */
    tft_clear(THEME_BG);
    ui_topbar("PIN de Seguridad", '#', THEME_PRIMARY_DIM);

    tft_draw_string_centered(LAYOUT_CONTENT_Y + 12,
        desc ? desc : "Ingresa tu PIN:",
        THEME_TEXT_PRIMARY, THEME_BG, FONT_BODY);

    /* Mostrar puntos del PIN según los dígitos ya ingresados */
    uint8_t pin_len = pin ? (uint8_t)strlen(pin) : 0;
    ui_pin_dots(tft_get_width() / 2, LAYOUT_CONTENT_Y + 60,
                pin_len, 9, pin_len);

    /* Dígito actual (simulado — protect.c lo maneja) */
    tft_draw_string_centered(LAYOUT_CONTENT_Y + 95,
        "[ 0 ]", THEME_PRIMARY, THEME_BG, FONT_TITLE);

    tft_draw_string_centered(LAYOUT_CONTENT_Y + 138,
        "Girar: cambiar", THEME_TEXT_SECONDARY, THEME_BG, FONT_SMALL);
    tft_draw_string_centered(LAYOUT_CONTENT_Y + 152,
        "Click: confirmar digito", THEME_TEXT_SECONDARY, THEME_BG, FONT_SMALL);
    tft_draw_string_centered(LAYOUT_CONTENT_Y + 166,
        "Long press: borrar / aceptar", THEME_TEXT_SECONDARY, THEME_BG, FONT_SMALL);

    ui_bottombar("Cancelar", THEME_ERROR_DIM, "OK", THEME_SUCCESS_DIM);
}


/* =========================================================
 * SEMILLA BIP39
 * ========================================================= */

/**
 * layoutWords — Mostrar palabras de la semilla
 * [MOD-L6] Delega a ui_word_chip() de ui_components.c
 */
void layoutWords(const char *const *words, int start, int count) {
    _notify_activity();
    _last_screen = SCREEN_SEED;

    tft_clear(THEME_BG);

    char title[32];
    snprintf(title, sizeof(title), "Semilla — Palabras %d-%d",
             start + 1, start + count);
    ui_topbar(title, '*', THEME_WARNING_DIM);

    /* Dos columnas de 3 palabras */
    int col_x[2] = { LAYOUT_MARGIN_X, LAYOUT_MARGIN_X + LAYOUT_INNER_W / 2 + 4 };

    for (int i = 0; i < count && i < 6; i++) {
        if (!words || !words[start + i]) continue;
        int col = i / 3;
        int row = i % 3;
        int y   = LAYOUT_CONTENT_Y + SPACING_SM + row * 58;
        ui_word_chip((tft_coord_t)col_x[col], (tft_coord_t)y,
                     (uint8_t)(start + i + 1), words[start + i], true);
    }

    bool is_last = ((start + count) >= 24);
    ui_bottombar(start > 0 ? "Anterior" : NULL, THEME_PRIMARY_DIM,
                 is_last ? "Finalizar" : "Siguiente", THEME_PRIMARY_DIM);
}


/* =========================================================
 * PROGRESO Y ESTADO
 * ========================================================= */

/**
 * layoutFirmwareUpdate — Pantalla de actualización de firmware
 */
void layoutFirmwareUpdate(void) {
    _notify_activity();

    tft_clear(THEME_BG);
    ui_topbar("Actualizando Firmware", 0, THEME_WARNING_DIM);

    ui_spinner(tft_get_width() / 2, LAYOUT_CONTENT_Y + 80,
               25, 0, THEME_WARNING);
    tft_draw_string_centered(LAYOUT_CONTENT_Y + 115,
        "Actualizando...", THEME_TEXT_PRIMARY, THEME_BG, FONT_BODY);
    tft_draw_string_centered(LAYOUT_CONTENT_Y + 140,
        "No desconectes el USB.", THEME_ERROR, THEME_BG, FONT_SMALL);
    tft_draw_string_centered(LAYOUT_CONTENT_Y + 155,
        "No apagues el dispositivo.", THEME_ERROR, THEME_BG, FONT_SMALL);
}

/**
 * layoutProgressBar — Barra de progreso animada
 * [MOD-L9] Anima el spinner junto con la barra de progreso
 *
 * @param desc     Descripción de la operación
 * @param progress Progreso de 0 a 1000 (‰)
 */
void layoutProgressBar(const char *desc, int progress) {
    static bool   first   = true;
    static uint8_t frame  = 0;
    static uint32_t last_frame_ms = 0;

    _notify_activity();

    if (first) {
        tft_clear(THEME_BG);
        ui_topbar("Procesando...", 0, THEME_PRIMARY_DIM);

        if (desc) {
            tft_draw_string_centered(LAYOUT_CONTENT_Y + 18,
                desc, THEME_TEXT_PRIMARY, THEME_BG, FONT_BODY);
        }
        first = false;
    }

    /* Animar spinner cada 80ms */
    uint32_t now = HAL_GetTick();
    if ((now - last_frame_ms) >= 80) {
        last_frame_ms = now;
        frame = (frame + 1) % 8;
        ui_spinner(tft_get_width() / 2, LAYOUT_CONTENT_Y + 80,
                   22, frame, THEME_PRIMARY);
    }

    /* Barra de progreso (0-1000 → 0-100%) */
    uint8_t pct = (uint8_t)(progress / 10);
    ui_progress_bar(LAYOUT_MARGIN_X,
                    LAYOUT_CONTENT_Y + 112,
                    LAYOUT_INNER_W,
                    pct, THEME_PRIMARY, true);

    if (progress >= 1000) first = true;
}

/**
 * layoutProgressUpdate — Actualizar progreso sin redibujar todo
 * [MOD-L7] Función nueva no presente en Fase 2
 *
 * Más eficiente que layoutProgressBar para actualizaciones
 * frecuentes — solo actualiza la barra y el porcentaje.
 */
void layoutProgressUpdate(int progress) {
    uint8_t pct = (uint8_t)(progress > 1000 ? 100 : progress / 10);
    ui_progress_bar(LAYOUT_MARGIN_X,
                    LAYOUT_CONTENT_Y + 112,
                    LAYOUT_INNER_W,
                    pct, THEME_PRIMARY, true);
}

/**
 * layoutRequireConfirmation — Pedir confirmación simple
 * [MOD-L7] Función nueva
 *
 * Pantalla minimalista de confirmación: mensaje + YES/NO.
 * Más simple que layoutDialogSwipe para casos básicos.
 */
void layoutRequireConfirmation(const char *message) {
    _notify_activity();
    _last_screen = SCREEN_CONFIRM;

    tft_clear(THEME_BG);
    ui_topbar("Confirmar", 0, THEME_PRIMARY_DIM);

    if (message) {
        tft_draw_string_centered(
            CENTER_CONTENT_Y(FONT_H_BODY),
            message, THEME_TEXT_PRIMARY, THEME_BG, FONT_BODY
        );
    }

    ui_confirm_buttons("No", "Si", false);
}
