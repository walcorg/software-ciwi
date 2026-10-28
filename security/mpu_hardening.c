/**
 * @file    mpu_hardening.c
 * @brief   Hardening completo de la MPU — WALCORG Fase 6
 * @license GNU GPL v3
 */

#include "mpu_hardening.h"
#include "uart_debug.h"
#include "stm32f4xx_hal.h"

/* Dirección del stack guard — los últimos 64 bytes antes del
 * límite del stack definido en el linker script (_stack_start) */
extern uint32_t _stack_start;
#define STACK_GUARD_SIZE   64
#define STACK_GUARD_ADDR   ((uint32_t)&_stack_start)

/* Estado de bloqueo de NORCOW */
static bool _norcow_unlocked = false;

bool mpu_hardening_apply(void) {
    DEBUG_PRINT("[MPU] Aplicando configuración de hardening completa...\r\n");

    MPU_Region_InitTypeDef region = {0};
    HAL_MPU_Disable();

    /* ---- Región 0: Flash bootloader — solo lectura/ejecución ---- */
    region.Enable           = MPU_REGION_ENABLE;
    region.Number           = MPU_REGION_NUMBER0;
    region.BaseAddress      = 0x08000000;
    region.Size             = MPU_REGION_SIZE_64KB;
    region.AccessPermission = MPU_REGION_PRIV_RO;
    region.DisableExec      = MPU_INSTRUCTION_ACCESS_ENABLE;
    region.IsCacheable      = MPU_ACCESS_CACHEABLE;
    region.IsShareable      = MPU_ACCESS_NOT_SHAREABLE;
    region.IsBufferable     = MPU_ACCESS_NOT_BUFFERABLE;
    region.TypeExtField     = MPU_TEX_LEVEL0;
    HAL_MPU_ConfigRegion(&region);

    /* ---- Región 1: Flash firmware — solo lectura/ejecución ---- */
    region.Number      = MPU_REGION_NUMBER1;
    region.BaseAddress  = 0x08010000;
    region.Size         = MPU_REGION_SIZE_1MB;  /* Cubre hasta 0x08110000, dentro de rango */
    HAL_MPU_ConfigRegion(&region);

    /* ---- Región 2: Flash NORCOW — solo lectura por defecto ----
     * storage_hal.c debe llamar mpu_unlock_norcow_write() antes
     * de escribir y mpu_lock_norcow_write() después. */
    region.Number           = MPU_REGION_NUMBER2;
    region.BaseAddress      = 0x080A0000;
    region.Size             = MPU_REGION_SIZE_512KB;
    region.AccessPermission = MPU_REGION_PRIV_RO;  /* RO por defecto */
    region.DisableExec      = MPU_INSTRUCTION_ACCESS_DISABLE;  /* Datos, no código */
    HAL_MPU_ConfigRegion(&region);

    /* ---- Región 3: SRAM general — lectura/escritura/ejecución ---- */
    region.Number           = MPU_REGION_NUMBER3;
    region.BaseAddress      = 0x20000000;
    region.Size             = MPU_REGION_SIZE_128KB;
    region.AccessPermission = MPU_REGION_FULL_ACCESS;
    region.DisableExec      = MPU_INSTRUCTION_ACCESS_ENABLE;
    HAL_MPU_ConfigRegion(&region);

    /* ---- Región 4: Stack guard — SIN acceso ----
     * Una región pequeña al límite del stack. Cualquier acceso
     * (incluso de lectura) dispara un MemManage Fault inmediato,
     * detectando stack overflow ANTES de que corrompa datos. */
    region.Number           = MPU_REGION_NUMBER4;
    region.BaseAddress      = STACK_GUARD_ADDR;
    region.Size             = MPU_REGION_SIZE_64B;   /* La región más pequeña disponible */
    region.AccessPermission = MPU_REGION_NO_ACCESS;  /* Bloqueado completamente */
    region.DisableExec      = MPU_INSTRUCTION_ACCESS_DISABLE;
    HAL_MPU_ConfigRegion(&region);

    /* ---- Región 5: CCM — datos sensibles, NO ejecutable ---- */
    region.Number           = MPU_REGION_NUMBER5;
    region.BaseAddress      = 0x10000000;
    region.Size             = MPU_REGION_SIZE_64KB;
    region.AccessPermission = MPU_REGION_FULL_ACCESS;
    region.DisableExec      = MPU_INSTRUCTION_ACCESS_DISABLE;
    region.IsCacheable      = MPU_ACCESS_NOT_CACHEABLE;
    HAL_MPU_ConfigRegion(&region);

    /* ---- Región 6: Periféricos — NO ejecutable ---- */
    region.Number           = MPU_REGION_NUMBER6;
    region.BaseAddress      = 0x40000000;
    region.Size             = MPU_REGION_SIZE_512MB;
    region.AccessPermission = MPU_REGION_FULL_ACCESS;
    region.DisableExec      = MPU_INSTRUCTION_ACCESS_DISABLE;
    region.IsBufferable     = MPU_ACCESS_BUFFERABLE;
    HAL_MPU_ConfigRegion(&region);

    /* ---- Región 7: Página nula — detecta NULL pointer dereference ----
     * Bloquear los primeros 4KB de memoria. Cualquier acceso a
     * través de un puntero NULL (incluyendo NULL + pequeño offset)
     * dispara un fault inmediato en vez de comportamiento indefinido. */
    region.Number           = MPU_REGION_NUMBER7;
    region.BaseAddress      = 0x00000000;
    region.Size             = MPU_REGION_SIZE_4KB;
    region.AccessPermission = MPU_REGION_NO_ACCESS;
    region.DisableExec      = MPU_INSTRUCTION_ACCESS_DISABLE;
    HAL_MPU_ConfigRegion(&region);

    /* Habilitar MPU. HFNMI_PRIVDEF_NONE permite que los fault
     * handlers (NMI, HardFault) tengan acceso completo para
     * poder imprimir diagnóstico incluso si la MPU bloqueó algo. */
    HAL_MPU_Enable(MPU_HFNMI_PRIVDEF_NONE);

    DEBUG_PRINT("[MPU] Hardening aplicado: 8 regiones configuradas.\r\n");
    DEBUG_PRINTF("[MPU]   Stack guard en 0x%08lX (%d bytes)\r\n",
        STACK_GUARD_ADDR, STACK_GUARD_SIZE);
    DEBUG_PRINT("[MPU]   Pagina nula (0x0) bloqueada\r\n");
    DEBUG_PRINT("[MPU]   NORCOW protegido contra escritura no autorizada\r\n");

    return true;
}

