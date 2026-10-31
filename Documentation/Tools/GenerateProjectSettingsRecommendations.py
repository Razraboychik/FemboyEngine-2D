#!/usr/bin/env python3
"""Generates the general project-settings recommendations document.

Outputs (all three formats, both languages):
    Documentation/md/{en,ru}/ProjectSettingsRecommendations_{EN,RU}.md
    Documentation/docx/{en,ru}/ProjectSettingsRecommendations_{EN,RU}.docx
    Documentation/pdf/{en,ru}/ProjectSettingsRecommendations_{EN,RU}.pdf

Recommendations are split by game scale: light, medium and large productions.
The DOCX is the source for the PDF export (LibreOffice Kit CLI).
"""

from __future__ import annotations

import os
import pathlib
import subprocess
import sys

from docx import Document
from docx.enum.text import WD_ALIGN_PARAGRAPH
from docx.oxml import OxmlElement
from docx.shared import Pt

sys.dont_write_bytecode = True
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import DocPaths  # noqa: E402
import DocumentConvert as docconvert  # noqa: E402
from DocVersion import VERSION as DOC_VERSION  # noqa: E402

NODE = pathlib.Path(os.environ.get("FBY_NODE", "node"))
LIBREOFFICE_CLI = pathlib.Path(os.environ.get("FBY_LO_CLI", ""))


def MatrixHeadersEn() -> list[str]:
    return ["Setting", "Light", "Medium", "Large"]


def MatrixHeadersRu() -> list[str]:
    return ["Параметр", "Лёгкая", "Средняя", "Крупная"]


