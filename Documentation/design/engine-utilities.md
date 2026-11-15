# Engine utilities for 2D games

These modules replace the common "one plugin per tiny feature" pattern with
engine-owned, allocation-bounded APIs.

## Platform/SystemInfo

`Fby::Platform::SystemInfo` reports:

- every display: name, resolution, refresh rate, DPI scale and primary flag;
- the monitor that hosts a window (`WindowDisplay`);
- CPU name and logical cores;
- GPU name and dedicated video memory (DXGI on Windows);
- total/available RAM;
- total/free storage for a path.

`DisplayInfo::Aspect()` and `AspectLabel()` provide `1.777` and `16:9` directly.
The profile is sampled on demand; it is not a per-frame allocation.

## 2D reverb presets

`Fby::Audio::MakeReverbPreset` maps `Room`, `Hall`, `Cave`, `Plate` and
`Cathedral` to the existing `DspGraph` Schroeder reverb. The preset only sets
`roomSize`, `damping` and `wet`; the mix still keeps the bus send/return levels
authored by the game.

## Tween

`Fby::Animation::Tween` is an allocation-free scalar tween with:

- `Progress()` and `Value()`;
- pause control (`SetAffectByGamePause`);
- time dilation control (`SetAffectByTimeDilation`);
- delay, loop and ping-pong;
- `TweenGroup` fixed at 256 entries, which reuses finished slots.

## Rich text line handling

`Text::CompileTextMarkup` now accepts:

- `\t` — four spaces;
- `\p` — line break plus four-space indent;
- `\n` — line break;
- `\r\n` — normalized to one line break;
- `\\` — a literal backslash.

Tags can span those breaks, so `[b]line 1\nline 2[/b]` remains one formatted
run. Actual tabs and CRLF input are normalized the same way.

## DataTable

`Fby::DataTable` parses RFC-4180-style CSV/TSV into a column header plus rows.
It supports quoted fields with delimiters, CRLF and doubled quotes, and typed
`GetInt`/`GetFloat`/`GetBool` lookups by column name. This is the small
gameplay/UI table layer, not a full spreadsheet formula engine.

## WebSocket

`Fby::Net::WebSocketClient` is a small RFC 6455 client:

- `ws://host:port/path` with blocking connect and non-blocking `Poll`/`Receive`;
- handshake validation through `Sec-WebSocket-Accept`;
- masked client text/binary frames, server fragmentation, ping/pong and close;
- 1 MiB message cap and fixed per-client buffers;
- `MakeWebSocketAccept` is public for local test servers.

`wss://` is rejected explicitly until a TLS transport is added. No new third-party
library is required: the implementation uses the platform TCP socket already
used by the network module.

## Not implemented yet

- WebSocket TLS (`wss://`); plain `ws://` is available now;
- monitor brightness/HDR/color-profile controls; the display profile only reads
  resolution, refresh, scale and primary state.
