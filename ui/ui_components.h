/**
 * @file    ui_components.h
 * @brief   Widgets reutilizables de la UI — WALCORG Fase 4
 *
 * Biblioteca de componentes visuales que implementan el
 * diseño del tema WALCORG. Cada componente es stateless
 * (no guarda estado interno) — el llamador decide cuándo
 * y cómo dibujarlo.
 *
 * COMPONENTES DISPONIBLES:
 *   ui_topbar()        → Barra superior con título e ícono
 *   ui_bottombar()     → Barra inferior con botones
 *   ui_button()        → Botón rectangular con texto
 *   ui_card()          → Contenedor con borde redondeado
 *   ui_list_item()     → Ítem de lista con separador
 *   ui_progress_bar()  → Barra de progreso con porcentaje
 *   ui_dialog()        → Diálogo modal de confirmación
 *   ui_spinner()       → Indicador de carga animado
 *   ui_toast()         → Notificación temporal en overlay
 *   ui_address_box()   → Caja especial para direcciones Bitcoin
 *   ui_amount()        → Mostrar monto BTC con formato
 *   ui_pin_dots()      → Indicador de PIN (puntos)
 *   ui_word_chip()     → Chip para palabras de semilla BIP39
 *
 * @license GNU GPL v3
 */

#ifndef UI_COMPONENTS_H
#define UI_COMPONENTS_H

#include <stdint.h>
#include <stdbool.h>
#include "ui_theme.h"
#include "display_tft.h"

/* =========================================================
 * TIPOS COMUNES
 * ========================================================= */

/** Estado visual de un componente */
typedef enum {
    UI_STATE_NORMAL   = 0,
    UI_STATE_SELECTED,     /**< Elemento seleccionado / con foco   */
    UI_STATE_PRESSED,      /**< Elemento siendo presionado         */
    UI_STATE_DISABLED,     /**< Elemento deshabilitado             */
} ui_state_t;

/** Variante de color de un botón */
typedef enum {
    UI_BTN_PRIMARY  = 0,   /**< Azul — acción principal            */
    UI_BTN_SUCCESS,        /**< Verde — confirmación               */
    UI_BTN_WARNING,        /**< Naranja — precaución               */
    UI_BTN_ERROR,          /**< Rojo — cancelar / peligro          */
    UI_BTN_GHOST,          /**< Sin relleno — acción secundaria    */
} ui_btn_variant_t;

/** Posición del ícono en un componente */
typedef enum {
    UI_ICON_NONE  = 0,
    UI_ICON_LEFT,
    UI_ICON_RIGHT,
    UI_ICON_CENTER,
} ui_icon_pos_t;


/* =========================================================
 * API — ESTRUCTURA DE PANTALLA
 * ========================================================= */

/**
 * @brief Dibuja la barra superior con título y opcionalmente un ícono.
 *
 * @param title      Texto del título (centrado)
 * @param icon_char  Carácter de ícono a la izquierda (0=ninguno)
 * @param bg_color   Color de fondo de la barra
 */
void ui_topbar(const char *title, char icon_char, tft_color_t bg_color);

/**
 * @brief Dibuja la barra inferior con hasta 2 botones.
 *
 * @param left_label   Etiqueta del botón izquierdo (NULL=ocultar)
 * @param left_color   Color del botón izquierdo
 * @param right_label  Etiqueta del botón derecho (NULL=ocultar)
 * @param right_color  Color del botón derecho
 */
void ui_bottombar(const char *left_label,  tft_color_t left_color,
                  const char *right_label, tft_color_t right_color);

/**
 * @brief Limpia el área de contenido con el color de fondo.
 * Equivalente a tft_fill_rect del área entre topbar y bottombar.
 */
void ui_clear_content(void);


/* =========================================================
 * API — BOTONES
 * ========================================================= */

/**
 * @brief Dibuja un botón rectangular.
 *
 * @param x, y     Posición de la esquina superior izquierda
 * @param w, h     Dimensiones del botón
 * @param label    Texto del botón (centrado)
 * @param variant  Variante de color (primary, success, etc.)
 * @param state    Estado visual (normal, selected, disabled)
 */
void ui_button(tft_coord_t x, tft_coord_t y,
               tft_coord_t w, tft_coord_t h,
               const char *label,
               ui_btn_variant_t variant, ui_state_t state);

/**
 * @brief Dibuja un par de botones NO / SÍ en la barra inferior.
 * Patrón muy común en pantallas de confirmación.
 *
 * @param no_label   Texto del botón NO (izquierda)
 * @param yes_label  Texto del botón SÍ (derecha)
 * @param yes_focus  true si el foco está en el botón SÍ
 */
void ui_confirm_buttons(const char *no_label, const char *yes_label,
                        bool yes_focus);


/* =========================================================
 * API — CONTENEDORES
 * ========================================================= */

/**
 * @brief Dibuja una card (contenedor con borde redondeado).
 *
 * @param x, y   Posición de la esquina superior izquierda
 * @param w, h   Dimensiones de la card
 * @param color  Color de fondo (THEME_BG_CARD por defecto)
 */
void ui_card(tft_coord_t x, tft_coord_t y,
             tft_coord_t w, tft_coord_t h,
             tft_color_t color);

/**
 * @brief Dibuja un ítem de lista con separador inferior.
 *
 * @param x, y       Posición
 * @param w          Ancho del ítem
 * @param label      Texto principal
 * @param sublabel   Texto secundario (NULL=ocultar)
 * @param value      Valor a mostrar a la derecha (NULL=ocultar)
 * @param state      Estado visual
 */
