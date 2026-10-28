/**
 * @file    security_audit.h
 * @brief   Auditoría automatizada de seguridad — WALCORG Fase 6
 *
 * Checklist ejecutable que verifica TODAS las protecciones de
 * seguridad antes de considerar el firmware listo para producción.
 *
 * Este es el último gate antes de:
 *   1. Aplicar RDP Level 1 (flash_protect_enable_rdp1)
 *   2. Compilar con PRODUCTION=1
 *   3. Publicar el firmware para distribución
 *
 * @license GNU GPL v3
 */

#ifndef SECURITY_AUDIT_H
#define SECURITY_AUDIT_H

#include <stdint.h>
#include <stdbool.h>

/** Resultado detallado de la auditoría */
typedef struct {
    bool rdp_configured;        /**< RDP Level 1 o 2 activo            */
    bool mpu_hardened;          /**< 8 regiones MPU configuradas       */
    bool rng_validated;         /**< RNG pasó todos los tests          */
    bool crypto_validated;      /**< BIP39/BIP32/ECDSA validados       */
    bool production_build;      /**< Compilado con PRODUCTION=1        */
    bool tamper_active;         /**< Sistema anti-tamper inicializado  */
    bool pin_delay_active;      /**< Rate-limiting de PIN configurado  */
    bool storage_encrypted;     /**< Verificación de cifrado AES       */
    bool no_debug_strings;      /**< Sin strings de debug en el binario*/
    uint8_t score;              /**< Puntaje 0-100                     */
    bool production_ready;      /**< true si TODO pasó                 */
} security_audit_result_t;

/**
 * @brief Ejecuta la auditoría completa de seguridad.
 *
 * @param result  Struct donde se guardan los resultados detallados
 * @return        true si el firmware está listo para producción
 */
bool security_audit_run(security_audit_result_t *result);

/**
 * @brief Imprime un reporte detallado de la auditoría por UART.
 * @param result  Resultados de security_audit_run()
 */
void security_audit_print_report(const security_audit_result_t *result);

#endif /* SECURITY_AUDIT_H */
