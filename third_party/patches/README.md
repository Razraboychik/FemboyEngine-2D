# Third-party patches

Vendored sources stay close to their upstream release. A fix that must change a
vendored file is kept here as a `git apply`-able patch, with a regression test
where the affected code is compiled by the engine.

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
