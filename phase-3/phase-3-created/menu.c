/**
 * @file    menu.c
 * @brief   Motor de menú navegable — WalCorg Fase 3
 *
 * Implementa el renderizado y la navegación del menú.
 * Usa escritura parcial al TFT para minimizar el número de
 * píxeles que se redibujan en cada acción de navegación.
 *
 * ESTRATEGIA DE REDIBUJADO EFICIENTE:
 *   Cuando el usuario navega, solo hay dos ítems que cambian:
 *   el ítem que pierde el resaltado y el que lo gana.
 *   Redibujamos SOLO esos dos ítems en vez de toda la pantalla.
 *   Esto evita el parpadeo visible al navegar rápido.
 *
 * @license GNU GPL v3
 */

#include "menu.h"
#include "input_manager.h"
#include "display_tft.h"
#include "uart_debug.h"
#include "stm32f4xx_hal.h"
#include <string.h>
#include <stdio.h>

/* =========================================================
 * SÍMBOLOS DE ÍCONOS (caracteres ASCII aproximados)
 * ========================================================= */
static const char *_icon_str[] = {
    " ",    /* NONE     */
    "v",    /* RECEIVE  */
    "^",    /* SEND     */
    "*",    /* KEY      */
    "=",    /* SETTINGS */
    "i",    /* INFO     */
    "#",    /* LOCK     */
    "U",    /* USB      */
    "!",    /* WARNING  */
};


/* =========================================================
 * FUNCIONES PRIVADAS DE RENDERIZADO
 * ========================================================= */

/**
 * _draw_title_bar — Dibujar la barra de título
 *
 * Fondo de color sólido con el título centrado en blanco.
 */
static void _draw_title_bar(const menu_t *menu) {
    /* Fondo de la barra */
    tft_fill_rect(0, 0, tft_get_width(), MENU_FIRST_ITEM_Y,
                  menu->title_color);

    /* Título centrado */
    tft_draw_string_centered(
        (MENU_FIRST_ITEM_Y - 8 * MENU_TITLE_FONT_SIZE) / 2,
        menu->title,
        ILI9488_WHITE, menu->title_color,
        MENU_TITLE_FONT_SIZE
    );
}

/**
 * _draw_help_bar — Dibujar barra de ayuda inferior
 *
 * Muestra las instrucciones de uso del click-wheel.
 */
static void _draw_help_bar(void) {
    /* Línea separadora */
    tft_draw_hline(0, MENU_HELP_BAR_Y - 1, tft_get_width(), WALCORG_SUBTEXT);

    /* Fondo ligeramente diferente al del menú */
    tft_fill_rect(0, MENU_HELP_BAR_Y, tft_get_width(), MENU_HELP_BAR_H,
                  ILI9488_DARKGRAY);

    /* Instrucciones */
    tft_draw_string(10, MENU_HELP_BAR_Y + 8,
                    "Girar: navegar",
                    WALCORG_SUBTEXT, ILI9488_DARKGRAY, 1);

    tft_draw_string(200, MENU_HELP_BAR_Y + 8,
                    "Click: seleccionar",
                    WALCORG_SUBTEXT, ILI9488_DARKGRAY, 1);

    tft_draw_string(10, MENU_HELP_BAR_Y + 22,
                    "Long press: volver",
                    WALCORG_SUBTEXT, ILI9488_DARKGRAY, 1);

    tft_draw_string(200, MENU_HELP_BAR_Y + 22,
                    "2x click: inicio",
                    WALCORG_SUBTEXT, ILI9488_DARKGRAY, 1);
}

/**
 * _get_item_y — Calcular la posición Y de un ítem en pantalla
 *
 * @param visible_index  Posición del ítem en la lista visible (0-based)
 * @return               Coordenada Y en píxeles
 */
static tft_coord_t _get_item_y(int visible_index) {
    return (tft_coord_t)(MENU_FIRST_ITEM_Y +
                         visible_index * MENU_ITEM_HEIGHT);
}

