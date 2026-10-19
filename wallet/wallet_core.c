/**
 * @file    wallet_core.c
 * @brief   Implementación del núcleo del wallet — WalCorg Fase 5
 *
 * Todas las operaciones que involucran la semilla o claves privadas
 * siguen el mismo patrón de seguridad:
 *
 *   1. Descifrar semilla desde Flash usando el PIN
 *   2. Derivar la clave necesaria en un buffer temporal de SRAM
 *   3. Ejecutar la operación crypto
 *   4. Limpiar TODOS los buffers sensibles con memset(0)
 *   5. Retornar solo el resultado (nunca la clave)
 *
 * @license GNU GPL v3
 */

#include "wallet_core.h"
#include "storage_hal.h"
#include "rng_test.h"
#include "uart_debug.h"
#include "stm32f4xx_hal.h"

/* Crypto de Trezor — sin modificar */
#include "bip39.h"
#include "bip32.h"
#include "curves.h"
#include "secp256k1.h"   /* [v0.2] declara la curva secp256k1 */
#include "ecdsa.h"
#include "address.h"
#include "segwit_addr.h"
#include "sha2.h"

#include <string.h>
#include <stdio.h>

/* =========================================================
 * FUNCIONES PRIVADAS — DERIVACIÓN DE CLAVES
 * ========================================================= */

/**
 * _seed_to_hdnode — Convertir semilla binaria a nodo HD raíz
 *
 * Aplica HMAC-SHA512 con la clave "Bitcoin seed" para generar
 * la clave maestra según el estándar BIP32.
 *
 * @param seed      Semilla binaria de 64 bytes
 * @param node_out  Nodo HD resultante
 * @return          true si la conversión fue exitosa
 */
static bool _seed_to_hdnode(const uint8_t *seed, HDNode *node_out) {
    int ret = hdnode_from_seed(seed, WALLET_SEED_LEN,
                               SECP256K1_NAME, node_out);
    return (ret == 1);
}

/**
 * _derive_bip44_node — Derivar nodo HD en ruta BIP44
 *
 * Ruta: m/44'/coin'/0'/change/index
 *
 * @param root    Nodo HD raíz (clave maestra)
 * @param coin    Coin type (0=Bitcoin, 1=testnet)
 * @param change  0=dirección externa (recibir), 1=cambio
 * @param index   Índice de la dirección
 * @param node    Nodo HD resultante en la ruta BIP44
 * @return        true si la derivación fue exitosa
 */
static bool _derive_bip44_node(const HDNode *root, uint8_t coin,
                                uint32_t change, uint32_t index,
                                HDNode *node) {
    /* Copiar el nodo raíz */
    memcpy(node, root, sizeof(HDNode));

    /* Derivar cada nivel de la ruta BIP44 */
    /* Los ' indican derivación endurecida (hardened):
     * hardened child index = index + 0x80000000 */
    bool ok = true;
    ok &= (hdnode_private_ckd_prime(node, 44)   == 1); /* m/44'        */
    ok &= (hdnode_private_ckd_prime(node, coin)  == 1); /* m/44'/coin'  */
    ok &= (hdnode_private_ckd_prime(node, 0)     == 1); /* m/44'/c'/0'  */
    ok &= (hdnode_private_ckd(node, change)      == 1); /* m/44'/c'/0'/chg */
    ok &= (hdnode_private_ckd(node, index)       == 1); /* m/44'/c'/0'/c/i */

    if (ok) {
        /* Calcular clave pública comprimida */
        hdnode_fill_public_key(node);
    }
    return ok;
}

/**
 * _node_to_addresses — Calcular direcciones desde un nodo HD
 *
 * Calcula tanto la dirección Legacy (P2PKH) como la SegWit (P2WPKH)
 * desde la clave pública del nodo.
 *
 * @param node      Nodo HD con clave pública calculada
 * @param coin      Coin type para determinar la red
 * @param addr_out  Struct donde se guardan las direcciones
 */
static void _node_to_addresses(const HDNode *node, uint8_t coin,
                                wallet_address_t *addr_out) {
    /* Versión de red: 0x00 = mainnet, 0x6F = testnet */
    uint8_t version = (coin == WALLET_COIN_TESTNET) ? 0x6F : 0x00;

    /* Dirección Legacy P2PKH (Base58Check) */
    ecdsa_get_address(node->public_key, version,
                      HASHER_SHA2_RIPEMD,
                      HASHER_SHA2D,
                      addr_out->address_legacy,
                      sizeof(addr_out->address_legacy));

    /* Dirección SegWit P2WPKH (Bech32) */
    /* Necesitamos el HASH160 de la clave pública */
    uint8_t hash160[20];
    ecdsa_get_pubkeyhash(node->public_key, HASHER_SHA2_RIPEMD, hash160);

    /* HRP: "bc" mainnet, "tb" testnet */
    const char *hrp = (coin == WALLET_COIN_TESTNET) ? "tb" : "bc";
    segwit_addr_encode(addr_out->address_bech32, hrp, 0,
                       hash160, sizeof(hash160));

    /* Copiar clave pública comprimida */
    memcpy(addr_out->public_key, node->public_key, 33);
}


