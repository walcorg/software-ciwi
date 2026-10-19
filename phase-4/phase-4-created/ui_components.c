/**
 * @file    ui_components.c
 * @brief   Implementación de widgets UI — WalCorg Fase 4
 *
 * Todos los componentes usan exclusivamente constantes de
 * ui_theme.h — nunca valores RGB hardcodeados.
 *
 * @license GNU GPL v3
 */

#include "ui_components.h"
#include "display_tft.h"
#include "input_manager.h"
#include "uart_debug.h"
#include "stm32f4xx_hal.h"
#include <stdio.h>
#include <string.h>

/* =========================================================
 * ESTRUCTURA DE PANTALLA
 * ========================================================= */

void ui_topbar(const char *title, char icon_char, tft_color_t bg_color) {
    /* Fondo de la barra */
    tft_fill_rect(0, LAYOUT_TOPBAR_Y,
                  tft_get_width(), LAYOUT_TOPBAR_H, bg_color);

    /* Línea separadora inferior */
    tft_draw_hline(0, LAYOUT_TOPBAR_H - 1,
                   tft_get_width(), THEME_SEPARATOR);

    /* Ícono a la izquierda */
    if (icon_char) {
        char icon_str[2] = { icon_char, '\0' };
        tft_draw_string(SPACING_MD,
                        (LAYOUT_TOPBAR_H - FONT_H_BODY) / 2,
                        icon_str,
                        THEME_TEXT_PRIMARY, bg_color,
                        FONT_BODY);
    }

    /* Título centrado */
    if (title) {
        tft_draw_string_centered(
            (LAYOUT_TOPBAR_H - FONT_H_BODY) / 2,
            title,
            THEME_TEXT_PRIMARY, bg_color,
            FONT_BODY
        );
    }
}

void ui_bottombar(const char *left_label,  tft_color_t left_color,
                  const char *right_label, tft_color_t right_color) {
    /* Línea separadora superior */
    tft_draw_hline(0, LAYOUT_BOTTOMBAR_Y,
                   tft_get_width(), THEME_SEPARATOR);

    /* Fondo de la barra */
    tft_fill_rect(0, LAYOUT_BOTTOMBAR_Y + 1,
                  tft_get_width(), LAYOUT_BOTTOMBAR_H - 1,
                  THEME_BG_OVERLAY);

    int btn_y = LAYOUT_BOTTOMBAR_Y + SPACING_XS + 1;
    int btn_h = LAYOUT_BOTTOMBAR_H - SPACING_XS * 2 - 1;

    /* Botón izquierdo */
    if (left_label) {
        tft_fill_round_rect(SPACING_MD, btn_y,
                            BTN_HALF_W, btn_h,
                            BTN_RADIUS / 2, left_color);
        tft_draw_string_centered(
            btn_y + (btn_h - FONT_H_SMALL) / 2,
            left_label,
            THEME_TEXT_PRIMARY, left_color,
            FONT_SMALL
        );
    }

    /* Botón derecho */
    if (right_label) {
        int rx = SPACING_MD + BTN_HALF_W + SPACING_SM;
        tft_fill_round_rect(rx, btn_y,
                            BTN_HALF_W, btn_h,
                            BTN_RADIUS / 2, right_color);
        tft_draw_string_centered(
            btn_y + (btn_h - FONT_H_SMALL) / 2,
            right_label,
            THEME_TEXT_PRIMARY, right_color,
            FONT_SMALL
        );
    }
}

void ui_clear_content(void) {
    tft_fill_rect(0, LAYOUT_CONTENT_Y,
                  tft_get_width(), LAYOUT_CONTENT_H,
                  THEME_BG);
}


/* =========================================================
 * BOTONES
 * ========================================================= */