/**
 * _draw_item — Dibujar un único ítem del menú
 *
 * Renderiza el ítem en su posición correcta con el estilo
 * apropiado (seleccionado/normal/deshabilitado).
 *
 * @param item           Datos del ítem a dibujar
 * @param visible_index  Posición en la lista visible
 * @param is_selected    true si este ítem está seleccionado
 */
static void _draw_item(const menu_item_t *item, int visible_index,
                        bool is_selected) {
    tft_coord_t y = _get_item_y(visible_index);
    tft_coord_t w = (tft_coord_t)tft_get_width();

    /* Colores según estado */
    tft_color_t bg_color, fg_color, sub_color;

    if (is_selected) {
        /* Ítem seleccionado: fondo azul primario */
        bg_color  = WALCORG_PRIMARY;
        fg_color  = ILI9488_WHITE;
        sub_color = RGB565(200, 220, 255);  /* Azul claro para sublabel */
    } else if (!item->enabled) {
        /* Ítem deshabilitado: gris */
        bg_color  = WALCORG_BG;
        fg_color  = WALCORG_SUBTEXT;
        sub_color = WALCORG_SUBTEXT;
    } else {
        /* Ítem normal */
        bg_color  = WALCORG_BG;
        fg_color  = WALCORG_TEXT;
        sub_color = WALCORG_SUBTEXT;
    }

    /* Fondo del ítem */
    tft_fill_rect(0, y, w, (tft_coord_t)MENU_ITEM_HEIGHT, bg_color);

    /* Indicador de selección: barra vertical izquierda */
    if (is_selected) {
        tft_fill_rect(0, y, MENU_SELECTOR_W,
                      (tft_coord_t)MENU_ITEM_HEIGHT, ILI9488_WHITE);
    }

    /* Ícono */
    if (item->icon != MENU_ICON_NONE && item->icon < 9) {
        tft_draw_string(
            MENU_SELECTOR_W + 8, y + 8,
            _icon_str[item->icon],
            fg_color, bg_color, MENU_ITEM_FONT_SIZE
        );
    }

    /* Label principal */
    tft_draw_string(
        MENU_ITEM_MARGIN_X, y + 5,
        item->label ? item->label : "",
        fg_color, bg_color, MENU_ITEM_FONT_SIZE
    );

    /* Sublabel (texto secundario más pequeño) */
    if (item->sublabel && item->sublabel[0]) {
        tft_draw_string(
            MENU_ITEM_MARGIN_X, y + 22,
            item->sublabel,
            sub_color, bg_color, 1
        );
    }

    /* Separador inferior de cada ítem */
    tft_draw_hline(MENU_SELECTOR_W, y + MENU_ITEM_HEIGHT - 1,
                   w - MENU_SELECTOR_W, WALCORG_DARKGRAY);

    /* Flecha de navegación derecha para ítems habilitados */
    if (item->enabled && !is_selected) {
        tft_draw_string(w - 20, y + 10, ">", fg_color, bg_color, 2);
    }
}

/**
 * _draw_scroll_indicator — Indicador de scroll (barra lateral derecha)
 *
 * Muestra la posición relativa en la lista cuando hay más
 * ítems que los que caben en pantalla.
 *
 * @param menu  Estado actual del menú
 */
static void _draw_scroll_indicator(const menu_t *menu) {
    if (menu->item_count <= MENU_VISIBLE_ITEMS) return;

    tft_coord_t w = (tft_coord_t)tft_get_width();
    tft_coord_t track_h = (tft_coord_t)(MENU_VISIBLE_ITEMS * MENU_ITEM_HEIGHT);
    tft_coord_t track_y = MENU_FIRST_ITEM_Y;

    /* Fondo del track de scroll */
    tft_fill_rect(w - 6, track_y, 6, track_h, ILI9488_DARKGRAY);

    /* Tamaño y posición del thumb (indicador) */
    int thumb_h = (track_h * MENU_VISIBLE_ITEMS) / menu->item_count;
    if (thumb_h < 10) thumb_h = 10;

    int thumb_y = track_y + ((track_h - thumb_h) * menu->scroll_offset) /
                  (menu->item_count - MENU_VISIBLE_ITEMS);

    tft_fill_rect(w - 5, (tft_coord_t)thumb_y, 4,
                  (tft_coord_t)thumb_h, WALCORG_SUBTEXT);
}


