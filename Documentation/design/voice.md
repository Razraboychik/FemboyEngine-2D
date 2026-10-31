# Voice: capture, DSP, AEC and networking

Status: 0.1.0-alpha.1 tree. This is the per-subsystem voice document; the forward work
lives in [ROADMAP.md](../ROADMAP.md), and the security status of the underlying
transport lives in [security.md](security.md).

## Voice runtime

- `Source/Engine/Audio/VoiceSystem.hpp/.cpp`: owns the local voice path:
  microphone capture into a raw ring, AEC against the mastered-output reference,
  denoise/AGC/limiter chain, processed ring feeding `VoiceChannel`, decoded peer
  rings bound to a dedicated Voice bus, and live denoise-tier switching.
  `StartCapture` / `SwitchInputDevice` handle input selection and fallback;
  `PushCaptured` covers headless hosts and tests.
- Tests: `voice_system` (capture -> DSP -> Opus packets sent, live Rnnoise
  switch, participant binding on the Voice bus).

## Sound cues and voice captions (UwUshher)

- `Source/Engine/UI/SoundCues.hpp/.cpp`: `SoundCueRegistry` maps `cueId` to a
  localization id, category (ui/world/impact/footstep/voice/ambient), priority
  and cooldown. `TryTrigger` enforces the cooldown so footsteps and impacts do
  not flood captions. Ids are gated by `DeafMode` or the independent
  `showSubtitles` switch (`AccessibilityKernel::SubtitlesVisible`); without one
  of them no cue caption is queued, while BlindMode still writes the narrative
  log.
- `AccessibilityKernel::PublishCueSound` resolves the loc id through
  `SetCueLocalization` (the active locale with default-locale fallback, the
  common case) or a caller-supplied resolver, publishes the caption with the cue
  priority, and mirrors it into the narrative log when BlindMode is on.
- Namespace: voice captions use `0x4000 + player` (`kVoiceCaptionCueFirst` ..
  `kVoiceCaptionCueLast`). `SoundCueRegistry::Add` rejects authored cues in that
  range, so a voice line can never collide with a sound cue; a repeated speaker
  refreshes the same line (including its text) instead of stacking duplicates.
- `VoiceCaptionProvider` polls one event per frame; while subtitles are visible
  the caption lands in `SubtitleQueue`, and BlindMode additionally writes it to
  the narrative log. The provider decides the text (VAD speaker label or an
  external speech-to-text backend).
- Project settings: `[accessibility] deaf_mode` and `show_subtitles` (the latter
  shows captions without muting Master).
- Tests: `sound_cues` (registry add/duplicate/find, reserved namespace, cooldown
  timing, localized caption text and priority, `showSubtitles` and DeafMode
  gating, BlindMode narrative mirror, the localization adapter, unknown cue ids).

## Echo cancellation (UwUshher)

- `Source/Engine/Audio/VoiceAec.hpp/.cpp`: full-band overlapping partitioned block
  frequency-domain NLMS at the 48 kHz capture rate. The tail is split into
  128-sample partitions (160 ms = 60 partitions), the echo spectrum is built from
  the last N reference samples, and the per-bin NLMS step is normalized by the
  smoothed reference power, so no decimation to 8 kHz remains.
- Delay and drift: the bulk delay is locked by cross-correlation over the
  configured window and re-estimated every 64 blocks within ±96 samples, which
  follows a ±100 ppm clock offset by moving the integer delay (measured drift in
  `voice_aec`: +100 ppm → 2372 samples, -100 ppm → 2428 samples on a 2400-sample
  path). The output is delayed by one 128-sample block.
- Double talk: when the microphone block carries more than six times the
  estimated echo power, the weight update freezes while the residual keeps
  flowing, so near-end speech survives.
- Reference signal: `AudioSystem::Mix` writes a mono downmix of the mastered
  output into an optional `FloatRing` (`SetReferenceRing`); the AEC consumes the
  same block as the microphone, so no extra thread or copy is required.
- Settings: `[audio] aec`, `aec_tail_ms` and `aec_step` in the project, exposed
  by the Voice Devices panel (tail 8..160 ms, step 0.01..1). Defaults: tail
  128 ms, delay window 120 ms, step 0.25.
- Budgets (measured on the development machine, 10 s of 48 kHz audio):
  AEC 0.217 s (gate < 1.0 s), spectral denoise 0.107 s, RNNoise 0.092 s (gate
  < 1.0 s); the Opus stream measures 21.2 kbit/s including FVOX headers.
- Tests: `voice_aec` (settings clamp and partition count, delay lock within
  ±4 samples, ERLE above 12 dB and more than 10 dB real output reduction on a
  wideband echo, ±100 ppm drift, double-talk detection with preserved near-end
  speech, null reference bypass).

## Voice network (UwUshher)

- `Source/Engine/Audio/VoiceCodec.hpp/.cpp`: Opus wrappers (48 kHz mono, 20 ms
  frames, VoIP mode, bitrate/complexity/FEC/DTX), one encoder per outgoing
  stream and one decoder per peer with the codec's PLC for loss concealment.
  `FBY_HAS_OPUS` is defined by CMake when the vendored Opus target exists.