void ui_button(tft_coord_t x, tft_coord_t y,
               tft_coord_t w, tft_coord_t h,
               const char *label,
               ui_btn_variant_t variant, ui_state_t state) {

    /* Determinar colores según variante y estado */
    tft_color_t bg, fg, border;

    switch (variant) {
        case UI_BTN_SUCCESS:
            bg = (state == UI_STATE_SELECTED) ? THEME_SUCCESS : THEME_SUCCESS_DIM;
            border = THEME_SUCCESS;
            break;
        case UI_BTN_WARNING:
            bg = (state == UI_STATE_SELECTED) ? THEME_WARNING : THEME_WARNING_DIM;
            border = THEME_WARNING;
            break;
        case UI_BTN_ERROR:
            bg = (state == UI_STATE_SELECTED) ? THEME_ERROR : THEME_ERROR_DIM;
            border = THEME_ERROR;
            break;
        case UI_BTN_GHOST:
            bg = THEME_BG;
            border = (state == UI_STATE_SELECTED) ?
                     THEME_PRIMARY : THEME_BORDER;
            break;
        default: /* UI_BTN_PRIMARY */
            bg = (state == UI_STATE_SELECTED) ? THEME_PRIMARY : THEME_PRIMARY_DIM;
            border = THEME_PRIMARY;
            break;
    }

    if (state == UI_STATE_DISABLED) {
        bg     = THEME_BG_CARD;
        border = THEME_BORDER;
        fg     = THEME_TEXT_DISABLED;
    } else {
        fg = THEME_TEXT_PRIMARY;
    }

    /* Fondo con esquinas redondeadas */
    tft_fill_round_rect(x, y, w, h, BTN_RADIUS, bg);

    /* Borde */
    if (variant == UI_BTN_GHOST || state == UI_STATE_SELECTED) {
        tft_draw_round_rect(x, y, w, h, BTN_RADIUS, border);
    }

    /* Etiqueta centrada */
    if (label) {
        int label_y = y + (h - FONT_H_BODY) / 2;
        tft_draw_string_centered(label_y, label, fg, bg, FONT_BODY);
    }
}

void ui_confirm_buttons(const char *no_label, const char *yes_label,
                        bool yes_focus) {
    /* Línea separadora */
    tft_draw_hline(0, LAYOUT_BOTTOMBAR_Y,
                   tft_get_width(), THEME_SEPARATOR);
    tft_fill_rect(0, LAYOUT_BOTTOMBAR_Y + 1,
                  tft_get_width(), LAYOUT_BOTTOMBAR_H - 1,
                  THEME_BG_OVERLAY);

    int btn_y = LAYOUT_BOTTOMBAR_Y + SPACING_XS + 1;
    int btn_h = LAYOUT_BOTTOMBAR_H - SPACING_XS * 2 - 1;

    /* Botón NO (izquierda) */
    if (no_label) {
        ui_btn_variant_t v = UI_BTN_ERROR;
        ui_state_t s = yes_focus ? UI_STATE_NORMAL : UI_STATE_SELECTED;
        ui_button(SPACING_MD, btn_y, BTN_HALF_W, btn_h,
                  no_label, v, s);
    }

    /* Botón SÍ (derecha) */
    if (yes_label) {
        int rx = SPACING_MD + BTN_HALF_W + SPACING_SM;
        ui_btn_variant_t v = UI_BTN_SUCCESS;
        ui_state_t s = yes_focus ? UI_STATE_SELECTED : UI_STATE_NORMAL;
        ui_button(rx, btn_y, BTN_HALF_W, btn_h,
                  yes_label, v, s);
    }
}


/* =========================================================
 * CONTENEDORES
 * ========================================================= */

void ui_card(tft_coord_t x, tft_coord_t y,
             tft_coord_t w, tft_coord_t h,
             tft_color_t color) {
    tft_fill_round_rect(x, y, w, h, CARD_RADIUS, color);
    tft_draw_round_rect(x, y, w, h, CARD_RADIUS, THEME_BORDER);
}

