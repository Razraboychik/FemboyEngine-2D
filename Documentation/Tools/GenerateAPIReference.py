#!/usr/bin/env python3
"""Generate the FemboyEngine2D API Reference Manual.

The manual is produced by code, not by hand:

  * curated sections describe the contracts an integrator must understand;
  * an automatic pass over every public header builds the complete symbol index
    (classes, structs, enums, FORCEINLINE macros), so a new type cannot ship
    undocumented;
  * the same content is rendered twice, independently, into English and Russian
    Markdown, Word and PDF.

Outputs (all under Documentation/):
  FemboyEngineAPIReference_EN.md      English Markdown
  FemboyEngineAPIReference_RU.md      Russian Markdown
  FemboyEngineAPIReference.md         both languages, concatenated
  FemboyEngineAPIReference_EN.docx    Word, English
  FemboyEngineAPIReference_RU.docx    Word, Russian
  FemboyEngineAPIReference_EN.pdf     PDF, exported from the DOCX
  FemboyEngineAPIReference_RU.pdf     PDF, exported from the DOCX

Requirements: python-docx (pip install python-docx). PDF export uses LibreOffice
Kit through Node; pass --skip-pdf when neither is available.

Usage:
  python Documentation/Tools/GenerateAPIReference.py
  python Documentation/Tools/GenerateAPIReference.py --skip-pdf
  FBY_NODE=... FBY_LO_CLI=... python Documentation/Tools/GenerateAPIReference.py
"""

from __future__ import annotations

import argparse
import datetime as dt
import os
import pathlib
import re
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
import DocPaths as docpaths
import DocumentConvert as docconvert
from DocVersion import STATUS as DOC_STATUS
from DocVersion import VERSION as DOC_VERSION

try:
    from docx import Document
    from docx.enum.section import WD_SECTION  # noqa: F401  (kept for parity)
    from docx.enum.table import WD_TABLE_ALIGNMENT
    from docx.enum.text import WD_ALIGN_PARAGRAPH, WD_BREAK
    from docx.oxml import OxmlElement
    from docx.oxml.ns import qn
    from docx.shared import Cm, Pt, RGBColor

    HAVE_DOCX = True
except ImportError:  # pragma: no cover - exercised only on a bare agent
    HAVE_DOCX = False

ROOT = pathlib.Path(__file__).resolve().parents[2]
DOCUMENTATION = ROOT / "Documentation"
TODAY = dt.date.today().isoformat()

ACCENT = RGBColor(0xE8, 0x5D, 0x9E) if HAVE_DOCX else None
INK = RGBColor(0x1E, 0x1E, 0x24) if HAVE_DOCX else None
MUTED = RGBColor(0x6B, 0x6B, 0x76) if HAVE_DOCX else None
CODE_BG = "F4F4F7"
HEADER_BG = "2B2B33"

DEFAULT_NODE = os.environ.get("FBY_NODE", "node")
DEFAULT_LO_CLI = os.environ.get("FBY_LO_CLI", "")

LANGUAGES = {"en": "English", "ru": "Russian"}
TITLES = {
    "en": "FemboyEngine2D API Reference Manual",
    "ru": "FemboyEngine2D — Справочник API",
}
SUBTITLES = {
    "en": "C++26 data-oriented engine - complete public surface, contracts and a build-from-zero walkthrough",
    "ru": "Движок на C++26 в парадигме data-oriented: полная публичная поверхность, контракты и пошаговый пример",
}


def B(en: str, ru: str) -> dict[str, str]:
    return {"en": en, "ru": ru}


def t(block: dict[str, str], lang: str) -> str:
    return block[lang]


# ---------------------------------------------------------------------------
# Curated content
# ---------------------------------------------------------------------------

