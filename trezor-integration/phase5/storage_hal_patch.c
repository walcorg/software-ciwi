/**
 * @file    storage_hal_patch.c
 * @brief   PARCHE PARA storage_hal.c — WALCORG Fase 5
 *
 * Este archivo documenta los cambios [MOD-S3] que deben aplicarse
 * a storage_hal.c. El cambio es mínimo: añadir 3 líneas que integran
 * mpu_unlock_norcow_write() y mpu_lock_norcow_write() como weak externals.
 *
 * INSTRUCCIONES DE APLICACIÓN:
 * ============================================================
 *
 * 1. Añadir después de #include "stm32f4xx_hal.h":
 *
 *    // Fase 6: integración con MPU hardening
 *    // Symbols son weak — no-op si mpu_hardening.c no está enlazado
 *    extern void __attribute__((weak)) mpu_unlock_norcow_write(void);
 *    extern void __attribute__((weak)) mpu_lock_norcow_write(void);
 *
 * 2. En _flash_unlock(), ANTES de HAL_FLASH_Unlock():
 *
 *    // [MOD-S3] Abrir ventana de escritura MPU
 *    if (mpu_unlock_norcow_write) mpu_unlock_norcow_write();
 *
 * 3. En _flash_lock(), DESPUÉS de HAL_FLASH_Lock():
 *
 *    // [MOD-S3] Cerrar ventana de escritura MPU
 *    if (mpu_lock_norcow_write) mpu_lock_norcow_write();
 *
 * RESULTADO:
 *   En builds de Fase 1-5 (sin mpu_hardening.c): los símbolos weak
 *   se resuelven a NULL y las llamadas condicionales son no-op.
 *   En builds de Fase 6 (con mpu_hardening.c): se ejecutan las
 *   implementaciones reales que abren y cierran la ventana MPU.
 *
 * @license GNU GPL v3
 */

// Este archivo es solo documentación del parche.
// Ver storage_hal.c para el código real.
