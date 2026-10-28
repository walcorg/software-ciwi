/**
 * @file    tr01_ciwi.h
 * @brief   TROPIC01 secure element, as used by the CiWi.
 *
 * This is the only module that talks to TROPIC01. It wraps libtropic (Tropic Square) and fixes the
 * CiWi's use of the chip:
 *
 *   Resource            Index      Use
 *   ------------------  ---------  -------------------------------------------------------------
 *   Pairing key slot 0  SH0        Factory key. Used once, at the bench, to provision slot 1.
 *   Pairing key slot 1  SH1        The CiWi's own key. Its private half lives in the STM32 flash.
 *   ECC key slot 0      anchor     P-256 key generated inside TROPIC01. Signs Anchor Protocol
 *                                  messages (Claim, Bind, NftClaim). Never leaves the chip.
 *   Monotonic counter 0 claims     ciwiCounter for Claim messages (strictly increasing).
 *   MAC-and-Destroy 0-11           PIN attempts (see tr01_pin.h).
 *   User data slot 511             PIN state for MAC-and-Destroy (see tr01_pin.h).
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
 * 1 = during provisioning, write the CiWi access policy to the R-config (reversible: the R-config can
 * be erased and rewritten). 0 = keep the chip's current policy. Default 0 for the first bring-up.
 */
#ifndef CIWI_TR01_APPLY_RCONFIG
#define CIWI_TR01_APPLY_RCONFIG 0
#endif

/**
 * 1 = after provisioning, invalidate the factory pairing key SH0.
 * IRREVERSIBLE: after this, only the key stored in this STM32 can open a session. If the STM32
 * flash is erased, the chip can no longer be used. Never enable on a development board.
 */
#ifndef CIWI_TR01_INVALIDATE_SH0
#define CIWI_TR01_INVALIDATE_SH0 0
#endif

#define CIWI_TR01_PAIRING_SLOT   TR01_PAIRING_KEY_SLOT_INDEX_1
#define CIWI_TR01_ANCHOR_SLOT    TR01_ECC_SLOT_0
#define CIWI_TR01_CLAIM_COUNTER  TR01_MCOUNTER_INDEX_0

/** Anchor Protocol curve identifier for P-256. */
#define CIWI_CURVE_P256 1

/* ------------------------------------------------------------------ types */

typedef enum {
    CIWI_TR01_STATE_UNKNOWN = 0,
    CIWI_TR01_STATE_NEW,       /**< No CiWi pairing key stored in this STM32 yet. */
    CIWI_TR01_STATE_READY,     /**< Session with SH1 works. */
    CIWI_TR01_STATE_FOREIGN,   /**< This STM32 has a key, but the chip rejects it (chip swapped?). */
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

/**
 * Open the CiWi session (SH1) with the key stored in this STM32 and load the anchor key.
 * Sets t->state to READY, NEW or FOREIGN.
 */
lt_ret_t ciwi_tr01_start(ciwi_tr01_t *t);

/**
 * First-time setup at the bench. Idempotent: steps already done are skipped.
 *   1. Session with the factory key SH0.
 *   2. New X25519 pairing key (TROPIC01 RNG XOR STM32 RNG); public half written to slot 1,
 *      private half stored in the STM32 flash.
 *   3. Optional: CiWi access policy in the R-config (CIWI_TR01_APPLY_RCONFIG).
 *   4. Session with SH1; P-256 anchor key generated in ECC slot 0; claim counter initialised.
 *   5. Optional, irreversible: invalidate SH0 (CIWI_TR01_INVALIDATE_SH0).
 */
lt_ret_t ciwi_tr01_provision(ciwi_tr01_t *t);

/** Anchor public key (X || Y) and ciwiId. Requires ciwi_tr01_start() == LT_OK. */
const uint8_t *ciwi_tr01_anchor_pub(const ciwi_tr01_t *t);
const uint8_t *ciwi_tr01_ciwi_id(const ciwi_tr01_t *t);

/**
 * Sign a 32-byte EIP-712 digest with the anchor key.
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
