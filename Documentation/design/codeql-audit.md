# CodeQL audit

At the time of the `main` audit (`edaea34`) GitHub reported 136 open alerts.
They are not 136 independent engine bugs; almost all are the same rule firing in
vendored code. The later `v0.3.0alpha.1` PR analysis (`7795ed3`) returned
**0 c-cpp and 0 python results**; the 8 own-code alerts below are the fixes that
changed that result. The vendor alerts remain visible on the old `main` analysis.

| Category | Count | Location | Decision |
|---|---|---|---|
| `cpp/integer-multiplication-cast-to-long` | 127 | miniaudio, HarfBuzz, Opus, stb, ImGui, Monocypher, ImPlot | vendor audit; upstream or bounded-input proof |
| `cpp/comparison-with-wider-type` | 5 | `MaterialGraph.cpp`, `MaterialGraphPanel.cpp` | fixed by widening loop counters to `uint16_t` |
| `cpp/suspicious-add-sizeof` | 3 | `RenderEcs.hpp`, `FempakWriter.cpp` | rewritten with `std::span` subviews and bounds checks |
| `cpp/pointer-overflow-check` | 1 | miniaudio `stb_vorbis.c` | vendor error; reproduce with large input before patching upstream |

## Own-code fixes

- Material graph slot loops now use `std::uint16_t` and clamp the pin slot to
  `uint8_t`. This removes the wider-type comparison and the theoretical
  `uint8_t` wrap when an input count exceeds 255.
- `RenderChunkArena::SlotData` uses `std::span<std::byte>::subspan` with the
  sprite stride instead of raw `payload + index * sizeof`.
- `FempakWriter` checks the entry-table offset and copies through a byte span.

## Vendor policy

1. The vendored tree is not edited in place.
2. A real overflow gets a patch under `third_party/patches/<library>/`, a
   regression test and an upstream report.
3. The miniaudio warnings are grouped by the `ma_uint32` multiplication pattern;
   only call paths that accept asset/device-controlled sizes are candidates for
   patching.
4. The `pointer-overflow-check` in the optional
   `miniaudio/extras/stb_vorbis.c` is a real pointer-overflow check, but the file
   is not compiled by the engine. A `git apply`-able patch now lives at
   `third_party/patches/miniaudio/stb_vorbis-pointer-overflow.patch`; it is not
   applied because the Vorbis extras path is disabled, and upstream still has
   the original code.
5. CodeQL exclusions for `third_party/**` are not enabled globally; a separate
   scheduled vendor scan keeps those alerts visible instead of silently hiding
   them.

The next step is an upstream update of miniaudio and a fuzz case for the Vorbis
decoder; until then, the engine keeps the vendor sources unchanged and records
the audit trail here. The patch set is not a licence exception: miniaudio is
MIT-0/Unlicense and stb_vorbis is MIT/Public Domain, so both permit modification
and redistribution under their terms.
