/**
 * @file    tr01_pin.h
 * @brief   PIN verification with TROPIC01 MAC-and-Destroy.
 *
 * Replaces the v0.2 PIN (PBKDF2 hash in the STM32 flash). With MAC-and-Destroy the STM32 holds
 * nothing that lets an attacker test PINs offline: every attempt consumes one slot inside TROPIC01,
 * and after CIWI_PIN_ATTEMPTS wrong PINs the secret is gone for good.
 *
 * A correct PIN releases a 32-byte key (pin_key) that the wallet uses to encrypt the seed.
 * Scheme: Tropic Square application note ODN_TR01_app_002 (PIN verification), as in the libtropic
 * "mac_and_destroy" example, with the slot re-arming fixed to cover every used slot.
 *
 * Requires an open CiWi session (ciwi_tr01_start() == LT_OK).
 */
#ifndef TR01_PIN_H
#define TR01_PIN_H

#include <stdint.h>

#include "tr01_ciwi.h"

/** Wrong PINs allowed before the secret is destroyed. Max 12 (R-memory slot size). */
#define CIWI_PIN_ATTEMPTS 10
#define CIWI_PIN_LEN_MIN  4
#define CIWI_PIN_LEN_MAX  8
/** TROPIC01 user-data slot that holds the PIN state (ciphertexts, tag, attempts left). */
#define CIWI_PIN_RMEM_SLOT 511
/** First MAC-and-Destroy slot used; slots FIRST .. FIRST+ATTEMPTS-1 are used. */
#define CIWI_PIN_MACANDD_FIRST 0

#define CIWI_PIN_KEY_LEN 32

/**
 * Set a new PIN (also used to change it). Generates a fresh secret, so the previous pin_key is
 * no longer valid: re-encrypt the seed with the new pin_key.
 *
 * @param bind     Extra data mixed into every key (the STM32 unique ID): the PIN state only
 *                 works with this MCU and this TROPIC01.
 */
lt_ret_t ciwi_pin_set(ciwi_tr01_t *t, const char *pin, const uint8_t *bind, uint8_t bind_len,
                      uint8_t pin_key[CIWI_PIN_KEY_LEN]);

/**
 * Check a PIN. LT_OK and pin_key on success; on a wrong PIN, LT_FAIL and one attempt is consumed.
 * The attempt is recorded in TROPIC01 BEFORE the check, so cutting the power does not save it.
 */
lt_ret_t ciwi_pin_check(ciwi_tr01_t *t, const char *pin, const uint8_t *bind, uint8_t bind_len,
                        uint8_t pin_key[CIWI_PIN_KEY_LEN]);

/** Attempts left (0 = locked, the secret is destroyed). */
lt_ret_t ciwi_pin_attempts_left(ciwi_tr01_t *t, uint8_t *left);

#endif /* TR01_PIN_H */
