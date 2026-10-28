/**
 * @file    anchor_ciwi.h
 * @brief   The CiWi's part of the WALCORG Anchor Protocol v0.2.
 *
 * Two steps per message, so the owner confirms between them:
 *   1. check:  verify the shield's signature and the fields that concern this CiWi;
 *              show the result on screen and wait for the button.
 *   2. sign:   only after the button press; signs with the TROPIC01 anchor key.
 *
 * P-256 signatures are r || s (64 bytes, big-endian), low-s, as P256VERIFY / OpenZeppelin expect.
 */
#ifndef ANCHOR_CIWI_H
#define ANCHOR_CIWI_H

#include <stdbool.h>
#include <stdint.h>

#include "anchor_eip712.h"
#include "tr01_ciwi.h"

typedef enum {
    ANCHOR_OK = 0,
    ANCHOR_ERR_VERSION,       /**< unsupported version or curve */
    ANCHOR_ERR_SHIELD_ID,     /**< shieldId does not match the shield public key */
    ANCHOR_ERR_SHIELD_SIG,    /**< shield signature invalid or high-s */
    ANCHOR_ERR_WRONG_CIWI,    /**< message names a different ciwiId */
    ANCHOR_ERR_WRONG_OWNER,   /**< owner does not match */
    ANCHOR_ERR_WINDOW,        /**< windowEnd <= windowStart */
    ANCHOR_ERR_TR01,          /**< TROPIC01 error (see log) */
} anchor_status_t;

const char *anchor_status_str(anchor_status_t s);

/** P-256 verification with the low-s rule. pub = X || Y. */
bool anchor_p256_verify(const uint8_t pub[64], const uint8_t digest[32], const uint8_t sig[64]);

/* ---------------------------------------------------------------- core: CaptureReceipt -> Claim */

anchor_status_t anchor_check_receipt(const anchor_domain_t *dom,
                                     const anchor_capture_receipt_t *rcpt,
                                     const uint8_t shield_pub[64], const uint8_t shield_sig[64],
                                     const uint8_t owner[20]);

/** Fills claim (receiptHash, ciwiId, owner, next ciwiCounter) and signs it. */
anchor_status_t anchor_sign_claim(ciwi_tr01_t *t, const anchor_domain_t *dom,
                                  const anchor_capture_receipt_t *rcpt, anchor_claim_t *claim,
                                  uint8_t sig[64]);

/* ---------------------------------------------------------------- core: Bind */

anchor_status_t anchor_check_bind(const anchor_domain_t *dom, const ciwi_tr01_t *t,
                                  const anchor_bind_t *bind, const uint8_t shield_pub[64],
                                  const uint8_t shield_sig[64]);

anchor_status_t anchor_sign_bind(ciwi_tr01_t *t, const anchor_domain_t *dom,
                                 const anchor_bind_t *bind, uint8_t sig[64]);

/* ---------------------------------------------------------------- NFT profile */

/**
 * The collection contract inserts its fixed shieldId and fwHash into the record hash, so
 * rec->shield_id and rec->fw_hash must hold the collection's values.
 */
anchor_status_t anchor_check_window_record(const anchor_domain_t *dom,
                                           const anchor_window_record_t *rec,
                                           const uint8_t shield_pub[64],
                                           const uint8_t shield_sig[64]);

anchor_status_t anchor_sign_nft_claim(ciwi_tr01_t *t, const anchor_domain_t *dom,
                                      const anchor_window_record_t *rec,
                                      const uint8_t metadata_hash[32], const uint8_t owner[20],
                                      anchor_nft_claim_t *claim, uint8_t sig[64]);

#endif /* ANCHOR_CIWI_H */