SECTIONS: list[dict] = [
    {
        "number": "1",
        "title": B("Scope, conformance and engine rules", "Область действия, соответствие стандарту и правила движка"),
        "blocks": [
            {
                "type": "p",
                "text": B(
                    "This manual specifies the public surface of FemboyEngine2D: every header under "
                    "Source/Engine and the editor infrastructure that a host links against. It is written "
                    "against the release-candidate tree, so a symbol described here exists in the headers that "
                    "ship with that tree, and the appendix index is generated from those headers on every run.",
                    "Этот справочник описывает публичную поверхность FemboyEngine2D: все заголовки из "
                    "Source/Engine и инфраструктуру редактора, с которой линкуется хост. Текст написан по дереву "
                    "Release, поэтому описанный символ существует в поставляемых заголовках, а индекс "
                    "в приложении генерируется из этих заголовков при каждом запуске.",
                ),
            },
            {
                "type": "p",
                "text": B(
                    "The engine targets C++26 with GCC 15 or newer and Clang 19 or newer; the Windows release "
                    "candidate is built with clang-cl against the MSVC standard library. Four rules are load "
                    "bearing and every public API is designed around them:",
                    "Движок рассчитан на C++26 с GCC 15 или новее и Clang 19 или новее; релиз-кандидат под Windows "
                    "собирается clang-cl со стандартной библиотекой MSVC. Четыре правила являются несущими, и "
                    "каждый публичный API спроектирован вокруг них:",
                ),
            },
            {
                "type": "bullets",
                "items": [
                    B(
                        "Simulation state is plain, trivially copyable data. No virtual calls, no owning pointers "
                        "and no std::string belong in a component, snapshot or network payload.",
                        "Состояние симуляции — это простые trivially copyable данные. Ни виртуальных вызовов, ни "
                        "владеющих указателей, ни std::string в компонентах, снапшотах и сетевых пакетах.",
                    ),
                    B(
                        "Hot paths do not allocate. Frame scratch memory comes from FrameArena, logs go through "
                        "fixed char buffers, and callbacks are function pointers plus void*, never std::function.",
                        "Горячие пути не выделяют память. Кадровая память берётся из FrameArena, логи пишутся в "
                        "фиксированные char-буферы, а колбэки — это указатель на функцию плюс void*, никогда "
                        "std::function.",
                    ),
                    B(
                        "Determinism is a property of the data, not of the scheduler. Every subsystem exposes a "
                        "checksummed image, and NetRollbackSession can only replay what it can reproduce byte for "
                        "byte.",
                        "Детерминизм — свойство данных, а не планировщика. Каждая подсистема отдаёт образ с "
                        "контрольной суммой, и NetRollbackSession может воспроизвести только то, что "
                        "воспроизводится байт в байт.",
                    ),
                    B(
                        "No RTTI in engine code: dynamic_cast and typeid are rejected by the style gate. Cast<> "
                        "walks stable type ids instead (see section 3).",
                        "Никакого RTTI в коде движка: dynamic_cast и typeid запрещены стилевым гейтом. Cast<> "
                        "вместо этого обходит стабильные идентификаторы типов (см. раздел 3).",
                    ),
                ],
            },
            {
                "type": "note",
                "text": B(
                    "The style gate (Source/Tools/Dev/CheckStyle.py) enforces the naming and structure rules: "
                    "PascalCase file stems, Allman braces, braced control bodies, In-prefixed parameters, "
                    "Scoped-prefixed locals, no trailing line comments and no Cyrillic in engine sources.",
                    "Стилевой гейт (Source/Tools/Dev/CheckStyle.py) проверяет правила именования и структуры: "
                    "PascalCase имена файлов, Allman-скобки, обязательные фигурные скобки у управляющих "
                    "конструкций, префикс In у параметров, префикс Scoped у локальных переменных, отсутствие "
                    "комментариев в конце строки и кириллицы в исходниках движка.",
                ),
            },
        ],
    },
    {
        "number": "2",
        "title": B("Building, targets and compiler flags", "Сборка, цели и флаги компилятора"),
        "blocks": [
            {
                "type": "p",
                "text": B(
                    "One CMake tree produces three kinds of artefact: the engine static library "
                    "(FemboyEngine2D::Engine), the editor library and application (FemboyEngine2D::Editor), and the "
                    "offline tools. A shipped game links the engine only; Dear ImGui, ImGuizmo, ImPlot and the "
                    "TOML writer never enter a shipping target.",
                    "Одно дерево CMake производит три вида артефактов: статическую библиотеку движка "
                    "(FemboyEngine2D::Engine), библиотеку и приложение редактора (FemboyEngine2D::Editor) и офлайн-"
                    "инструменты. Поставляемая игра линкует только движок; Dear ImGui, ImGuizmo, ImPlot и "
                    "TOML-писатель не попадают в поставочные цели.",
                ),
            },
            {
                "type": "table",
                "headers": [B("CMake option", "Опция CMake"), B("Default", "По умолчанию"), B("Effect", "Действие")],
                "rows": [
                    [
                        B("FBY_BUILD_EDITOR", "FBY_BUILD_EDITOR"),
                        B("ON", "ON"),
                        B("Builds fby_editor, the editor app and the panel plugins.",
                          "Собирает fby_editor, приложение редактора и плагины панелей."),
                    ],
                    [
                        B("FBY_BUILD_TESTS", "FBY_BUILD_TESTS"),
                        B("OFF", "OFF"),
                        B("Builds the ctest suite under Source/Tests.",
                          "Собирает набор ctest в Source/Tests."),
                    ],
                    [
                        B("FBY_BUILD_TOOLS", "FBY_BUILD_TOOLS"),
                        B("ON", "ON"),
                        B("FempakCooker, LocTool, SceneTool, ShaderCompiler, AotCompiler.",
                          "FempakCooker, LocTool, SceneTool, ShaderCompiler, AotCompiler."),
                    ],
                    [
                        B("FBY_BUILD_PLATFORM_PROVIDERS", "FBY_BUILD_PLATFORM_PROVIDERS"),
                        B("ON", "ON"),
                        B("Steam and Epic Games Store provider plugins.",
                          "Плагины провайдеров Steam и Epic Games Store."),
                    ],
                    [
                        B("FBY_RHI_BACKEND", "FBY_RHI_BACKEND"),
                        B("auto", "auto"),
                        B("auto, vulkan, metal or null. auto selects per platform.",
                          "auto, vulkan, metal или null. auto выбирается по платформе."),
                    ],
                ],
            },
            {
                "type": "code",
                "lang": "bash",
                "text": "cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \\\n"
                        "      -DFBY_BUILD_TESTS=ON -DFBY_BUILD_EDITOR=ON\n"
                        "cmake --build build --target fby_editor\n"
                        "ctest --test-dir build --output-on-failure\n"
                        "python Source/Tools/Dev/CheckStyle.py",
            },
            {
                "type": "p",
                "text": B(
                    "The compiler wrapper (cmake/FbyCompiler.cmake) owns the C++26 flag, the warning set and the "
                    "sanitizer switches. Do not add flags per target: a target that needs a different dialect is "
                    "a target that should not exist.",
                    "Обёртка компилятора (cmake/FbyCompiler.cmake) задаёт флаг C++26, набор предупреждений и "
                    "переключатели санитайзеров. Не добавляйте флаги отдельным целям: цель, которой нужен другой "
                    "диалект, не должна существовать.",
                ),
            },
        ],
    },
    {
        "number": "3",
        "title": B("Core value types, FORCEINLINE and the cast family", "Базовые типы, FORCEINLINE и семейство приведений"),
        "blocks": [
            {
                "type": "p",
                "text": B(
                    "Definitions.hpp is the contract header of the engine. It supplies the export macros, the "
                    "assertion macros (FBY_ASSERT, FBY_VERIFY, FBY_UNREACHABLE), the stable TypeId machinery, the "
                    "cast family and the component registration macros. Every other header includes it, directly "
                    "or transitively.",
                    "Definitions.hpp — контрактный заголовок движка. Он содержит макросы экспорта, макросы "
                    "утверждений (FBY_ASSERT, FBY_VERIFY, FBY_UNREACHABLE), механизм стабильных TypeId, семейство "
                    "приведений и макросы регистрации компонентов. Остальные заголовки включают его прямо или "
                    "транзитивно.",
                ),
            },
            {
                "type": "code",
                "lang": "cpp",
                "text": "#if defined(_MSC_VER)\n"
                        "#define FORCEINLINE __forceinline\n"
                        "#elif defined(__GNUC__) || defined(__clang__)\n"
                        "#define FORCEINLINE inline __attribute__((always_inline))\n"
                        "#else\n"
                        "#define FORCEINLINE inline\n"
                        "#endif",
            },
            {
                "type": "p",
                "text": B(
                    "FORCEINLINE is the only inlining spelling in public headers. It is used for accessors, "
                    "hot arithmetic and template casts; it never replaces a measured optimisation. The three "
                    "families are:",
                    "FORCEINLINE — единственное написание инлайна в публичных заголовках. Он применяется к "
                    "аксессорам, горячей арифметике и шаблонным приведениям и никогда не заменяет измеренную "
                    "оптимизацию. Существуют три семейства:",
                ),
            },
            {
                "type": "table",
                "headers": [B("Symbol", "Символ"), B("Contract", "Контракт")],
                "rows": [
                    [
                        B("FemCast<To>(value)", "FemCast<To>(value)"),
                        B(
                            "Unchecked static_cast with reference collapsing. Use when the concrete type is "
                            "proven by construction (factory output, storage tag). No runtime cost.",
                            "Непроверяемый static_cast со сворачиванием ссылок. Используется, когда конкретный тип "
                            "доказан конструкцией (выход фабрики, тег хранилища). Нулевая цена в рантайме.",
                        ),
                    ],
                    [
                        B("Cast<To>(object)", "Cast<To>(object)"),
                        B(
                            "Checked cast. Upcast is a compile-time static_cast; downcast compares the object's "
                            "GetTypeId() against StableTypeId<To>() and its registered parent chain; unrelated "
                            "types fail to compile. Returns nullptr on a mismatch, never throws, never uses RTTI.",
                            "Проверяемое приведение. Апкаст — compile-time static_cast; даункаст сравнивает "
                            "GetTypeId() объекта с StableTypeId<To>() и цепочкой родителей; несвязанные типы не "
                            "компилируются. При несовпадении возвращается nullptr, исключений и RTTI нет.",
                        ),
                    ],
                    [
                        B("CastComponent<To>(registry, entity)", "CastComponent<To>(registry, entity)"),
                        B(
                            "ECS spelling of Cast<>: EnTT proves the component type at compile time and the "
                            "registry validates the entity. Returns nullptr when the entity lacks the component.",
                            "ECS-написание Cast<>: EnTT проверяет тип компонента на этапе компиляции, а реестр "
                            "проверяет сущность. Возвращает nullptr, если компонента у сущности нет.",
                        ),
                    ],
                ],
            },
            {
                "type": "code",
                "lang": "cpp",
                "text": "// Downcast through the stable type table, not through RTTI.\n"
                        "Fby::TypeObject* base = factory.Create(\"Sprite\");\n"
                        "if (Fby::Sprite* sprite = Fby::Cast<Fby::Sprite>(base))\n"
                        "{\n"
                        "    sprite->Animate(dt);\n"
                        "}\n"
                        "\n"
                        "// Registration binds Sprite to TypeObject and derives its stable id.\n"
                        "FBY_REGISTER_TYPE(Fby::Sprite, Fby::TypeObject);",
            },
            {
                "type": "p",
                "text": B(
                    "StableTypeId<T>() is a consteval FNV-1a of the type's registered name (or the compiler "
                    "signature when no name is pinned). A type that changes its registered name changes its id, "
                    "which is why component names are part of the serialized schema and why Snapshot v4 stores a "
                    "LayoutHash next to the name.",
                    "StableTypeId<T>() — consteval FNV-1a от зарегистрированного имени типа (или сигнатуры "
                    "компилятора, если имя не закреплено). Смена зарегистрированного имени меняет id, поэтому имена "
                    "компонентов входят в сериализованную схему, а Snapshot v4 хранит LayoutHash рядом с именем.",
                ),
            },
        ],
    },
    {
        "number": "4",
        "title": B("FrameArena: the only frame allocator", "FrameArena: единственный кадровый аллокатор"),
        "blocks": [
            {
                "type": "p",
                "text": B(
                    "FrameArena is a bump pointer over caller-owned storage. It is reset at the start of a frame "
                    "and never frees individual allocations. Systems that need transient arrays take it from "
                    "SystemContext::frameArena; a system that calls operator new in its frame path is a defect, "
                    "not a style preference.",
                    "FrameArena — это bump-указатель по памяти, которой владеет вызывающий код. Он сбрасывается в "
                    "начале кадра и никогда не освобождает отдельные выделения. Системы, которым нужны временные "
                    "массивы, берут его из SystemContext::frameArena; система, вызывающая operator new в кадровом "
                    "пути, — дефект, а не стилистическое предпочтение.",
                ),
            },
            {
                "type": "code",
                "lang": "cpp",
                "text": "Fby::FrameArena arena(storage);          // caller owns storage\n"
                        "arena.Reset();                          // once per frame\n"
                        "auto* items = arena.AllocateArray<MeshRenderItem>(count);\n"
                        "if (items == nullptr)\n"
                        "{\n"
                        "    // Arena exhausted: degrade, never allocate on the heap.\n"
                        "    return;\n"
                        "}\n"
                        "const std::size_t used = arena.Used();\n"
                        "const std::size_t capacity = arena.Capacity();",
            },
            {
                "type": "bullets",
                "items": [
                    B(
                        "Allocate() returns nullptr instead of throwing when the arena is exhausted; callers must "
                        "handle the failure.",
                        "Allocate() возвращает nullptr вместо исключения при исчерпании арены; вызывающий код "
                        "обязан обработать отказ.",
                    ),
                    B(
                        "AllocateArray<T>() static_asserts std::is_trivially_copyable_v<T>: the arena stores POD "
                        "payloads only.",
                        "AllocateArray<T>() проверяет std::is_trivially_copyable_v<T>: арена хранит только POD.",
                    ),
                    B(
                        "Over-aligned types must pass their alignment explicitly; the default is "
                        "alignof(std::max_align_t).",
                        "Для типов с повышенным выравниванием нужно явно передать выравнивание; по умолчанию "
                        "используется alignof(std::max_align_t).",
                    ),
                ],
            },
        ],
    },
    {
        "number": "5",
        "title": B("JobSystem and per-thread diagnostics", "JobSystem и потоковая диагностика"),
        "blocks": [
            {
                "type": "p",
                "text": B(
                    "JobSystem owns a pool of std::jthread workers. Tasks are function pointers plus a context "
                    "pointer plus a half-open range, so submitting a task never allocates. ParallelFor splits a "
                    "range into chunks, lets every participant pull the next chunk from an atomic counter, and "
                    "always runs at least one chunk on the caller, which keeps single-core machines correct and "
                    "worker re-entry safe.",
                    "JobSystem владеет пулом std::jthread. Задача — это указатель на функцию, указатель на "
                    "контекст и полуоткрытый диапазон, поэтому отправка задачи не выделяет память. ParallelFor "
                    "делит диапазон на куски, позволяет каждому участнику брать следующий кусок из атомарного "
                    "счётчика и всегда выполняет хотя бы один кусок на вызывающем потоке — так однопроцессорные "
                    "машины остаются корректными, а повторный вход из воркера безопасным.",
                ),
            },
            {
                "type": "code",
                "lang": "cpp",
                "text": "jobs.ParallelFor(entityCount, 4096, [&](std::uint32_t begin, std::uint32_t end)\n"
                        "{\n"
                        "    for (std::uint32_t i = begin; i < end; ++i)\n"
                        "    {\n"
                        "        Integrate(entities[i], dt);\n"
                        "    }\n"
                        "});\n"
                        "jobs.WaitIdle();\n"
                        "\n"
                        "// Diagnostics: one row per thread, slot 0 is the main thread.\n"
                        "std::array<Fby::JobSystem::ThreadLoad, Fby::JobSystem::kMaxThreadSlots> loads{};\n"
                        "const std::uint32_t slots = jobs.SampleThreadLoad(loads);",
            },
            {
                "type": "p",
                "text": B(
                    "ThreadLoad carries busyNanoseconds (wall time inside engine work), taskCount and a 16-byte "
                    "name. The counters are one cache line per thread, so two workers finishing at the same time "
                    "do not contend. SceneDiagnosticsPanel turns two samples into a per-thread occupancy "
                    "percentage.",
                    "ThreadLoad содержит busyNanoseconds (время внутри работы движка), taskCount и 16-байтное "
                    "имя. Счётчики выровнены по кэш-линии на поток, поэтому два одновременно завершившихся "
                    "воркера не конфликтуют. SceneDiagnosticsPanel превращает два замера в процент занятости "
                    "каждого потока.",
                ),
            },
            {
                "type": "note",
                "text": B(
                    "ThreadSlotCount() is ThreadCount() + 1 and is clamped by the fixed MCounters array; a device "
                    "with more than 64 workers reports the first 64 slots.",
                    "ThreadSlotCount() равен ThreadCount() + 1 и ограничен фиксированным массивом MCounters; "
                    "устройство с более чем 64 воркерами сообщает первые 64 слота.",
                ),
            },
        ],
    },
    {
        "number": "6",
        "title": B("ECS, scheduling and snapshots", "ECS, планирование и снапшоты"),
        "blocks": [
            {
                "type": "p",
                "text": B(
                    "The registry is EnTT. Systems are registered on a SystemGraph in one of five phases "
                    "(PreUpdate, FixedUpdate, Update, PostUpdate, Render). SystemContext is the only parameter a "
                    "system receives; it carries the registry, JobSystem, frame arena, renderer, window, VFS, "
                    "AssetDatabase, audio, physics, joints, cloth, fluid, camera stack, accessibility kernel and "
                    "font atlases.",
                    "Реестр — это EnTT. Системы регистрируются в SystemGraph в одной из пяти фаз (PreUpdate, "
                    "FixedUpdate, Update, PostUpdate, Render). SystemContext — единственный параметр системы; он "
                    "содержит реестр, JobSystem, кадровую арену, рендерер, окно, VFS, AssetDatabase, аудио, "
                    "физику, суставы, ткань, жидкость, стек камер, ядро доступности и атласы шрифтов.",
                ),
            },
            {
                "type": "code",
                "lang": "cpp",
                "text": "Fby::Ecs::Schedule schedule;\n"
                        "schedule.Add(Fby::Ecs::Phase::FixedUpdate, \"physics\", &PhysicsStepSystem, &physics);\n"
                        "schedule.Add(Fby::Ecs::Phase::Update, \"movement\", &MovementSystem, nullptr);\n"
                        "schedule.Add(Fby::Ecs::Phase::PostUpdate, \"platform\", &GogAchievementSystem, &gog);\n"
                        "\n"
                        "Fby::Ecs::SystemContext context{registry, jobs};\n"
                        "context.physics = &physics;\n"
                        "context.window = &window;\n"
                        "schedule.Run(Fby::Ecs::Phase::FixedUpdate, context);",
            },
            {
                "type": "p",
                "text": B(
                    "Snapshot v4 is a canonical, checksummed byte image of the registry. Components are written by "
                    "hand-written codecs when they contain runtime handles (b2BodyId, b2ShapeId); the snapshot "
                    "never serializes those handles, and the restore path recreates bodies from the component "
                    "fields. The schema stores the Mirror name and LayoutHash of every component, so a field "
                    "change is rejected instead of silently misread.",
                    "Snapshot v4 — канонический образ реестра с контрольной суммой. Компоненты с рантайм-хэндлами "
                    "(b2BodyId, b2ShapeId) пишутся рукописными кодеками; снапшот никогда не сериализует эти "
                    "хэндлы, а восстановление пересоздаёт тела из полей компонента. Схема хранит Mirror-имя и "
                    "LayoutHash каждого компонента, поэтому изменение поля отклоняется, а не читается молча "
                    "неверно.",
                ),
            },
            {
                "type": "table",
                "headers": [B("API", "API"), B("Contract", "Контракт")],
                "rows": [
                    [
                        B("GetOrAdd<T>(registry, entity)", "GetOrAdd<T>(registry, entity)"),
                        B("Adds the component when missing and returns a stable reference.",
                          "Добавляет компонент при отсутствии и возвращает стабильную ссылку."),
                    ],
                    [
                        B("ecs::SnapshotWriter / SnapshotReader", "ecs::SnapshotWriter / SnapshotReader"),
                        B("Writes and validates the canonical image; checksum is FNV-1a over the payload.",
                          "Пишет и валидирует канонический образ; контрольная сумма — FNV-1a по payload."),
                    ],
                    [
                        B("Fby::Net::NetRollbackSession", "Fby::Net::NetRollbackSession"),
                        B("Consumes snapshot v4 and replays ticks on late input (section 10).",
                          "Использует snapshot v4 и воспроизводит тики при опоздании ввода (раздел 10)."),
                    ],
                ],
            },
        ],
    },
    {
        "number": "7",
        "title": B("RHI, render items and frame statistics", "RHI, элементы отрисовки и статистика кадра"),
        "blocks": [
            {
                "type": "p",
                "text": B(
                    "The RHI is a hand-written Vulkan/Metal layer. Frames are recorded into persistent ring "
                    "buffers; sprite items are 48-byte PODs; meshes are drawn through one indirect call per "
                    "material/layer; linear-blend skinning is one compute dispatch. FrameStats exposes what the "
                    "pass actually submitted, which is what SceneDiagnosticsPanel plots.",
                    "RHI — рукописный слой Vulkan/Metal. Кадры записываются в постоянные кольцевые буферы; "
                    "спрайты — это 48-байтные POD; меши рисуются одним indirect-вызовом на материал/слой; "
                    "linear-blend skinning — один compute-диспатч. FrameStats показывает, что реально отправил "
                    "проход, и именно это рисует SceneDiagnosticsPanel.",
                ),
            },
            {
                "type": "table",
                "headers": [B("FrameStats field", "Поле FrameStats"), B("Meaning", "Значение")],
                "rows": [
                    [B("items / batches / drawCalls", "items / batches / drawCalls"),
                     B("Sprite items, batches and submitted draws.", "Спрайт-элементы, батчи и отправленные вызовы.")],
                    [B("meshItems / meshIndirectCalls", "meshItems / meshIndirectCalls"),
                     B("MeshRenderItem count and indirect draws.",
                       "Количество MeshRenderItem и indirect-вызовов.")],
                    [B("skinDispatches / skinRanges", "skinDispatches / skinRanges"),
                     B("Skinning compute dispatches and ranges.",
                       "Диспатчи и диапазоны compute-скиннинга.")],
                    [B("uploadedBytes", "uploadedBytes"),
                     B("Bytes uploaded this frame.", "Байты, загруженные в этом кадре.")],
                ],
            },
            {
                "type": "note",
                "text": B(
                    "The editor's Viewport renders into an offscreen target, not into the swapchain. "
                    "EnsureViewportTarget() sizes that target in physical pixels; a target sized in points is the "
                    "usual cause of a blurry Viewport on a HiDPI monitor.",
                    "Viewport редактора рендерит в offscreen-цель, а не в swapchain. "
                    "EnsureViewportTarget() задаёт размер цели в физических пикселях; цель, заданная в точках, — "
                    "обычная причина размытого Viewport на HiDPI-мониторе.",
                ),
            },
        ],
    },
    {
        "number": "8",
        "title": B("Materials: cooked graphs, the pipeline cache and the shipped path",
                   "Материалы: кук графов, кэш пайплайнов и продакшн-путь"),
        "blocks": [
            {
                "type": "p",
                "text": B(
                    "Material authoring and material compilation live on opposite sides of the engine boundary. "
                    "`Render::MaterialGraph` is the authoring DAG: fixed capacities (128 nodes, 384 pins, 64 "
                    "parameters), trivially copyable, and hashed without the editor positions. "
                    "`Render::MaterialGraphCompiler` turns a validated graph into GLSL 450 text. `fby_matc` runs "
                    "that compiler and glslang at build time and writes a `.femmat` payload, so a shipped game "
                    "never links glslang and never parses GLSL. The full rationale is in [ADR 0002](../../adr/0002-materials-in-engine.md).",
                    "Авторинг материала и его компиляция находятся по разные стороны границы движка. "
                    "`Render::MaterialGraph` — авторский DAG с фиксированными ёмкостями (128 узлов, 384 пина, "
                    "64 параметра), trivially copyable, хешируется без позиций в редакторе. "
                    "`Render::MaterialGraphCompiler` превращает прошедший валидацию граф в текст GLSL 450. "
                    "`fby_matc` запускает этот компилятор и glslang на этапе сборки и пишет payload `.femmat`, "
                    "поэтому поставленная игра не линкует glslang и не разбирает GLSL. Полное обоснование — в [ADR 0002](../../adr/0002-materials-in-engine.md).",
                ),
            },
            {
                "type": "table",
                "headers": [B("Piece", "Компонент"), B("Header", "Заголовок"), B("Contract", "Контракт")],
                "rows": [
                    [
                        B("`Render::MaterialGraph`", "`Render::MaterialGraph`"),
                        B("`Engine/RHI/MaterialGraph.hpp`", "`Engine/RHI/MaterialGraph.hpp`"),
                        B("Authoring DAG; `Hash()` is the pipeline identity and excludes UI positions",
                          "Авторский DAG; `Hash()` — идентификатор пайплайна, позиции в UI не входят"),
                    ],
                    [
                        B("`MaterialGraphCompiler`", "`MaterialGraphCompiler`"),
                        B("`Engine/RHI/MaterialGraphCompiler.hpp`", "`Engine/RHI/MaterialGraphCompiler.hpp`"),
                        B("DAG to GLSL 450 and the shader contract flags",
                          "DAG в GLSL 450 и флаги контракта шейдера"),
                    ],
                    [
                        B("`EncodeMaterialGraph` / `DecodeMaterialGraph`", "`EncodeMaterialGraph` / `DecodeMaterialGraph`"),
                        B("`Engine/RHI/MaterialAsset.hpp`", "`Engine/RHI/MaterialAsset.hpp`"),
                        B("`.fgraph` wire format: explicit little-endian fields, no struct memcpy",
                          "Wire-формат `.fgraph`: явные little-endian поля, без memcpy структуры"),
                    ],
                    [
                        B("`EncodeCookedMaterial` / `DecodeCookedMaterial`", "`EncodeCookedMaterial` / `DecodeCookedMaterial`"),
                        B("`Engine/RHI/MaterialAsset.hpp`", "`Engine/RHI/MaterialAsset.hpp`"),
                        B("`.femmat` payload: 32-byte header, vertex and fragment SPIR-V, zero-copy decoding",
                          "Payload `.femmat`: 32-байтный заголовок, SPIR-V вершинного и фрагментного этапов, "
                          "декодирование без копирования"),
                    ],
                    [
                        B("`Render::MaterialPipelineCache`", "`Render::MaterialPipelineCache`"),
                        B("`Engine/RHI/MaterialPipelineCache.hpp`", "`Engine/RHI/MaterialPipelineCache.hpp`"),
                        B("128 pipelines keyed by graph hash, reference counted, LRU eviction, no heap use",
                          "128 пайплайнов по хешу графа, с подсчётом ссылок, LRU-эвикцией и без обращений к куче"),
                    ],
                    [
                        B("`Rhi::RHI::SupportsRuntimeShaderPipelines`", "`Rhi::RHI::SupportsRuntimeShaderPipelines`"),
                        B("`Engine/RHI/RHI.hpp`", "`Engine/RHI/RHI.hpp`"),
                        B("Capability query: Vulkan returns true, Null and Metal return false",
                          "Запрос возможности: Vulkan возвращает true, Null и Metal — false"),
                    ],
                ],
            },
            {
                "type": "code",
                "lang": "bash",
                "text": "# Author the graph once, cook it in the build step\n"
                        "build/bin/fby_matc assets/materials/tinted.fgraph -o assets/materials/tinted.femmat\n",
            },
            {
                "type": "code",
                "lang": "cpp",
                "text": "// `InBytes` is the .femmat payload from the VFS or a package entry.\n"
                        "Render::CookedMaterialView ScopedMaterial;\n"
                        "if (!Render::DecodeCookedMaterial(InBytes, &ScopedMaterial))\n"
                        "{\n"
                        "    return false;\n"
                        "}\n"
                        "\n"
                        "// One cache per device: acquire at load time, release at shutdown.\n"
                        "const Rhi::ShaderPipelineHandle ScopedPipeline = InMaterials.Acquire(InRhi, ScopedMaterial);\n"
                        "\n"
                        "Rhi::MeshRenderItem ScopedItem;\n"
                        "ScopedItem.materialPipeline =\n"
                        "    ScopedPipeline.IsValid() ? ScopedPipeline.id : Rhi::kDefaultMaterialPipeline;\n",
            },
            {
                "type": "bullets",
                "items": [
                    B("The graph hash is the pipeline identity. Editing a graph produces a new hash, so the old "
                      "pipeline becomes an unreferenced cache entry instead of being mutated in place.",
                      "Хеш графа — идентификатор пайплайна. Правка графа даёт новый хеш, поэтому старый пайплайн "
                      "становится записью кэша без ссылок, а не меняется на месте."),
                    B("The contract mask travels with the payload: generated vertex stage, fragment view projection, "
                      "material push constants, world and vertex position varyings. A live entry whose hash arrives "
                      "with another contract is refused, because that is a different shader.",
                      "Маска контракта едет вместе с payload: сгенерированный вершинный этап, view projection во "
                      "фрагменте, push-константы материала, varying world/vertex position. Живая запись, чей хеш "
                      "приходит с другим контрактом, отклоняется: это другой шейдер."),
                    B("`Acquire()` adds a reference and `Release()` drops one. A pipeline at zero references stays "
                      "resident, so a material acquired once per frame never recreates a pipeline; it is destroyed "
                      "by LRU eviction, `ReleaseAll()` or `Shutdown()`.",
                      "`Acquire()` добавляет ссылку, `Release()` снимает. Пайплайн с нулём ссылок остаётся "
                      "резидентным, поэтому материал, берущийся раз в кадр, не пересоздаётся; он уничтожается "
                      "только при LRU-эвикции, `ReleaseAll()` или `Shutdown()`."),
                    B("The cache never allocates and never formats text on the frame path; `LastError()` returns "
                      "the fixed error buffer.",
                      "Кэш не аллоцирует и не форматирует текст на кадровом пути; `LastError()` возвращает "
                      "фиксированный буфер ошибки."),
                    B("Backends without runtime shader pipelines keep the engine shaders. Query "
                      "`SupportsRuntimeShaderPipelines()` instead of probing for an invalid handle.",
                      "Бэкенды без рантайм-пайплайнов остаются на шейдерах движка. Спрашивайте "
                      "`SupportsRuntimeShaderPipelines()`, а не определяйте это по невалидному хендлу."),
                ],
            },
            {
                "type": "table",
                "headers": [B("Payload", "Payload"), B("Fields", "Поля"), B("Validated on load", "Проверяется при загрузке")],
                "rows": [
                    [
                        B("`.fgraph`", "`.fgraph`"),
                        B("magic `FBYG`, version, node/pin/param counts, nodes, pins, params",
                          "magic `FBYG`, версия, счётчики узлов/пинов/параметров, узлы, пины, параметры"),
                        B("magic, version, counts against capacity, exact size, node kinds, pin and param indices",
                          "magic, версия, счётчики против ёмкости, точный размер, виды узлов, индексы пинов и параметров"),
                    ],
                    [
                        B("`.femmat`", "`.femmat`"),
                        B("magic `FBYM`, version, contract mask, graph hash, vertex and fragment byte lengths",
                          "magic `FBYM`, версия, маска контракта, хеш графа, длины вершинного и фрагментного этапов"),
                        B("magic, version, size against the lengths, both SPIR-V modules, contract consistency",
                          "magic, версия, размер против длин, оба SPIR-V модуля, согласованность контракта"),
                    ],
                ],
            },
            {
                "type": "p",
                "text": B(
                    "The editor keeps live editing on top of the same cache: `Fby::Editor::MaterialGraphRuntime` "
                    "recompiles a graph when its hash changes, reports per-tab errors and delegates pipeline "
                    "ownership to `MaterialPipelineCache`. `MaterialGraphRuntime::SetDomain` switches the live "
                    "compile between the mesh and the sprite contract, and the panel's `SaveGraph`/`LoadGraph` "
                    "write and read the `.fgraph` payload the cooker consumes. A shipped build skips glslang entirely and only calls "
                    "`DecodeCookedMaterial` and `Acquire`.",
                    "Редактор сохраняет живое редактирование поверх того же кэша: "
                    "`Fby::Editor::MaterialGraphRuntime` перекомпилирует граф при смене хеша, отдаёт ошибки по "
                    "вкладкам и передаёт владение пайплайном в `MaterialPipelineCache`. Поставленная сборка "
                    "обходится без glslang и вызывает только `DecodeCookedMaterial` и `Acquire`.",
                ),
            },
            {
                "type": "p",
                "text": B(
                    "Sprites take materials through the third artefact: `MaterialCompileOptions::domain` selects "
                    "the sprite domain, where the generated fragment stage matches the `quad.vert` varyings "
                    "(`vUv`, `vColor`, `vLayer`, and `vWorld` when a node needs it) and samples the item's atlas slice "
                    "through `vLayer`. `Time` is available: the sprite push block carries a material clock at "
                    "offset 80, so the generated stage reads `pc.materialParams.x` exactly like a mesh shader. `fby_matc --sprite` cooks that variant. "
                    "`DrawOrder::MaterialBucket` places the sprite in a bucket, "
                    "`ChangeMaskRenderExtract::SetMaterialPipelines` maps buckets to cached pipeline ids, and the "
                    "extractor emits one `DrawBatch` per bucket with `DrawBatch::pipeline` already resolved. The "
                    "64-bit render key carries the bucket (16 layer, 8 material, 32 depth, 8 sequence), so a "
                    "material batch is never split by depth. Nodes that need the mesh view-projection "
                    "(`Fresnel`, `VertexPosition`) are refused in the sprite domain with a readable reason.",
                    "Спрайты получают материалы через третий артефакт: `MaterialCompileOptions::domain` выбирает "
                    "спрайтовый домен, где сгенерированный фрагментный этап совпадает с varying-ами `quad.vert` "
                    "(`vUv`, `vColor`, `vLayer` и `vWorld`, если узел его требует), сэмплирует слой атласа "
                    "элемента через `vLayer`. `Time` доступен: спрайтовый push-блок несёт часы материала по "
                    "смещению 80, поэтому сгенерированный этап читает `pc.materialParams.x` так же, как "
                    "меш-шейдер. `fby_matc --sprite` кукает такой "
                    "вариант. `DrawOrder::MaterialBucket` помещает спрайт в бакет, "
                    "`ChangeMaskRenderExtract::SetMaterialPipelines` сопоставляет бакеты с id кэшированных "
                    "пайплайнов, а экстракт выдаёт один `DrawBatch` на бакет с уже разрешённым "
                    "`DrawBatch::pipeline`. 64-битный ключ несёт бакет (16 слой, 8 материал, 32 глубина, "
                    "8 последовательность), поэтому материальный батч не разрывается глубиной. Узлы, которым "
                    "нужна меш-проекция (`Fresnel`, `VertexPosition`), отклоняются в спрайтовом домене с "
                    "понятной причиной.",
                ),
            },
        ],
    },
    {
        "number": "9",
        "title": B("Physics: Box2D v3, joints, cloth and SdfCollisionSystem", "Физика: Box2D v3, суставы, ткань и SdfCollisionSystem"),
        "blocks": [
            {
                "type": "p",
                "text": B(
                    "PhysicsSystem owns one b2WorldId and exposes it to extension modules as a packed uint32, so "
                    "box2d.h never leaks into a public header. Bodies are mirrored from RigidBody2D/Collider2D "
                    "components into Box2D, stepped at the fixed rate, and read back before interpolation. "
                    "PhysicsJointSystem adds rope and revolute-link chains; ClothSystem integrates a Verlet grid "
                    "and projects it against the same Box2D colliders.",
                    "PhysicsSystem владеет одним b2WorldId и отдаёт его расширениям как упакованный uint32, "
                    "поэтому box2d.h не попадает в публичные заголовки. Тела зеркалируются из компонентов "
                    "RigidBody2D/Collider2D в Box2D, шагают с фиксированной частотой и читаются обратно перед "
                    "интерполяцией. PhysicsJointSystem добавляет верёвки и цепочки revolute-звеньев; ClothSystem "
                    "интегрирует Verlet-сетку и проецирует её на те же коллайдеры Box2D.",
                ),
            },
            {
                "type": "p",
                "text": B("SdfCollisionSystem contract:", "Контракт SdfCollisionSystem:"),
            },
            {
                "type": "table",
                "headers": [B("Member", "Член"), B("Contract", "Контракт")],
                "rows": [
                    [B("SetField(const SdfField&)", "SetField(const SdfField&)"),
                     B(
                         "Installs a non-owning mdspan over a row-major grid. Negative values are solid; the "
                         "field's memory is owned by the renderer/terrain, not by the system.",
                         "Принимает невладеющий mdspan на row-major сетку. Отрицательные значения — твёрдое тело; "
                         "память поля принадлежит рендереру/рельефу, а не системе.",
                     )],
                    [B("Step(registry, physics, fixedDt)", "Step(registry, physics, fixedDt)"),
                     B(
                         "Samples the grid at body corners and centre, converts the gradient to a normal, applies "
                         "a mass-scaled push impulse, reflects inward velocity and writes the corrected state "
                         "back. Returns the number of corrected bodies.",
                         "Сэмплирует сетку в углах и центре тела, превращает градиент в нормаль, применяет "
                         "импульс выталкивания с учётом массы, отражает внутрь направленную скорость и пишет "
                         "исправленное состояние обратно. Возвращает число исправленных тел.",
                     )],
                    [B("Update(SystemContext&)", "Update(SystemContext&)"),
                     B("ECS convenience wrapper around Step(); a headless call without physics is a no-op.",
                       "ECS-обёртка вокруг Step(); вызов без физики ничего не делает.")],
                    [B("Contacts()", "Contacts()"),
                     B("Fixed array of SdfContact {entity, point, normal, penetration, impulse, sequence}.",
                       "Фиксированный массив SdfContact {entity, point, normal, penetration, impulse, sequence}.")],
                    [B("SdfCollisionDesc", "SdfCollisionDesc"),
                     B("pushOut, restitution, maxImpulse, subSamples, dynamicOnly.",
                       "pushOut, restitution, maxImpulse, subSamples, dynamicOnly.")],
                ],
            },
            {
                "type": "code",
                "lang": "cpp",
                "text": "Fby::SdfCollisionDesc desc;\n"
                        "desc.pushOut = 0.65f;\n"
                        "desc.restitution = 0.05f;\n"
                        "desc.subSamples = 2;\n"
                        "sdf.Initialize(desc);\n"
                        "sdf.SetField(terrain.Field());\n"
                        "sdf.Register(graph);                    // Phase::FixedUpdate\n"
                        "\n"
                        "// Viewport overlay: red vectors whose length is the penetration.\n"
                        "debug.Build(context, Fby::Editor::PhysicsDebugSdf, &sdf);",
            },
            {
                "type": "note",
                "text": B(
                    "SdfCollisionSystem is not part of SystemContext. A host that wants SDF physics registers the "
                    "system explicitly through Register(graph) and keeps the instance alive; the Viewport debug "
                    "pass takes the same pointer.",
                    "SdfCollisionSystem не входит в SystemContext. Хост, которому нужна SDF-физика, регистрирует "
                    "систему явно через Register(graph) и хранит экземпляр живым; отладочный проход Viewport "
                    "принимает тот же указатель.",
                ),
            },
        ],
    },
    {
        "number": "10",
        "title": B("NetRollbackSession: deterministic rollback", "NetRollbackSession: детерминированный роллбэк"),
        "blocks": [
            {
                "type": "p",
                "text": B(
                    "A session binds snapshot v4, CommandLog, Pcg32 and GameplayState into one fixed-capacity "
                    "rollback loop. Everything after Initialize() is allocation-free: one snapshot ring, one "
                    "input ring per player, a fixed entity map and fixed frame metadata. The simulation callback "
                    "is a function pointer plus void*.",
                    "Сессия связывает snapshot v4, CommandLog, Pcg32 и GameplayState в один контур роллбэка с "
                    "фиксированной ёмкостью. После Initialize() аллокаций нет: одно кольцо снапшотов, одно кольцо "
                    "ввода на игрока, фиксированная карта сущностей и фиксированные метаданные кадров. Колбэк "
                    "симуляции — указатель на функцию плюс void*.",
                ),
            },
            {
                "type": "table",
                "headers": [B("Method", "Метод"), B("Contract", "Контракт")],
                "rows": [
                    [B("Initialize(registry, schema, config, arena)", "Initialize(registry, schema, config, arena)"),
                     B("Allocation-free setup with a caller-owned snapshot arena.",
                       "Настройка без аллокаций с ареной снапшотов, принадлежащей вызывающему.")],
                    [B("Initialize(registry, schema, config)", "Initialize(registry, schema, config)"),
                     B("Convenience overload; allocates one arena (the only allocation the session makes).",
                       "Удобная перегрузка; выделяет одну арену (единственная аллокация сессии).")],
                    [B("SetSimulation(fn, user) / SetCommandLog(log)", "SetSimulation(fn, user) / SetCommandLog(log)"),
                     B("Binds the pure simulation function and the structural command log.",
                       "Привязывает чистую функцию симуляции и лог структурных команд.")],
                    [B("Start(rng)", "Start(rng)"),
                     B("Captures the base tick and stores the session RNG. Required before Step().",
                       "Фиксирует базовый тик и сохраняет RNG сессии. Обязателен до Step().")],
                    [B("Step(localInput)", "Step(localInput)"),
                     B("Simulates exactly one tick with prediction for missing remote inputs and captures the "
                       "snapshot.",
                       "Симулирует ровно один тик с предсказанием отсутствующего удалённого ввода и фиксирует "
                       "снапшот.")],
                    [B("SubmitRemoteInput(player, frame)", "SubmitRemoteInput(player, frame)"),
                     B("Stores a late or current input; a frame for an already simulated tick marks a pending "
                       "rollback.",
                       "Сохраняет опоздавший или текущий ввод; кадр для уже симулированного тика помечает "
                       "ожидающий роллбэк.")],
                    [B("ReplayPending(report)", "ReplayPending(report)"),
                     B("Restores the snapshot before the earliest corrected tick, restores RNG and CommandLog "
                       "cursor, re-simulates, captures new snapshots and reports changed ticks.",
                       "Восстанавливает снапшот перед самым ранним исправленным тиком, восстанавливает RNG и "
                       "курсор CommandLog, пересимулирует, фиксирует новые снапшоты и сообщает изменённые тики.")],
                    [B("CompareChecksums(tick, snap, state, out)", "CompareChecksums(tick, snap, state, out)"),
                     B("Compares a remote report with the stored frame; the two checksums isolate a byte-level "
                       "desync from a gameplay-state desync.",
                       "Сравнивает удалённый отчёт с сохранённым кадром; две контрольные суммы отделяют "
                       "побайтовый desync от desync игрового состояния.")],
                ],
            },
            {
                "type": "code",
                "lang": "cpp",
                "text": "Fby::Net::NetRollbackSession::Config cfg;\n"
                        "cfg.playerCount = 2;\n"
                        "cfg.localPlayer = 0;\n"
                        "cfg.rollbackTicks = 64;\n"
                        "session.Initialize(registry, schema, cfg, arena);\n"
                        "session.SetSimulation(&SimulateTick, &game);\n"
                        "session.Start(pcg);\n"
                        "\n"
                        "while (running)\n"
                        "{\n"
                        "    session.SubmitRemoteInput(remote, received);\n"
                        "    session.Step(localInput);\n"
                        "    if (session.RollbackPending())\n"
                        "    {\n"
                        "        Fby::Net::RollbackReport report;\n"
                        "        session.ReplayPending(&report);\n"
                        "        if (report.changedTicks > 0)\n"
                        "        {\n"
                        "            LogBoy().Log(Fby::Editor::LogBoySeverity::Desync, \"net\",\n"
                        "                         \"rollback {}..{} changed {}\", report.fromTick, report.toTick,\n"
                        "                         report.changedTicks);\n"
                        "        }\n"
                        "    }\n"
                        "}",
            },
            {
                "type": "bullets",
                "items": [
                    B(
                        "NetInputFrame is 24 bytes of integer axes (int16 fixed point). Floats are not allowed: a "
                        "float bit pattern is not guaranteed to survive a different compiler.",
                        "NetInputFrame — 24 байта целочисленных осей (int16 с фиксированной точкой). Float "
                        "запрещён: битовый шаблон float не гарантированно переживает другой компилятор.",
                    ),
                    B(
                        "Result enumerates every failure mode (NotInitialized, BufferFull, RollbackTooOld, "
                        "ChecksumMismatch, ...); ResultName() is the log spelling.",
                        "Result перечисляет все режимы отказа (NotInitialized, BufferFull, RollbackTooOld, "
                        "ChecksumMismatch, ...); ResultName() — написание для логов.",
                    ),
                    B(
                        "A session is non-copyable and owns its arena when the convenience overload is used.",
                        "Сессия некопируемая и владеет ареной, если использована удобная перегрузка.",
                    ),
                ],
            },
        ],
    },
    {
        "number": "11",
        "title": B("Audio: mixer, DSP graph, limiter", "Аудио: микшер, DSP-граф, лимитер"),
        "blocks": [
            {
                "type": "p",
                "text": B(
                    "AudioSystem owns the miniaudio device and a fixed mixer. DspGraph routes nodes into buses, "
                    "supports ducking and reverb, and receives commands through a lock-free ring so the game "
                    "thread never blocks the audio callback. Limiter.hpp is the final true-peak stage.",
                    "AudioSystem владеет устройством miniaudio и фиксированным микшером. DspGraph маршрутизирует "
                    "узлы в шины, поддерживает дакинг и реверберацию и принимает команды через lock-free кольцо, "
                    "поэтому игровой поток не блокирует аудиоколбэк. Limiter.hpp — финальная стадия true-peak.",
                ),
            },
            {
                "type": "bullets",
                "items": [
                    B(
                        "DeafMode mutes the graph at the Master root and turns sound triggers into subtitle lines; "
                        "it is a policy of the audio system, not a post-effect.",
                        "DeafMode глушит граф на корне Master и превращает звуковые триггеры в строки субтитров; "
                        "это политика аудиосистемы, а не пост-эффект.",
                    ),
                    B(
                        "Ogg/Opus is decoded off the audio thread; the mixer only sees cooked .fpcm buffers.",
                        "Ogg/Opus декодируется вне аудиопотока; микшер видит только приготовленные .fpcm буферы.",
                    ),
                ],
            },
        ],
    },
    {
        "number": "12",
        "title": B("PinkUI and WidgetCanvasSystem", "PinkUI и WidgetCanvasSystem"),
        "blocks": [
            {
                "type": "p",
                "text": B(
                    "PinkUI is the in-game UI: a fixed-capacity canvas, mdspan grid layout, anchor resolution, "
                    "hit testing and one-batch packing. WidgetCanvasSystem turns a WidgetCanvas into FrameOutput "
                    "(draw items) without allocating; font glyphs come from a .fbyfont atlas mapped through the "
                    "VFS, so the shipping runtime carries no TTF rasterizer. Glyphs are atlas-only: there is no "
                    "embedded 8x8 bitmap fallback, and a text frame without a mounted font array is refused "
                    "instead of drawn with the wrong layer.",
                    "PinkUI — игровой интерфейс: канвас фиксированной ёмкости, mdspan-раскладка сетки, разрешение "
                    "якорей, hit-testing и упаковка в один батч. WidgetCanvasSystem превращает WidgetCanvas в "
                    "FrameOutput (элементы отрисовки) без аллокаций; глифы берутся из атласа .fbyfont, "
                    "сматпленного через VFS, поэтому поставочный рантайм не содержит TTF-растеризатора. Глифы "
                    "существуют только в атласе: встроенного 8x8-фолбэка нет, а текстовый кадр без "
                    "смонтированного массива шрифтов отклоняется, а не рисуется неверным слоем.",
                ),
            },
            {
                "type": "table",
                "headers": [B("Member", "Член"), B("Contract", "Контракт")],
                "rows": [
                    [B("WidgetCanvasSystem::MountFonts(RHI&)", "WidgetCanvasSystem::MountFonts(RHI&)"),
                     B("Mandatory cold-start stage on the main thread. Builds the GPU texture array from the "
                       "VFS .fbyfont set; a failure is a PANIC log plus FBY_ASSERT_MSG, never a fallback.",
                       "Обязательный этап холодного старта в главном потоке. Собирает GPU-массив из набора "
                       ".fbyfont в VFS; ошибка — это PANIC в лог плюс FBY_ASSERT_MSG, а не фолбэк.")],
                    [B("BuildFrame(context, canvas, output)", "BuildFrame(context, canvas, output)"),
                     B("Lays out and culls one canvas, returning BuildStatus and filling FrameOutput. A text "
                       "canvas without a current mounted atlas returns FontsNotMounted and writes nothing. "
                       "Allocation-free; safe to call from Phase::Render.",
                       "Раскладывает и отсекает один канвас, возвращая BuildStatus и заполняя FrameOutput. "
                       "Текстовый канвас без актуального смонтированного атласа возвращает FontsNotMounted и "
                       "ничего не пишет. Без аллокаций; безопасно вызывать из Phase::Render.")],
                    [B("Update(context, canvases, output)", "Update(context, canvases, output)"),
                     B("Builds every active canvas into one output; the host submits the result as one batch. "
                       "A font-contract violation stops the pass and sets fontContractViolated.",
                       "Собирает все активные канвасы в один вывод; хост отправляет результат одним батчем. "
                       "Нарушение шрифтового контракта останавливает проход и ставит fontContractViolated.")],
                    [B("WidgetCanvas::SetDisplayScale", "WidgetCanvas::SetDisplayScale"),
                     B("Density from CameraStackSystem::DisplayScale; font sizes are rounded to whole physical "
                       "pixels.",
                       "Плотность из CameraStackSystem::DisplayScale; размеры шрифтов округляются до целых "
                       "физических пикселей.")],
                    [B("OledShiftState", "OledShiftState"),
                     B("Integer 2 px orbit for OLED safety; ApplyOledShift() binds it to a canvas pool.",
                       "Целочисленный 2-пиксельный сдвиг для защиты OLED; ApplyOledShift() привязывает его к пулу "
                       "канвасов.")],
                ],
            },
            {
                "type": "p",
                "text": B(
                    "WidgetCanvasSystem is also the accessibility surface: BlindMode reads widget labels for the "
                    "narrative log, DeafMode receives subtitle widgets, and colour-vision matrices plus FlashGuard "
                    "are applied to the canvas colours before packing.",
                    "WidgetCanvasSystem — ещё и поверхность доступности: BlindMode читает подписи виджетов для "
                    "нарративного лога, DeafMode получает виджеты субтитров, а матрицы цветокоррекции и FlashGuard "
                    "применяются к цветам канваса перед упаковкой.",
                ),
            },
        ],
    },
    {
        "number": "13",
        "title": B("Accessibility kernel (BlindMode, DeafMode, FlashGuard)", "Ядро доступности (BlindMode, DeafMode, FlashGuard)"),
        "blocks": [
            {
                "type": "p",
                "text": B(
                    "Accessibility is a frame policy, not an overlay. BlindMode skips the game pass, indirect draws "
                    "and the skinning dispatch, then publishes a bounded narrative log. DeafMode mutes the Master "
                    "bus and routes sound triggers into SubtitleLines. FlashGuard compresses sudden luminance "
                    "changes. Colour-vision correction uses the published Machado 2009 matrices, and "
                    "SrgbRelativeLuminance()/ContrastRatio() implement WCAG 2.2 for authoring-time validation.",
                    "Доступность — это политика кадра, а не оверлей. BlindMode пропускает игровой проход, "
                    "indirect-вызовы и диспатч скиннинга и публикует ограниченный нарративный лог. DeafMode глушит "
                    "шину Master и превращает звуковые триггеры в SubtitleLines. FlashGuard сжимает резкие "
                    "изменения яркости. Цветокоррекция использует опубликованные матрицы Machado 2009, а "
                    "SrgbRelativeLuminance()/ContrastRatio() реализуют WCAG 2.2 для проверки при авторинге.",
                ),
            },
            {
                "type": "table",
                "headers": [B("Item", "Элемент"), B("Contract", "Контракт")],
                "rows": [
                    [B("NarrativeEvent", "NarrativeEvent"),
                     B("128-slot SPSC ring of 160-byte text payloads; the simulation thread pushes, the speech "
                       "thread pops.",
                       "SPSC-кольцо на 128 слотов с текстовым payload 160 байт; поток симуляции пишет, поток речи "
                       "читает.")],
                    [B("ColorVisionMatrix(deficiency)", "ColorVisionMatrix(deficiency)"),
                     B("Returns the 3x3 matrix for protanopia, deuteranopia or tritanopia.",
                       "Возвращает матрицу 3x3 для протанопии, дейтеранопии или тританопии.")],
                    [B("ApplyColorVision(color, matrix, strength)", "ApplyColorVision(color, matrix, strength)"),
                     B("Applies the matrix in linear light to an sRGB Color32.",
                       "Применяет матрицу в линейном свете к sRGB Color32.")],
                    [B("FlashGuard::Evaluate(luminance, dt)", "FlashGuard::Evaluate(luminance, dt)"),
                     B("Returns the gain for the current frame; a large delta is compressed to the threshold.",
                       "Возвращает усиление для текущего кадра; большое изменение сжимается до порога.")],
                    [B("EditorSettings::ide*", "EditorSettings::ide*"),
                     B("The same policies applied to the editor window itself: Machado correction, WCAG "
                       "high-contrast, FlashGuard, keyboard navigation and LogBoy as the narration sink.",
                       "Те же политики применяются к самому окну редактора: коррекция Machado, высокий контраст "
                       "WCAG, FlashGuard, навигация с клавиатуры и LogBoy как приёмник нарратива.")],
                ],
            },
        ],
    },
    {
        "number": "14",
        "title": B("Asset pipeline, VFS and localization", "Конвейер ассетов, VFS и локализация"),
        "blocks": [
            {
                "type": "p",
                "text": B(
                    "Assets are addressed by relative path inside the VFS. A .fempak archive is mounted with a "
                    "priority; a directory mount can shadow it, which is how the editor writes a loose file before "
                    "the cooker folds it back in. Read() returns a span into an mmap that stays valid until the "
                    "mount is dropped or the file is hot-reloaded.",
                    "Ассеты адресуются относительным путём внутри VFS. Архив .fempak монтируется с приоритетом; "
                    "каталог-монитор может его перекрывать — так редактор пишет loose-файл до того, как кукер "
                    "вернёт его в архив. Read() возвращает span на mmap, который валиден до размонтирования или "
                    "горячей перезагрузки файла.",
                ),
            },
            {
                "type": "table",
                "headers": [B("Format", "Формат"), B("Owner", "Владелец"), B("Purpose", "Назначение")],
                "rows": [
                    [B(".fempak", ".fempak"), B("FempakCooker", "FempakCooker"),
                     B("Immutable shipped archive with an index and optional encryption.",
                       "Неизменяемый поставочный архив с индексом и опциональным шифрованием.")],
                    [B(".fbyfont", ".fbyfont"), B("FontAtlas (PinkUI)", "FontAtlas (PinkUI)"),
                     B("Baked glyph atlas: [header][glyph records][pixels]; no runtime TTF rasterizer.",
                       "Испечённый атлас глифов: [header][glyph records][pixels]; без TTF-растеризатора в рантайме.")],
                    [B(".femeta", ".femeta"), B("AssetDatabase", "AssetDatabase"),
                     B("Typed metadata, import settings and stable AssetId.",
                       "Типизированные метаданные, настройки импорта и стабильный AssetId.")],
                    [B(".fbin", ".fbin"), B("LocTool", "LocTool"),
                     B("Compiled localization table; the editor UI itself is English-only.",
                       "Скомпилированная таблица локализации; сам интерфейс редактора — только английский.")],
                ],
            },
            {
                "type": "note",
                "text": B(
                    "The editor UI is not localizable by design: menus, panel titles, errors and the inspector are "
                    "English-only. Localization applies to game content and documentation, where a .fbin table and "
                    "the EN/RU manual cover the shipped languages.",
                    "Интерфейс редактора намеренно не локализуется: меню, заголовки панелей, ошибки и инспектор — "
                    "только на английском. Локализация относится к игровому контенту и документации, где таблица "
                    ".fbin и руководство EN/RU покрывают поставляемые языки.",
                ),
            },
        ],
    },
    {
        "number": "15",
        "title": B("Platform services: Steam, Epic Games Store, GOG", "Платформенные сервисы: Steam, Epic Games Store, GOG"),
        "blocks": [
            {
                "type": "p",
                "text": B(
                    "PlatformService is the engine-side bridge; PlatformAPI.h is a flat pure-C ABI that a store "
                    "SDK plugin implements. The provider is loaded dynamically, resolves only documented entry "
                    "points and converts every store-specific type at the boundary, so a game never includes "
                    "steam_api.h, EOSSDK or Galaxy headers.",
                    "PlatformService — мост на стороне движка; PlatformAPI.h — плоский чистый C ABI, который "
                    "реализует плагин SDK магазина. Провайдер загружается динамически, резолвит только "
                    "документированные точки входа и преобразует все типы магазина на границе, поэтому игра "
                    "никогда не включает steam_api.h, EOSSDK или заголовки Galaxy.",
                ),
            },
            {
                "type": "table",
                "headers": [B("Provider", "Провайдер"), B("Status in this tree", "Статус в этом дереве"), B("Notes", "Примечания")],
                "rows": [
                    [B("SteamProvider.cpp", "SteamProvider.cpp"), B("Implemented", "Реализован"),
                     B("Steamworks flat API through a function table.",
                       "Плоский API Steamworks через таблицу функций.")],
                    [B("EgsProvider.cpp", "EgsProvider.cpp"), B("Implemented", "Реализован"),
                     B("Epic Online Services provider through the same C ABI.",
                       "Провайдер Epic Online Services через тот же C ABI.")],
                    [B("GogProvider.cpp", "GogProvider.cpp"), B("Implemented (shim)", "Реализован (шим)"),
                     B("Loads galaxy64.dll / libgalaxy64.so / libgalaxy64.dylib at runtime and resolves the flat "
                       "fby_gog_* shim ABI; the game ships the shim built against its SDK revision. Galaxy is a "
                       "C++ interface library, so a shim is mandatory. GogAchievementSystem and GogStorageSystem "
                       "write the binary PlatformCommandLog in Phase::PostUpdate.",
                       "Загружает galaxy64.dll / libgalaxy64.so / libgalaxy64.dylib во время работы и резолвит "
                       "плоский ABI шима fby_gog_*. Шим поставляет игра, собранная под свою версию SDK. Galaxy — "
                       "библиотека C++-интерфейсов, поэтому шим обязателен. GogAchievementSystem и GogStorageSystem "
                       "пишут бинарный PlatformCommandLog в Phase::PostUpdate.")],
                    [B("fby_fontc", "fby_fontc"), B("Implemented", "Реализован"),
                     B("Offline TTF -> .fbyfont cooker (stb_truetype, RGBA8 atlas, verified header).",
                       "Офлайн-кукер TTF -> .fbyfont (stb_truetype, RGBA8-атлас, проверяемый заголовок).")],
                ],
            },
            {
                "type": "code",
                "lang": "cpp",
                "text": "// Pure C ABI: the provider is a shared library, not a C++ dependency.\n"
                        "typedef struct FbyPlatformApi {\n"
                        "    uint32_t version;\n"
                        "    void* user;\n"
                        "    bool (*init)(void* user);\n"
                        "    void (*shutdown)(void* user);\n"
                        "    void (*unlockAchievement)(void* user, const char* id);\n"
                        "    bool (*writeCloudFile)(void* user, const char* name, const uint8_t* data, uint32_t size);\n"
                        "} FbyPlatformApi;",
            },
        ],
    },
    {
        "number": "16",
        "title": B("Editor: dashboard, fonts, LogBoy, Scene Diagnostics", "Редактор: дашборд, шрифты, LogBoy, Scene Diagnostics"),
        "blocks": [
            {
                "type": "p",
                "text": B(
                    "The dashboard is the only editor surface that exists before a project is open. It installs "
                    "the editor font set before the first frame, so the first window is already Inter at the "
                    "physical DPI. A new project starts in Projects/ under the engine root, resolved by "
                    "DefaultProjectsRoot() - never in the process working directory and never in the install "
                    "directory.",
                    "Дашборд — единственная поверхность редактора, существующая до открытия проекта. Он "
                    "устанавливает набор шрифтов редактора до первого кадра, поэтому первое окно уже отрисовано "
                    "Inter с учётом физического DPI. Новый проект создаётся в Projects/ в корне движка, путь "
                    "определяет DefaultProjectsRoot() — никогда рабочий каталог процесса и никогда каталог "
                    "установки.",
                ),
            },
            {
                "type": "table",
                "headers": [B("Component", "Компонент"), B("Contract", "Контракт")],
                "rows": [
                    [B("EditorFonts", "EditorFonts"),
                     B("Loads Inter and JetBrains Mono from the VFS mmap. Subpixel advances (PixelSnapH=false), "
                       "auto oversampling, no bitmap fallback, dynamic atlas baked at the viewport pixel density. "
                       "Legacy backends get RasterizerDensity + inverse FontGlobalScale.",
                       "Загружает Inter и JetBrains Mono из mmap VFS. Субпиксельные продвижения "
                       "(PixelSnapH=false), авто-оверсэмплинг, без bitmap-fallback, динамический атлас печётся под "
                       "плотность пикселей вьюпорта. Устаревшие бэкенды получают RasterizerDensity + обратный "
                       "FontGlobalScale.")],
                    [B("ProjectDashboard", "ProjectDashboard"),
                     B("Initialize(paths, config, visible, window, vfs, fonts, uiScale): installs typography, "
                       "resolves the default Projects/ location and creates it.",
                       "Initialize(paths, config, visible, window, vfs, fonts, uiScale): устанавливает типографику, "
                       "определяет расположение Projects/ по умолчанию и создаёт его.")],
                    [B("LogBoySystem / LogBoyPanel", "LogBoySystem / LogBoyPanel"),
                     B("Fixed 4096-record ring of 192-byte text payloads with severity Trace/Info/Warning/"
                       "Critical/Desync, FNV-1a system hashes and colour+hash filtering.",
                       "Фиксированное кольцо на 4096 записей с текстом 192 байта, уровнями Trace/Info/Warning/"
                       "Critical/Desync, FNV-1a хэшами систем и фильтрацией по цвету и хэшу.")],
                    [B("SceneDiagnosticsPanel", "SceneDiagnosticsPanel"),
                     B("Per-thread occupancy bars and heatmap, FrameArena bytes, MeshRenderItem count, indirect "
                       "draws, draw calls and exact simulation microseconds with avg/p99/peak.",
                       "Столбцы и тепловая карта занятости потоков, байты FrameArena, количество MeshRenderItem, "
                       "indirect-вызовы, draw calls и точное время симуляции в микросекундах с avg/p99/peak.")],
                    [B("PhysicsDebugVisualizer", "PhysicsDebugVisualizer"),
                     B("Fixed-buffer line/point geometry for Box2D bodies, ropes, cloth and SDF normals, drawn "
                       "over the Viewport through a caller-supplied world-to-screen map.",
                       "Геометрия линий и точек в фиксированных буферах для тел Box2D, верёвок, ткани и нормалей "
                       "SDF, рисуется поверх Viewport через карту мир→экран, заданную вызывающим.")],
                ],
            },
            {
                "type": "note",
                "text": B(
                    "The .fbyfont atlas is a baked PinkUI asset and cannot feed ImGui's vector rasterizer. The "
                    "editor therefore loads the Inter TTF from the same VFS mount; the atlas header is still "
                    "validated on startup, so a corrupt game-UI atlas is reported by the editor, not by the player.",
                    "Атлас .fbyfont — испечённый ассет PinkUI и не может питать векторный растеризатор ImGui. "
                    "Поэтому редактор загружает TTF Inter из того же монтирования VFS; заголовок атласа всё равно "
                    "валидируется при старте, чтобы испорченный атлас игрового UI обнаруживал редактор, а не игрок.",
                ),
            },
        ],
    },
    {
        "number": "17",
        "title": B("Step-by-step: a game from an empty folder", "Пошагово: игра с пустой папки"),
        "blocks": [
            {
                "type": "p",
                "text": B(
                    "This walkthrough produces a playable project with a sprite, physics, keyboard input, a PinkUI "
                    "HUD and a deterministic rollback session. Every step maps to a real header from the index.",
                    "Этот пример создаёт играбельный проект со спрайтом, физикой, вводом с клавиатуры, HUD на "
                    "PinkUI и детерминированной rollback-сессией. Каждый шаг соответствует реальному заголовку из "
                    "индекса.",
                ),
            },
            {
                "type": "bullets",
                "items": [
                    B("1. Launch the editor and create a project with New Project. The dashboard creates "
                      "<engineRoot>/Projects/<name> with assets/{textures,audio,scenes}, configs/ and "
                      "project.femproj.",
                      "1. Запустите редактор и создайте проект через New Project. Дашборд создаёт "
                      "<engineRoot>/Projects/<name> с assets/{textures,audio,scenes}, configs/ и "
                      "project.femproj."),
                    B("2. Import a sprite into assets/textures. The watcher imports it, writes a .femeta and cooks a "
                      ".ftex texture array layer.",
                      "2. Импортируйте спрайт в assets/textures. Наблюдатель импортирует файл, пишет .femeta и "
                      "готовит слой текстурного массива .ftex."),
                    B("3. Create assets/scenes/main.toml with one SpriteRenderer, one Transform2D and one "
                      "RigidBody2D + Collider2D.",
                      "3. Создайте assets/scenes/main.toml с одним SpriteRenderer, одним Transform2D и одним "
                      "RigidBody2D + Collider2D."),
                    B("4. Register the systems: input in PreUpdate, physics in FixedUpdate, movement in Update, "
                      "PinkUI in Render.",
                      "4. Зарегистрируйте системы: ввод в PreUpdate, физика в FixedUpdate, движение в Update, "
                      "PinkUI в Render."),
                    B("5. Give the camera a CameraStackSystem and call SetDisplayScale() from "
                      "SDL_GetWindowDisplayScale so UI glyphs are crisp on HiDPI.",
                      "5. Дайте камере CameraStackSystem и вызовите SetDisplayScale() из "
                      "SDL_GetWindowDisplayScale, чтобы глифы UI были чёткими на HiDPI."),
                    B("6. Add an AccessibilityKernel from project settings; BlindMode and DeafMode are read from "
                      "project.femproj, not hard-coded.",
                      "6. Подключите AccessibilityKernel из настроек проекта; BlindMode и DeafMode читаются из "
                      "project.femproj, а не зашиты в код."),
                    B("7. Create a NetRollbackSession per match, bind a pure SimulateTick callback and feed "
                      "NetInputFrame values.",
                      "7. Создайте NetRollbackSession на матч, привяжите чистый колбэк SimulateTick и подавайте "
                      "значения NetInputFrame."),
                    B("8. Log through LogBoy: severity + system hash + fixed-format message. Open the LogBoy panel "
                      "and filter by Desync while testing netcode.",
                      "8. Логируйте через LogBoy: уровень + хэш системы + сообщение в фиксированном формате. "
                      "Откройте панель LogBoy и фильтруйте по Desync при тестировании сети."),
                    B("9. Enable Physics Debug Draw in the Viewport while tuning colliders and cloth.",
                      "9. Включите Physics Debug Draw во Viewport при настройке коллайдеров и ткани."),
                    B("10. Cook with FempakCooker, run the suite with ctest and the style gate, then ship the "
                      "engine-only target.",
                      "10. Соберите архив через FempakCooker, прогоните ctest и стилевой гейт, затем "
                      "поставляйте цель только с движком."),
                ],
            },
            {
                "type": "code",
                "lang": "cpp",
                "text": "// 4-7 in code: systems, accessibility and rollback for one match.\n"
                        "Fby::Ecs::Schedule schedule;\n"
                        "schedule.Add(Fby::Ecs::Phase::PreUpdate, \"input\", &InputSystem);\n"
                        "schedule.Add(Fby::Ecs::Phase::FixedUpdate, \"physics\", &PhysicsStepSystem, &physics);\n"
                        "schedule.Add(Fby::Ecs::Phase::Update, \"movement\", &MovementSystem);\n"
                        "schedule.Add(Fby::Ecs::Phase::Render, \"ui\", &UiSystem, &ui);\n"
                        "\n"
                        "Fby::AccessibilityKernel kernel;\n"
                        "kernel.Configure(settings);              // blind/deaf/colour/flash\n"
                        "\n"
                        "Fby::Net::NetRollbackSession session;\n"
                        "if (session.Initialize(registry, schema, cfg, arena) != Fby::Net::NetRollbackSession::Result::Ok)\n"
                        "{\n"
                        "    LogBoy().Log(Fby::Editor::LogBoySeverity::Critical, \"net\", \"rollback init failed\");\n"
                        "}",
            },
        ],
    },
    {
        "number": "18",
        "title": B("Testing, style gate and release checklist", "Тесты, стилевой гейт и чек-лист релиза"),
        "blocks": [
            {
                "type": "p",
                "text": B(
                    "The suite is deliberately framework-free: every test is an executable with a main() that "
                    "returns non-zero on failure, so it runs on a headless CI agent, a console devkit and a "
                    "developer workstation with the same command. The full suite passes at Release.",
                    "Набор намеренно не использует фреймворк: каждый тест — исполняемый файл с main(), "
                    "возвращающим ненулевой код при ошибке, поэтому одинаково работает на headless CI-агенте, "
                    "консольном devkit и рабочей станции разработчика. На релиз-кандидате проходит весь набор.",
                ),
            },
            {
                "type": "code",
                "lang": "bash",
                "text": "ctest --test-dir build --output-on-failure          # the full suite\n"
                        "python Source/Tools/Dev/CheckStyle.py               # style gate\n"
                        "python Documentation/Tools/GenerateAPIReference.py  # this manual\n"
                        "python Documentation/Tools/GenerateDocumentation.py\n"
                        "python Documentation/Tools/GenerateApplicationGuide.py",
            },
            {
                "type": "bullets",
                "items": [
                    B("A release build enables asserts and runs the whole suite once with FBY_ENABLE_ASSERTS=1, "
                      "because two latent debug-only compile errors existed in JobSystem at RC.",
                      "Релизная сборка включает утверждения и один раз прогоняет весь набор с "
                      "FBY_ENABLE_ASSERTS=1, потому что на RC существовали две скрытые ошибки компиляции "
                      "debug-сборки в JobSystem."),
                    B("The POSIX branch of WriteFileAtomically() and LogLine() must be compiled on Linux/macOS in "
                      "CI; the Windows-only build never saw them.",
                      "POSIX-ветка WriteFileAtomically() и LogLine() должна компилироваться на Linux/macOS в CI; "
                      "Windows-only сборка их не видела."),
                    B("The style gate must report zero failures before a commit is eligible for release.",
                      "Стилевой гейт должен сообщать ноль ошибок, прежде чем коммит может считаться релизным."),
                ],
            },
        ],
    },
    {
        "number": "19",
        "title": B("Known gaps and recommended next work", "Известные пробелы и рекомендуемая следующая работа"),
        "blocks": [
            {
                "type": "p",
                "text": B(
                    "The live backlog, with file/line evidence and a status per item, is Documentation/ROADMAP.md. "
                    "The items previously listed here (the GOG shim build step, a canvas mixing text with its own "
                    "image array, std::function in the window observers/dialogs, BlindMode focused-widget "
                    "narration, SceneDiagnostics simulation time and fixed-step enforcement) are tracked there; "
                    "this chapter keeps no second copy.",
                    "Живой бэклог с файлами, строками и статусом каждого пункта — Documentation/ROADMAP.md. "
                    "Ранее перечисленные здесь пункты (сборка GOG-шима, канвас со смешением текста и собственного "
                    "массива изображений, std::function в наблюдателях/диалогах окна, озвучивание подписи "
                    "сфокусированного виджета в BlindMode, время симуляции в SceneDiagnostics и контроль "
                    "фиксированного шага) отслеживаются там; эта глава не держит вторую копию.",
                ),
            },
        ],
    },
]


