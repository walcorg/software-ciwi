/**
 * @file    tamper_detect.c
 * @brief   Detección de tamper — WALCORG Fase 6
 *
 * @license GNU GPL v3
 */

#include "tamper_detect.h"
#include "storage_hal.h"
#include "uart_debug.h"
#include "stm32f4xx_hal.h"
#include "clock_config.h"    /* clock_get_sysclk_hz() para detección de glitch */

/* =========================================================
 * REGISTROS DEL CORTEX-M4 PARA DEBUG
 * ========================================================= */

/** DHCSR: Debug Halting Control and Status Register */
#define DHCSR_ADDR          0xE000EDF0UL
#define DHCSR_C_DEBUGEN     (1UL << 0)   /**< Bit 0: debug habilitado */

/* =========================================================
 * ESTADO INTERNO
 * ========================================================= */
static tamper_policy_t _policy = TAMPER_POLICY_LOG;
static uint32_t        _event_count = 0;
static bool            _initialized = false;
static uint32_t        _last_check_ms = 0;

/** Intervalo mínimo entre checks de tamper (evita overhead excesivo) */
#define TAMPER_CHECK_INTERVAL_MS   500


/* =========================================================
 * API PÚBLICA
 * ========================================================= */

void tamper_init(tamper_policy_t policy) {
    _policy        = policy;
    _event_count   = 0;
    _initialized   = true;
    _last_check_ms = HAL_GetTick();

    DEBUG_PRINTF("[TAMPER] Inicializado. Politica: %s\r\n",
        policy == TAMPER_POLICY_LOG  ? "LOG (solo registrar)" :
        policy == TAMPER_POLICY_LOCK ? "LOCK (bloquear)" :
        "WIPE (borrar semilla)");

    /* Verificación inicial */
    if (tamper_is_debugger_attached()) {
        DEBUG_PRINT("[TAMPER] WARN: Debugger detectado durante init.\r\n");
        DEBUG_PRINT("[TAMPER] Normal si estas en desarrollo con GDB.\r\n");
    }
}

bool tamper_is_debugger_attached(void) {
    /* Leer el registro DHCSR directamente.
     * C_DEBUGEN=1 significa que un debugger tiene control del CPU. */
    volatile uint32_t dhcsr = *(volatile uint32_t *)DHCSR_ADDR;
    return (dhcsr & DHCSR_C_DEBUGEN) != 0;
}

tamper_event_t tamper_poll(void) {
    if (!_initialized) return TAMPER_NONE;

    uint32_t now = HAL_GetTick();
    if ((now - _last_check_ms) < TAMPER_CHECK_INTERVAL_MS) {
        return TAMPER_NONE;  /* Throttle de checks */
    }
    _last_check_ms = now;

    /* Check 1: Debugger conectado */
    if (tamper_is_debugger_attached()) {
        tamper_respond(TAMPER_DEBUGGER_ATTACH);
        return TAMPER_DEBUGGER_ATTACH;
    }

    /* Check 2: Verificar clock sigue siendo 168MHz
     * Un glitch de voltaje/clock podría haber desincronizado el PLL */
    uint32_t freq = clock_get_sysclk_hz();
    if (freq < 160000000UL || freq > 176000000UL) {
        /* Desviación >5% del clock esperado — posible glitch attack */
        tamper_respond(TAMPER_CLOCK_GLITCH);
        return TAMPER_CLOCK_GLITCH;
    }

    /* Check 3: Verificar nivel de RDP no fue degradado
     * (un downgrade de RDP borra la Flash automáticamente por
     * hardware, pero verificamos para logging y detección temprana) */
    extern uint8_t flash_protect_get_rdp_level(void);
    uint8_t rdp = flash_protect_get_rdp_level();
    extern bool storage_is_initialized(void);
    if (rdp == 0xAA && storage_is_initialized()) {
        /* RDP en nivel 0 con wallet configurado = configuración insegura
         * (debería estar en RDP1 si hay datos sensibles) */
        DEBUG_PRINT("[TAMPER] WARN: Wallet activo sin RDP. Inseguro.\r\n");
    }

    return TAMPER_NONE;
}

bool tamper_check_safe_to_proceed(void) {
    tamper_event_t event = tamper_poll();
    if (event != TAMPER_NONE) {
        DEBUG_PRINT("[TAMPER] Operacion bloqueada por seguridad.\r\n");
        return false;
    }
    return true;
}

void tamper_respond(tamper_event_t event) {
    _event_count++;

    const char *event_name =
        event == TAMPER_DEBUGGER_ATTACH ? "DEBUGGER_ATTACH" :
        event == TAMPER_CLOCK_GLITCH    ? "CLOCK_GLITCH"    :
        event == TAMPER_VOLTAGE_ANOMALY ? "VOLTAGE_ANOMALY" :
        event == TAMPER_RDP_DOWNGRADE   ? "RDP_DOWNGRADE"   :
        "UNKNOWN";

    DEBUG_PRINTF("\r\n[TAMPER] !!! EVENTO DETECTADO: %s !!!\r\n", event_name);
    DEBUG_PRINTF("[TAMPER] Total de eventos: %lu\r\n", _event_count);

    switch (_policy) {

        case TAMPER_POLICY_LOG:
            /* Solo registrar — usar exclusivamente en desarrollo */
            DEBUG_PRINT("[TAMPER] Politica LOG — continuando sin acción.\r\n");
            break;

        case TAMPER_POLICY_LOCK:
            /* Bloquear el wallet — requiere power-cycle para continuar */
            DEBUG_PRINT("[TAMPER] Politica LOCK — bloqueando dispositivo.\r\n");
            DEBUG_PRINT("[TAMPER] Desconecta y reconecta la alimentacion.\r\n");
            __disable_irq();
            while (1) {
                /* Loop infinito — requiere reset físico para salir */
                __NOP();
            }
            break;  /* No alcanzable */

        case TAMPER_POLICY_WIPE:
            /* Respuesta máxima — borrar la semilla inmediatamente
             * Esta es la respuesta recomendada para producción */
            DEBUG_PRINT("[TAMPER] !!! Politica WIPE — borrando semilla !!!\r\n");
            extern bool storage_wipe(void);
            storage_wipe();
            DEBUG_PRINT("[TAMPER] Semilla borrada. Bloqueando dispositivo.\r\n");
            __disable_irq();
            while (1) { __NOP(); }
            break;  /* No alcanzable */
    }
}

uint32_t tamper_get_event_count(void) {
    return _event_count;
}

void tamper_print_status(void) {
#if (PRODUCTION == 0)
    DEBUG_PRINT("\r\n[TAMPER] === Estado Anti-Tamper ===\r\n");
    DEBUG_PRINTF("[TAMPER] Inicializado    : %s\r\n",
        _initialized ? "SI" : "NO");
    DEBUG_PRINTF("[TAMPER] Politica activa : %s\r\n",
        _policy == TAMPER_POLICY_LOG  ? "LOG" :
        _policy == TAMPER_POLICY_LOCK ? "LOCK" : "WIPE");
    DEBUG_PRINTF("[TAMPER] Debugger activo : %s\r\n",
        tamper_is_debugger_attached() ? "SI (sospechoso!)" : "no");
    DEBUG_PRINTF("[TAMPER] Eventos totales : %lu\r\n", _event_count);
    DEBUG_PRINT("[TAMPER] ============================\r\n\r\n");
#endif
}
