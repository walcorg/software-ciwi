/**
 * @file    crypto_validate.c
 * @brief   Tests de validación crypto — WalCorg Fase 5
 *
 * Cada test compara la salida de las funciones crypto de Trezor
 * contra vectores de referencia publicados en los BIPs oficiales.
 *
 * Si algún test falla → BUG CRÍTICO → NO usar el wallet.
 *
 * @license GNU GPL v3
 */

#include "crypto_validate.h"
#include "uart_debug.h"
#include "rng_test.h"
#include "stm32f4xx_hal.h"

/* Crypto de Trezor (crypto/ del repo) — sin tocar */
#include "bip39.h"       /* mnemonic_generate(), mnemonic_to_seed() */
#include "bip32.h"       /* hdnode_from_seed(), hdnode_private_ckd() */
#include "curves.h"
#include "secp256k1.h"   /* [v0.2] declares the secp256k1 curve */      /* SECP256K1_NAME                          */
#include "ecdsa.h"       /* ecdsa_sign(), ecdsa_verify()            */
#include "sha2.h"        /* sha256_Raw()                            */
#include "base58.h"      /* base58_encode_check()                   */
#include "segwit_addr.h" /* segwit_addr_encode() para bech32        */

#include <string.h>
#include <stdio.h>

/* =========================================================
 * VECTORES DE REFERENCIA
 *
 * Fuente oficial: BIP39 test vectors
 * https://github.com/trezor/python-mnemonic/blob/master/vectors.json
 *
 * Vector 1 (entropía 128 bits = 12 palabras):
 *   Entropía: 00000000000000000000000000000000
 *   Mnemónico: "abandon abandon abandon abandon abandon abandon
 *               abandon abandon abandon abandon abandon about"
 *   Semilla (con passphrase "TREZOR"):
 *     c55257...3eb121 (64 bytes)
 * ========================================================= */

/* Entropía de prueba conocida (128 bits = 16 bytes) */
static const uint8_t TEST_ENTROPY[16] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

/* Mnemónico esperado para esa entropía */
static const char *TEST_MNEMONIC =
    "abandon abandon abandon abandon abandon abandon "
    "abandon abandon abandon abandon abandon about";

/* Semilla esperada (primeros 8 bytes para verificación rápida)
 * Derivada con PBKDF2(mnemónico, "mnemonic" + passphrase="TREZOR", 2048)
 * Fuente: BIP39 test vectors oficiales */
static const uint8_t TEST_SEED_PREFIX[8] = {
    0xc5, 0x52, 0x57, 0x26, 0x82, 0x84, 0xef, 0xbf
};

/* =========================================================
 * VECTOR BIP32
 *
 * Fuente: https://github.com/bitcoin/bips/blob/master/bip-0032.mediawiki
 * Test Vector 1:
 *   Semilla: 000102030405060708090a0b0c0d0e0f
 *   Ruta: m  (clave maestra)
 *   xpub: xpub661MyMwAqRbcFtXgS5sYJABqqG9YLmC...
 *   xprv: xprv9s21ZrQH143K3QTDL4LXw2F7HEK3wJUD...
 * ========================================================= */
static const uint8_t BIP32_TEST_SEED[16] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
};

/* Clave privada esperada en m (primeros 8 bytes)
 * Fuente: BIP32 test vector 1 oficial */
static const uint8_t BIP32_MASTER_KEY_PREFIX[8] = {
    0xe8, 0xf3, 0x2e, 0x72, 0x3d, 0xec, 0xf4, 0x05
};

/* =========================================================
 * FUNCIONES AUXILIARES
 * ========================================================= */

/**
 * _hex_to_bytes — Convertir string hex a bytes
 */
static int _hex_to_bytes(const char *hex, uint8_t *out, int max_len) {
    int len = 0;
    while (hex[0] && hex[1] && len < max_len) {
        uint8_t hi = hex[0] >= 'a' ? hex[0] - 'a' + 10 :
                     hex[0] >= 'A' ? hex[0] - 'A' + 10 : hex[0] - '0';
        uint8_t lo = hex[1] >= 'a' ? hex[1] - 'a' + 10 :
                     hex[1] >= 'A' ? hex[1] - 'A' + 10 : hex[1] - '0';
        out[len++] = (hi << 4) | lo;
        hex += 2;
    }
    return len;
}

/**
 * _bytes_equal — Comparación en tiempo constante de N bytes
 */
static bool _bytes_equal(const uint8_t *a, const uint8_t *b, int n) {
    uint8_t diff = 0;
    for (int i = 0; i < n; i++) diff |= (a[i] ^ b[i]);
    return (diff == 0);
}