/* =========================================================
 * API PÚBLICA
 * ========================================================= */

void menu_init(menu_t *menu, const char *title,
               const menu_item_t *items, uint8_t item_count) {
    if (!menu) return;

    menu->title        = title ? title : "";
    menu->items        = items;
    menu->item_count   = item_count > MENU_MAX_ITEMS ?
                         MENU_MAX_ITEMS : item_count;
    menu->selected     = 0;
    menu->scroll_offset = 0;
    menu->title_color  = WALCORG_PRIMARY;
    menu->show_back    = true;
    menu->needs_redraw = true;

    /* Asegurar que el primer ítem habilitado esté seleccionado */
    for (uint8_t i = 0; i < menu->item_count; i++) {
        if (items[i].enabled) {
            menu->selected = i;
            break;
        }
    }
}

void menu_set_title_color(menu_t *menu, tft_color_t color) {
    if (!menu) return;
    menu->title_color = color;
    menu->needs_redraw = true;
}

void menu_draw(menu_t *menu) {
    if (!menu || !menu->needs_redraw) return;

    /* Calcular qué ítems son visibles */
    uint8_t visible_start = menu->scroll_offset;
    uint8_t visible_end   = visible_start + MENU_VISIBLE_ITEMS;
    if (visible_end > menu->item_count) visible_end = menu->item_count;

    /* 1. Barra de título */
    _draw_title_bar(menu);

    /* 2. Área de contenido — fondo base */
    tft_fill_rect(0, MENU_FIRST_ITEM_Y,
                  (tft_coord_t)tft_get_width(),
                  (tft_coord_t)(MENU_VISIBLE_ITEMS * MENU_ITEM_HEIGHT),
                  WALCORG_BG);

    /* 3. Ítems visibles */
    for (uint8_t i = visible_start; i < visible_end; i++) {
        bool is_selected = (i == menu->selected);
        _draw_item(&menu->items[i], i - visible_start, is_selected);
    }

    /* 4. Indicador de scroll si hay más ítems */
    _draw_scroll_indicator(menu);

    /* 5. Barra de ayuda */
    _draw_help_bar();

    menu->needs_redraw = false;
}

void menu_invalidate(menu_t *menu) {
    if (menu) menu->needs_redraw = true;
}

