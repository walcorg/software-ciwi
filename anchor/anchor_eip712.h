/**
 * @file    anchor_eip712.h
 * @brief   WALCORG Anchor Protocol v0.2: EIP-712 hashing of every signed message.
 *
 * Mirrors eip712/types.json in walcorg/anchor-protocol. The CiWi, the shield, the platform and the
 * Solidity contracts must produce the same 32-byte digests; tests/host checks this module against
 * ethers.js (TypedDataEncoder).
 *
 * Conventions: integers are host values; bytes32/bytes16/address are big-endian byte arrays as they
 * appear on chain.
 */
#ifndef ANCHOR_EIP712_H
#define ANCHOR_EIP712_H

#include <stdint.h>

#define ANCHOR_DOMAIN_NAME    "WALCORG Anchor"
#define ANCHOR_DOMAIN_VERSION "0.2"
#define ANCHOR_CHAIN_POLYGON  137u
#define ANCHOR_CHAIN_AMOY     80002u

typedef struct {
    uint64_t chain_id;
    uint8_t verifying_contract[20];
} anchor_domain_t;

/* ------------------------------------------------------------------ core profile */

typedef struct {
    uint8_t version;
    uint8_t curve;
    uint8_t shield_id[32];
    uint8_t owner[20];
    uint64_t counter;
    uint64_t window_start;
    uint64_t window_end;
    uint16_t node_count;
    uint64_t co2_mg;
    uint8_t data_root[32];
    uint8_t challenge[32];
    uint8_t response_hash[32];
    uint8_t nonce[16];
    uint8_t fw_hash[32];
} anchor_capture_receipt_t;

typedef struct {
    uint8_t receipt_hash[32]; /**< struct hash of the CaptureReceipt */
    uint8_t ciwi_id[32];
    uint8_t owner[20];
    uint64_t ciwi_counter;
} anchor_claim_t;

typedef struct {
    uint8_t shield_id[32];
    uint8_t ciwi_id[32];
    uint8_t owner[20];
    uint8_t nonce[32]; /**< uint256, big-endian */
} anchor_bind_t;

typedef struct {
    uint8_t ciwi_id[32];
    uint8_t curve;
    uint8_t pub_x[32];
    uint8_t pub_y[32];
    uint8_t chip_serial_hash[32];
    uint8_t owner[20];
    uint64_t expiry;
} anchor_registry_approval_t;

/* ------------------------------------------------------------------ NFT profile */

typedef struct {
    uint8_t version;
    uint8_t curve;
    uint8_t shield_id[32];
    uint8_t fw_hash[32];
    uint16_t token_id;
    uint64_t window_start;
    uint64_t window_end;
    uint64_t challenge_block;
    uint8_t challenge[32];
    uint8_t response_hash[32];
    uint8_t data_root[32];
    uint8_t nonce[16];
} anchor_window_record_t;

typedef struct {
    uint8_t record_hash[32]; /**< struct hash of the WindowRecord */
    uint8_t metadata_hash[32];
    uint8_t ciwi_id[32];
    uint8_t owner[20];
} anchor_nft_claim_t;

/* ------------------------------------------------------------------ hashing */

void anchor_domain_separator(const anchor_domain_t *d, uint8_t out[32]);

void anchor_hash_capture_receipt(const anchor_capture_receipt_t *m, uint8_t out[32]);
void anchor_hash_claim(const anchor_claim_t *m, uint8_t out[32]);
void anchor_hash_bind(const anchor_bind_t *m, uint8_t out[32]);
void anchor_hash_registry_approval(const anchor_registry_approval_t *m, uint8_t out[32]);
void anchor_hash_window_record(const anchor_window_record_t *m, uint8_t out[32]);
void anchor_hash_nft_claim(const anchor_nft_claim_t *m, uint8_t out[32]);

/** keccak256(0x19 0x01 || domainSeparator || structHash): the value that is signed. */
void anchor_digest(const uint8_t domain_separator[32], const uint8_t struct_hash[32],
                   uint8_t out[32]);

/** keccak256(curve || pubX || pubY): shieldId and ciwiId. */
void anchor_device_id(uint8_t curve, const uint8_t pub_xy[64], uint8_t out[32]);

#endif /* ANCHOR_EIP712_H */
