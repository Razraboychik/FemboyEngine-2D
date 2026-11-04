# Multi-window and PIE presenter

## Model

One simulation, many presenters.

```text
WindowHost
  Window 0: editor  -> WindowManager + RHI + swapchain
  Window 1: PIE     -> WindowManager + RHI + swapchain
  Window N: online  -> WindowManager + RHI + swapchain
```

Each window owns:

- `WindowManager` (SDL window);
- `Rhi::RHI` (device + swapchain);
- its own input snapshot.

The shared simulation state is not owned by a window. This keeps online games
and PIE on the same architecture: a match can have several windows or clients,
while the world state stays shared.

## WindowHost

`Engine/Core/WindowHost.hpp`:

```cpp
WindowHost::Handle Create(const WindowDesc&, const Rhi::DeviceDesc&);
void Destroy(Handle);
WindowManager* Window(Handle);
Rhi::RHI* Gpu(Handle);
bool PumpEvents();
```

`WindowManager` remains one-window-per-instance. `WindowHost` composes any
number of them, so no global singleton window state is introduced.

## PIE

PIE uses a separate window and its own RHI. The editor viewport can keep its
texture-space preview, while the PIE window is the real swapchain presenter.

The PIE window must not own game state. It receives the same extracted frame as
the editor and presents it. This avoids a second ECS/render extraction path.

## Online games

- one world per match/session;
- one or more windows per client;
- per-window RHI;
- shared simulation and network state;
- no window-specific game logic.

## Status

- `WindowHost` implemented and compiles.
- PIE creates a second window and a second RHI when PIE starts.
- Each frame the editor captures the PIE display texture, uploads it to the PIE
  device as a texture array layer, draws one fullscreen sprite into a PIE render
  target, presents it and ends the PIE frame.
- The PIE window has its own swapchain and never owns ECS/game state.
- Editor runtime validation still exits 127 in this environment; the PIE window
  path is compile-verified only.