def ContentEn() -> list[tuple]:
    return [
        ("h1", "Project Settings Recommendations"),
        ("h1sub", f"FemboyEngine2D {DOC_VERSION} - by game scale: light, medium, large"),
        ("p", "Every project descriptor (project.femproj) mixes three kinds of settings: "
              "gameplay simulation, shipping quality and build policy. The same numbers do not fit a "
              "two-week jam and a streaming open world. This guide gives one recommendation column per "
              "scale, explains the cost behind each value, and lists the mistakes that show up in "
              "profiles and bug reports. Treat the values as starting points and measure before "
              "shipping."),
        ("h2", "Three scale profiles"),
        ("table", ["Profile", "Typical game", "Assets", "Target"], [
            ["Light", "Game jam, visual novel, puzzle, small pixel platformer", "< 200 MB",
             "60 FPS at 1080p on integrated graphics, CPU frame < 6 ms"],
            ["Medium", "Commercial metroidvania, roguelike, action platformer", "0.2-1 GB",
             "60 FPS at 1440p on a mid-range GPU, CPU frame < 8 ms"],
            ["Large", "Open world, large streaming tilemaps, heavy zoom and pan", "1-8 GB",
             "60+ FPS at 4K on a discrete GPU, CPU frame < 12 ms, VT"],
        ]),
        ("h2", "At a glance"),
        ("table", MatrixHeadersEn(), [
            ["fixed_update_hz", "60", "120", "120 (240 for fighting/rhythm)"],
            ["max_fixed_steps", "4", "8", "8"],
            ["master/sfx/music", "1.0 / 1.0 / 1.0", "1.0 / 1.0 / 1.0", "1.0 / 1.0 / 1.0, mix at asset level"],
            ["blind_mode / deaf_mode", "false, options menu", "false, options menu", "false, options menu"],
            ["flash_guard", "true", "true", "true"],
            ["flash_threshold / flash_gain", "0.20 / 0.35", "0.20 / 0.35", "0.15 / 0.25 for strict safety"],
            ["oled_pixel_shift / interval", "true / 60 s", "true / 60 s", "true / 30-60 s"],
            ["texture_format (art-driven)", "rgba8 pixel art; bc7 painted", "bc7-srgb; rgba8 for pixel art", "bc7-srgb desktop; astc4x4-srgb mobile"],
            ["max_texture_size", "1024", "2048", "4096"],
            ["build.optimization", "O2", "O2", "O3 (measure O2+LTO first)"],
            ["build.link_time_optimization", "false for iteration", "true for release", "true for release"],
            ["build.strip_debug_info", "true for shipping", "true for shipping", "true for shipping"],
            ["build.unity_build", "true", "true", "true; disable only for build memory"],
            ["build.native_arch", "false", "false", "false"],
            ["collision layers", "3-4 used", "up to 6 used", "named set, unused pairs off"],
            ["virtual_texture.enabled", "false", "true for zoom-heavy maps", "true"],
            ["VT page/tile/layers/table", "-", "1024/64/2/128", "2048/64/4/256"],
            ["VT budget / prefetch", "-", "6 / 1", "8 / 2"],
            ["editor.ui_scale", "1.0", "1.0-1.25", "1.0-1.25"],
            ["editor.vsync", "on", "on", "on; off only for latency profiling"],
            ["editor.window_mode", "1 borderless", "1 borderless", "1 borderless; 2 for perf checks"],
        ]),
        ("h2", "Simulation"),
        ("h3", "fixed_update_hz"),
        ("p", "The fixed step is the physics and gameplay tick. 60 Hz is enough for puzzles, "
              "visual novels and slow platformers; 120 Hz pays off for precise action, ropes, "
              "projectiles and anything where 16 ms of input latency is felt. Fighting and rhythm "
              "games can justify 240 Hz, but then every fixed system must stay inside ~4 ms. The "
              "render rate is independent: never tie simulation to the monitor refresh."),
        ("h3", "max_fixed_steps"),
        ("p", "The catch-up cap for a slow frame. 4 is fine for light scenes; 8 is the usual "
              "commercial value. Raising it beyond 8 turns a single hitch into a spiral of death: "
              "the simulation runs more steps, the frame gets slower and the next frame needs even "
              "more steps. Lower it, do not raise it, if you see that pattern in a profile."),
        ("h2", "Audio"),
        ("h3", "masterVolume / sfxVolume / musicVolume"),
        ("p", "Keep the defaults at 1.0 and build headroom into the assets: mix music around -6 dBFS "
              "and leave peaks for explosions and UI. Boosting the master bus above 1.0 in the "
              "project to compensate for quiet assets clips the output and defeats the accessibility "
              "and deaf-mode paths. Use the buses for balance and a limiter on the master."),
        ("h2", "Accessibility"),
        ("h3", "flash_guard, flash_threshold, flash_gain"),
        ("p", "Keep the flash guard on for every shipping profile. A luminance delta threshold of "
              "0.20 and a gain of 0.35 stop most rapid flashes without flattening normal effects; "
              "strict safety settings are 0.15 and 0.25. Expose the toggle in the options menu but "
              "default it to on, and do not disable it in release builds for a 'punchier' look."),
        ("h3", "oled_pixel_shift / oled_interval_seconds"),
        ("p", "Leave the shift on for static HUDs, menus and long sessions. 60 seconds is a safe "
              "default; 30-60 seconds suits OLED monitors and handhelds. The cost is a hidden pixel "
              "offset, not a layout change, so it never affects gameplay logic."),
        ("h3", "blind_mode / deaf_mode"),
        ("p", "Default both to off and expose them as options. Blind mode rewires the UI to a "
              "screen-reader-friendly flow and deaf mode mutes the master bus at the graph root, so "
              "they must be tested with real content: narration text, subtitle timing and visual "
              "cues for audio events."),
        ("h2", "Textures and rendering"),
        ("h3", "texture_format"),
        ("p", "Compression choice follows the art, not the production scale: a jam game with a large "
              "painted background benefits from BC7, and a large pixel-art game should still use "
              "RGBA8. Scale only changes max_texture_size and whether VT is needed. BC7 is the best "
              "general-purpose desktop codec in the engine's closed format set; it is not a "
              "'medium' feature."),
        ("table", ["Format", "Bits per pixel", "Best for", "Avoid", "Availability"], [
            ["RGBA8", "32", "Pixel art, crisp UI, masks, small atlases", "Large painted 4K art (memory)",
             "Everywhere"],
            ["BC7 sRGB/UNORM", "8", "Painted 2D art, gradients, alpha cutouts", "1-pixel pixel art (edge bleed)",
             "Desktop (D3D11+/Vulkan)"],
            ["ASTC 4x4", "8", "Mobile/Apple painted art, same quality class as BC7", "Desktops without ASTC",
             "Mobile, Apple, many modern GPUs"],
            ["ASTC 6x6", "3.56", "Mobile size-first assets", "Fine detail, text, UI", "Mobile, Apple"],
        ]),
        ("p", "The format set is deliberately closed: RGBA8, BC7 and ASTC 4x4/6x6 only. BC3, BC4, "
              "BC5, BC6H and Basis/KTX2 are not cooked today; add them only with a concrete asset "
              "class that needs them (BC4/BC5 for single/two-channel masks, BC6H for HDR, Basis for "
              "download size with transcoding)."),
        ("p", "What actually looks better is often not the codec: mip generation, atlas padding "
              "(gutters) and sRGB handling remove bleeding and edge artifacts that no codec fixes. "
              "The practical next step for a mixed project is a per-asset format override, so an "
              "atlas can stay RGBA8 while a painted background uses BC7; today the project setting is "
              "the default for all cooked textures."),
        ("h3", "max_texture_size"),
        ("p", "Match the asset pipeline, not the monitor resolution. 1024 is plenty for a 1080p "
              "pixel game; 2048 covers most commercial 2D art at 1440p; 4096 is for 4K painted "
              "backgrounds and world atlases. Every doubling quadruples memory: a 4K RGBA8 page is "
              "64 MB, with mips about 85 MB, while BC7 is about 16 MB and 21 MB with mips."),
        ("h2", "Collision"),
        ("h3", "collision matrix"),
        ("p", "Start from the named layers and disable every pair the game does not use. Each "
              "enabled pair is broadphase and narrowphase work; in a bullets-and-pickups game the "
              "matrix can outweigh the level geometry. Light games need 3-4 layers, commercial "
              "titles up to 6, and large productions should keep the reserved layers but switch "
              "unused pairs off. Re-check the matrix after every new gameplay system."),
        ("h2", "Build and release"),
        ("h3", "optimization"),
        ("p", "O2 is the sane default for iteration and most shipping builds; O3 adds code size and "
              "compile time with diminishing returns. For large projects compare O2+LTO against O3 "
              "with a real profile: LTO usually wins more than the optimisation level. Never ship "
              "an O0 build."),
        ("h3", "link_time_optimization"),
        ("p", "Off while iterating: LTO makes incremental links slow. On for release builds. If link "
              "memory is a problem on CI, use thin LTO or keep unity builds on and split the largest "
              "translation units."),
        ("h3", "strip_debug_info"),
        ("p", "Strip for distribution, keep the symbols archive next to the release: crash reports "
              "without symbols cannot be triaged. The engine keeps the unstripped archive available "
              "for the same build; do not strip it."),
        ("h3", "unity_build"),
        ("p", "On by default: the engine's ~120 translation units build faster and link smaller. Turn "
              "it off only when a single huge unit exceeds memory or when a file needs isolated "
              "compile diagnostics; then expect slower iteration."),
        ("h3", "native_arch"),
        ("p", "Never enable it for a distributed build: binaries compiled for the host CPU can trap "
              "with an illegal instruction on older machines. It is a local benchmarking switch "
              "only."),
        ("h2", "Virtual texturing"),
        ("p", "VT is opt-in and only pays off when the world is larger than VRAM. Light games should "
              "leave it off. Medium games enable it for zoom-heavy tilemaps; large games depend on it. "
              "The layout is cooked into the chunks, so change it before the content is baked, not "
              "after."),
        ("table", MatrixHeadersEn(), [
            ["virtual_texture.enabled", "false", "true for zoom-heavy maps", "true"],
            ["layers", "-", "2", "4"],
            ["page_size / tile_size", "-", "1024 / 64", "2048 / 64"],
            ["chunk_tiles", "-", "2", "4"],
            ["page_table_size", "-", "128", "256"],
            ["world_tile_size", "-", "256", "256"],
            ["budget_loads_per_frame", "-", "6", "8"],
            ["prefetch_tiles", "-", "1", "2"],
        ]),
        ("h2", "Editor settings"),
        ("p", "Editor settings are fixed in a separate document: md/en/EditorSettingsReference_EN.md (Editor "
              "Settings Reference) lists every key with its type, default, clamp, the editor.ini mirror and "
              "the single settings file behind them. The short version: keep UI scale at 1.0-1.25, keep "
              "vsync on for stable authoring frame pacing (turn it off only for latency profiling), keep "
              "asset watching and auto-refresh on in a team so the browser reflects a colleague's import, "
              "and use borderless fullscreen (window_mode 1) for daily work; exclusive fullscreen (2) is "
              "for performance measurements."),
        ("h2", "Recommended presets"),
        ("h3", "Light"),
        ("code", """[simulation]
fixed_update_hz = 60.0
max_fixed_steps = 4

[render]
texture_format = "rgba8"
max_texture_size = 1024

[build]
optimization = "O2"
lto = false
strip_debug_info = true
unity_build = true
native_arch = false

[virtual_texture]
enabled = false"""),
        ("h3", "Medium"),
        ("code", """[simulation]
fixed_update_hz = 120.0
max_fixed_steps = 8

[render]
texture_format = "bc7-srgb"
max_texture_size = 2048

[build]
optimization = "O2"
lto = true
strip_debug_info = true
unity_build = true
native_arch = false

[virtual_texture]
enabled = true
layers = 2
page_size = 1024
tile_size = 64
chunk_tiles = 2
page_table_size = 128
world_tile_size = 256.0
budget_loads_per_frame = 6
prefetch_tiles = 1"""),
        ("h3", "Large"),
        ("code", """[simulation]
fixed_update_hz = 120.0
max_fixed_steps = 8

[render]
texture_format = "bc7-srgb"
max_texture_size = 4096

[build]
optimization = "O3"
lto = true
strip_debug_info = true
unity_build = true
native_arch = false

[virtual_texture]
enabled = true
layers = 4
page_size = 2048
tile_size = 64
chunk_tiles = 4
page_table_size = 256
world_tile_size = 256.0
budget_loads_per_frame = 8
prefetch_tiles = 2"""),
        ("h2", "Common mistakes"),
        ("bullets", [
            "Light: 4K textures, VT, O3+LTO, 240 Hz physics - cost without visible benefit.",
            "Light and medium: BC7 for pixel art - soft edges and palette shifts.",
            "Choosing the codec by project scale instead of art style - a painted jam game still wants BC7.",
            "Any scale: native_arch in a distribution build - illegal instructions on older CPUs.",
            "Large: one VT layer with a 2048 page - constant eviction and blurred pages.",
            "Large: a huge page table instead of shifting its origin - wasted memory.",
            "Shipping with streaming disabled or the flash guard off.",
        ]),
        ("h2", "Verification checklist"),
        ("bullets", [
            "The profile matches the target frame time at the shipping resolution.",
            "Fixed-step count stays below max_fixed_steps in the worst scene.",
            "Texture memory fits the budget with mips (measure, do not estimate).",
            "Collision pairs are all reachable by gameplay and none are unused.",
            "Release uses LTO, stripped binaries and keeps the symbols archive.",
            "Accessibility toggles work with content, not only with placeholders.",
            "VT (if enabled) shows no deferred backlog and no magenta tiles in steady state.",
        ]),
        ("h2", "Related documentation"),
        ("p", "See ROADMAP.md for the forward plan, the Application Guide for the "
              "editor-to-release workflow and the Complete Reference for the settings architecture."),
    ]