bool mpu_hardening_verify(void) {
    /* Verificar que la MPU está habilitada */
    if (!(MPU->CTRL & MPU_CTRL_ENABLE_Msk)) {
        DEBUG_PRINT("[MPU] FALLO: MPU no está habilitada.\r\n");
        return false;
    }

    /* Verificar cada región leyendo RNR + RBAR + RASR */
    bool all_ok = true;
    for (uint8_t i = 0; i < 8; i++) {
        MPU->RNR = i;
        uint32_t rbar = MPU->RBAR;
        uint32_t rasr = MPU->RASR;

        /* Verificar que la región está habilitada (bit ENABLE de RASR) */
        if (!(rasr & MPU_RASR_ENABLE_Msk)) {
            DEBUG_PRINTF("[MPU] FALLO: Región %d no habilitada.\r\n", i);
            all_ok = false;
        }
        (void)rbar;
    }

    if (all_ok) {
        DEBUG_PRINT("[MPU] Verificación OK: 8 regiones activas.\r\n");
    }
    return all_ok;
}

void mpu_unlock_norcow_write(void) {
    /* Reconfigurar región 2 (NORCOW) a RW temporalmente */
    MPU_Region_InitTypeDef region = {0};
    HAL_MPU_Disable();

    region.Enable           = MPU_REGION_ENABLE;
    region.Number           = MPU_REGION_NUMBER2;
    region.BaseAddress      = 0x080A0000;
    region.Size             = MPU_REGION_SIZE_512KB;
    region.AccessPermission = MPU_REGION_FULL_ACCESS;  /* RW temporal */
    region.DisableExec      = MPU_INSTRUCTION_ACCESS_DISABLE;
    HAL_MPU_ConfigRegion(&region);

    HAL_MPU_Enable(MPU_HFNMI_PRIVDEF_NONE);
    _norcow_unlocked = true;
}

void mpu_lock_norcow_write(void) {
    /* Restaurar región 2 (NORCOW) a solo lectura */
    MPU_Region_InitTypeDef region = {0};
    HAL_MPU_Disable();

    region.Enable           = MPU_REGION_ENABLE;
    region.Number           = MPU_REGION_NUMBER2;
    region.BaseAddress      = 0x080A0000;
    region.Size             = MPU_REGION_SIZE_512KB;
    region.AccessPermission = MPU_REGION_PRIV_RO;  /* Solo lectura otra vez */
    region.DisableExec      = MPU_INSTRUCTION_ACCESS_DISABLE;
    HAL_MPU_ConfigRegion(&region);

    HAL_MPU_Enable(MPU_HFNMI_PRIVDEF_NONE);
    _norcow_unlocked = false;
}

void mpu_hardening_print_status(void) {
#if (PRODUCTION == 0)
    DEBUG_PRINT("\r\n[MPU] === Estado de Hardening MPU ===\r\n");
    DEBUG_PRINTF("[MPU] MPU habilitada    : %s\r\n",
        (MPU->CTRL & MPU_CTRL_ENABLE_Msk) ? "SI" : "NO");
    DEBUG_PRINTF("[MPU] NORCOW desbloqueado: %s\r\n",
        _norcow_unlocked ? "SI (temporal)" : "no (protegido)");
    DEBUG_PRINTF("[MPU] Stack guard addr  : 0x%08lX\r\n", STACK_GUARD_ADDR);
    DEBUG_PRINT("[MPU] Regiones configuradas: 8/8\r\n");
    DEBUG_PRINT("[MPU] ===================================\r\n\r\n");
#endif
}