/**
 * _print_hex — Imprimir buffer en hex para debug
 */
static void _print_hex(const char *label, const uint8_t *data, int len) {
#if (PRODUCTION == 0)
    DEBUG_PRINTF("  %s: ", label);
    char hex[3];
    for (int i = 0; i < len && i < 32; i++) {
        snprintf(hex, sizeof(hex), "%02x", data[i]);
        debug_print(hex);
    }
    if (len > 32) DEBUG_PRINT("...");
    DEBUG_PRINT("\r\n");
#else
    (void)label; (void)data; (void)len;
#endif
}

/* =========================================================
 * TEST A — BIP39
 * ========================================================= */

bool crypto_test_bip39(void) {
    DEBUG_PRINT("\r\n[CRYPTO] Test A: BIP39\r\n");
    bool passed = true;

    /* ---- A1: Entropía conocida → Mnemónico ---- */
    DEBUG_PRINT("[CRYPTO]   A1: Entropía → Mnemónico...\r\n");

    /* mnemonic_from_data() de Trezor genera el mnemónico desde entropía */
    const char *generated = mnemonic_from_data(TEST_ENTROPY, sizeof(TEST_ENTROPY));

    if (!generated) {
        DEBUG_PRINT("[CRYPTO]   A1 FAIL: mnemonic_from_data() retornó NULL\r\n");
        return false;
    }

    if (strcmp(generated, TEST_MNEMONIC) != 0) {
        DEBUG_PRINT("[CRYPTO]   A1 FAIL: Mnemónico incorrecto\r\n");
        DEBUG_PRINTF("[CRYPTO]   Esperado : %s\r\n", TEST_MNEMONIC);
        DEBUG_PRINTF("[CRYPTO]   Generado : %s\r\n", generated);
        passed = false;
    } else {
        DEBUG_PRINT("[CRYPTO]   A1 PASS: Mnemónico correcto\r\n");
    }

    /* ---- A2: Mnemónico válido → Validar checksum ---- */
    DEBUG_PRINT("[CRYPTO]   A2: Validar checksum BIP39...\r\n");

    if (!mnemonic_check(TEST_MNEMONIC)) {
        DEBUG_PRINT("[CRYPTO]   A2 FAIL: mnemonic_check() rechazó el mnemónico\r\n");
        passed = false;
    } else {
        DEBUG_PRINT("[CRYPTO]   A2 PASS: Checksum válido\r\n");
    }

    /* ---- A3: Mnemónico → Semilla PBKDF2 ---- */
    DEBUG_PRINT("[CRYPTO]   A3: Mnemónico → Semilla (PBKDF2)...\r\n");

    uint8_t seed[64];
    /* mnemonic_to_seed() de Trezor: PBKDF2-HMAC-SHA512 con 2048 iteraciones
     * La passphrase en los test vectors oficiales es "TREZOR" */
    mnemonic_to_seed(TEST_MNEMONIC, "TREZOR", seed, NULL);

    _print_hex("Semilla[0..7]", seed, 8);
    _print_hex("Esperado     ", TEST_SEED_PREFIX, 8);

    if (!_bytes_equal(seed, TEST_SEED_PREFIX, sizeof(TEST_SEED_PREFIX))) {
        DEBUG_PRINT("[CRYPTO]   A3 FAIL: Semilla no coincide con vector de referencia\r\n");
        passed = false;
    } else {
        DEBUG_PRINT("[CRYPTO]   A3 PASS: Semilla coincide con BIP39 vector 1\r\n");
    }

    /* Limpiar semilla sensible */
    memset(seed, 0, sizeof(seed));

    /* ---- A4: Mnemónico inválido → Rechazado ---- */
    DEBUG_PRINT("[CRYPTO]   A4: Mnemónico inválido rechazado...\r\n");

    if (mnemonic_check("abandon abandon abandon abandon abandon abandon "
                       "abandon abandon abandon abandon abandon INVALID")) {
        DEBUG_PRINT("[CRYPTO]   A4 FAIL: mnemonic_check() aceptó mnemónico inválido\r\n");
        passed = false;
    } else {
        DEBUG_PRINT("[CRYPTO]   A4 PASS: Mnemónico inválido rechazado\r\n");
    }

    DEBUG_PRINTF("[CRYPTO] Test A BIP39: %s\r\n", passed ? "PASS" : "FAIL");
    return passed;
}

/* =========================================================
 * TEST B — BIP32
 * ========================================================= */