def ContentRu() -> list[tuple]:
    return [
        ("h1", "Рекомендации по настройкам проекта"),
        ("h1sub", f"FemboyEngine2D {DOC_VERSION} - по масштабу игры: лёгкая, средняя, крупная"),
        ("p", "Дескриптор проекта (project.femproj) смешивает три вида настроек: симуляцию "
              "геймплея, качество релиза и политику сборки. Одинаковые числа не подходят и "
              "двухнедельному джему, и потоковому открытому миру. Здесь на каждый масштаб дана "
              "своя колонка рекомендаций, объяснена цена каждого значения и перечислены ошибки, "
              "которые видны в профилях и баг-репортах. Считайте значения стартовыми и измеряйте "
              "перед релизом."),
        ("h2", "Три профиля масштаба"),
        ("table", ["Профиль", "Типичная игра", "Ассеты", "Цель"], [
            ["Лёгкая", "Джем, визуальная новелла, головоломка, небольшой пиксельный платформер", "< 200 МБ",
             "60 FPS в 1080p на встроенной графике, кадр CPU < 6 мс"],
            ["Средняя", "Коммерческая метроидвания, рогалик, экшен-платформер", "0.2-1 ГБ",
             "60 FPS в 1440p на средней GPU, кадр CPU < 8 мс"],
            ["Крупная", "Открытый мир, большие потоковые тайлмапы, сильный зум и панорамирование", "1-8 ГБ",
             "60+ FPS в 4K на дискретной GPU, кадр CPU < 12 мс, VT"],
        ]),
        ("h2", "Краткая матрица"),
        ("table", MatrixHeadersRu(), [
            ["fixed_update_hz", "60", "120", "120 (240 для файтингов/ритма)"],
            ["max_fixed_steps", "4", "8", "8"],
            ["master/sfx/music", "1.0 / 1.0 / 1.0", "1.0 / 1.0 / 1.0", "1.0 / 1.0 / 1.0, микс в ассетах"],
            ["blind_mode / deaf_mode", "false, в опциях", "false, в опциях", "false, в опциях"],
            ["flash_guard", "true", "true", "true"],
            ["flash_threshold / flash_gain", "0.20 / 0.35", "0.20 / 0.35", "0.15 / 0.25 для строгой безопасности"],
            ["oled_pixel_shift / interval", "true / 60 с", "true / 60 с", "true / 30-60 с"],
            ["texture_format (по арту)", "rgba8 пиксель-арт; bc7 рисованный", "bc7-srgb; rgba8 для пиксель-арта", "bc7-srgb десктоп; astc4x4-srgb мобильные"],
            ["max_texture_size", "1024", "2048", "4096"],
            ["build.optimization", "O2", "O2", "O3 (сначала измерьте O2+LTO)"],
            ["build.link_time_optimization", "false на итерациях", "true на релизе", "true на релизе"],
            ["build.strip_debug_info", "true на релизе", "true на релизе", "true на релизе"],
            ["build.unity_build", "true", "true", "true; выключать только при нехватке памяти сборки"],
            ["build.native_arch", "false", "false", "false"],
            ["слои коллизий", "3-4 используемых", "до 6 используемых", "именованный набор, лишние пары выключены"],
            ["virtual_texture.enabled", "false", "true для карт с зумом", "true"],
            ["VT page/tile/layers/table", "-", "1024/64/2/128", "2048/64/4/256"],
            ["VT budget / prefetch", "-", "6 / 1", "8 / 2"],
            ["editor.ui_scale", "1.0", "1.0-1.25", "1.0-1.25"],
            ["editor.vsync", "вкл", "вкл", "вкл; выключать только для замера задержки"],
            ["editor.window_mode", "1 borderless", "1 borderless", "1 borderless; 2 для замеров"],
        ]),
        ("h2", "Симуляция"),
        ("h3", "fixed_update_hz"),
        ("p", "Фиксированный шаг — это тик физики и геймплея. 60 Гц достаточно головоломкам, "
              "новеллам и медленным платформерам; 120 Гц окупается в точном экшене, верёвках, "
              "снарядах и везде, где заметны 16 мс задержки ввода. Файтинги и ритм-игры могут "
              "оправдать 240 Гц, но тогда каждая фиксированная система должна укладываться в ~4 мс. "
              "Частота рендера независима: не привязывайте симуляцию к герцовке монитора."),
        ("h3", "max_fixed_steps"),
        ("p", "Предел догоняющих шагов при медленном кадре. 4 хватает лёгким сценам, 8 — обычное "
              "коммерческое значение. Поднимать выше 8 опасно: один провал кадра превращается в "
              "спираль смерти, когда симуляция делает больше шагов, кадр становится медленнее, а "
              "следующему нужно ещё больше шагов. Если видите такой профиль — уменьшайте, не "
              "увеличивайте."),
        ("h2", "Аудио"),
        ("h3", "masterVolume / sfxVolume / musicVolume"),
        ("p", "Держите значения по умолчанию 1.0 и закладывайте запас в ассетах: музыку микшируйте "
              "около -6 dBFS и оставляйте пики для взрывов и UI. Поднимать master выше 1.0, чтобы "
              "компенсировать тихие ассеты, — это клиппинг на выходе и обход путей доступности и "
              "deaf mode. Балансируйте шинами и ставьте лимитер на master."),
        ("h2", "Доступность"),
        ("h3", "flash_guard, flash_threshold, flash_gain"),
        ("p", "Держите flash guard включённым во всех релизных профилях. Порог изменения яркости "
              "0.20 и усиление 0.35 гасят большинство быстрых вспышек, не убивая обычные эффекты; "
              "строгие настройки безопасности — 0.15 и 0.25. Выведите переключатель в опции, но по "
              "умолчанию держите включённым и не отключайте в релизе ради «сочности»."),
        ("h3", "oled_pixel_shift / oled_interval_seconds"),
        ("p", "Оставляйте сдвиг включённым для статичных HUD, меню и долгих сессий. 60 секунд — "
              "безопасный дефолт; 30-60 секунд подходят OLED-мониторам и портативкам. Цена — "
              "скрытый сдвиг пикселей, а не изменение раскладки, поэтому на геймплей он не влияет."),
        ("h3", "blind_mode / deaf_mode"),
        ("p", "По умолчанию оба выключены и вынесены в опции. Blind mode перестраивает UI под "
              "экранные дикторы, deaf mode глушит мастер-шину в корне графа, поэтому их надо "
              "тестировать на реальном контенте: текст повествования, тайминги субтитров и "
              "визуальные подсказки для звуковых событий."),
        ("h2", "Текстуры и рендер"),
        ("h3", "texture_format"),
        ("p", "Выбор сжатия зависит от арта, а не от масштаба продакшена: джем с большим рисованным "
              "фоном выигрывает от BC7, а крупная пиксель-арт игра всё равно должна брать RGBA8. "
              "Масштаб меняет только max_texture_size и необходимость VT. BC7 — лучший "
              "универсальный десктопный кодек в закрытом наборе форматов движка, а не «фича для "
              "средних проектов»."),
        ("table", ["Формат", "Бит на пиксель", "Лучше всего", "Избегать", "Доступность"], [
            ["RGBA8", "32", "Пиксель-арт, чёткий UI, маски, небольшие атласы",
             "Большой рисованный 4K-арт (память)", "Везде"],
            ["BC7 sRGB/UNORM", "8", "Рисованный 2D-арт, градиенты, альфа-вырезы",
             "Пиксель-арт в 1 пиксель (затекание краёв)", "Десктоп (D3D11+/Vulkan)"],
            ["ASTC 4x4", "8", "Мобильный/Apple рисованный арт, класс качества как у BC7",
             "Десктопы без ASTC", "Мобильные, Apple, многие современные GPU"],
            ["ASTC 6x6", "3.56", "Мобильные ассеты, где важен размер", "Мелкая детализация, текст, UI",
             "Мобильные, Apple"],
        ]),
        ("p", "Набор форматов намеренно закрыт: только RGBA8, BC7 и ASTC 4x4/6x6. BC3, BC4, BC5, "
              "BC6H и Basis/KTX2 сегодня не готовятся; добавляйте их только под конкретный класс "
              "ассетов (BC4/BC5 для одно- и двухканальных масок, BC6H для HDR, Basis — для размера "
              "загрузки с транскодированием)."),
        ("p", "На то, что видно на экране, часто влияет не кодек: генерация мипов, паддинг атласов "
              "(гаттеры) и sRGB убирают затекание и артефакты краёв, которые не исправит никакое "
              "сжатие. Практичный следующий шаг для смешанного проекта — переопределение формата на "
              "ассет: атлас остаётся RGBA8, а рисованный фон идёт в BC7; сегодня настройка проекта "
              "задаёт формат по умолчанию для всех текстур."),
        ("h3", "max_texture_size"),
        ("p", "Согласуйте с пайплайном ассетов, а не с разрешением монитора. 1024 достаточно "
              "пиксельной игре в 1080p; 2048 покрывает большинство коммерческого 2D-арта в 1440p; "
              "4096 — для 4K-фонов и мировых атласов. Каждое удвоение учетверяет память: страница "
              "4K RGBA8 — 64 МБ, с мипами около 85 МБ, а BC7 — около 16 МБ и 21 МБ с мипами."),
        ("h2", "Коллизии"),
        ("h3", "матрица коллизий"),
        ("p", "Начните с именованных слоёв и выключите все пары, которые игра не использует. "
              "Каждая включённая пара — это работа broadphase и narrowphase; в игре про пули и "
              "подборы матрица может стоить дороже геометрии уровня. Лёгким играм хватает 3-4 "
              "слоёв, коммерческим — до 6, крупным стоит сохранить резервные слои, но выключить "
              "неиспользуемые пары. Перепроверяйте матрицу после каждой новой геймплейной системы."),
        ("h2", "Сборка и релиз"),
        ("h3", "optimization"),
        ("p", "O2 — разумный дефолт для итераций и большинства релизов; O3 добавляет размер кода и "
              "время компиляции с убывающей отдачей. Для крупных проектов сравните O2+LTO и O3 на "
              "реальном профиле: LTO обычно даёт больше, чем уровень оптимизации. Никогда не "
              "выпускайте O0."),
        ("h3", "link_time_optimization"),
        ("p", "Выключено на итерациях: LTO замедляет инкрементальную линковку. Включено на релизе. "
              "Если на CI не хватает памяти под линковку, используйте thin LTO или оставьте unity "
              "build и разделите самые большие единицы трансляции."),
        ("h3", "strip_debug_info"),
        ("p", "Стрипьте для распространения, но держите архив символов рядом с релизом: баг-репорты "
              "без символов не разобрать. Движок сохраняет нестрипленный архив той же сборки; не "
              "удаляйте его."),
        ("h3", "unity_build"),
        ("p", "Включён по умолчанию: ~120 единиц трансляции движка собираются быстрее и линкуются "
              "компактнее. Выключайте только если одна огромная единица не влезает в память или "
              "нужна изолированная диагностика компиляции; тогда итерации замедлятся."),
        ("h3", "native_arch"),
        ("p", "Никогда не включайте для распространяемой сборки: бинарник под CPU хоста может "
              "упасть с illegal instruction на старых машинах. Это только локальный "
              "бенчмарк-переключатель."),
        ("h2", "Виртуальные текстуры"),
        ("p", "VT — opt-in и окупается только когда мир больше VRAM. Лёгким играм она не нужна. "
              "Средние включают её для карт с сильным зумом, крупные зависят от неё. Раскладка "
              "запекается в чанки, поэтому меняйте её до запекания контента, а не после."),
        ("table", MatrixHeadersRu(), [
            ["virtual_texture.enabled", "false", "true для карт с зумом", "true"],
            ["layers", "-", "2", "4"],
            ["page_size / tile_size", "-", "1024 / 64", "2048 / 64"],
            ["chunk_tiles", "-", "2", "4"],
            ["page_table_size", "-", "128", "256"],
            ["world_tile_size", "-", "256", "256"],
            ["budget_loads_per_frame", "-", "6", "8"],
            ["prefetch_tiles", "-", "1", "2"],
        ]),
        ("h2", "Настройки редактора"),
        ("p", "Настройки редактора зафиксированы в отдельном документе: md/ru/EditorSettingsReference_RU.md "
              "(Справочник настроек редактора) перечисляет каждый ключ с типом, значением по умолчанию, "
              "границами, зеркало editor.ini и единственный файл настроек. Коротко: UI-масштаб 1.0-1.25, "
              "vsync включён для ровного кадрового темпа авторинга (выключайте только для замера "
              "задержки), слежение за ассетами и автообновление браузера включены в команде, чтобы "
              "браузер отражал импорт коллеги; borderless fullscreen (window_mode 1) — для ежедневной "
              "работы, эксклюзивный полный экран (2) — для замеров производительности."),
        ("h2", "Рекомендуемые пресеты"),
        ("h3", "Лёгкая"),
        ("code", """[simulation]
fixed_update_hz = 60.0
max_fixed_steps = 4

[render]
texture_format = "rgba8"
max_texture_size = 1024

[build]
optimization = "O2"
lto = false
strip_debug_info = true
unity_build = true
native_arch = false

[virtual_texture]
enabled = false"""),
        ("h3", "Средняя"),
        ("code", """[simulation]
fixed_update_hz = 120.0
max_fixed_steps = 8

[render]
texture_format = "bc7-srgb"
max_texture_size = 2048

[build]
optimization = "O2"
lto = true
strip_debug_info = true
unity_build = true
native_arch = false

[virtual_texture]
enabled = true
layers = 2
page_size = 1024
tile_size = 64
chunk_tiles = 2
page_table_size = 128
world_tile_size = 256.0
budget_loads_per_frame = 6
prefetch_tiles = 1"""),
        ("h3", "Крупная"),
        ("code", """[simulation]
fixed_update_hz = 120.0
max_fixed_steps = 8

[render]
texture_format = "bc7-srgb"
max_texture_size = 4096

[build]
optimization = "O3"
lto = true
strip_debug_info = true
unity_build = true
native_arch = false

[virtual_texture]
enabled = true
layers = 4
page_size = 2048
tile_size = 64
chunk_tiles = 4
page_table_size = 256
world_tile_size = 256.0
budget_loads_per_frame = 8
prefetch_tiles = 2"""),
        ("h2", "Частые ошибки"),
        ("bullets", [
            "Лёгкая: текстуры 4K, VT, O3+LTO, физика 240 Гц — затраты без видимой пользы.",
            "Лёгкая и средняя: BC7 для пиксель-арта — мягкие края и сдвиг палитры.",
            "Выбор кодека по масштабу проекта, а не по стилю арта — рисованный джем всё равно хочет BC7.",
            "Любой масштаб: native_arch в распространяемой сборке — illegal instruction на старых CPU.",
            "Крупная: один слой VT со страницей 2048 — постоянное вытеснение и размытые страницы.",
            "Крупная: огромная таблица вместо сдвига origin — лишняя память.",
            "Релиз с выключенным стримингом или flash guard.",
        ]),
        ("h2", "Чек-лист проверки"),
        ("bullets", [
            "Профиль попадает в целевое время кадра на релизном разрешении.",
            "Число фиксированных шагов не превышает max_fixed_steps в худшей сцене.",
            "Память текстур укладывается в бюджет с мипами (измеряйте, не оценивайте).",
            "Все пары коллизий достижимы геймплеем, неиспользуемых нет.",
            "Релиз использует LTO, стрипнутые бинарники и сохраняет архив символов.",
            "Переключатели доступности проверены на контенте, а не на заглушках.",
            "VT (если включена) не показывает отложенных запросов и магенты в установившемся режиме.",
        ]),
        ("h2", "Связанная документация"),
        ("p", "См. ROADMAP.md с планом работ, Application Guide с процессом "
              "«редактор → релиз» и Complete Reference с архитектурой настроек."),
    ]


