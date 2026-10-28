/**
 * @file    input_manager.c
 * @brief   Gestor de entrada — WALCORG Fase 3
 *
 * Implementa la capa de abstracción entre el hardware del
 * click-wheel y el resto del firmware. Mantiene una cola
 * circular de acciones pendientes de tamaño fijo para
 * desacoplar la frecuencia de polling de la frecuencia
 * de consumo.
 *
 * @license GNU GPL v3
 */

#include "input_manager.h"
#include "uart_debug.h"
#include "stm32f4xx_hal.h"

/* =========================================================
 * COLA CIRCULAR DE ACCIONES
 * ========================================================= */

/** Tamaño máximo de la cola de acciones pendientes */
#define INPUT_QUEUE_SIZE    8

/** Cola circular de acciones */
static input_action_t _queue[INPUT_QUEUE_SIZE];
static uint8_t        _queue_head = 0;   /* Índice de lectura  */
static uint8_t        _queue_tail = 0;   /* Índice de escritura */
static uint8_t        _queue_count = 0;  /* Elementos en cola  */

/** Estado global del click-wheel — accesible para debug */
cw_state_t g_cw_state;

/** Estado de inicialización */
static bool _initialized = false;

/** Último estado del "botón virtual" para compatibilidad Trezor */
static uint16_t _last_btn_state = 0;


/* =========================================================
 * FUNCIONES PRIVADAS DE LA COLA
 * ========================================================= */

/**
 * _queue_push — Añadir una acción a la cola
 *
 * Si la cola está llena, descarta la acción más antigua
 * (política drop-oldest) para no bloquear el sistema.
 */
static void _queue_push(input_action_t action) {
    if (action == INPUT_NONE) return;

    if (_queue_count >= INPUT_QUEUE_SIZE) {
        /* Cola llena — descartar la más antigua */
        _queue_head = (_queue_head + 1) % INPUT_QUEUE_SIZE;
        _queue_count--;
    }

    _queue[_queue_tail] = action;
    _queue_tail = (_queue_tail + 1) % INPUT_QUEUE_SIZE;
    _queue_count++;
}

/**
 * _queue_pop — Extraer la siguiente acción de la cola
 */
static input_action_t _queue_pop(void) {
    if (_queue_count == 0) return INPUT_NONE;

    input_action_t action = _queue[_queue_head];
    _queue_head = (_queue_head + 1) % INPUT_QUEUE_SIZE;
    _queue_count--;
    return action;
}


/* =========================================================
 * MAPEO DE EVENTOS A ACCIONES
 *
 * Aquí está la lógica de traducción de eventos hardware
 * (cw_event_t) a acciones semánticas (input_action_t).
 *
 * Este es el único lugar que necesita cambiar si decides
 * usar un hardware diferente o cambiar el mapeo.
 * ========================================================= */

/**
 * _map_event — Convertir evento de click-wheel a acción
 *
 * Mapeo actual:
 *   SCROLL_UP    → NAV_UP    (girar en sentido horario)
 *   SCROLL_DOWN  → NAV_DOWN  (girar en sentido antihorario)
 *   CLICK        → CONFIRM   (presión corta del botón central)
 *   LONG_PRESS   → BACK      (presión larga → volver/cancelar)
 *   DOUBLE_CLICK → HOME      (doble click → pantalla de inicio)
 */
static input_action_t _map_event(cw_event_t event) {
    switch (event) {
        case CW_EVENT_SCROLL_UP:     return INPUT_NAV_UP;
        case CW_EVENT_SCROLL_DOWN:   return INPUT_NAV_DOWN;
        case CW_EVENT_CLICK:         return INPUT_CONFIRM;
        case CW_EVENT_LONG_PRESS:    return INPUT_BACK;
        case CW_EVENT_DOUBLE_CLICK:  return INPUT_HOME;
        default:                     return INPUT_NONE;
    }
}


/* =========================================================
 * API PÚBLICA
 * ========================================================= */

/**
 * input_init — Inicializar el gestor de entrada
 *
 * Inicializa el click-wheel (AS5600 via I2C en PB6/PB7)
 * y limpia la cola de acciones.
 */