- `Source/Engine/Audio/VoiceChannel.hpp/.cpp`: `FVOX` packets (magic, version,
  speaker id, sequence, samples, Opus payload), an 8-slot jitter buffer per peer
  with wrap-safe sequence ordering, playout at 20 ms, PLC on holes and
  underruns, and per-peer `FloatRing` output for `DspGraph::PlayStream`.
  First-packet policy: playout is held for one 20 ms interval so a lower
  out-of-order sequence can still become the first played frame; the first
  played sequence is exposed in the stats. A hole first tries Opus in-band FEC
  from the next queued packet (`decode_fec = 1`) and only then falls back to PLC;
  recovered frames are counted separately.
  Routing is a policy on the same code: `Relay` sends to one server player which
  forwards the authenticated packet to every other participant (never back to
  the sender); `Mesh` sends once per participant directly. The transport is a
  send-function pointer, so packets ride on `SecureChannel`/`SecureEndpoint`
  after the caller authenticates them; the payload format is transport-agnostic.
  Mute/deafen/PTT gating comes from `VoiceSession::CanTransmit`, and decoded RMS
  feeds `SetParticipantLevel` for talking indicators.
- `ConfigureVoiceDucking` routes the Voice bus into the Music bus with ducking
  (threshold 0.08, depth 0.7, 10 ms attack, 400 ms release).
- Voice bus creation and per-participant `PlayStream` binding stay with the
  caller (`DspGraph::CreateBus` then `VoiceSession::SetParticipantVoice`), so a
  headless server never opens a playback device.
- Tests: `voice_network` (Opus encode/decode and PLC, mesh delivery to two
  peers, relay forwarding without echo, out-of-order jitter playback, ducking
  configuration), `voice_fec` (FEC recovery of a lost frame) and `jitter_init`
  (the first played frame is the lowest sequence held for one interval).

## Voice processing (UwUshher)

- `Source/Engine/Audio/VoiceDsp.hpp/.cpp`: fixed-state chain for one mono
  microphone stream: DC blocker, second-order high-pass (`HighPassHz`, 80 Hz by
  default), gate/VAD (threshold, attack, release, hold), spectral denoise
  (128-point analysis, 50 percent overlap-add, per-bin noise estimate,
  over-subtraction and a residual floor), AGC (target dBFS, maximum gain,
  attack/release) and a limiter at the configured ceiling. `VoiceDspChain` never
  allocates and takes any block size.
- Tiers: `none`, `gate`, `spectral`, `rnnoise`. The vendored RNNoise (BSD-3,
  model in-tree, notices manifest, setup pin) runs a 480-sample frame through the
  model; `DenoiseStrength` is a wet/dry mix between the pre-model frame and the
  model output, and the model's voice-activity probability is exposed as
  telemetry. The gate stays bypassed for the RNNoise tier because the model does
  its own noise suppression; the tier reports when it is compiled out.
- Tests: `rnnoise_mix` (strength 0 is the dry path shifted by one frame,
  strength 0.5 is the exact wet/dry average, probability in range).
- Presets: `headset`, `laptop-mic`, `noisy-room`, `low-bandwidth`
  (`VoicePresetSettings`). Every preset is clamped into range, and
  `VoiceDspSettings::Issue()` names the clamped field.
- Project file: `[audio] denoise` (0..3), `gate_db`, `agc`, `agc_target_db`,
  `aec`, `aec_tail_ms`, `aec_step`, alongside `input_device`/`output_device`.
  The Voice Devices panel edits them, shows the microphone meter and runs the
  capture test; changes are persisted with the project.
- Capture: `VoiceCapture` is the only miniaudio capture path. It writes mono f32
  frames into a caller `FloatRing` and publishes peak/RMS from the same callback;
  `AudioCaptureMonitor` is a thin wrapper with a private ring, so the panel meter
  and the voice path share one implementation and one callback. A stale monitor
  ring reports `DroppedFrames` as the drift signal.
- Device ids: `AudioDeviceInfo` carries the persisted 64-bit hash, the full raw
  backend id and its byte count. `ResolveAudioDeviceIndexByRaw` finds a device
  exactly; `AudioDeviceIdCollisions` reports a hash collision, and capture
  refuses an ambiguous hash instead of opening the wrong microphone.
- Live output: the Voice Devices panel has "Apply output", which resolves the
  selected id (default when empty) and calls `AudioSystem::ReopenOutput` without
  restarting the editor. The same helper is tested headlessly.
- Tests: `voice_dsp` (DC/HPF convergence, gate open/close, AGC gain and limiter
  ceiling, spectral silence/tone/noise finiteness, settings clamp, presets),
  `voice_pipeline` (ring math, stream voices, session policy), `device_id`
  (hash/raw collision resolution) and `voice_device_panel` (live output apply
  failure policy without a started mixer).