# ---------------------------------------------------------------------------
# Generated index
# ---------------------------------------------------------------------------

DECL_RE = re.compile(
    r"^\s*(?:template\s*<[^>]*>\s*)?(?:class|struct)\s+(?:alignas\([^)]*\)\s+)?([A-Za-z_]\w*)"
)
ENUM_RE = re.compile(r"^\s*enum\s+(?:class\s+|struct\s+)?([A-Za-z_]\w*)")
MACRO_RE = re.compile(r"^\s*#define\s+(FBY_\w+|FORCEINLINE\w*)")
SKIP_NAMES = {"Count", "None"}


def iter_public_headers() -> list[pathlib.Path]:
    headers: list[pathlib.Path] = []
    for directory in (ROOT / "Source" / "Engine", ROOT / "Source" / "Editor", ROOT / "Source" / "Plugins"):
        if not directory.is_dir():
            continue
        headers.extend(sorted(directory.rglob("*.hpp")))
        headers.extend(sorted(directory.rglob("*.h")))
    return headers


def module_of(path: pathlib.Path) -> str:
    parts = path.relative_to(ROOT).parts
    if len(parts) >= 3 and parts[0] == "Source":
        if parts[1] == "Plugins":
            return "Plugins/" + (parts[2] if len(parts) > 3 else "")
        if len(parts) >= 4 and parts[1] == "Engine":
            return "Engine/" + parts[2]
        if len(parts) >= 4 and parts[1] == "Editor":
            return "Editor/" + parts[2]
    return "Other"


