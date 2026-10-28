/**
 * @file    usb_test.h
 * @brief   Test de enumeración USB — WALCORG Fase 1
 * @license GNU GPL v3
 */

#ifndef USB_TEST_H
#define USB_TEST_H

#include "stm32f4xx_hal.h"

/** Handle global del controlador USB — accedido desde stm32f4xx_it.c */
extern PCD_HandleTypeDef hpcd_USB_OTG_FS;

/**
 * @brief Inicializa el USB OTG FS y comienza la enumeración.
 *        Llamar en main() después de SystemClock_Config().
 */
void usb_test_init(void);

/**
 * @brief Polling del estado USB — llamar en el loop principal.
 *        Imprime estadísticas cada 5 segundos por UART.
 */
void usb_test_poll(void);

/**
 * @brief Retorna 1 si el host reconoció el dispositivo, 0 si no.
 */
uint8_t usb_is_connected(void);

#endif /* USB_TEST_H */
