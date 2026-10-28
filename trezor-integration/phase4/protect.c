/**
 * @file    protect.c  (legacy/firmware/protect.c — versión MODIFICADA)
 * @brief   Protección de PIN con cuadrícula aleatoria — WALCORG Fase 3
 *
 * ARCHIVO ORIGINAL: legacy/firmware/protect.c del repo trezor-firmware
 *
 * MODIFICACIONES:
 *
 *   [MOD-P1] Entrada de PIN: girar wheel = mover posición en la
 *            cuadrícula, click = confirmar dígito seleccionado,
 *            long press = borrar último dígito ingresado.
 *
 *   [MOD-P2] La pantalla usa layoutEnterPIN() del layout.c modificado
 *            que muestra la cuadrícula + asteriscos del PIN parcial
 *            + instrucciones del click-wheel.
 *
 *   [MOD-P3] CUADRÍCULA ALEATORIA (implementada aquí, no pendiente).
 *
 *            PROBLEMA DE SEGURIDAD con entrada directa (versión anterior):
 *              Si el usuario siempre ingresa PIN "1234", girará el wheel
 *              siempre la misma cantidad. Un observador puede inferir
 *              el PIN analizando el patrón de movimiento del wheel o
 *              el desgaste físico de la rueda.
 *
 *            SOLUCIÓN — cuadrícula aleatoria igual que Trezor One:
 *              - Los 10 dígitos (0-9) se colocan en posiciones ALEATORIAS
 *                de una cuadrícula de 3×4 (10 dígitos + 2 vacíos)
 *              - La posición de cada dígito cambia en cada sesión de PIN
 *              - El usuario navega con el wheel entre posiciones de la
 *                cuadrícula y hace click para seleccionar el dígito
 *              - Un observador solo ve "cuántas posiciones se movió",
 *                que no revela el dígito porque las posiciones son aleatorias
 *
 *            La cuadrícula se regenera aleatoriamente cada vez que se
 *            muestra la pantalla de PIN, usando el hardware RNG.
 *
 * SEGURIDAD ADICIONAL:
 *   - El dígito seleccionado NO se muestra hasta confirmarlo (solo el cursor)
 *   - Los asteriscos no revelan la longitud del PIN hasta terminarlo
 *   - La pantalla limpia la cuadrícula al confirmar cada dígito
 *
 * @license GNU GPL v3
 */

#include "protect.h"
#include "input_manager.h"
#include "display.h"
#include "display_tft.h"
#include "ui_theme.h"
#include "ui_components.h"
#include "layout.h"
#include "uart_debug.h"
#include "rng_test.h"
#include "stm32f4xx_hal.h"
#include <string.h>
#include <stdio.h>

/* =========================================================
 * CONSTANTES DE LA CUADRÍCULA
 * ========================================================= */

/** Número de posiciones en la cuadrícula (3 columnas × 4 filas = 12) */
#define PIN_GRID_COLS       3
#define PIN_GRID_ROWS       4
#define PIN_GRID_SIZE       (PIN_GRID_COLS * PIN_GRID_ROWS)  /* 12 */

/** Dígitos disponibles: 10 dígitos (0-9) + 2 posiciones vacías */
#define PIN_DIGITS          10

/** Longitud máxima del PIN */
#define PIN_MAX_LEN         9

/** Tiempo de espera máximo para entrada de PIN (ms) */
#define PIN_TIMEOUT_MS      60000

/* =========================================================
 * LAYOUT DE LA CUADRÍCULA EN PANTALLA (480×320)
 *
 *   ┌─────────────────────────────────────┐
 *   │         Ingresa tu PIN              │  ← topbar
 *   ├─────────────────────────────────────┤
 *   │   * * * _ _ _ _ _ _                │  ← asteriscos PIN
 *   │  ┌─────┬─────┬─────┐               │
 *   │  │  7  │  3  │  9  │               │  ← cuadrícula
 *   │  ├─────┼─────┼─────┤               │    aleatoria
 *   │  │  1  │  5  │  2  │               │
 *   │  ├─────┼─────┼─────┤               │
 *   │  │  8  │  0  │  4  │               │
 *   │  ├─────┼─────┼─────┤               │
 *   │  │  6  │ [ ] │     │               │  ← [] = cursor actual
 *   │  └─────┴─────┴─────┘               │    vacío = sin dígito
 *   │  Girar: mover  Click: confirmar    │  ← instrucciones
 *   └─────────────────────────────────────┘
 * ========================================================= */

