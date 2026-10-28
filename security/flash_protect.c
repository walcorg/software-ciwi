/**
 * @file    flash_protect.c
 * @brief   Protección física de Flash — WALCORG Fase 6
 *
 * Los Option Bytes del STM32F4 viven en una región especial de
 * Flash separada del código de usuario (0x1FFFC000-0x1FFFC00F).
 * Se configuran via el registro FLASH_OPTCR y requieren un
 * procedimiento de desbloqueo distinto al de la Flash normal.
 *
 * SECUENCIA PARA MODIFICAR OPTION BYTES:
 *   1. HAL_FLASH_Unlock()        — desbloquear Flash normal
 *   2. HAL_FLASH_OB_Unlock()     — desbloquear Option Bytes (clave distinta)
 *   3. Modificar OPTCR via HAL_FLASHEx_OBProgram()
 *   4. HAL_FLASH_OB_Launch()     — aplicar cambios (causa system reset)
 *   5. HAL_FLASH_OB_Lock()       — re-bloquear
 *   6. HAL_FLASH_Lock()          — re-bloquear Flash normal
 *
 * @license GNU GPL v3
 */

#include "flash_protect.h"
#include "uart_debug.h"
#include "stm32f4xx_hal.h"
#include <stdio.h>

/* =========================================================
 * API PÚBLICA
 * ========================================================= */

uint8_t flash_protect_get_rdp_level(void) {
    FLASH_OBProgramInitTypeDef ob_config = {0};
    HAL_FLASHEx_OBGetConfig(&ob_config);
    return (uint8_t)ob_config.RDPLevel;
}

bool flash_protect_is_protected(void) {
    uint8_t level = flash_protect_get_rdp_level();
    return (level != RDP_LEVEL_0);
}

flash_protect_result_t flash_protect_enable_rdp1(void) {
    DEBUG_PRINT("[PROT] Aplicando RDP Level 1...\r\n");

    /* Verificar si ya está protegido */
    uint8_t current = flash_protect_get_rdp_level();
    if (current != RDP_LEVEL_0) {
        DEBUG_PRINTF("[PROT] Ya protegido (RDP=0x%02X). Sin cambios.\r\n",
            current);
        return FLASH_PROT_ALREADY_SET;
    }

    /* Desbloquear Flash y Option Bytes */
    if (HAL_FLASH_Unlock() != HAL_OK) {
        DEBUG_PRINT("[PROT] ERROR: No se pudo desbloquear Flash.\r\n");
        return FLASH_PROT_ERROR;
    }
    if (HAL_FLASH_OB_Unlock() != HAL_OK) {
        DEBUG_PRINT("[PROT] ERROR: No se pudo desbloquear Option Bytes.\r\n");
        HAL_FLASH_Lock();
        return FLASH_PROT_ERROR;
    }

    /* Configurar RDP Level 1 */
    FLASH_OBProgramInitTypeDef ob_config = {0};
    ob_config.OptionType = OPTIONBYTE_RDP;
    ob_config.RDPLevel   = OB_RDP_LEVEL_1;

    HAL_StatusTypeDef st = HAL_FLASHEx_OBProgram(&ob_config);

    if (st != HAL_OK) {
        DEBUG_PRINT("[PROT] ERROR: HAL_FLASHEx_OBProgram falló.\r\n");
        HAL_FLASH_OB_Lock();
        HAL_FLASH_Lock();
        return FLASH_PROT_ERROR;
    }

    DEBUG_PRINT("[PROT] RDP Level 1 configurado. Aplicando (reset)...\r\n");
    DEBUG_PRINT("[PROT] El dispositivo se reiniciara ahora.\r\n");

    /* Dar tiempo a que el mensaje UART se transmita antes del reset */
    HAL_Delay(100);

    /* HAL_FLASH_OB_Launch() causa un reset INMEDIATO del sistema
     * para que los nuevos option bytes tomen efecto.
     * El código después de esta línea NO se ejecuta. */
    HAL_FLASH_OB_Launch();

    /* Nunca llegamos aquí — el chip se resetea en OB_Launch() */
    return FLASH_PROT_OK;
}

