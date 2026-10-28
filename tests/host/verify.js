// verify.js — independent check of vectors.json produced by ciwi_host.
//
// Recomputes every EIP-712 digest with ethers.js (TypedDataEncoder), the identifiers with keccak256,
// and verifies every P-256 signature with @noble/curves (lowS enforced, as on chain).
//
//   npm install            (once, in tests/host)
//   node verify.js vectors.json
"use strict";
const fs = require("fs");
const path = require("path");
const { TypedDataEncoder, keccak256, solidityPacked, getBytes } = require("ethers");
const { p256 } = require("@noble/curves/p256");

const v = JSON.parse(fs.readFileSync(process.argv[2] || "vectors.json", "utf8"));
const spec = JSON.parse(
  fs.readFileSync(path.join(__dirname, "anchor-types.json"), "utf8")
);

const domain = {
  name: spec.domain.name,
  version: spec.domain.version,
  chainId: BigInt(v.domain.chainId),
  verifyingContract: v.domain.verifyingContract,
};
const core = spec.core;
const nft = spec.nftProfile;

let pass = 0, fail = 0;
function check(name, ok) {
  if (ok) { pass++; console.log("  PASS  " + name); }
  else { fail++; console.log("  FAIL  " + name); }
}
const strip = (o) => Object.fromEntries(Object.entries(o).filter(([k]) => !k.startsWith("_")));
const digest = (name, types, msg) =>
  TypedDataEncoder.hash(domain, { [name]: types[name] }, strip(msg));
const structHash = (name, types, msg) =>
  TypedDataEncoder.hashStruct(name, { [name]: types[name] }, strip(msg));

function p256ok(pubHex, digestHex, sigHex) {
  const pub = getBytes("0x04" + pubHex.slice(2));
  const sig = getBytes(sigHex);
  return p256.verify(sig, getBytes(digestHex), pub, { prehash: false, lowS: true });
}

console.log("== EIP-712 conformance (C vs ethers) ==");
check("domain separator", TypedDataEncoder.hashDomain(domain) === v.domain.separator);

const id = (pub) =>
  keccak256(solidityPacked(["uint8", "bytes32", "bytes32"], [1, "0x" + pub.slice(2, 66), "0x" + pub.slice(66)]));
check("shieldId = keccak256(curve||X||Y)", id(v.keys.shieldPub) === v.keys.shieldId);
check("ciwiId = keccak256(curve||X||Y)", id(v.keys.ciwiPub) === v.keys.ciwiId);

const rcDigest = digest("CaptureReceipt", core, v.captureReceipt);
check("CaptureReceipt digest", rcDigest === v.captureReceipt._digest);
check("Claim.receiptHash = struct hash of the receipt",
  structHash("CaptureReceipt", core, v.captureReceipt) === v.claim.receiptHash);
check("Bind digest", digest("Bind", core, v.bind) === v.bind._digest);
check("RegistryApproval struct hash",
  structHash("RegistryApproval", core, v.registryApproval) === v.registryApproval._structHash);
const wrDigest = digest("WindowRecord", nft, v.windowRecord);
check("WindowRecord digest", wrDigest === v.windowRecord._digest);
check("NftClaim.recordHash = struct hash of the record",
  structHash("WindowRecord", nft, v.windowRecord) === v.nftClaim.recordHash);

console.log("== Signatures (P-256, low-s) ==");
check("shield sig on CaptureReceipt", p256ok(v.keys.shieldPub, rcDigest, v.captureReceipt._shieldSig));
check("CiWi (TROPIC01) sig on Claim",
  p256ok(v.keys.ciwiPub, digest("Claim", core, v.claim), v.claim._ciwiSig));
check("shield sig on Bind", p256ok(v.keys.shieldPub, v.bind._digest, v.bind._shieldSig));
check("CiWi (TROPIC01) sig on Bind", p256ok(v.keys.ciwiPub, v.bind._digest, v.bind._ciwiSig));
check("shield sig on WindowRecord", p256ok(v.keys.shieldPub, wrDigest, v.windowRecord._shieldSig));
check("CiWi (TROPIC01) sig on NftClaim",
  p256ok(v.keys.ciwiPub, digest("NftClaim", nft, v.nftClaim), v.nftClaim._ciwiSig));
const tampered = { ...v.claim, ciwiCounter: String(BigInt(v.claim.ciwiCounter) + 1n) };
check("changed ciwiCounter breaks the CiWi sig",
  !p256ok(v.keys.ciwiPub, digest("Claim", core, tampered), v.claim._ciwiSig));

console.log(`\n${pass} passed, ${fail} failed.`);
process.exit(fail ? 1 : 0);
