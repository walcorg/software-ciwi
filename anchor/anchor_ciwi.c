/**
 * @file    anchor_ciwi.c
 * @brief   The CiWi's part of the WALCORG Anchor Protocol v0.2. See anchor_ciwi.h.
 */
#include "anchor_ciwi.h"

#include <string.h>

#include "bignum.h"
#include "ciwi_log.h"
#include "ecdsa.h"
#include "nist256p1.h"

#define ANCHOR_PROTOCOL_VERSION 2

const char *anchor_status_str(anchor_status_t s)
{
    switch (s) {
    case ANCHOR_OK: return "OK";
    case ANCHOR_ERR_VERSION: return "unsupported version or curve";
    case ANCHOR_ERR_SHIELD_ID: return "shieldId does not match the shield key";
    case ANCHOR_ERR_SHIELD_SIG: return "shield signature invalid";
    case ANCHOR_ERR_WRONG_CIWI: return "message is for another CiWi";
    case ANCHOR_ERR_WRONG_OWNER: return "owner does not match";
    case ANCHOR_ERR_WINDOW: return "invalid window";
    case ANCHOR_ERR_TR01: return "TROPIC01 error";
    }
    return "?";
}

bool anchor_p256_verify(const uint8_t pub[64], const uint8_t digest[32], const uint8_t sig[64])
{
    bignum256 s;
    bn_read_be(sig + 32, &s);
    if (bn_is_less(&nist256p1.order_half, &s)) return false; /* high-s is rejected */

    uint8_t pub65[65];
    pub65[0] = 0x04;
    memcpy(pub65 + 1, pub, 64);
    return ecdsa_verify_digest(&nist256p1, pub65, sig, digest) == 0;
}

static anchor_status_t sign(ciwi_tr01_t *t, const uint8_t digest[32], uint8_t sig[64])
{
    return ciwi_tr01_anchor_sign(t, digest, sig) == LT_OK ? ANCHOR_OK : ANCHOR_ERR_TR01;
}

/* ================================================================ CaptureReceipt -> Claim */

anchor_status_t anchor_check_receipt(const anchor_domain_t *dom,
                                     const anchor_capture_receipt_t *rcpt,
                                     const uint8_t shield_pub[64], const uint8_t shield_sig[64],
                                     const uint8_t owner[20])
{
    uint8_t id[32], ds[32], sh[32], dg[32];

    if (rcpt->version != ANCHOR_PROTOCOL_VERSION || rcpt->curve != CIWI_CURVE_P256) {
        return ANCHOR_ERR_VERSION;
    }
    if (rcpt->window_end <= rcpt->window_start) return ANCHOR_ERR_WINDOW;
    anchor_device_id(rcpt->curve, shield_pub, id);
    if (memcmp(id, rcpt->shield_id, 32) != 0) return ANCHOR_ERR_SHIELD_ID;
    if (memcmp(owner, rcpt->owner, 20) != 0) return ANCHOR_ERR_WRONG_OWNER;

    anchor_domain_separator(dom, ds);
    anchor_hash_capture_receipt(rcpt, sh);
    anchor_digest(ds, sh, dg);
    if (!anchor_p256_verify(shield_pub, dg, shield_sig)) return ANCHOR_ERR_SHIELD_SIG;
    return ANCHOR_OK;
}

anchor_status_t anchor_sign_claim(ciwi_tr01_t *t, const anchor_domain_t *dom,
                                  const anchor_capture_receipt_t *rcpt, anchor_claim_t *claim,
                                  uint8_t sig[64])
{
    uint8_t ds[32], sh[32], dg[32];
    const uint8_t *ciwi_id = ciwi_tr01_ciwi_id(t);
    if (!ciwi_id) return ANCHOR_ERR_TR01;

    memset(claim, 0, sizeof(*claim));
    anchor_hash_capture_receipt(rcpt, claim->receipt_hash);
    memcpy(claim->ciwi_id, ciwi_id, 32);
    memcpy(claim->owner, rcpt->owner, 20);
    /* The counter moves before signing: a signature is never produced twice for one value. */
    if (ciwi_tr01_next_claim_counter(t, &claim->ciwi_counter) != LT_OK) return ANCHOR_ERR_TR01;

    anchor_domain_separator(dom, ds);
    anchor_hash_claim(claim, sh);
    anchor_digest(ds, sh, dg);
    return sign(t, dg, sig);
}

