/**
 * @file    tamper_detect.h
 * @brief   Detección de manipulación física y debug — WALCORG Fase 6
 *
 * Detecta intentos de:
 *   1. Conectar un debugger (SWD/JTAG) mientras el wallet opera
 *   2. Lectura no autorizada de memoria via fault injection
 *   3. Modificación del reloj del sistema (glitching)
 *
 * MECANISMO DE DETECCIÓN DE DEBUGGER:
 *   El registro DHCSR (Debug Halting Control and Status Register)
 *   del Cortex-M4 tiene un bit C_DEBUGEN que se activa cuando
 *   un debugger está conectado y controlando el CPU.
 *
 *   DHCSR está en 0xE000EDF0 (siempre accesible, parte del core).
 *   Bit 0 (C_DEBUGEN): 1 = debug habilitado, 0 = debug deshabilitado
 *
 * RESPUESTA ANTE TAMPER DETECTADO:
 *   Política configurable:
 *     TAMPER_POLICY_LOG    → solo registrar el evento (desarrollo)
 *     TAMPER_POLICY_LOCK   → bloquear el wallet hasta reset
 *     TAMPER_POLICY_WIPE   → borrar la semilla inmediatamente (máxima seguridad)
 *
 * @license GNU GPL v3
 */

#ifndef TAMPER_DETECT_H
#define TAMPER_DETECT_H

#include <stdint.h>
#include <stdbool.h>

/* =========================================================
 * POLÍTICAS DE RESPUESTA
 * ========================================================= */
typedef enum {
    TAMPER_POLICY_LOG  = 0,  /**< Solo registrar — usar en desarrollo  */
    TAMPER_POLICY_LOCK,      /**< Bloquear hasta power-cycle           */
    TAMPER_POLICY_WIPE,      /**< Borrar semilla inmediatamente        */
} tamper_policy_t;

/* =========================================================
 * TIPOS DE EVENTO DE TAMPER
 * ========================================================= */
typedef enum {
    TAMPER_NONE             = 0,
    TAMPER_DEBUGGER_ATTACH,   /**< Debugger SWD/JTAG conectado          */
    TAMPER_CLOCK_GLITCH,      /**< Anomalía en el clock del sistema     */
    TAMPER_VOLTAGE_ANOMALY,   /**< Voltaje fuera de rango (glitching)   */
    TAMPER_RDP_DOWNGRADE,     /**< Intento de bajar el nivel de RDP     */
} tamper_event_t;

/* =========================================================
 * API PÚBLICA
 * ========================================================= */

/**
 * @brief Inicializa el sistema de detección de tamper.
 *
 * Configura la política de respuesta y prepara los registros
 * de monitoreo (DHCSR, DWT).
 *
 * @param policy  Política de respuesta ante tamper detectado
 */
void tamper_init(tamper_policy_t policy);

/**
 * @brief Verifica si hay un debugger conectado en este momento.
 *
 * Lee el bit C_DEBUGEN del registro DHCSR del Cortex-M4.
 *
 * @return true si un debugger está activo
 */
bool tamper_is_debugger_attached(void);

/**
 * @brief Polling de tamper — llamar periódicamente en el loop principal.
 *
 * Verifica todos los vectores de tamper configurados y ejecuta
 * la política de respuesta si detecta alguno.
 *
 * Llamar idealmente cada vez que se accede a datos sensibles
 * (antes de descifrar la semilla, antes de firmar, etc.)
 *
 * @return  El tipo de evento detectado, o TAMPER_NONE si todo normal
 */
tamper_event_t tamper_poll(void);

/**
 * @brief Verifica tamper inmediatamente antes de una operación crítica.
 *
 * Wrapper conveniente para usar antes de operaciones con la semilla:
 *   if (!tamper_check_safe_to_proceed()) { return ERROR; }
 *   // ... operación con clave privada ...
 *
 * @return true si es seguro proceder, false si se detectó tamper
 *         (y ya se ejecutó la política de respuesta)
 */
bool tamper_check_safe_to_proceed(void);

/**
 * @brief Ejecuta manualmente la política de respuesta a tamper.
 *
 * Normalmente se llama internamente desde tamper_poll(), pero
 * puede invocarse manualmente para pruebas o eventos externos.
 *
 * @param event  Tipo de evento detectado
 */
void tamper_respond(tamper_event_t event);

/**
 * @brief Retorna el número de eventos de tamper detectados desde el boot.
 */
uint32_t tamper_get_event_count(void);

/**
 * @brief Imprime el estado del sistema anti-tamper por UART.
 */
void tamper_print_status(void);

#endif /* TAMPER_DETECT_H */