void ui_list_item(tft_coord_t x, tft_coord_t y, tft_coord_t w,
                  const char *label, const char *sublabel,
                  const char *value, ui_state_t state) {

    tft_color_t bg = (state == UI_STATE_SELECTED) ?
                     THEME_SELECTED_BG : THEME_BG;
    tft_color_t fg = (state == UI_STATE_DISABLED) ?
                     THEME_TEXT_DISABLED : THEME_TEXT_PRIMARY;
    tft_color_t sub_fg = THEME_TEXT_SECONDARY;

    /* Fondo del ítem */
    tft_fill_rect(x, y, w, LIST_ITEM_H, bg);

    /* Barra lateral si está seleccionado */
    if (state == UI_STATE_SELECTED) {
        tft_fill_rect(x, y, 4, LIST_ITEM_H, THEME_SELECTED_BAR);
    }

    /* Label principal */
    if (label) {
        int label_y = sublabel ?
                      y + SPACING_XS :
                      y + (LIST_ITEM_H - FONT_H_BODY) / 2;
        tft_draw_string(x + LIST_ITEM_PADDING_X + 6, label_y,
                        label, fg, bg, FONT_BODY);
    }

    /* Sublabel */
    if (sublabel) {
        tft_draw_string(x + LIST_ITEM_PADDING_X + 6,
                        y + LIST_ITEM_H / 2 + 2,
                        sublabel, sub_fg, bg, FONT_SMALL);
    }

    /* Valor a la derecha */
    if (value) {
        uint16_t val_w = tft_string_width(value, FONT_BODY);
        tft_draw_string(x + w - val_w - LIST_ITEM_PADDING_X,
                        y + (LIST_ITEM_H - FONT_H_BODY) / 2,
                        value, THEME_PRIMARY, bg, FONT_BODY);
    }

    /* Flecha derecha para ítems habilitados */
    if (state != UI_STATE_DISABLED) {
        tft_draw_string(x + w - SPACING_MD - 6,
                        y + (LIST_ITEM_H - FONT_H_BODY) / 2,
                        ">", THEME_TEXT_SECONDARY, bg, FONT_BODY);
    }

    /* Separador inferior */
    tft_draw_hline(x + LIST_ITEM_PADDING_X,
                   y + LIST_ITEM_H - 1,
                   w - LIST_ITEM_PADDING_X, THEME_SEPARATOR);
}

void ui_separator(tft_coord_t y, tft_color_t color) {
    tft_fill_rect(LAYOUT_MARGIN_X, y, LAYOUT_INNER_W, 1, color);
}


/* =========================================================
 * INDICADORES
 * ========================================================= */

void ui_progress_bar(tft_coord_t x, tft_coord_t y, tft_coord_t w,
                     uint8_t progress, tft_color_t color, bool show_pct) {
    if (progress > 100) progress = 100;

    /* Track (fondo) */
    tft_fill_round_rect(x, y, w, PROGRESS_H,
                        PROGRESS_RADIUS, THEME_BG_INPUT);
    tft_draw_round_rect(x, y, w, PROGRESS_H,
                        PROGRESS_RADIUS, THEME_BORDER);

    /* Relleno según progreso */
    if (progress > 0) {
        tft_coord_t fill_w = (tft_coord_t)((w - 4) * progress / 100);
        if (fill_w > 0) {
            tft_fill_round_rect(x + 2, y + 2,
                                fill_w, PROGRESS_H - 4,
                                PROGRESS_RADIUS - 2, color);
        }
    }

    /* Porcentaje */
    if (show_pct) {
        char pct[8];
        snprintf(pct, sizeof(pct), "%d%%", progress);
        tft_draw_string_centered(y + PROGRESS_H + SPACING_XS,
                                 pct, THEME_TEXT_SECONDARY,
                                 THEME_BG, FONT_SMALL);
    }
}

