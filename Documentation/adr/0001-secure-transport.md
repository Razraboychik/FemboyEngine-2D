# ADR 0001: standard Noise as the long-term secure transport

- Status: accepted (v2 is the shipping default since the current alpha line; v1 remains
  available as an explicit `ProtocolV2 = false` opt-out for frozen vectors and
  legacy peers)
- Date: 2026-10-29 (updated 2026-10-31: v2 default, vectors/differential/fuzz)
- Decision owners: engine networking

The v2 transcript (standard XX name, ticket-hash prologue, raw X25519, standard
record layer) and its binding specification are in
[protocol-v2.md](../design/protocol-v2.md). Official noise-c vectors, the reproducible
differential job and the v1/v2 parser fuzzers are green. Ed25519 v2 tickets stay
open because the tree only vendors classic Monocypher (BLAKE2b EdDSA); IK/PSK is
deliberately **not** part of 1.0 (see the decision in `protocol-v2.md` §6).

## Context

The current tree ships a Noise-XX-shaped handshake named
`Fby_XX_25519_ChaChaPoly_SHA256`. It is deliberately **not** wire compatible
with standard Noise; the KAT package and `Documentation/design/security.md` record the
deviations. Two of them reach below the framing:

1. Every handshake datagram has an 8-byte FBY header (outside the transcript).
2. The prologue is fixed (`FemboyEngine2D-secure-channel-v1` + session id) and
   may carry a 32-byte ticket-hash binding.
3. Message 1 omits the empty-payload AEAD tag that standard XX emits.
4. A cookie challenge precedes message 1 and is not part of the transcript.
5. The record layer is custom: AAD is the session id, keys are rekeyed per
   record epoch with `FBY-REKEY`, and there are no standard Noise transport
   messages.
6. `Crypto::X25519SharedSecret` hashes the raw X25519 output with BLAKE2b before
   it reaches `MixKey`. Standard Noise mixes the raw shared secret (after
   rejecting the all-zero low-order result). This alone makes the derived keys
   incompatible with every Noise implementation.
7. Tickets are signed with Monocypher's EdDSA over BLAKE2b, not RFC 8032
   Ed25519.

For a universal engine, the long-term cost of a private crypto protocol
(interop, audit, third-party integrations, tooling) outweighs the migration
cost. The goal is therefore standard Noise with a documented transport binding,
not a private variant.

## Decision

1. The target handshake is **standard Noise XX**
   (`Noise_XX_25519_ChaChaPoly_SHA256`). `Noise_IK` may be added later for the
   client/server 1-RTT path; `XXpsk3` is the only planned PSK mode.
2. The **FBY datagram envelope** (magic, version, type, fragmentation) stays, but
   it is a transport binding outside the Noise transcript, described in its own
   short specification. Interop means "standard Noise messages inside this
   binding"; a generic Noise library still needs a small adapter, exactly as
   WireGuard has its own envelope.
3. **Prologue**: empty by default. The signed-ticket hash remains an
   application-provided prologue value (allowed by Noise) and keeps the
   ticket-to-handshake binding.
4. **Cookie**: transport-level anti-DoS before message 1, never mixed into the
   handshake hash.
5. **Record layer**: standard Noise transport messages (64-bit nonce counter,
   empty AD, `CipherState` semantics). Rekey uses the Noise `REKEY()` rule at a
   configurable record count; no custom epochs.
6. **X25519**: the shared secret is the raw X25519 output with the all-zero
   check; the BLAKE2b post-hash is removed from the Noise path. Other callers
   that want the hashed form keep an explicitly named helper.
7. **Signatures**: ticket protocol v2 uses RFC 8032 Ed25519. EdDSA-BLAKE2b stays
   only in the frozen v1 ticket format and is documented as non-interoperable.
8. **Suites**: SHA-256 is the primary hash (hardware accelerated, standard
   vectors). BLAKE2b is acceptable only as a second *standard* suite
   (`..._BLAKE2b`) with its own KATs; no custom suites.

## Migration plan

| Phase | Content | Gate |
|---|---|---|
| v1 (now) | FBY XX, frozen for the current line; documented, KAT-locked, no interop claim | current `ctest` |
| v2a | raw X25519 DH, standard message layout (msg1 tag), empty prologue option, version 2 envelope, official XX KATs | `noise_conformance` + all existing tests |
| v2b | standard record layer and `REKEY()` (landed, `record_layer_v2`), official XX vector and noise-c differential (landed, `noise_conformance` + `noise-differential`), endpoint integration (open) | `secure_*` suite + fuzzing |
| v2c | Ed25519 tickets (ticket protocol v2), optional IK/PSK, deprecation window for v1 | `ticket_*` + interop report |

Rules for the migration:

- `kSecureVersion` gates the wire format; v1 and v2 must never silently
  downgrade. A v2 peer rejects v1 after the deprecation date and logs the reason.
- Official Noise test vectors are mandatory before v2 is called standard; the
  frozen FBY KAT stays as a regression for v1 only.
- Differential testing against an independent implementation (`noise-c`) and a
  fuzzer for message parsing are acceptance criteria for v2.
- The ticket binding, cookie, rate limits and identity storage stay; only the
  handshake/record internals change.

## Consequences

- v2 is a wire break and therefore its own package, not part of a patch release.
- Until v2 lands, README and `Documentation/design/security.md` state that the tree is
  not Noise-interoperable and `GCC не прогонялся` for the full gate.
- The FBY envelope specification must be published alongside v2 so third parties
  can write an adapter without reading the engine source.
