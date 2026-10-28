/**
 * @file    pin_delay.h
 * @brief   Rate-limiting de intentos de PIN — WALCORG Fase 6
 *
 * Implementa el mismo mecanismo de delay exponencial que usa
 * el Trezor real: cada intento fallido de PIN incrementa el
 * tiempo de espera obligatorio antes del siguiente intento.
 *
 * TABLA DE DELAYS (igual que Trezor One):
 *   Intento 1-2:   sin delay
 *   Intento 3:     1 segundo
 *   Intento 4:     4 segundos
 *   Intento 5:     8 segundos
 *   Intento 6:     16 segundos
 *   Intento 7:     32 segundos
 *   Intento 8:     64 segundos (~1 min)
 *   Intento 9:     128 segundos (~2 min)
 *   Intento 10:    256 segundos (~4 min) → luego WIPE automático
 *
 * Fórmula: delay = 2^(intento-2) segundos, para intento >= 3
 *
 * PROPÓSITO:
 *   Sin este delay, un atacante con acceso físico al dispositivo
 *   podría probar miles de PINs por segundo. Con el delay
 *   exponencial, los primeros 10 intentos toman acumulativamente
 *   más de 8 minutos, y al onceavo intento el wallet se borra.
 *
 *   Combinado con storage_hal.c (10 intentos máximos → wipe),
 *   esto hace que un PIN de 4 dígitos (10,000 combinaciones)
 *   sea efectivamente irrompible por fuerza bruta física.
 *
 * @license GNU GPL v3
 */

#ifndef PIN_DELAY_H
#define PIN_DELAY_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Calcula el delay requerido para el intento actual.
 *
 * @param attempt_number  Número de intento (1-based)
 * @return                Delay en milisegundos antes de permitir
 *                        el siguiente intento
 */
uint32_t pin_delay_calculate_ms(uint8_t attempt_number);

/**
 * @brief Aplica el delay bloqueante antes de un intento de PIN.
 *
 * Llamar ANTES de mostrar la pantalla de entrada de PIN cuando
 * hay intentos fallidos previos. Muestra una cuenta regresiva
 * en pantalla durante la espera.
 *
 * @param attempt_number  Número de intento actual
 */
void pin_delay_enforce(uint8_t attempt_number);

/**
 * @brief Retorna el delay total acumulado si se agotan todos los intentos.
 * Útil para mostrar al usuario cuánto tiempo le tomaría a un
 * atacante agotar los 10 intentos.
 * @return Tiempo total en segundos
 */
uint32_t pin_delay_total_lockout_seconds(void);

#endif /* PIN_DELAY_H */