void ui_spinner(tft_coord_t cx, tft_coord_t cy, tft_coord_t r,
                uint8_t frame, tft_color_t color) {
    /* 8 posiciones del spinner — puntos en círculo */
    /* Limpiar el área del spinner primero */
    tft_fill_circle(cx, cy, r + 4, THEME_BG);

    /* Calcular posiciones de los 8 puntos */
    /* Usamos aproximaciones enteras de sin/cos */
    static const int8_t cos8[8] = {  8,  6,  0, -6, -8, -6,  0,  6 };
    static const int8_t sin8[8] = {  0,  6,  8,  6,  0, -6, -8, -6 };

    for (int i = 0; i < 8; i++) {
        /* Calcular brillo según distancia al frame activo */
        int dist = (i - frame + 8) % 8;
        tft_color_t dot_color;

        /* Puntos más brillantes cerca del frame activo */
        switch (dist) {
            case 0: dot_color = color; break;
            case 1: dot_color = THEME_PRIMARY_DIM; break;
            case 2: dot_color = THEME_BG_INPUT; break;
            default: dot_color = THEME_BG_CARD; break;
        }

        tft_coord_t px = cx + (cos8[i] * r) / 8;
        tft_coord_t py = cy + (sin8[i] * r) / 8;
        tft_fill_circle(px, py, 3, dot_color);
    }
}

void ui_pin_dots(tft_coord_t cx, tft_coord_t y,
                 uint8_t filled, uint8_t total, uint8_t current) {
    if (total == 0 || total > 9) return;

    /* Calcular espaciado para centrar los puntos */
    int dot_size  = 14;
    int dot_gap   = 10;
    int total_w   = total * dot_size + (total - 1) * dot_gap;
    int start_x   = cx - total_w / 2;

    for (int i = 0; i < total; i++) {
        int x = start_x + i * (dot_size + dot_gap);

        if (i < filled) {
            /* Punto lleno — dígito confirmado */
            tft_fill_circle(x + dot_size / 2, y + dot_size / 2,
                            dot_size / 2, THEME_PRIMARY);
        } else if (i == current) {
            /* Punto actual — resaltado parpadeante */
            tft_fill_circle(x + dot_size / 2, y + dot_size / 2,
                            dot_size / 2, THEME_PRIMARY_GLOW);
            tft_draw_circle(x + dot_size / 2, y + dot_size / 2,
                            dot_size / 2 + 2, THEME_PRIMARY);
        } else {
            /* Punto vacío — dígito por ingresar */
            tft_draw_circle(x + dot_size / 2, y + dot_size / 2,
                            dot_size / 2, THEME_BORDER);
        }
    }
}


/* =========================================================
 * ELEMENTOS ESPECÍFICOS DEL WALLET
 * ========================================================= */

void ui_address_box(tft_coord_t x, tft_coord_t y, tft_coord_t w,
                    const char *address) {
    if (!address) return;

    int h = FONT_H_SMALL * 2 + SPACING_SM * 2 + 4;

    /* Fondo diferenciado */
    tft_fill_round_rect(x, y, w, h, CARD_RADIUS, THEME_BG_INPUT);
    tft_draw_round_rect(x, y, w, h, CARD_RADIUS, THEME_BORDER_FOCUS);

    /* La dirección puede ser larga (bech32 = ~62 chars)
     * Dividir en dos líneas si es necesario */
    int addr_len = strlen(address);
    int max_per_line = (w - SPACING_SM * 2) / FONT_W_SMALL;

    if (addr_len <= max_per_line) {
        /* Una sola línea */
        int text_y = y + (h - FONT_H_SMALL) / 2;
        tft_draw_string(x + SPACING_SM, text_y,
                        address, THEME_TEXT_PRIMARY,
                        THEME_BG_INPUT, FONT_SMALL);
    } else {
        /* Dos líneas: primera mitad / segunda mitad */
        char line1[64], line2[64];
        int split = addr_len / 2;
        strncpy(line1, address, split);
        line1[split] = '\0';
        strncpy(line2, address + split, sizeof(line2) - 1);
        line2[sizeof(line2) - 1] = '\0';

        tft_draw_string(x + SPACING_SM, y + SPACING_XS,
                        line1, THEME_TEXT_PRIMARY,
                        THEME_BG_INPUT, FONT_SMALL);
        tft_draw_string(x + SPACING_SM,
                        y + SPACING_XS + FONT_H_SMALL + 2,
                        line2, THEME_TEXT_PRIMARY,
                        THEME_BG_INPUT, FONT_SMALL);
    }
}

