/**
 * @file    host_main.c
 * @brief   Runs the CiWi TROPIC01 and Anchor code on a PC against the TROPIC01 model.
 *
 * The same tropic/ and anchor/ sources as the firmware are compiled here; only the platform file
 * differs. Writes vectors.json for verify.js, which recomputes every digest with ethers.js and
 * checks every signature with @noble/curves.
 *
 *   ./ciwi_host [vectors.json]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "anchor_ciwi.h"
#include "anchor_eip712.h"
#include "anchor_selftest.h"
#include "ciwi_log.h"
#include "ecdsa.h"
#include "nist256p1.h"
#include "tr01_ciwi.h"
#include "tr01_pin.h"
#include "tr01_platform.h"

static int g_pass, g_fail;

#define EXPECT(name, cond)                                         \
    do {                                                           \
        if (cond) {                                                \
            g_pass++;                                              \
            printf("  PASS  %s\n", name);                          \
        } else {                                                   \
            g_fail++;                                              \
            printf("  FAIL  %s  (%s:%d)\n", name, __FILE__, __LINE__); \
        }                                                          \
    } while (0)

/* ------------------------------------------------------------- selftest I/O */

static FILE *J;

static void emit_file(const char *text) { fputs(text, J); }

static bool auto_confirm(const char *title, const char *detail)
{
    printf("  [confirm] %s: %s -> approved (host)\n", title, detail);
    return true;
}

static void result(const char *name, bool ok)
{
    EXPECT(name, ok);
}

static void fill(uint8_t *b, size_t n, uint8_t seed)
{
    for (size_t i = 0; i < n; i++) b[i] = (uint8_t)(seed + 7 * i);
}

