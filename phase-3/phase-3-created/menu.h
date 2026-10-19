/**
 * @file    menu.h
 * @brief   Motor de menú navegable con click-wheel — WalCorg Fase 3
 *
 * Motor de menú genérico que:
 *   - Muestra una lista de opciones en el TFT
 *   - Resalta el ítem seleccionado actualmente
 *   - Maneja la navegación con input_manager
 *   - Soporta scroll cuando hay más ítems que caben en pantalla
 *   - Llama un callback cuando se confirma una selección
 *
 * USO:
 *   menu_t m;
 *   menu_item_t items[] = {
 *       { "Recibir BTC",   NULL, MENU_ICON_RECEIVE },
 *       { "Enviar BTC",    NULL, MENU_ICON_SEND    },
 *       { "Ver semilla",   NULL, MENU_ICON_KEY     },
 *       { "Configuracion", NULL, MENU_ICON_SETTINGS},
 *   };
 *   menu_init(&m, "WalCorg", items, 4);
 *   int selected = menu_run(&m);   // Bloquea hasta selección
 *
 * DISEÑO VISUAL (480×320):
 *
 *   ┌─────────────────────────────────────┐
 *   │         WalCorg — Inicio            │  ← Barra título (azul)
 *   ├─────────────────────────────────────┤
 *   │  ▸  Recibir BTC                     │  ← Ítem seleccionado (resaltado)
 *   ├─────────────────────────────────────┤
 *   │     Enviar BTC                      │
 *   ├─────────────────────────────────────┤
 *   │     Ver semilla                     │
 *   ├─────────────────────────────────────┤
 *   │     Configuracion                   │
 *   ├─────────────────────────────────────┤
 *   │  ↑↓ Girar  ·  ● Seleccionar        │  ← Ayuda (barra inferior)
 *   └─────────────────────────────────────┘
 *
 * @license GNU GPL v3
 */

#ifndef MENU_H
#define MENU_H

#include <stdint.h>
#include <stdbool.h>
#include "display_tft.h"

/* =========================================================
 * CONFIGURACIÓN DEL MENÚ
 * ========================================================= */

/** Número máximo de ítems por menú */
#define MENU_MAX_ITEMS          16

/** Número máximo de ítems visibles en pantalla a la vez */
#define MENU_VISIBLE_ITEMS      6

/** Altura de cada ítem en píxeles */
#define MENU_ITEM_HEIGHT        38

/** Posición Y del primer ítem (debajo del título) */
#define MENU_FIRST_ITEM_Y       38

/** Posición Y de la barra de ayuda inferior */
#define MENU_HELP_BAR_Y         280

/** Altura de la barra de ayuda */
#define MENU_HELP_BAR_H         40

/** Tamaño de fuente de los ítems del menú */
#define MENU_ITEM_FONT_SIZE     2

/** Tamaño de fuente del título */
#define MENU_TITLE_FONT_SIZE    2

/** Margen izquierdo del texto de los ítems */
#define MENU_ITEM_MARGIN_X      40

/** Ancho del indicador de selección (barra izquierda) */
#define MENU_SELECTOR_W         6


/* =========================================================
 * ICONOS DE MENÚ
 *
 * Símbolos de texto que se muestran junto a cada ítem.
 * Usamos caracteres ASCII especiales para compatibilidad.
 * En Fase 4 se pueden reemplazar por bitmaps de íconos reales.
 * ========================================================= */
typedef enum {
    MENU_ICON_NONE     = 0,
    MENU_ICON_RECEIVE,     /**< Flecha abajo  → recibir */
    MENU_ICON_SEND,        /**< Flecha arriba → enviar  */
    MENU_ICON_KEY,         /**< Llave → semilla/claves  */
    MENU_ICON_SETTINGS,    /**< Engranaje → configuración*/
    MENU_ICON_INFO,        /**< i → información          */
    MENU_ICON_LOCK,        /**< Candado → seguridad      */
    MENU_ICON_USB,         /**< Conector → USB           */
    MENU_ICON_WARNING,     /**< ! → advertencia          */
} menu_icon_t;


