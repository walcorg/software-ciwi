/**
 * @file    anchor_selftest.c
 * @brief   End-to-end Anchor Protocol test. See anchor_selftest.h. TEST ONLY.
 */
#include "anchor_selftest.h"

#include <stdio.h>
#include <string.h>

#include "anchor_ciwi.h"
#include "anchor_eip712.h"
#include "bignum.h"
#include "ecdsa.h"
#include "nist256p1.h"

/* Fixed TEST shield key. Public: never use it for anything but this test. */
static const uint8_t TEST_SHIELD_PRIV[32] = {
    0x51, 0x9b, 0x42, 0x3d, 0x71, 0x5f, 0x8b, 0x58, 0x1f, 0x4f, 0xa8, 0xee, 0x59, 0xf4, 0x77, 0x1a,
    0x5b, 0x44, 0xc8, 0x13, 0x0b, 0x4e, 0x3e, 0xac, 0xca, 0x54, 0xa5, 0x6d, 0xda, 0x72, 0xb4, 0x64};

static const anchor_selftest_io_t *IO;
static int g_fail;

static void check(const char *name, bool ok)
{
    if (!ok) g_fail++;
    IO->result(name, ok);
}

static void fill(uint8_t *b, size_t n, uint8_t seed)
{
    for (size_t i = 0; i < n; i++) b[i] = (uint8_t)(seed + 7 * i);
}

static void shield_sign(const uint8_t digest[32], uint8_t sig[64])
{
    bignum256 s, ns;
    ecdsa_sign_digest(&nist256p1, TEST_SHIELD_PRIV, digest, sig, NULL, NULL);
    bn_read_be(sig + 32, &s);
    if (bn_is_less(&nist256p1.order_half, &s)) {
        bn_subtract(&nist256p1.order, &s, &ns);
        bn_write_be(&ns, sig + 32);
    }
}

/* ------------------------------------------------------------- JSON output */

static void jraw(const char *s) { IO->emit(s); }

static void jhex(const char *key, const uint8_t *b, size_t n, int last)
{
    char buf[48 + 2 * 64 + 8];
    int p = snprintf(buf, sizeof(buf), "    \"%s\": \"0x", key);
    for (size_t i = 0; i < n && p < (int)sizeof(buf) - 8; i++) {
        p += snprintf(buf + p, sizeof(buf) - p, "%02x", b[i]);
    }
    snprintf(buf + p, sizeof(buf) - p, "\"%s\n", last ? "" : ",");
    IO->emit(buf);
}