/* =========================================================
 * API PÚBLICA
 * ========================================================= */

wallet_result_t wallet_generate_seed(uint8_t num_words, const char *pin,
                                      char *mnemonic_out, uint32_t buf_len) {
    if (!pin || !mnemonic_out || buf_len < WALLET_MNEMONIC_MAX_LEN) {
        return WALLET_ERR_PARAM;
    }
    if (num_words != 12 && num_words != 24) {
        return WALLET_ERR_PARAM;
    }

    DEBUG_PRINTF("[WALLET] Generando semilla BIP39 de %d palabras...\r\n",
        num_words);

    /* Número de bits de entropía:
     *   12 palabras = 128 bits = 16 bytes de entropía
     *   24 palabras = 256 bits = 32 bytes de entropía */
    uint8_t entropy_len = (num_words == 12) ? 16 : 32;
    uint8_t entropy[32];

    /* Generar entropía con el hardware RNG del STM32F405 */
    if (!rng_fill_buffer(entropy, entropy_len)) {
        DEBUG_PRINT("[WALLET] ERROR: Fallo del hardware RNG.\r\n");
        memset(entropy, 0, sizeof(entropy));
        return WALLET_ERR_CRYPTO;
    }

    /* Convertir entropía a mnemónico BIP39
     * mnemonic_from_data() de Trezor añade el checksum automáticamente */
    const char *mnemonic = mnemonic_from_data(entropy, entropy_len);
    memset(entropy, 0, sizeof(entropy));  /* Limpiar entropía inmediatamente */

    if (!mnemonic) {
        DEBUG_PRINT("[WALLET] ERROR: mnemonic_from_data() falló.\r\n");
        return WALLET_ERR_CRYPTO;
    }

    /* Verificar el checksum del mnemónico generado */
    if (!mnemonic_check(mnemonic)) {
        DEBUG_PRINT("[WALLET] ERROR: Checksum BIP39 inválido.\r\n");
        return WALLET_ERR_CRYPTO;
    }

    /* Copiar al buffer de salida para mostrar al usuario */
    strncpy(mnemonic_out, mnemonic, buf_len - 1);
    mnemonic_out[buf_len - 1] = '\0';

    DEBUG_PRINTF("[WALLET] Mnemónico generado (%zu chars).\r\n",
        strlen(mnemonic_out));

    /* Cifrar y almacenar en Flash */
    if (!storage_set_seed(mnemonic, pin)) {
        DEBUG_PRINT("[WALLET] ERROR: No se pudo almacenar la semilla.\r\n");
        memset(mnemonic_out, 0, buf_len);  /* Limpiar si falla el almacenamiento */
        return WALLET_ERR_STORAGE;
    }

    DEBUG_PRINT("[WALLET] Semilla almacenada cifrada en Flash.\r\n");
    return WALLET_OK;
}

wallet_result_t wallet_import_seed(const char *mnemonic, const char *pin) {
    if (!mnemonic || !pin) return WALLET_ERR_PARAM;

    DEBUG_PRINT("[WALLET] Importando semilla BIP39...\r\n");

    /* Verificar checksum BIP39 antes de almacenar */
    if (!mnemonic_check(mnemonic)) {
        DEBUG_PRINT("[WALLET] ERROR: Mnemónico inválido (checksum incorrecto).\r\n");
        return WALLET_ERR_CRYPTO;
    }

    /* Almacenar cifrada */
    if (!storage_set_seed(mnemonic, pin)) {
        return WALLET_ERR_STORAGE;
    }

    DEBUG_PRINT("[WALLET] Semilla importada correctamente.\r\n");
    return WALLET_OK;
}

wallet_result_t wallet_get_address(const char *pin, uint32_t index,
                                    uint8_t coin,
                                    wallet_address_t *addr_out) {
    if (!pin || !addr_out || index >= WALLET_MAX_ADDR_INDEX) {
        return WALLET_ERR_PARAM;
    }
    if (!storage_is_initialized()) {
        return WALLET_ERR_NO_SEED;
    }

    DEBUG_PRINTF("[WALLET] Derivando dirección BIP44 índice %lu...\r\n",
        (unsigned long)index);

    /* Paso 1: Obtener mnemónico descifrado desde Flash */
    char mnemonic[WALLET_MNEMONIC_MAX_LEN];
    if (!storage_get_seed(pin, mnemonic, sizeof(mnemonic))) {
        return WALLET_ERR_WRONG_PIN;
    }

    /* Paso 2: Convertir mnemónico a semilla binaria de 64 bytes
     * PBKDF2-HMAC-SHA512(mnemonic, "mnemonic"+passphrase, 2048 iter) */
    uint8_t seed[WALLET_SEED_LEN];
    /* Passphrase vacía para el wallet estándar.
     * En Fase 6 se puede añadir soporte para passphrase BIP39. */
    mnemonic_to_seed(mnemonic, "", seed, NULL);
    memset(mnemonic, 0, sizeof(mnemonic));  /* Limpiar mnemónico */

    /* Paso 3: Semilla → nodo HD raíz (BIP32 master key) */
    HDNode root_node;
    if (!_seed_to_hdnode(seed, &root_node)) {
        memset(seed, 0, sizeof(seed));
        return WALLET_ERR_CRYPTO;
    }
    memset(seed, 0, sizeof(seed));  /* Limpiar semilla binaria */

    /* Paso 4: Derivar ruta BIP44 */
    HDNode child_node;
    if (!_derive_bip44_node(&root_node, coin, 0, index, &child_node)) {
        memset(&root_node,  0, sizeof(root_node));
        memset(&child_node, 0, sizeof(child_node));
        return WALLET_ERR_CRYPTO;
    }
    memset(&root_node, 0, sizeof(root_node));  /* Limpiar raíz */

    /* Paso 5: Clave pública → dirección */
    _node_to_addresses(&child_node, coin, addr_out);
    addr_out->index     = index;
    addr_out->is_change = false;

    memset(&child_node, 0, sizeof(child_node));  /* Limpiar nodo hijo */

    DEBUG_PRINTF("[WALLET] Dirección Legacy : %s\r\n", addr_out->address_legacy);
    DEBUG_PRINTF("[WALLET] Dirección SegWit : %s\r\n", addr_out->address_bech32);

    return WALLET_OK;
}

