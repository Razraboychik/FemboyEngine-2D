# CodeQL audit

## Why the alert count did not drop

The GitHub alert list is the **default setup** analysis, and default setup does
not read `.github/codeql/codeql-config.yml`. That file is only applied by an
advanced-setup workflow or by the `github-codeql-config-file` repository
property, which is an organization-level property and is not available to this
user-owned repository. As a result, the `paths-ignore` entries never took effect
and the unused-vendor alerts stayed open.

There were two more reasons the 0.4.0/0.5.0 work was not reflected:

- default setup scans `main`; the compiled-vendor patches and the own-code
  changes were on `v0.5.0alpha.1` (`5807cec`), not on `main` (`de8abf4`);
- one own-code alert in `Source/Tests/AdvancedPipelinesTest.cpp` was still
  unpatched.

The latest analysis was `2149258970` at `2026-11-12T04:32:00Z`, commit
`de8abf4a4e`, with **74 open results** (the earlier 76 count was a previous
snapshot). Every result is `cpp/integer-multiplication-cast-to-long`.

## Current triage

| Group | Count | Action |
|---|---:|---|
| Unused miniaudio split copy, Opus tests/demos/DNN tools, FlatBuffers tests | 51 | dismissed as `won't fix`; these paths are outside every build target |
| Compiled Opus/Monocypher sources | 22 | patched with widening casts in the working tree and mirrored under `third_party/patches/` |
| `Source/Tests/AdvancedPipelinesTest.cpp` | 1 | patched with an explicit 64-bit pixel-byte product |

After the 51 dismissals, the GitHub page shows **23 open alerts**. The 22 vendor
patches and the test patch still need to reach `main` and a new default-branch
analysis; until then the old `de8abf4` results remain visible.

## Own-code fix

```cpp
const std::uint64_t ScopedPixelBytes =
    static_cast<std::uint64_t>(ScopedChunkPixels) * static_cast<std::uint64_t>(ScopedChunkPixels) * 4u;
ScopedBytes.resize(sizeof(std::uint64_t) + 6u * sizeof(std::uint32_t) +
                   static_cast<std::size_t>(ScopedPixelBytes));
```

## Vendor policy

1. Vendored trees are updated from pinned upstream tags where possible.
2. Every local vendor change is mirrored under `third_party/patches/<library>/`;
   it must apply to the pinned source and be rechecked after an update.
3. Do not add a global `third_party/**` exclusion: it would hide the next real
   vendor advisory. Use the narrow config above.
4. Compiled hits in Opus and Monocypher are patched with widening casts; the
   patch files are the source of truth for a re-import.
5. Unused hits are dismissed while the vendored copies stay pinned. If the
   repository moves to an organization, set `github-codeql-config-file` to
   `.github/codeql/codeql-config.yml` so the exclusions become automatic again.

## Existing patches

The compiled-asset patches are in `third_party/patches`:
`stb/stb_image-size-overflow.patch`, the two
`imgui/imstb_truetype-*size-overflow.patch` files,
`miniaudio/miniaudio-size-hardening.patch` and
`opus/codeql-integer-multiplication.patch`,
`monocypher/codeql-integer-multiplication.patch`. They are validated with
`git apply --check` after re-import and by a full build and CTest run after
applying them. The patch set is not a licence exception: miniaudio is
MIT-0/Unlicense and stb_vorbis is MIT/Public Domain, so both permit
modification and redistribution under their terms.

No query filter suppresses the rule. The compiled hits are patched in place;
the unused hits are dismissed with a link to this document.

## Unused-alert policy and local writer

The narrow exclusion list lives in `.github/codeql/codeql-config.yml`; it is
kept for advanced setup and for the future organization property. Developer
guidance is in [CodeQL unused alerts](../md/en/CodeQLUnusedAlerts_EN.md) and its
Russian pair. `stb_image_write` was replaced by `Fby::Assets::WritePng`.
