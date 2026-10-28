/**
 * @file    tr01_ciwi.c
 * @brief   TROPIC01 secure element, as used by the CiWi. See tr01_ciwi.h.
 */
#include "tr01_ciwi.h"

#include <string.h>

#include "ciwi_log.h"
#include "tr01_platform.h"

/* trezor-crypto */
#include "bignum.h"
#include "ecdsa.h"
#include "ed25519-donna/ed25519.h"
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

/* ================================================================ start (every boot) */

lt_ret_t ciwi_tr01_start(ciwi_tr01_t *t)
{
    uint8_t priv[TR01_PAIRING_KEY_LEN], pub[TR01_PAIRING_KEY_LEN];

    t->anchor_ready = false;
    if (!tr01_keystore_load(priv, pub)) {
        t->state = CIWI_TR01_STATE_NEW;
        return LT_FAIL;
    }
    lt_ret_t r = session_open(t, priv, pub, CIWI_TR01_PAIRING_SLOT);
    memzero(priv, sizeof(priv));
    if (r != LT_OK) {
        CIWI_LOG("[TR01] session with SH1 refused: %s\r\n", lt_ret_verbose(r));
        t->state = CIWI_TR01_STATE_FOREIGN;
        return r;
    }
    t->state = CIWI_TR01_STATE_READY;

    r = load_anchor_key(t);
    if (r == LT_L3_INVALID_KEY || r == LT_L3_SLOT_EMPTY) {
        CIWI_LOG("[TR01] no anchor key yet (run provisioning)\r\n");
    }
    return r;
}

/* ================================================================ provisioning */

/** CiWi access policy: only the CiWi session (SH1) may use the anchor key, counter and PIN. */
static void ciwi_r_config(struct lt_config_t *c)
{
    const uint32_t SH1 = LT_SESSION_SH1_HAS_ACCESS;
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
    /* Reading the public key stays open to every session. */
    c->obj[TR01_CFG_UAP_ECC_KEY_READ_IDX] |= LT_TO_ECC_KEY_SLOT_0_7(ALL);

    /* Monotonic counters 0-3: SH1 only. */
    c->obj[TR01_CFG_UAP_MCOUNTER_INIT_IDX] &= ~LT_TO_MCOUNTER_0_3(ALL);
    c->obj[TR01_CFG_UAP_MCOUNTER_INIT_IDX] |= LT_TO_MCOUNTER_0_3(SH1);
    c->obj[TR01_CFG_UAP_MCOUNTER_UPDATE_IDX] &= ~LT_TO_MCOUNTER_0_3(ALL);
    c->obj[TR01_CFG_UAP_MCOUNTER_UPDATE_IDX] |= LT_TO_MCOUNTER_0_3(SH1);
    c->obj[TR01_CFG_UAP_MCOUNTER_GET_IDX] |= LT_TO_MCOUNTER_0_3(ALL);

    /* MAC-and-Destroy slots 0-31 (PIN attempts): SH1 only. */
    c->obj[TR01_CFG_UAP_MAC_AND_DESTROY_IDX] &= ~LT_TO_MACANDD_SLOT_0_31(ALL);
    c->obj[TR01_CFG_UAP_MAC_AND_DESTROY_IDX] |= LT_TO_MACANDD_SLOT_0_31(SH1);

    /* User data slots 384-511 (PIN state lives in 511): SH1 only. */
    c->obj[TR01_CFG_UAP_R_MEM_DATA_WRITE_IDX] &= ~(ALL << 24);
    c->obj[TR01_CFG_UAP_R_MEM_DATA_WRITE_IDX] |= (SH1 << 24);
    c->obj[TR01_CFG_UAP_R_MEM_DATA_READ_IDX] &= ~(ALL << 24);
    c->obj[TR01_CFG_UAP_R_MEM_DATA_READ_IDX] |= (SH1 << 24);
    c->obj[TR01_CFG_UAP_R_MEM_DATA_ERASE_IDX] &= ~(ALL << 24);
    c->obj[TR01_CFG_UAP_R_MEM_DATA_ERASE_IDX] |= (SH1 << 24);

    /* Ping and random numbers: every session. R-config write/erase: unchanged, so the policy can
     * always be rewritten at the bench. */
    c->obj[TR01_CFG_UAP_PING_IDX] |= ALL;
    c->obj[TR01_CFG_UAP_RANDOM_VALUE_GET_IDX] |= ALL;
}