/* =========================================================
 * TIPOS
 * ========================================================= */

/**
 * Un ítem individual del menú.
 * label: texto que se muestra
 * sublabel: texto secundario más pequeño (puede ser NULL)
 * icon: símbolo visual junto al texto
 * enabled: si false, el ítem se muestra gris y no es seleccionable
 */
typedef struct {
    const char  *label;        /**< Texto principal del ítem */
    const char  *sublabel;     /**< Texto secundario (NULL para ocultar) */
    menu_icon_t  icon;         /**< Ícono del ítem */
    bool         enabled;      /**< false = ítem deshabilitado (gris) */
} menu_item_t;

/**
 * Estado completo de un menú.
 * Opaco para el usuario — inicializar con menu_init().
 */
typedef struct {
    const char       *title;           /**< Título del menú */
    const menu_item_t *items;          /**< Array de ítems */
    uint8_t           item_count;      /**< Número de ítems */
    uint8_t           selected;        /**< Índice del ítem seleccionado */
    uint8_t           scroll_offset;   /**< Primer ítem visible (scroll) */
    tft_color_t       title_color;     /**< Color de la barra de título */
    bool              show_back;       /**< Mostrar opción "Volver" automática */
    bool              needs_redraw;    /**< Bandera de redibujado pendiente */
} menu_t;

/** Valor de retorno cuando el usuario presiona BACK/HOME */
#define MENU_RESULT_BACK    -1
#define MENU_RESULT_HOME    -2


/* =========================================================
 * API PÚBLICA
 * ========================================================= */

/**
 * @brief Inicializa un menú con sus ítems y configuración.
 *
 * @param menu         Puntero al struct del menú
 * @param title        Título mostrado en la barra superior
 * @param items        Array de ítems (debe persistir mientras el menú esté activo)
 * @param item_count   Número de ítems en el array
 */
void menu_init(menu_t *menu, const char *title,
               const menu_item_t *items, uint8_t item_count);

/**
 * @brief Cambia el color de la barra de título.
 * Por defecto es WALCORG_PRIMARY (azul).
 *
 * @param menu   Menú a modificar
 * @param color  Nuevo color de la barra
 */
void menu_set_title_color(menu_t *menu, tft_color_t color);

/**
 * @brief Dibuja el menú completo en el display.
 *
 * Dibuja la barra de título, todos los ítems visibles
 * y la barra de ayuda inferior. Solo redibuja si
 * needs_redraw es true.
 *
 * @param menu  Menú a dibujar
 */
void menu_draw(menu_t *menu);

/**
 * @brief Fuerza un redibujado completo en la próxima llamada a menu_draw.
 */
void menu_invalidate(menu_t *menu);

/**
 * @brief Procesa una acción de input y actualiza el estado del menú.
 *
 * @param menu    Menú a actualizar
 * @param action  Acción recibida del input_manager
 * @return        Índice del ítem seleccionado si CONFIRM,
 *                MENU_RESULT_BACK si BACK/HOME,
 *                -3 si la acción solo navegó (sin selección).
 */
int menu_handle_action(menu_t *menu, input_action_t action);

/**
 * @brief Ejecuta el menú de forma bloqueante hasta que se seleccione algo.
 *
 * Loop interno: input_poll → menu_handle_action → menu_draw
 * Sale cuando el usuario confirma una selección o presiona BACK.
 *
 * @param menu  Menú a ejecutar
 * @return      Índice del ítem seleccionado (0-based),
 *              MENU_RESULT_BACK si canceló,
 *              MENU_RESULT_HOME si doble-click.
 */
int menu_run(menu_t *menu);

/**
 * @brief Retorna el índice del ítem actualmente seleccionado.
 */
uint8_t menu_get_selected(const menu_t *menu);

/**
 * @brief Mueve la selección a un índice específico.
 * Útil para recordar la posición anterior al volver a un menú.
 *
 * @param menu   Menú a modificar
 * @param index  Índice del ítem a seleccionar
 */
void menu_set_selected(menu_t *menu, uint8_t index);

#endif /* MENU_H */
