/**
 * @file    wallet_core.h
 * @brief   Núcleo del wallet Bitcoin — WalCorg Fase 5
 *
 * Capa de alto nivel sobre la librería crypto de Trezor.
 * Encapsula las operaciones Bitcoin más comunes:
 *   - Generar semilla BIP39 con hardware RNG
 *   - Derivar claves BIP44 desde la semilla
 *   - Obtener dirección Bitcoin (Legacy y SegWit)
 *   - Preparar y firmar transacciones
 *
 * RELACIÓN CON LA LIBRERÍA DE TREZOR:
 *
 *   wallet_core.c
 *       ↓ usa directamente
 *   crypto/bip39.c  →  mnemonic_generate(), mnemonic_to_seed()
 *   crypto/bip32.c  →  hdnode_from_seed(), hdnode_private_ckd()
 *   crypto/ecdsa.c  →  ecdsa_sign_digest()
 *   crypto/address.c→  ecdsa_get_address()
 *       ↓ almacena via
 *   storage_hal.c   →  storage_set_seed(), storage_get_seed()
 *
 * RUTA DE DERIVACIÓN BIP44 PARA BITCOIN:
 *   m / 44' / 0' / 0' / 0 / index
 *       │      │    │    │    │
 *       │      │    │    │    └── Índice de dirección (0, 1, 2...)
 *       │      │    │    └─────── 0=externo (recibir), 1=interno (cambio)
 *       │      │    └──────────── Número de cuenta (0 = primera)
 *       │      └───────────────── 0=Bitcoin mainnet, 1=testnet
 *       └──────────────────────── 44=BIP44 purpose
 *
 * @license GNU GPL v3
 */

#ifndef WALLET_CORE_H
#define WALLET_CORE_H

#include <stdint.h>
#include <stdbool.h>

/* =========================================================
 * CONSTANTES
 * ========================================================= */

/** Número de palabras de la semilla (12 o 24) */
#define WALLET_MNEMONIC_WORDS_12    12
#define WALLET_MNEMONIC_WORDS_24    24

/** Longitud máxima del mnemónico en caracteres
 *  24 palabras × 8 chars max + espacios = ~216 chars */
#define WALLET_MNEMONIC_MAX_LEN     240

/** Longitud de la semilla binaria en bytes (512 bits = 64 bytes) */
#define WALLET_SEED_LEN             64

/** Longitud de una dirección Bitcoin Legacy (Base58Check) */
#define WALLET_ADDR_LEGACY_LEN      35

/** Longitud de una dirección Bitcoin SegWit (Bech32) */
#define WALLET_ADDR_BECH32_LEN      63

/** Máximo de índices de dirección soportados */
#define WALLET_MAX_ADDR_INDEX       100

/** BIP44 coin type: 0 = Bitcoin mainnet, 1 = testnet */
#define WALLET_COIN_BITCOIN         0
#define WALLET_COIN_TESTNET         1

/* =========================================================
 * TIPOS
 * ========================================================= */

/** Información completa de una dirección derivada */
typedef struct {
    char     address_legacy[WALLET_ADDR_LEGACY_LEN + 1]; /**< P2PKH Base58 */
    char     address_bech32[WALLET_ADDR_BECH32_LEN + 1]; /**< P2WPKH Bech32*/
    uint8_t  public_key[33];      /**< Clave pública comprimida (33 bytes)  */
    uint32_t index;               /**< Índice BIP44 de esta dirección       */
    bool     is_change;           /**< true si es dirección de cambio       */
} wallet_address_t;

/** Resultado de una operación del wallet */
typedef enum {
    WALLET_OK              =  0,
    WALLET_ERR_NO_SEED     = -1,  /**< No hay semilla almacenada            */
    WALLET_ERR_WRONG_PIN   = -2,  /**< PIN incorrecto                       */
    WALLET_ERR_CRYPTO      = -3,  /**< Error en operación crypto            */
    WALLET_ERR_STORAGE     = -4,  /**< Error de almacenamiento Flash        */
    WALLET_ERR_PARAM       = -5,  /**< Parámetro inválido                   */
} wallet_result_t;

