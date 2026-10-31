# Editor workflows and scene authoring

This document records the editor contracts that are not obvious from the panels:
project resolution, scene lifecycle, shell behaviour, authoring commands and the
dirty/autosave rules. The task walkthrough stays in
[Authoring.md](../md/en/Authoring.md); the forward plan is in
[ROADMAP.md](../ROADMAP.md).

## Project resolution and opening

- `ResolveProjectPaths` accepts either a project folder or a `project.femproj`
  file. A folder is only a project when it contains `project.femproj`; a random
  working directory is never offered as "Current project".
- `fby_editor <project.femproj>` and `--project` open a project directly.
  `SetupScripts/RegisterFemboyProjects.ps1` registers the `.femproj` association
  for the current user.
- File dialogs use SDL's bare extension syntax (`femproj`), not
  `project.femproj`; the longer form made the dialog look for
  `*.project.femproj`.
- New Project opens the created project immediately. The dashboard, the command
  line and the working directory all run the same `OpenProject` path, and a
  project opened at startup loads its default scene instead of leaving the
  engine demo world on screen.

## Scene lifecycle

- `Scene::DestroyAllEntities` clears both the entities and the entity pool.
  EnTT's `registry::clear()` only invalidates slots in this version, so without
  the pool clear the hierarchy kept ghosts after Load/Reload/PIE stop and
  snapshot restore did not start from an empty registry.
- Contact-end events are guarded against dead shapes: destroying a touching body
  (opening a project over the demo world, reloading, Play/Stop) makes Box2D push
  end-touch events whose shape ids are already dead. `EntityFromShape` validates
  the shape and the body before `b2Shape_GetBody`; `sdf_physics` covers a
  touching body destroyed between steps.
- Opening a project replaces the startup demo world with the project's default
  scene, including on the command-line startup path.
- `File > New Scene...` writes an empty TOML scene through the native save
  dialog and opens it. The dialog result reaches the editor thread through a
  lock, because SDL answers on its own dialog thread.

## Shell and presentation

- The editor and the games link as Windows GUI programs
  (`WIN32_EXECUTABLE` plus `mainCRTStartup`); `-DFBY_CONSOLE_APPS=ON` keeps a
  console for development. Assertions go to the log file in both modes.
- The window title is fixed; frame timing lives in the Profiler/Stats panels.
  Escape no longer quits (that could discard unsaved work): quitting is
  `File > Quit` / Ctrl+Q, and the dashboard keeps its own Quit button.
- Typography defaults to 18 px body / 24 px panel titles. A project whose
  typography block still has the untouched 16 px values is migrated once at
  load.
- The default dock layout (versioned next to the ImGui ini) puts the Viewport in
  the centre as the selected tab and docks the timeline, script debugger and
  graph editors below it. An older saved layout is rebuilt once.
- Exit diagnostics log each shutdown phase with its duration
  (`shutdown: gpu idle`, `editor`, `audio`, `simulation`, `plugins`, `gpu`,
  `window`, `shutdown complete`), so a parked exit names its phase. The ImGui
  platform-window handle is cleared on teardown.
- Automation hooks: `FBY_EDITOR_QUIT_AFTER_FRAMES=<n>`,
  `FBY_EDITOR_SMOKE_PROJECT=<folder>` and
  `FBY_EDITOR_SMOKE_DASHBOARD_OPEN=<project file>` exercise create, open and
  teardown without a human click.

## Authoring commands

- `+ Entity` / `Duplicate` / `Delete` exist in the Hierarchy, the Viewport
  toolbar and the `Entity` menu. A new entity carries `Transform2D`, `Sprite2D`
  and `DrawOrder` at the viewport centre and is selected; create and duplicate
  are spawn commands (one undo step each).
- The viewport owns its input: wheel zoom around the cursor, middle/right-drag
  pan, click picking (topmost sprite, falling back to collider extents), Delete
  and F. The old wheel handler lived in the host loop behind `WantCaptureMouse`,
  which is always true over an ImGui window, so it could never fire.
- The viewport toolbar has Move/Rotate/Scale (W/E/R), a World/Local gizmo space
  toggle and one-shot transform steps (X/Y offsets, 5-degree turns, uniform
  scale, Shift for a bigger step). Each step goes through
  `InteractionTransaction::BeginWithSnapshot`, so Ctrl+Z reverts it as one edit.