def scan_symbols() -> tuple[dict[str, list[tuple[str, str, int]]], list[tuple[str, str, int]]]:
    by_module: dict[str, list[tuple[str, str, int]]] = {}
    macros: list[tuple[str, str, int]] = []
    for path in iter_public_headers():
        try:
            lines = path.read_text(encoding="utf-8").splitlines()
        except (OSError, UnicodeDecodeError):
            continue
        relative = path.relative_to(ROOT).as_posix()
        for index, line in enumerate(lines, start=1):
            if line.lstrip().startswith("//"):
                continue
            macro = MACRO_RE.match(line)
            if macro:
                macros.append((macro.group(1), relative, index))
                continue
            match = DECL_RE.match(line) or ENUM_RE.match(line)
            if not match:
                continue
            name = match.group(1)
            if name in SKIP_NAMES or len(name) < 2:
                continue
            kind = "enum" if ENUM_RE.match(line) else ("struct" if "struct" in line.split(name)[0] else "class")
            by_module.setdefault(module_of(path), []).append((name, relative, index))
    for entries in by_module.values():
        entries.sort(key=lambda item: (item[0].lower(), item[1], item[2]))
    return by_module, macros


# ---------------------------------------------------------------------------
# Markdown rendering
# ---------------------------------------------------------------------------