static void jnum(const char *key, uint64_t v, int last)
{
    char num[24], buf[64];
    int i = sizeof(num) - 1;
    num[i] = '\0';
    do {
        num[--i] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    snprintf(buf, sizeof(buf), "    \"%s\": \"%s\"%s\n", key, num + i, last ? "" : ",");
    IO->emit(buf);
}

/* ------------------------------------------------------------- the test */

int anchor_selftest_run(ciwi_tr01_t *t, const anchor_selftest_io_t *io)
{
    IO = io;
    g_fail = 0;

    const uint8_t *apub = ciwi_tr01_anchor_pub(t);
    const uint8_t *cid = ciwi_tr01_ciwi_id(t);
    check("anchor key available", apub && cid);
    if (!apub) return g_fail;

    anchor_domain_t dom = {.chain_id = ANCHOR_CHAIN_AMOY};
    fill(dom.verifying_contract, 20, 0x10);
    uint8_t shield_pub65[65], shield_pub[64], shield_id[32], owner[20], other[20];
    ecdsa_get_public_key65(&nist256p1, TEST_SHIELD_PRIV, shield_pub65);
    memcpy(shield_pub, shield_pub65 + 1, 64);
    anchor_device_id(CIWI_CURVE_P256, shield_pub, shield_id);
    fill(owner, 20, 0xc3);
    fill(other, 20, 0x99);
    uint8_t ds[32], sh[32];
    anchor_domain_separator(&dom, ds);

    /* ---- CaptureReceipt -> Claim */
    anchor_capture_receipt_t rc = {.version = 2, .curve = 1, .counter = 41,
                                   .window_start = 1767225600, .window_end = 1767226800,
                                   .node_count = 3, .co2_mg = 123456789};
    memcpy(rc.shield_id, shield_id, 32);
    memcpy(rc.owner, owner, 20);
    fill(rc.data_root, 32, 1);
    fill(rc.challenge, 32, 2);
    fill(rc.response_hash, 32, 3);
    fill(rc.nonce, 16, 4);
    fill(rc.fw_hash, 32, 5);
    uint8_t rc_digest[32], rc_sig[64];
    anchor_hash_capture_receipt(&rc, sh);
    anchor_digest(ds, sh, rc_digest);
    shield_sign(rc_digest, rc_sig);
    check("receipt: shield signature verified",
          anchor_check_receipt(&dom, &rc, shield_pub, rc_sig, owner) == ANCHOR_OK);
    check("receipt for another owner refused",
          anchor_check_receipt(&dom, &rc, shield_pub, rc_sig, other) == ANCHOR_ERR_WRONG_OWNER);
    anchor_capture_receipt_t rc2 = rc;
    rc2.co2_mg += 1;
    check("tampered receipt refused",
          anchor_check_receipt(&dom, &rc2, shield_pub, rc_sig, owner) == ANCHOR_ERR_SHIELD_SIG);

    anchor_claim_t cl;
    uint8_t cl_sig[64];
    bool ok = io->confirm("Claim CO2in", "shield ...test, window 20 min, 123.456789 kg CO2");
    check("owner approved the claim", ok);
    check("claim signed by TROPIC01", ok && anchor_sign_claim(t, &dom, &rc, &cl, cl_sig) == ANCHOR_OK);

    /* ---- Bind */
    anchor_bind_t bd;
    memcpy(bd.shield_id, shield_id, 32);
    memcpy(bd.ciwi_id, cid, 32);
    memcpy(bd.owner, owner, 20);
    memset(bd.nonce, 0, 32);
    bd.nonce[31] = 7;
    uint8_t bd_digest[32], bd_ssig[64], bd_csig[64];
    anchor_hash_bind(&bd, sh);
    anchor_digest(ds, sh, bd_digest);
    shield_sign(bd_digest, bd_ssig);
    check("bind: shield signature verified",
          anchor_check_bind(&dom, t, &bd, shield_pub, bd_ssig) == ANCHOR_OK);
    anchor_bind_t bd2 = bd;
    bd2.ciwi_id[0] ^= 1;
    check("bind for another CiWi refused",
          anchor_check_bind(&dom, t, &bd2, shield_pub, bd_ssig) == ANCHOR_ERR_WRONG_CIWI);
    ok = io->confirm("Bind shield", "bind the test shield to this CiWi");
    check("owner approved the bind", ok);
    check("bind signed by TROPIC01", ok && anchor_sign_bind(t, &dom, &bd, bd_csig) == ANCHOR_OK);

    /* ---- RegistryApproval (signed by the platform; hashed here for conformance) */
    anchor_registry_approval_t ra = {.curve = 1, .expiry = 1767312000};
    memcpy(ra.ciwi_id, cid, 32);
    memcpy(ra.pub_x, apub, 32);
    memcpy(ra.pub_y, apub + 32, 32);
    check("chip serial hash", ciwi_tr01_chip_serial_hash(t, ra.chip_serial_hash) == LT_OK);
    memcpy(ra.owner, owner, 20);
    uint8_t ra_hash[32];
    anchor_hash_registry_approval(&ra, ra_hash);

    /* ---- NFT profile: WindowRecord -> NftClaim */
    anchor_window_record_t wr = {.version = 2, .curve = 1, .token_id = 1,
                                 .window_start = 1767225600, .window_end = 1767226800,
                                 .challenge_block = 18000000};
    memcpy(wr.shield_id, shield_id, 32);
    fill(wr.fw_hash, 32, 6);
    fill(wr.challenge, 32, 7);
    fill(wr.response_hash, 32, 8);
    fill(wr.data_root, 32, 9);
    fill(wr.nonce, 16, 10);
    uint8_t wr_digest[32], wr_sig[64], meta[32], nc_sig[64];
    anchor_hash_window_record(&wr, sh);
    anchor_digest(ds, sh, wr_digest);
    shield_sign(wr_digest, wr_sig);
    fill(meta, 32, 11);
    check("window record: shield signature verified",
          anchor_check_window_record(&dom, &wr, shield_pub, wr_sig) == ANCHOR_OK);
    anchor_nft_claim_t nc;
    ok = io->confirm("Mint NFT #1", "window 20 min, data and metadata hash on the UART");
    check("owner approved the mint", ok);
    check("NFT claim signed by TROPIC01",
          ok && anchor_sign_nft_claim(t, &dom, &wr, meta, owner, &nc, nc_sig) == ANCHOR_OK);

    /* ---- JSON (tests/host/verify.js) */
    jraw("{\n  \"domain\": {\n");
    jnum("chainId", dom.chain_id, 0);
    jhex("verifyingContract", dom.verifying_contract, 20, 0);
    jhex("separator", ds, 32, 1);
    jraw("  },\n  \"keys\": {\n");
    jhex("shieldPub", shield_pub, 64, 0);
    jhex("shieldId", shield_id, 32, 0);
    jhex("ciwiPub", apub, 64, 0);
    jhex("ciwiId", cid, 32, 1);
    jraw("  },\n  \"captureReceipt\": {\n");
    jnum("version", rc.version, 0);
    jnum("curve", rc.curve, 0);
    jhex("shieldId", rc.shield_id, 32, 0);
    jhex("owner", rc.owner, 20, 0);
    jnum("counter", rc.counter, 0);
    jnum("windowStart", rc.window_start, 0);
    jnum("windowEnd", rc.window_end, 0);
    jnum("nodeCount", rc.node_count, 0);
    jnum("co2Mg", rc.co2_mg, 0);
    jhex("dataRoot", rc.data_root, 32, 0);
    jhex("challenge", rc.challenge, 32, 0);
    jhex("responseHash", rc.response_hash, 32, 0);
    jhex("nonce", rc.nonce, 16, 0);
    jhex("fwHash", rc.fw_hash, 32, 0);
    jhex("_digest", rc_digest, 32, 0);
    jhex("_shieldSig", rc_sig, 64, 1);
    jraw("  },\n  \"claim\": {\n");
    jhex("receiptHash", cl.receipt_hash, 32, 0);
    jhex("ciwiId", cl.ciwi_id, 32, 0);
    jhex("owner", cl.owner, 20, 0);
    jnum("ciwiCounter", cl.ciwi_counter, 0);
    jhex("_ciwiSig", cl_sig, 64, 1);
    jraw("  },\n  \"bind\": {\n");
    jhex("shieldId", bd.shield_id, 32, 0);
    jhex("ciwiId", bd.ciwi_id, 32, 0);
    jhex("owner", bd.owner, 20, 0);
    jhex("nonce", bd.nonce, 32, 0);
    jhex("_digest", bd_digest, 32, 0);
    jhex("_shieldSig", bd_ssig, 64, 0);
    jhex("_ciwiSig", bd_csig, 64, 1);
    jraw("  },\n  \"registryApproval\": {\n");
    jhex("ciwiId", ra.ciwi_id, 32, 0);
    jnum("curve", ra.curve, 0);
    jhex("pubX", ra.pub_x, 32, 0);
    jhex("pubY", ra.pub_y, 32, 0);
    jhex("chipSerialHash", ra.chip_serial_hash, 32, 0);
    jhex("owner", ra.owner, 20, 0);
    jnum("expiry", ra.expiry, 0);
    jhex("_structHash", ra_hash, 32, 1);
    jraw("  },\n  \"windowRecord\": {\n");
    jnum("version", wr.version, 0);
    jnum("curve", wr.curve, 0);
    jhex("shieldId", wr.shield_id, 32, 0);
    jhex("fwHash", wr.fw_hash, 32, 0);
    jnum("tokenId", wr.token_id, 0);
    jnum("windowStart", wr.window_start, 0);
    jnum("windowEnd", wr.window_end, 0);
    jnum("challengeBlock", wr.challenge_block, 0);
    jhex("challenge", wr.challenge, 32, 0);
    jhex("responseHash", wr.response_hash, 32, 0);
    jhex("dataRoot", wr.data_root, 32, 0);
    jhex("nonce", wr.nonce, 16, 0);
    jhex("_digest", wr_digest, 32, 0);
    jhex("_shieldSig", wr_sig, 64, 1);
    jraw("  },\n  \"nftClaim\": {\n");
    jhex("recordHash", nc.record_hash, 32, 0);
    jhex("metadataHash", nc.metadata_hash, 32, 0);
    jhex("ciwiId", nc.ciwi_id, 32, 0);
    jhex("owner", nc.owner, 20, 0);
    jhex("_ciwiSig", nc_sig, 64, 1);
    jraw("  }\n}\n");

    return g_fail;
}
