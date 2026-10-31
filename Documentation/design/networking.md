# Networking: transport, input packets and replication

This document covers the plain transport and the ECS bridge. The secure layer
(FBY XX, protocol v2, tickets, rate limits, identity) lives in
[security.md](security.md) and [protocol-v2.md](protocol-v2.md); the deterministic
rollback session is in Complete Reference chapter 10.

## Transport

`Source/Engine/Network/Transport.hpp/.cpp` provides a non-blocking IPv4 datagram
socket, `NetAddress`, `ParseAddress`, up to eight peers, sequence/ack with a
32-bit ack mask, a reliable ring with retransmission and an RTT estimate, MTU
fragmentation with reassembly, a loopback drop simulation for tests, and
statistics.

## Input packets

`Source/Engine/Network/NetProtocol.hpp/.cpp` is the bridge from bytes to the
simulation:

- a versioned `InputPacket` codec (magic, version, player, frame stride, exact
  size);
- `InputChannel`, which batches up to eight `NetInputFrame`s per reliable packet,
  decodes arrivals and feeds `NetRollbackSession::SubmitRemoteInput`, counting
  rejected players and failed submissions;
- a frame's player id always comes from the packet header, so a forged per-frame
  id cannot impersonate another player.

## Replication ECS bridge

`Source/Engine/Network/ReplicationEcs.hpp/.cpp` wires the transport channel to
the ECS:

- `CaptureReplicationBaseline` captures a full `SnapshotSchema::BuiltIns` state;
- `CaptureReplicationDelta` produces a delta against that baseline;
- `ApplyReplicationBaseline` / `ApplyReplicationDelta` restore it on the client;
- `FilterReplicationDelta` wraps `NetRollbackSession::FilterDeltaForPlayer` with a
  `RelevancePlan` built from the relevance grid.

The PIE transport uses the same bridge with a 4096-byte state cap; the cap and
the PIE-specific behaviour are in [pie-multiplayer.md](pie-multiplayer.md).

## Tests

`transport` (loopback datagram, reliable retransmit after a dropped datagram,
outbox ack, 5 KB fragmentation/reassembly, second peer, malformed datagram
rejection, send after close), `net_protocol` (codec round trip and corruption
rejection; two frames plus a three-frame ring batch delivered over loopback and
accepted by a real `NetRollbackSession`; unknown player rejected and counted) and
`net_replication_ecs` (baseline/delta/restore round trip on
Transform2D/Velocity, relevance plan filter call).

## Known constraints

- The replication state cap is 4096 bytes (`kReplicationMaxStateBytes`), and the
  chunk budget (16 x 448) is larger than the cap, so several chunk slots are
  unreachable.
- `CaptureReplicationBaseline` uses the bare ECS restore, so a physics world
  attached to a replicated baseline inherits the native-body lifetime problem
  tracked in [ROADMAP.md](../ROADMAP.md).
