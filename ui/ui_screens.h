/**
 * @file    ui_screens.h
 * @brief   Pantallas completas del wallet — WALCORG Fase 4
 *
 * Cada función representa una pantalla completa de la UI.
 * Usan ui_components.h internamente y se integran con
 * input_manager para manejar la navegación con click-wheel.
 *
 * FLUJO DE NAVEGACIÓN:
 *
 *   screen_home()
 *       ├── screen_receive()     → mostrar dirección
 *       ├── screen_send()        → confirmar TX (desde trezorctl)
 *       ├── screen_seed()        → ver palabras semilla
 *       ├── screen_pin_entry()   → ingresar PIN
 *       └── screen_settings()   → configuración
 *           ├── screen_about()
 *           └── screen_usb_mode()
 *
 * @license GNU GPL v3
 */

#ifndef UI_SCREENS_H
#define UI_SCREENS_H

#include <stdint.h>
#include <stdbool.h>

/* Resultado de retorno de las pantallas */
typedef enum {
    SCREEN_RESULT_OK      =  0,   /**< Acción completada con éxito  */
    SCREEN_RESULT_CANCEL  = -1,   /**< Usuario canceló              */
    SCREEN_RESULT_TIMEOUT = -2,   /**< Sin acción por timeout       */
    SCREEN_RESULT_HOME    = -3,   /**< Ir a pantalla de inicio      */
} screen_result_t;

/**
 * @brief Pantalla de inicio — logo y estado del wallet.
 * Bloquea hasta que el usuario activa el click-wheel.
 * @return Opción del menú principal seleccionada (0-N)
 */
int screen_home(void);

/**
 * @brief Pantalla de recepción — muestra dirección Bitcoin.
 * @param address  Dirección a mostrar (NULL = placeholder)
 * @return         SCREEN_RESULT_OK si confirmó, CANCEL si volvió
 */
screen_result_t screen_receive(const char *address);

/**
 * @brief Pantalla de confirmación de envío — detalles de TX.
 * Llamada por fsm.c cuando trezorctl solicita firma.
 * @param amount    Monto a enviar
 * @param fee       Comisión de red
 * @param to_addr   Dirección destino
 * @return          SCREEN_RESULT_OK si confirmó, CANCEL si rechazó
 */
screen_result_t screen_send_confirm(const char *amount,
                                     const char *fee,
                                     const char *to_addr);

/**
 * @brief Pantalla de entrada de PIN.
 * @param prompt   Texto descriptivo (ej: "Ingresa tu PIN")
 * @param pin_out  Buffer donde se guarda el PIN ingresado
 * @param max_len  Tamaño máximo del PIN
 * @return         SCREEN_RESULT_OK con PIN en pin_out, CANCEL si volvió
 */
screen_result_t screen_pin_entry(const char *prompt,
                                  char *pin_out, uint8_t max_len);

/**
 * @brief Pantalla de visualización de semilla BIP39.
 * Requiere confirmación de PIN antes de mostrar.
 * @param words    Array de 12 o 24 palabras
 * @param count    Número de palabras (12 o 24)
 * @return         SCREEN_RESULT_OK cuando el usuario terminó de ver
 */
screen_result_t screen_seed_view(const char *const *words, uint8_t count);

/**
 * @brief Pantalla de configuración.
 * @return  Opción seleccionada o SCREEN_RESULT_CANCEL
 */
screen_result_t screen_settings(void);

/**
 * @brief Pantalla "Acerca de" con información del dispositivo.
 */
void screen_about(void);

/**
 * @brief Pantalla de espera USB — indica que el dispositivo
 * está conectado y listo para trezorctl.
 */
void screen_usb_ready(void);

/**
 * @brief Pantalla de error genérica.
 * @param title    Título del error
 * @param message  Descripción del error
 */
void screen_error(const char *title, const char *message);

/**
 * @brief Pantalla de éxito genérica.
 * @param message  Mensaje de confirmación
 * @param duration_ms Tiempo en pantalla (0 = hasta interacción)
 */
void screen_success(const char *message, uint32_t duration_ms);

#endif /* UI_SCREENS_H */