def Markdown(blocks: list[tuple]) -> str:
    out: list[str] = []
    for block in blocks:
        kind = block[0]
        if kind == "h1":
            out.append(f"# {block[1]}\n")
        elif kind == "h1sub":
            out.append(f"*{block[1]}*\n")
        elif kind == "h2":
            out.append(f"## {block[1]}\n")
        elif kind == "h3":
            out.append(f"### {block[1]}\n")
        elif kind == "p":
            out.append(f"{block[1]}\n")
        elif kind == "bullets":
            out.append("\n".join(f"- {item}" for item in block[1]) + "\n")
        elif kind == "table":
            headers, rows = block[1], block[2]
            out.append("| " + " | ".join(headers) + " |")
            out.append("|" + "|".join("---" for _ in headers) + "|")
            for row in rows:
                out.append("| " + " | ".join(row) + " |")
            out.append("")
        elif kind == "code":
            out.append("```toml\n" + block[1] + "\n```\n")
    return "\n".join(out)


def BuildDocx(blocks: list[tuple], path: pathlib.Path) -> None:
    document = Document()
    styles = document.styles
    styles["Normal"].font.name = "Calibri"
    styles["Normal"].font.size = Pt(10.5)
    for block in blocks:
        kind = block[0]
        if kind == "h1":
            document.add_heading(block[1], level=0)
        elif kind == "h1sub":
            paragraph = document.add_paragraph(block[1])
            paragraph.runs[0].italic = True
            paragraph.alignment = WD_ALIGN_PARAGRAPH.LEFT
        elif kind == "h2":
            document.add_heading(block[1], level=1)
        elif kind == "h3":
            document.add_heading(block[1], level=2)
        elif kind == "p":
            document.add_paragraph(block[1])
        elif kind == "bullets":
            for item in block[1]:
                document.add_paragraph(item, style="List Bullet")
        elif kind == "table":
            headers, rows = block[1], block[2]
            table = document.add_table(rows=1, cols=len(headers))
            table.style = "Table Grid"
            header_row = table.rows[0]
            header_row._tr.get_or_add_trPr().append(OxmlElement("w:tblHeader"))
            for index, header in enumerate(headers):
                cell = header_row.cells[index]
                cell.text = ""
                run = cell.paragraphs[0].add_run(header)
                run.bold = True
            for row in rows:
                cells = table.add_row().cells
                for index, value in enumerate(row):
                    cells[index].text = value
            for row in table.rows:
                for cell in row.cells:
                    for paragraph in cell.paragraphs:
                        for run in paragraph.runs:
                            run.font.size = Pt(9)
        elif kind == "code":
            paragraph = document.add_paragraph()
            run = paragraph.add_run(block[1])
            run.font.name = "Consolas"
            run.font.size = Pt(8.5)
    document.save(str(path))