def inline_code(text: str) -> str:
    return text


def render_markdown(lang: str) -> str:
    out: list[str] = []
    out.append(f"# {TITLES[lang]}\n")
    out.append(f"**{SUBTITLES[lang]}**\n")
    out.append(f"\nVersion {DOC_VERSION} ({DOC_STATUS}) · {TODAY} · {LANGUAGES[lang]}\n")
    out.append("\n---\n")
    out.append("\n## Table of contents\n")
    for section in SECTIONS:
        out.append(f"\n{section['number']}. {t(section['title'], lang)}")
    out.append(f"\n{len(SECTIONS) + 1}. Appendix A - generated symbol index")
    out.append(f"\n{len(SECTIONS) + 2}. Appendix B - macros and platform defines")
    out.append("\n")

    for section in SECTIONS:
        out.append(f"\n## {section['number']}. {t(section['title'], lang)}\n")
        for block in section["blocks"]:
            kind = block["type"]
            if kind == "p":
                out.append(t(block["text"], lang) + "\n")
            elif kind == "note":
                out.append("> " + t(block["text"], lang).replace("\n", "\n> ") + "\n")
            elif kind == "bullets":
                for item in block["items"]:
                    out.append("- " + t(item, lang))
                out.append("")
            elif kind == "code":
                out.append(f"```{block.get('lang', 'cpp')}\n{block['text']}\n```\n")
            elif kind == "table":
                headers = [t(cell, lang) for cell in block["headers"]]
                out.append("| " + " | ".join(headers) + " |")
                out.append("|" + "|".join(["---"] * len(headers)) + "|")
                for row in block["rows"]:
                    cells = [t(cell, lang).replace("|", "\\|").replace("\n", " ") for cell in row]
                    out.append("| " + " | ".join(cells) + " |")
                out.append("")

    by_module, macros = scan_symbols()
    out.append(f"\n## {len(SECTIONS) + 1}. Appendix A - generated symbol index\n")
    out.append(
        t(
            B(
                "Generated from the headers on every run. A symbol listed here is declared in the shipped tree; "
                "the header and line are the authoritative location.",
                "Генерируется из заголовков при каждом запуске. Символ из этого списка объявлен в поставляемом "
                "дереве; заголовок и строка — авторитетное расположение.",
            ),
            lang,
        )
        + "\n"
    )
    for module, entries in sorted(by_module.items()):
        out.append(f"\n### {module} ({len(entries)})\n")
        out.append(t(B("| Symbol | Kind | Header | Line |", "| Символ | Вид | Заголовок | Строка |"), lang))
        out.append("|---|---|---|---|")
        for name, header, line in entries:
            out.append(f"| `{name}` | {module.split('/')[-1]} | `{header}` | {line} |")

    out.append(f"\n## {len(SECTIONS) + 2}. Appendix B - macros and platform defines\n")
    out.append(t(B("| Macro | Header | Line |", "| Макрос | Заголовок | Строка |"), lang))
    out.append("|---|---|---|")
    for name, header, line in sorted(macros):
        out.append(f"| `{name}` | `{header}` | {line} |")

    out.append("\n---\n")
    out.append(
        t(
            B(
                "Generated by Documentation/Tools/GenerateAPIReference.py. Do not edit this file by hand: "
                "change the generator and regenerate.",
                "Сгенерировано Documentation/Tools/GenerateAPIReference.py. Не редактируйте этот файл вручную: "
                "измените генератор и перегенерируйте.",
            ),
            lang,
        )
        + "\n"
    )
    return "\n".join(out)


