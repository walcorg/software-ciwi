/**
 * @file    protect.c  (legacy/firmware/protect.c — versión MODIFICADA)
 * @brief   Protección de PIN adaptada al click-wheel — WalCorg Fase 3
 *
 * ARCHIVO ORIGINAL: legacy/firmware/protect.c del repo trezor-firmware
 *
 * CONTEXTO:
 *   protect.c maneja la entrada del PIN de seguridad del wallet.
 *   En Trezor One, el usuario ingresa el PIN usando 2 botones:
 *     BTN_YES → avanzar al siguiente dígito (0→1→...→9→0)
 *     BTN_NO  → confirmar dígito actual y pasar al siguiente
 *   La pantalla mostraba una cuadrícula aleatoria de dígitos
 *   para evitar ataques de análisis de desgaste.
 *
 * MODIFICACIÓN PARA CLICK-WHEEL:
 *   [MOD-P1] Entrada de PIN: girar wheel = cambiar dígito,
 *            click = confirmar dígito, long press = borrar último
 *
 *   [MOD-P2] La pantalla de PIN usa layoutEnterPIN() del layout.c
 *            modificado, que muestra asteriscos y las instrucciones
 *            del click-wheel.
 *
 *   [MOD-P3] La cuadrícula aleatoria del Trezor One se reemplaza
 *            por entrada directa de dígitos con el wheel.
 *            IMPLICACIÓN DE SEGURIDAD: la entrada directa es más
 *            susceptible a análisis de patrones de uso físico del
 *            wheel. En Fase 6 (security hardening) se puede
 *            reimplementar la cuadrícula para mayor seguridad.
 *
 * FUNCIONES MODIFICADAS:
 *   protectPIN()    → [MOD-P1][MOD-P2] entrada con click-wheel
 *   protectButton() → sin cambios (usa button_read() que ya fue adaptado)
 *
 * FUNCIONES SIN CAMBIOS (mantienen la lógica de seguridad de Trezor):
 *   protectAbortedByInitialize()
 *   protectAbortedByCancel()
 *   session_cache[]  y su lógica
 *
 * @license GNU GPL v3
 */

#include "protect.h"        /* Header original de Trezor */
#include "input_manager.h"
#include "display.h"        /* Nuestro display.h modificado */
#include "layout.h"         /* Nuestro layout.c modificado */
#include "uart_debug.h"
#include "stm32f4xx_hal.h"
#include <string.h>
#include <stdio.h>

/* =========================================================
 * CONSTANTES DE PIN
 * ========================================================= */

/** Longitud máxima del PIN en Trezor (9 dígitos) */
#define PIN_MAX_LEN     9

/** Dígitos válidos del PIN */
#define PIN_DIGITS      10   /* 0-9 */

/** Tiempo de espera máximo para entrada de PIN (ms)
 *  Trezor tiene un timeout de inactividad para el PIN */
#define PIN_TIMEOUT_MS  60000   /* 60 segundos */


/* =========================================================
 * FUNCIÓN DE ENTRADA DE PIN
 *
 * [MOD-P1] Reimplementación de la entrada de PIN para click-wheel
 *
 * FLUJO DE ENTRADA:
 *   1. Mostrar dígito actual en pantalla (0-9)
 *   2. SCROLL_UP / SCROLL_DOWN → cambiar dígito (0↔9)
 *   3. CLICK → confirmar dígito y pasar al siguiente
 *   4. LONG_PRESS → borrar último dígito (backspace)
 *   5. DOUBLE_CLICK → cancelar entrada de PIN completamente
 *   6. Cuando PIN tiene al menos 1 dígito: mostrar botón "OK"
 *      Long press cuando PIN completado → aceptar PIN
 * ========================================================= */

/**
 * _pin_draw_screen — Dibujar la pantalla de entrada de PIN
 *
 * [MOD-P2] Usa layoutEnterPIN() del layout.c modificado
 * que muestra los asteriscos y las instrucciones del click-wheel.
 *
 * @param current_digit  Dígito actualmente seleccionado (0-9)
 * @param pin_so_far     PIN ingresado hasta ahora (parcial)
 * @param pin_len        Longitud del PIN ingresado
 * @param is_confirm     true si es la pantalla de confirmación
 */