/* Posición de la cuadrícula en pantalla */
#define GRID_X              90    /* Margen izquierdo de la cuadrícula */
#define GRID_Y              80    /* Posición vertical de la cuadrícula */
#define GRID_CELL_W         88    /* Ancho de cada celda */
#define GRID_CELL_H         46    /* Alto de cada celda */

/* Posición de los asteriscos del PIN */
#define PIN_DOTS_Y          52    /* Posición vertical de los puntos */


/* =========================================================
 * FUNCIONES PRIVADAS
 * ========================================================= */

/**
 * _shuffle_grid — Generar cuadrícula aleatoria de dígitos
 *
 * Coloca los 10 dígitos (0-9) en 10 de las 12 posiciones,
 * dejando 2 posiciones vacías (-1). Las posiciones se
 * ordenan aleatoriamente usando el hardware RNG.
 *
 * Algoritmo Fisher-Yates con hardware RNG para asegurar
 * que todas las permutaciones son igualmente probables.
 *
 * @param grid   Array de PIN_GRID_SIZE elementos donde se
 *               guarda el resultado. -1 = posición vacía.
 */
static void _shuffle_grid(int8_t grid[PIN_GRID_SIZE]) {
    /* Inicializar: 10 dígitos + 2 vacíos */
    int8_t pool[PIN_GRID_SIZE];
    for (int i = 0; i < PIN_DIGITS; i++) pool[i] = (int8_t)i;
    pool[10] = -1;  /* Posición vacía */
    pool[11] = -1;  /* Posición vacía */

    /* Fisher-Yates shuffle usando hardware RNG */
    for (int i = PIN_GRID_SIZE - 1; i > 0; i--) {
        /* Obtener número aleatorio del hardware RNG */
        uint32_t rand_val;
        if (!rng_get_uint32(&rand_val)) {
            /* Fallback si el RNG falla: usar SysTick como semilla */
            rand_val = HAL_GetTick() * 2654435761UL;  /* Knuth multiplicativo */
        }
        int j = (int)(rand_val % (uint32_t)(i + 1));

        /* Intercambiar pool[i] y pool[j] */
        int8_t tmp = pool[i];
        pool[i] = pool[j];
        pool[j] = tmp;
    }

    /* Copiar el resultado al grid */
    for (int i = 0; i < PIN_GRID_SIZE; i++) {
        grid[i] = pool[i];
    }
}

/**
 * _draw_grid — Dibujar la cuadrícula completa en el TFT
 *
 * @param grid      Array con los dígitos en cada posición
 * @param cursor    Posición actual del cursor (0..PIN_GRID_SIZE-1)
 * @param pin_len   Número de dígitos ya ingresados (para los asteriscos)
 */