void ui_amount(tft_coord_t cx, tft_coord_t y,
               const char *amount, const char *unit,
               tft_color_t color) {
    if (!amount) return;

    /* Monto en tamaño TITLE */
    tft_draw_string_centered(y, amount, color, THEME_BG, FONT_TITLE);

    /* Unidad en tamaño BODY debajo */
    if (unit) {
        tft_draw_string_centered(y + FONT_H_TITLE + SPACING_XS,
                                 unit, THEME_TEXT_SECONDARY,
                                 THEME_BG, FONT_BODY);
    }
    (void)cx;
}

void ui_word_chip(tft_coord_t x, tft_coord_t y,
                  uint8_t number, const char *word, bool revealed) {
    int chip_w = 180;
    int chip_h = 32;

    /* Fondo del chip */
    tft_fill_round_rect(x, y, chip_w, chip_h, 6, THEME_BG_CARD);
    tft_draw_round_rect(x, y, chip_w, chip_h, 6, THEME_BORDER);

    /* Número de la palabra */
    char num_str[6];
    snprintf(num_str, sizeof(num_str), "%2d.", number);
    tft_draw_string(x + SPACING_XS, y + (chip_h - FONT_H_SMALL) / 2,
                    num_str, THEME_TEXT_SECONDARY,
                    THEME_BG_CARD, FONT_SMALL);

    /* Palabra o placeholder */
    if (revealed && word) {
        tft_draw_string(x + 28, y + (chip_h - FONT_H_BODY) / 2,
                        word, THEME_TEXT_PRIMARY,
                        THEME_BG_CARD, FONT_BODY);
    } else {
        tft_draw_string(x + 28, y + (chip_h - FONT_H_BODY) / 2,
                        "••••••", THEME_TEXT_DISABLED,
                        THEME_BG_CARD, FONT_BODY);
    }
}


/* =========================================================
 * OVERLAY Y NOTIFICACIONES
 * ========================================================= */

bool ui_dialog(const char *title, const char *message,
               const char *yes_btn, const char *no_btn) {

    /* Dibujar overlay semitransparente — rellenar con bg oscuro */
    tft_fill_rect(0, 0, tft_get_width(), tft_get_height(),
                  THEME_BG_OVERLAY);

    int dx = DIALOG_MARGIN_X;
    int dy = DIALOG_MARGIN_Y;

    /* Card del diálogo */
    ui_card(dx, dy, DIALOG_W, DIALOG_H, THEME_BG_CARD);

    /* Título */
    if (title) {
        tft_fill_rect(dx, dy, DIALOG_W, 36, THEME_PRIMARY_DIM);
        tft_draw_string_centered(dy + (36 - FONT_H_BODY) / 2,
                                 title, THEME_TEXT_PRIMARY,
                                 THEME_PRIMARY_DIM, FONT_BODY);
    }

    /* Mensaje */
    if (message) {
        int msg_y = dy + 46;
        tft_draw_string_centered(msg_y, message,
                                 THEME_TEXT_PRIMARY, THEME_BG_CARD,
                                 FONT_BODY);
    }

    /* Botones del diálogo */
    int btn_y = dy + DIALOG_H - BTN_HEIGHT - SPACING_SM;
    int btn_w = (DIALOG_W - SPACING_MD * 3) / 2;

    if (no_btn) {
        ui_button(dx + SPACING_MD, btn_y, btn_w, BTN_HEIGHT,
                  no_btn, UI_BTN_ERROR, UI_STATE_NORMAL);
    }
    if (yes_btn) {
        ui_button(dx + SPACING_MD * 2 + btn_w, btn_y, btn_w, BTN_HEIGHT,
                  yes_btn, UI_BTN_SUCCESS, UI_STATE_SELECTED);
    }

    /* Esperar respuesta del usuario */
    input_flush();
    input_action_t action = input_wait(0);
    return (action == INPUT_CONFIRM);
}