bool input_init(void) {
    DEBUG_PRINT("[INPUT] Inicializando gestor de entrada...\r\n");

    /* Limpiar cola */
    _queue_head  = 0;
    _queue_tail  = 0;
    _queue_count = 0;
    _last_btn_state = 0;

    /* Inicializar el click-wheel */
    bool ok = cw_init(&g_cw_state);

    if (ok) {
        _initialized = true;
        DEBUG_PRINT("[INPUT] Click-wheel AS5600 detectado en I2C 0x36\r\n");
        DEBUG_PRINT("[INPUT]   Girar: NAV_UP / NAV_DOWN\r\n");
        DEBUG_PRINT("[INPUT]   Click: CONFIRM\r\n");
        DEBUG_PRINT("[INPUT]   Long press (800ms): BACK\r\n");
        DEBUG_PRINT("[INPUT]   Double click: HOME\r\n");
    } else {
        DEBUG_PRINT("[INPUT] WARN: AS5600 no detectado.\r\n");
        DEBUG_PRINT("[INPUT]   Verificar pull-ups 4.7kOhm en PB6 y PB7.\r\n");
        DEBUG_PRINT("[INPUT]   Verificar alimentacion 3.3V del AS5600.\r\n");
        DEBUG_PRINT("[INPUT]   El sistema continuara sin input hardware.\r\n");
        _initialized = false;
    }

    return ok;
}

/**
 * input_poll — Leer el estado del click-wheel y encolar acciones
 *
 * Llamar cada ~10ms desde el loop principal.
 * Internamente llama a cw_poll() que hace la lectura I2C
 * y la lógica de debounce/detección de eventos.
 */
void input_poll(void) {
    if (!_initialized) return;

    /* Obtener evento del driver de click-wheel */
    cw_event_t event = cw_poll(&g_cw_state);

    if (event == CW_EVENT_NONE) return;

    /* Traducir a acción semántica */
    input_action_t action = _map_event(event);

    if (action != INPUT_NONE) {
        _queue_push(action);

        /* Actualizar estado de botones virtuales para Trezor */
        if (action == INPUT_CONFIRM) {
            _last_btn_state = BTN_YES;
        } else if (action == INPUT_BACK) {
            _last_btn_state = BTN_NO;
        } else {
            _last_btn_state = 0;
        }

        DEBUG_PRINTF("[INPUT] %s → %s\r\n",
            cw_event_name(event),
            input_action_name(action));
    }
}

/**
 * input_get_action — Obtener la siguiente acción de la cola
 */
input_action_t input_get_action(void) {
    input_action_t action = _queue_pop();

    /* Al consumir CONFIRM o BACK, limpiar el estado de botón */
    if (action == INPUT_CONFIRM || action == INPUT_BACK) {
        _last_btn_state = 0;
    }

    return action;
}

/**
 * input_wait — Bloquear hasta recibir una acción o timeout
 *
 * Llama input_poll() repetidamente hasta que haya algo en
 * la cola o se agote el timeout. Usa HAL_Delay(5) entre
 * polls para no saturar el bus I2C.
 */
input_action_t input_wait(uint32_t timeout_ms) {
    uint32_t start = HAL_GetTick();

    while (1) {
        input_poll();

        if (_queue_count > 0) {
            return input_get_action();
        }

        /* Verificar timeout */
        if (timeout_ms > 0) {
            if ((HAL_GetTick() - start) >= timeout_ms) {
                return INPUT_NONE;
            }
        }

        HAL_Delay(5);   /* ~200 polls/segundo → suficiente para respuesta fluida */
    }
}

/**
 * input_flush — Vaciar la cola de acciones
 */
void input_flush(void) {
    _queue_head  = 0;
    _queue_tail  = 0;
    _queue_count = 0;
    _last_btn_state = 0;
}

/**
 * input_has_action — Verificar si hay acción pendiente
 */
bool input_has_action(void) {
    return (_queue_count > 0);
}

/**
 * input_button_read — Compatibilidad con Trezor legacy
 *
 * Reemplaza button_read() de legacy/firmware/buttons.c.
 *
 * El firmware de Trezor hace en su loop:
 *   uint16_t btn = button_read();
 *   if (btn & BTN_YES) { ... }
 *   if (btn & BTN_NO)  { ... }
 *
 * Esta función retorna esos flags según las acciones del click-wheel.
 *
 * IMPORTANTE: también llama input_poll() internamente para que
 * el firmware de Trezor no necesite llamar input_poll() por separado.
 */
uint16_t input_button_read(void) {
    /* Hacer polling antes de retornar el estado */
    input_poll();
    return _last_btn_state;
}

/**
 * input_action_name — Nombre de la acción para debug
 */
const char *input_action_name(input_action_t action) {
    switch (action) {
        case INPUT_NAV_UP:   return "NAV_UP";
        case INPUT_NAV_DOWN: return "NAV_DOWN";
        case INPUT_CONFIRM:  return "CONFIRM";
        case INPUT_BACK:     return "BACK";
        case INPUT_HOME:     return "HOME";
        default:             return "NONE";
    }
}
