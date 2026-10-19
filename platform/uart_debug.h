/**
 * @file    uart_debug.h
 * @brief   Debug por UART1 (PA9 TX) — WalCorg Fase 1
 *
 * Redirige printf() al puerto UART1 del STM32F405 para que
 * puedas ver mensajes de debug en cualquier monitor serial
 * (picocom, minicom, PuTTY, el monitor serial de VS Code).
 *
 * CONEXIÓN FÍSICA:
 *   WeAct board PA9  → adaptador USB-UART TX
 *   WeAct board GND  → adaptador USB-UART GND
 *   (Solo necesitas TX + GND para ver mensajes de debug)
 *
 *   Configuración del monitor serial:
 *     Baud rate : 115200
 *     Data bits : 8
 *     Stop bits : 1
 *     Paridad   : ninguna
 *     Flow ctrl : ninguno
 *
 * USO EN CÓDIGO:
 *   DEBUG_PRINT("Arrancando...\r\n");
 *   DEBUG_PRINTF("SYSCLK = %lu MHz\r\n", freq / 1000000);
 *   DEBUG_PRINTF("Error en linea %d\r\n", __LINE__);
 *
 * PRODUCCIÓN:
 *   Con PRODUCTION=1 todas las macros se compilan a código vacío.
 *   No ocupan ni Flash ni ciclos de CPU en el firmware final.
 *
 * IMPORTANTE DE SEGURIDAD:
 *   NUNCA imprimir claves privadas, semillas, o PINs por UART,
 *   incluso en desarrollo. Los mensajes de debug son visibles
 *   para cualquiera con acceso físico a los pines.
 *
 * @license GNU GPL v3
 */

#ifndef UART_DEBUG_H
#define UART_DEBUG_H

#include "stm32f4xx_hal.h"
#include <stdarg.h>     /* Para va_list en debug_printf */

/* =========================================================
 * CONFIGURACIÓN
 * ========================================================= */

/** Baud rate de la UART de debug */
#define DEBUG_UART_BAUDRATE     115200

/** Pin TX de debug: PA9 = USART1_TX (Alternate Function 7) */
#define DEBUG_UART_TX_PORT      GPIOA
#define DEBUG_UART_TX_PIN       GPIO_PIN_9
#define DEBUG_UART_TX_AF        GPIO_AF7_USART1

/** Tamaño del buffer para debug_printf */
#define DEBUG_PRINTF_BUF_SIZE   256


/* =========================================================
 * MACROS DE DEBUG
 *
 * Se activan solo cuando PRODUCTION=0.
 * En PRODUCTION=1 se reemplazan por nada ((void)0).
 *
 * El do { } while(0) es un truco de C para que las macros
 * funcionen correctamente dentro de if/else sin llaves:
 *   if (error) DEBUG_PRINT("error"); ← funciona bien
 * ========================================================= */
#if (PRODUCTION == 0)

    /** Imprimir string literal por UART */
    #define DEBUG_PRINT(str) \
        do { debug_print(str); } while(0)

    /** Imprimir con formato (como printf) por UART */
    #define DEBUG_PRINTF(fmt, ...) \
        do { debug_printf(fmt, ##__VA_ARGS__); } while(0)

    /** Imprimir un número en hexadecimal con etiqueta */
    #define DEBUG_HEX(label, val) \
        do { debug_printf(label ": 0x%08lX\r\n", (uint32_t)(val)); } while(0)

    /** Imprimir contenido de un buffer en hex (para datos binarios) */
    #define DEBUG_HEXDUMP(label, buf, len) \
        do { debug_hexdump(label, (uint8_t*)(buf), (len)); } while(0)

    /** Marcar que la ejecución llegó a esta línea (para tracing) */
    #define DEBUG_HERE() \
        do { debug_printf(">> %s:%d\r\n", __FILE__, __LINE__); } while(0)

#else
    /* En PRODUCTION=1: todas las macros son no-ops */
    #define DEBUG_PRINT(str)              ((void)0)
    #define DEBUG_PRINTF(fmt, ...)        ((void)0)
    #define DEBUG_HEX(label, val)         ((void)0)
    #define DEBUG_HEXDUMP(label, buf, len)((void)0)
    #define DEBUG_HERE()                  ((void)0)
#endif


/* =========================================================
 * API PÚBLICA
 * No llamar directamente — usar las macros de arriba.
 * ========================================================= */

/**
 * @brief Inicializa UART1 a 115200 baud en PA9 (TX only).
 *        Llamar una vez en main() después de SystemClock_Config().
 */
void debug_uart_init(void);

/**
 * @brief Transmite un string terminado en '\0' por UART.
 * @param str String a transmitir
 */
void debug_print(const char *str);

/**
 * @brief Printf por UART. Formatea e imprime.
 * @param fmt Formato estilo printf
 */
void debug_printf(const char *fmt, ...);

/**
 * @brief Imprime un buffer en formato hex dump.
 *        Formato: "label[0..7]: AA BB CC DD EE FF 00 11"
 * @param label  Etiqueta descriptiva
 * @param buf    Buffer a imprimir
 * @param len    Longitud del buffer en bytes
 */
void debug_hexdump(const char *label, const uint8_t *buf, uint32_t len);

/** Handle global de UART — necesario para stm32f4xx_it.c */
extern UART_HandleTypeDef huart1;

#endif /* UART_DEBUG_H */
