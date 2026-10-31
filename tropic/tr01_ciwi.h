/**
 * @file    tr01_ciwi.h
 * @brief   TROPIC01 secure element, as used by the CiWi.
 *
 * This is the only module that talks to TROPIC01. It wraps libtropic (Tropic Square) and fixes the
 * CiWi's use of the chip:
 *
 *   Resource             Index      Use
 *   -------------------  ---------  -----------------------------------------------------------
 *   Pairing key slot 0   SH0        Factory key (public in libtropic). Used once, at the bench.
 *   Pairing key slot 1   SH1        "Signing" key. Only this session may use the anchor key, the
 *                                   claim counter and rewrite the access policy. Its private
 *                                   half is stored ENCRYPTED with a key released by the PIN.
 *   Pairing key slot 2   SH2        "Boot" key, stored in plain. Its session can only check the
 *                                   PIN (MAC-and-Destroy) and read public data.
 *   ECC key slot 0       anchor     P-256 key generated inside the chip; signs Claim, Bind and
 *                                   NftClaim. Never leaves the chip.
 *   Monotonic counter 0  claims     ciwiCounter of the Claim messages.
 *   MAC-and-Destroy 0-9  PIN        10 attempts (tr01_pin.h).
 *   User data slot 511   PIN state
 *
 * Boot sequence: ciwi_tr01_open() -> ciwi_tr01_start() (SH2 session, state LOCKED) ->
 * ciwi_tr01_unlock(PIN) (PIN checked in TROPIC01, SH1 decrypted, SH1 session, state READY).
 * Without the PIN, the STM32 holds nothing that can sign. Why: see tr01_ciwi.c, "Threat model".
 *
 * The code is portable: it only needs tr01_platform.h, so the same file runs on the CiWi and on the
 * host test bench against the TROPIC01 model.
 */
#ifndef TR01_CIWI_H
#define TR01_CIWI_H

#include <stdbool.h>
#include <stdint.h>

#include "libtropic.h"
#include "libtropic_common.h"
#include "libtropic_trezor_crypto.h"

/* ------------------------------------------------------------------ configuration */

/** 1 = engineering-sample chip (different factory SH0 keys); 0 = production chip. */
#ifndef CIWI_TR01_SH0_ENG_SAMPLE
#define CIWI_TR01_SH0_ENG_SAMPLE 0
#endif

/**
 * 1 = during provisioning, write the CiWi access policy to the R-config. The PIN protection
 * depends on it: without the policy every session may sign. After it, only the SH1 session (which
 * needs the PIN) can change the policy again. Default 1 since v0.5.
 */
#ifndef CIWI_TR01_APPLY_RCONFIG
#define CIWI_TR01_APPLY_RCONFIG 1
#endif

/**
 * 1 = after provisioning, invalidate the factory pairing key SH0.
 * IRREVERSIBLE. With the v0.5 policy SH0 can no longer sign or change the policy, so this is not
 * required; it removes the last factory access. Never enable on a development board.
 */
#ifndef CIWI_TR01_INVALIDATE_SH0
#define CIWI_TR01_INVALIDATE_SH0 0
#endif

#define CIWI_TR01_SIGN_SLOT      TR01_PAIRING_KEY_SLOT_INDEX_1
#define CIWI_TR01_BOOT_SLOT      TR01_PAIRING_KEY_SLOT_INDEX_2
#define CIWI_TR01_ANCHOR_SLOT    TR01_ECC_SLOT_0
#define CIWI_TR01_CLAIM_COUNTER  TR01_MCOUNTER_INDEX_0

/** Anchor Protocol curve identifier for P-256. */
#define CIWI_CURVE_P256 1

/* ------------------------------------------------------------------ types */

typedef enum {
    CIWI_TR01_STATE_UNKNOWN = 0,
    CIWI_TR01_STATE_NEW,       /**< Not provisioned: no boot key in this STM32. */
    CIWI_TR01_STATE_LOCKED,    /**< Boot session (SH2) open: the PIN is needed to sign. */
    CIWI_TR01_STATE_READY,     /**< Signing session (SH1) open: anchor key usable. */
    CIWI_TR01_STATE_FOREIGN,   /**< This STM32 has keys, but the chip rejects them. */
} ciwi_tr01_state_t;

typedef struct {
    lt_handle_t lt;                 /**< libtropic handle */
    lt_ctx_trezor_crypto_t crypto;  /**< crypto backend context (trezor-crypto) */
    bool session_open;
    lt_pkey_index_t session_slot;
    ciwi_tr01_state_t state;
    uint8_t anchor_pub[64];         /**< P-256 public key, X || Y */
    uint8_t ciwi_id[32];            /**< keccak256(curve || X || Y) */
    bool anchor_ready;
} ciwi_tr01_t;

