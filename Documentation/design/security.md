# Security notes

This document records the security-relevant design of the 0.1.0-alpha.1 tree and, where
the implementation deliberately differs from a published standard, the exact
deviations. It is the companion to `docs/security.md` in the remaining-work plan;
the tree keeps documentation under `Documentation/`.

## Handshake: standard XX by default (v2), FBY XX opt-out

The default session handshake (`SecureChannelConfig::ProtocolV2 = true`) uses the
standard `Noise_XX_25519_ChaChaPoly_SHA256` transcript: standard protocol name,
the ticket hash as the only application prologue, raw RFC 7748 X25519 output and
the standard Noise record layer. The official noise-c vectors and the
reproducible differential job pass ([protocol-v2.md](protocol-v2.md)).

A peer that still needs the frozen FBY transcript must set `ProtocolV2 = false`
explicitly. The FBY transcript uses the standard primitives but is **not wire
compatible** with Noise, its protocol name is
`Fby_XX_25519_ChaChaPoly_SHA256`, and no peer may treat it as an interoperable
Noise implementation.

Both versions are still wrapped in the FBY envelope and the 16-byte FBY datagram
header, so the transport is **not** interoperable with a plain Noise UDP peer.
v2 is per-session, has envelope version 2 and never silently downgrades to v1.
Ed25519 v2 tickets remain open (the tree vendors classic Monocypher EdDSA over
BLAKE2b, not RFC 8032 Ed25519). IK/PSK is rejected for 1.0: LAN sessions use XX
and the ticket-issued static key, and a future `XXpsk3` needs its own name,
vectors and provisioning design.

Deviations that remain in both versions:

1. Every handshake datagram carries an 8-byte FBY header (magic `0x31435346`,
   version, message type, flags). The header is not part of the transcript.
2. The v1 prologue is fixed to `FemboyEngine2D-secure-channel-v1` followed by the
   little-endian 64-bit session id, and may be extended by a 32-byte prologue
   binding (the signed ticket hash). v2 uses only the optional binding.
3. Message 1 carries `e` without a payload tag (standard: message 1 has no key
   yet); messages 2 and 3 carry empty-payload tags in both versions.
4. A cookie challenge (type 4) can precede message 1. The cookie fields are
   excluded from the transcript and only gate the Diffie-Hellman work.
5. v1 keeps the frozen FBY record layer: the sequence is the first eight bytes of
   the datagram, the ChaChaPoly nonce is that little-endian sequence followed by
   four zero bytes (not the RFC 7539 layout), the AAD is the session id, and the
   key is re-derived with the `FBY-REKEY` HKDF label every `RecordsPerKey`
   records.
6. v2 uses the Noise transport rules: the datagram starts with a 16-byte header
   `[epoch u64][sequence u64]`, the header is the AAD, the nonce is the 64-bit
   message number in the standard four-zero-byte layout, and the epoch key is the
   Noise `REKEY()` chain from the split key (`NoiseRekey`, epoch 0 is the split
   key). The receiver commits the derived key only after the tag succeeds, so a
   forged epoch header cannot move the peer ahead of the stream. `record_layer_v2`
   covers round-trip, reorder, loss, epoch rollover, the rekey boundary, the
   epoch-jump cap and header tampering.
7. v1 hashes the raw X25519 result with BLAKE2b before `MixKey`; v2 uses the raw
   RFC 7748 output and rejects the all-zero low-order result, as standard Noise
   does. v1 also derives the handshake nonce with the same non-standard layout as
   its records, hashes its 31-byte protocol name instead of zero padding it, and
   mixes the prologue into the chaining key. v2 follows the specification on all
   three points: the standard nonce layout, the padded-name initial hash and a
   chaining key that stays at `h0` while the prologue updates only `h`.
8. Ticket signatures use Monocypher's EdDSA over BLAKE2b, not RFC 8032 Ed25519.
9. PSK mode is not implemented. A LAN/PSK mode is a separate decision; it would
   be added as a standard `XXpsk3` protocol, not as an optional field of this
   one.

The target architecture is standard Noise with a documented transport binding;
[ADR 0001](../adr/0001-secure-transport.md) records the decision, the migration
phases and the acceptance criteria.

### Known-answer checks

`noise_kats` verifies:

- the standard `Noise_XX_25519_ChaChaPoly_SHA256` initial hash: the 32-byte
  protocol name itself, zero padded, not its digest (the name is exactly
  HASHLEN bytes), and that hashing the name gives a different value;
- the FBY h0 and the initial transcript hash after the fixed prologue, with and
  without a prologue binding (independent SHA-256 values);
- a deterministic handshake transcript (fixed static keys, fixed ephemeral
  seeds, fixed session id and binding) whose final handshake hash is frozen, so
  an accidental change of the mixing order or message layout fails the test;
- that different prologue bindings make the handshake fail with `BadTag`;
- the RFC 7748 X25519 vector and that the raw v2 output differs from the
  BLAKE2b-post-hashed v1 output;
- a deterministic v2 handshake: both sides agree, the transcript starts at the
  standard initial hash, and the envelope carries version 2.

`noise_conformance` verifies the v2 handshake against the noise-c reference
vector in `Source/Tests/Vectors/`:

- all three handshake messages (inner Noise bytes, envelope stripped) and both
  non-empty payloads match byte for byte, which also covers the padded-name
  initial hash, the prologue update of `h` without touching `ck`, the standard
  nonce layout and the optional handshake payload;
- the final handshake hash matches;
- the three reference transport ciphertexts are reproduced from the engine's
  split keys in both directions with nonces 0 and 1, so the split keys match the
  reference implementation's keys byte for byte.

`record_layer_v2` verifies the v2 record path:

