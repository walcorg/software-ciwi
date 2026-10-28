/**
 * @file    crypto_validate.h
 * @brief   Validación crypto contra vectores conocidos — WALCORG Fase 5
 *
 * Verifica que la librería crypto de Trezor (crypto/) produce
 * resultados correctos en el STM32F405RGT6. Tests contra
 * vectores del estándar BIP39, BIP32 y ECDSA secp256k1.
 *
 * IMPORTANCIA:
 *   Un bug en la derivación de claves produciría direcciones
 *   incorrectas y fondos inaccesibles. Estos tests deben pasar
 *   al 100% antes de usar el wallet con fondos reales.
 *
 * VECTORES USADOS:
 *   BIP39: https://github.com/trezor/python-mnemonic/blob/master/vectors.json
 *   BIP32: https://github.com/bitcoin/bips/blob/master/bip-0032.mediawiki
 *   BIP44: m/44'/0'/0'/0/0 para Bitcoin mainnet
 *
 * @license GNU GPL v3
 */

#ifndef CRYPTO_VALIDATE_H
#define CRYPTO_VALIDATE_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Ejecuta todos los tests de validación crypto.
 * @return true si TODOS los tests pasaron (100% requerido)
 */
bool crypto_run_all_tests(void);

/** Test A: BIP39 — entropía → mnemónico → semilla */
bool crypto_test_bip39(void);

/** Test B: BIP32 — semilla → clave maestra → derivación de ruta */
bool crypto_test_bip32(void);

/** Test C: Dirección Bitcoin — clave pública → dirección base58/bech32 */
bool crypto_test_address(void);

/** Test D: ECDSA secp256k1 — firma y verificación */
bool crypto_test_ecdsa(void);

/** Test E: Hardware RNG — calidad de la entropía generada */
bool crypto_test_rng_quality(void);

#endif /* CRYPTO_VALIDATE_H */