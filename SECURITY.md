# CiWi security notes

How the CiWi firmware uses the TROPIC01 secure element, what it protects against and what is still
open. Code: [`tropic/tr01_ciwi.c`](tropic/tr01_ciwi.c) (section "Threat model").

## Keys

| Key | Where | Protection | Can do |
| --- | --- | --- | --- |
| Anchor key (P-256) | TROPIC01, ECC slot 0 | Generated inside the chip, never leaves it, import disabled | Sign Claim, Bind and NftClaim |
| SH1, signing pairing key (X25519) | STM32 flash | **Encrypted** with a key released only by the right PIN | Open the session that may use the anchor key and the claim counter, and change the access policy |
| SH2, boot pairing key (X25519) | STM32 flash | Plain (RDP) | Open the session that may only check the PIN |
| PIN | TROPIC01 MAC-and-Destroy (slots 0-9) and user data slot 511 | 10 attempts in total, counted inside the chip | Release the PIN key |
| SH0, factory key | Published in libtropic | — | Nothing beyond public reads, once the CiWi access policy is written |

Boot: TROPIC01 → boot session (SH2, **LOCKED**) → PIN checked in the chip → SH1 decrypted in RAM →
signing session (SH1, **READY**). Locking (MENU held, auto-lock) closes the signing session. After 10
wrong PINs the PIN key, and with it SH1, is gone for good: the anchor key can no longer be used, and
the CiWi must be re-provisioned on a new pairing slot and registered again.

The access policy (R-config) is written at provisioning and enforced by TROPIC01. Only the SH1
session can rewrite it, so the public factory key cannot undo it. Checked on the TROPIC01 model in
`tests/host` (section 4b): neither SH0 nor SH2 can sign, move the counter, erase the anchor key or
rewrite the policy, and SH0 cannot touch the PIN slots.

## Why two pairing keys: side channels on repeated scalar multiplications

Every secure session starts with an X25519 handshake that uses the same host pairing key. On the
STM32 this runs in trezor-crypto without scalar blinding, and the key sits in flash. An attacker
holding the device can probe power or electromagnetic emissions and trigger the operation as often
as they like.

Roche, Imbert and Lomné show that, on curves whose order is close to a power of 2 (secp256k1,
Curve25519, Ed25519), scalar blinding with a 64-bit random factor no longer protects a key that is
used many times: about 10 000 measured operations are enough to recover it, and the blinding factor
must be larger than half the key size (more than 128 bits for 256-bit keys).

> T. Roche, L. Imbert, V. Lomné. *Side-channel Attacks on Blinded Scalar Multiplications Revisited.*
> CARDIS 2019. NinjaLab and LIRMM (CNRS, University of Montpellier).
> [IACR ePrint 2019/1220](https://eprint.iacr.org/2019/1220)

So the key that can sign (SH1) is never usable without the PIN, and its handshake runs once per
unlock, not on demand. The key that is always available (SH2) can at most try 10 PINs, a limit
enforced by TROPIC01 itself.

## Open items

- **Blinding inside TROPIC01.** The chip's own static X25519 key is used in every handshake and an
  attacker with the chip can request handshakes freely. Ask Tropic Square for the blinding size used
  for X25519, Ed25519 and P-256 (R > 128 bits per the paper), and for secp256k1 when it arrives.
- **Bitcoin keys.** The wallet seed (phases 5-6) still lives on the STM32, with the v0.2 PIN, and
  every address shown recomputes a public key from the same secp256k1 private key. Next steps: wrap
  the seed with the PIN key, cache public keys, and move to TROPIC01 once it supports secp256k1.
- **Flash read-out protection** (RDP) is not set on development boards. Without it SH2 and the
  encrypted SH1 record can be read with a debugger: SH2 still only gives 10 PIN attempts.
- **Shield nodes** keep their pairing key on the Raspberry Pi and stay in the field unattended; the
  same split (or a measurement MCU) applies there.