static void _pin_draw_screen(uint8_t current_digit,
                               const char *pin_so_far, uint8_t pin_len,
                               bool is_confirm) {
    /* Construir string de asteriscos */
    char pin_mask[PIN_MAX_LEN * 2 + 1] = {0};
    for (uint8_t i = 0; i < pin_len && i < PIN_MAX_LEN; i++) {
        pin_mask[i * 2]     = '*';
        pin_mask[i * 2 + 1] = ' ';
    }

    /* Descripción según el contexto */
    const char *desc = is_confirm ?
        "Confirma tu nuevo PIN:" :
        "Ingresa tu PIN:";

    /* Usar layoutEnterPIN del layout.c modificado */
    layoutEnterPIN(desc, pin_mask);

    /* Mostrar el dígito actual que se va a añadir */
    oledSetFgColor(WALCORG_WARNING);
    char digit_str[8];
    snprintf(digit_str, sizeof(digit_str), "[ %d ]", current_digit);
    oledDrawStringCenter(120, digit_str, 3);  /* Grande y centrado */
    oledResetColors();

    /* Mostrar longitud del PIN */
    char len_str[24];
    snprintf(len_str, sizeof(len_str), "%d digitos ingresados", pin_len);
    oledSetFgColor(WALCORG_SUBTEXT);
    oledDrawStringCenter(175, len_str, 1);
    oledResetColors();

    (void)pin_so_far;  /* Nunca mostrar el PIN real */
}

/**
 * protectPIN — Solicitar y verificar el PIN al usuario
 *
 * [MOD-P1][MOD-P2] Reimplementación para click-wheel.
 *
 * Esta función reemplaza a la original de Trezor que usaba
 * una cuadrícula de dígitos controlada con 2 botones.
 *
 * @param use_cached  true = usar la sesión cacheada si existe
 *                    false = siempre pedir el PIN
 * @return            true  si el PIN es correcto
 *                    false si se canceló o el PIN es incorrecto
 */
