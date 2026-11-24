# Developer operations: Doctor, incidents, provenance and replay

**Language section:** English

**Version:** 0.6.0-alpha.1
**Status:** Alpha

## 1. Project Doctor

`Ops::ProjectDoctor::Run` is a read-only health check:

- VFS is mounted and non-empty;
- bundled font files and their license ids exist;
- the legal manifest and documents load;
- every `*.femeta` parses;
- asset GUIDs are unique;
- asset license ids resolve.

It never repairs or writes. Results are `Info`, `Warning` or `Error`.
`DoctorOptions::Strict` promotes warnings to errors.

## 2. Incident bundle

`Ops::WriteIncidentBundle` writes a local directory:

```text
Incidents/<name>/
  README.txt
  manifest.toml
  build.txt
  engine.log
  settings.toml
  replay.fbyrply
  Licenses/
  Legal/
```

The request controls the directory, reason, build info, log limit and which
parts are included. `ArchivePath` optionally writes a store-only ZIP of the same
files. There is no network upload and no hidden telemetry. When
`incident_on_crash` is enabled, the editor installs a signal-safe hook that writes
only `incident-request.txt` beside the crash report. The next editor start scans
`Saved/Crashes` and builds the full bundle at a safe point.

## 3. Provenance and credits

`AssetMeta` carries optional provenance:

```toml
license = "CC-BY-4.0"
author = "Example Artist"
source_url = "https://example.invalid/source"
attribution = "Example Studio"
modified = true
```

`Ops::BuildCreditsMarkdown` emits a credits table and the exact license texts.
A missing license id is rendered as an explicit warning row.

## 4. Deterministic replay

The engine already has `Core::ReplaySession`. `Ops::ReplayArchive` saves and
loads `.fbyrply` files, and `Ops::ReplayPolicy` supplies the frame budget,
keyframe interval and verify-on-stop policy. The project stores the policy; the
recorder stops at the budget instead of growing without bound.

## 5. Project settings

`project.femproj` has an `[ops]` section:

```toml
[ops]
doctor_on_open = true
doctor_strict = false
doctor_check_licenses = true
doctor_check_legal = true
doctor_check_fonts = false
incident_on_crash = false
incident_include_settings = true
incident_include_logs = true
incident_include_replay = true
incident_include_licenses = true
incident_max_log_bytes = 4194304
incident_directory = "Incidents"
replay_auto_record = false
replay_keyframe_interval = 60
replay_max_frames = 216000
replay_verify_on_stop = true
replay_directory = "Replays"
```

Project Settings exposes the same fields under **Developer Operations**, with
**Run Project Doctor** and **Create Incident Bundle** buttons. Defaults are
conservative: Doctor on open is read-only, crash bundles are off, replay
recording is off.

## 6. Universal-engine rules

- all paths are project-relative and configurable;
- no fixed incident or replay location;
- no network or automatic upload;
- missing files are reported, not invented;
- the editor is a client of the Ops APIs, not their owner;
- an actual signal probe and a synthetic crash-marker test are part of CTest;
- a shipped game can call the same APIs without ImGui.

## 7. Limits

The ZIP is store-only (no compression) and is an optional copy of the directory.
The crash marker is written with `CreateFileA`/`open` and no allocation. The
full bundle is built only on the next start, so a fatal path never allocates.
A session lock covers a process killed before its crash handler runs: a normal
exit removes it, and a stale lock becomes an "unclean shutdown" bundle. Doctor checks the project data it can see; it cannot
prove external store compliance or legal validity.