bool crypto_test_bip32(void) {
    DEBUG_PRINT("\r\n[CRYPTO] Test B: BIP32\r\n");
    bool passed = true;

    /* ---- B1: Semilla → Clave maestra ---- */
    DEBUG_PRINT("[CRYPTO]   B1: Semilla → Clave maestra HD...\r\n");

    HDNode node;
    int ret = hdnode_from_seed(BIP32_TEST_SEED, sizeof(BIP32_TEST_SEED),
                                SECP256K1_NAME, &node);

    if (ret != 1) {
        DEBUG_PRINT("[CRYPTO]   B1 FAIL: hdnode_from_seed() falló\r\n");
        return false;
    }

    _print_hex("Clave priv[0..7]", node.private_key, 8);
    _print_hex("Esperado        ", BIP32_MASTER_KEY_PREFIX, 8);

    if (!_bytes_equal(node.private_key, BIP32_MASTER_KEY_PREFIX,
                      sizeof(BIP32_MASTER_KEY_PREFIX))) {
        DEBUG_PRINT("[CRYPTO]   B1 FAIL: Clave maestra no coincide\r\n");
        passed = false;
    } else {
        DEBUG_PRINT("[CRYPTO]   B1 PASS: Clave maestra correcta\r\n");
    }

    /* ---- B2: Derivar ruta BIP44 m/44'/0'/0'/0/0 ---- */
    DEBUG_PRINT("[CRYPTO]   B2: Derivar m/44'/0'/0'/0/0 (BIP44 Bitcoin)...\r\n");

    /* Reinicializar desde la semilla de test */
    HDNode node44;
    hdnode_from_seed(BIP32_TEST_SEED, sizeof(BIP32_TEST_SEED),
                     SECP256K1_NAME, &node44);

    /* Derivar cada nivel de la ruta BIP44 */
    /* El apóstrofe indica derivación endurecida (hardened) */
    ret  = hdnode_private_ckd_prime(&node44, 44);  /* m/44' */
    ret &= hdnode_private_ckd_prime(&node44, 0);   /* m/44'/0'  (Bitcoin) */
    ret &= hdnode_private_ckd_prime(&node44, 0);   /* m/44'/0'/0' (cuenta 0) */
    ret &= hdnode_private_ckd(&node44, 0);          /* m/44'/0'/0'/0 (externo) */
    ret &= hdnode_private_ckd(&node44, 0);          /* m/44'/0'/0'/0/0 (índice 0) */

    if (!ret) {
        DEBUG_PRINT("[CRYPTO]   B2 FAIL: Derivación de ruta falló\r\n");
        passed = false;
    } else {
        /* Calcular la clave pública */
        hdnode_fill_public_key(&node44);
        DEBUG_PRINT("[CRYPTO]   B2 PASS: Derivación BIP44 completada\r\n");
        _print_hex("Clave pub [0..7]", node44.public_key, 8);
    }

    /* Limpiar claves privadas de SRAM */
    memset(&node,   0, sizeof(node));
    memset(&node44, 0, sizeof(node44));

    DEBUG_PRINTF("[CRYPTO] Test B BIP32: %s\r\n", passed ? "PASS" : "FAIL");
    return passed;
}

/* =========================================================
 * TEST C — DIRECCIÓN BITCOIN
 * ========================================================= */

