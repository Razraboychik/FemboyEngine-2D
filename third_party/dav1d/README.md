# dav1d 1.4.3 (Windows x64)

This directory contains a prebuilt MSVC x64 dav1d distribution used only by the
Windows AV1 backend:

- `include/dav1d/` - public headers;
- `lib/dav1d.lib` - import library;
- `bin/dav1d.dll` - runtime library;
- `COPYING` - BSD-2-Clause license and copyright notice.

Source: https://code.videolan.org/videolan/dav1d

The DLL is copied next to the engine executables and tests. Linux and macOS do
not use these files: CMake finds the system dav1d through pkg-config. Any binary
distribution must keep `COPYING` and the dav1d copyright notice.