/* ================================================================ Bind */

anchor_status_t anchor_check_bind(const anchor_domain_t *dom, const ciwi_tr01_t *t,
                                  const anchor_bind_t *bind, const uint8_t shield_pub[64],
                                  const uint8_t shield_sig[64])
{
    uint8_t id[32], ds[32], sh[32], dg[32];
    const uint8_t *ciwi_id = ciwi_tr01_ciwi_id(t);
    if (!ciwi_id) return ANCHOR_ERR_TR01;

    anchor_device_id(CIWI_CURVE_P256, shield_pub, id);
    if (memcmp(id, bind->shield_id, 32) != 0) return ANCHOR_ERR_SHIELD_ID;
    if (memcmp(ciwi_id, bind->ciwi_id, 32) != 0) return ANCHOR_ERR_WRONG_CIWI;

    anchor_domain_separator(dom, ds);
    anchor_hash_bind(bind, sh);
    anchor_digest(ds, sh, dg);
    if (!anchor_p256_verify(shield_pub, dg, shield_sig)) return ANCHOR_ERR_SHIELD_SIG;
    return ANCHOR_OK;
}

anchor_status_t anchor_sign_bind(ciwi_tr01_t *t, const anchor_domain_t *dom,
                                 const anchor_bind_t *bind, uint8_t sig[64])
{
    uint8_t ds[32], sh[32], dg[32];
    const uint8_t *ciwi_id = ciwi_tr01_ciwi_id(t);
    if (!ciwi_id || memcmp(ciwi_id, bind->ciwi_id, 32) != 0) return ANCHOR_ERR_WRONG_CIWI;

    anchor_domain_separator(dom, ds);
    anchor_hash_bind(bind, sh);
    anchor_digest(ds, sh, dg);
    return sign(t, dg, sig);
}

/* ================================================================ NFT profile */

anchor_status_t anchor_check_window_record(const anchor_domain_t *dom,
                                           const anchor_window_record_t *rec,
                                           const uint8_t shield_pub[64],
                                           const uint8_t shield_sig[64])
{
    uint8_t id[32], ds[32], sh[32], dg[32];

    if (rec->version != ANCHOR_PROTOCOL_VERSION || rec->curve != CIWI_CURVE_P256) {
        return ANCHOR_ERR_VERSION;
    }
    if (rec->window_end <= rec->window_start) return ANCHOR_ERR_WINDOW;
    anchor_device_id(rec->curve, shield_pub, id);
    if (memcmp(id, rec->shield_id, 32) != 0) return ANCHOR_ERR_SHIELD_ID;

    anchor_domain_separator(dom, ds);
    anchor_hash_window_record(rec, sh);
    anchor_digest(ds, sh, dg);
    if (!anchor_p256_verify(shield_pub, dg, shield_sig)) return ANCHOR_ERR_SHIELD_SIG;
    return ANCHOR_OK;
}

anchor_status_t anchor_sign_nft_claim(ciwi_tr01_t *t, const anchor_domain_t *dom,
                                      const anchor_window_record_t *rec,
                                      const uint8_t metadata_hash[32], const uint8_t owner[20],
                                      anchor_nft_claim_t *claim, uint8_t sig[64])
{
    uint8_t ds[32], sh[32], dg[32];
    const uint8_t *ciwi_id = ciwi_tr01_ciwi_id(t);
    if (!ciwi_id) return ANCHOR_ERR_TR01;

    memset(claim, 0, sizeof(*claim));
    anchor_hash_window_record(rec, claim->record_hash);
    memcpy(claim->metadata_hash, metadata_hash, 32);
    memcpy(claim->ciwi_id, ciwi_id, 32);
    memcpy(claim->owner, owner, 20);

    anchor_domain_separator(dom, ds);
    anchor_hash_nft_claim(claim, sh);
    anchor_digest(ds, sh, dg);
    return sign(t, dg, sig);
}
