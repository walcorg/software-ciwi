/**
 * @file    flash_protect.h
 * @brief   Protección física de Flash — WalCorg Fase 6
 *
 * Configura los Option Bytes del STM32F405 para proteger
 * el firmware y los datos del wallet contra extracción física.
 *
 * NIVELES DE READOUT PROTECTION (RDP):
 *
 *   Level 0 (0xAA) — Sin protección
 *     Cualquiera con un ST-Link puede leer toda la Flash
 *     incluyendo el firmware y los datos NORCOW cifrados.
 *     ESTADO POR DEFECTO DE FÁBRICA — usado en desarrollo.
 *
 *   Level 1 (0x00 o cualquier valor ≠ 0xAA/0xCC) — Protegido
 *     El debugger NO puede leer la Flash mientras RDP=1.
 *     Volver a RDP=0 (downgrade) BORRA TODA LA FLASH automáticamente
 *     — esto es una característica de seguridad, no un bug.
 *     ÚSESE EN PRODUCCIÓN.
 *
 *   Level 2 (0xCC) — Protección máxima, IRREVERSIBLE
 *     Deshabilita PERMANENTEMENTE el puerto de debug (JTAG/SWD).
 *     No se puede volver a Level 0 o 1 NUNCA — el chip queda
 *     sellado para siempre. Solo para producción final masiva,
 *     después de validar exhaustivamente el firmware.
 *
 * WRITE PROTECTION (WRP):
 *   Además de RDP, se puede proteger sectores específicos contra
 *   escritura — útil para proteger el bootloader contra
 *   modificación accidental o maliciosa.
 *
 * ADVERTENCIA CRÍTICA:
 *   Configurar RDP Level 2 es IRREVERSIBLE. Una vez aplicado,
 *   NUNCA podrás volver a flashear el dispositivo via SWD/JTAG.
 *   Solo usar en la versión FINAL y VALIDADA del firmware.
 *
 * @license GNU GPL v3
 */

#ifndef FLASH_PROTECT_H
#define FLASH_PROTECT_H

#include <stdint.h>
#include <stdbool.h>

/* =========================================================
 * NIVELES DE RDP
 * ========================================================= */
#define RDP_LEVEL_0     0xAA   /**< Sin protección — desarrollo        */
#define RDP_LEVEL_1     0x55   /**< Protegido, reversible — producción */
#define RDP_LEVEL_2     0xCC   /**< Sellado IRREVERSIBLE — masivo       */

/** Sectores a proteger contra escritura (bootloader)
 *  Sectores 0-3 = 64KB del bootloader del F405 */
#define WRP_PROTECT_BOOTLOADER  (FLASH_SECTOR_0 | FLASH_SECTOR_1 | \
                                 FLASH_SECTOR_2 | FLASH_SECTOR_3)

/* =========================================================
 * RESULTADOS
 * ========================================================= */
typedef enum {
    FLASH_PROT_OK = 0,
    FLASH_PROT_ALREADY_SET,    /**< Ya estaba en el nivel solicitado    */
    FLASH_PROT_ERROR,          /**< Error al escribir option bytes      */
    FLASH_PROT_IRREVERSIBLE,   /**< Se solicitó Level 2 — requiere confirmación */
} flash_protect_result_t;

/* =========================================================
 * API PÚBLICA
 * ========================================================= */

/**
 * @brief Lee el nivel actual de RDP del chip.
 * @return RDP_LEVEL_0, RDP_LEVEL_1, o RDP_LEVEL_2
 */
uint8_t flash_protect_get_rdp_level(void);

/**
 * @brief Verifica si la Flash ya tiene protección activa (Level 1 o 2).
 * @return true si RDP > 0
 */
bool flash_protect_is_protected(void);

/**
 * @brief Aplica RDP Level 1 (protección reversible).
 *
 * Recomendado para producción normal. Protege contra lectura
 * via debugger pero permite reflashear (con borrado completo)
 * si es necesario para actualizaciones de firmware autorizadas.
 *
 * IMPORTANTE: esta función reinicia el chip después de aplicar
 * el cambio (los option bytes requieren reset para tomar efecto).
 *
 * @return Código de resultado
 */
flash_protect_result_t flash_protect_enable_rdp1(void);

/**
 * @brief Aplica RDP Level 2 (sellado IRREVERSIBLE).
 *
 * ADVERTENCIA: Esta operación NO SE PUEDE DESHACER.
 * El puerto de debug queda deshabilitado PERMANENTEMENTE.
 *
 * Requiere doble confirmación explícita via el parámetro
 * confirm_irreversible para prevenir activación accidental.
 *
 * @param confirm_irreversible  DEBE ser exactamente 0xDEADBEEF
 *                              para proceder. Cualquier otro valor
 *                              cancela la operación.
 * @return FLASH_PROT_IRREVERSIBLE si confirm_irreversible es incorrecto,
 *         FLASH_PROT_OK si se aplicó (el chip se reiniciará)
 */
flash_protect_result_t flash_protect_enable_rdp2(uint32_t confirm_irreversible);

/**
 * @brief Aplica Write Protection a los sectores especificados.
 *
 * Protege contra escritura/borrado accidental. Diferente de RDP:
 * WRP no impide la LECTURA, solo la ESCRITURA.
 *
 * @param sector_mask  Máscara de bits de los sectores a proteger
 *                      (usar WRP_PROTECT_BOOTLOADER para el bootloader)
 * @return Código de resultado
 */
flash_protect_result_t flash_protect_set_wrp(uint32_t sector_mask);

/**
 * @brief Verifica el estado de Write Protection actual.
 * @return Máscara de sectores actualmente protegidos contra escritura
 */
uint32_t flash_protect_get_wrp(void);

/**
 * @brief Imprime el estado completo de protección por UART.
 * Solo funciona si PRODUCTION=0 (en producción no hay UART debug).
 */
void flash_protect_print_status(void);

#endif /* FLASH_PROTECT_H */
