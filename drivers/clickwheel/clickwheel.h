/**
 * @file    clickwheel.h
 * @brief   Driver para click-wheel via I2C — WALCORG Cold Wallet
 *
 * Soporta dos ICs comunes de encoder rotativo via I2C:
 *   - AS5600  (encoder magnético, dirección 0x36) — RECOMENDADO
 *   - TM1637  (encoder con display integrado, dirección 0x40) — alternativa
 *
 * Hardware (WeAct STM32F405RGT6 Mini Core):
 *   PB6  → I2C1_SCL  (con pull-up 4.7kΩ a 3.3V)
 *   PB7  → I2C1_SDA  (con pull-up 4.7kΩ a 3.3V)
 *   PB8  → CLICK_BTN (GPIO input, pull-up interno activo)
 *
 * @author  WALCORG Project
 * @license GNU GPL v3
 */

#ifndef CLICKWHEEL_H
#define CLICKWHEEL_H

#include <stdint.h>
#include <stdbool.h>

/* =========================================================
 * CONFIGURACIÓN DE HARDWARE — ajusta según tu cableado
 * ========================================================= */

/** Puerto y pin del botón de click central */
#define CW_BTN_PORT         GPIOB
#define CW_BTN_PIN          GPIO_PIN_8

/** Dirección I2C del AS5600 (fija en hardware, no configurable) */
#define AS5600_I2C_ADDR     (0x36 << 1)   /* HAL usa dirección × 2 */

/** Timeout I2C en ms */
#define CW_I2C_TIMEOUT      10

/** Umbral de movimiento mínimo para detectar giro (0–4095 = 360°) */
#define CW_DEADZONE         20

/** Cuántos ticks de encoder = 1 paso de navegación */
#define CW_TICKS_PER_STEP   40

/** Tiempo mínimo entre eventos de scroll (ms) — debounce */
#define CW_SCROLL_DEBOUNCE_MS   50

/** Tiempo para considerar un "long press" del botón (ms) */
#define CW_LONGPRESS_MS     800

/** Tiempo de debounce del botón (ms) */
#define CW_BTN_DEBOUNCE_MS  30

/* =========================================================
 * TIPOS PÚBLICOS
 * ========================================================= */

/**
 * Eventos que puede generar la click-wheel.
 * El resto del firmware solo consume estos eventos —
 * no necesita saber nada del I2C ni del AS5600.
 */
typedef enum {
    CW_EVENT_NONE       = 0,
    CW_EVENT_SCROLL_UP,       /**< Giro en sentido horario      */
    CW_EVENT_SCROLL_DOWN,     /**< Giro en sentido antihorario  */
    CW_EVENT_CLICK,           /**< Presión corta del botón      */
    CW_EVENT_LONG_PRESS,      /**< Presión larga (>800ms)       */
    CW_EVENT_DOUBLE_CLICK,    /**< Doble click rápido           */
} cw_event_t;

/**
 * Estado interno del driver (opaco para el usuario).
 * Inicializado por cw_init(), actualizado por cw_poll().
 */
typedef struct {
    uint16_t    last_angle;       /**< Último ángulo leído del AS5600   */
    int32_t     accumulated;      /**< Acumulador de ticks de encoder    */
    uint32_t    last_scroll_ms;   /**< Timestamp último evento scroll    */
    uint32_t    btn_press_ms;     /**< Timestamp cuando se presionó btn  */
    uint32_t    last_click_ms;    /**< Timestamp del click anterior      */
    bool        btn_was_pressed;  /**< Estado anterior del botón         */
    bool        long_press_fired; /**< Ya se disparó long press          */
    uint8_t     click_count;      /**< Contador para double-click        */
    bool        initialized;      /**< Driver inicializado correctamente */
} cw_state_t;

/* =========================================================
 * API PÚBLICA
 * ========================================================= */

/**
 * @brief Inicializa el driver: configura I2C1 y GPIO del botón.
 *
 * Debe llamarse una vez en el startup, después de SystemClock_Config().
 *
 * @param  state   Puntero al estado del driver (puede ser estático)
 * @return true    Si el AS5600 respondió correctamente
 * @return false   Si no hay respuesta en el bus I2C (revisar cableado)
 */
bool cw_init(cw_state_t *state);

/**
 * @brief Polling del estado — llamar cada ~10ms desde el loop principal.
 *
 * Lee el ángulo del AS5600, compara con la lectura anterior,
 * verifica el botón, y retorna el evento detectado (si hay alguno).
 *
 * @param  state   Puntero al estado inicializado por cw_init()
 * @return         Evento detectado, o CW_EVENT_NONE si no hay nada
 */
cw_event_t cw_poll(cw_state_t *state);

/**
 * @brief Verifica si el AS5600 está presente y responde en el bus.
 * @return true si el IC responde, false si hay error de conexión
 */
bool cw_is_connected(void);

/**
 * @brief Lee el ángulo crudo del AS5600 (0–4095 = 0°–360°).
 * @param  angle   Puntero donde se guarda el valor
 * @return true    Si la lectura fue exitosa
 */
bool cw_read_raw_angle(uint16_t *angle);

/**
 * @brief Retorna descripción del evento como string (para debug).
 */
const char *cw_event_name(cw_event_t event);

#endif /* CLICKWHEEL_H */