wallet_result_t wallet_sign_tx(const char *pin,
                                const uint8_t *tx_hash,
                                uint32_t key_index, uint8_t coin,
                                uint8_t *sig_out, uint8_t *pby_out) {
    if (!pin || !tx_hash || !sig_out) return WALLET_ERR_PARAM;
    if (!storage_is_initialized()) return WALLET_ERR_NO_SEED;

    DEBUG_PRINTF("[WALLET] Firmando TX con clave índice %lu...\r\n",
        (unsigned long)key_index);

    /* Obtener semilla descifrada */
    char mnemonic[WALLET_MNEMONIC_MAX_LEN];
    if (!storage_get_seed(pin, mnemonic, sizeof(mnemonic))) {
        return WALLET_ERR_WRONG_PIN;
    }

    /* Convertir a semilla binaria */
    uint8_t seed[WALLET_SEED_LEN];
    mnemonic_to_seed(mnemonic, "", seed, NULL);
    memset(mnemonic, 0, sizeof(mnemonic));

    /* Derivar clave privada en la ruta BIP44 */
    HDNode root_node, child_node;
    if (!_seed_to_hdnode(seed, &root_node)) {
        memset(seed, 0, sizeof(seed));
        return WALLET_ERR_CRYPTO;
    }
    memset(seed, 0, sizeof(seed));

    if (!_derive_bip44_node(&root_node, coin, 0, key_index, &child_node)) {
        memset(&root_node,  0, sizeof(root_node));
        return WALLET_ERR_CRYPTO;
    }
    memset(&root_node, 0, sizeof(root_node));

    /* Firmar el hash con ECDSA secp256k1 */
    uint8_t pby = 0;
    int ret = ecdsa_sign_digest(
        &secp256k1,
        child_node.private_key,  /* Clave privada derivada */
        tx_hash,                  /* Hash de 32 bytes de la TX */
        sig_out,                  /* Firma resultante (64 bytes: r||s) */
        &pby,                     /* Byte de paridad */
        NULL
    );

    if (pby_out) *pby_out = pby;

    /* Limpiar clave privada INMEDIATAMENTE después de firmar */
    memset(&child_node, 0, sizeof(child_node));

    if (ret != 0) {
        DEBUG_PRINT("[WALLET] ERROR: ecdsa_sign_digest() falló.\r\n");
        return WALLET_ERR_CRYPTO;
    }

    DEBUG_PRINT("[WALLET] TX firmada correctamente.\r\n");
    return WALLET_OK;
}

bool wallet_is_initialized(void) {
    return storage_is_initialized();
}

void wallet_get_info(char *label_out, uint32_t label_len) {
    if (label_out && label_len > 0) {
        storage_get_label(label_out);
    }
}

wallet_result_t wallet_wipe(const char *pin) {
    if (!pin) return WALLET_ERR_PARAM;

    /* Verificar PIN antes de borrar */
    if (!storage_verify_pin(pin)) {
        return WALLET_ERR_WRONG_PIN;
    }

    DEBUG_PRINT("[WALLET] Factory reset autorizado. Borrando...\r\n");
    if (!storage_wipe()) {
        return WALLET_ERR_STORAGE;
    }
    return WALLET_OK;
}

const char *wallet_result_str(wallet_result_t result) {
    switch (result) {
        case WALLET_OK:            return "OK";
        case WALLET_ERR_NO_SEED:   return "Sin semilla";
        case WALLET_ERR_WRONG_PIN: return "PIN incorrecto";
        case WALLET_ERR_CRYPTO:    return "Error crypto";
        case WALLET_ERR_STORAGE:   return "Error Flash";
        case WALLET_ERR_PARAM:     return "Parámetro inválido";
        default:                   return "Error desconocido";
    }
}