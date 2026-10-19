/**
 * @file    storage_hal.h
 * @brief   HAL de almacenamiento seguro NORCOW — WalCorg Fase 5
 *
 * NORCOW (NOR flash Copy-On-Write) es el sistema de almacenamiento
 * de clave-valor de Trezor. Guarda en Flash:
 *   - Hash del PIN
 *   - Semilla cifrada (BIP39 mnemónico cifrado con PIN)
 *   - Configuración del dispositivo
 *   - Contador de intentos de PIN fallidos
 *
 * LAYOUT DE FLASH STM32F405RGT6:
 *   Sector 0-3  (4×16KB = 64KB)  → Bootloader
 *   Sector 4-8  (576KB)          → Firmware WalCorg
 *   Sector 9    (128KB) 0x080A0000 → NORCOW bloque A
 *   Sector 10   (128KB) 0x08080000 → NORCOW bloque B
 *   Sector 11   (128KB) 0x080C0000 → NORCOW bloque C (backup)
 *
 * NORCOW alterna entre bloques A y B para wear leveling.
 * El bloque C es copia de seguridad contra corte de energía.
 *
 * ADAPTACIÓN DEL ORIGINAL DE TREZOR:
 *   Trezor One usa STM32F205 con sectores de 128KB en 0x08020000.
 *   Nosotros usamos F405 con sectores en 0x080A0000.
 *   [MOD-S1] Cambiar NORCOW_SECTOR_A y NORCOW_SECTOR_B
 *   [MOD-S2] Cambiar los números de sector Flash para HAL_FLASH
 *
 * SEGURIDAD:
 *   La semilla NUNCA se almacena en texto plano.
 *   Se cifra con AES-256-CBC usando una clave derivada del PIN.
 *   El PIN se almacena como PBKDF2-HMAC-SHA256 con sal aleatoria.
 *
 * @license GNU GPL v3
 */

#ifndef STORAGE_HAL_H
#define STORAGE_HAL_H

#include <stdint.h>
#include <stdbool.h>
#include "stm32f4xx_hal.h"

/* =========================================================
 * LAYOUT DE FLASH — SECTORES NORCOW
 * [MOD-S1] Direcciones ajustadas para STM32F405RGT6
 * ========================================================= */

/** Inicio del sector 9 (NORCOW bloque A) en el F405 */
#define NORCOW_SECTOR_A_ADDR    0x080A0000UL

/** Inicio del sector 10 (NORCOW bloque B) */
#define NORCOW_SECTOR_B_ADDR    0x080C0000UL

/** Inicio del sector 11 (NORCOW bloque backup) */
#define NORCOW_SECTOR_BACKUP    0x080E0000UL

/** Tamaño de cada sector NORCOW (128KB) */
#define NORCOW_SECTOR_SIZE      (128 * 1024)

/** [MOD-S2] Números de sector para HAL_FLASHEx_Erase() del F405
 *  El F405 tiene: sectores 0-3 (16KB), 4 (64KB), 5-11 (128KB) */
#define NORCOW_HAL_SECTOR_A     FLASH_SECTOR_9
#define NORCOW_HAL_SECTOR_B     FLASH_SECTOR_10
#define NORCOW_HAL_SECTOR_BACKUP FLASH_SECTOR_11

/* =========================================================
 * CLAVES DE ALMACENAMIENTO
 * Cada entrada en NORCOW tiene una clave de 2 bytes (app_id, key)
 * ========================================================= */

/** App ID para datos del wallet */
#define STORAGE_APP_WALLET      0x01

/** Claves dentro del app wallet */
#define STORAGE_KEY_PIN_HASH    0x01  /**< Hash PBKDF2 del PIN         */
#define STORAGE_KEY_PIN_SALT    0x02  /**< Sal aleatoria del PIN       */
#define STORAGE_KEY_SEED_ENC    0x03  /**< Semilla cifrada con PIN     */
#define STORAGE_KEY_SEED_IV     0x04  /**< IV del cifrado de semilla   */
#define STORAGE_KEY_FAIL_COUNT  0x05  /**< Contador de PINs fallidos   */
#define STORAGE_KEY_LABEL       0x06  /**< Nombre del dispositivo      */
#define STORAGE_KEY_FLAGS       0x07  /**< Flags de configuración      */
#define STORAGE_KEY_INITIALIZED 0x08  /**< true si el wallet está init */

/** Máximo de intentos de PIN antes de borrar (wipe) */
#define STORAGE_PIN_MAX_FAILS   10

/** Longitud del hash del PIN en bytes (SHA256 = 32 bytes) */
#define STORAGE_PIN_HASH_LEN    32

/** Longitud de la sal del PIN en bytes */
#define STORAGE_PIN_SALT_LEN    16

/** Longitud máxima de la semilla cifrada
 *  24 palabras BIP39 × máx 8 chars + espacios = ~200 chars
 *  Cifrado AES-256 con padding → 224 bytes */
#define STORAGE_SEED_ENC_LEN    224

/** Longitud del IV de AES (128 bits = 16 bytes) */
#define STORAGE_AES_IV_LEN      16