int menu_handle_action(menu_t *menu, input_action_t action) {
    if (!menu || !menu->items) return -3;

    switch (action) {

        case INPUT_NAV_UP: {
            /* Buscar el ítem habilitado anterior */
            int prev = menu->selected;
            do {
                prev = (prev == 0) ? menu->item_count - 1 : prev - 1;
                if (prev == (int)menu->selected) break;  /* Vuelta completa */
            } while (!menu->items[prev].enabled);

            if (prev != (int)menu->selected) {
                /* Redibujar solo los dos ítems que cambian */
                int old_vis = menu->selected - menu->scroll_offset;
                menu->selected = (uint8_t)prev;

                /* Ajustar scroll si el ítem seleccionado salió de la vista */
                if ((int)menu->selected < (int)menu->scroll_offset) {
                    menu->scroll_offset = menu->selected;
                    menu->needs_redraw = true;  /* Scroll → redibujado completo */
                } else {
                    /* Solo redibujar los dos ítems afectados */
                    int new_vis = menu->selected - menu->scroll_offset;
                    if (old_vis >= 0 && old_vis < MENU_VISIBLE_ITEMS) {
                        _draw_item(&menu->items[menu->selected + (old_vis - new_vis)],
                                   old_vis, false);
                    }
                    if (new_vis >= 0 && new_vis < MENU_VISIBLE_ITEMS) {
                        _draw_item(&menu->items[menu->selected], new_vis, true);
                    }
                }
            }
            return -3;  /* Solo navegación, sin selección */
        }

        case INPUT_NAV_DOWN: {
            /* Buscar el siguiente ítem habilitado */
            int next = menu->selected;
            do {
                next = (next + 1) % menu->item_count;
                if (next == (int)menu->selected) break;
            } while (!menu->items[next].enabled);

            if (next != (int)menu->selected) {
                int old_vis = menu->selected - menu->scroll_offset;
                menu->selected = (uint8_t)next;

                /* Ajustar scroll si el ítem seleccionado salió de la vista */
                if ((int)menu->selected >= (int)(menu->scroll_offset + MENU_VISIBLE_ITEMS)) {
                    menu->scroll_offset = menu->selected - MENU_VISIBLE_ITEMS + 1;
                    menu->needs_redraw = true;
                } else {
                    int new_vis = menu->selected - menu->scroll_offset;
                    if (old_vis >= 0 && old_vis < MENU_VISIBLE_ITEMS) {
                        _draw_item(&menu->items[menu->selected - (new_vis - old_vis)],
                                   old_vis, false);
                    }
                    if (new_vis >= 0 && new_vis < MENU_VISIBLE_ITEMS) {
                        _draw_item(&menu->items[menu->selected], new_vis, true);
                    }
                }
            }
            return -3;
        }

        case INPUT_CONFIRM:
            /* Verificar que el ítem seleccionado está habilitado */
            if (menu->items[menu->selected].enabled) {
                DEBUG_PRINTF("[MENU] Seleccionado: %d - %s\r\n",
                    menu->selected,
                    menu->items[menu->selected].label);
                return (int)menu->selected;
            }
            return -3;

        case INPUT_BACK:
            DEBUG_PRINT("[MENU] BACK — volviendo\r\n");
            return MENU_RESULT_BACK;

        case INPUT_HOME:
            DEBUG_PRINT("[MENU] HOME — pantalla de inicio\r\n");
            return MENU_RESULT_HOME;

        default:
            return -3;
    }
}

int menu_run(menu_t *menu) {
    if (!menu) return MENU_RESULT_BACK;

    /* Limpiar acciones acumuladas antes de entrar */
    input_flush();

    /* Dibujar el menú inicial */
    menu->needs_redraw = true;
    menu_draw(menu);

    /* Loop principal del menú */
    while (1) {
        /* Hacer polling del input */
        input_poll();

        /* Procesar acción si hay una */
        if (input_has_action()) {
            input_action_t action = input_get_action();
            int result = menu_handle_action(menu, action);

            /* Redibujar si hubo cambios */
            menu_draw(menu);

            /* Salir si hay selección o cancelación */
            if (result >= 0 || result == MENU_RESULT_BACK ||
                result == MENU_RESULT_HOME) {
                return result;
            }
        }

        /* Pequeña pausa para no saturar el bus I2C */
        HAL_Delay(8);
    }
}

uint8_t menu_get_selected(const menu_t *menu) {
    return menu ? menu->selected : 0;
}

void menu_set_selected(menu_t *menu, uint8_t index) {
    if (!menu || index >= menu->item_count) return;
    menu->selected = index;

    /* Ajustar scroll para que el ítem sea visible */
    if (index < menu->scroll_offset) {
        menu->scroll_offset = index;
    } else if (index >= menu->scroll_offset + MENU_VISIBLE_ITEMS) {
        menu->scroll_offset = index - MENU_VISIBLE_ITEMS + 1;
    }
    menu->needs_redraw = true;
}
