# Protocol v2: standard Noise XX and the FBY datagram binding

Status: **opt-in v2 handshake, record layer and the official noise-c vector
landed; wire default remains v1.** This document is the binding specification and
the checklist for finishing the interoperable protocol. It is deliberately
explicit about what is implemented.

## 1. What v2 is

- Noise pattern: `XX` with `25519`, `ChaChaPoly`, `SHA256`.
- Protocol name: `Noise_XX_25519_ChaChaPoly_SHA256`.
- Initial hash: the name is exactly 32 bytes, which does not exceed the SHA-256
  digest size, so the standard `h0` is the name itself zero padded, not its
  digest. The FBY name is different on purpose and its v1 path keeps the frozen
  hashed form.
- Prologue: empty by default. The only application prologue is the signed ticket
  hash (32 bytes) when a ticket is presented; both sides must mix the same bytes.
  The prologue updates `h` only: the chaining key stays at `h0`, which v1 did not
  do (v1 mixed it into `ck`, frozen with its KATs).
- DH: the raw RFC 7748 X25519 output. The all-zero low-order result is rejected.
  `Crypto::X25519RawSharedSecret` is the primitive; the v1 BLAKE2b post-hash is
  not used on this path.
- Nonces: the ChaChaPoly nonce is the 64-bit counter encoded as 32 zero bits
  followed by the little-endian counter (RFC 7539 layout). v1 froze a different
  layout; v2 follows the specification for both handshake and transport.
- Transcript: the standard Noise token sequence. Message 1 is `e`; message 2 is
  `e, ee, s, es` plus the payload; message 3 is `s, se` plus the payload. The
  payload is optional (`WriteHandshake`/`ReadHandshake`), and an empty encrypted
  payload still produces a 16-byte tag whenever a handshake key exists.

`SecureChannelConfig::ProtocolV2 = true` selects this transcript. The
per-session header version becomes 2; v1 peers are not silently downgraded.

## 2. FBY envelope (outer, not part of the Noise transcript)

Every handshake datagram starts with the 8-byte FBY header, then the complete
Noise message bytes:

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | magic `0x31435346` ("FSC1", kept for routing) |
| 4 | 2 | envelope version (2 for v2) |
| 6 | 1 | type: 1 = msg1, 2 = msg2, 3 = msg3, 4 = cookie challenge |
| 7 | 1 | flags: bit 0 = cookie present on msg1 |

A standard Noise peer can use the inner message by stripping this header; a peer
that wants the cookie/session layer needs the binding in this document. The
cookie challenge is an FBY extension before message 1 and is not in the Noise
transcript.

## 3. Record layer

v1 keeps the frozen FBY framing: `[sequence u64 LE][ciphertext][tag]`, nonce =
sequence in the v1 nonce layout, AAD = 8-byte session id, keys split with HKDF and
re-derived every `RecordsPerKey` with the `FBY-REKEY` label. The receiver keeps the
previous epoch.

v2 uses the Noise transport with an explicit datagram binding:

```
[epoch u64 LE][sequence u64 LE][ciphertext][tag]
```

- `sequence` is the per-direction transport message number: it is the ChaChaPoly
  nonce (standard layout), it increments for every datagram and it is never
  reused under the same key.
- `epoch` selects the key. Epoch 0 is the split key; epoch `e + 1` is
  `NoiseRekey(epoch e)`, the Noise `REKEY()` rule (32 zero bytes encrypted under
  the maximum nonce with empty AAD, first 32 bytes of ciphertext).
- AAD is the complete 16-byte header. A rewritten epoch or sequence therefore
  fails the tag, and the session id and the `FBY-REKEY` label are not part of a
  v2 record.
- The sender rekeys when `sequence / RecordsPerKey` changes; the receiver follows
  the epoch from the header and does not need the sender's `RecordsPerKey`.
- The receiver derives keys for a new epoch into a temporary state and commits
  only after the tag succeeds, so a forged header cannot advance the chain.
- Out-of-order delivery is accepted inside the 64-record replay window, and a
  record from the immediately previous epoch is decryptable; older epochs are
  rejected as `Replayed`.
- A header may not claim an epoch more than 64 epochs ahead of the receiver's
  current epoch (`kSecureMaxEpochJump`); derivations are bounded per datagram.

`NoiseRekey` exposes the REKEY primitive for vectors and diagnostics. The record
header carries no version or magic: the handshake envelope version selects the
channel parser, and mixing versions fails the handshake before any record is
accepted.