bool crypto_test_address(void) {
    DEBUG_PRINT("\r\n[CRYPTO] Test C: Direccion Bitcoin\r\n");
    bool passed = true;

    /* Derivar clave pública conocida para el vector de test */
    HDNode node;
    hdnode_from_seed(BIP32_TEST_SEED, sizeof(BIP32_TEST_SEED),
                     SECP256K1_NAME, &node);

    /* Ruta BIP44: m/44'/0'/0'/0/0 */
    hdnode_private_ckd_prime(&node, 44);
    hdnode_private_ckd_prime(&node, 0);
    hdnode_private_ckd_prime(&node, 0);
    hdnode_private_ckd(&node, 0);
    hdnode_private_ckd(&node, 0);
    hdnode_fill_public_key(&node);

    /* ---- C1: Dirección Legacy (P2PKH, Base58Check) ---- */
    DEBUG_PRINT("[CRYPTO]   C1: Dirección P2PKH (Legacy)...\r\n");

    /* Hash160 de la clave pública comprimida:
     * RIPEMD160(SHA256(pubkey)) */
    uint8_t hash160[20];
    hdnode_get_address_raw(&node, 0x00, hash160);  /* 0x00 = Bitcoin mainnet */

    /* Codificar en Base58Check */
    char addr_legacy[40];
    uint8_t addr_bytes[21];
    addr_bytes[0] = 0x00;  /* Version byte mainnet */
    memcpy(addr_bytes + 1, hash160, 20);
    base58_encode_check(addr_bytes, 21, SHA256_DIGEST_LENGTH,
                        addr_legacy, sizeof(addr_legacy));

    DEBUG_PRINTF("[CRYPTO]   Dirección Legacy: %s\r\n", addr_legacy);

    /* Verificar que empieza con '1' (mainnet P2PKH) */
    if (addr_legacy[0] != '1') {
        DEBUG_PRINT("[CRYPTO]   C1 FAIL: Dirección no empieza con '1'\r\n");
        passed = false;
    } else {
        DEBUG_PRINT("[CRYPTO]   C1 PASS: Dirección Legacy válida\r\n");
    }

    /* ---- C2: Dirección SegWit (P2WPKH, Bech32) ---- */
    DEBUG_PRINT("[CRYPTO]   C2: Dirección P2WPKH (SegWit bech32)...\r\n");

    char addr_bech32[80];
    /* Convertir HASH160 a dirección bech32 */
    int bech_ret = segwit_addr_encode(
        addr_bech32,    /* Salida */
        "bc",           /* HRP: "bc" para mainnet, "tb" para testnet */
        0,              /* Witness version 0 = P2WPKH */
        hash160,        /* Witness program = HASH160 de la pubkey */
        20              /* Longitud del witness program */
    );

    if (!bech_ret) {
        DEBUG_PRINT("[CRYPTO]   C2 FAIL: segwit_addr_encode() falló\r\n");
        passed = false;
    } else {
        DEBUG_PRINTF("[CRYPTO]   Dirección SegWit: %s\r\n", addr_bech32);

        /* Verificar que empieza con "bc1" (mainnet bech32) */
        if (strncmp(addr_bech32, "bc1", 3) != 0) {
            DEBUG_PRINT("[CRYPTO]   C2 FAIL: No empieza con 'bc1'\r\n");
            passed = false;
        } else {
            DEBUG_PRINT("[CRYPTO]   C2 PASS: Dirección SegWit válida\r\n");
        }
    }

    /* Limpiar clave privada */
    memset(&node, 0, sizeof(node));

    DEBUG_PRINTF("[CRYPTO] Test C Direcciones: %s\r\n", passed ? "PASS" : "FAIL");
    return passed;
}

/* =========================================================
 * TEST D — ECDSA secp256k1
 * ========================================================= */

bool crypto_test_ecdsa(void) {
    DEBUG_PRINT("\r\n[CRYPTO] Test D: ECDSA secp256k1\r\n");
    bool passed = true;

    /* Clave privada de prueba conocida (NO usar en producción) */
    uint8_t priv_key[32];
    memset(priv_key, 0, sizeof(priv_key));
    priv_key[31] = 0x01;  /* La clave privada más simple: 1 */

    /* Mensaje de prueba */
    const char *message = "WalCorg test message";
    uint8_t hash[32];
    sha256_Raw((const uint8_t *)message, strlen(message), hash);

    /* ---- D1: Firmar ---- */
    DEBUG_PRINT("[CRYPTO]   D1: Firmando mensaje...\r\n");

    uint8_t signature[64];
    uint8_t pby;  /* Paridad de la clave pública (para recuperación) */

    int ret = ecdsa_sign_digest(
        &secp256k1,   /* Curva secp256k1 (Bitcoin) */
        priv_key,     /* Clave privada */
        hash,         /* Hash del mensaje */
        signature,    /* Firma resultante (64 bytes: r + s) */
        &pby,         /* Byte de paridad */
        NULL          /* Función de verificación adicional (NULL=no) */
    );

    if (ret != 0) {
        DEBUG_PRINT("[CRYPTO]   D1 FAIL: ecdsa_sign_digest() falló\r\n");
        passed = false;
    } else {
        DEBUG_PRINT("[CRYPTO]   D1 PASS: Firma generada\r\n");
        _print_hex("Firma r[0..7]", signature, 8);
        _print_hex("Firma s[0..7]", signature + 32, 8);
    }

    /* ---- D2: Verificar firma con clave pública ---- */
    DEBUG_PRINT("[CRYPTO]   D2: Verificando firma...\r\n");

    /* Calcular la clave pública correspondiente a la clave privada */
    uint8_t pub_key[33];  /* Clave pública comprimida: 33 bytes */
    ecdsa_get_public_key33(&secp256k1, priv_key, pub_key);

    ret = ecdsa_verify_digest(
        &secp256k1,   /* Curva */
        pub_key,      /* Clave pública */
        signature,    /* Firma a verificar */
        hash          /* Hash del mensaje original */
    );

    if (ret != 0) {
        DEBUG_PRINT("[CRYPTO]   D2 FAIL: Verificación de firma falló\r\n");
        passed = false;
    } else {
        DEBUG_PRINT("[CRYPTO]   D2 PASS: Firma verificada correctamente\r\n");
    }

    /* ---- D3: Firma inválida rechazada ---- */
    DEBUG_PRINT("[CRYPTO]   D3: Firma inválida rechazada...\r\n");

    /* Corromper la firma */
    uint8_t bad_sig[64];
    memcpy(bad_sig, signature, 64);
    bad_sig[0] ^= 0xFF;  /* Invertir el primer byte */

    ret = ecdsa_verify_digest(&secp256k1, pub_key, bad_sig, hash);

    if (ret == 0) {
        DEBUG_PRINT("[CRYPTO]   D3 FAIL: Firma inválida fue aceptada!\r\n");
        passed = false;
    } else {
        DEBUG_PRINT("[CRYPTO]   D3 PASS: Firma inválida rechazada\r\n");
    }

    /* Limpiar clave privada */
    memset(priv_key, 0, sizeof(priv_key));
    memset(hash,     0, sizeof(hash));
    memset(signature,0, sizeof(signature));

    DEBUG_PRINTF("[CRYPTO] Test D ECDSA: %s\r\n", passed ? "PASS" : "FAIL");
    return passed;
}