int main(int argc, char **argv)
{
    const char *out = argc > 1 ? argv[1] : "vectors.json";
    ciwi_tr01_t t;
    lt_ret_t r;

    printf("== 1. TROPIC01 bring-up ==\n");
    r = ciwi_tr01_open(&t);
    EXPECT("open: lt_init + application mode", r == LT_OK);
    if (r != LT_OK) return 1;
    EXPECT("chip info", ciwi_tr01_print_info(&t) == LT_OK);

    static const uint8_t bind[12] = {0x31, 0x00, 0x2b, 0x00, 0x12, 0x51, 0x33, 0x35,
                                     0x37, 0x38, 0x39, 0x30}; /* stands in for the STM32 UID */
    uint8_t left = 0;

    printf("== 2. Provisioning (SH0 -> boot key SH2 + signing key SH1, policy, PIN, anchor key) ==\n");
    tr01_keystore_erase(TR01_KS_BOOT);
    tr01_keystore_erase(TR01_KS_SH1);
    EXPECT("start before provisioning reports NEW",
           ciwi_tr01_start(&t) != LT_OK && t.state == CIWI_TR01_STATE_NEW);
    EXPECT("provision with the first PIN", ciwi_tr01_provision(&t, "2468", bind, sizeof(bind)) == LT_OK);
    EXPECT("ends READY", t.state == CIWI_TR01_STATE_READY && t.anchor_ready);
    EXPECT("provision again is refused", ciwi_tr01_provision(&t, "2468", bind, sizeof(bind)) != LT_OK);
    uint8_t first_id[32];
    memcpy(first_id, ciwi_tr01_ciwi_id(&t), 32);

    printf("== 3. Normal boot: LOCKED until the PIN ==\n");
    ciwi_tr01_close(&t);
    EXPECT("re-open", ciwi_tr01_open(&t) == LT_OK);
    EXPECT("start: boot session SH2, state LOCKED",
           ciwi_tr01_start(&t) == LT_OK && t.state == CIWI_TR01_STATE_LOCKED);
    uint8_t dg[32], sig[64];
    fill(dg, 32, 0xa0);
    EXPECT("LOCKED: no anchor key, no signature",
           !ciwi_tr01_anchor_pub(&t) && ciwi_tr01_anchor_sign(&t, dg, sig) != LT_OK);
    EXPECT("LOCKED: ping works", ciwi_tr01_ping(&t) == LT_OK);
    EXPECT("attempts = 10", ciwi_tr01_attempts_left(&t, &left) == LT_OK && left == 10);
    EXPECT("wrong PIN: stays LOCKED, 9 left",
           ciwi_tr01_unlock(&t, "1357", bind, sizeof(bind), &left) != LT_OK && left == 9 &&
               t.state == CIWI_TR01_STATE_LOCKED);
    EXPECT("right PIN: READY, attempts back to 10",
           ciwi_tr01_unlock(&t, "2468", bind, sizeof(bind), &left) == LT_OK && left == 10 &&
               t.state == CIWI_TR01_STATE_READY);
    EXPECT("ping over the signing session", ciwi_tr01_ping(&t) == LT_OK);
    uint8_t rnd[300];
    EXPECT("random 300 bytes (2 chunks)", ciwi_tr01_random(&t, rnd, sizeof(rnd)) == LT_OK);
    const uint8_t *apub = ciwi_tr01_anchor_pub(&t);
    const uint8_t *cid = ciwi_tr01_ciwi_id(&t);
    EXPECT("anchor key loaded, same ciwiId as at provisioning",
           apub && cid && memcmp(cid, first_id, 32) == 0);
    if (!apub) return 1;
    ciwi_log_hex("  anchor X : ", apub, 32);
    ciwi_log_hex("  anchor Y : ", apub + 32, 32);
    ciwi_log_hex("  ciwiId   : ", cid, 32);
    uint8_t id2[32];
    anchor_device_id(CIWI_CURVE_P256, apub, id2);
    EXPECT("ciwiId matches anchor_device_id", memcmp(id2, cid, 32) == 0);

    printf("== 4. Signing ==\n");
    EXPECT("anchor sign (low-s, self-verified)", ciwi_tr01_anchor_sign(&t, dg, sig) == LT_OK);
    EXPECT("anchor_p256_verify accepts it", anchor_p256_verify(apub, dg, sig));
    sig[5] ^= 1;
    EXPECT("anchor_p256_verify rejects a changed signature", !anchor_p256_verify(apub, dg, sig));
    uint64_t c0, c1, c2;
    EXPECT("claim counter read", ciwi_tr01_claim_counter(&t, &c0) == LT_OK);
    EXPECT("claim counter next", ciwi_tr01_next_claim_counter(&t, &c1) == LT_OK);
    EXPECT("claim counter next again", ciwi_tr01_next_claim_counter(&t, &c2) == LT_OK);
    printf("  counter %llu -> %llu -> %llu\n", (unsigned long long)c0, (unsigned long long)c1,
           (unsigned long long)c2);
    EXPECT("counter strictly increasing by 1", c1 == c0 + 1 && c2 == c1 + 1);

#if CIWI_TR01_APPLY_RCONFIG
    printf("== 4b. Access policy is enforced by TROPIC01 ==\n");
    uint8_t md[32] = {0}, md_out[32];
    ciwi_tr01_lock(&t);
    EXPECT("boot session SH2 may NOT sign with the anchor key",
           lt_ecc_ecdsa_sign(&t.lt, CIWI_TR01_ANCHOR_SLOT, dg, 32, sig) == LT_L3_UNAUTHORIZED);
    EXPECT("boot session SH2 may NOT advance the claim counter",
           lt_mcounter_update(&t.lt, CIWI_TR01_CLAIM_COUNTER) == LT_L3_UNAUTHORIZED);
    EXPECT("boot session SH2 may NOT rewrite the policy",
           lt_r_config_erase(&t.lt) == LT_L3_UNAUTHORIZED);
    lt_session_abort(&t.lt);
    t.session_open = false;
    EXPECT("factory session SH0 (public key) still opens",
           lt_verify_chip_and_start_secure_session(&t.lt, lt_sh0priv_prod0, lt_sh0pub_prod0,
                                                   TR01_PAIRING_KEY_SLOT_INDEX_0) == LT_OK);
    EXPECT("SH0 may NOT sign", lt_ecc_ecdsa_sign(&t.lt, CIWI_TR01_ANCHOR_SLOT, dg, 32, sig) ==
                                   LT_L3_UNAUTHORIZED);
    EXPECT("SH0 may NOT erase the anchor key",
           lt_ecc_key_erase(&t.lt, CIWI_TR01_ANCHOR_SLOT) == LT_L3_UNAUTHORIZED);
    EXPECT("SH0 may NOT erase or rewrite the policy", lt_r_config_erase(&t.lt) == LT_L3_UNAUTHORIZED);
    EXPECT("SH0 may NOT use the PIN slots (no PIN guessing)",
           lt_mac_and_destroy(&t.lt, 0, md, md_out) == LT_L3_UNAUTHORIZED);
    static const uint8_t k32[32] = {1};
    EXPECT("SH0 may NOT write pairing slot 3",
           lt_pairing_key_write(&t.lt, k32, TR01_PAIRING_KEY_SLOT_INDEX_3) == LT_L3_UNAUTHORIZED);
    lt_session_abort(&t.lt);
    t.session_open = false;
    EXPECT("back to LOCKED, then READY with the PIN",
           ciwi_tr01_start(&t) == LT_OK &&
               ciwi_tr01_unlock(&t, "2468", bind, sizeof(bind), NULL) == LT_OK);
    EXPECT("SH1 may NOT import a key (key store disabled)",
           lt_ecc_key_store(&t.lt, TR01_ECC_SLOT_1, TR01_CURVE_P256, k32) == LT_L3_UNAUTHORIZED);
#endif

    printf("== 5. PIN: repeated entries, change, binding ==\n");
    int ok3 = 1;
    for (int i = 0; i < 3; i++) {
        ok3 &= ciwi_tr01_lock(&t) == LT_OK &&
               ciwi_tr01_unlock(&t, "2468", bind, sizeof(bind), NULL) == LT_OK;
    }
    EXPECT("lock + unlock 3 times in a row (slots re-armed)", ok3);
    EXPECT("change PIN 2468 -> 13579 (ends READY)",
           ciwi_tr01_change_pin(&t, "2468", "13579", bind, sizeof(bind)) == LT_OK &&
               t.state == CIWI_TR01_STATE_READY);
    EXPECT("same anchor key after the change", memcmp(ciwi_tr01_ciwi_id(&t), first_id, 32) == 0);
    ciwi_tr01_lock(&t);
    EXPECT("old PIN no longer works", ciwi_tr01_unlock(&t, "2468", bind, sizeof(bind), NULL) != LT_OK);
    EXPECT("right PIN with another MCU's ID is rejected",
           ciwi_tr01_unlock(&t, "13579", (const uint8_t *)"other-mcu-id", 12, NULL) != LT_OK);
    EXPECT("non-digit PIN refused",
           ciwi_tr01_unlock(&t, "12a4", bind, sizeof(bind), NULL) == LT_PARAM_ERR);
    EXPECT("new PIN works", ciwi_tr01_unlock(&t, "13579", bind, sizeof(bind), &left) == LT_OK &&
                                left == 10);

    printf("== 6. Anchor Protocol messages (anchor_selftest, shared with phase 7) ==\n");
    J = fopen(out, "w");
    if (!J) return 1;
    anchor_selftest_io_t io = {.emit = emit_file, .confirm = auto_confirm, .result = result};
    anchor_selftest_run(&t, &io);
    fclose(J);

    printf("== 7. Damaged or foreign keys in the MCU: nothing is overwritten ==\n");
    uint8_t boot_rec[64], sh1_rec[TR01_KS_MAX], bad[64];
    uint16_t boot_len = 0, sh1_len = 0;
    tr01_keystore_load(TR01_KS_BOOT, boot_rec, sizeof(boot_rec), &boot_len);
    tr01_keystore_load(TR01_KS_SH1, sh1_rec, sizeof(sh1_rec), &sh1_len);
    memset(bad, 0x5a, sizeof(bad));
    tr01_keystore_save(TR01_KS_BOOT, bad, sizeof(bad));
    ciwi_tr01_lock(&t);
    EXPECT("wrong boot key: state FOREIGN", t.state == CIWI_TR01_STATE_FOREIGN);
    EXPECT("provisioning refused (not a new CiWi)",
           ciwi_tr01_provision(&t, "1111", bind, sizeof(bind)) != LT_OK);
    tr01_keystore_save(TR01_KS_BOOT, boot_rec, boot_len);
    EXPECT("original boot key opens again", ciwi_tr01_start(&t) == LT_OK);
    uint8_t tampered[TR01_KS_MAX];
    memcpy(tampered, sh1_rec, sh1_len);
    tampered[20] ^= 1;                                /* a bit of the encrypted SH1 key */
    tr01_keystore_save(TR01_KS_SH1, tampered, sh1_len);
    EXPECT("tampered SH1 record: right PIN, but no signing session",
           ciwi_tr01_unlock(&t, "13579", bind, sizeof(bind), NULL) == LT_CRYPTO_ERR &&
               t.state == CIWI_TR01_STATE_LOCKED);
    tr01_keystore_save(TR01_KS_SH1, sh1_rec, sh1_len);
    EXPECT("restored record unlocks", ciwi_tr01_unlock(&t, "13579", bind, sizeof(bind), NULL) == LT_OK);

    printf("== 8. Ten wrong PINs: the signing key is gone for good ==\n");
    ciwi_tr01_lock(&t);
    int wrong_all = 1;
    for (int i = 0; i < CIWI_PIN_ATTEMPTS; i++) {
        wrong_all &= ciwi_tr01_unlock(&t, "9999", bind, sizeof(bind), &left) != LT_OK;
    }
    EXPECT("10 wrong PINs, 0 left", wrong_all && left == 0);
    EXPECT("the right PIN no longer unlocks",
           ciwi_tr01_unlock(&t, "13579", bind, sizeof(bind), NULL) != LT_OK &&
               t.state == CIWI_TR01_STATE_LOCKED);

    ciwi_tr01_close(&t);
    printf("\n%d passed, %d failed. Vectors: %s\n", g_pass, g_fail, out);
    return g_fail ? 1 : 0;
}
