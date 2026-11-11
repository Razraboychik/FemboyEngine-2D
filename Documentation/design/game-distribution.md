# Building and distributing a game

[English](game-distribution.md) | [Русский](game-distribution_RU.md)

This guide covers a shipping build and the storefront integration points for
Steam, Epic Games Store (EOS) and GOG. It is deliberately separate from the
editor build workflow: a shipped game does not include the editor, the asset
tools or the test suite.

## Shipping build

```bat
cmake -S . -B build-ship -G Ninja ^
      -DCMAKE_BUILD_TYPE=Release ^
      -DFBY_BUILD_EDITOR=OFF ^
      -DFBY_BUILD_TESTS=OFF ^
      -DFBY_BUILD_TOOLS=OFF ^
      -DFBY_BUILD_SANDBOX=OFF ^
      -DFBY_BUILD_SERVER=OFF
cmake --build build-ship --parallel 8
```

The runtime links `FemboyEngine2D::Engine`. The editor, asset cooker and test
targets are excluded from the shipped archive.

## Packaging

`Source/Tools/Dev/PackageDesktop.py` collects the runtime, assets, notices and
the platform service plugin into one directory. The cooked content is written by
`fby_fempak`; the package step never runs the editor.

- Windows: code-sign the executable and the platform service DLL after packaging.
- Linux: ship the Vulkan loader dependency in the store manifest, not in the
  archive.
- macOS: sign and notarize the `.app`; the Metal backend has no Vulkan runtime
  dependency.

## Steam

- Link the Steamworks SDK through `Fby::Platform::PlatformService`; the engine
  does not link the SDK into the runtime directly.
- Map `SteamAppId`, achievements, stats, cloud saves and Steam Input to the
  platform service interface.
- Keep the Steamworks redistributable out of the source tree and out of the
  MPL-covered archive; document it in `THIRD_PARTY_NOTICES.md`.
- Build one depot per platform; the null RHI remains available for headless
  server builds.

## Epic Games Store / EOS

- Use Epic Online Services for achievements, sessions, lobbies and cloud saves.
- The EOS SDK is an optional platform service; a build without it must still
  launch and play offline.
- Keep the EOS overlay and the engine window lifecycle separate: the platform
  service owns the overlay hooks.

## GOG

- GOG Galaxy SDK integration follows the same platform service interface:
  achievements, cloud saves and playtime.
- The DRM-free build must not require a store client at runtime; the Galaxy
  service is optional.
- Offline saves use the same storage path as the other stores.

## Store matrix

| Feature | Steam | EGS / EOS | GOG |
|---|---|---|---|
| Achievements | Steamworks | EOS | Galaxy |
| Cloud saves | Steam Cloud | EOS Player Data Storage | Galaxy Cloud |
| Overlay | Steam overlay | EOS overlay | Galaxy overlay |
| Input | Steam Input | SDL/gamepad | SDL/gamepad |
| DRM-free build | yes | yes | yes |

## Legal

The engine is MPL 2.0. The shipped archive includes `LICENSE` and
`THIRD_PARTY_NOTICES.md`; store SDKs are optional and must be listed in the
notices when they are linked or redistributed.