# ---------------------------------------------------------------------------
# DOCX rendering
# ---------------------------------------------------------------------------


def shade(paragraph, fill: str) -> None:
    p_pr = paragraph._p.get_or_add_pPr()
    shd = OxmlElement("w:shd")
    shd.set(qn("w:val"), "clear")
    shd.set(qn("w:color"), "auto")
    shd.set(qn("w:fill"), fill)
    p_pr.append(shd)


def set_cell_background(cell, fill: str) -> None:
    tc_pr = cell._tc.get_or_add_tcPr()
    shd = OxmlElement("w:shd")
    shd.set(qn("w:val"), "clear")
    shd.set(qn("w:color"), "auto")
    shd.set(qn("w:fill"), fill)
    tc_pr.append(shd)


def add_code(document, text: str) -> None:
    for line in text.splitlines() or [""]:
        paragraph = document.add_paragraph()
        paragraph.style = document.styles["Normal"]
        paragraph.paragraph_format.space_before = Pt(0)
        paragraph.paragraph_format.space_after = Pt(0)
        paragraph.paragraph_format.line_spacing = 1.0
        shade(paragraph, CODE_BG)
        run = paragraph.add_run(line if line else " ")
        run.font.name = "Consolas"
        run.font.Size = Pt(8.5)
        run.font.color.rgb = RGBColor(0x24, 0x24, 0x2C)
    document.add_paragraph()