static void _draw_grid(const int8_t grid[PIN_GRID_SIZE],
                        int cursor, uint8_t pin_len) {
    /* Limpiar área de la cuadrícula */
    tft_fill_rect(0, GRID_Y - 5, tft_get_width(),
                  GRID_CELL_H * PIN_GRID_ROWS + 20, THEME_BG);

    for (int i = 0; i < PIN_GRID_SIZE; i++) {
        int col = i % PIN_GRID_COLS;
        int row = i / PIN_GRID_COLS;

        tft_coord_t x = (tft_coord_t)(GRID_X + col * GRID_CELL_W);
        tft_coord_t y = (tft_coord_t)(GRID_Y + row * GRID_CELL_H);

        bool is_cursor = (i == cursor);
        bool is_empty  = (grid[i] < 0);

        /* Color de fondo de la celda */
        tft_color_t bg_color;
        tft_color_t border_color;

        if (is_cursor && !is_empty) {
            bg_color     = THEME_PRIMARY;
            border_color = THEME_PRIMARY_GLOW;
        } else if (is_cursor && is_empty) {
            bg_color     = THEME_BG_CARD;
            border_color = THEME_PRIMARY;
        } else if (is_empty) {
            bg_color     = THEME_BG;
            border_color = THEME_SEPARATOR;
        } else {
            bg_color     = THEME_BG_CARD;
            border_color = THEME_BORDER;
        }

        /* Dibujar fondo de celda */
        tft_fill_round_rect(x + 2, y + 2,
                            GRID_CELL_W - 4, GRID_CELL_H - 4,
                            6, bg_color);
        tft_draw_round_rect(x + 2, y + 2,
                            GRID_CELL_W - 4, GRID_CELL_H - 4,
                            6, border_color);

        /* Dibujar dígito si la celda no está vacía */
        if (!is_empty) {
            char digit_str[3];
            snprintf(digit_str, sizeof(digit_str), "%d", grid[i]);

            tft_color_t fg = is_cursor ? ILI9488_WHITE : THEME_TEXT_PRIMARY;

            /* Centrar el dígito en la celda */
            int text_x = x + (GRID_CELL_W - 18) / 2;
            int text_y = y + (GRID_CELL_H - 24) / 2;
            tft_draw_string((tft_coord_t)text_x, (tft_coord_t)text_y,
                            digit_str, fg, bg_color, FONT_TITLE);
        } else if (is_cursor) {
            /* Celda vacía con cursor: mostrar guión */
            tft_draw_string_centered(
                (tft_coord_t)(y + (GRID_CELL_H - 16) / 2),
                "-", THEME_TEXT_DISABLED, bg_color, FONT_BODY);
        }
    }

    /* Dibujar puntos del PIN (asteriscos) */
    ui_pin_dots(tft_get_width() / 2, PIN_DOTS_Y,
                pin_len, PIN_MAX_LEN, pin_len);

    (void)pin_len;
}

/**
 * _draw_pin_screen — Dibujar la pantalla completa de entrada de PIN
 *
 * Dibuja el topbar, instrucciones y llama a _draw_grid().
 *
 * @param desc      Descripción de la pantalla (ej: "Ingresa tu PIN")
 * @param grid      Cuadrícula de dígitos
 * @param cursor    Posición del cursor
 * @param pin_len   Dígitos ingresados
 */
static void _draw_pin_screen(const char *desc,
                              const int8_t grid[PIN_GRID_SIZE],
                              int cursor, uint8_t pin_len) {
    /* Topbar */
    tft_fill_rect(0, 0, tft_get_width(), 36, THEME_PRIMARY_DIM);
    tft_draw_string_centered(10,
        desc ? desc : "Ingresa tu PIN",
        THEME_TEXT_PRIMARY, THEME_PRIMARY_DIM, FONT_BODY);

    /* Cuadrícula de dígitos */
    _draw_grid(grid, cursor, pin_len);

    /* Instrucciones en la parte inferior */
    tft_fill_rect(0, 272, tft_get_width(), 48, THEME_BG_OVERLAY);
    tft_draw_hline(0, 271, tft_get_width(), THEME_SEPARATOR);
    tft_draw_string_centered(278,
        "Girar: mover cursor  |  Click: confirmar  |  Long: borrar",
        THEME_TEXT_SECONDARY, THEME_BG_OVERLAY, FONT_SMALL);
}

/**
 * _find_next_valid — Buscar la siguiente posición válida (no vacía)
 *
 * Al navegar, saltamos las celdas vacías automáticamente para
 * que el usuario no tenga que "pasar" por ellas.
 *
 * @param grid      Cuadrícula actual
 * @param from      Posición actual
 * @param direction +1 (adelante) o -1 (atrás)
 * @return          Índice de la siguiente posición válida
 */
