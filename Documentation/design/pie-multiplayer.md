# Play-In-Editor and the client viewport

Play-In-Editor (PIE) runs the game in an isolated world inside the editor. The
same feature also drives a second, replicated client world and, for multiplayer,
a real sandbox process. This document records the ownership rules; the step
commands live in [editor-workflows.md](editor-workflows.md).

## Frame composition

`Source/Editor/Infrastructure/FrameComposer.hpp/.cpp` owns the world-to-frame
extraction that used to live in the editor application: sprites and tilemap
quads are key-sorted into batches, skinned and cloth meshes go through the mesh
path, and the packet points into the composer's arrays. Every viewport keeps its
own composer, so a second world never overwrites the first packet mid-frame.

## Local multiplayer viewport

- The viewport toolbar starts a session with `Play MP`
  (`PlayInEditor::StartMultiplayer`). The session steps the host clone and then
  publishes the delta into the replicated client world on every editor frame.
  `Stop` releases the client target together with the PIE worlds.
- `Source/Editor/Panels/PieClientViewport.hpp/.cpp` owns the client camera and
  the second offscreen target. The target mirrors the main viewport resolution,
  the panel letterboxes it, middle-drag pans and the wheel zooms the client
  camera. The target is released when the session stops or the panel is hidden,
  so an idle editor keeps one offscreen image, not two.
- `Source/Apps/Editor/Main.cpp` composes the client packet after the editor
  packet and records it into the client target before the editor target. That
  keeps the main viewport the last writer of the shared virtual-texture feedback
  image.
- Test: `pie_viewport` (target creation and reuse, a client-world packet with
  three sprites, recorded draw into the target, release on Stop through
  `PieClientViewport::SyncSession`).

## External client process

- `Play MP` also launches `fby_sandbox` as
  `--pie-client --connect 127.0.0.1:<host> --listen <client>` from the editor
  directory. `PlayInEditor::StartExternalClient` opens a loopback UDP transport,
  retransmits the baseline until the sandbox binds its socket and publishes the
  same delta stream the local client world receives. The requested port pair is
  tried first, then a few neighbours; `Stop` terminates the child (Windows
  `TerminateProcess`, POSIX `SIGTERM`).
- A missing binary, a bind failure or a state that does not fit the replication
  budget is **not** a session failure: the editor logs the reason, keeps the
  local client world and states it in the status line.
- `Source/Apps/Sandbox/PieClient.hpp/.cpp` and `SandboxOptions.hpp/.cpp` are the
  client side. The parser accepts `--pie-client`, `--connect host:port`,
  `--listen port` (default host port + 1), `--headless` and the demo sprite
  count. `PieClient` owns the sandbox registry and the snapshot receiver, applies
  baselines and in-place deltas, and turns host silence into a clean exit
  (`FirstStateTimeoutMs` before the first state, `StateTimeoutMs` of no host
  datagram afterwards).
- A paused session stays alive: the editor republishes its last state every
  500 ms under the same tick; the receiver ignores the old tick but the datagram
  proves the host is still there.
- The windowed sandbox renders the replicated sprites and latches the replicated
  `Camera2D` once. `--headless` runs the same client without a window, which is
  the CI and manual smoke path.
- Test: `sandbox_client` (argument parsing including the default listen port,
  loopback baseline and delta receive, entity handles kept across the delta,
  timeout after host silence, explicit stop request).

## PIE voice

- `PlayInEditor::StartVoice` creates a host player (0) and a simulated client
  player (1), each with a `VoiceSystem`, a `VoiceSession` and a
  `VoiceNetworkBridge` on dedicated endpoints (41006/41007), over an in-process
  loopback link on the same `MemoryNetwork` as the session.
- Routing is mesh, so every packet the client captures reaches the host
  directly. The bridge has `StartLoopback`/`Link` for the unencrypted,
  single-peer case and keeps the secure-session path intact.
- The host voice system uses the editor mixer and its reference ring when audio
  exists; the simulated client is headless, so the host is not played back into
  the same speakers. `StepMultiplayer` polls both endpoints and ticks both
  bridges per PIE frame; `Stop` tears the link down with the session.
- The viewport toolbar shows `Voice mute` and `PTT (V)` while PIE is playing and
  voice is up. Both drive the host `VoiceSession`, which gates the capture ring
  through `CanTransmit` (muted, or push-to-talk enabled without the key held).
- Test: `voice_pie` (client packets reach and decode on the host, mute and
  push-to-talk stop the encoded stream, Stop clears the voice link).

## Known limits

- PIE voice starts no microphone capture on its own: the link and gating work,
  but the real microphone path is still the editor voice runtime. Wiring that
  capture into the PIE host `VoiceSystem` is a follow-up; the PIE graphs already
  run AEC and the project denoise tier.
- One replication state is capped at 4096 bytes (`kReplicationMaxStateBytes`,
  16 chunks of 448 bytes). A larger snapshot cannot be published, so a large PIE
  world keeps only the local client world and the spawned sandbox exits after
  its first-state timeout; the editor status line states the reason.
- The PIE client packet sets no virtual-texture handle and no SDF lighting of its
  own. VT/SDF are frame-global today, so the client draws with the editor's last
  state rather than a client-specific one; a client-side pass is a follow-up.
- These three limits are tracked in [ROADMAP.md](../ROADMAP.md) (P5).