void ui_list_item(tft_coord_t x, tft_coord_t y, tft_coord_t w,
                  const char *label, const char *sublabel,
                  const char *value, ui_state_t state);

/**
 * @brief Dibuja un separador horizontal.
 *
 * @param y      Posición vertical
 * @param color  Color de la línea (THEME_SEPARATOR por defecto)
 */
void ui_separator(tft_coord_t y, tft_color_t color);


/* =========================================================
 * API — INDICADORES
 * ========================================================= */

/**
 * @brief Dibuja una barra de progreso.
 *
 * @param x, y     Posición
 * @param w        Ancho total de la barra
 * @param progress Progreso de 0 a 100 (porcentaje)
 * @param color    Color del relleno (THEME_PRIMARY por defecto)
 * @param show_pct true para mostrar el % encima de la barra
 */
void ui_progress_bar(tft_coord_t x, tft_coord_t y, tft_coord_t w,
                     uint8_t progress, tft_color_t color, bool show_pct);

/**
 * @brief Dibuja un spinner de carga animado (un frame).
 *
 * Llamar repetidamente con frame++ para la animación.
 * 8 frames totales (0-7) que rotan el spinner.
 *
 * @param cx, cy  Centro del spinner
 * @param r       Radio del spinner
 * @param frame   Frame actual (0-7, se repite cíclicamente)
 * @param color   Color del spinner
 */
void ui_spinner(tft_coord_t cx, tft_coord_t cy, tft_coord_t r,
                uint8_t frame, tft_color_t color);

/**
 * @brief Dibuja los puntos de PIN (asteriscos o círculos).
 *
 * @param cx       Centro horizontal
 * @param y        Posición vertical
 * @param filled   Número de dígitos ya ingresados
 * @param total    Longitud total esperada del PIN
 * @param current  Índice del punto activo (resaltado)
 */
void ui_pin_dots(tft_coord_t cx, tft_coord_t y,
                 uint8_t filled, uint8_t total, uint8_t current);


/* =========================================================
 * API — ELEMENTOS ESPECÍFICOS DEL WALLET
 * ========================================================= */

/**
 * @brief Dibuja una caja para mostrar una dirección Bitcoin.
 *
 * Diseñada para que la dirección sea fácil de leer y verificar.
 * Usa fuente pequeña para caber en una línea y un fondo
 * diferenciado para destacar la dirección.
 *
 * @param x, y    Posición
 * @param w       Ancho de la caja
 * @param address String de la dirección Bitcoin
 */
void ui_address_box(tft_coord_t x, tft_coord_t y, tft_coord_t w,
                    const char *address);

/**
 * @brief Muestra un monto Bitcoin con formato.
 *
 * Formato: "0.00123456 BTC" con el valor principal grande
 * y "BTC" en tamaño menor.
 *
 * @param cx      Centro horizontal
 * @param y       Posición vertical del valor
 * @param amount  String del monto (ej: "0.00123456")
 * @param unit    Unidad (ej: "BTC", "sat")
 * @param color   Color del texto (THEME_TEXT_PRIMARY por defecto)
 */
void ui_amount(tft_coord_t cx, tft_coord_t y,
               const char *amount, const char *unit,
               tft_color_t color);

/**
 * @brief Dibuja un chip de palabra de semilla BIP39.
 *
 * Muestra el número de la palabra y la palabra en sí
 * dentro de un chip redondeado con fondo diferenciado.
 *
 * @param x, y     Posición
 * @param number   Número de la palabra (1-24)
 * @param word     La palabra BIP39
 * @param revealed true para mostrar la palabra, false para ocultarla
 */
void ui_word_chip(tft_coord_t x, tft_coord_t y,
                  uint8_t number, const char *word, bool revealed);


/* =========================================================
 * API — OVERLAY Y NOTIFICACIONES
 * ========================================================= */

/**
 * @brief Muestra un diálogo modal de confirmación.
 *
 * Superpone un diálogo sobre la pantalla actual.
 * Bloquea hasta que el usuario confirma o cancela.
 *
 * @param title    Título del diálogo
 * @param message  Mensaje descriptivo
 * @param yes_btn  Texto del botón de confirmación
 * @param no_btn   Texto del botón de cancelación
 * @return         true si confirmó, false si canceló
 */
bool ui_dialog(const char *title, const char *message,
               const char *yes_btn, const char *no_btn);

/**
 * @brief Muestra un toast (notificación temporal no bloqueante).
 *
 * Aparece en la parte inferior de la pantalla y desaparece
 * automáticamente después de duration_ms.
 *
 * @param message     Texto de la notificación
 * @param duration_ms Duración en ms (2000 por defecto)
 * @param color       Color del toast
 */
void ui_toast(const char *message, uint32_t duration_ms, tft_color_t color);

/**
 * @brief Muestra una pantalla de carga (bloquea hasta completar).
 *
 * Muestra un spinner animado con un mensaje y opcionalmente
 * una barra de progreso.
 *
 * @param message    Texto descriptivo
 * @param duration_ms Duración de la carga (0=indefinido, necesita ui_loading_end)
 */
void ui_loading_start(const char *message, uint32_t duration_ms);

/**
 * @brief Actualiza el progreso de la pantalla de carga activa.
 * @param progress Progreso 0-100
 */
void ui_loading_progress(uint8_t progress);

/**
 * @brief Finaliza la pantalla de carga.
 * @param success true=mostrar check verde, false=mostrar X roja
 * @param message Mensaje final (NULL=usar el original)
 */
void ui_loading_end(bool success, const char *message);

#endif /* UI_COMPONENTS_H */