def ExportPdf(docx_path: pathlib.Path, pdf_path: pathlib.Path) -> bool:
    if pdf_path.exists():
        pdf_path.unlink()
    result = subprocess.run(
        [str(NODE), str(LIBREOFFICE_CLI), "convert", "--input", str(docx_path), "--output", str(pdf_path)],
        capture_output=True,
        text=True,
        timeout=300,
    )
    if result.returncode != 0:
        print(f"PDF export failed for {docx_path.name}: {result.stderr.strip()}")
        return False
    if not pdf_path.is_file() or pdf_path.stat().st_size == 0:
        print(f"PDF export produced no file for {docx_path.name}")
        return False
    if pdf_path.read_bytes()[:5] != b"%PDF-":
        print(f"PDF export is not a PDF: {pdf_path.name}")
        return False
    return True


def main() -> int:
    DocPaths.EnsureLayout()
    documents = [
        ("en", "ProjectSettingsRecommendations_EN", ContentEn()),
        ("ru", "ProjectSettingsRecommendations_RU", ContentRu()),
    ]
    for language, name, blocks in documents:
        DocPaths.Md(language, f"{name}.md").write_text(Markdown(blocks), encoding="utf-8")
        docx_path = DocPaths.Docx(language, f"{name}.docx")
        BuildDocx(blocks, docx_path)
        pdf_path = DocPaths.Pdf(language, f"{name}.pdf")
        if not ExportPdf(docx_path, pdf_path):
            return 1
        docconvert.BuildDjvu(pdf_path, DocPaths.Djvu(language, f"{name}.djvu"))
        print(f"wrote {name}.md/.docx/.pdf ({language})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
