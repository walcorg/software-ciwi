/**
 * @file    tr01_ciwi.c
 * @brief   TROPIC01 secure element, as used by the CiWi. See tr01_ciwi.h.
 */
#include "tr01_ciwi.h"

#include <string.h>

#include "ciwi_log.h"
#include "tr01_pin.h"
#include "tr01_platform.h"

/* trezor-crypto */
#include "bignum.h"
#include "ecdsa.h"
#include "ed25519-donna/ed25519.h"
#include "hmac.h"
#include "memzero.h"
#include "nist256p1.h"
#include "sha3.h"

#if CIWI_TR01_SH0_ENG_SAMPLE
#define SH0_PRIV lt_sh0priv_eng_sample
#define SH0_PUB  lt_sh0pub_eng_sample
#else
#define SH0_PRIV lt_sh0priv_prod0
#define SH0_PUB  lt_sh0pub_prod0
#endif

#define CHECK(expr)                                                              \
    do {                                                                         \
        lt_ret_t _r = (expr);                                                    \
        if (_r != LT_OK) {                                                       \
            CIWI_LOG("[TR01] %s failed: %s\r\n", #expr, lt_ret_verbose(_r));     \
            return _r;                                                           \
        }                                                                        \
    } while (0)

void ciwi_log_hex(const char *label, const unsigned char *buf, unsigned len)
{
    static const char hex[] = "0123456789abcdef";
    char line[2 * 80 + 1];
    unsigned n = len > 80 ? 80 : len;
    for (unsigned i = 0; i < n; i++) {
        line[2 * i] = hex[buf[i] >> 4];
        line[2 * i + 1] = hex[buf[i] & 0x0f];
    }
    line[2 * n] = '\0';
    CIWI_LOG("%s0x%s\r\n", label, line);
}

/* ================================================================ sessions */

static lt_ret_t session_open(ciwi_tr01_t *t, const uint8_t *priv, const uint8_t *pub,
                             lt_pkey_index_t slot)
{
    if (t->session_open) {
        lt_session_abort(&t->lt);
        t->session_open = false;
    }
    lt_ret_t r = lt_verify_chip_and_start_secure_session(&t->lt, priv, pub, slot);
    if (r == LT_OK) {
        t->session_open = true;
        t->session_slot = slot;
    }
    return r;
}

static void session_close(ciwi_tr01_t *t)
{
    if (t->session_open) {
        lt_session_abort(&t->lt);
        t->session_open = false;
    }
}

/* ================================================================ open / info */

lt_ret_t ciwi_tr01_open(ciwi_tr01_t *t)
{
    memset(t, 0, sizeof(*t));
    if (!tr01_platform_attach(&t->lt)) {
        CIWI_LOG("[TR01] platform attach failed\r\n");
        return LT_HAL_ERROR;
    }
    t->lt.l3.crypto_ctx = &t->crypto;
    CHECK(lt_init(&t->lt));

    lt_tr01_mode_t mode;
    lt_ret_t r = lt_get_tr01_mode(&t->lt, &mode);
    if (r != LT_OK || mode != LT_TR01_APPLICATION) {
        /* After power-up the chip may still be in its start-up (bootloader) mode. */
        CHECK(lt_reboot(&t->lt, TR01_REBOOT));
    }
    return LT_OK;
}

void ciwi_tr01_close(ciwi_tr01_t *t)
{
    session_close(t);
    lt_deinit(&t->lt);
    memzero(t->anchor_pub, sizeof(t->anchor_pub));
    t->anchor_ready = false;
}

lt_ret_t ciwi_tr01_print_info(ciwi_tr01_t *t)
{
    struct lt_chip_id_t id;
    uint8_t riscv[TR01_L2_GET_INFO_RISCV_FW_SIZE];
    uint8_t spect[TR01_L2_GET_INFO_SPECT_FW_SIZE];

    CHECK(lt_get_info_chip_id(&t->lt, &id));
    CHECK(lt_get_info_riscv_fw_ver(&t->lt, riscv));
    CHECK(lt_get_info_spect_fw_ver(&t->lt, spect));

    CIWI_LOG("[TR01] Silicon revision : %c%c%c%c\r\n", id.silicon_rev[0], id.silicon_rev[1],
             id.silicon_rev[2], id.silicon_rev[3]);
    /* Firmware versions are sent least significant byte first: [3].[2].[1] */
    CIWI_LOG("[TR01] RISC-V firmware  : %u.%u.%u\r\n", riscv[3], riscv[2], riscv[1]);
    CIWI_LOG("[TR01] SPECT firmware   : %u.%u.%u\r\n", spect[3], spect[2], spect[1]);
    ciwi_log_hex("[TR01] Serial number  : ", (const uint8_t *)&id.ser_num, sizeof(id.ser_num));
    ciwi_log_hex("[TR01] Batch ID       : ", id.batch_id, sizeof(id.batch_id));
    return LT_OK;
}

lt_ret_t ciwi_tr01_chip_serial_hash(ciwi_tr01_t *t, uint8_t out[32])
{
    struct lt_chip_id_t id;
    CHECK(lt_get_info_chip_id(&t->lt, &id));
    keccak_256((const uint8_t *)&id.ser_num, sizeof(id.ser_num), out);
    return LT_OK;
}

/* ================================================================ anchor key */

static void compute_ciwi_id(const uint8_t pub[64], uint8_t out[32])
{
    uint8_t buf[65];
    buf[0] = CIWI_CURVE_P256;
    memcpy(buf + 1, pub, 64);
    keccak_256(buf, sizeof(buf), out);
}

static lt_ret_t load_anchor_key(ciwi_tr01_t *t)
{
    lt_ecc_curve_type_t curve;
    lt_ecc_key_origin_t origin;
    lt_ret_t r = lt_ecc_key_read(&t->lt, CIWI_TR01_ANCHOR_SLOT, t->anchor_pub,
                                 sizeof(t->anchor_pub), &curve, &origin);
    if (r != LT_OK) return r; /* LT_L3_INVALID_KEY: slot empty, not provisioned yet */
    if (curve != TR01_CURVE_P256) {
        CIWI_LOG("[TR01] ECC slot %d holds a non-P-256 key\r\n", (int)CIWI_TR01_ANCHOR_SLOT);
        return LT_FAIL;
    }
    if (origin != TR01_KEY_GENERATED) {
        /* An imported key could exist outside the chip: refuse it as an anchor key. */
        CIWI_LOG("[TR01] ECC slot %d key was imported, not generated: refused\r\n",
                 (int)CIWI_TR01_ANCHOR_SLOT);
        return LT_FAIL;
    }
    compute_ciwi_id(t->anchor_pub, t->ciwi_id);
    t->anchor_ready = true;
    return LT_OK;
}

/* ================================================================ threat model
 *
 * Side-channel attacks need many operations with the SAME secret. ECDSA signatures use a fresh
 * nonce each time, but two operations here repeat a fixed key:
 *   - the X25519 handshake of every secure session, with the chip's static key (inside TROPIC01)
 *     and with the host's pairing key (on the STM32, computed by trezor-crypto WITHOUT scalar
 *     blinding);
 *   - a pairing key stored in the STM32 flash can also be read out if RDP is bypassed.
 * Roche, Imbert and Lomné show that scalar blinding with R = 64 random bits no longer protects
 * curves whose order is close to a power of 2 (secp256k1, Curve25519, ...): an attacker who can
 * measure about 10 000 operations with the same key recovers it; R must exceed half the key size.
 *
 *   T. Roche, L. Imbert, V. Lomné, "Side-channel Attacks on Blinded Scalar Multiplications
 *   Revisited", CARDIS 2019 (NinjaLab / LIRMM). https://eprint.iacr.org/2019/1220
 *
 * So the STM32 must not hold, in usable form, a key that can sign, and must not let anyone repeat
 * key operations freely. v0.5 splits the host keys:
 *   SH2 (boot)    plain in flash; the access policy lets its session do nothing but check the
 *                 PIN with MAC-and-Destroy (10 attempts in total, enforced by TROPIC01).
 *   SH1 (signing) stored encrypted with a key released by the correct PIN; only its session may
 *                 sign with the anchor key, move the claim counter or change the policy.
 * Extracting SH2 (from flash or by side channel) gives at most 10 PIN guesses. SH1 is only in RAM
 * after the PIN, and its handshake runs once per unlock, not on demand.
 * Not covered here: the blinding inside TROPIC01 (to confirm with Tropic Square: R > 128 bits for
 * X25519 and P-256) and the Bitcoin keys of the wallet (future work).
 */

/* ================================================================ host keys */

#define SH1_REC_VER  1
#define SH1_REC_LEN  (1 + 16 + 32 + 32 + 32) /* ver, nonce, ciphertext, pub, tag */

/** Wrapping key for SH1, domain-separated from other uses of the PIN key (the seed). */
static void wrap_key(const uint8_t pin_key[32], uint8_t k[32])
{
    static const char label[] = "CiWi SH1 wrap v1";
    hmac_sha256(pin_key, 32, (const uint8_t *)label, sizeof(label) - 1, k);
}

static void wrap_pad_tag(const uint8_t k[32], const uint8_t *rec, uint8_t pad[32], uint8_t tag[32])
{
    uint8_t buf[3 + 1 + 16 + 32 + 32];
    memcpy(buf, "enc", 3);
    memcpy(buf + 3, rec + 1, 16);                              /* nonce */
    hmac_sha256(k, 32, buf, 3 + 16, pad);
    memcpy(buf, "mac", 3);
    memcpy(buf + 3, rec, 1 + 16 + 32 + 32);                    /* ver, nonce, ct, pub */
    hmac_sha256(k, 32, buf, sizeof(buf), tag);
    memzero(buf, sizeof(buf));
}

static lt_ret_t sh1_save(ciwi_tr01_t *t, const uint8_t pin_key[32], const uint8_t priv[32],
                         const uint8_t pub[32])
{
    uint8_t rec[SH1_REC_LEN], k[32], pad[32], tag[32];
    rec[0] = SH1_REC_VER;
    CHECK(ciwi_tr01_random(t, rec + 1, 16));
    memcpy(rec + 49, pub, 32);
    wrap_key(pin_key, k);
    wrap_pad_tag(k, rec, pad, tag);                 /* pad uses only the nonce */
    for (int i = 0; i < 32; i++) rec[17 + i] = priv[i] ^ pad[i];
    wrap_pad_tag(k, rec, pad, tag);                 /* tag covers the ciphertext */
    memcpy(rec + 81, tag, 32);
    bool ok = tr01_keystore_save(TR01_KS_SH1, rec, sizeof(rec));
    memzero(k, sizeof(k));
    memzero(pad, sizeof(pad));
    return ok ? LT_OK : LT_FAIL;
}

static lt_ret_t sh1_load(const uint8_t pin_key[32], uint8_t priv[32], uint8_t pub[32])
{
    uint8_t rec[SH1_REC_LEN], k[32], pad[32], tag[32];
    uint16_t n = 0;
    if (!tr01_keystore_load(TR01_KS_SH1, rec, sizeof(rec), &n) || n != sizeof(rec) ||
        rec[0] != SH1_REC_VER) {
        return LT_FAIL;
    }
    wrap_key(pin_key, k);
    wrap_pad_tag(k, rec, pad, tag);
    uint8_t diff = 0;
    for (int i = 0; i < 32; i++) diff |= tag[i] ^ rec[81 + i];
    if (diff == 0) {
        for (int i = 0; i < 32; i++) priv[i] = rec[17 + i] ^ pad[i];
        memcpy(pub, rec + 49, 32);
    }
    memzero(k, sizeof(k));
    memzero(pad, sizeof(pad));
    return diff == 0 ? LT_OK : LT_CRYPTO_ERR;
}

static bool boot_load(uint8_t priv[32], uint8_t pub[32])
{
    uint8_t rec[64];
    uint16_t n = 0;
    bool ok = tr01_keystore_load(TR01_KS_BOOT, rec, sizeof(rec), &n) && n == sizeof(rec);
    if (ok) {
        memcpy(priv, rec, 32);
        memcpy(pub, rec + 32, 32);
    }
    memzero(rec, sizeof(rec));
    return ok;
}

/** New X25519 key: TROPIC01 TRNG XOR STM32 RNG, so a weakness in one source is not enough. */
static lt_ret_t new_x25519(ciwi_tr01_t *t, uint8_t priv[32], uint8_t pub[32])
{
    uint8_t mcu[32];
    CHECK(lt_random_value_get(&t->lt, priv, 32));
    if (!tr01_platform_random(mcu, sizeof(mcu))) return LT_FAIL;
    for (int i = 0; i < 32; i++) priv[i] ^= mcu[i];
    memzero(mcu, sizeof(mcu));
    priv[0] &= 248;
    priv[31] &= 127;
    priv[31] |= 64;
    curve25519_scalarmult_basepoint(pub, priv);
    return LT_OK;
}

/* ================================================================ boot, unlock, lock */

lt_ret_t ciwi_tr01_start(ciwi_tr01_t *t)
{
    uint8_t priv[32], pub[32];
    t->anchor_ready = false;
    if (!boot_load(priv, pub)) {
        t->state = CIWI_TR01_STATE_NEW;
        return LT_FAIL;
    }
    lt_ret_t r = session_open(t, priv, pub, CIWI_TR01_BOOT_SLOT);
    memzero(priv, sizeof(priv));
    if (r != LT_OK) {
        CIWI_LOG("[TR01] boot session (SH2) refused: %s\r\n", lt_ret_verbose(r));
        t->state = CIWI_TR01_STATE_FOREIGN;
        return r;
    }
    t->state = CIWI_TR01_STATE_LOCKED;
    return LT_OK;
}

/** From LOCKED with the SH1 key in RAM: open the signing session and load the anchor key. */
static lt_ret_t enter_ready(ciwi_tr01_t *t, const uint8_t priv[32], const uint8_t pub[32])
{
    CHECK(session_open(t, priv, pub, CIWI_TR01_SIGN_SLOT));
    t->state = CIWI_TR01_STATE_READY;
    lt_ret_t r = load_anchor_key(t);
    if (r == LT_L3_INVALID_KEY || r == LT_L3_SLOT_EMPTY) {
        CIWI_LOG("[TR01] no anchor key yet\r\n");
    }
    return r;
}

lt_ret_t ciwi_tr01_unlock(ciwi_tr01_t *t, const char *pin, const uint8_t *bind, uint8_t bind_len,
                          uint8_t *attempts_left)
{
    uint8_t pin_key[CIWI_PIN_KEY_LEN], priv[32], pub[32];
    if (t->state != CIWI_TR01_STATE_LOCKED) return LT_HOST_NO_SESSION;

    lt_ret_t r = ciwi_pin_check(t, pin, bind, bind_len, pin_key);
    if (attempts_left) ciwi_pin_attempts_left(t, attempts_left);
    if (r != LT_OK) return r;

    r = sh1_load(pin_key, priv, pub);
    memzero(pin_key, sizeof(pin_key));
    if (r != LT_OK) {
        CIWI_LOG("[TR01] SH1 record missing or damaged\r\n");
        return r;
    }
    r = enter_ready(t, priv, pub);
    memzero(priv, sizeof(priv));
    return r;
}

lt_ret_t ciwi_tr01_lock(ciwi_tr01_t *t)
{
    session_close(t);
    t->anchor_ready = false;
    return ciwi_tr01_start(t);
}

lt_ret_t ciwi_tr01_attempts_left(ciwi_tr01_t *t, uint8_t *left)
{
    *left = 0;
    if (t->state != CIWI_TR01_STATE_LOCKED) return LT_HOST_NO_SESSION; /* SH2 reads it */
    return ciwi_pin_attempts_left(t, left);
}

lt_ret_t ciwi_tr01_change_pin(ciwi_tr01_t *t, const char *old_pin, const char *new_pin,
                              const uint8_t *bind, uint8_t bind_len)
{
    uint8_t pin_key[CIWI_PIN_KEY_LEN], priv[32], pub[32];
    if (t->state != CIWI_TR01_STATE_LOCKED) CHECK(ciwi_tr01_lock(t));
    /* Same secret, same pin_key: the encrypted SH1 record stays valid. */
    lt_ret_t r = ciwi_pin_change(t, old_pin, new_pin, bind, bind_len, pin_key);
    if (r != LT_OK) return r;
    r = sh1_load(pin_key, priv, pub);
    memzero(pin_key, sizeof(pin_key));
    if (r == LT_OK) r = enter_ready(t, priv, pub);
    memzero(priv, sizeof(priv));
    return r;
}

/* ================================================================ provisioning */

/**
 * CiWi access policy (R-config).
 *   SH1 (signing): anchor key, claim counter, pairing-key and policy changes.
 *   SH2 (boot):    PIN only (MAC-and-Destroy slots, user data 384-511).
 *   SH0 (factory, public key): nothing beyond what every session may do.
 *   Every session: ping, random numbers, read public keys and counters.
 */
static void ciwi_r_config(struct lt_config_t *c)
{
    const uint32_t SH1 = LT_SESSION_SH1_HAS_ACCESS;
    const uint32_t SH2 = LT_SESSION_SH2_HAS_ACCESS;
    const uint32_t ALL = LT_SESSION_SH0_HAS_ACCESS | LT_SESSION_SH1_HAS_ACCESS |
                         LT_SESSION_SH2_HAS_ACCESS | LT_SESSION_SH3_HAS_ACCESS;

    /* ECC slots 0-7: generate, sign, erase only from SH1; never import (key store disabled). */
    c->obj[TR01_CFG_UAP_ECC_KEY_GENERATE_IDX] &= ~LT_TO_ECC_KEY_SLOT_0_7(ALL);
    c->obj[TR01_CFG_UAP_ECC_KEY_GENERATE_IDX] |= LT_TO_ECC_KEY_SLOT_0_7(SH1);
    c->obj[TR01_CFG_UAP_ECC_KEY_STORE_IDX] &= ~LT_TO_ECC_KEY_SLOT_0_7(ALL);
    c->obj[TR01_CFG_UAP_ECDSA_SIGN_IDX] &= ~LT_TO_ECC_KEY_SLOT_0_7(ALL);
    c->obj[TR01_CFG_UAP_ECDSA_SIGN_IDX] |= LT_TO_ECC_KEY_SLOT_0_7(SH1);
    c->obj[TR01_CFG_UAP_EDDSA_SIGN_IDX] &= ~LT_TO_ECC_KEY_SLOT_0_7(ALL);
    c->obj[TR01_CFG_UAP_ECC_KEY_ERASE_IDX] &= ~LT_TO_ECC_KEY_SLOT_0_7(ALL);
    c->obj[TR01_CFG_UAP_ECC_KEY_ERASE_IDX] |= LT_TO_ECC_KEY_SLOT_0_7(SH1);
    c->obj[TR01_CFG_UAP_ECC_KEY_READ_IDX] |= LT_TO_ECC_KEY_SLOT_0_7(ALL);

    /* Monotonic counters 0-3: change only from SH1. */
    c->obj[TR01_CFG_UAP_MCOUNTER_INIT_IDX] &= ~LT_TO_MCOUNTER_0_3(ALL);
    c->obj[TR01_CFG_UAP_MCOUNTER_INIT_IDX] |= LT_TO_MCOUNTER_0_3(SH1);
    c->obj[TR01_CFG_UAP_MCOUNTER_UPDATE_IDX] &= ~LT_TO_MCOUNTER_0_3(ALL);
    c->obj[TR01_CFG_UAP_MCOUNTER_UPDATE_IDX] |= LT_TO_MCOUNTER_0_3(SH1);
    c->obj[TR01_CFG_UAP_MCOUNTER_GET_IDX] |= LT_TO_MCOUNTER_0_3(ALL);

    /* PIN: MAC-and-Destroy slots 0-31 and user data 384-511 (PIN state in 511): SH2 only. */
    c->obj[TR01_CFG_UAP_MAC_AND_DESTROY_IDX] &= ~LT_TO_MACANDD_SLOT_0_31(ALL);
    c->obj[TR01_CFG_UAP_MAC_AND_DESTROY_IDX] |= LT_TO_MACANDD_SLOT_0_31(SH2);
    c->obj[TR01_CFG_UAP_R_MEM_DATA_WRITE_IDX] &= ~(ALL << 24);
    c->obj[TR01_CFG_UAP_R_MEM_DATA_WRITE_IDX] |= (SH2 << 24);
    c->obj[TR01_CFG_UAP_R_MEM_DATA_READ_IDX] &= ~(ALL << 24);
    c->obj[TR01_CFG_UAP_R_MEM_DATA_READ_IDX] |= (SH2 << 24);
    c->obj[TR01_CFG_UAP_R_MEM_DATA_ERASE_IDX] &= ~(ALL << 24);
    c->obj[TR01_CFG_UAP_R_MEM_DATA_ERASE_IDX] |= (SH2 << 24);

    /* Pairing keys: write and invalidate only from SH1. Reading public halves stays open. */
    c->obj[TR01_CFG_UAP_PAIRING_KEY_WRITE_IDX] =
        LT_TO_PAIRING_KEY_SH0(SH1) | LT_TO_PAIRING_KEY_SH1(SH1) | LT_TO_PAIRING_KEY_SH2(SH1) |
        LT_TO_PAIRING_KEY_SH3(SH1);
    c->obj[TR01_CFG_UAP_PAIRING_KEY_INVALIDATE_IDX] =
        LT_TO_PAIRING_KEY_SH0(SH1) | LT_TO_PAIRING_KEY_SH1(SH1) | LT_TO_PAIRING_KEY_SH2(SH1) |
        LT_TO_PAIRING_KEY_SH3(SH1);

    /* The policy itself: only SH1 may rewrite it. Without this, the public factory key SH0
     * could erase the policy and give itself the anchor key. */
    c->obj[TR01_CFG_UAP_R_CONFIG_WRITE_ERASE_IDX] &= ~ALL;
    c->obj[TR01_CFG_UAP_R_CONFIG_WRITE_ERASE_IDX] |= SH1;

    c->obj[TR01_CFG_UAP_PING_IDX] |= ALL;
    c->obj[TR01_CFG_UAP_RANDOM_VALUE_GET_IDX] |= ALL;
}

static lt_ret_t slot_must_be_empty(ciwi_tr01_t *t, lt_pkey_index_t slot)
{
    uint8_t pub[32];
    lt_ret_t r = lt_pairing_key_read(&t->lt, pub, slot);
    if (r == LT_OK) {
        CIWI_LOG("[TR01] pairing slot %d is already written. Slots are write-once: nothing was "
                 "changed.\r\n", (int)slot);
        return LT_L3_SLOT_NOT_EMPTY;
    }
    return r == LT_L3_SLOT_EMPTY ? LT_OK : r;
}

lt_ret_t ciwi_tr01_provision(ciwi_tr01_t *t, const char *pin, const uint8_t *bind,
                             uint8_t bind_len)
{
    uint8_t sh1_priv[32], sh1_pub[32], boot[64], pin_key[CIWI_PIN_KEY_LEN];
    lt_ret_t r;

    if (t->state != CIWI_TR01_STATE_NEW) {
        CIWI_LOG("[TR01] provisioning needs a CiWi without keys (state NEW)\r\n");
        return LT_FAIL;
    }

    CIWI_LOG("[TR01] step 1: session with factory key SH0\r\n");
    r = session_open(t, SH0_PRIV, SH0_PUB, TR01_PAIRING_KEY_SLOT_INDEX_0);
    if (r != LT_OK) {
        CIWI_LOG("[TR01] SH0 refused (%s). Engineering sample? Build with "
                 "CIWI_TR01_SH0_ENG_SAMPLE=1\r\n", lt_ret_verbose(r));
        return r;
    }
    if ((r = slot_must_be_empty(t, CIWI_TR01_SIGN_SLOT)) != LT_OK ||
        (r = slot_must_be_empty(t, CIWI_TR01_BOOT_SLOT)) != LT_OK) {
        session_close(t);
        return r;
    }

    CIWI_LOG("[TR01] step 2: boot key (slot 2) and signing key (slot 1)\r\n");
    if ((r = new_x25519(t, boot, boot + 32)) != LT_OK) goto fail;
    if ((r = new_x25519(t, sh1_priv, sh1_pub)) != LT_OK) goto fail;
    /* Boot key saved before its slot is written: a write-once slot is never left without its key. */
    if (!tr01_keystore_save(TR01_KS_BOOT, boot, sizeof(boot))) { r = LT_FAIL; goto fail; }
    if ((r = lt_pairing_key_write(&t->lt, boot + 32, CIWI_TR01_BOOT_SLOT)) != LT_OK) goto fail;
    if ((r = lt_pairing_key_write(&t->lt, sh1_pub, CIWI_TR01_SIGN_SLOT)) != LT_OK) goto fail;
    ciwi_log_hex("[TR01] SH2 boot pub    : ", boot + 32, 32);
    ciwi_log_hex("[TR01] SH1 signing pub : ", sh1_pub, 32);

#if CIWI_TR01_APPLY_RCONFIG
    CIWI_LOG("[TR01] step 3: CiWi access policy (R-config)\r\n");
    {
        struct lt_config_t cfg;
        if ((r = lt_read_whole_R_config(&t->lt, &cfg)) != LT_OK) goto fail;
        ciwi_r_config(&cfg);
        if ((r = lt_r_config_erase(&t->lt)) != LT_OK) goto fail;
        if ((r = lt_write_whole_R_config(&t->lt, &cfg)) != LT_OK) goto fail;
    }
    session_close(t);
    if ((r = lt_reboot(&t->lt, TR01_REBOOT)) != LT_OK) goto fail; /* the policy applies now */
#else
    (void)ciwi_r_config;
    CIWI_LOG("[TR01] step 3: R-config NOT written (CIWI_TR01_APPLY_RCONFIG=0): the PIN does not "
             "protect the anchor key\r\n");
    session_close(t);
#endif

    CIWI_LOG("[TR01] step 4: PIN (MAC-and-Destroy) in the boot session\r\n");
    if ((r = session_open(t, boot, boot + 32, CIWI_TR01_BOOT_SLOT)) != LT_OK) goto fail;
    t->state = CIWI_TR01_STATE_LOCKED;
    if ((r = ciwi_pin_set(t, pin, bind, bind_len, pin_key)) != LT_OK) goto fail;
    r = sh1_save(t, pin_key, sh1_priv, sh1_pub);
    memzero(pin_key, sizeof(pin_key));
    if (r != LT_OK) goto fail;

    CIWI_LOG("[TR01] step 5: anchor key (P-256, ECC slot %d) and claim counter\r\n",
             (int)CIWI_TR01_ANCHOR_SLOT);
    r = enter_ready(t, sh1_priv, sh1_pub);
    if (r != LT_OK && !t->anchor_ready) {
        if ((r = lt_ecc_key_generate(&t->lt, CIWI_TR01_ANCHOR_SLOT, TR01_CURVE_P256)) != LT_OK) {
            goto fail;
        }
        if ((r = load_anchor_key(t)) != LT_OK) goto fail;
    }
    {
        uint32_t mc;
        if (lt_mcounter_get(&t->lt, CIWI_TR01_CLAIM_COUNTER, &mc) != LT_OK &&
            (r = lt_mcounter_init(&t->lt, CIWI_TR01_CLAIM_COUNTER, TR01_MCOUNTER_VALUE_MAX)) !=
                LT_OK) {
            goto fail;
        }
    }

#if CIWI_TR01_INVALIDATE_SH0
    CIWI_LOG("[TR01] step 6: invalidating factory key SH0 (irreversible)\r\n");
    if ((r = lt_pairing_key_invalidate(&t->lt, TR01_PAIRING_KEY_SLOT_INDEX_0)) != LT_OK) goto fail;
#else
    CIWI_LOG("[TR01] step 6: factory key SH0 kept (CIWI_TR01_INVALIDATE_SH0=0)\r\n");
#endif

    CIWI_LOG("[TR01] provisioning complete\r\n");
    r = LT_OK;
fail:
    if (r != LT_OK) CIWI_LOG("[TR01] provisioning stopped: %s\r\n", lt_ret_verbose(r));
    memzero(sh1_priv, sizeof(sh1_priv));
    memzero(boot, sizeof(boot));
    return r;
}

/* ================================================================ anchor key use */

const uint8_t *ciwi_tr01_anchor_pub(const ciwi_tr01_t *t)
{
    return t->anchor_ready ? t->anchor_pub : NULL;
}

const uint8_t *ciwi_tr01_ciwi_id(const ciwi_tr01_t *t)
{
    return t->anchor_ready ? t->ciwi_id : NULL;
}

/** s := n - s when s > n/2 (Anchor Protocol and OpenZeppelin P256 accept low-s only). */
static void normalise_low_s(uint8_t sig[64])
{
    bignum256 s, ns;
    bn_read_be(sig + 32, &s);
    if (bn_is_less(&nist256p1.order_half, &s)) {
        bn_subtract(&nist256p1.order, &s, &ns);
        bn_write_be(&ns, sig + 32);
    }
}

lt_ret_t ciwi_tr01_anchor_sign(ciwi_tr01_t *t, const uint8_t digest[32], uint8_t sig_rs[64])
{
    if (!t->anchor_ready || !t->session_open) return LT_HOST_NO_SESSION;

    CHECK(lt_ecc_ecdsa_sign(&t->lt, CIWI_TR01_ANCHOR_SLOT, digest, 32, sig_rs));
    normalise_low_s(sig_rs);

    uint8_t pub65[65];
    pub65[0] = 0x04;
    memcpy(pub65 + 1, t->anchor_pub, 64);
    if (ecdsa_verify_digest(&nist256p1, pub65, sig_rs, digest) != 0) {
        memzero(sig_rs, 64);
        CIWI_LOG("[TR01] signature did not verify: discarded\r\n");
        return LT_CRYPTO_ERR;
    }
    return LT_OK;
}

lt_ret_t ciwi_tr01_claim_counter(ciwi_tr01_t *t, uint64_t *ciwi_counter)
{
    uint32_t v;
    CHECK(lt_mcounter_get(&t->lt, CIWI_TR01_CLAIM_COUNTER, &v));
    /* TROPIC01 counters count down from the initial value. */
    *ciwi_counter = (uint64_t)(TR01_MCOUNTER_VALUE_MAX - v);
    return LT_OK;
}

lt_ret_t ciwi_tr01_next_claim_counter(ciwi_tr01_t *t, uint64_t *ciwi_counter)
{
    CHECK(lt_mcounter_update(&t->lt, CIWI_TR01_CLAIM_COUNTER));
    return ciwi_tr01_claim_counter(t, ciwi_counter);
}

lt_ret_t ciwi_tr01_random(ciwi_tr01_t *t, uint8_t *buf, uint16_t len)
{
    uint8_t chunk[TR01_RANDOM_VALUE_GET_LEN_MAX];
    uint8_t mcu[TR01_RANDOM_VALUE_GET_LEN_MAX];
    while (len) {
        uint8_t n = len > sizeof(chunk) ? sizeof(chunk) : (uint8_t)len;
        CHECK(lt_random_value_get(&t->lt, chunk, n));
        if (!tr01_platform_random(mcu, n)) return LT_FAIL;
        for (uint8_t i = 0; i < n; i++) *buf++ = chunk[i] ^ mcu[i];
        len -= n;
    }
    memzero(chunk, sizeof(chunk));
    memzero(mcu, sizeof(mcu));
    return LT_OK;
}

lt_ret_t ciwi_tr01_ping(ciwi_tr01_t *t)
{
    static const uint8_t msg[] = "CiWi <-> TROPIC01 secure channel";
    uint8_t back[sizeof(msg)];
    CHECK(lt_ping(&t->lt, msg, back, sizeof(msg)));
    return memcmp(msg, back, sizeof(msg)) == 0 ? LT_OK : LT_FAIL;
}
