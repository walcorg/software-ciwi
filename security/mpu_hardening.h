/**
 * @file    mpu_hardening.h
 * @brief   Configuración completa de la MPU — WALCORG Fase 6
 *
 * Extiende la configuración mínima de la MPU de Fase 1
 * (mpu_config_minimal en stm32_walcorg.c) con protecciones
 * adicionales específicas de producción.
 *
 * REGIONES MPU EN FASE 6 (8 regiones, el máximo del Cortex-M4):
 *
 *   0: Flash bootloader   (0x08000000, 64KB)  — RO, ejecutable
 *   1: Flash firmware     (0x08010000, 576KB) — RO, ejecutable
 *   2: Flash NORCOW       (0x080A0000, 384KB) — RO para código normal,
 *                                                solo storage_hal.c
 *                                                puede escribir (via
 *                                                unlock temporal)
 *   3: SRAM general       (0x20000000, 96KB)  — RWX
 *   4: SRAM stack guard   (al final del stack) — sin acceso (detecta overflow)
 *   5: CCM datos sensibles(0x10000000, 64KB)  — RW, NO ejecutable
 *   6: Periféricos        (0x40000000, 512MB) — RW, NO ejecutable
 *   7: Región nula        (0x00000000, 4KB)   — SIN acceso (detecta NULL deref)
 *
 * STACK GUARD:
 *   Una técnica común para detectar stack overflow es reservar
 *   una región pequeña (32-64 bytes) al final del área de stack
 *   y marcarla como NO accesible. Si el stack crece demasiado
 *   (por ejemplo, por una función con recursión excesiva o un
 *   buffer local muy grande), el primer acceso a esa región
 *   dispara un MemManage Fault INMEDIATAMENTE, en vez de corromper
 *   silenciosamente otras variables o el código de retorno.
 *
 * @license GNU GPL v3
 */

#ifndef MPU_HARDENING_H
#define MPU_HARDENING_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Aplica la configuración completa de la MPU para producción.
 *
 * Reemplaza la configuración mínima de Fase 1 con las 8 regiones
 * descritas arriba, incluyendo stack guard y región nula protegida.
 *
 * DEBE llamarse después de mpu_config_minimal() de Fase 1,
 * la sobreescribe completamente.
 *
 * @return true si la configuración fue aplicada correctamente
 */
bool mpu_hardening_apply(void);

/**
 * @brief Verifica que las 8 regiones MPU están configuradas correctamente.
 *
 * Lee los registros RBAR/RASR de cada región y compara con los
 * valores esperados. Útil para el security audit.
 *
 * @return true si todas las regiones coinciden con lo esperado
 */
bool mpu_hardening_verify(void);

/**
 * @brief Desbloquea temporalmente la región NORCOW para escritura.
 *
 * storage_hal.c debe llamar esto antes de escribir en Flash,
 * y mpu_lock_norcow() inmediatamente después. Limitar la ventana
 * de escritura reduce la superficie de ataque ante bugs que
 * intenten escribir en NORCOW desde código no autorizado.
 */
void mpu_unlock_norcow_write(void);

/**
 * @brief Re-bloquea la región NORCOW contra escritura.
 */
void mpu_lock_norcow_write(void);

/**
 * @brief Imprime el estado de todas las regiones MPU por UART.
 */
void mpu_hardening_print_status(void);

#endif /* MPU_HARDENING_H */
