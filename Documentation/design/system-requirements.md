# Editor system requirements

[English](system-requirements.md) | [Русский](system-requirements_RU.md)

These are requirements for the **editor and development build**, not for a
shipped game. A finished 2D game is usually lighter than the tools that produced
it. The numbers below are targets validated with the requirements probe; the
probe prints the measured engine baseline, and the editor delta is measured on
the same reference project.

## Reference workloads

| Workload | Scene | Editor use |
|---|---|---|
| Small project | 10k sprites, 1K textures, one viewport, 1080p | small tilemap, modest audio |
| Medium project | 100k sprites, 4K textures, SDF lighting, 1440p | large tilemaps, particles, PIE, voice or multiplayer |

## Minimum

| Component | Target |
|---|---|
| CPU | 4 cores / 8 threads, e.g. Ryzen 3 3100 or Core i3-10100 |
| RAM | 8 GB absolute minimum; 16 GB practical once an IDE and a browser are open |
| GPU | Vulkan 1.4 driver or Metal 2, 2 GB VRAM, e.g. GTX 1050, RX 560, Iris Xe |
| Storage | 10 GB free on an SSD; the build tree adds 2-6 GB |
| Display | 1920x1080 |
| OS | Windows 10 22H2+/11, Ubuntu 22.04+/24.04, macOS 14+ |

## Recommended

| Component | Target |
|---|---|
| CPU | 8 cores / 16 threads, e.g. Ryzen 7 5800X or Core i7-12700 |
| RAM | 16 GB; 32 GB for medium projects with PIE, GI and several editor panels |
| GPU | Vulkan 1.4 or Metal 3, 6 GB VRAM, e.g. RTX 3060, RX 6600, Apple M1 Pro |
| Storage | 25 GB free on NVMe |
| Display | 2560x1440 |
| OS | Windows 11, Ubuntu 24.04, macOS 14+ on Apple Silicon |

## Measurement

`fby_requirements_benchmark` builds a synthetic 10k and 100k sprite scene,
composes 120 frames and reports average frame time, peak resident memory and the
estimated instance-buffer footprint. `fby_editor_requirements_benchmark` adds the
headless editor backbone: component registration, an inspector traversal and a
bounded undo transaction at the same scene sizes. It reports 0.09 / 0.89 ms per
inspector pass, 0.94 / 0.89 us per edit and 10.9 / 9.2 ms PIE start at
10k / 100k; ImGui draw cost is still measured on the reference project. An editor dashboard startup on the
reference GPU held 291-335 MiB RSS after 8-12 s; generated 10k/100k-entity
scenes held 293.4/302.2 MiB and loaded in about 0.6/2.1 s. The editor ran the
10k/100k scenes at ~82-87/~32-34 FPS at 2560x1600. The current numbers are in
[performance baseline](performance-baseline.md).

The probe reports numbers; it does not fail on a slower CI machine. The table is
updated from the probe output, not from a hardware guess. 16K/32K assets are
content-heavy scenarios outside these editor targets: a 16K RGBA8 texture is
1 GB and a 32K RGBA8 texture is 4 GB before mipmaps, so they require a separate
memory budget rather than a higher "maximum" row.
