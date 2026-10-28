/**
 * @file    buttons.c  (legacy/firmware/buttons.c — versión MODIFICADA)
 * @brief   Adaptación de botones físicos a click-wheel — WALCORG Fase 3
 *
 * ARCHIVO ORIGINAL: legacy/firmware/buttons.c del repo trezor-firmware
 *
 * CONTEXTO:
 *   El Trezor One tiene 2 botones físicos conectados a GPIOs:
 *     BTN_LEFT  → PA5  (NO / cancelar)
 *     BTN_RIGHT → PA8  (YES / confirmar)
 *
 *   El firmware de Trezor llama button_read() en su loop principal
 *   y verifica los flags BTN_YES y BTN_NO para saber qué botón
 *   presionó el usuario.
 *
 * MODIFICACIÓN:
 *   [MOD-B1] Eliminamos la lectura de GPIO de los botones físicos
 *   [MOD-B2] button_read() ahora llama input_button_read() del
 *            input_manager, que traduce los eventos del click-wheel
 *            a los flags BTN_YES / BTN_NO
 *   [MOD-B3] La inicialización ya no configura GPIO de botones —
 *            el click-wheel ya fue inicializado por input_init()
 *
 * COMPATIBILIDAD:
 *   La firma de button_read() es idéntica al original.
 *   fsm.c, protect.c y el resto del firmware no necesitan cambiar.
 *
 * MAPEO:
 *   CW_EVENT_CLICK      → BTN_YES  (confirmar)
 *   CW_EVENT_LONG_PRESS → BTN_NO   (cancelar)
 *   CW_EVENT_SCROLL_*   → (ninguno — manejado por menu.c)
 *
 * @license GNU GPL v3
 */

#include "buttons.h"        /* Header original de Trezor */
#include "input_manager.h"  /* Nuestro gestor de entrada */
#include "uart_debug.h"

/* =========================================================
 * [MOD-B1] ELIMINADO: configuración GPIO de botones físicos
 *
 * Original:
 *   void buttonUpdate(void) {
 *     uint16_t state = GPIO_ReadInputData(BTN_PORT);
 *     btn_yes = (state & BTN_PIN_YES) == 0;  // activo LOW
 *     btn_no  = (state & BTN_PIN_NO)  == 0;
 *   }
 *
 * Nuevo: todo el input viene del click-wheel via input_manager
 * ========================================================= */

/**
 * buttonInit — Inicializar el sistema de botones
 *
 * [MOD-B3] Original: configuraba GPIO PA5 y PA8 como entradas
 * con pull-up para los botones físicos del Trezor One.
 *
 * Nuevo: el click-wheel ya fue inicializado por input_init()
 * en stm32_walcorg.c. Esta función solo verifica que está listo.
 *
 * Se mantiene la función por compatibilidad — fsm.c la llama
 * durante el arranque del firmware.
 */
void buttonInit(void) {
    DEBUG_PRINT("[BTN] buttonInit() — usando click-wheel via input_manager\r\n");
    /* input_init() ya fue llamado en stm32_walcorg.c
     * No hay GPIO de botones físicos que configurar. */
}

/**
 * button_read — Leer el estado de los "botones" del dispositivo
 *
 * [MOD-B2] Esta es la función más llamada del sistema de botones.
 * El loop principal de Trezor en fsm.c la llama cada iteración:
 *
 *   for (;;) {
 *     usbPoll();
 *     uint16_t btn = button_read();
 *     if (btn & BTN_YES) handleYes();
 *     if (btn & BTN_NO)  handleNo();
 *   }
 *
 * Original: leía el estado de GPIO directamente
 * Nuevo: delega a input_button_read() que:
 *   1. Llama input_poll() para obtener eventos del click-wheel
 *   2. Traduce CONFIRM → BTN_YES, BACK → BTN_NO
 *   3. Retorna el bitmask de flags
 *
 * La razón de hacer el polling AQUÍ (en button_read) es que
 * el loop de Trezor llama button_read() frecuentemente, así
 * que el polling del click-wheel ocurre automáticamente.
 */
uint16_t button_read(void) {
    /* [MOD-B2] Delegar al input_manager en vez de leer GPIO */
    return input_button_read();
}

/**
 * buttonUpdate — Actualizar el estado de los botones
 *
 * [MOD-B2] Original: actualizaba variables internas btn_yes/btn_no
 * Nuevo: NO-OP — input_button_read() hace el polling internamente.
 *
 * Se mantiene por compatibilidad con código que pueda llamarla.
 */
void buttonUpdate(void) {
    /* NO-OP — input_poll() se llama dentro de input_button_read() */
}

/**
 * buttonWaitForYesNo — Esperar hasta que el usuario presione YES o NO
 *
 * [MOD-B2] Esta función bloquea hasta recibir BTN_YES o BTN_NO.
 * Trezor la usa en pantallas de confirmación críticas.
 *
 * Original: polling de GPIO en loop tight
 * Nuevo: input_wait() con HAL_Delay para no saturar el I2C
 *
 * @return BTN_YES si confirmó, BTN_NO si canceló
 */
uint16_t buttonWaitForYesNo(void) {
    /* Limpiar acciones previas para evitar confirmaciones accidentales */
    input_flush();

    DEBUG_PRINT("[BTN] Esperando YES o NO del click-wheel...\r\n");

    while (1) {
        input_action_t action = input_wait(0);  /* Sin timeout */

        if (action == INPUT_CONFIRM) {
            DEBUG_PRINT("[BTN] YES recibido\r\n");
            return BTN_YES;
        }
        if (action == INPUT_BACK || action == INPUT_HOME) {
            DEBUG_PRINT("[BTN] NO recibido\r\n");
            return BTN_NO;
        }
        /* NAV_UP / NAV_DOWN se ignoran aquí — no son YES/NO */
    }
}