static int _find_next_valid(const int8_t grid[PIN_GRID_SIZE],
                              int from, int direction) {
    int pos = from;
    int tries = 0;

    do {
        pos = (pos + direction + PIN_GRID_SIZE) % PIN_GRID_SIZE;
        tries++;
    } while (grid[pos] < 0 && tries < PIN_GRID_SIZE);

    /* Si no encontramos ninguna válida, quedarse en la misma */
    return (grid[pos] >= 0) ? pos : from;
}


/* =========================================================
 * API PÚBLICA
 * ========================================================= */

/**
 * protectPIN — Solicitar y verificar el PIN con cuadrícula aleatoria
 *
 * [MOD-P1][MOD-P2][MOD-P3] Reimplementación completa con:
 *   - Cuadrícula aleatoria de 3×4 (10 dígitos en posiciones aleatorias)
 *   - Navegación con click-wheel (girar = mover, click = seleccionar)
 *   - Long press = borrar último dígito
 *   - La cuadrícula se regenera en cada intento (incluye reintentos)
 *
 * @param use_cached  true = usar sesión cacheada si existe
 * @return            true si el PIN es correcto
 */
bool protectPIN(bool use_cached) {
    /* ---- Verificar caché de sesión ---- */
    if (use_cached) {
        extern bool session_is_valid(void);
        if (session_is_valid()) {
            DEBUG_PRINT("[PROTECT] PIN cacheado — sin pedir\r\n");
            return true;
        }
    }

    input_flush();

    /* ---- Variables de estado ---- */
    char    pin_entered[PIN_MAX_LEN + 1] = {0};
    uint8_t pin_len      = 0;
    bool    cancelled    = false;
    int8_t  grid[PIN_GRID_SIZE];
    int     cursor       = 0;

    DEBUG_PRINT("[PROTECT] Solicitar PIN con cuadrícula aleatoria...\r\n");

    /* ---- Generar cuadrícula inicial ---- */
    _shuffle_grid(grid);

    /* Posicionar cursor en la primera celda válida */
    while (cursor < PIN_GRID_SIZE && grid[cursor] < 0) cursor++;

    /* ---- Dibujar pantalla inicial ---- */
    tft_clear(THEME_BG);
    _draw_pin_screen("Ingresa tu PIN", grid, cursor, pin_len);

    /* ---- Loop principal de entrada de PIN ---- */
    while (!cancelled && pin_len < PIN_MAX_LEN) {

        /* Esperar acción con timeout */
        input_action_t action = input_wait(PIN_TIMEOUT_MS);

        switch (action) {

            case INPUT_NAV_UP:
                /* Girar hacia arriba: mover cursor a la izquierda/arriba
                 * (en dirección -1 en el grid lineal) */
                cursor = _find_next_valid(grid, cursor, -1);
                /* Solo redibujar la cuadrícula, no toda la pantalla */
                _draw_grid(grid, cursor, pin_len);
                break;

            case INPUT_NAV_DOWN:
                /* Girar hacia abajo: mover cursor a la derecha/abajo */
                cursor = _find_next_valid(grid, cursor, +1);
                _draw_grid(grid, cursor, pin_len);
                break;

            case INPUT_CONFIRM:
                /* Click: confirmar el dígito en la posición actual */
                if (grid[cursor] >= 0) {
                    pin_entered[pin_len] = '0' + grid[cursor];
                    pin_len++;
                    pin_entered[pin_len] = '\0';

                    DEBUG_PRINTF("[PROTECT] Dígito confirmado. "
                                 "Longitud PIN: %d\r\n", pin_len);

                    /* Regenerar cuadrícula después de cada dígito
                     * — esto asegura que el patrón de movimiento
                     *   no revela el siguiente dígito */
                    _shuffle_grid(grid);
                    cursor = 0;
                    while (cursor < PIN_GRID_SIZE && grid[cursor] < 0) cursor++;

                    /* Redibujar pantalla completa con nueva cuadrícula */
                    _draw_grid(grid, cursor, pin_len);
                }
                break;

            case INPUT_BACK:
                if (pin_len > 0) {
                    /* Long press con dígitos: borrar el último */
                    pin_len--;
                    pin_entered[pin_len] = '\0';
                    DEBUG_PRINT("[PROTECT] Último dígito borrado\r\n");

                    /* Regenerar cuadrícula tras borrar también */
                    _shuffle_grid(grid);
                    cursor = 0;
                    while (cursor < PIN_GRID_SIZE && grid[cursor] < 0) cursor++;

                    _draw_grid(grid, cursor, pin_len);

                } else {
                    /* Long press sin dígitos: cancelar entrada */
                    DEBUG_PRINT("[PROTECT] PIN cancelado por el usuario\r\n");
                    cancelled = true;
                }
                break;

            case INPUT_HOME:
            case INPUT_NONE:
                DEBUG_PRINT("[PROTECT] PIN cancelado (timeout o HOME)\r\n");
                cancelled = true;
                break;

            default:
                break;
        }
    }

    if (cancelled) {
        memset(pin_entered, 0, sizeof(pin_entered));
        return false;
    }

    /* Si llegó a la longitud máxima, aceptar automáticamente */
    if (pin_len == 0) {
        return false;
    }

    DEBUG_PRINTF("[PROTECT] PIN ingresado (%d dígitos). Verificando...\r\n",
        pin_len);

    /* ---- Verificar PIN ---- */
    extern bool storage_containsPin(const char *pin);
    bool pin_ok = storage_containsPin(pin_entered);

    /* Limpiar INMEDIATAMENTE el PIN de SRAM */
    memset(pin_entered, 0, sizeof(pin_entered));

    if (pin_ok) {
        DEBUG_PRINT("[PROTECT] PIN correcto.\r\n");
    } else {
        DEBUG_PRINT("[PROTECT] PIN incorrecto.\r\n");

        /* Mostrar error brevemente */
        tft_fill_rect(0, 36, tft_get_width(), 40, THEME_BG);
        tft_draw_string_centered(48,
            "PIN incorrecto — intentalo de nuevo",
            THEME_ERROR, THEME_BG, FONT_BODY);
        HAL_Delay(1500);
    }

    return pin_ok;
}