void ui_toast(const char *message, uint32_t duration_ms,
              tft_color_t color) {
    if (!message) return;

    int toast_h = 32;
    int toast_y = LAYOUT_BOTTOMBAR_Y - toast_h - SPACING_SM;

    /* Dibujar toast */
    tft_fill_round_rect(SPACING_LG, toast_y,
                        tft_get_width() - SPACING_LG * 2,
                        toast_h, 8, color);
    tft_draw_string_centered(toast_y + (toast_h - FONT_H_SMALL) / 2,
                             message, THEME_TEXT_PRIMARY,
                             color, FONT_SMALL);

    /* Esperar y borrar */
    HAL_Delay(duration_ms);
    tft_fill_rect(SPACING_LG, toast_y,
                  tft_get_width() - SPACING_LG * 2, toast_h, THEME_BG);
}

/* Estado interno de la pantalla de carga */
static struct {
    char     message[64];
    bool     active;
    uint8_t  spinner_frame;
    uint32_t last_frame_ms;
} _loading = {0};

void ui_loading_start(const char *message, uint32_t duration_ms) {
    strncpy(_loading.message, message ? message : "Cargando...",
            sizeof(_loading.message) - 1);
    _loading.active        = true;
    _loading.spinner_frame = 0;
    _loading.last_frame_ms = HAL_GetTick();

    tft_clear(THEME_BG);
    ui_topbar("Por favor espera...", 0, THEME_PRIMARY_DIM);

    tft_draw_string_centered(
        CENTER_CONTENT_Y(FONT_H_BODY),
        _loading.message,
        THEME_TEXT_PRIMARY, THEME_BG, FONT_BODY
    );

    ui_spinner(tft_get_width() / 2,
               LAYOUT_CONTENT_Y + LAYOUT_CONTENT_H / 2 + 30,
               20, 0, THEME_PRIMARY);

    if (duration_ms > 0) {
        /* Progreso automático */
        uint32_t start = HAL_GetTick();
        while ((HAL_GetTick() - start) < duration_ms) {
            uint8_t pct = (uint8_t)((HAL_GetTick() - start) * 100 / duration_ms);
            ui_loading_progress(pct);
            HAL_Delay(50);
        }
        ui_loading_end(true, NULL);
    }
}

void ui_loading_progress(uint8_t progress) {
    uint32_t now = HAL_GetTick();

    /* Animar spinner cada 80ms */
    if ((now - _loading.last_frame_ms) >= 80) {
        _loading.last_frame_ms = now;
        _loading.spinner_frame = (_loading.spinner_frame + 1) % 8;
        ui_spinner(tft_get_width() / 2,
                   LAYOUT_CONTENT_Y + LAYOUT_CONTENT_H / 2 + 30,
                   20, _loading.spinner_frame, THEME_PRIMARY);
    }

    /* Actualizar barra de progreso */
    ui_progress_bar(
        LAYOUT_MARGIN_X,
        LAYOUT_CONTENT_Y + LAYOUT_CONTENT_H / 2 + 60,
        LAYOUT_INNER_W,
        progress, THEME_PRIMARY, true
    );
}

void ui_loading_end(bool success, const char *message) {
    _loading.active = false;

    /* Ícono de resultado */
    tft_fill_circle(tft_get_width() / 2,
                    LAYOUT_CONTENT_Y + LAYOUT_CONTENT_H / 2 + 30,
                    24, success ? THEME_SUCCESS_DIM : THEME_ERROR_DIM);
    tft_draw_string_centered(
        LAYOUT_CONTENT_Y + LAYOUT_CONTENT_H / 2 + 18,
        success ? "OK" : "X",
        success ? THEME_SUCCESS : THEME_ERROR,
        success ? THEME_SUCCESS_DIM : THEME_ERROR_DIM,
        FONT_TITLE
    );

    if (message) {
        tft_fill_rect(0, LAYOUT_CONTENT_Y + 10,
                      tft_get_width(), FONT_H_BODY + 4, THEME_BG);
        tft_draw_string_centered(LAYOUT_CONTENT_Y + 12,
                                 message, THEME_TEXT_PRIMARY,
                                 THEME_BG, FONT_BODY);
    }
    HAL_Delay(1200);
}