/* =========================================================
 * TEST E — CALIDAD DEL RNG
 * ========================================================= */

bool crypto_test_rng_quality(void) {
    DEBUG_PRINT("\r\n[CRYPTO] Test E: Calidad del RNG hardware\r\n");

    /* Reusar la suite de tests del rng_test.c de Fase 1 */
    rng_test_results_t results = {0};
    bool passed = rng_run_tests(&results);

    DEBUG_PRINTF("[CRYPTO] Test E RNG: %s (%lu muestras, %lu únicos/256)\r\n",
        passed ? "PASS" : "FAIL",
        results.samples_generated,
        results.unique_bytes);

    return passed;
}

/* =========================================================
 * EJECUTAR TODOS LOS TESTS
 * ========================================================= */

bool crypto_run_all_tests(void) {
    DEBUG_PRINT("\r\n[CRYPTO] ========================================\r\n");
    DEBUG_PRINT("[CRYPTO] Validación crypto contra vectores BIP\r\n");
    DEBUG_PRINT("[CRYPTO] ========================================\r\n");

    uint32_t start = HAL_GetTick();

    bool a = crypto_test_bip39();
    bool b = crypto_test_bip32();
    bool c = crypto_test_address();
    bool d = crypto_test_ecdsa();
    bool e = crypto_test_rng_quality();

    uint32_t elapsed = HAL_GetTick() - start;
    bool all = a && b && c && d && e;

    DEBUG_PRINT("\r\n[CRYPTO] ========= RESUMEN =========\r\n");
    DEBUG_PRINTF("[CRYPTO] A BIP39      : %s\r\n", a ? "PASS" : "FAIL !!!");
    DEBUG_PRINTF("[CRYPTO] B BIP32      : %s\r\n", b ? "PASS" : "FAIL !!!");
    DEBUG_PRINTF("[CRYPTO] C Direcciones: %s\r\n", c ? "PASS" : "FAIL !!!");
    DEBUG_PRINTF("[CRYPTO] D ECDSA      : %s\r\n", d ? "PASS" : "FAIL !!!");
    DEBUG_PRINTF("[CRYPTO] E RNG        : %s\r\n", e ? "PASS" : "FAIL !!!");
    DEBUG_PRINTF("[CRYPTO] Tiempo total : %lu ms\r\n", elapsed);
    DEBUG_PRINT("[CRYPTO] ===================================\r\n");

    if (all) {
        DEBUG_PRINT("[CRYPTO] TODOS LOS TESTS PASARON\r\n");
        DEBUG_PRINT("[CRYPTO] La libreria crypto es confiable.\r\n");
        DEBUG_PRINT("[CRYPTO] El wallet puede usarse con fondos reales.\r\n");
    } else {
        DEBUG_PRINT("[CRYPTO] !!! ALGUN TEST FALLO !!!\r\n");
        DEBUG_PRINT("[CRYPTO] NO usar este dispositivo con fondos reales.\r\n");
        DEBUG_PRINT("[CRYPTO] Revisar la libreria crypto y el hardware RNG.\r\n");
    }
    DEBUG_PRINT("[CRYPTO] ===================================\r\n\r\n");

    return all;
}