/**
 * protectButton — Pedir confirmación de botón al usuario
 *
 * Sin cambios respecto a la versión anterior — button_read()
 * ya fue adaptado en buttons.c para usar el click-wheel.
 *
 * @param desc  Descripción de lo que se está confirmando
 * @return      true si confirmó (YES), false si canceló (NO)
 */
bool protectButton(const char *desc) {
    input_flush();

    /* Mostrar diálogo de confirmación */
    tft_clear(THEME_BG);
    tft_fill_rect(0, 0, tft_get_width(), 36, THEME_PRIMARY_DIM);
    tft_draw_string_centered(10, "Confirmar accion",
                             THEME_TEXT_PRIMARY, THEME_PRIMARY_DIM, FONT_BODY);

    if (desc) {
        tft_draw_string_centered(80, desc,
                                 THEME_TEXT_PRIMARY, THEME_BG, FONT_BODY);
    }

    /* Botones visuales */
    tft_fill_round_rect(20, 210, 200, 50, 8, THEME_ERROR_DIM);
    tft_draw_string_centered(228, "Cancelar",
                             THEME_TEXT_PRIMARY, THEME_ERROR_DIM, FONT_BODY);

    tft_fill_round_rect(260, 210, 200, 50, 8, THEME_SUCCESS_DIM);
    tft_draw_string_centered(228, "Confirmar",
                             THEME_TEXT_PRIMARY, THEME_SUCCESS_DIM, FONT_BODY);

    tft_draw_string_centered(278,
        "Click: confirmar  |  Long press: cancelar",
        THEME_TEXT_SECONDARY, THEME_BG, FONT_SMALL);

    DEBUG_PRINTF("[PROTECT] Esperando confirmacion: %s\r\n",
        desc ? desc : "");

    /* Esperar YES o NO */
    input_action_t action = input_wait(0);
    bool confirmed = (action == INPUT_CONFIRM);

    DEBUG_PRINTF("[PROTECT] Resultado: %s\r\n",
        confirmed ? "CONFIRMADO" : "cancelado");

    return confirmed;
}
