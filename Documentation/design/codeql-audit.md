# CodeQL audit

The GitHub code-scanning API currently reports 136 open alerts. They are not 136
independent engine bugs; almost all are the same rule firing in vendored code.

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
4. The one `pointer-overflow-check` in `stb_vorbis.c` is treated as a vendor
   defect until reproduced with a crafted Ogg file.
5. CodeQL exclusions for `third_party/**` are not enabled globally; a separate
   scheduled vendor scan keeps those alerts visible instead of silently hiding
   them.

The next step is an upstream update of miniaudio and a fuzz case for the Vorbis
decoder; until then, the engine keeps the vendor sources unchanged and records
the audit trail here.