static lt_ret_t provision_pairing_key(ciwi_tr01_t *t)
{
    uint8_t priv[TR01_PAIRING_KEY_LEN], pub[TR01_PAIRING_KEY_LEN];
    uint8_t mcu_rnd[TR01_PAIRING_KEY_LEN];
    uint8_t slot_pub[TR01_PAIRING_KEY_LEN];

    lt_ret_t r = lt_pairing_key_read(&t->lt, slot_pub, CIWI_TR01_PAIRING_SLOT);
    if (r == LT_OK) {
        CIWI_LOG("[TR01] pairing slot 1 is already written, but not with the key in this STM32.\r\n");
        CIWI_LOG("[TR01] Pairing slots are write-once: nothing was changed.\r\n");
        return LT_L3_SLOT_NOT_EMPTY;
    }
    if (r != LT_L3_SLOT_EMPTY) {
        CIWI_LOG("[TR01] pairing slot 1 read: %s\r\n", lt_ret_verbose(r));
        return r;
    }

    /* Private key = TROPIC01 TRNG XOR STM32 RNG: a weakness in one source is not enough. */
    CHECK(lt_random_value_get(&t->lt, priv, sizeof(priv)));
    if (!tr01_platform_random(mcu_rnd, sizeof(mcu_rnd))) {
        memzero(priv, sizeof(priv));
        return LT_FAIL;
    }
    for (unsigned i = 0; i < sizeof(priv); i++) priv[i] ^= mcu_rnd[i];
    memzero(mcu_rnd, sizeof(mcu_rnd));
    /* X25519 clamping, then the public key. */
    priv[0] &= 248;
    priv[31] &= 127;
    priv[31] |= 64;
    curve25519_scalarmult_basepoint(pub, priv);

    /* Save first: if the write to TROPIC01 succeeded but the flash write failed, the slot would
     * be lost for good. */
    if (!tr01_keystore_save(priv, pub)) {
        memzero(priv, sizeof(priv));
        CIWI_LOG("[TR01] could not store the pairing key in flash\r\n");
        return LT_FAIL;
    }
    memzero(priv, sizeof(priv));
    CHECK(lt_pairing_key_write(&t->lt, pub, CIWI_TR01_PAIRING_SLOT));
    ciwi_log_hex("[TR01] SH1 public key  : ", pub, sizeof(pub));
    return LT_OK;
}

lt_ret_t ciwi_tr01_provision(ciwi_tr01_t *t)
{
    lt_ret_t r = ciwi_tr01_start(t);
    if (r == LT_OK) {
        CIWI_LOG("[TR01] already provisioned\r\n");
        return LT_OK;
    }

    if (t->state != CIWI_TR01_STATE_READY) {
        CIWI_LOG("[TR01] step 1: session with factory key SH0\r\n");
        r = session_open(t, SH0_PRIV, SH0_PUB, TR01_PAIRING_KEY_SLOT_INDEX_0);
        if (r != LT_OK) {
            CIWI_LOG("[TR01] SH0 refused (%s). Engineering sample? Build with "
                     "CIWI_TR01_SH0_ENG_SAMPLE=1\r\n",
                     lt_ret_verbose(r));
            return r;
        }

        CIWI_LOG("[TR01] step 2: CiWi pairing key into slot 1\r\n");
        /* Safe in the FOREIGN state too: provision_pairing_key() refuses if slot 1 is already
         * written, and only replaces the stored key when the slot is still empty (e.g. the chip
         * write failed after the flash save). The stored key is never erased here. */
        r = provision_pairing_key(t);
        if (r != LT_OK) {
            session_close(t);
            return r;
        }

#if CIWI_TR01_APPLY_RCONFIG
        CIWI_LOG("[TR01] step 3: CiWi access policy (R-config)\r\n");
        struct lt_config_t cfg;
        CHECK(lt_read_whole_R_config(&t->lt, &cfg));
        ciwi_r_config(&cfg);
        CHECK(lt_r_config_erase(&t->lt));
        CHECK(lt_write_whole_R_config(&t->lt, &cfg));
        session_close(t);
        /* The R-config takes effect after a reboot. */
        CHECK(lt_reboot(&t->lt, TR01_REBOOT));
#else
        (void)ciwi_r_config;
        CIWI_LOG("[TR01] step 3: R-config left unchanged (CIWI_TR01_APPLY_RCONFIG=0)\r\n");
        session_close(t);
#endif

        CIWI_LOG("[TR01] step 4: session with SH1\r\n");
        r = ciwi_tr01_start(t);
        if (t->state != CIWI_TR01_STATE_READY) return r;
    }

    if (!t->anchor_ready) {
        CIWI_LOG("[TR01] step 4: anchor key (P-256) in ECC slot %d\r\n", (int)CIWI_TR01_ANCHOR_SLOT);
        CHECK(lt_ecc_key_generate(&t->lt, CIWI_TR01_ANCHOR_SLOT, TR01_CURVE_P256));
        CHECK(load_anchor_key(t));
    }

    uint32_t mc;
    r = lt_mcounter_get(&t->lt, CIWI_TR01_CLAIM_COUNTER, &mc);
    if (r != LT_OK) {
        CIWI_LOG("[TR01] step 4: claim counter\r\n");
        CHECK(lt_mcounter_init(&t->lt, CIWI_TR01_CLAIM_COUNTER, TR01_MCOUNTER_VALUE_MAX));
    }

#if CIWI_TR01_INVALIDATE_SH0
    CIWI_LOG("[TR01] step 5: invalidating factory key SH0 (irreversible)\r\n");
    {
        uint8_t priv[TR01_PAIRING_KEY_LEN], pub[TR01_PAIRING_KEY_LEN];
        bool ok = tr01_keystore_load(priv, pub);
        memzero(priv, sizeof(priv));
        if (!ok) return LT_FAIL;  /* never invalidate SH0 without a working SH1 key */
    }
    CHECK(lt_pairing_key_invalidate(&t->lt, TR01_PAIRING_KEY_SLOT_INDEX_0));
#else
    CIWI_LOG("[TR01] step 5: factory key SH0 kept (CIWI_TR01_INVALIDATE_SH0=0)\r\n");
#endif

    CIWI_LOG("[TR01] provisioning complete\r\n");
    return LT_OK;
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