bool protectPIN(bool use_cached) {
    /* ---- Verificar caché de sesión (lógica original de Trezor) ---- */
    if (use_cached) {
        /* La lógica de caché de Trezor verifica si la sesión
         * sigue siendo válida. Si lo es, no pide el PIN.
         * Mantenemos esta lógica sin cambios. */
        extern bool session_is_valid(void);  /* Definido en session.c de Trezor */
        if (session_is_valid()) {
            DEBUG_PRINT("[PROTECT] PIN cacheado — sin pedir\r\n");
            return true;
        }
    }

    /* ---- Limpiar acciones previas ---- */
    input_flush();

    /* ---- Variables de estado ---- */
    char pin_entered[PIN_MAX_LEN + 1] = {0};  /* PIN siendo ingresado */
    uint8_t pin_len      = 0;
    uint8_t current_digit = 0;   /* Dígito actualmente mostrado (0-9) */
    bool cancelled        = false;

    DEBUG_PRINT("[PROTECT] Solicitando PIN al usuario...\r\n");

    /* ---- Loop principal de entrada de PIN ---- */
    while (!cancelled && pin_len < PIN_MAX_LEN) {

        /* Dibujar pantalla de PIN */
        _pin_draw_screen(current_digit, pin_entered, pin_len, false);

        /* Esperar acción con timeout */
        input_action_t action = input_wait(PIN_TIMEOUT_MS);

        switch (action) {

            case INPUT_NAV_UP:
                /* Girar hacia arriba → incrementar dígito (wrap 9→0) */
                current_digit = (current_digit + 1) % PIN_DIGITS;
                break;

            case INPUT_NAV_DOWN:
                /* Girar hacia abajo → decrementar dígito (wrap 0→9) */
                current_digit = (current_digit == 0) ?
                                PIN_DIGITS - 1 :
                                current_digit - 1;
                break;

            case INPUT_CONFIRM:
                /* Click → añadir el dígito actual al PIN */
                if (pin_len < PIN_MAX_LEN) {
                    pin_entered[pin_len] = '0' + current_digit;
                    pin_len++;
                    pin_entered[pin_len] = '\0';
                    DEBUG_PRINTF("[PROTECT] Dígito añadido, longitud: %d\r\n",
                        pin_len);

                    /* Si el PIN tiene al menos 4 dígitos, el siguiente
                     * CONFIRM completa el PIN en vez de añadir un dígito.
                     * Esto se maneja con el estado de la siguiente iteración. */
                }

                /* Si ya alcanzó la longitud mínima (4), long press = aceptar */
                if (pin_len >= 4) {
                    /* Preguntar al usuario si quiere confirmar o seguir */
                    /* En la siguiente iteración LONG_PRESS = aceptar */
                    DEBUG_PRINT("[PROTECT] PIN tiene 4+ dígitos. "
                                "Long press para aceptar.\r\n");
                }
                break;

            case INPUT_BACK:
                if (pin_len > 0) {
                    /* Long press → borrar último dígito (backspace) */
                    pin_len--;
                    pin_entered[pin_len] = '\0';
                    DEBUG_PRINT("[PROTECT] Último dígito borrado\r\n");

                    /* Si el PIN ya tenía mínimo y ahora tiene 4+,
                     * long press cuando está completo = aceptar PIN */
                    if (pin_len >= 4) {
                        /* Segundo long press = aceptar el PIN */
                        /* (Se maneja en la siguiente iteración) */
                        goto pin_accept;
                    }
                } else {
                    /* No hay dígitos → long press = cancelar */
                    DEBUG_PRINT("[PROTECT] PIN cancelado\r\n");
                    cancelled = true;
                }
                break;

            case INPUT_HOME:
            case INPUT_NONE:
                /* Timeout o home → cancelar */
                DEBUG_PRINT("[PROTECT] PIN cancelado (timeout o HOME)\r\n");
                cancelled = true;
                break;

            default:
                break;
        }
    }

    /* PIN completo (máxima longitud alcanzada) */
    if (pin_len == PIN_MAX_LEN) {
        goto pin_accept;
    }

    if (cancelled) {
        /* Limpiar datos sensibles de la memoria */
        memset(pin_entered, 0, sizeof(pin_entered));
        return false;
    }

pin_accept:
    DEBUG_PRINTF("[PROTECT] PIN ingresado (%d dígitos). Verificando...\r\n",
        pin_len);

    /* ---- Verificar el PIN contra el almacenado ---- */
    /* La verificación real usa la función de Trezor storage_containsPin()
     * que compara con el hash almacenado en Flash.
     * NO comparamos el PIN en texto plano. */
    extern bool storage_containsPin(const char *pin);
    bool pin_ok = storage_containsPin(pin_entered);

    /* Limpiar datos sensibles INMEDIATAMENTE después de usarlos */
    memset(pin_entered, 0, sizeof(pin_entered));

    if (pin_ok) {
        DEBUG_PRINT("[PROTECT] PIN correcto\r\n");
        /* Actualizar el caché de sesión */
        extern void session_clear(bool lock);
        /* No limpiar la sesión — el PIN fue correcto */
    } else {
        DEBUG_PRINT("[PROTECT] PIN incorrecto\r\n");
        /* Mostrar error brevemente */
        oledClear();
        oledSetFgColor(WALCORG_ERROR);
        oledDrawStringCenter(OLED_HEIGHT / 2 - 20, "PIN incorrecto", 2);
        oledDrawStringCenter(OLED_HEIGHT / 2 + 10,
                             "Intentalo de nuevo", 1);
        oledResetColors();
        HAL_Delay(2000);
    }

    return pin_ok;
}

/**
 * protectButton — Pedir confirmación de botón al usuario
 *
 * Esta función muestra un diálogo y espera YES o NO.
 * [MOD-B2] No se modifica — button_read() ya fue adaptado
 * en buttons.c para usar el click-wheel.
 *
 * @param desc  Descripción de lo que se está confirmando
 * @return      true si confirmó (YES), false si canceló (NO)
 */
bool protectButton(const char *desc) {
    input_flush();  /* Limpiar acciones previas */

    /* Mostrar diálogo de confirmación */
    oledClear();
    layoutDialogSwipe(NULL, "Cancelar", "Confirmar",
                      desc, NULL, NULL, NULL, NULL, NULL);

    DEBUG_PRINTF("[PROTECT] Esperando confirmacion: %s\r\n",
        desc ? desc : "");

    /* Esperar YES o NO */
    input_action_t action = input_wait(0);  /* Sin timeout */

    bool confirmed = (action == INPUT_CONFIRM);
    DEBUG_PRINTF("[PROTECT] Confirmacion: %s\r\n",
        confirmed ? "SI" : "NO");

    return confirmed;
}