The record-layer test is `record_layer_v2`. Official Noise vectors and the
`noise-c` differential are still open, so v2 remains opt-in.

## 4. Versioning and deprecation

- v2 is opt-in per `SecureChannelConfig`; the default stays v1 until the official
  vectors and the differential pass.
- A v1 channel rejects a v2 message with `BadVersion` (and vice versa); there is
  no negotiation that can fall back from v2 to v1.
- Planned deprecation window: v1 accepted by the session server for one release
  after v2 becomes the default; the server logs and counts v1 sessions; after the
  window, v1 is rejected with a clear `BadVersion`.
- The envelope magic is not changed; the version field carries the decision.

## 5. Ticket signatures: Ed25519 v2

v1 tickets use Monocypher EdDSA over BLAKE2b. Protocol v2 tickets must use
RFC 8032 Ed25519 with the same 32-byte seed, 32-byte public key and 64-byte
signature sizes, plus a domain-separation prefix in the signed message (for
example `FBY-TICKET-V2\0`). The identity store gains a version 3 record with an
explicit algorithm field; v2 tickets are rejected by v1 verifiers and vice
versa. This requires vendoring `monocypher-ed25519` (or an equivalent audited
implementation) and adding RFC 8032 vectors; it is **open**.

## 6. Optional IK/PSK

**Decision for 1.0: no IK and no PSK.** LAN sessions use the same XX transcript;
the lobby ticket already authenticates the host's static key and the session id,
so a second shared secret would add provisioning and rotation work without a
threat model that needs it. A future LAN mode may use `XXpsk3`, but only with a
distinct protocol name, official vectors for that name and a written PSK
provisioning/rotation design; it must not be smuggled into XX as an optional
field.

## 7. Verification plan

1. Official `Noise_XX_25519_ChaChaPoly_SHA256` vectors (initiator/responder
   static and ephemeral keys, three messages, split keys) as a test whose expected
   bytes are copied from the Noise specification/noise-c repository, not
   generated by this implementation.
2. Differential harness: build `noise-c` in a separate job, run a fixed transcript
   through both implementations and compare every message byte and split key.
3. Parser fuzzing for the envelope: length, type, cookie flags, truncated tags,
   oversized payloads, version confusion.
4. Negative tests: ticket prologue mismatch, downgrade attempt, replayed record,
   epoch rollover, low-order public key.
5. Only after 1-4 pass: make v2 the default, start the v1 deprecation window and
   update `security.md` from "opt-in" to "interoperable".

Current checks: `noise_kats` includes the standard initial hash, the RFC 7748
X25519 vector, the raw-vs-hashed DH distinction and a deterministic v2 handshake
whose two sides agree and whose header carries version 2. `noise_conformance`
replays the noise-c reference vector: all three handshake messages, both
payloads, the handshake hash and the three transport ciphertexts must match the
vendored bytes, and the transport ciphertexts verify the split keys under both
directions and two nonces. `record_layer_v2` covers the v2 record header, the
REKEY chain, reorder/loss, epoch rollover, the rekey boundary, the epoch-jump cap
and tampering.

The `noise-differential` workflow rebuilds noise-c at the pinned commit, replays
the same inputs through it and compares every message, the hash and the transport
ciphertexts with the vendored vector (`Source/Tests/NoiseC/`). The engine test and
the job share the same expected bytes, so either side drifting fails.

## 8. Migration checklist

- [x] Standard protocol name, padded-name initial hash and the default prologue
  rule.
- [x] Raw X25519 for the Noise path; RFC 7748 vector.
- [x] Opt-in v2 envelope version and no silent downgrade.
- [x] Datagram binding documented (this file).
- [x] Standard record layer with Noise `REKEY()` and epoch binding
  (`record_layer_v2`).
- [x] Official Noise XX vector from noise-c (`noise_conformance`) and the
  reproducible noise-c differential job (`noise-differential`).
- [x] Parser fuzzing (`fuzz_noise` sweeps handshake and records on v1 and v2).
- [ ] Ed25519 v2 tickets and identity store v3.
- [x] v2 default (`SecureChannelConfig::ProtocolV2` is true; the session
  transport uses v2), v1 opt-out for frozen vectors and legacy peers, and the
  security status note in `security.md`.
- [x] IK/PSK decision for LAN mode (rejected for 1.0; see §6).