def add_language_mark(paragraph, lang: str) -> None:
    for run in paragraph.runs:
        r_pr = run._element.get_or_add_rPr()
        lang_element = OxmlElement("w:lang")
        lang_element.set(qn("w:val"), "en-US" if lang == "en" else "ru-RU")
        r_pr.append(lang_element)


def build_docx(path: pathlib.Path, lang: str) -> None:
    document = Document()

    section = document.sections[0]
    section.left_margin = Cm(2.2)
    section.right_margin = Cm(2.2)
    section.top_margin = Cm(2.0)
    section.bottom_margin = Cm(2.0)

    style = document.styles["Normal"]
    style.font.name = "Arial"
    style.font.Size = Pt(10.5)
    style.paragraph_format.space_after = Pt(6)

    title = document.add_paragraph()
    title.alignment = WD_ALIGN_PARAGRAPH.CENTER
    title_run = title.add_run(TITLES[lang])
    title_run.bold = True
    title_run.font.Size = Pt(26)
    title_run.font.color.rgb = ACCENT

    subtitle = document.add_paragraph()
    subtitle.alignment = WD_ALIGN_PARAGRAPH.CENTER
    subtitle_run = subtitle.add_run(SUBTITLES[lang])
    subtitle_run.font.Size = Pt(12)
    subtitle_run.font.color.rgb = MUTED

    meta = document.add_paragraph()
    meta.alignment = WD_ALIGN_PARAGRAPH.CENTER
    meta.add_run(f"Version {DOC_VERSION} ({DOC_STATUS}) - {TODAY} - {LANGUAGES[lang]}").font.Size = Pt(9)

    document.add_paragraph()
    document.add_heading(t(B("Contents", "Содержание"), lang), level=1)
    for item in SECTIONS:
        document.add_paragraph(f"{item['number']}. {t(item['title'], lang)}", style="List Bullet")
    document.add_paragraph(f"{len(SECTIONS) + 1}. " + t(B("Appendix A - generated symbol index",
                                                         "Приложение A - сгенерированный индекс символов"), lang),
                           style="List Bullet")
    document.add_paragraph(f"{len(SECTIONS) + 2}. " + t(B("Appendix B - macros and platform defines",
                                                         "Приложение B - макросы и платформенные define"), lang),
                           style="List Bullet")
    document.add_page_break()

    for item in SECTIONS:
        heading = document.add_heading(f"{item['number']}. {t(item['title'], lang)}", level=1)
        for run in heading.runs:
            run.font.color.rgb = INK
        for block in item["blocks"]:
            kind = block["type"]
            if kind == "p":
                add_language_mark(document.add_paragraph(t(block["text"], lang)), lang)
            elif kind == "note":
                paragraph = document.add_paragraph()
                shade(paragraph, "FFF0F7")
                run = paragraph.add_run(t(block["text"], lang))
                run.italic = True
                add_language_mark(paragraph, lang)
            elif kind == "bullets":
                for bullet in block["items"]:
                    paragraph = document.add_paragraph(style="List Bullet")
                    paragraph.add_run(t(bullet, lang))
                    add_language_mark(paragraph, lang)
            elif kind == "code":
                add_code(document, block["text"])
            elif kind == "table":
                headers = [t(cell, lang) for cell in block["headers"]]
                table = document.add_table(rows=1, cols=len(headers))
                table.style = "Light Grid Accent 1"
                table.alignment = WD_TABLE_ALIGNMENT.CENTER
                header_cells = table.rows[0].cells
                for index, header in enumerate(headers):
                    header_cells[index].text = ""
                    paragraph = header_cells[index].paragraphs[0]
                    run = paragraph.add_run(header)
                    run.bold = True
                    run.font.color.rgb = RGBColor(0xFF, 0xFF, 0xFF)
                    set_cell_background(header_cells[index], HEADER_BG)
                for row in block["rows"]:
                    cells = table.add_row().cells
                    for index, cell in enumerate(row):
                        cells[index].text = t(cell, lang)
                document.add_paragraph()

    by_module, macros = scan_symbols()
    document.add_page_break()
    heading = document.add_heading(f"{len(SECTIONS) + 1}. " + t(B("Appendix A - generated symbol index",
                                                                    "Приложение A - сгенерированный индекс символов"),
                                                                   lang), level=1)
    for run in heading.runs:
        run.font.color.rgb = INK
    add_language_mark(
        document.add_paragraph(
            t(
                B(
                    "Generated from the headers on every run. Header and line are the authoritative location.",
                    "Генерируется из заголовков при каждом запуске. Заголовок и строка — авторитетное расположение.",
                ),
                lang,
            )
        ),
        lang,
    )
    for module, entries in sorted(by_module.items()):
        document.add_heading(f"{module} ({len(entries)})", level=2)
        table = document.add_table(rows=1, cols=4)
        table.style = "Light Grid Accent 1"
        for index, header in enumerate([t(B("Symbol", "Символ"), lang), t(B("Kind", "Вид"), lang),
                                        t(B("Header", "Заголовок"), lang), t(B("Line", "Строка"), lang)]):
            cell = table.rows[0].cells[index]
            cell.text = ""
            run = cell.paragraphs[0].add_run(header)
            run.bold = True
            run.font.color.rgb = RGBColor(0xFF, 0xFF, 0xFF)
            set_cell_background(cell, HEADER_BG)
        for name, header, line in entries:
            cells = table.add_row().cells
            cells[0].text = name
            cells[1].text = module.split("/")[-1]
            cells[2].text = header
            cells[3].text = str(line)
        document.add_paragraph()

    heading = document.add_heading(f"{len(SECTIONS) + 2}. " + t(B("Appendix B - macros and platform defines",
                                                                  "Приложение B - макросы и платформенные define"),
                                                                 lang), level=1)
    for run in heading.runs:
        run.font.color.rgb = INK
    table = document.add_table(rows=1, cols=3)
    table.style = "Light Grid Accent 1"
    for index, header in enumerate([t(B("Macro", "Макрос"), lang), t(B("Header", "Заголовок"), lang),
                                    t(B("Line", "Строка"), lang)]):
        cell = table.rows[0].cells[index]
        cell.text = ""
        run = cell.paragraphs[0].add_run(header)
        run.bold = True
        run.font.color.rgb = RGBColor(0xFF, 0xFF, 0xFF)
        set_cell_background(cell, HEADER_BG)
    for name, header, line in sorted(macros):
        cells = table.add_row().cells
        cells[0].text = name
        cells[1].text = header
        cells[2].text = str(line)

    footer = document.sections[0].footer.paragraphs[0]
    footer.alignment = WD_ALIGN_PARAGRAPH.CENTER
    footer_run = footer.add_run(f"{TITLES[lang]} - {LANGUAGES[lang]}")
    footer_run.font.Size = Pt(8)
    footer_run.font.color.rgb = MUTED

    document.core_properties.title = TITLES[lang]
    document.core_properties.subject = SUBTITLES[lang]
    document.core_properties.author = "FemboyEngine"
    document.core_properties.language = "en-US" if lang == "en" else "ru-RU"
    document.save(path)