/* ------------------------------------------------------------------ API */

/** Attach the bus, initialise libtropic and make sure TROPIC01 runs its application firmware. */
lt_ret_t ciwi_tr01_open(ciwi_tr01_t *t);

/** Close any session and release the bus. */
void ciwi_tr01_close(ciwi_tr01_t *t);

/** Print chip ID, silicon revision and firmware versions (no session needed). */
lt_ret_t ciwi_tr01_print_info(ciwi_tr01_t *t);

/** keccak256 of the chip serial number (RegistryApproval.chipSerialHash). */
lt_ret_t ciwi_tr01_chip_serial_hash(ciwi_tr01_t *t, uint8_t out[32]);

/** Open the boot session (SH2). Sets t->state to LOCKED, NEW or FOREIGN. */
lt_ret_t ciwi_tr01_start(ciwi_tr01_t *t);

/**
 * Check the PIN in TROPIC01 (one attempt is consumed before the check), decrypt the SH1 key with
 * the key the PIN releases, open the signing session and load the anchor key.
 * @param bind          extra data bound to the PIN (the STM32 unique ID)
 * @param attempts_left receives the attempts left after this one (may be NULL)
 * @return LT_OK (state READY), LT_FAIL on a wrong PIN, or a libtropic error
 */
lt_ret_t ciwi_tr01_unlock(ciwi_tr01_t *t, const char *pin, const uint8_t *bind, uint8_t bind_len,
                          uint8_t *attempts_left);

/** Close the signing session and return to the boot session (state LOCKED). */
lt_ret_t ciwi_tr01_lock(ciwi_tr01_t *t);

/** Attempts left before the PIN state is destroyed (state LOCKED only). */
lt_ret_t ciwi_tr01_attempts_left(ciwi_tr01_t *t, uint8_t *left);

/**
 * Change the PIN: checks the old one, sets the new one and re-encrypts the SH1 key.
 * Ends in state READY.
 */
lt_ret_t ciwi_tr01_change_pin(ciwi_tr01_t *t, const char *old_pin, const char *new_pin,
                              const uint8_t *bind, uint8_t bind_len);

/**
 * First-time setup at the bench, from state NEW. Needs the owner's first PIN.
 *   1. Session with the factory key SH0.
 *   2. New boot key (slot 2) and signing key (slot 1), each = TROPIC01 TRNG XOR STM32 RNG.
 *   3. CiWi access policy (R-config) and reboot (CIWI_TR01_APPLY_RCONFIG).
 *   4. Boot session: PIN set with MAC-and-Destroy; the SH1 key is stored encrypted.
 *   5. Signing session: P-256 anchor key generated in ECC slot 0; claim counter initialised.
 *   6. Optional, irreversible: invalidate SH0 (CIWI_TR01_INVALIDATE_SH0).
 * Ends in state READY.
 */
lt_ret_t ciwi_tr01_provision(ciwi_tr01_t *t, const char *pin, const uint8_t *bind,
                             uint8_t bind_len);

/** Anchor public key (X || Y) and ciwiId. Available in state READY. */
const uint8_t *ciwi_tr01_anchor_pub(const ciwi_tr01_t *t);
const uint8_t *ciwi_tr01_ciwi_id(const ciwi_tr01_t *t);

/**
 * Sign a 32-byte EIP-712 digest with the anchor key (state READY).
 * The signature is normalised to low-s and verified with the public key before it is returned,
 * so a faulted signature is never released.
 */
lt_ret_t ciwi_tr01_anchor_sign(ciwi_tr01_t *t, const uint8_t digest[32], uint8_t sig_rs[64]);

/** Advance the claim counter in TROPIC01 and return the new ciwiCounter (1, 2, 3...). */
lt_ret_t ciwi_tr01_next_claim_counter(ciwi_tr01_t *t, uint64_t *ciwi_counter);

/** Current ciwiCounter without advancing it (0 = no claim signed yet). */
lt_ret_t ciwi_tr01_claim_counter(ciwi_tr01_t *t, uint64_t *ciwi_counter);

/** Random bytes from TROPIC01's TRNG, mixed (XOR) with the STM32 RNG. */
lt_ret_t ciwi_tr01_random(ciwi_tr01_t *t, uint8_t *buf, uint16_t len);

/** Encrypted round trip, to check the secure channel. */
lt_ret_t ciwi_tr01_ping(ciwi_tr01_t *t);

#endif /* TR01_CIWI_H */
