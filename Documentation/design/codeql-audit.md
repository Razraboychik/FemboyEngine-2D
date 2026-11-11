# CodeQL audit

Local verification on the patched tree:
`CodeQL 2.27.4`, query pack `cpp-queries 1.10.0`, database built from
`fby_engine`, query `Likely Bugs/Arithmetic/IntMultToLong.ql` — **0 results**.
The GitHub analysis below is the last pushed snapshot; it still shows the
pre-patch 89 alerts until the next scan.

Latest `main` analysis at the time of this audit:

- analysis id `2121904212`, `2026-11-08T16:11:41Z`;
- commit `1dff82a831f20ee2ad4fa031f8ddf4a6eaa9150d`, ref `refs/heads/main`;
- **89 open results**, **58 rules** in the database;
- every result is `cpp/integer-multiplication-cast-to-long`, severity
  `warning`, security severity `high`.

There are **no own-code alerts**. All 89 are the same hardening rule firing in
vendored sources. The old `136 alerts` entry in this document was a previous
`main` scan; it is superseded by this analysis.

## Where the 89 alerts are

| Vendor | Alerts | Compiled by the engine? |
|---|---:|---|
| Opus | 39 | partly: 18 core sources; 21 tests/demos/tools |
| miniaudio | 29 | no: generated `extras/miniaudio_split/miniaudio.c` |
| HarfBuzz | 6 | yes |
| ImPlot | 6 | yes |
| Monocypher | 4 | yes |
| stb | 3 | tool/test only before this change; now replaced by `Fby::Assets::WritePng` |
| ImGui | 1 | yes |
| flatbuffers | 1 | no: `tests/native_type_test_impl.h` |

The 89-alert snapshot is split as follows: **51 unused** (29 miniaudio split
copy, 21 Opus tests/demos/tools, 1 FlatBuffers test), **38 used or tool/test**
(35 compiled engine vendor hits plus the 3 `stb_image_write` hits replaced by
the local writer). The current tree patches the 35 compiled hits, excludes the
51 unused paths and replaces `stb_image_write`; the expected next-scan result is
**0 open alerts**. Until a new analysis runs, GitHub still shows the 89-alert
snapshot from `1dff82a8`, so this is a target state, not a verified scan. The rule
is about an `unsigned int` multiplication converted to `size_t`/`uint64_t` after
the multiply; it is a hardening class, not proof that every hit is exploitable.

## Own-code fixes

None are required for this report: the scan produced zero own-code results. The
previous own-code fixes (material graph slot widening, `RenderChunkArena` span
slicing, `FempakWriter` bounds checks) remain in the tree and are no longer
reported.

## Vendor policy

1. Vendored trees are updated from pinned upstream tags where possible.
2. Every local vendor change is mirrored under `third_party/patches/<library>/`;
   it must apply to the pinned source and be rechecked after an update.
3. Do not add a global `third_party/**` CodeQL exclusion: it would hide the
   next real vendor advisory. Use a separate scheduled vendor scan and triage
   the results above.
4. Compiled hits in HarfBuzz/ImPlot/Monocypher/Opus core are candidates for
   upstream patches; find a call path that accepts asset/device-controlled
   dimensions first.
5. Uncompiled hits are not build risks today. Keep them visible until the
   vendored copy is updated or the unused extras are removed.

## Existing patches

The compiled-asset patches are in `third_party/patches`:
`stb/stb_image-size-overflow.patch`, the two
`imgui/imstb_truetype-*size-overflow.patch` files and
`miniaudio/miniaudio-size-hardening.patch`. They are validated with
`git apply --check`, by compiling standalone patched copies and by a full build
and CTest run after applying them. The patch set is not a licence exception:
miniaudio is MIT-0/Unlicense and stb_vorbis is MIT/Public Domain, so both permit
modification and redistribution under their terms.

The compiled hits are patched in place for this change: Opus core size/energy
math, HarfBuzz table multipliers, ImGui texture allocation, ImPlot point
multiplication and Monocypher Poly1305 terms. The matching patch files live under
`third_party/patches/`; no query filter suppresses the rule.

## Unused-alert policy and local writer

The narrow exclusion list lives in `.github/codeql/codeql-config.yml`; it covers
the 29 generated miniaudio split alerts, 21 Opus test/demo/tool alerts and the
FlatBuffers test header. Developer guidance is in
[CodeQL unused alerts](../md/en/CodeQLUnusedAlerts_EN.md) and its Russian pair.
`stb_image_write` was replaced by `Fby::Assets::WritePng`, so its three tool/test
alerts should disappear on the next scan. Until a new analysis runs, the checked-in
report above still shows the 89-alert snapshot from `1dff82a8`.
