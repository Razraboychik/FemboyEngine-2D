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
`extras/stb_vorbis.c` is absent from the build. Apply the patch only when the
Vorbis extras path is enabled, or send it upstream.

License: the target is MIT-0 OR Unlicense (miniaudio) and MIT OR Public Domain
(stb_vorbis). This patch may be distributed under the target's license.