flash_protect_result_t flash_protect_enable_rdp2(uint32_t confirm_irreversible) {

    /* Verificación de seguridad CRÍTICA contra activación accidental */
    if (confirm_irreversible != 0xDEADBEEF) {
        DEBUG_PRINT("[PROT] RDP Level 2 CANCELADO — falta confirmación.\r\n");
        DEBUG_PRINT("[PROT] Esta operación es IRREVERSIBLE.\r\n");
        return FLASH_PROT_IRREVERSIBLE;
    }

    DEBUG_PRINT("\r\n");
    DEBUG_PRINT("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\r\n");
    DEBUG_PRINT("!!! ADVERTENCIA: RDP LEVEL 2 ES IRREVERSIBLE !!!\r\n");
    DEBUG_PRINT("!!! El puerto de debug se deshabilita PARA SIEMPRE !!!\r\n");
    DEBUG_PRINT("!!! NUNCA podras reflashear este chip via SWD/JTAG !!!\r\n");
    DEBUG_PRINT("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\r\n\r\n");
    DEBUG_PRINT("[PROT] Aplicando en 5 segundos...\r\n");
    HAL_Delay(5000);  /* Última oportunidad de cancelar (power-off) */

    if (HAL_FLASH_Unlock() != HAL_OK) return FLASH_PROT_ERROR;
    if (HAL_FLASH_OB_Unlock() != HAL_OK) {
        HAL_FLASH_Lock();
        return FLASH_PROT_ERROR;
    }

    FLASH_OBProgramInitTypeDef ob_config = {0};
    ob_config.OptionType = OPTIONBYTE_RDP;
    ob_config.RDPLevel   = OB_RDP_LEVEL_2;

    HAL_StatusTypeDef st = HAL_FLASHEx_OBProgram(&ob_config);
    if (st != HAL_OK) {
        HAL_FLASH_OB_Lock();
        HAL_FLASH_Lock();
        return FLASH_PROT_ERROR;
    }

    DEBUG_PRINT("[PROT] RDP Level 2 configurado. Aplicando (reset)...\r\n");
    HAL_Delay(100);
    HAL_FLASH_OB_Launch();  /* Reset inmediato, irreversible */

    return FLASH_PROT_OK;  /* Nunca se alcanza */
}

flash_protect_result_t flash_protect_set_wrp(uint32_t sector_mask) {
    DEBUG_PRINTF("[PROT] Aplicando Write Protection a sectores: 0x%08lX\r\n",
        sector_mask);

    if (HAL_FLASH_Unlock() != HAL_OK) return FLASH_PROT_ERROR;
    if (HAL_FLASH_OB_Unlock() != HAL_OK) {
        HAL_FLASH_Lock();
        return FLASH_PROT_ERROR;
    }

    FLASH_OBProgramInitTypeDef ob_config = {0};
    ob_config.OptionType = OPTIONBYTE_WRP;
    ob_config.WRPState   = OB_WRPSTATE_ENABLE;
    ob_config.WRPSector  = sector_mask;
    ob_config.Banks      = FLASH_BANK_1;

    HAL_StatusTypeDef st = HAL_FLASHEx_OBProgram(&ob_config);

    HAL_FLASH_OB_Lock();
    HAL_FLASH_Lock();

    if (st != HAL_OK) {
        DEBUG_PRINT("[PROT] ERROR: No se pudo configurar WRP.\r\n");
        return FLASH_PROT_ERROR;
    }

    DEBUG_PRINT("[PROT] Write Protection configurada.\r\n");
    DEBUG_PRINT("[PROT] NOTA: requiere HAL_FLASH_OB_Launch() para aplicar.\r\n");
    return FLASH_PROT_OK;
}

uint32_t flash_protect_get_wrp(void) {
    FLASH_OBProgramInitTypeDef ob_config = {0};
    ob_config.Banks = FLASH_BANK_1;
    HAL_FLASHEx_OBGetConfig(&ob_config);
    /* WRPSector contiene el inverso (1=NO protegido) en algunos HAL —
     * verificar contra el datasheet de tu versión de HAL */
    return ob_config.WRPSector;
}

void flash_protect_print_status(void) {
#if (PRODUCTION == 0)
    uint8_t rdp = flash_protect_get_rdp_level();
    uint32_t wrp = flash_protect_get_wrp();

    DEBUG_PRINT("\r\n[PROT] === Estado de Protección Flash ===\r\n");
    DEBUG_PRINTF("[PROT] RDP Level    : 0x%02X (%s)\r\n", rdp,
        rdp == RDP_LEVEL_0 ? "0 - SIN PROTECCION" :
        rdp == RDP_LEVEL_2 ? "2 - SELLADO IRREVERSIBLE" :
        "1 - PROTEGIDO");
    DEBUG_PRINTF("[PROT] WRP Sectors  : 0x%08lX\r\n", wrp);
    DEBUG_PRINTF("[PROT] Recomendado en producción: RDP Level 1\r\n");

    if (rdp == RDP_LEVEL_0) {
        DEBUG_PRINT("[PROT] !!! ADVERTENCIA: Flash SIN PROTEGER !!!\r\n");
        DEBUG_PRINT("[PROT] Cualquiera con ST-Link puede leer el firmware\r\n");
        DEBUG_PRINT("[PROT] y los datos NORCOW cifrados de este dispositivo.\r\n");
    }
    DEBUG_PRINT("[PROT] =====================================\r\n\r\n");
#endif
}