# ---------------------------------------------------------------------------
# PDF export
# ---------------------------------------------------------------------------


def export_pdf(docx_path: pathlib.Path, pdf_path: pathlib.Path, node: str, lo_cli: str) -> bool:
    if not node or not pathlib.Path(node).is_file():
        print("pdf: node executable not found; pass --node or set FBY_NODE", file=sys.stderr)
        return False
    if not lo_cli or not pathlib.Path(lo_cli).is_file():
        print("pdf: LibreOffice Kit CLI not found; pass --lo-cli or set FBY_LO_CLI", file=sys.stderr)
        return False
    pdf_path.unlink(missing_ok=True)
    # Keep the LibreOffice transcript out of Documentation/: the style gate
    # rejects a committed .log, and the generator must be runnable on a clean
    # tree without leaving junk behind.
    log_path = pathlib.Path(tempfile.gettempdir()) / "fby_api_reference_pdf.log"
    with log_path.open("w", encoding="utf-8") as log:
        completed = subprocess.run(
            [node, lo_cli, "convert", "--input", str(docx_path), "--output", str(pdf_path)],
            stdout=log,
            stderr=log,
        )
    if completed.returncode != 0 or not pdf_path.is_file() or pdf_path.stat().st_size == 0:
        print(f"pdf: export failed; see {log_path}", file=sys.stderr)
        return False
    if pdf_path.read_bytes()[:5] != b"%PDF-":
        print("pdf: output is not a PDF", file=sys.stderr)
        return False
    return True


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--skip-pdf", action="store_true", help="write Markdown and DOCX only")
    parser.add_argument("--node", default=DEFAULT_NODE, help="node executable for LibreOffice Kit")
    parser.add_argument("--lo-cli", default=DEFAULT_LO_CLI, help="LibreOffice Kit cli.js")
    args = parser.parse_args()

    docpaths.EnsureLayout()

    en_md = render_markdown("en")
    ru_md = render_markdown("ru")
    docpaths.Md("en", "FemboyEngineAPIReference_EN.md").write_text(en_md, encoding="utf-8")
    docpaths.Md("ru", "FemboyEngineAPIReference_RU.md").write_text(ru_md, encoding="utf-8")

    by_module, macros = scan_symbols()
    symbol_count = sum(len(entries) for entries in by_module.values())
    print(f"markdown: {symbol_count} symbols, {len(macros)} macros, {len(SECTIONS)} sections")

    if not HAVE_DOCX:
        print("docx: python-docx is not installed; Markdown written, DOCX/PDF skipped", file=sys.stderr)
        return 1

    en_docx = docpaths.Docx("en", "FemboyEngineAPIReference_EN.docx")
    ru_docx = docpaths.Docx("ru", "FemboyEngineAPIReference_RU.docx")
    build_docx(en_docx, "en")
    build_docx(ru_docx, "ru")
    print(f"docx: {en_docx.name}, {ru_docx.name}")

    if args.skip_pdf:
        print("pdf: skipped by request")
        return 0

    en_pdf = docpaths.Pdf("en", "FemboyEngineAPIReference_EN.pdf")
    ru_pdf = docpaths.Pdf("ru", "FemboyEngineAPIReference_RU.pdf")
    ok_en = export_pdf(en_docx, en_pdf, args.node, args.lo_cli)
    ok_ru = export_pdf(ru_docx, ru_pdf, args.node, args.lo_cli)
    if not (ok_en and ok_ru):
        return 1
    print(f"pdf: {en_pdf.name}, {ru_pdf.name}")
    docconvert.BuildDjvu(en_pdf, docpaths.Djvu("en", "FemboyEngineAPIReference_EN.djvu"))
    docconvert.BuildDjvu(ru_pdf, docpaths.Djvu("ru", "FemboyEngineAPIReference_RU.djvu"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
