/**
 * @file    input_manager.h
 * @brief   Gestor de entrada — abstracción del click-wheel — WalCorg Fase 3
 *
 * PROPÓSITO:
 *   El input_manager es la capa entre el hardware del click-wheel
 *   (AS5600 via I2C, manejado por clickwheel.c) y el resto del
 *   firmware (menús, PIN, confirmaciones).
 *
 *   Sin esta capa, cada parte del firmware tendría que llamar
 *   directamente a cw_poll() y manejar sus propios eventos.
 *   Con esta capa, el firmware solo llama input_get_action()
 *   y recibe una acción semántica de alto nivel.
 *
 * ARQUITECTURA:
 *
 *   Hardware (AS5600)
 *       ↓  I2C
 *   clickwheel.c  →  cw_event_t (SCROLL_UP, CLICK, etc.)
 *       ↓
 *   input_manager.c  →  input_action_t (NAV_UP, CONFIRM, BACK, etc.)
 *       ↓
 *   menu.c / protect.c / layout.c
 *
 * MAPEO DE EVENTOS:
 *
 *   cw_event_t          →  input_action_t
 *   ──────────────────────────────────────
 *   CW_EVENT_SCROLL_UP   →  INPUT_NAV_UP
 *   CW_EVENT_SCROLL_DOWN →  INPUT_NAV_DOWN
 *   CW_EVENT_CLICK       →  INPUT_CONFIRM
 *   CW_EVENT_LONG_PRESS  →  INPUT_BACK
 *   CW_EVENT_DOUBLE_CLICK→  INPUT_HOME
 *   (ninguno)            →  INPUT_NONE
 *
 * COMPATIBILIDAD CON TREZOR:
 *   Trezor legacy usa BTN_YES y BTN_NO (2 botones físicos).
 *   input_manager expone button_read() que retorna los flags
 *   compatibles con buttons.c de Trezor:
 *     INPUT_CONFIRM → BTN_YES
 *     INPUT_BACK    → BTN_NO
 *
 * @license GNU GPL v3
 */

#ifndef INPUT_MANAGER_H
#define INPUT_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "clickwheel.h"

/* =========================================================
 * TIPOS DE ACCIÓN
 * ========================================================= */

/**
 * Acciones semánticas del dispositivo de entrada.
 * Son independientes del hardware — si mañana cambias el
 * click-wheel por botones o touch, solo cambias input_manager.c.
 */
typedef enum {
    INPUT_NONE        = 0,   /**< Sin acción                              */
    INPUT_NAV_UP,            /**< Navegar hacia arriba / valor anterior   */
    INPUT_NAV_DOWN,          /**< Navegar hacia abajo / valor siguiente   */
    INPUT_CONFIRM,           /**< Confirmar selección actual (YES)        */
    INPUT_BACK,              /**< Volver / cancelar / NO                  */
    INPUT_HOME,              /**< Ir a la pantalla de inicio              */
} input_action_t;

/**
 * Flags de botón compatibles con Trezor legacy (buttons.h)
 * El firmware de Trezor llama button_read() y verifica estos bits.
 */
#define BTN_YES    (1 << 1)   /**< Botón de confirmación (derecho en Trezor) */
#define BTN_NO     (1 << 0)   /**< Botón de cancelación  (izquierdo en Trezor) */

/* =========================================================
 * CONFIGURACIÓN
 * ========================================================= */

/** Velocidad de scroll: cuántos NAV_UP/DOWN por segundo al mantener girado */
#define INPUT_SCROLL_REPEAT_MS      150

/** Tiempo de espera antes de activar el scroll automático */
#define INPUT_SCROLL_ACCEL_DELAY_MS 500

/* =========================================================
 * API PÚBLICA
 * ========================================================= */

/**
 * @brief Inicializa el gestor de entrada.
 *
 * Inicializa el click-wheel (cw_init) y configura el
 * estado interno del input manager.
 *
 * @return true  si el AS5600 respondió correctamente
 * @return false si hay error de hardware (I2C o pull-ups)
 */
bool input_init(void);

/**
 * @brief Polling del input — llamar cada ~10ms en el loop principal.
 *
 * Lee el estado del click-wheel y actualiza el estado interno.
 * No retorna la acción — usa input_get_action() para eso.
 * Separar poll() de get_action() permite llamar poll() a
 * alta frecuencia y consumir acciones a baja frecuencia.
 */
void input_poll(void);

/**
 * @brief Obtiene la siguiente acción pendiente.
 *
 * Retorna la acción más reciente y la elimina de la cola interna.
 * Si no hay acción pendiente, retorna INPUT_NONE.
 *
 * @return  La siguiente acción pendiente, o INPUT_NONE
 */
input_action_t input_get_action(void);

/**
 * @brief Versión bloqueante — espera hasta que haya una acción.
 *
 * Bloquea la ejecución hasta recibir cualquier acción.
 * Útil en pantallas de confirmación donde no hay nada más
 * que hacer excepto esperar la respuesta del usuario.
 *
 * @param timeout_ms  Tiempo máximo de espera (0 = sin límite)
 * @return            La acción recibida, o INPUT_NONE si hubo timeout
 */
input_action_t input_wait(uint32_t timeout_ms);

/**
 * @brief Descarta todas las acciones pendientes en la cola.
 *
 * Útil al entrar a una pantalla nueva para asegurar que
 * no hay acciones "heredadas" de la pantalla anterior.
 */
void input_flush(void);

/**
 * @brief Verifica si hay alguna acción pendiente sin consumirla.
 *
 * @return true  si hay al menos una acción en la cola
 */
bool input_has_action(void);

/**
 * @brief Compatibilidad con Trezor — lee el estado de los "botones".
 *
 * Reemplaza la función button_read() de legacy/firmware/buttons.c.
 * Traduce las acciones del click-wheel a los flags BTN_YES / BTN_NO
 * que usa el firmware de Trezor.
 *
 * @return  Bitmask: BTN_YES si CONFIRM pendiente, BTN_NO si BACK,
 *          0 si no hay botón activo.
 */
uint16_t input_button_read(void);

/**
 * @brief Retorna descripción de la acción como string (debug).
 */
const char *input_action_name(input_action_t action);

/** Estado global del click-wheel — accesible para debug */
extern cw_state_t g_cw_state;

#endif /* INPUT_MANAGER_H */
