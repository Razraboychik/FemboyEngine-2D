# Gated RHI optimizations: FIF3 dirty upload and GPU motion vectors

This document is a plan, not an enabled feature. Neither optimization ships in the
default path. Both are high-leverage because they remove the two largest remaining
costs, but both change buffer ownership and GPU synchronization. They must be
opt-in, capability-gated and reversible.

## Rules for both features

- Default is the existing path: FIF3 full instance upload and CPU motion history.
- Every new path is behind a build/runtime flag and reports `unsupported` instead
  of failing silently.
- Every new path has a full-data fallback. A scene change, camera cut, resize,
  multi-viewport frame or invalid range disables the incremental path for that
  frame.
- No public `FramePacket` layout change is enabled without updating every RHI
  backend and the frame-composer tests in the same change.
- The existing serial extract and full upload remain the reference for correctness
  tests.

## FIF3 dirty upload

### Problem

The current staging buffer is sliced per frame in flight. A slice used at frame N
was last written at frame N-3, so applying only deltas relative to frame N-1
leaves stale instances. That is why dirty ranges are currently restricted to
`framesInFlight == 1`.

### Phased implementation

1. **Stable slots (CPU only, no RHI change).**
   - Assign each render object a persistent slot independent of sort order.
   - Keep the existing compact item list and add a slot-id array.
   - Gate behind a disabled experimental flag.
   - Exit gate: CPU packets are byte-identical to the compact path after sorting.

2. **Per-frame change log.**
   - Record changed slot ranges for the last `framesInFlight` frames.
   - On upload, replay the ranges that are newer than the target slice.
   - Keep a full-upload fallback on first use, count change, slot reuse or invalid
     history.
   - Exit gate: a three-frame test that mutates a known set of objects and compares
     the resulting instance buffer against a full-upload reference.

3. **Indirect draw integration.**
   - Batches reference slot ranges instead of compact positions.
   - The sort still determines batch order; only instance addressing changes.
   - Keep compact draw batching for devices without indirect-draw support.
   - Exit gate: draw-call count and final image match the compact path on Vulkan;
     Metal must compile and use its buffer-update path.

4. **Activation.**
   - Enable only for `framesInFlight >= 1` when the capability flag is present and
     the scene passed the three-frame test.
   - Retain a runtime toggle and a full-upload fallback.
   - Roll back if `RHI::Stats().uploadedBytes` does not improve or any validation
     error appears.

## GPU motion vectors

### Problem

CPU motion extraction is still the largest remaining CPU block. A compute pass can
read current and previous transforms and write motion vectors without touching the
CPU history array.

### Phased implementation

1. **Capability and reference.**
   - Add a `motionVectors` capability to `DeviceInfo`, default false.
   - Keep CPU motion as the reference and fallback for Null, unsupported Metal and
     devices without compute.
   - Exit gate: CPU and GPU motion output match within an explicit tolerance for a
     controlled animated scene.

2. **History buffers and invalidation.**
   - Add previous/current transform buffers owned by the RHI frame slice.
   - Invalidate on camera cut, scene change, resize, first frame and history reset.
   - Exit gate: camera-cut and scene-reload tests produce no stale vectors.

3. **Vulkan compute pass.**
   - Add a compute pipeline, descriptor sets and barriers from compute writes to
     vertex/fragment reads.
   - Record the pass before the main render pass.
   - Exit gate: validation layers clean on two and three frames in flight.

4. **Metal compute pass.**
   - Use a compute encoder with the same history-invalidation contract and managed
     buffer updates.
   - Exit gate: golden-image comparison against the CPU reference on macOS.

5. **Activation.**
   - Enable only when the capability is true and the golden test passes.
   - Keep the CPU path selectable for diagnosis and unsupported backends.

## Why propose these at all

The potential gains are large: FIF3 can remove most of the 4.8 MB per-frame
instance upload, and GPU motion can remove most of the remaining CPU motion cost.
The risk is real, but it is containable with flags, explicit fallbacks, frame-count
tests, validation layers and golden images. If those gates are not met, the
feature stays disabled and the current safe path remains the product path.

## Non-goals for the current milestone

- No default activation of either feature.
- No removal of the CPU motion fallback.
- No compact-packet ABI change without all backends and tests updated together.
- No performance claim before a three-frame and a backend-parity measurement.
