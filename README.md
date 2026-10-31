# FemboyEngine2D

**A data-oriented 2D game engine written from scratch in C++26.**

[![License: MPL 2.0](https://img.shields.io/badge/License-MPL_2.0-brightgreen.svg)](LICENSE)
[![C++26](https://img.shields.io/badge/C%2B%2B-26-blue.svg)]()
[![CMake 3.28+](https://img.shields.io/badge/CMake-3.28%2B-064F8C.svg)]()
[![Platforms](https://img.shields.io/badge/platforms-Windows%20%7C%20Linux%20%7C%20macOS%20%7C%20Android-lightgrey.svg)]()
[![Status: 0.1.0-alpha.1](https://img.shields.io/badge/status-0.1.0--alpha.1-blue.svg)]()

FemboyEngine2D is a compact, deterministic, data-oriented engine for 2D games:
a hand-written Vulkan/Metal RHI, an EnTT-based ECS, a declarative parallel
`SystemGraph`, Box2D v3 physics, rollback networking, a transactional asset
pipeline, a full editor and a clean-slate project generator.

The runtime has no `std::function` on the frame path, no allocation on the frame
path, no JSON in the shipped runtime and no hidden object graph. Components are
trivially copyable data; systems are free functions; the frame is a flat
`RenderItem` array submitted through one RHI packet.

**Status: 0.1.0-alpha.1.** This is the current public alpha line. The tree
includes a working FemboyScript debugger (source
spans, breakpoints, stepping, locals, self fields), isometric and tile-based maps
with a map editor, skeletal IK, retargeting and clip compression (`.fanim`), grid
navigation, 2D lighting, a UDP transport with reliability and MTU fragmentation,
live spatial audio, texture thumbnails in the editor and import backpressure.
Public CMake targets and C++ namespaces are PascalCase
(`FemboyEngine2D::Engine`, `Fby::Ecs`, `Fby::Rhi`); no legacy compatibility
aliases remain. The current gate passes the full CTest suite with Clang 23.1.4
on Windows and Clang on Linux; MSVC is not a supported compiler. The style gate
passes, and a generated Clean Slate project builds and launches with the null RHI.
The engine is strictly 2D: there is no 3D renderer, 3D physics or 3D scene format.

---

## Clean Slate projects

A new project is deliberately empty. `CreateProject()` writes only:

```text
<Name>/
├─ CMakeLists.txt          links FemboyEngine2D::Engine
├─ GameMain.cpp            window -> RHI -> registry -> fixed step -> extract
├─ project.femproj         project descriptor (paths, simulation, render, build)
├─ assets/
│  └─ scenes/main.toml
├─ build/
├─ archives/
└─ stores/
```

There are no player, enemy, camera or gameplay components in the template. The
generated `GameMain.cpp` contains exactly the system boundary every project
needs:

```text
WindowManager (SDL3)
        │
        ▼
RHI (Vulkan / Metal / Null)
        │
        ▼
ecs::Registry + SystemContext
        │
        ▼
SystemGraph: PreUpdate -> FixedUpdate -> PostUpdate -> Render
        │
        ▼
allocation-free render extract -> BeginFrame/Submit/EndFrame
```

Gameplay is added by registering free functions on the graph:

```cpp
ScopedGraph.Add(Fby::Ecs::Phase::FixedUpdate, "my_system", &MySystem, &MyState);
```

No `std::function`, no capturing lambda and no per-frame heap allocation is
required. The template is defined once in
[`Source/Engine/Assets/ProjectScaffold.hpp`](Source/Engine/Assets/ProjectScaffold.hpp)
and is used by the editor dashboard and `CreateProject()`.

---

## What is in the engine

| Domain | Highlights |
|---|---|
| Core | `Definitions.hpp`, `FemCast<>`, checked `Cast<>`, `FORCEINLINE`, `Vector2D`, `Vector3D`, `Vector4D`, Mirror reflection, jobs, SDL3 platform, plugin ABI |
| RHI | Persistent frame rings, 48-byte `RenderItem`, 64-bit keys, Vulkan indirect MDI, one compute dispatch for LBS, Metal and null backends |
| ECS | EnTT registry, trivially copyable components, four-phase schedule, parallel `SystemGraph` with per-call semaphores, Snapshot v4 |
| Physics | Box2D v3 bridge with packed ids, rope/chain joints, Verlet cloth, shallow-water fields, SDF collision |
| Network | `NetRollbackSession`, 256-tick snapshot ring, input prediction, late-input replay, stable `NetEntityId`, relevance filtering, UDP `NetTransport` with acks, retransmit and MTU fragmentation, and versioned `InputPacket`s that feed `SubmitRemoteInput` |
| Assets | VFS with owning `Pin()`, `.femeta` transactions, BC7/ASTC texture cooker, audio/localization cookers, watcher, `.fanim` compressed clips |
| Distribution | `FemArchiver` `.femarc`, optional LZMA, AES-256-GCM / ChaCha20-Poly1305, `fby_fempak secure` |
| Platform | C-ABI `PlatformService`, Steam/EGS providers, deterministic achievements/cloud saves |
| Animation | 2D skeleton SoA, LBS, blend cycles, `AnimGraph`, animation timeline, PIE seqlock hand-off, two-bone/FABRIK IK, retargeting, pose and clip quantization |
| Isometry | `Grid::ProjectionMode` side/top-down/iso-rect/iso-hex, cell/world conversion, depth keys, `TilemapComponent` with a culled render bridge and a tilemap editor |
| Navigation | Cost-grid A* on rect and hex layouts, tilemap-derived maps, authored `NavAgentSettings` separated from runtime paths, per-frame repath budget |
| Lighting | `Light2DComponent` radial lights, ambient, emissive targets, optional Reinhard tonemap, screen-space SDF shadow mask (render-graph compute pass, static `SdfField` + dynamic `Light2DOccluderComponent` casters), sampled per pixel |
| Audio | DSP graph with buses/ducking/reverb, zones, limiter, Ogg/Opus, live spatial gain and pan |
| UI | Procedural PinkUI SDF primitives, DPI scaling, themes, hypertext, a widget kit (border, slider, checkbox, toggle, radio, scrollbar, switcher, overlay, tooltip, text input, text block, nine-slice, custom painters, text buttons), opt-in Tab/arrow focus with an optional ring, inherited visibility, CPU clip rects, a host `TextRuntime` with a HarfBuzz `TextFontStack` in the apps, shaped line-window viewports, accessibility kernel |
| Editor | Transactional `CommandStack`, ImNodes canvases, project manager, PIE Play/Pause/Stop, Play (Multiplayer) with a second client-world viewport, script debugger, tilemap editor, texture thumbnails; starts borderless fullscreen (Windowed / Borderless / Exclusive in Editor Settings) |
| Script | LogicGraph to bytecode VM and LogicGraph to C++26 AOT plugin, debug info (spans, locals), VM breakpoints and stepping; `Compile + Nativize` and an AOT manifest cook gate so a build runs native modules only (`FBY_WITH_SCRIPT_VM=OFF`) |

---

## Setup

One-click scripts install the pinned toolchain and materialise the exact sources
in `third_party/`; CMake then prefers those local checkouts. The manuals are
published as Markdown, Word, PDF and DjVu; open the DjVu editions with Okular (or
any DjVu viewer). The documentation tooling is local to the publishing machine
and is not an engine dependency - see
[Documentation/README.md](Documentation/README.md).

```bat
SetupScripts/Setup.bat
```

```bash
./SetupScripts/Setup.sh          # Ubuntu/Debian
./SetupScripts/Setup.command     # macOS, also works from Finder
```

Versions are fixed at SDL3 `release-3.4.18`, Vulkan-Headers `v1.4.366`, glslang
`15.3.0`, miniaudio `0.11.23`, Box2D `v3.0.0` and LZMA SDK `25.01`.

---

## Build

Requirements: CMake 3.28+, Ninja, Clang 19+ (clang-cl on Windows, clang++ on
Unix) or GCC 15+. On Windows, run from a shell that provides the Windows SDK
headers and libraries; the MSVC compiler itself is not a supported toolchain.
The setup scripts install both compiler families: clang-cl / clang++ for the
validated build and GCC 15+ (MinGW-w64 UCRT on Windows, Homebrew or the
distribution package elsewhere) for the second-opinion toolchain.

The validated build uses **clang-cl 23.1.4 on Windows and clang++ elsewhere**;
that is what CI and the release gate run. GCC 16.2.0 (MinGW-W64 UCRT) is
installed by `Setup.bat` and recorded in the dependency lock; a `-std=c++2c`
smoke program and an engine header syntax check pass with it, but the full gate
has not been run with GCC yet. The honest status for this release is therefore
`GCC not run` for the full build, until a Linux GCC agent joins CI.

```bat
set PATH=C:\Program Files\LLVM\bin;%PATH%

cmake -S . -B build -G Ninja ^
      -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl ^
      -DCMAKE_BUILD_TYPE=RelWithDebInfo ^
      -DFBY_BUILD_TESTS=ON
cmake --build build --parallel 8
ctest --test-dir build --output-on-failure
```

On Linux/macOS use `-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++`.
The Metal backend is selected on Apple; Vulkan on Windows/Linux; the null
backend keeps CI and servers headless.

Main executables:

```text
build/bin/fby_sandbox
build/bin/fby_editor
build/bin/fby_fempak
build/bin/fby_scene_tool
build/bin/fby_aotc
```

The apps resolve `assets` from `FBY_ASSET_DIR`, `cwd/assets`, or the executable's
parent directories, so an IDE launch from `build/bin` works without copying
files. `Saved/` (logs, config, cache) still follows the working directory;
`FBY_STORAGE_DIR` moves it.

---

## Tests

The suite is a set of plain executables, no test framework. It covers snapshots,
rollback, RHI smoke, script VM/AOT, the script debugger (spans, breakpoints,
stepping, fields), audio including live spatial gain/pan, video, cooking,
animation and clip compression, isometric grid math and tilemaps, navigation,
lighting, UDP transport on loopback, editor command transactions, PIE, physics,
SDF, weather, accessibility and data-driven prefabs. The editor/PIE tests verify
that a play session owns an isolated Box2D world and that Stop restores the
authoring scene, and that the PIE client viewport creates a second offscreen
target, renders the replicated client world into it and frees the target on Stop.
The sandbox client test drives `fby_sandbox --pie-client` headlessly: argument
parsing, a baseline and a delta over loopback UDP, and the timeout exit when the
host stops publishing. The PIE voice test carries synthesized client speech to
the host through the PIE loopback voice link and checks mute and push-to-talk
gating. The security tests cover the FBY XX handshake KATs, the signed-ticket
prologue binding, the v2 record layer (`record_layer_v2`), the noise-c XX vector
(`noise_conformance`) and the handshake/voice token buckets (`noise_kats`,
`ticket_binding`, `rate_limit`). The identity test covers the versioned DPAPI /
0600 store and the migration from the bare record (`identity_storage`). The audio
quality tests cover RNNoise wet/dry (`rnnoise_mix`), Opus FEC recovery
(`voice_fec`), the first-packet jitter policy (`jitter_init`), device hash/raw
collisions (`device_id`) and the panel's live output path (`voice_device_panel`).
`ctest -N` prints the current test inventory; this README does not restate a
count that changes with every package.

```bash
ctest --test-dir build --output-on-failure
```

The style gate is machine-checked:

```bash
python Source/Tools/Dev/CheckStyle.py
```

---

## Using the editor

The editor opens a project from three places:

- **Explorer or the command line**: `fby_editor path\to\project.femproj`
  (a project folder or `--project <path>` works too). Register the file
  association once for the current user so a double-click opens the editor:

  ```powershell
  powershell -ExecutionPolicy Bypass -File SetupScripts/RegisterFemboyProjects.ps1
  ```

- **Project dashboard**: it appears when no project is open. Open Project browses
  `*.femproj` files; New Project creates the typed tree under `Projects/` and
  opens it immediately.
- **Working directory**: if the current folder contains `project.femproj`, the
  editor opens it on startup.

The window starts borderless fullscreen; F11 (or View > Fullscreen) toggles it.
Editor typography defaults to 18 px body and 24 px titles (the untouched 16 px
values in older projects are migrated once); the per-category sizes and the UI
scale live in Project Settings > Editor Settings.

**Authoring a scene.** The editor ships without a console window; the log is in
`Saved/Logs/editor`. The Hierarchy and the Viewport toolbar have `+ Entity`,
`Duplicate` and `Delete`, and the `Entity` menu repeats them (Focus Selected is
`F`). A new entity gets a Transform, a 64x64 sprite and a draw order at the
viewport centre and is selected immediately; Add Entity and Duplicate are one
undo step (Ctrl+Z), and Delete is undoable too (the entity returns with all of
its components). `File > New Scene...` writes an empty scene next to the project
scenes and opens it; Ctrl+S saves the current scene.

Entities can carry a name: double-click a Hierarchy row to rename it and use the
search field to filter the list. Selection is multi-entity (Ctrl+click toggles,
Shift+click adds, in the Hierarchy and the Viewport); Ctrl+C / Ctrl+V / Ctrl+D
copy, paste and duplicate the whole selection as one undo step. The Viewport
`Snap` toggle rounds the gizmo and the step buttons to the grid (8/16/32),
15 degrees and 0.1 scale.

`Entity > Create Prefab from Selection...` saves the selected entities under
`assets/prefabs`; the prefab can be dragged from the Content Browser into the
Viewport to spawn it. The Content Browser's `File` menu creates folders and
scenes, renames, duplicates and deletes project assets (Delete asks for
confirmation), and double-clicking a `.toml` scene opens it in the editor.

The window title marks unsaved work with `*`. Closing the window or quitting with
a dirty scene asks Save / Discard / Cancel; `File > Autosave scene (60 s)` keeps
saving the open scene while editing. In Play-In-Editor, `Step` (or F10) advances
exactly one fixed tick while the session stays paused.

The viewport is interactive: the wheel zooms around the cursor, the middle or
right mouse button pans (the Camera speed slider scales both the drag pan and the
arrow keys), a left click selects the topmost sprite or collider under the cursor
(empty space clears the selection), Delete removes the selected entity and F
centres the view on it. A second toolbar row below the first one holds the
transform tools: Move/Rotate/Scale (also W/E/R), a World/Local space toggle and
one-shot steps for the selection (`X -`, `X +`, `Y -`, `Y +`, `-5 deg`/`+5 deg`,
`Scale -`/`Scale +`; hold Shift for a bigger step) — every step is a single
undo. Zoom is a left-dragged slider from 25% to 800% and `Fit` frames the
selection or resets the view, so neither zoom nor camera speed needs the mouse
wheel. A HUD in the corner shows the X/Y compass and the zoom factor, and when
the world origin is outside the visible area an arrow on the viewport edge points
to it with its bearing in degrees and the distance in world units (`0,0 209 deg
734 u`). Escape no longer closes the editor (it used to discard unsaved work);
quit from `File > Quit` or Ctrl+Q.

Automated checks can use `FBY_EDITOR_QUIT_AFTER_FRAMES=<n>` (run n frames and
shut down), `FBY_EDITOR_SMOKE_PROJECT=<folder>` (create the project if it is
missing, then open it) and `FBY_EDITOR_SMOKE_DASHBOARD_OPEN=<project file>`
(press Open on the dashboard after `FBY_EDITOR_SMOKE_DASHBOARD_FRAME` frames,
default 600). The log records every shutdown phase in milliseconds, so a stalled
exit names the phase it is stuck in.

The editor and the games are linked as Windows GUI programs: starting them from
Explorer or a shortcut shows only the game window. Configure with
`-DFBY_CONSOLE_APPS=ON` to keep a console attached for development; assertions
are written to the log file in both cases.

---

## Documentation

The documentation set is English/Russian, with independent Markdown, Word, PDF
and DjVu editions. Start at [Documentation](Documentation/README.md).

- [Architecture specification (EN)](Documentation/md/en/FemboyEngineArchitecture_EN.md)
- [Architecture specification (RU)](Documentation/md/ru/FemboyEngineArchitecture_RU.md)
- [Application guide (EN)](Documentation/md/en/FemboyEngineApplicationGuide_EN.md)
- [Application guide (RU)](Documentation/md/ru/FemboyEngineApplicationGuide_RU.md)
- [API reference (EN)](Documentation/md/en/FemboyEngineAPIReference_EN.md)
- [API reference (RU)](Documentation/md/ru/FemboyEngineAPIReference_RU.md)
- [Complete reference manual (EN)](Documentation/md/en/FemboyEngineCompleteReference_EN.md)
- [Complete reference manual (RU)](Documentation/md/ru/FemboyEngineCompleteReference_RU.md)
- [CMake project and engine management (EN)](Documentation/md/en/CMakeProjectManagement_EN.md)
- [CMake project and engine management (RU)](Documentation/md/ru/CMakeProjectManagement_RU.md)
- [FemboyScript manual (EN)](Documentation/md/en/FemboyScript_EN.md)
- [FemboyScript manual (RU)](Documentation/md/ru/FemboyScript_RU.md)
- [Project settings recommendations (EN)](Documentation/md/en/ProjectSettingsRecommendations_EN.md)
- [Project settings recommendations (RU)](Documentation/md/ru/ProjectSettingsRecommendations_RU.md)
- [Code style](Documentation/style/CodeStyle.md)
- [Naming conventions](Documentation/style/NAMING.md)
- [Roadmap](Documentation/ROADMAP.md)
- [Virtual texturing contract](Documentation/design/virtual-texturing.md)
- [Editor workflows and scene authoring](Documentation/design/editor-workflows.md)
- [Play-In-Editor and the client viewport](Documentation/design/pie-multiplayer.md)
- [Text stack](Documentation/design/text-stack.md)
- [Time, timers and dates](Documentation/design/time.md)
- [Accessibility contract](Documentation/design/accessibility.md)
- [World systems: tilemaps, IK and navigation](Documentation/design/world-systems.md)
- [Lighting and SDF shadows](Documentation/design/lighting.md)
- [Networking: transport and replication](Documentation/design/networking.md)
- [Scripting: debugger and nativization](Documentation/design/scripting.md)
- [Voice subsystem: capture, DSP, AEC and networking](Documentation/design/voice.md)
- [Security notes: FBY XX, ticket binding, rate limits](Documentation/design/security.md)
- [Protocol v2 binding and finishing checklist](Documentation/design/protocol-v2.md)
- [ADR 0001: standard Noise as the long-term secure transport](Documentation/adr/0001-secure-transport.md)

The generated documentation keeps its historical `FemboyEngine*` file names so
existing links remain valid; the product name inside the documents is
FemboyEngine2D.

---

## Repository layout

```text
FemboyEngine2D/
├─ CMakeLists.txt, cmake/        build configuration and dependency pins
├─ Source/
│  ├─ Engine/                    Core, RHI, ECS, Physics, Audio, Network,
│  │                             Assets, Script, UI, Video, Weather
│  ├─ Editor/                    Panels + Infrastructure
│  ├─ Apps/                      Editor + Sandbox executables
│  ├─ Tests/                     plain CTest executables
│  ├─ Tools/                     shader compiler, cooker, AOT, docs tools
│  └─ Plugins/                   C-ABI fixtures and storefront providers
├─ Documentation/                MD/DOCX/PDF/DjVu editions, design notes, generators
├─ assets/                       source assets and cooked samples
└─ third_party/                  vendored dependencies
```

Every source file is PascalCase. Inputs are `In*`, outputs `Out*`, locals
`Scoped*`, members `M*`. Braces follow Allman; `if`/`for` bodies are always
braced; comments are English-only, sit above the block they describe and contain
no ASCII-art banners. `.clang-format` owns the layout, `.clang-tidy` mirrors the
naming and brace rules, and `Source/Tools/Dev/CheckStyle.py` binds both to CI.

---

## License

FemboyEngine2D is released under the **Mozilla Public License 2.0**. See
[LICENSE](LICENSE). Third-party notices are in `THIRD_PARTY_NOTICES.md` where
present.

---

## Contributing and security

- [CONTRIBUTING.md](.github/CONTRIBUTING.md)
- [SECURITY.md](.github/SECURITY.md)
- [CODE_OF_CONDUCT.md](.github/CODE_OF_CONDUCT.md)

The 0.1 alpha line is the first public baseline. It will develop with community
feedback; 1.0 is reserved for the own-library milestone.

---

<div align="center">────────────  РУССКАЯ ВЕРСИЯ  ────────────</div>

# FemboyEngine2D

**Data-oriented 2D-движок для игр, написанный с нуля на C++26.**

FemboyEngine2D — компактный, детерминированный движок для 2D-игр: собственный
RHI на Vulkan/Metal, ECS на EnTT, декларативный параллельный `SystemGraph`,
физика Box2D v3, rollback-сеть, транзакционный ассет-пайплайн, полноценный
редактор и генератор чистых проектов.

В рантайме нет `std::function` на кадровом пути, нет аллокаций на кадровом пути,
нет JSON в поставляемом рантайме и нет скрытого объектного графа. Компоненты —
тривиально копируемые данные; системы — свободные функции; кадр — плоский массив
`RenderItem`, отправляемый одним RHI-пакетом.

**Статус: 0.1.0-alpha.1.** Это текущая публичная alpha-линия. В дереве есть
рабочий отладчик FemboyScript (spans, breakpoints, stepping, locals, self-поля),
изометрические и тайловые карты с редактором, скелетный IK, retargeting и сжатие
клипов (`.fanim`), навигация по сетке, 2D-освещение, UDP-транспорт с надёжностью
и MTU-фрагментацией, живой пространственный звук, превью текстур в редакторе и
backpressure при импорте. Публичные CMake-цели и
C++-пространства имён — PascalCase (`FemboyEngine2D::Engine`, `Fby::Ecs`,
`Fby::Rhi`); legacy-алиасов нет. Текущий гейт проходит полный CTest с Clang
23.1.4 на Windows и Clang на Linux; MSVC не поддерживается как компилятор.
Style gate проходит, сгенерированный Clean Slate проект собирается и
запускается с null RHI. Движок строго 2D: в нём нет 3D-рендерера, 3D-физики и
3D-формата сцен.

---

## Чистые проекты

Новый проект намеренно пуст. `CreateProject()` создаёт только:

```text
<Name>/
├─ CMakeLists.txt          подключает FemboyEngine2D::Engine
├─ GameMain.cpp            окно -> RHI -> registry -> fixed step -> extract
├─ project.femproj         дескриптор проекта (пути, симуляция, рендер, сборка)
├─ assets/
│  └─ scenes/main.toml
├─ build/
├─ archives/
└─ stores/
```

В шаблоне нет компонентов игрока, врага, камеры или геймплея. Сгенерированный
`GameMain.cpp` содержит ровно ту границу систем, которая нужна любому проекту:

```text
WindowManager (SDL3)
        │
        ▼
RHI (Vulkan / Metal / Null)
        │
        ▼
ecs::Registry + SystemContext
        │
        ▼
SystemGraph: PreUpdate -> FixedUpdate -> PostUpdate -> Render
        │
        ▼
allocation-free render extract -> BeginFrame/Submit/EndFrame
```

Геймплей добавляется регистрацией свободных функций в графе:

```cpp
ScopedGraph.Add(Fby::Ecs::Phase::FixedUpdate, "my_system", &MySystem, &MyState);
```

Не требуются `std::function`, захватывающие лямбды и аллокации в куче на кадр.
Шаблон определён один раз в
[`Source/Engine/Assets/ProjectScaffold.hpp`](Source/Engine/Assets/ProjectScaffold.hpp)
и используется панелью проекта и `CreateProject()`.

---

## Что есть в движке

| Домен | Возможности |
|---|---|
| Core | `Definitions.hpp`, `FemCast<>`, проверяемый `Cast<>`, `FORCEINLINE`, `Vector2D`, `Vector3D`, `Vector4D`, Mirror-рефлексия, jobs, платформа SDL3, plugin ABI |
| RHI | Постоянные кольца кадров, 48-байтный `RenderItem`, 64-битные ключи, Vulkan indirect MDI, один compute dispatch для LBS, Metal и null backends |
| ECS | EnTT registry, тривиально копируемые компоненты, четырёхфазный schedule, параллельный `SystemGraph` с семафорами на вызов, Snapshot v4 |
| Физика | Мост Box2D v3 с упакованными id, верёвочные/цепные джойнты, ткань Verlet, поля мелкой воды, SDF-коллизии |
| Сеть | `NetRollbackSession`, кольцо снапшотов на 256 тиков, предсказание ввода, повтор позднего ввода, стабильный `NetEntityId`, фильтрация релевантности, UDP `NetTransport` с ack, ретрансмиссией и MTU-фрагментацией, версионированные `InputPacket` для `SubmitRemoteInput` |
| Ассеты | VFS с владеющим `Pin()`, транзакции `.femeta`, кукер текстур BC7/ASTC, аудио/локализационные кукеры, watcher, сжатые клипы `.fanim` |
| Дистрибуция | `FemArchiver` `.femarc`, опциональный LZMA, AES-256-GCM / ChaCha20-Poly1305, `fby_fempak secure` |
| Платформа | C-ABI `PlatformService`, провайдеры Steam/EGS, детерминированные достижения и облачные сохранения |
| Анимация | 2D-скелет SoA, LBS, blend-циклы, `AnimGraph`, таймлайн анимации, PIE seqlock hand-off, two-bone/FABRIK IK, retargeting, квантование поз и клипов |
| Изометрия | `Grid::ProjectionMode` side/top-down/iso-rect/iso-hex, преобразование клетка/мир, depth-ключи, `TilemapComponent` с culled render bridge и редактором тайлмапов |
| Навигация | A* по cost-grid на прямоугольных и гексагональных сетках, карты из тайлмапов, авторские `NavAgentSettings` отдельно от runtime-путей, бюджет repath на кадр |
| Освещение | Радиальные источники `Light2DComponent`, ambient, emissive-цели, опциональный Reinhard-тонмаппинг, экранная SDF-маска теней (compute-pass графа рендера, статический `SdfField` + динамические `Light2DOccluderComponent`), выборка на пиксель |
| Аудио | DSP-граф с шинами/ducking/reverb, зоны, лимитер, Ogg/Opus, живое пространственное усиление и панорама |
| UI | Процедурные SDF-примитивы PinkUI, DPI-масштабирование, темы, гипертекст, набор виджетов (border, slider, checkbox, toggle, radio, scrollbar, switcher, overlay, tooltip, text input, text block, nine-slice, custom painters, text buttons), опциональный Tab/arrow-фокус с кольцом, наследуемая видимость, CPU clip rects, host `TextRuntime` с HarfBuzz `TextFontStack` в приложениях, shaped line-window viewports, accessibility kernel |
| Редактор | Транзакционный `CommandStack`, канвасы ImNodes, менеджер проектов, PIE Play/Pause/Stop, Play (Multiplayer) со вторым viewport клиентского мира, отладчик скриптов, редактор тайлмапов, превью текстур; стартует в borderless fullscreen (Windowed / Borderless / Exclusive в Editor Settings) |
| Скрипты | LogicGraph в bytecode VM и LogicGraph в C++26 AOT-плагин, debug info (spans, locals), breakpoints и stepping в VM; `Compile + Nativize` и AOT manifest cook gate, чтобы сборка запускала только нативные модули (`FBY_WITH_SCRIPT_VM=OFF`) |

---

## Установка

Скрипты в один клик устанавливают закреплённый toolchain и раскладывают точные
исходники в `third_party/`; CMake предпочитает эти локальные копии. Руководства
публикуются в Markdown, Word, PDF и DjVu; DjVu открывается в Okular (или любом
DjVu-просмотрщике). Инструменты документации локальны для машины публикации и не
являются зависимостью движка — см.
[Documentation/README.md](Documentation/README.md).

```bat
SetupScripts/Setup.bat
```

```bash
./SetupScripts/Setup.sh          # Ubuntu/Debian
./SetupScripts/Setup.command     # macOS, работает и из Finder
```

Версии закреплены: SDL3 `release-3.4.18`, Vulkan-Headers `v1.4.366`, glslang
`15.3.0`, miniaudio `0.11.23`, Box2D `v3.0.0` и LZMA SDK `25.01`.

---

## Сборка

Требования: CMake 3.28+, Ninja, Clang 19+ (clang-cl на Windows, clang++ на
Unix) или GCC 15+. На Windows запускайте из оболочки, которая предоставляет
заголовки и библиотеки Windows SDK; сам компилятор MSVC не поддерживается.
Скрипты установки ставят оба семейства компиляторов: clang-cl / clang++ для
проверенной сборки и GCC 15+ (MinGW-w64 UCRT на Windows, Homebrew или пакет
дистрибутива в остальных случаях) для второго мнения.

Проверенная сборка использует **clang-cl 23.1.4 на Windows и clang++ на других
платформах**; именно её запускают CI и release gate. GCC 16.2.0 (MinGW-W64 UCRT)
устанавливается `Setup.bat` и записывается в dependency lock; smoke-программа
`-std=c++2c` и проверка синтаксиса заголовков движка проходят, но полный гейт
под GCC пока не запускался. Честный статус этой версии — `GCC not run` для
полной сборки, пока в CI не появится Linux GCC-агент.

```bat
set PATH=C:\Program Files\LLVM\bin;%PATH%

cmake -S . -B build -G Ninja ^
      -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl ^
      -DCMAKE_BUILD_TYPE=RelWithDebInfo ^
      -DFBY_BUILD_TESTS=ON
cmake --build build --parallel 8
ctest --test-dir build --output-on-failure
```

На Linux/macOS используйте `-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++`.
На Apple выбирается Metal backend; на Windows/Linux — Vulkan; null backend
оставляет CI и серверы без графического окружения.

Основные исполняемые файлы:

```text
build/bin/fby_sandbox
build/bin/fby_editor
build/bin/fby_fempak
build/bin/fby_scene_tool
build/bin/fby_aotc
```

Приложения ищут `assets` в `FBY_ASSET_DIR`, `cwd/assets` или родительских
каталогах исполняемого файла, поэтому запуск из `build/bin` в IDE работает без
копирования файлов. `Saved/` (логи, конфиг, кеш) по-прежнему следует за рабочей
директорией; `FBY_STORAGE_DIR` переносит его.

---

## Тесты

Набор тестов — это обычные исполняемые файлы без тестового фреймворка. Они
покрывают снапшоты, rollback, smoke RHI, script VM/AOT, отладчик скриптов
(spans, breakpoints, stepping, fields), аудио включая живое пространственное
усиление/панораму, видео, кукинг, анимацию и сжатие клипов, изометрическую
математику сетки и тайлмапы, навигацию, освещение, UDP-транспорт на loopback,
транзакции команд редактора, PIE, физику, SDF, погоду, доступность и
data-driven префабы. Тесты редактора/PIE проверяют, что игровая сессия владеет
изолированным миром Box2D и что Stop восстанавливает авторскую сцену, а также
что viewport PIE-клиента создаёт второй offscreen-таргет, рендерит в него
реплицированный клиентский мир и освобождает таргет при Stop. Тест sandbox-
клиента headless-образом прогоняет `fby_sandbox --pie-client`: разбор
аргументов, baseline и delta по loopback UDP и выход по таймауту, когда хост
перестаёт публиковать. Тест PIE-голоса передаёт синтезированную речь клиента на
хост через loopback-ссылку PIE и проверяет mute и push-to-talk. Тесты
безопасности покрывают KAT-векторы FBY XX, привязку подписанного тикета к
прологу, record layer v2 (`record_layer_v2`), вектор noise-c XX
(`noise_conformance`) и token buckets handshake/voice (`noise_kats`,
`ticket_binding`, `rate_limit`). Тест identity покрывает версионированное
хранилище DPAPI / 0600 и миграцию из старой записи (`identity_storage`). Тесты
качества аудио покрывают RNNoise wet/dry (`rnnoise_mix`), восстановление Opus
FEC (`voice_fec`), политику jitter первого пакета (`jitter_init`), коллизии
device hash/raw (`device_id`) и живой выход панели (`voice_device_panel`).
`ctest -N` печатает актуальный список тестов; README не повторяет число, которое
меняется с каждым пакетом.

```bash
ctest --test-dir build --output-on-failure
```

Style gate проверяется машиной:

```bash
python Source/Tools/Dev/CheckStyle.py
```

---

## Работа с редактором

Редактор открывает проект из трёх мест:

- **Проводник или командная строка**: `fby_editor path\to\project.femproj`
  (папка проекта или `--project <path>` тоже работают). Один раз
  зарегистрируйте ассоциацию файлов для текущего пользователя, чтобы двойной
  клик открывал редактор:

  ```powershell
  powershell -ExecutionPolicy Bypass -File SetupScripts/RegisterFemboyProjects.ps1
  ```

- **Панель проектов**: появляется, когда проект не открыт. Open Project
  показывает `*.femproj`; New Project создаёт типизированное дерево в
  `Projects/` и сразу открывает его.
- **Рабочая директория**: если в текущей папке есть `project.femproj`,
  редактор открывает его при старте.

Окно стартует в borderless fullscreen; F11 (или View > Fullscreen)
переключает режим. Типографика редактора по умолчанию — 18 px для тела и 24 px
для заголовков (нетронутые 16 px в старых проектах мигрируются один раз);
размеры по категориям и масштаб UI находятся в Project Settings > Editor
Settings.

**Создание сцены.** Редактор поставляется без окна консоли; лог находится в
`Saved/Logs/editor`. В Hierarchy и на панели Viewport есть `+ Entity`,
`Duplicate` и `Delete`, меню `Entity` повторяет их (Focus Selected — `F`).
Новая сущность получает Transform, спрайт 64x64 и draw order в центре viewport и
сразу выделяется; Add Entity и Duplicate — один шаг undo (Ctrl+Z), Delete тоже
отменяется (сущность возвращается со всеми компонентами). `File > New Scene...`
записывает пустую сцену рядом со сценами проекта и открывает её; Ctrl+S
сохраняет текущую сцену.

Сущности могут иметь имя: двойной клик по строке Hierarchy переименовывает её,
поле поиска фильтрует список. Выделение — многосущностное (Ctrl+click
переключает, Shift+click добавляет, в Hierarchy и Viewport); Ctrl+C / Ctrl+V /
Ctrl+D копируют, вставляют и дублируют всё выделение одним шагом undo.
Переключатель `Snap` во Viewport привязывает гизмо и кнопки шага к сетке
(8/16/32), 15 градусам и масштабу 0.1.

`Entity > Create Prefab from Selection...` сохраняет выделенные сущности в
`assets/prefabs`; префаб можно перетащить из Content Browser во Viewport, чтобы
заспавнить его. Меню `File` в Content Browser создаёт папки и сцены,
переименовывает, дублирует и удаляет ассеты проекта (Delete спрашивает
подтверждение), а двойной клик по `.toml`-сцене открывает её в редакторе.

Заголовок окна помечает несохранённую работу `*`. Закрытие окна или выход с
изменённой сценой спрашивает Save / Discard / Cancel; `File > Autosave scene
(60 s)` продолжает сохранять открытую сцену во время редактирования. В
Play-In-Editor `Step` (или F10) продвигает ровно один фиксированный тик, пока
сессия остаётся на паузе.

Viewport интерактивен: колесо масштабирует вокруг курсора, средняя или правая
кнопка мыши панорамирует (ползунок Camera speed масштабирует и drag-pan, и
стрелки), левый клик выбирает самый верхний спрайт или коллайдер под курсором
(пустое место сбрасывает выделение), Delete удаляет выбранную сущность, F
центрирует на ней вид. Вторая строка панели инструментов содержит
преобразования: Move/Rotate/Scale (также W/E/R), переключатель World/Local и
одноразовые шаги для выделения (`X -`, `X +`, `Y -`, `Y +`, `-5 deg`/`+5 deg`,
`Scale -`/`Scale +`; Shift — больший шаг) — каждый шаг это один undo. Zoom —
ползунок от 25% до 800%, `Fit` кадрирует выделение или сбрасывает вид, поэтому
ни zoom, ни скорость камеры не требуют колеса. HUD в углу показывает компас
X/Y и коэффициент масштаба, а если мировой ноль вне видимой области, стрелка на
краю viewport указывает на него с азимутом в градусах и расстоянием в мировых
единицах (`0,0 209 deg 734 u`). Escape больше не закрывает редактор (раньше
это могло потерять несохранённую работу); выход — `File > Quit` или Ctrl+Q.

Автоматические проверки могут использовать `FBY_EDITOR_QUIT_AFTER_FRAMES=<n>`
(выполнить n кадров и завершиться), `FBY_EDITOR_SMOKE_PROJECT=<folder>` (создать
проект, если его нет, затем открыть) и `FBY_EDITOR_SMOKE_DASHBOARD_OPEN=<project
file>` (нажать Open на панели после `FBY_EDITOR_SMOKE_DASHBOARD_FRAME` кадров,
по умолчанию 600). Лог записывает каждую фазу завершения в миллисекундах, поэтому
зависший выход называет фазу.

Редактор и игры собраны как Windows GUI-программы: запуск из Проводника или
ярлыка показывает только окно игры. Сборка с `-DFBY_CONSOLE_APPS=ON` оставляет
консоль для разработки; в обоих случаях утверждения пишутся в лог-файл.

---

## Документация

Документация доступна на английском и русском, с независимыми изданиями
Markdown, Word, PDF и DjVu. Начните с [Documentation](Documentation/README.md).

- [Архитектура (EN)](Documentation/md/en/FemboyEngineArchitecture_EN.md)
- [Архитектура (RU)](Documentation/md/ru/FemboyEngineArchitecture_RU.md)
- [Практическое руководство (EN)](Documentation/md/en/FemboyEngineApplicationGuide_EN.md)
- [Практическое руководство (RU)](Documentation/md/ru/FemboyEngineApplicationGuide_RU.md)
- [API Reference (EN)](Documentation/md/en/FemboyEngineAPIReference_EN.md)
- [API Reference (RU)](Documentation/md/ru/FemboyEngineAPIReference_RU.md)
- [Полное руководство (EN)](Documentation/md/en/FemboyEngineCompleteReference_EN.md)
- [Полное руководство (RU)](Documentation/md/ru/FemboyEngineCompleteReference_RU.md)
- [CMake и управление движком (EN)](Documentation/md/en/CMakeProjectManagement_EN.md)
- [CMake и управление движком (RU)](Documentation/md/ru/CMakeProjectManagement_RU.md)
- [Руководство FemboyScript (EN)](Documentation/md/en/FemboyScript_EN.md)
- [Руководство FemboyScript (RU)](Documentation/md/ru/FemboyScript_RU.md)
- [Рекомендации по настройкам проекта (EN)](Documentation/md/en/ProjectSettingsRecommendations_EN.md)
- [Рекомендации по настройкам проекта (RU)](Documentation/md/ru/ProjectSettingsRecommendations_RU.md)
- [Стиль кода](Documentation/style/CodeStyle.md)
- [Соглашения об именовании](Documentation/style/NAMING.md)
- [Roadmap](Documentation/ROADMAP.md)
- [Контракт виртуальных текстур](Documentation/design/virtual-texturing.md)
- [Workflow редактора и создание сцен](Documentation/design/editor-workflows.md)
- [Play-In-Editor и viewport клиента](Documentation/design/pie-multiplayer.md)
- [Текстовый стек](Documentation/design/text-stack.md)
- [Время, таймеры и даты](Documentation/design/time.md)
- [Контракт доступности](Documentation/design/accessibility.md)
- [Мировые системы: тайлмапы, IK и навигация](Documentation/design/world-systems.md)
- [Освещение и SDF-тени](Documentation/design/lighting.md)
- [Сеть: транспорт и репликация](Documentation/design/networking.md)
- [Скрипты: отладчик и нативизация](Documentation/design/scripting.md)
- [Голосовая подсистема: захват, DSP, AEC и сеть](Documentation/design/voice.md)
- [Безопасность: FBY XX, привязка тикетов, rate limits](Documentation/design/security.md)
- [Protocol v2: привязка и чек-лист завершения](Documentation/design/protocol-v2.md)
- [ADR 0001: стандартный Noise как долгосрочный защищённый транспорт](Documentation/adr/0001-secure-transport.md)

Сгенерированные документы сохраняют исторические имена `FemboyEngine*`, чтобы
существующие ссылки продолжали работать; внутри документов продукт называется
FemboyEngine2D.

---

## Структура репозитория

```text
FemboyEngine2D/
├─ CMakeLists.txt, cmake/        конфигурация сборки и пины зависимостей
├─ Source/
│  ├─ Engine/                    Core, RHI, ECS, Physics, Audio, Network,
│  │                             Assets, Script, UI, Video, Weather
│  ├─ Editor/                    Panels + Infrastructure
│  ├─ Apps/                      исполняемые файлы редактора и sandbox
│  ├─ Tests/                     обычные CTest-исполняемые файлы
│  ├─ Tools/                     компилятор шейдеров, кукер, AOT, инструменты документации
│  └─ Plugins/                   C-ABI фикстуры и провайдеры магазинов
├─ Documentation/                издания MD/DOCX/PDF/DjVu, заметки, генераторы
├─ assets/                       исходные ассеты и приготовленные примеры
└─ third_party/                  вендоренные зависимости
```

Каждый исходный файл — PascalCase. Входные параметры — `In*`, выходные — `Out*`,
локальные — `Scoped*`, члены — `M*`. Скобки по Allman; тела `if`/`for` всегда
в скобках; комментарии только на английском, стоят над блоком, который
описывают, и не содержат ASCII-арта. `.clang-format` задаёт layout,
`.clang-tidy` повторяет правила имён и скобок, а
`Source/Tools/Dev/CheckStyle.py` связывает оба с CI.

---

## Лицензия

FemboyEngine2D распространяется по **Mozilla Public License 2.0**. См.
[LICENSE](LICENSE). Уведомления третьих сторон находятся в
`THIRD_PARTY_NOTICES.md`, где он присутствует.

---

## Участие и безопасность

- [CONTRIBUTING.md](.github/CONTRIBUTING.md)
- [SECURITY.md](.github/SECURITY.md)
- [CODE_OF_CONDUCT.md](.github/CODE_OF_CONDUCT.md)

Линия 0.1 alpha — первый публичный baseline. Она развивается с учётом отзывов
сообщества; версия 1.0 зарезервирована для перехода на собственные библиотеки.