/** Longitud máxima del label del dispositivo */
#define STORAGE_LABEL_MAX_LEN   33

/* =========================================================
 * FLAGS DE CONFIGURACIÓN
 * ========================================================= */
#define STORAGE_FLAG_INITIALIZED    (1 << 0)  /**< Wallet configurado    */
#define STORAGE_FLAG_PIN_ENABLED    (1 << 1)  /**< PIN activo            */
#define STORAGE_FLAG_PASSPHRASE     (1 << 2)  /**< Passphrase habilitada */

/* =========================================================
 * TIPOS
 * ========================================================= */

/** Información completa de una entrada almacenada */
typedef struct {
    uint8_t  pin_hash[STORAGE_PIN_HASH_LEN];   /**< PBKDF2 del PIN       */
    uint8_t  pin_salt[STORAGE_PIN_SALT_LEN];   /**< Sal aleatoria        */
    uint8_t  seed_enc[STORAGE_SEED_ENC_LEN];   /**< Semilla cifrada      */
    uint8_t  seed_iv[STORAGE_AES_IV_LEN];      /**< IV del cifrado       */
    uint8_t  fail_count;                        /**< Intentos fallidos    */
    uint32_t flags;                             /**< Flags de config      */
    char     label[STORAGE_LABEL_MAX_LEN];      /**< Nombre del device    */
} storage_data_t;

/* =========================================================
 * API PÚBLICA
 * ========================================================= */

/**
 * @brief Inicializa el subsistema de almacenamiento.
 *
 * Verifica que los sectores NORCOW están accesibles y
 * carga el estado actual en memoria caché.
 *
 * @return true  si inicializó correctamente (incluso si vacío)
 * @return false si hay error de Flash hardware
 */
bool storage_hal_init(void);

/**
 * @brief Verifica si el wallet está inicializado (tiene semilla).
 * @return true si hay semilla almacenada
 */
bool storage_is_initialized(void);

/**
 * @brief Verifica un PIN contra el hash almacenado.
 *
 * Calcula PBKDF2-HMAC-SHA256(PIN, salt, iterations) y
 * compara con el hash almacenado en Flash.
 *
 * @param pin    PIN en texto plano (máx 9 dígitos)
 * @return       true si el PIN es correcto
 */
bool storage_verify_pin(const char *pin);

/**
 * @brief Establece un nuevo PIN.
 *
 * Genera sal aleatoria, calcula el hash y lo guarda en Flash.
 * También re-cifra la semilla con el nuevo PIN si ya existe.
 *
 * @param pin    Nuevo PIN en texto plano
 * @return       true si se guardó correctamente
 */
bool storage_set_pin(const char *pin);

/**
 * @brief Guarda la semilla BIP39 cifrada con el PIN.
 *
 * La semilla se cifra con AES-256-CBC usando una clave
 * derivada del PIN con PBKDF2. El IV es aleatorio.
 *
 * @param mnemonic  Frase mnemónica BIP39 (12 o 24 palabras)
 * @param pin       PIN actual para cifrar
 * @return          true si se guardó correctamente
 */
bool storage_set_seed(const char *mnemonic, const char *pin);

/**
 * @brief Recupera la semilla BIP39 descifrada.
 *
 * Descifra la semilla usando el PIN provisto.
 * El buffer de salida debe tener al menos 240 bytes.
 *
 * @param pin       PIN para descifrar
 * @param mnemonic_out  Buffer donde se guarda la semilla
 * @param buf_size  Tamaño del buffer (mínimo 240)
 * @return          true si el PIN es correcto y se descifró
 */
bool storage_get_seed(const char *pin, char *mnemonic_out,
                      uint32_t buf_size);

/**
 * @brief Incrementa el contador de intentos fallidos de PIN.
 *
 * Si llega a STORAGE_PIN_MAX_FAILS, borra automáticamente
 * todos los datos del wallet (wipe).
 *
 * @return Número de intentos fallidos acumulados
 */
uint8_t storage_increment_fail_count(void);

/**
 * @brief Resetea el contador de intentos fallidos (PIN correcto).
 */
void storage_reset_fail_count(void);

/**
 * @brief Retorna el número de intentos fallidos actuales.
 */
uint8_t storage_get_fail_count(void);

/**
 * @brief Borra TODOS los datos del wallet (factory reset).
 *
 * Borra y reinicializa los sectores NORCOW 9, 10 y 11.
 * OPERACIÓN IRREVERSIBLE — los fondos son inaccesibles
 * sin la semilla de respaldo en papel.
 *
 * @return true si el borrado fue exitoso
 */
bool storage_wipe(void);

/**
 * @brief Lee el label del dispositivo.
 * @param label_out  Buffer de al menos STORAGE_LABEL_MAX_LEN bytes
 */
void storage_get_label(char *label_out);

/**
 * @brief Establece el label del dispositivo.
 * @param label  Nuevo label (máx 32 chars)
 * @return       true si se guardó correctamente
 */
bool storage_set_label(const char *label);

/**
 * @brief Imprime estado del almacenamiento por UART (debug).
 * Solo disponible con PRODUCTION=0.
 */
void storage_print_status(void);

#endif /* STORAGE_HAL_H */
