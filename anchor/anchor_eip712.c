/**
 * @file    anchor_eip712.c
 * @brief   WALCORG Anchor Protocol v0.2: EIP-712 hashing. See anchor_eip712.h.
 */
#include "anchor_eip712.h"

#include <string.h>

#include "sha3.h"

/* Type strings, copied from SPEC.md section 5 and the reference contracts. */
static const char T_DOMAIN[] =
    "EIP712Domain(string name,string version,uint256 chainId,address verifyingContract)";
static const char T_CAPTURE_RECEIPT[] =
    "CaptureReceipt(uint8 version,uint8 curve,bytes32 shieldId,address owner,uint64 counter,"
    "uint64 windowStart,uint64 windowEnd,uint16 nodeCount,uint64 co2Mg,bytes32 dataRoot,"
    "bytes32 challenge,bytes32 responseHash,bytes16 nonce,bytes32 fwHash)";
static const char T_CLAIM[] =
    "Claim(bytes32 receiptHash,bytes32 ciwiId,address owner,uint64 ciwiCounter)";
static const char T_BIND[] = "Bind(bytes32 shieldId,bytes32 ciwiId,address owner,uint256 nonce)";
static const char T_REGISTRY_APPROVAL[] =
    "RegistryApproval(bytes32 ciwiId,uint8 curve,bytes32 pubX,bytes32 pubY,bytes32 chipSerialHash,"
    "address owner,uint64 expiry)";
static const char T_WINDOW_RECORD[] =
    "WindowRecord(uint8 version,uint8 curve,bytes32 shieldId,bytes32 fwHash,uint16 tokenId,"
    "uint64 windowStart,uint64 windowEnd,uint64 challengeBlock,bytes32 challenge,"
    "bytes32 responseHash,bytes32 dataRoot,bytes16 nonce)";
static const char T_NFT_CLAIM[] =
    "NftClaim(bytes32 recordHash,bytes32 metadataHash,bytes32 ciwiId,address owner)";

/* abi.encode of static types: one 32-byte word per field. Max 15 words (CaptureReceipt + type). */
typedef struct {
    uint8_t buf[32 * 15];
    unsigned n;
} enc_t;

static uint8_t *word(enc_t *e)
{
    uint8_t *w = e->buf + 32 * e->n++;
    memset(w, 0, 32);
    return w;
}

static void put_type(enc_t *e, const char *type)
{
    keccak_256((const uint8_t *)type, strlen(type), word(e));
}

static void put_b32(enc_t *e, const uint8_t v[32]) { memcpy(word(e), v, 32); }

/** bytesN (N < 32) is left-aligned, zero-padded on the right. */
static void put_bytesn(enc_t *e, const uint8_t *v, unsigned n) { memcpy(word(e), v, n); }

/** address is right-aligned (left-padded). */
static void put_addr(enc_t *e, const uint8_t v[20]) { memcpy(word(e) + 12, v, 20); }

/** uintN is big-endian, right-aligned. */
static void put_uint(enc_t *e, uint64_t v)
{
    uint8_t *w = word(e);
    for (int i = 31; i >= 24; i--) {
        w[i] = (uint8_t)v;
        v >>= 8;
    }
}

static void finish(const enc_t *e, uint8_t out[32]) { keccak_256(e->buf, 32 * e->n, out); }

void anchor_domain_separator(const anchor_domain_t *d, uint8_t out[32])
{
    enc_t e = {.n = 0};
    put_type(&e, T_DOMAIN);
    put_type(&e, ANCHOR_DOMAIN_NAME); /* string fields are hashed */
    put_type(&e, ANCHOR_DOMAIN_VERSION);
    put_uint(&e, d->chain_id);
    put_addr(&e, d->verifying_contract);
    finish(&e, out);
}

void anchor_hash_capture_receipt(const anchor_capture_receipt_t *m, uint8_t out[32])
{
    enc_t e = {.n = 0};
    put_type(&e, T_CAPTURE_RECEIPT);
    put_uint(&e, m->version);
    put_uint(&e, m->curve);
    put_b32(&e, m->shield_id);
    put_addr(&e, m->owner);
    put_uint(&e, m->counter);
    put_uint(&e, m->window_start);
    put_uint(&e, m->window_end);
    put_uint(&e, m->node_count);
    put_uint(&e, m->co2_mg);
    put_b32(&e, m->data_root);
    put_b32(&e, m->challenge);
    put_b32(&e, m->response_hash);
    put_bytesn(&e, m->nonce, 16);
    put_b32(&e, m->fw_hash);
    finish(&e, out);
}

void anchor_hash_claim(const anchor_claim_t *m, uint8_t out[32])
{
    enc_t e = {.n = 0};
    put_type(&e, T_CLAIM);
    put_b32(&e, m->receipt_hash);
    put_b32(&e, m->ciwi_id);
    put_addr(&e, m->owner);
    put_uint(&e, m->ciwi_counter);
    finish(&e, out);
}

void anchor_hash_bind(const anchor_bind_t *m, uint8_t out[32])
{
    enc_t e = {.n = 0};
    put_type(&e, T_BIND);
    put_b32(&e, m->shield_id);
    put_b32(&e, m->ciwi_id);
    put_addr(&e, m->owner);
    put_b32(&e, m->nonce);
    finish(&e, out);
}

void anchor_hash_registry_approval(const anchor_registry_approval_t *m, uint8_t out[32])
{
    enc_t e = {.n = 0};
    put_type(&e, T_REGISTRY_APPROVAL);
    put_b32(&e, m->ciwi_id);
    put_uint(&e, m->curve);
    put_b32(&e, m->pub_x);
    put_b32(&e, m->pub_y);
    put_b32(&e, m->chip_serial_hash);
    put_addr(&e, m->owner);
    put_uint(&e, m->expiry);
    finish(&e, out);
}

void anchor_hash_window_record(const anchor_window_record_t *m, uint8_t out[32])
{
    enc_t e = {.n = 0};
    put_type(&e, T_WINDOW_RECORD);
    put_uint(&e, m->version);
    put_uint(&e, m->curve);
    put_b32(&e, m->shield_id);
    put_b32(&e, m->fw_hash);
    put_uint(&e, m->token_id);
    put_uint(&e, m->window_start);
    put_uint(&e, m->window_end);
    put_uint(&e, m->challenge_block);
    put_b32(&e, m->challenge);
    put_b32(&e, m->response_hash);
    put_b32(&e, m->data_root);
    put_bytesn(&e, m->nonce, 16);
    finish(&e, out);
}

void anchor_hash_nft_claim(const anchor_nft_claim_t *m, uint8_t out[32])
{
    enc_t e = {.n = 0};
    put_type(&e, T_NFT_CLAIM);
    put_b32(&e, m->record_hash);
    put_b32(&e, m->metadata_hash);
    put_b32(&e, m->ciwi_id);
    put_addr(&e, m->owner);
    finish(&e, out);
}

void anchor_digest(const uint8_t domain_separator[32], const uint8_t struct_hash[32],
                   uint8_t out[32])
{
    uint8_t buf[66];
    buf[0] = 0x19;
    buf[1] = 0x01;
    memcpy(buf + 2, domain_separator, 32);
    memcpy(buf + 34, struct_hash, 32);
    keccak_256(buf, sizeof(buf), out);
}

void anchor_device_id(uint8_t curve, const uint8_t pub_xy[64], uint8_t out[32])
{
    uint8_t buf[65];
    buf[0] = curve;
    memcpy(buf + 1, pub_xy, 64);
    keccak_256(buf, sizeof(buf), out);
}
