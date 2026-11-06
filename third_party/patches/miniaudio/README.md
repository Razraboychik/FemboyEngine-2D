# miniaudio / stb_vorbis patches

## `stb_vorbis-pointer-overflow.patch`

`stb_vorbis.c` (the optional extras copy under miniaudio) checks a memory-mode
seek with:

```c
f->stream_start + loc >= f->stream_end || f->stream_start + loc < f->stream_start
```

The second comparison relies on pointer overflow, which is undefined behaviour.
The patch checks the byte index against the buffer length first:

```c
if ((size_t) loc > (size_t) (f->stream_end - f->stream_start))
```

Upstream `nothings/stb` master still contains the original pattern at the time
of writing. The file is not compiled by FemboyEngine2D: `MiniAudioImpl.c`
includes `miniaudio.h` without defining `STB_VORBIS_INCLUDE_STB_VORBIS_H`, and
`extras/stb_vorbis.c` is absent from the build. The patch is applied on this
branch as an upstream candidate; it has no runtime effect until the Vorbis extras
path is enabled. Apply it only once on a clean vendor checkout.

License: the target is MIT-0 OR Unlicense (miniaudio) and MIT OR Public Domain
(stb_vorbis). This patch may be distributed under the target's license.


## `miniaudio-size-hardening.patch`

The compiled `miniaudio.h` had 35 `cpp/integer-multiplication-cast-to-long`
alerts: frame counts, channel counts and byte counts were multiplied in 32-bit
before conversion to `size_t`/`ma_uint64`. The patch promotes the first operand
before each multiplication. This is API hardening: miniaudio callers supply
these sizes, so it is not treated as a remote exploit.

The patch is applied to the vendored header on this branch. It was validated by
a full build and CTest on the patched tree. `stb_vorbis.c` is a separate optional
extras file and is not compiled by the engine.
