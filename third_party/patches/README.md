# Third-party patches

Vendored sources stay close to their upstream release. A fix that must change a
vendored file is kept here as a `git apply`-able patch, with a regression test
where the affected code is compiled by the engine.

The four patches below are applied to the vendored files on this branch. The
patch files remain as the upstream/reapply record; do not apply them twice on
this tree.

## Apply

From the repository root:

```bash
git apply third_party/patches/<library>/<name>.patch
```

Do not hand-edit a vendored file and leave the change undocumented. If a patch is
temporary, record the upstream URL and the condition for removing it.

## Licensing

A patch is distributed under the license of the file it changes. The current
patches:

| Patch | Upstream license |
|---|---|
| `miniaudio/stb_vorbis-pointer-overflow.patch` | miniaudio MIT-0 OR Unlicense; stb_vorbis MIT OR Public Domain |
| `stb/stb_image-size-overflow.patch` | stb MIT OR Public Domain |
| `imgui/imstb_truetype-size-overflow.patch` | Dear ImGui MIT; imstb_truetype MIT OR Public Domain |
| `imgui/imstb_truetype-sdf-size-overflow.patch` | Dear ImGui MIT; imstb_truetype MIT OR Public Domain |