- round-trip with the explicit `[epoch][sequence]` header and the 16-byte header
  size;
- reorder and loss inside and across epochs, including one record from the
  previous epoch;
- epoch rollover at `RecordsPerKey`, the rekey boundary and replay rejection;
- tampering with the ciphertext, the epoch or the sequence fails the tag without
  advancing the receive chain;
- the epoch-jump cap, and that the `NoiseRekey` primitive matches the
  specification recipe (32 zero bytes, maximum nonce, empty AAD).

The frozen FBY transcript hash is a regression answer for the v1 variant. v1 is
**not** a Noise test vector: its envelope, prologue, record layer, name hash and
nonce layout change the transcript, so interoperability is not claimed for v1.
The v2 path is checked against the noise-c vector and the reproducible
`noise-differential` job, so v2 is vector-conformant; the wire default stays v1
until the deprecation plan lands. The v2 checklist is in
[protocol-v2.md](protocol-v2.md).

## Ticket binding

The lobby-issued ticket is presented in the unencrypted AUTH datagram before the
handshake. The server verifies the EdDSA signature, the expiry window, the player
role and that the ticket's server public key equals its own X25519 static key
before it starts a `SecureChannel` for that address.

`SHA-256(signed ticket)` is then mixed into the handshake prologue on both sides
(client: from its configured ticket; server: from the ticket it just verified).
The transcript therefore commits to the exact ticket bytes: a client that
presents ticket A in AUTH and completes the handshake with a different ticket, or
an on-path attacker that swaps the ticket between AUTH and the handshake, fails
authentication. A ticket signed for a different server key is rejected before
the handshake starts.

The limitation is explicit: the ticket itself is not confidential (it travels in
plaintext AUTH); it is a bearer credential bound to the handshake. TLS-like
confidentiality begins with the handshake.

## Rate limits

- Handshake: a per-address token bucket covers every pre-handshake datagram
  (AUTH, cookie challenge and Noise messages). The defaults are a burst of 32
  datagrams and 8 per second; `SetHandshakeRateLimit` changes them and a burst of
  0 disables the limit. Buckets refill only through `Tick(delta)`, which hosts
  call once per frame. Established peers bypass the bucket, so records and voice
  are never throttled by it. `HandshakeRateLimited()` counts drops.
- Voice: `VoiceNetworkBridge` keeps a per-speaker token bucket (default burst 64,
  refill 64 packets/second). A flooding speaker is dropped before
  `VoiceChannel::ReceivePacket`, so a relay server never forwards the flood.
  `SetRateLimit` tunes it and `PacketsRateLimited()` counts drops.
- The AUTH failure counter (8 failed tickets per address) is unchanged and
  complements the token bucket.

Both limits are probabilistic best-effort protection, not admission control. A
distributed flood still costs a `Poll` per datagram; the first line of defense
remains the stateless cookie, which keeps forged-source handshakes away from the
X25519 path.

## Session transport wiring

`Source/Engine/Network/SecureSessionTransport.hpp/.cpp` composes the handshake
and the session: the server keeps one `SecureEndpoint` per client address and the
client one for the server, and a single transport poll dispatches each datagram
by address before handing the decrypted payload to `NetSession`. `NetSession`
sends through a hook, so session traffic never touches the raw link. The roster
carries each player's address, which is what lets the voice mesh route to a peer.

## Voice trust model

Relay voice is encrypted on the wire by the secure session transport, but the
relay endpoint decrypts it and sees Opus packets. Participants therefore trust
the host/relay with their voice. End-to-end sender keys (X25519 + AEAD, server
relays ciphertext it cannot read) are a design item, not part of 0.1.0-alpha.1; enabling
them would change `VoiceNetworkBridge` and the relay's forwarding contract.

## Identity storage

`Source/Engine/Network/Identity.cpp` writes a version-2 container:

- header: magic `FID2`, version 2, flags and payload length (12 bytes);
- payload on Windows: the 240-byte identity record encrypted with DPAPI
  (`CryptProtectData` with `CRYPTPROTECT_UI_FORBIDDEN`), so the file only
  decrypts for the same Windows user profile;
- payload on POSIX: the 240-byte record itself, written with mode 0600 and
  re-applied owner-only permissions on every save;
- version-1 files (exactly 240 bytes) still load and are upgraded by the next
  save; a truncated file, an unknown magic/version or a payload-length mismatch
  is rejected before any key is used.

The integrity digest inside the record (BLAKE2b-256) detects corruption and
accidental edits, not a malicious rewrite: a local attacker who can replace the
file can create a new self-consistent identity. DPAPI (Windows) and file mode
0600 (POSIX) are the actual at-rest protections. Tests: `identity_storage`
(round trip, DPAPI flag and secret absence on Windows, 0600 and header checks on
POSIX, legacy migration, truncation/version/length rejection) and the existing
`net_identity` record/ticket checks.

## Status of the remaining security items

| Item | Status |
|---|---|
| Noise KAT / protocol naming | v1 renamed to the FBY variant, KATs and deviations documented; standard-Noise v2 accepted in ADR 0001 |
| Ticket binding to the handshake | Done: prologue binding plus pre-handshake validation |
| Cookie challenge | Done (`secure_channel` covers challenge and message-1 replay) |
| Handshake and voice rate limits | Done (`rate_limit`) |
| PSK/LAN mode | Deferred; standard `XXpsk3` in v2 if required |
| EdDSA-BLAKE2b | Frozen v1 ticket format, non-interoperable; RFC 8032 Ed25519 is the v2 target |
| Identity storage (DPAPI / 0600) | Done: versioned container, migration and `identity_storage` |
| Voice E2E sender keys | Design only; relay currently sees Opus |