- Camera: left-dragged zoom (25-800%) and speed (0.25x-4x, scaling pan and arrow
  keys), plus `Fit`; the wheel remains a shortcut.
- The viewport HUD draws an X/Y compass, the current zoom and, when the world
  origin is off-screen, an edge arrow with bearing and distance.
- Snapping: the `Snap` toggle feeds ImGuizmo's snap array (grid in world units,
  15 degrees rotation, 0.1 scale) and the step buttons use the same values.
- Selection is multi-entity through `EditorSelected`: Ctrl+click toggles,
  Shift+click adds. The Inspector keeps component editing to one entity; the
  toolbar and hierarchy buttons act on the whole set.
- Entities carry an optional `NameComponent` (fixed 64-byte string, serialized).
  The Hierarchy shows the name, filters case-insensitively and starts an inline
  rename on double-click as one undo step.
- Clipboard and structure: Ctrl+C copies serializable component images by layout
  hash, Ctrl+V pastes them as new entities, Ctrl+D duplicates; each is one
  transaction. `CommandStack` has a `Destroy` command that stores the
  pre-delete image, so Ctrl+Z resurrects deleted entities.

## Assets and prefabs

- `Entity > Create Prefab from Selection...` copies the selection into a
  temporary registry and saves it as a scene TOML under `assets/prefabs`; the
  Content Browser spawns `.toml` prefabs by drag and drop.
- Content Browser `File` menu: New Folder, New Scene, Rename, Duplicate, Delete,
  confined to the project assets root and confirmed for delete.
  `VirtualFileSystem::RefreshDirectoryMounts()` re-walks directory mounts after
  each operation so the index sees files created outside the VFS write path.
  Double-clicking a `.toml` entry opens it.

## Dirty state and autosave

The dirty marker is derived from the command stack (command count and undo
cursor) every frame, so undoing back to the saved position clears it. The title
shows `*`; closing the window or quitting opens Save / Discard / Cancel; the
dirty-scene guard is covered by `editor_infrastructure`. `File > Autosave scene
(60 s)` saves the open scene on a timer.

PIE `Step` (F10) advances one fixed tick while paused, through the same
fixed-step path as the normal play loop.

## Window presentation

- `WindowMode` (windowed / borderless fullscreen / exclusive fullscreen) lives in
  `WindowManager`. `WindowDesc::mode` applies on creation and `SetWindowMode`
  switches at runtime through SDL's display-mode API.
- Borderless fullscreen is the editor default: it covers the display like
  exclusive fullscreen, but alt-tab, overlays and multi-monitor behave normally
  and no display mode changes. Exclusive mode mirrors the desktop mode, never an
  unsupported resolution.
- The mode is part of `EditorSettings` (persisted in `project.femproj` and
  `editor.ini`) and is applied when a project loads and when the setting changes,
  so one project can start borderless on one machine and windowed on another.
- Switching modes is transactional: if the OS refuses a transition (exclusive
  fullscreen on a remote desktop is the common case), the previous mode is
  restored instead of leaving the window at its authored size. F11 and
  `View > Fullscreen` toggle borderless fullscreen.

## Viewport overlays and polish

- `show_grid` draws a world grid in the viewport that follows the selected
  tilemap's projection: axis-aligned cells for side/top-down, iso diamonds and
  hex lattices for the isometric modes, with the axes highlighted.
- "Navigation Debug Draw" shows every agent's path and arrival radius on the
  selected tilemap's grid; "Light Debug Draw" shows each enabled light's radius
  and position.
- Texture thumbnails: `RHI::TextureArrayLayerView` caches a per-layer 2D view,
  `TextureThumbnails` caches ImGui descriptors per (array, layer), and the sprite
  inspector draws the real sheet with the frame grid and a per-frame highlight.
- Import backpressure: a full completed ring throttles the worker with a
  condition variable (250 ms), counts `blocked` and only drops on shutdown.

## Known limits

- No parent-child transform hierarchy: there is no local/world transform split or
  propagation system yet (engine-level, tracked in [ROADMAP.md](../ROADMAP.md)).
- No prefab instances with overrides: only "spawn a prefab" exists.
- Material/scene creation wizards are limited to the empty TOML scene template.
- Content Browser rename/delete dialogs and the prefab save dialog are
  UI-driven; automation covers only the VFS index and the scene round trip
  (`editor_infrastructure`). They stay on the manual verification list in
  [ROADMAP.md](../ROADMAP.md).