/* =========================================================
 * API PÚBLICA
 * ========================================================= */

/**
 * @brief Genera una nueva semilla BIP39 usando el hardware RNG.
 *
 * Genera WALLET_MNEMONIC_WORDS_12 o 24 palabras, verifica
 * el checksum BIP39, cifra con el PIN y almacena en Flash.
 *
 * FLUJO:
 *   hardware RNG → entropía → BIP39 mnemónico → cifrar → Flash
 *
 * @param num_words  12 o 24 palabras
 * @param pin        PIN para cifrar la semilla
 * @param mnemonic_out  Buffer donde se copia el mnemónico generado
 *                      (para mostrar al usuario). LIMPIAR DESPUÉS.
 * @param buf_len    Tamaño del buffer (mínimo WALLET_MNEMONIC_MAX_LEN)
 * @return           WALLET_OK o código de error
 */
wallet_result_t wallet_generate_seed(uint8_t num_words, const char *pin,
                                      char *mnemonic_out, uint32_t buf_len);

/**
 * @brief Importa una semilla BIP39 existente.
 *
 * Para recuperar un wallet desde una semilla de respaldo.
 * Verifica el checksum BIP39 antes de almacenar.
 *
 * @param mnemonic  Frase mnemónica a importar
 * @param pin       PIN para cifrar la semilla
 * @return          WALLET_OK o código de error
 */
wallet_result_t wallet_import_seed(const char *mnemonic, const char *pin);

/**
 * @brief Deriva una dirección Bitcoin desde la semilla almacenada.
 *
 * Descifra la semilla con el PIN, deriva la clave en la ruta
 * BIP44 correspondiente al índice y genera la dirección.
 *
 * FLUJO:
 *   Flash → descifrar(PIN) → semilla → BIP32 → BIP44 → dirección
 *
 * @param pin      PIN para descifrar la semilla
 * @param index    Índice de dirección (0 = primera, 1 = segunda, etc.)
 * @param coin     WALLET_COIN_BITCOIN o WALLET_COIN_TESTNET
 * @param addr_out Struct donde se devuelve la dirección derivada
 * @return         WALLET_OK o código de error
 */
wallet_result_t wallet_get_address(const char *pin, uint32_t index,
                                    uint8_t coin,
                                    wallet_address_t *addr_out);

/**
 * @brief Firma un hash de transacción Bitcoin.
 *
 * Descifra la semilla, deriva la clave privada BIP44 y firma
 * el hash con ECDSA secp256k1.
 *
 * @param pin        PIN para descifrar la semilla
 * @param tx_hash    Hash de 32 bytes de la transacción a firmar
 * @param key_index  Índice BIP44 de la clave a usar
 * @param coin       WALLET_COIN_BITCOIN o WALLET_COIN_TESTNET
 * @param sig_out    Buffer de 64 bytes donde se devuelve la firma (r+s)
 * @param pby_out    Byte de paridad (para recuperación de clave pública)
 * @return           WALLET_OK o código de error
 */
wallet_result_t wallet_sign_tx(const char *pin,
                                const uint8_t *tx_hash,
                                uint32_t key_index, uint8_t coin,
                                uint8_t *sig_out, uint8_t *pby_out);

/**
 * @brief Verifica si el wallet tiene una semilla configurada.
 * @return true si hay semilla en Flash
 */
bool wallet_is_initialized(void);

/**
 * @brief Retorna información sobre el wallet activo (sin semilla).
 * @param label_out  Buffer para el nombre del dispositivo
 * @param label_len  Tamaño del buffer
 */
void wallet_get_info(char *label_out, uint32_t label_len);

/**
 * @brief Borra completamente el wallet (factory reset).
 * @param pin  PIN actual para autorizar el borrado
 * @return     WALLET_OK si se borró, WALLET_ERR_WRONG_PIN si el PIN falla
 */
wallet_result_t wallet_wipe(const char *pin);

/**
 * @brief Convierte un código de resultado a string (debug).
 */
const char *wallet_result_str(wallet_result_t result);

#endif /* WALLET_CORE_H */