#!/usr/bin/env python3
"""Compile the FemboyEngine2D architecture specification.

Outputs (all under Documentation/):
  * md/en/FemboyEngineArchitecture_EN.md       English edition
  * md/ru/FemboyEngineArchitecture_RU.md       Russian edition
  * docx/en/FemboyEngineArchitecture_EN.docx   English edition
  * docx/ru/FemboyEngineArchitecture_RU.docx   Russian edition
  * pdf/en/FemboyEngineArchitecture_EN.pdf     exported from the English DOCX
  * pdf/ru/FemboyEngineArchitecture_RU.pdf     exported from the Russian DOCX

The DOCX uses named, language-neutral styles with Arial / Consolas and
explicit w:lang tags on every run, so adding another locale only requires a new
translation table. No layout or style code changes per language.

Requirements: python-docx. PDF export uses LibreOffice Kit through Node.
"""

from __future__ import annotations

import argparse
import datetime as dt
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import textwrap

sys.dont_write_bytecode = True
import DocPaths as docpaths
import DocumentConvert as docconvert
from DocVersion import STATUS as DOC_STATUS
from DocVersion import VERSION as DOC_VERSION

from docx import Document
from docx.enum.table import WD_TABLE_ALIGNMENT
from docx.enum.text import WD_ALIGN_PARAGRAPH, WD_BREAK
from docx.oxml import OxmlElement
from docx.oxml.ns import qn
from docx.shared import Cm, Inches, Pt, RGBColor

ROOT = pathlib.Path(__file__).resolve().parents[2]
DOCUMENTATION = ROOT / "Documentation"
TODAY = dt.date.today().isoformat()

ACCENT = RGBColor(0xE8, 0x5D, 0x9E)
INK = RGBColor(0x1E, 0x1E, 0x24)
MUTED = RGBColor(0x6B, 0x6B, 0x76)
CODE_BG = "F4F4F7"
HEADER_BG = "2B2B33"
NOTE_BG = "FFF0F7"

DEFAULT_NODE = os.environ.get("FBY_NODE", "node")
DEFAULT_LO_CLI = os.environ.get("FBY_LO_CLI", "")


def B(en: str, ru: str) -> dict[str, str]:
    return {"en": en, "ru": ru}


SECTIONS: list[dict] = [
    {
        "number": "1",
        "title": B("Executive summary", "Краткое резюме"),
        "blocks": [
            (
                "p",
                B(
                    "FemboyEngine2D is a C++26, data-oriented 2D engine at release-candidate status. "
                    "The runtime is split into one static library per shipping role: a game links the engine, "
                    "an editor links the engine plus the editor UI library, and the offline cooker links only the "
                    "asset libraries. A shipped game never pulls in Dear ImGui, ImGuizmo, ImPlot or the TOML parser.",
                    "FemboyEngine2D — двумерный движок на C++26, построенный вокруг data-oriented design и "
                    "находящийся в статусе Release. Рантайм разделён на отдельные статические библиотеки "
                    "по роли поставки: игра линкует движок, редактор — движок и библиотеку UI редактора, а офлайн-кукер "
                    "линкует только библиотеки ассетов. В поставку игры не попадают Dear ImGui, ImGuizmo, ImPlot и "
                    "TOML-парсер.",
                ),
            ),
            (
                "p",
                B(
                    "The design rules are deliberately strict. Simulation state is plain, trivially copyable data; "
                    "hot paths use fixed capacity, function pointers and arena storage; frame paths do not allocate; "
                    "and every subsystem exposes deterministic, checksummed state. Those rules are what make rollback, "
                    "hot reload, snapshot testing and GPU multi-draw practical rather than aspirational.",
                    "Правила проектирования намеренно жёсткие. Состояние симуляции — это простые trivially copyable "
                    "данные; горячие пути используют фиксированную ёмкость, указатели на функции и арены; кадровые "
                    "пути не выделяют память; каждая подсистема предоставляет детерминированное состояние с "
                    "контрольной суммой. Именно это делает роллбэк, горячую перезагрузку, snapshot-тесты и GPU "
                    "multi-draw практичными, а не теоретическими.",
                ),
            ),
            (
                "bullets",
                B(
                    "Core: Definitions.hpp, Vector2D/3D/4D, jobs, platform, plugin ABI, Mirror reflection.",
                    "Ядро: Definitions.hpp, Vector2D/3D/4D, jobs, платформа, plugin ABI, рефлексия Mirror.",
                ),
            ),
            (
                "bullets",
                B(
                    "RHI: persistent frame rings, 48-byte sprite items, indirect mesh draws and one compute dispatch "
                    "for linear-blend skinning.",
                    "RHI: постоянные кольцевые буферы кадра, 48-байтные sprite-элементы, indirect-отрисовка мешей и "
                    "один compute-диспатч на linear-blend skinning.",
                ),
            ),
            (
                "bullets",
                B(
                    "ECS and physics: EnTT registry, declarative SystemGraph, Snapshot v4, Box2D v3 bridge, joint "
                    "chains, Verlet cloth and two water models.",
                    "ECS и физика: реестр EnTT, декларативный SystemGraph, Snapshot v4, мост Box2D v3, цепочки "
                    "суставов, Verlet-ткань и две модели воды.",
                ),
            ),
            (
                "bullets",
                B(
                    "Network: NetRollbackSession with a 256-tick snapshot ring, input prediction, late-input replay "
                    "and stable entity ids.",
                    "Сеть: NetRollbackSession с кольцом снапшотов на 256 тиков, предсказанием ввода, повтором "
                    "опоздавших входов и стабильными идентификаторами сущностей.",
                ),
            ),
            (
                "bullets",
                B(
                    "Tooling: transactional CommandStack, AssetDatabase with .femeta rows, watcher, cooker, VFS and "
                    "the ImNodes graph editors with an AOT transcriber.",
                    "Инструментарий: транзакционный CommandStack, AssetDatabase со строками .femeta, watcher, кукер, "
                    "VFS и редакторы графов на ImNodes с AOT-транспилятором.",
                ),
            ),
        ],
    },
    {
        "number": "2",
        "title": B("Repository hierarchy and build targets", "Иерархия репозитория и цели сборки"),
        "blocks": [
            (
                "p",
                B(
                    "All source lives under Source/. The include root is Source itself, so every include names its "
                    "domain explicitly. A header under Source/Engine/RHI is included as \"Engine/RHI/RHI.hpp\"; editor "
                    "code uses \"Editor/Panels/...\". This removes the old ambient fby/ prefix and makes ownership "
                    "visible in the first line of a file.",
                    "Весь исходный код находится в Source/. Корнем include-путей является сам Source, поэтому каждый "
                    "include явно называет свой домен. Заголовок в Source/Engine/RHI подключается как "
                    "\"Engine/RHI/RHI.hpp\"; код редактора использует \"Editor/Panels/...\". Это убирает старый "
                    "неявный префикс fby/ и делает принадлежность файла видимой с первой строки.",
                ),
            ),
            (
                "table",
                {
                    "headers": B("Path", "Путь"),
                    "rows": [
                        B("Source/Engine/Core", "Source/Engine/Core"),
                        B("Source/Engine/RHI", "Source/Engine/RHI"),
                        B("Source/Engine/ECS", "Source/Engine/ECS"),
                        B("Source/Engine/Physics", "Source/Engine/Physics"),
                        B("Source/Engine/Audio", "Source/Engine/Audio"),
                        B("Source/Engine/Network", "Source/Engine/Network"),
                        B("Source/Engine/Assets", "Source/Engine/Assets"),
                        B("Source/Engine/Script", "Source/Engine/Script"),
                        B("Source/Engine/UI", "Source/Engine/UI"),
                        B("Source/Engine/Video", "Source/Engine/Video"),
                        B("Source/Editor/Panels", "Source/Editor/Panels"),
                        B("Source/Editor/Infrastructure", "Source/Editor/Infrastructure"),
                        B("Source/Apps, Source/Tests, Source/Tools, Source/Plugins", "Source/Apps, Source/Tests, Source/Tools, Source/Plugins"),
                    ],
                    "descriptions": [
                        B("Definitions, value types, jobs, platform, plugins, Mirror", "Определения, типы значений, jobs, платформа, плагины, Mirror"),
                        B("Render hardware interface, shaders, materials, camera stack", "Интерфейс рендеринга, шейдеры, материалы, стек камер"),
                        B("Registry, components, scenes, snapshots, skeleton, animation", "Реестр, компоненты, сцены, снапшоты, скелет, анимация"),
                        B("Box2D v3 bridge, joints, cloth, fluid, particles", "Мост Box2D v3, суставы, ткань, жидкость, частицы"),
                        B("Mixer, DSP graph, limiter, Ogg/Opus", "Микшер, DSP-граф, лимитер, Ogg/Opus"),
                        B("NetRollbackSession, input rings, snapshot replication", "NetRollbackSession, кольца ввода, репликация снапшотов"),
                        B("VFS, .femeta, cooker, localization, texture/audio formats", "VFS, .femeta, кукер, локализация, форматы текстур/аудио"),
                        B("Bytecode VM, graph compiler, AOT backend", "Байткод-VM, компилятор графов, AOT-бэкенд"),
                        B("PinkUI, hypertext, font atlas, accessibility", "PinkUI, гипертекст, атлас шрифтов, доступность"),
                        B("WebM/VP9 playback and streaming", "Воспроизведение и стриминг WebM/VP9"),
                        B("Editor panels and ImNodes canvases", "Панели редактора и холсты ImNodes"),
                        B("Command stack, cooker watcher, project management", "Стек команд, watcher кукера, управление проектами"),
                        B("Applications, tests, offline tools, plugin fixtures", "Приложения, тесты, офлайн-инструменты, фикстуры плагинов"),
                    ],
                },
            ),
            (
                "p",
                B(
                    "File names are PascalCase without exception: Vector2D.hpp, NetRollbackSession.cpp, "
                    "ComponentRegistry.cpp. A style gate rejects a non-PascalCase stem, lowercase underscore names, "
                    "non-Allman braces, trailing comments, Cyrillic text, dynamic_cast, typeid and any surviving Vec2, "
                    "Vec3 or Vec4 identifier.",
                    "Имена файлов — PascalCase без исключений: Vector2D.hpp, NetRollbackSession.cpp, "
                    "ComponentRegistry.cpp. Style gate отклоняет не-PascalCase имена, нижние подчёркивания, "
                    "не-Allman скобки, комментарии в конце строки, кириллицу, dynamic_cast, typeid и любые "
                    "оставшиеся идентификаторы Vec2, Vec3 или Vec4.",
                ),
            ),
            (
                "table",
                {
                    "headers": B("Target", "Цель"),
                    "rows": [
                        B("FemboyEngine2D::Engine", "FemboyEngine2D::Engine"),
                        B("FemboyEngine2D::Editor", "FemboyEngine2D::Editor"),
                        B("FemboyEngine2D::AssetCook", "FemboyEngine2D::AssetCook"),
                        B("FemboyEngine2D::AssetLib", "FemboyEngine2D::AssetLib"),
                        B("fby_shaderc_core", "fby_shaderc_core"),
                        B("fby_sandbox, fby_editor, fby_* tools", "fby_sandbox, fby_editor, fby_* tools"),
                    ],
                    "descriptions": [
                        B("Runtime library; no editor UI, no TOML parser", "Рантайм-библиотека; без UI редактора и без TOML-парсера"),
                        B("Editor panels, command stack, ImGui/ImNodes integration", "Панели редактора, стек команд, интеграция ImGui/ImNodes"),
                        B("BC7/ASTC texture cooking shared by editor and offline tools", "Кукинг текстур BC7/ASTC, общий для редактора и офлайн-инструментов"),
                        B("Pack and localization compilation; never shipped in a game", "Компиляция паков и локализации; не поставляется с игрой"),
                        B("glslang wrapper reused in-process by the editor", "Обёртка glslang, переиспользуемая редактором в процессе"),
                        B("Applications, offline cookers, AOT compiler, scene tool", "Приложения, офлайн-кукеры, AOT-компилятор, scene tool"),
                    ],
                },
            ),
        ],
    },
    {
        "number": "3",
        "title": B("Core definitions and the type system", "Базовые определения и система типов"),
        "blocks": [
            (
                "p",
                B(
                    "Source/Engine/Core/Definitions.hpp is the single source of truth for the low-level vocabulary. "
                    "It defines FORCEINLINE, the static cast helper FemCast<>, the checked polymorphic cast Cast<>, "
                    "the stable TypeId and the TypeObject base. It includes Mirror and the core value types, so a "
                    "translation unit that includes it has the complete contract without a second include.",
                    "Source/Engine/Core/Definitions.hpp — единственный источник истины для низкоуровневого словаря. "
                    "Он определяет FORCEINLINE, помощник статического приведения FemCast<>, проверяемое "
                    "полиморфное приведение Cast<>, стабильный TypeId и базовый TypeObject. Он подключает Mirror и "
                    "базовые типы значений, поэтому единица трансляции, включившая его, получает полный контракт без "
                    "второго include.",
                ),
            ),
            (
                "code",
                B(
                    "using TypeId = std::uint64_t;\n"
                    "template <typename T> consteval TypeId StableTypeId() noexcept;\n\n"
                    "template <typename To, typename From>\n"
                    "[[nodiscard]] FORCEINLINE To FemCast(From&& InValue) noexcept;\n\n"
                    "template <typename To, typename From>\n"
                    "[[nodiscard]] FORCEINLINE To* Cast(From* InObject) noexcept;",
                    "using TypeId = std::uint64_t;\n"
                    "template <typename T> consteval TypeId StableTypeId() noexcept;\n\n"
                    "template <typename To, typename From>\n"
                    "[[nodiscard]] FORCEINLINE To FemCast(From&& InValue) noexcept;\n\n"
                    "template <typename To, typename From>\n"
                    "[[nodiscard]] FORCEINLINE To* Cast(From* InObject) noexcept;",
                ),
            ),
            (
                "p",
                B(
                    "StableTypeId<T>() hashes Mirror's component name with FNV-1a. Reflected components therefore "
                    "share one identity between the inspector, snapshot schema, plugin ABI and rollback checksum. "
                    "A non-component class can pin a compiler-independent name with FBY_TYPE_NAME or accept the "
                    "compiler signature hash. Cast<> upcasts at compile time and walks a flat parent table on a "
                    "downcast; a wrong downcast returns nullptr and no compiler-generated type information is read. "
                    "dynamic_cast and typeid are absent from the engine sources, and the style gate enforces that.",
                    "StableTypeId<T>() хеширует имя компонента Mirror через FNV-1a. Поэтому отражённые компоненты "
                    "имеют одну идентичность в инспекторе, схеме снапшотов, plugin ABI и контрольной сумме роллбэка. "
                    "Класс, не являющийся компонентом, может закрепить независимое от компилятора имя через "
                    "FBY_TYPE_NAME либо принять хеш сигнатуры компилятора. Cast<> выполняет upcast на этапе "
                    "компиляции и обходит плоскую таблицу родителей при downcast; неверный downcast возвращает "
                    "nullptr, а сгенерированная компилятором информация о типах не читается. dynamic_cast и typeid "
                    "отсутствуют в исходниках движка, и style gate это проверяет.",
                ),
            ),
            (
                "p",
                B(
                    "Core value types are trivially copyable and standard layout. The coordinate types are Vector2D, "
                    "Vector3D and Vector4D, with uppercase X, Y, Z and W components. Rect is a half-open axis-aligned "
                    "box; Color32 is one packed uint32 in 0xAABBGGRR order; Handle<Tag> is a nullable 32-bit pool index, "
                    "never a pointer. Every component is checked by the compiler for trivial copyability and standard "
                    "layout before it can enter a snapshot or a plugin table.",
                    "Базовые типы значений trivially copyable и standard layout. Координатные типы — Vector2D, "
                    "Vector3D и Vector4D с компонентами X, Y, Z и W в верхнем регистре. Rect — полуоткрытый "
                    "axis-aligned прямоугольник; Color32 — один упакованный uint32 в порядке 0xAABBGGRR; "
                    "Handle<Tag> — nullable 32-битный индекс пула, а не указатель. Каждый компонент проверяется "
                    "компилятором на trivial copyability и standard layout до попадания в снапшот или таблицу плагина.",
                ),
            ),
            (
                "table",
                {
                    "headers": B("Layer", "Слой"),
                    "rows": [
                        B("Jobs", "Jobs"),
                        B("Platform", "Платформа"),
                        B("Plugins", "Плагины"),
                        B("Mirror", "Mirror"),
                    ],
                    "descriptions": [
                        B("std::jthread pool, function-pointer tasks, ParallelFor with caller participation", "Пул std::jthread, задачи на указателях функций, ParallelFor с участием вызывающего потока"),
                        B("SDL3 window/input, native dialogs, Vulkan surface creation", "Окно/ввод SDL3, нативные диалоги, создание Vulkan surface"),
                        B("C ABI, layout hash validation, registry supplied by the host", "C ABI, проверка layout hash, реестр предоставляется хостом"),
                        B("consteval component descriptors from member pointers, no P2996 dependency", "consteval-дескрипторы компонентов из указателей на члены, без зависимости от P2996"),
                    ],
                },
            ),
        ],
    },
    {
        "number": "4",
        "title": B("RHI: Vulkan multi-draw and compute skinning", "RHI: Vulkan multi-draw и compute-скиннинг"),
        "blocks": [
            (
                "p",
                B(
                    "The RHI accepts a FramePacket of plain data and never sees a component, an entity or a string. "
                    "A sprite is one 48-byte RenderItem: position, size, uvMin, uvMax, layer, packed colour, rotation "
                    "and flags. Items are sorted once per frame by a 64-bit key (16 bits layer, 40 bits depth, 8 bits "
                    "sequence) and uploaded into a persistently mapped instance ring. The GPU reads one item per "
                    "instance; there is no per-sprite vertex buffer, descriptor set or texture bind.",
                    "RHI принимает FramePacket из простых данных и никогда не видит компонент, сущность или строку. "
                    "Спрайт — один 48-байтный RenderItem: позиция, размер, uvMin, uvMax, слой, упакованный цвет, "
                    "поворот и флаги. Элементы один раз за кадр сортируются 64-битным ключом (16 бит слой, 40 бит "
                    "глубина, 8 бит последовательность) и загружаются в постоянно отображённое кольцо инстансов. GPU "
                    "читает один элемент на инстанс; отдельного vertex-буфера, descriptor set или привязки текстуры "
                    "на спрайт нет.",
                ),
            ),
            (
                "p",
                B(
                    "The mesh path is parallel and deliberately different. Static geometry is uploaded once into a "
                    "device-local arena and addressed through MeshHandle. Deformable SkeletalAnimation/SkinComponent vertices are "
                    "streamed into a host-visible ring; one compute dispatch runs linear-blend skinning for the whole "
                    "frame; the result lands in a device-local SkinnedVertex buffer. MeshVertex is 40 bytes, "
                    "SkinnedVertex is 32 bytes, and both sides use a full column-major Mat4 so a 2.5D camera can "
                    "produce real z and perspective divide.",
                    "Путь мешей параллелен и намеренно устроен иначе. Статическая геометрия один раз загружается в "
                    "device-local арену и адресуется через MeshHandle. Вершины деформируемых SkeletalAnimation/SkinComponent "
                    "потоково попадают в host-visible кольцо; один compute-диспатч выполняет linear-blend skinning "
                    "для всего кадра; результат пишется в device-local буфер SkinnedVertex. MeshVertex занимает 40 "
                    "байт, SkinnedVertex — 32 байта, обе стороны используют полную column-major Mat4, поэтому "
                    "2.5D-камера даёт настоящий z и перспективное деление.",
                ),
            ),
            (
                "code",
                B(
                    "MeshVertex     40 B  position, uv, color, skin range, bone indices, weights\n"
                    "SkinnedVertex  32 B  homogeneous position (vec4), uv, color\n"
                    "RenderItem     48 B  position, size, uvMin, uvMax, layer, color, rotation, flags",
                    "MeshVertex     40 B  позиция, uv, цвет, skin range, индексы костей, веса\n"
                    "SkinnedVertex  32 B  однородная позиция (vec4), uv, цвет\n"
                    "RenderItem     48 B  позиция, размер, uvMin, uvMax, слой, цвет, поворот, флаги",
                ),
            ),
            (
                "p",
                B(
                    "Draws are emitted as one vkCmdDrawIndexedIndirect per (texture array, layer, geometry class). "
                    "Per-draw model, layer and material data live in a storage buffer indexed by gl_InstanceIndex, "
                    "and the indirect command's firstInstance selects the record. That is why one multi-draw can "
                    "carry a different model matrix per mesh without a per-mesh push constant. The Vulkan backend "
                    "requires shaderDrawParameters; the null backend still runs the full frame packet path so tests "
                    "exercise real data, not a mock.",
                    "Отрисовка выполняется одним vkCmdDrawIndexedIndirect на (texture array, слой, класс геометрии). "
                    "Данные модели, слоя и материала на каждый draw лежат в storage-буфере, индексируемом "
                    "gl_InstanceIndex, а firstInstance indirect-команды выбирает запись. Поэтому один multi-draw "
                    "несёт разные model-матрицы без push constant на каждый меш. Vulkan-бэкенд требует "
                    "shaderDrawParameters; null-бэкенд всё равно проходит полный путь FramePacket, чтобы тесты "
                    "проверяли реальные данные, а не заглушку.",
                ),
            ),
            (
                "table",
                {
                    "headers": B("Backend", "Бэкенд"),
                    "rows": [
                        B("Vulkan", "Vulkan"),
                        B("Metal", "Metal"),
                        B("Null", "Null"),
                    ],
                    "descriptions": [
                        B("Windows/Linux/Android; indirect mesh path, compute LBS, runtime loader", "Windows/Linux/Android; indirect-путь мешей, compute LBS, загрузчик в рантайме"),
                        B("macOS/iOS; same FramePacket contract, Metal argument buffers", "macOS/iOS; тот же контракт FramePacket, argument buffers Metal"),
                        B("Headless CI and servers; full packet/ring path, no device", "Headless CI и серверы; полный путь пакетов/колец без устройства"),
                    ],
                },
            ),
            (
                "p",
                B(
                    "Three frames in flight are the default. Each slice owns an instance ring, a mesh vertex ring and "
                    "a mesh index ring allocated once at device creation. Capacity is a startup decision, not a "
                    "per-frame allocation: the defaults cover 1,048,576 sprite items, 65,536 mesh vertices and "
                    "131,072 mesh indices per slice.",
                    "По умолчанию в полёте три кадра. Каждый срез владеет кольцом инстансов, кольцом вершин мешей и "
                    "кольцом индексов, выделенными один раз при создании устройства. Ёмкость — решение на старте, а "
                    "не аллокация на кадр: значения по умолчанию покрывают 1 048 576 sprite-элементов, 65 536 вершин "
                    "и 131 072 индекса мешей на срез.",
                ),
            ),
        ],
    },
    {
        "number": "5",
        "title": B("ECS, schedule and Snapshot v4", "ECS, планировщик и Snapshot v4"),
        "blocks": [
            (
                "p",
                B(
                    "The ECS is EnTT's registry with a strict component contract: trivially copyable, standard layout, "
                    "no string, no container, no vtable. Components are data; behaviour lives in systems. The frame "
                    "has four phases: PreUpdate (variable dt, input and streaming), FixedUpdate (constant dt, gameplay "
                    "and physics), PostUpdate (camera, culling, audio) and Render (extract and editor).",
                    "ECS — это реестр EnTT со строгим контрактом компонентов: trivially copyable, standard layout, без "
                    "строк, контейнеров и vtable. Компоненты — данные; поведение живёт в системах. Кадр имеет четыре "
                    "фазы: PreUpdate (переменный dt, ввод и стриминг), FixedUpdate (постоянный dt, геймплей и физика), "
                    "PostUpdate (камера, куллинг, аудио) и Render (извлечение и редактор).",
                ),
            ),
            (
                "p",
                B(
                    "SystemGraph is the production scheduler. Dependencies are explicit after/before bitmasks; a "
                    "one-time topological sort produces the order; a cycle or a cross-phase dependency fails Build(). "
                    "The graph holds up to 64 nodes and uses function pointers plus an opaque user pointer, never "
                    "std::function. PluginHost removes every node whose user pointer matches when a plugin unloads.",
                    "SystemGraph — производственный планировщик. Зависимости задаются явными битмасками after/before; "
                    "однократная топологическая сортировка строит порядок; цикл или зависимость через фазы приводят к "
                    "ошибке Build(). Граф вмещает до 64 узлов и использует указатели на функции плюс непрозрачный "
                    "user-указатель, а не std::function. PluginHost удаляет все узлы с совпадающим user-указателем "
                    "при выгрузке плагина.",
                ),
            ),
            (
                "table",
                {
                    "headers": B("Phase", "Фаза"),
                    "rows": [
                        B("PreUpdate", "PreUpdate"),
                        B("FixedUpdate", "FixedUpdate"),
                        B("PostUpdate", "PostUpdate"),
                        B("Render", "Render"),
                    ],
                    "descriptions": [
                        B("Input, window events, asset streaming; variable dt", "Ввод, события окна, стриминг ассетов; переменный dt"),
                        B("Gameplay, Box2D, joints, cloth, fluid; 0..N steps of constant dt", "Геймплей, Box2D, суставы, ткань, жидкость; 0..N шагов постоянного dt"),
                        B("Camera follow, culling, mixer submission; variable dt", "Слежение камеры, куллинг, отправка в микшер; переменный dt"),
                        B("Render extract, UI build, editor draw; once per frame", "Извлечение рендера, сборка UI, отрисовка редактора; раз за кадр"),
                    ],
                },
            ),
            (
                "p",
                B(
                    "Snapshot v4 is a canonical, checksummed byte image. The 104-byte header carries magic, version, "
                    "entity/component counts, tick/baseTick, payload bytes, FNV-1a checksum, base checksum, schema "
                    "hash, the RNG state and the v3 world-extension fields. ECS records come first, field-by-field "
                    "through Mirror; the extension bytes for chains, cloth, water and emitter clocks follow and are "
                    "addressed by stable subsystem ids, never by Box2D handles. A delta snapshot stores only changed "
                    "records and still checksums the base it was derived from.",
                    "Snapshot v4 — канонический образ байтов с контрольной суммой. 104-байтный заголовок содержит "
                    "magic, версию, число сущностей/компонентов, tick/baseTick, размер payload, FNV-1a-сумму, сумму "
                    "базы, хеш схемы, состояние RNG и поля мирового расширения v3. Сначала идут ECS-записи, поле за "
                    "полем через Mirror; затем байты расширения для цепочек, ткани, воды и часов эмиттеров, "
                    "адресуемые стабильными id подсистем, а не handles Box2D. Дельта-снапшот хранит только "
                    "изменённые записи и всё равно содержит сумму базы, от которой он построен.",
                ),
            ),
            (
                "code",
                B(
                    "struct SnapshotHeader {\n"
                    "    char magic[8];              // FBYSNP3\\0\n"
                    "    uint32 version;             // kSnapshotVersion\n"
                    "    uint32 flags;               // full or delta\n"
                    "    uint32 entityCount;\n"
                    "    uint32 componentCount;\n"
                    "    uint64 tick, baseTick;\n"
                    "    uint64 payloadBytes, checksum, baseChecksum, schemaHash;\n"
                    "    RngState rng;\n"
                    "    uint32 extensionBytes, extensionOffset;\n"
                    "    uint64 extensionChecksum;\n"
                    "};",
                    "struct SnapshotHeader {\n"
                    "    char magic[8];              // FBYSNP3\\0\n"
                    "    uint32 version;             // kSnapshotVersion\n"
                    "    uint32 flags;               // full or delta\n"
                    "    uint32 entityCount;\n"
                    "    uint32 componentCount;\n"
                    "    uint64 tick, baseTick;\n"
                    "    uint64 payloadBytes, checksum, baseChecksum, schemaHash;\n"
                    "    RngState rng;\n"
                    "    uint32 extensionBytes, extensionOffset;\n"
                    "    uint64 extensionChecksum;\n"
                    "};",
                ),
            ),
            (
                "p",
                B(
                    "Every registered component contributes a name, a stable type id, a Mirror layout hash, a "
                    "canonical size and four function pointers (has, capture, restore, remove). The layout hash "
                    "rejects a same-size field change; the schema hash rejects a different component set. Restore "
                    "does not allocate: the snapshot arena and entity tables are fixed at Initialize().",
                    "Каждый зарегистрированный компонент даёт имя, стабильный type id, layout hash Mirror, "
                    "канонический размер и четыре указателя на функции (has, capture, restore, remove). Layout hash "
                    "отклоняет изменение поля с тем же размером; schema hash отклоняет другой набор компонентов. "
                    "Restore не выделяет память: арена снапшотов и таблицы сущностей фиксированы в Initialize().",
                ),
            ),
        ],
    },
    {
        "number": "6",
        "title": B("Physics: Box2D v3, chains, cloth and water", "Физика: Box2D v3, цепочки, ткань и вода"),
        "blocks": [
            (
                "p",
                B(
                    "The physics header does not include box2d.h. RigidBody2D and Collider2D store packed "
                    "b2BodyId/b2ShapeId values in uint64_t, so gameplay, scene IO and the network layer compile without "
                    "the C API. PhysicsSystem is the only translation unit that knows Box2D exists. Units are explicit: "
                    "Box2D works in metres, the renderer in pixels, and PhysicsDesc::pixelsPerMeter is the single "
                    "conversion point.",
                    "Заголовок физики не включает box2d.h. RigidBody2D и Collider2D хранят упакованные b2BodyId/b2ShapeId "
                    "в uint64_t, поэтому геймплей, scene IO и сетевой слой компилируются без C API. PhysicsSystem — "
                    "единственная единица трансляции, знающая о существовании Box2D. Единицы заданы явно: Box2D "
                    "работает в метрах, рендерер в пикселях, а PhysicsDesc::pixelsPerMeter — единственная точка "
                    "преобразования.",
                ),
            ),
            (
                "table",
                {
                    "headers": B("Model", "Модель"),
                    "rows": [
                        B("Rigid bodies", "Твёрдые тела"),
                        B("Rope / chain", "Верёвка / цепь"),
                        B("Cloth", "Ткань"),
                        B("Water", "Вода"),
                    ],
                    "descriptions": [
                        B("Box2D v3 world, fixed step, contact events, teleport flag", "Мир Box2D v3, фиксированный шаг, события контактов, флаг телепорта"),
                        B("A chain of capsule links with distance or revolute joints; anchors are existing bodies", "Цепочка капсульных звеньев с distance- или revolute-суставами; якоря — существующие тела"),
                        B("32x32 Verlet grid, Gauss-Seidel constraints, overlap query against Box2D shapes", "Verlet-сетка 32x32, ограничения Гаусса-Зейделя, overlap-запрос к формам Box2D"),
                        B("GerstnerField analytic waves plus ShallowWaterGrid on mdspan", "Аналитические волны GerstnerField и ShallowWaterGrid на mdspan"),
                    ],
                },
            ),
            (
                "p",
                B(
                    "A rope is not a special body. It is a chain of small rigid links; a rope uses distance joints, a "
                    "rigid chain replaces every distance joint with a revolute joint and angle limits. The system "
                    "hard-caps chains, links and joints and allocates nothing on the frame path. It owns the link "
                    "entities it creates and never the anchor entities; Shutdown order matters because destroying the "
                    "Box2D world invalidates every stored id.",
                    "Верёвка — не специальное тело. Это цепочка небольших твёрдых звеньев; верёвка использует "
                    "distance-суставы, жёсткая цепь заменяет каждый distance на revolute с угловыми лимитами. "
                    "Система жёстко ограничивает число цепочек, звеньев и суставов и ничего не аллоцирует на кадровом "
                    "пути. Она владеет созданными звеньями и никогда — якорными сущностями; порядок Shutdown важен, "
                    "потому что уничтожение мира Box2D инвалидирует все сохранённые id.",
                ),
            ),
            (
                "p",
                B(
                    "Cloth is a Verlet mass-spring grid projected with a fixed number of Gauss-Seidel iterations, then "
                    "pushed out of the existing Box2D shapes. It is not a second collision world and does not "
                    "duplicate level geometry. The CPU writes MeshVertex positions; the renderer streams them through "
                    "the same deformable path as a skinned mesh, so cloth adds no second draw path. A pinned vertex is "
                    "simply invMass = 0.",
                    "Ткань — Verlet mass-spring сетка, проецируемая фиксированным числом итераций Гаусса-Зейделя и "
                    "затем выталкиваемая из существующих форм Box2D. Это не второй мир коллизий и не дублирование "
                    "геометрии уровня. CPU пишет позиции MeshVertex; рендерер передаёт их тем же деформируемым путём, "
                    "что и скинированный меш, поэтому ткань не добавляет второй путь отрисовки. Закреплённая вершина — "
                    "просто invMass = 0.",
                ),
            ),
            (
                "p",
                B(
                    "Two water models share one system. GerstnerField is a small sum of trochoidal waves: deterministic, "
                    "analytic, allocation-free, with a Jacobian that fades foam in at crests. ShallowWaterGrid stores a "
                    "height/velocity field in mdspan views over fixed arrays and integrates the linearised shallow-water "
                    "equations with a clamped CFL number. Neither model knows about the RHI; a renderer samples the "
                    "field or uploads its height view as an R32F texture.",
                    "Две модели воды живут в одной системе. GerstnerField — небольшая сумма трохоидальных волн: "
                    "детерминированная, аналитическая, без аллокаций, с якобианом для появления пены на гребнях. "
                    "ShallowWaterGrid хранит поле высоты/скорости в mdspan-представлениях над фиксированными "
                    "массивами и интегрирует линеаризованные уравнения мелкой воды с ограниченным числом CFL. Ни одна "
                    "модель не знает о RHI; рендерер сэмплирует поле или загружает его представление высоты как "
                    "R32F-текстуру.",
                ),
            ),
        ],
    },
    {
        "number": "7",
        "title": B("Network rollback and Snapshot v4", "Сетевой роллбэк и Snapshot v4"),
        "blocks": [
            (
                "p",
                B(
                    "NetRollbackSession is the glue between Snapshot v4, CommandLog, Pcg32 and GameplayState. A session "
                    "owns a fixed ring of full snapshots (one per simulated tick), one input ring per player, a stable "
                    "NetEntityId to local EnTT id map and the replay loop. Everything after Initialize() is "
                    "allocation-free: the snapshot arena, entity tables, input rings and frame metadata are fixed "
                    "capacity. The simulation callback is a function pointer plus void*, never std::function.",
                    "NetRollbackSession связывает Snapshot v4, CommandLog, Pcg32 и GameplayState. Сессия владеет "
                    "фиксированным кольцом полных снапшотов (один на симулированный тик), кольцом ввода на игрока, "
                    "картой стабильных NetEntityId в локальные EnTT id и циклом повтора. Всё после Initialize() не "
                    "аллоцирует память: арена снапшотов, таблицы сущностей, кольца ввода и метаданные кадров имеют "
                    "фиксированную ёмкость. Колбэк симуляции — указатель на функцию плюс void*, а не std::function.",
                ),
            ),
            (
                "p",
                B(
                    "When a remote input arrives late, the session restores the snapshot immediately before the "
                    "earliest corrected tick, restores the RNG and CommandLog cursor from the same slot, reapplies "
                    "structural commands, resimulates the affected ticks with the corrected input and prediction for "
                    "still-missing inputs, captures new snapshots, and reports which frames actually changed.",
                    "Когда удалённый ввод приходит с опозданием, сессия восстанавливает снапшот непосредственно перед "
                    "самым ранним исправленным тиком, восстанавливает RNG и курсор CommandLog из того же слота, "
                    "повторно применяет структурные команды, ресимулирует затронутые тики с исправленным вводом и "
                    "предсказанием для всё ещё отсутствующих входов, снимает новые снапшоты и сообщает, какие кадры "
                    "реально изменились.",
                ),
            ),
            (
                "table",
                {
                    "headers": B("Constant", "Константа"),
                    "rows": [
                        B("kMaxNetPlayers = 8", "kMaxNetPlayers = 8"),
                        B("kMaxRollbackTicks = 256", "kMaxRollbackTicks = 256"),
                        B("kMaxNetEntities = 4096", "kMaxNetEntities = 4096"),
                        B("kInputRingCapacity = 256", "kInputRingCapacity = 256"),
                    ],
                    "descriptions": [
                        B("Input ring and tick input array capacity", "Ёмкость кольца ввода и массива входов тика"),
                        B("Snapshot history depth for correction", "Глубина истории снапшотов для коррекции"),
                        B("Stable network entity table", "Таблица стабильных сетевых сущностей"),
                        B("Late input overwrite window", "Окно перезаписи опоздавшего ввода"),
                    ],
                },
            ),
            (
                "p",
                B(
                    "NetInputFrame is 24 bytes and uses signed 16-bit fixed-point axes, not floats: a float bit pattern "
                    "is not guaranteed to survive a different compiler, and the simulation must be a pure function of "
                    "the input bits. The status of each player frame is Confirmed, Predicted or Missing. A late input "
                    "overwrites the prediction in place at the same tick, which is exactly what replay needs.",
                    "NetInputFrame занимает 24 байта и использует знаковые 16-битные fixed-point оси, а не float: "
                    "битовый шаблон float не гарантированно переживает другой компилятор, а симуляция должна быть "
                    "чистой функцией входных битов. Статус кадра каждого игрока — Confirmed, Predicted или Missing. "
                    "Опоздавший ввод перезаписывает предсказание на месте в том же тике, что и требуется для повтора.",
                ),
            ),
            (
                "p",
                B(
                    "NetEntityId is assigned by the authority (or by the local peer for locally owned objects) and is "
                    "never derived from an EnTT id. A restored snapshot, a different spawn order or a different "
                    "allocator must not change it. GameplayState is a 64-bit mask, so the whole replicated state fits "
                    "one register and the checksum is one multiply-fold per entity. CommandLog records structural "
                    "spawn/despawn/add/remove, so a rollback can replay structure as well as values.",
                    "NetEntityId назначается авторитетом (или локальным пиром для локально принадлежащих объектов) и "
                    "никогда не выводится из EnTT id. Восстановленный снапшот, другой порядок спавна или другой "
                    "аллокатор не должны его менять. GameplayState — 64-битная маска, поэтому всё реплицируемое "
                    "состояние помещается в один регистр, а контрольная сумма — одно multiply-fold на сущность. "
                    "CommandLog фиксирует структурные spawn/despawn/add/remove, поэтому роллбэк повторяет и "
                    "структуру, и значения.",
                ),
            ),
        ],
    },
    {
        "number": "8",
        "title": B("Asset pipeline and editor infrastructure", "Пайплайн ассетов и инфраструктура редактора"),
        "blocks": [
            (
                "p",
                B(
                    "A component never stores a path. It stores a small, generation-checked AssetHandle that names one "
                    "row of the AssetDatabase metadata table. The row owns the stable AssetId/GUID, resource kind, "
                    "source and cooked paths, dependency list and import settings. AssetHandle::generation detects a "
                    "reused slot; AssetMeta::contentGeneration lets a renderer or cache compare content versions. "
                    "Hot reload never exposes a half-updated asset: edits are staged and committed atomically.",
                    "Компонент никогда не хранит путь. Он хранит небольшой AssetHandle с проверкой поколения, "
                    "указывающий на строку таблицы метаданных AssetDatabase. Строка владеет стабильными AssetId/GUID, "
                    "видом ресурса, исходным и приготовленным путями, списком зависимостей и настройками импорта. "
                    "AssetHandle::generation обнаруживает переиспользованный слот; AssetMeta::contentGeneration "
                    "позволяет рендереру или кешу сравнивать версии содержимого. Горячая перезагрузка никогда не "
                    "показывает наполовину обновлённый ассет: правки готовятся и фиксируются атомарно.",
                ),
            ),
            (
                "code",
                B(
                    "[[asset]]\n"
                    "guid = \"018f0c2e-...\"\n"
                    "type = \"texture\"\n"
                    "source = \"atlas/orb.png\"\n"
                    "cooked = \"atlas/orb.ftex\"\n"
                    "format = \"bc7_srgb\"\n"
                    "depends = [\"atlas/atlas.toml\"]\n\n"
                    "[import]\n"
                    "generate_mips = true\n"
                    "max_size = 2048",
                    "[[asset]]\n"
                    "guid = \"018f0c2e-...\"\n"
                    "type = \"texture\"\n"
                    "source = \"atlas/orb.png\"\n"
                    "cooked = \"atlas/orb.ftex\"\n"
                    "format = \"bc7_srgb\"\n"
                    "depends = [\"atlas/atlas.toml\"]\n\n"
                    "[import]\n"
                    "generate_mips = true\n"
                    "max_size = 2048",
                ),
            ),
            (
                "p",
                B(
                    "AssetDatabaseWatchSystem watches the source folders through the OS change API and pushes changed "
                    "paths into a fixed SPSC ring. The watcher thread never mutates the database or the RHI; Poll() "
                    "runs on the editor thread, resolves the AssetHandle, schedules a cook, stages the new metadata, "
                    "commits it, bumps the content generation and issues reload notices. A loose PNG/WAV/WebM without "
                    "a .femeta row is imported automatically with deterministic ids and default settings.",
                    "AssetDatabaseWatchSystem следит за исходными папками через OS API и складывает изменённые пути в "
                    "фиксированное SPSC-кольцо. Поток watcher никогда не меняет базу или RHI; Poll() выполняется в "
                    "потоке редактора, находит AssetHandle, планирует кукинг, готовит новые метаданные, фиксирует их, "
                    "увеличивает поколение содержимого и рассылает уведомления о перезагрузке. Свободный PNG/WAV/WebM "
                    "без строки .femeta импортируется автоматически с детерминированными id и настройками по умолчанию.",
                ),
            ),
            (
                "p",
                B(
                    "CommandStack is the editor's undo/redo engine. An inspector edit or an ImGuizmo drag is one "
                    "transaction: BeginTransaction, Record one or more entities, Commit. A recorded component is a "
                    "sparse byte delta, so moving one Vector2D in a 20 KB SkeletonComponent costs tens of bytes. "
                    "Storage is a fixed command array plus one payload arena; no operator new, no std::function, no "
                    "exceptions on the edit path. The stack is editor-only and never links into a game.",
                    "CommandStack — движок undo/redo редактора. Правка инспектора или перетаскивание ImGuizmo — одна "
                    "транзакция: BeginTransaction, Record одной или нескольких сущностей, Commit. Записанный компонент "
                    "хранится как разреженная байтовая дельта, поэтому перемещение одного Vector2D в 20-килобайтном "
                    "SkeletonComponent стоит десятки байт. Хранилище — фиксированный массив команд плюс одна арена "
                    "payload; никакого operator new, std::function и исключений на пути правки. Стек существует только "
                    "в редакторе и не линкуется в игру.",
                ),
            ),
            (
                "table",
                {
                    "headers": B("Tool", "Инструмент"),
                    "rows": [
                        B("VFS", "VFS"),
                        B("VFS", "VFS"),
                        B("fby_fempak", "fby_fempak"),
                        B("fby_loc_tool", "fby_loc_tool"),
                        B("fby_scene_tool", "fby_scene_tool"),
                        B("fby_aotc", "fby_aotc"),
                    ],
                    "descriptions": [
                        B("Mounts .fempak archives and loose directories behind one path space", "Монтирует архивы .fempak и свободные каталоги в одно пространство путей"),
                        B("Deterministic read order, range reads, no per-file allocation on the hot path", "Детерминированный порядок чтения, range-чтение, без аллокаций на файл в горячем пути"),
                        B("Texture/audio/localization cooker; byte-identical with the editor importer", "Кукер текстур/аудио/локализации; байт-в-байт совпадает с импортёром редактора"),
                        B("Localization TOML -> FlatBuffers pack", "TOML локализации -> пакет FlatBuffers"),
                        B("Scene/prefab compiler and round-trip verifier", "Компилятор сцен/префабов и проверка round-trip"),
                        B("Bytecode -> C++26 -> native plugin at cook time", "Байткод -> C++26 -> нативный плагин во время кукинга"),
                    ],
                },
            ),
        ],
    },
    {
        "number": "9",
        "title": B("Visual graphs and the AOT transcriber", "Визуальные графы и AOT-транспилятор"),
        "blocks": [
            (
                "p",
                B(
                    "The Material Graph is a small DAG stored in three flat fixed-capacity arrays: nodes, pins and "
                    "params. A whole material is one trivially copyable value that can be memcpy'd, snapshotted by the "
                    "undo system and hashed without an allocator. The compiler validates exactly one output node, "
                    "connected required pins, type compatibility and acyclicity, then emits one GLSL temporary per "
                    "node. No interpreter and no per-frame dispatch: a material costs exactly what its generated shader "
                    "costs. The hash excludes UI positions, so dragging a node does not recompile a shader.",
                    "Material Graph — небольшой DAG в трёх плоских массивах фиксированной ёмкости: узлы, пины и "
                    "параметры. Целый материал — одно trivially copyable значение, которое можно memcpy-ить, снимать "
                    "в undo-системе и хешировать без аллокатора. Компилятор проверяет ровно один выходной узел, "
                    "подключённость обязательных пинов, совместимость типов и отсутствие циклов, затем генерирует одну "
                    "GLSL-временную переменную на узел. Никакого интерпретатора и диспатча на кадр: материал стоит "
                    "ровно столько, сколько стоит его сгенерированный шейдер. Хеш не включает UI-позиции, поэтому "
                    "перетаскивание узла не перекомпилирует шейдер.",
                ),
            ),
            (
                "table",
                {
                    "headers": B("Limit", "Ограничение"),
                    "rows": [
                        B("MaterialGraph nodes = 128", "MaterialGraph nodes = 128"),
                        B("MaterialGraph pins = 384", "MaterialGraph pins = 384"),
                        B("MaterialGraph params = 64", "MaterialGraph params = 64"),
                        B("LogicGraph nodes = 128", "LogicGraph nodes = 128"),
                        B("LogicGraph links = 256", "LogicGraph links = 256"),
                    ],
                    "descriptions": [
                        B("A larger material is a design smell, not a reason to grow the runtime", "Больший материал — признак проблемы дизайна, а не повод растить рантайм"),
                        B("Every input slot of every node, contiguous per node", "Каждый входной слот каждого узла, непрерывно по узлу"),
                        B("Scalar, vec2, vec3, colour, atlas layer and texture sample payloads", "Payload скаляра, vec2, vec3, цвета, слоя атласа и texture sample"),
                        B("Editor document is one 10 KiB POD", "Документ редактора — один POD на 10 КиБ"),
                        B("Sparse link table; one transaction per edge edit", "Разреженная таблица связей; одна транзакция на правку ребра"),
                    ],
                },
            ),
            (
                "p",
                B(
                    "NodeCanvasPanel is a transactional ImNodes canvas. A panel supplies a CanvasModelOps table of "
                    "plain function pointers and void*; the canvas never sees a virtual interface. The canvas owns "
                    "only interaction state and opens exactly one CommandStack transaction per drag or edge edit. "
                    "Because the whole graph is registered as one external buffer, moving a node records an 8-byte "
                    "Vector2D, not the whole 7 KiB graph.",
                    "NodeCanvasPanel — транзакционный холст ImNodes. Панель предоставляет таблицу CanvasModelOps из "
                    "обычных указателей на функции и void*; холст не видит виртуального интерфейса. Холст владеет "
                    "только состоянием взаимодействия и открывает ровно одну транзакцию CommandStack на "
                    "перетаскивание или правку ребра. Поскольку весь граф зарегистрирован как один внешний буфер, "
                    "перемещение узла записывает 8-байтный Vector2D, а не весь граф на 7 КиБ.",
                ),
            ),
            (
                "p",
                B(
                    "VisualScript compiles the LogicGraph to canonical bytecode with a Kahn topological pass; cycles "
                    "are a compile error, never a runtime stack overflow. The VM executes the exact artifact the "
                    "cooker ships. For release, AotTranspiler emits one flat C++26 translation unit with one function "
                    "per event and zero engine dependencies beyond AotAbi.hpp; AotBuilder runs clang/g++ -O3 -shared "
                    "on a background jthread and the editor polls its state without blocking the frame. The generated "
                    "module carries the graph hash and every component LayoutHash; the loader validates them before "
                    "the first call.",
                    "VisualScript компилирует LogicGraph в канонический байткод с топологической проходкой Кана; цикл — "
                    "ошибка компиляции, а не переполнение стека в рантайме. VM исполняет ровно тот артефакт, который "
                    "поставляет кукер. Для релиза AotTranspiler генерирует одну плоскую единицу трансляции C++26 с "
                    "одной функцией на событие и без зависимостей от движка, кроме AotAbi.hpp; AotBuilder запускает "
                    "clang/g++ -O3 -shared в фоновом jthread, а редактор опрашивает его состояние, не блокируя кадр. "
                    "Сгенерированный модуль несёт хеш графа и каждый LayoutHash компонента; загрузчик проверяет их до "
                    "первого вызова.",
                ),
            ),
            (
                "code",
                B(
                    "LogicGraph -> GraphCompiler -> bytecode -> VM (PIE)\n"
                    "                         \\-> AotTranspiler -> C++26 -> clang/g++ -O3 -shared -> plugin\n\n"
                    "MaterialGraph -> type-check -> GLSL -> glslang -> SPIR-V -> VkPipeline",
                    "LogicGraph -> GraphCompiler -> байткод -> VM (PIE)\n"
                    "                         \\-> AotTranspiler -> C++26 -> clang/g++ -O3 -shared -> плагин\n\n"
                    "MaterialGraph -> проверка типов -> GLSL -> glslang -> SPIR-V -> VkPipeline",
                ),
            ),
        ],
    },
    {
        "number": "10",
        "title": B("Quality gates and verification", "Контроль качества и верификация"),
        "blocks": [
            (
                "p",
                B(
                    "The Release is verified by 38 CTest executables: rollback replay, RNG/noise, network "
                    "rollback, indirect mesh smoke, script VM, script AOT, PinkUI, DSP mixer, audio cook, material "
                    "graph, video, texture cook, asset metadata, animation timeline, editor command stack, editor "
                    "infrastructure, graph editor, physics extensions, UI accessibility, P3 integration and the "
                    "Definitions contract. Tests are plain executables with no framework, so they run headless and on "
                    "a console devkit.",
                    "Release candidate проверяется 21 исполняемым файлом CTest: rollback replay, RNG/noise, сетевой "
                    "роллбэк, indirect mesh smoke, script VM, script AOT, PinkUI, DSP mixer, audio cook, material "
                    "graph, video, texture cook, метаданные ассетов, animation timeline, editor command stack, "
                    "инфраструктура редактора, graph editor, расширения физики, доступность UI, P3 integration и "
                    "контракт Definitions. Тесты — обычные исполняемые файлы без фреймворка, поэтому работают "
                    "headless и на консольном devkit.",
                ),
            ),
            (
                "table",
                {
                    "headers": B("Gate", "Ворота"),
                    "rows": [
                        B("ctest", "ctest"),
                        B("CheckStyle.py", "CheckStyle.py"),
                        B("clang-format", "clang-format"),
                        B("clang-tidy", "clang-tidy"),
                        B("RTTI scan", "RTTI scan"),
                    ],
                    "descriptions": [
                        B("25/25 pass; no GPU required for the headless subset", "25/25 проходят; GPU не требуется для headless-подмножества"),
                        B("PascalCase stems, required tree, no Vec2/3/4, no RTTI tokens, no Cyrillic, Allman, braces", "PascalCase-имена, требуемое дерево, отсутствие Vec2/3/4, RTTI-токенов и кириллицы, Allman, скобки"),
                        B("Allman, 4 spaces, 120 columns, mandatory braces", "Allman, 4 пробела, 120 колонок, обязательные скобки"),
                        B("Parameters In*, outputs Out*, locals Scoped*, no type shadowing", "Параметры In*, выходы Out*, локальные Scoped*, без затенения типов"),
                        B("dynamic_cast/typeid are rejected in source; no engine code uses compiler type information", "dynamic_cast/typeid запрещены в исходниках; код движка не использует информацию типов компилятора"),
                    ],
                },
            ),
            (
                "p",
                B(
                    "The style gate intentionally checks the repository, not only a diff. A new file with a lowercase "
                    "name, a Vec2 alias, a trailing comment or a Cyrillic comment fails the build review before it can "
                    "reach main. The comments are English-only, one line, above the code and without a trailing period; "
                    "the only remaining trailing comments are two NOLINT suppressions required by clang-tidy.",
                    "Style gate намеренно проверяет репозиторий, а не только diff. Новый файл с именем в нижнем "
                    "регистре, алиасом Vec2, комментарием в конце строки или кириллицей падает на ревью до попадания "
                    "в main. Комментарии только на английском, одной строкой, над кодом и без точки в конце; "
                    "единственные оставшиеся комментарии в конце строки — два подавления NOLINT, требуемых clang-tidy.",
                ),
            ),
            (
                "code",
                B(
                    "cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DFBY_BUILD_TESTS=ON\n"
                    "cmake --build build --parallel 8\n"
                    "ctest --test-dir build --output-on-failure\n"
                    "python Source/Tools/Dev/CheckStyle.py",
                    "cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DFBY_BUILD_TESTS=ON\n"
                    "cmake --build build --parallel 8\n"
                    "ctest --test-dir build --output-on-failure\n"
                    "python Source/Tools/Dev/CheckStyle.py",
                ),
            ),
        ],
    },
    {
        "number": "11",
        "title": B("Localization and documentation", "Локализация и документация"),
        "blocks": [
            (
                "p",
                B(
                    "This document is generated by Documentation/Tools/GenerateDocumentation.py. The script owns one "
                    "language-neutral content model and renders it to independent English and Russian editions in "
                    "Markdown, Word and PDF. Each edition has its own DOCX and PDF; the PDF is exported from that "
                    "language's DOCX by LibreOffice Kit.",
                    "Этот документ генерируется Documentation/Tools/GenerateDocumentation.py. Скрипт владеет одной "
                    "языконейтральной моделью контента и рендерит независимые английское и русское издания в "
                    "Markdown, Word и PDF. У каждого издания собственный DOCX и PDF; PDF экспортируется из DOCX "
                    "соответствующего языка через LibreOffice Kit.",
                ),
            ),
            (
                "p",
                B(
                    "Styles are language-neutral: FBY Title, FBY Subtitle, FBY Heading 1-3, FBY Body, FBY Code, "
                    "FBY Caption, FBY Table Header, FBY Table Body and FBY Note. Every run carries an explicit "
                    "w:lang tag (en-US or ru-RU), and the body/heading styles set ascii, hAnsi, eastAsia and cs fonts. "
                    "Adding a language means adding a translation column and a new section; it does not require a "
                    "style rewrite or a per-language layout.",
                    "Стили не зависят от языка: FBY Title, FBY Subtitle, FBY Heading 1-3, FBY Body, FBY Code, "
                    "FBY Caption, FBY Table Header, FBY Table Body и FBY Note. Каждый run несёт явный тег w:lang "
                    "(en-US или ru-RU), а стили тела и заголовков задают шрифты ascii, hAnsi, eastAsia и cs. "
                    "Добавление языка означает добавление колонки перевода и новой секции; переписывать стили или "
                    "делать отдельную вёрстку не нужно.",
                ),
            ),
            (
                "table",
                {
                    "headers": B("Artifact", "Артефакт"),
                    "rows": [
                        B("FemboyEngineArchitecture.md", "FemboyEngineArchitecture.md"),
                        B("FemboyEngineArchitecture_EN.md", "FemboyEngineArchitecture_EN.md"),
                        B("FemboyEngineArchitecture_RU.md", "FemboyEngineArchitecture_RU.md"),
                        B("FemboyEngineArchitecture.docx", "FemboyEngineArchitecture.docx"),
                        B("FemboyEngineArchitecture.pdf", "FemboyEngineArchitecture.pdf"),
                    ],
                    "descriptions": [
                        B("Combined document with both language parts", "Объединённый документ с обеими языковыми частями"),
                        B("Standalone English Markdown", "Отдельный английский Markdown"),
                        B("Standalone Russian Markdown", "Отдельный русский Markdown"),
                        B("Word document with two independent sections", "Документ Word с двумя независимыми секциями"),
                        B("PDF exported from the Word document", "PDF, экспортированный из документа Word"),
                    ],
                },
            ),
            (
                "code",
                B(
                    "python Documentation/Tools/GenerateDocumentation.py\n"
                    "python Documentation/Tools/GenerateDocumentation.py --skip-pdf\n"
                    "python Documentation/Tools/GenerateDocumentation.py --node <node> --lo-cli <libreoffice-kit cli.js>",
                    "python Documentation/Tools/GenerateDocumentation.py\n"
                    "python Documentation/Tools/GenerateDocumentation.py --skip-pdf\n"
                    "python Documentation/Tools/GenerateDocumentation.py --node <node> --lo-cli <libreoffice-kit cli.js>",
                ),
            ),
        ],
    },
    {
        "number": "12",
        "title": B("Next steps after the Release", "Следующие шаги после Release"),
        "blocks": [
            (
                "bullets",
                B(
                    "Promote CheckStyle.py to a CI test and a pre-commit hook, so a lowercase file or a Vec2 alias "
                    "cannot merge.",
                    "Сделать CheckStyle.py тестом CI и pre-commit-хуком, чтобы файл в нижнем регистре или алиас Vec2 "
                    "не могли попасть в main.",
                ),
            ),
            (
                "bullets",
                B(
                    "Add a public C++ API stability policy: frozen component layouts, Snapshot v4 compatibility tests "
                    "and an ABI-version matrix for plugins.",
                    "Добавить политику стабильности публичного C++ API: замороженные layout компонентов, тесты "
                    "совместимости Snapshot v4 и матрицу ABI-версий для плагинов.",
                ),
            ),
            (
                "bullets",
                B(
                    "Finish Metal parity: indirect mesh path, compute skinning and the same FramePacket contract as "
                    "Vulkan, then enable macOS/iOS CI.",
                    "Завершить паритет Metal: indirect-путь мешей, compute-скиннинг и тот же контракт FramePacket, что "
                    "у Vulkan, затем включить CI macOS/iOS.",
                ),
            ),
            (
                "bullets",
                B(
                    "Add snapshot delta compression and a bandwidth budget, with a replay viewer that steps the "
                    "snapshot ring and overlays corrected frames.",
                    "Добавить сжатие дельта-снапшотов и бюджет пропускной способности, а также просмотрщик повторов, "
                    "который шагает по кольцу снапшотов и накладывает исправленные кадры.",
                ),
            ),
            (
                "bullets",
                B(
                    "Move cloth and shallow water to an optional GPU solver, keeping the CPU Verlet path as the "
                    "deterministic fallback used by rollback.",
                    "Перенести ткань и мелкую воду на опциональный GPU-солвер, сохранив CPU Verlet как "
                    "детерминированный fallback для роллбэка.",
                ),
            ),
            (
                "bullets",
                B(
                    "Add a dependency-graph view to the asset database, with cycle detection and cook-order export.",
                    "Добавить представление графа зависимостей в базу ассетов с обнаружением циклов и экспортом "
                    "порядка кукинга.",
                ),
            ),
            (
                "bullets",
                B(
                    "Add editor localization: the same language-tagged style system for panel strings, with hot "
                    "reload from a .fbin catalogue.",
                    "Добавить локализацию редактора: ту же систему стилей с языковыми тегами для строк панелей и "
                    "горячую перезагрузку из каталога .fbin.",
                ),
            ),
            (
                "bullets",
                B(
                    "Add a deterministic replay/fuzz harness to the CI matrix and a performance regression budget for "
                    "the frame path.",
                    "Добавить детерминированный replay/fuzz-стенд в матрицу CI и бюджет регрессии производительности "
                    "кадрового пути.",
                ),
            ),
        ],
    },
]


APPENDIX: list[dict] = [
    {
        "number": "A",
        "title": B("Invariants and capacities", "Инварианты и ёмкости"),
        "blocks": [
            (
                "table",
                {
                    "headers": B("Invariant", "Инвариант"),
                    "rows": [
                        B("RenderItem", "RenderItem"),
                        B("MeshVertex / SkinnedVertex", "MeshVertex / SkinnedVertex"),
                        B("SnapshotHeader", "SnapshotHeader"),
                        B("SystemGraph nodes", "Узлы SystemGraph"),
                        B("Net players / ticks / entities", "Сетевые игроки / тики / сущности"),
                        B("MaterialGraph nodes / pins / params", "Узлы / пины / параметры MaterialGraph"),
                        B("LogicGraph nodes / links", "Узлы / связи LogicGraph"),
                        B("CommandStack commands / payload", "Команды / payload CommandStack"),
                    ],
                    "descriptions": [
                        B("48 bytes, trivially copyable, fixed GPU layout", "48 байт, trivially copyable, фиксированный GPU-layout"),
                        B("40 / 32 bytes, std430-compatible", "40 / 32 байта, совместимы со std430"),
                        B("104 bytes, little-endian, FNV-1a checksum", "104 байта, little-endian, контрольная сумма FNV-1a"),
                        B("64, compile-time capacity, one-time topological sort", "64, ёмкость на этапе компиляции, однократная топологическая сортировка"),
                        B("8 / 256 / 4096, fixed rings and tables", "8 / 256 / 4096, фиксированные кольца и таблицы"),
                        B("128 / 384 / 64, flat arrays", "128 / 384 / 64, плоские массивы"),
                        B("128 / 256, one 10 KiB POD document", "128 / 256, один POD-документ на 10 КиБ"),
                        B("512 commands, 4 MiB default payload arena", "512 команд, арена payload 4 МиБ по умолчанию"),
                    ],
                },
            ),
        ],
    },
    {
        "number": "B",
        "title": B("Glossary", "Глоссарий"),
        "blocks": [
            (
                "table",
                {
                    "headers": B("Term", "Термин"),
                    "rows": [
                        B("DOD", "DOD"),
                        B("MDI", "MDI"),
                        B("LBS", "LBS"),
                        B(".femeta", ".femeta"),
                        B("AOT", "AOT"),
                        B("PIE", "PIE"),
                    ],
                    "descriptions": [
                        B("Data-oriented design: data is flat, hot paths are allocation-free and cache-friendly", "Data-oriented design: данные плоские, горячие пути без аллокаций и дружелюбны к кешу"),
                        B("Multi-draw indirect: one indexed indirect command carries many per-instance draws", "Multi-draw indirect: одна indexed indirect-команда несёт много отрисовок на инстанс"),
                        B("Linear-blend skinning: palette[bone] * bindPosition in one compute pass", "Linear-blend skinning: palette[bone] * bindPosition за один compute-проход"),
                        B("Per-asset metadata row: stable id, source/cooked paths, dependencies, import settings", "Строка метаданных ассета: стабильный id, исходный/приготовленный пути, зависимости, настройки импорта"),
                        B("Ahead-of-time: bytecode translated to a native shared library at cook time", "Ahead-of-time: байткод, превращённый в нативную разделяемую библиотеку во время кукинга"),
                        B("Play-in-editor: the editor runs the game simulation in its own viewport", "Play-in-editor: редактор запускает игровую симуляцию в собственном вьюпорте"),
                    ],
                },
            ),
        ],
    },
]


def blocks_of(section: dict) -> list[tuple[str, dict]]:
    return section["blocks"]


def text(block: dict, language: str) -> str:
    return block[language]


def table_rows(block: dict, language: str) -> tuple[str, list[list[str]]]:
    data = block
    headers = data["headers"][language]
    descriptions = data.get("descriptions", [])
    rows = []
    for index, row in enumerate(data["rows"]):
        value = row[language]
        description = descriptions[index][language] if index < len(descriptions) else ""
        rows.append([value, description])
    return headers, rows


def markdown_escape(value: str) -> str:
    return value.replace("|", "\\|").replace("\n", "<br>")


def render_markdown(section_list: list[dict], language: str, language_name: str, include_header: bool) -> str:
    lines: list[str] = []
    if include_header:
        lines += [
            "# FemboyEngine2D — Architecture Specification",
            "",
            f"**Language section:** {language_name}",
            "",
            f"**Version:** {DOC_VERSION}  ",
            f"**Status:** {DOC_STATUS}  ",
            f"**Date:** {TODAY}  ",
            f"**Document:** FBY-ARCH-0.1  ",
            "",
            "---",
            "",
        ]
    for section in section_list:
        lines.append(f"## {section['number']}. {section['title'][language]}")
        lines.append("")
        for kind, block in section["blocks"]:
            if kind == "p":
                lines.append(block[language])
                lines.append("")
            elif kind == "bullets":
                lines.append(f"- {block[language]}")
                lines.append("")
            elif kind == "code":
                lines += ["```cpp", block[language], "```", ""]
            elif kind == "table":
                headers, rows = table_rows(block, language)
                lines.append(f"| {markdown_escape(headers)} | Details |")
                lines.append("|---|---|")
                for row in rows:
                    lines.append(f"| {markdown_escape(row[0])} | {markdown_escape(row[1])} |")
                lines.append("")
    return "\n".join(lines).rstrip() + "\n"


def set_cell_shading(cell, fill: str) -> None:
    tc_pr = cell._tc.get_or_add_tcPr()
    shd = OxmlElement("w:shd")
    shd.set(qn("w:val"), "clear")
    shd.set(qn("w:color"), "auto")
    shd.set(qn("w:fill"), fill)
    tc_pr.append(shd)


def set_cell_margins(cell, top: int = 80, start: int = 120, bottom: int = 80, end: int = 120) -> None:
    tc = cell._tc
    tc_pr = tc.get_or_add_tcPr()
    tc_mar = tc_pr.first_child_found_in("w:tcMar")
    if tc_mar is None:
        tc_mar = OxmlElement("w:tcMar")
        tc_pr.append(tc_mar)
    for tag, value in (("top", top), ("start", start), ("bottom", bottom), ("end", end)):
        node = tc_mar.find(qn(f"w:{tag}"))
        if node is None:
            node = OxmlElement(f"w:{tag}")
            tc_mar.append(node)
        node.set(qn("w:w"), str(value))
        node.set(qn("w:type"), "dxa")


def repeat_table_header(row) -> None:
    tr_pr = row._tr.get_or_add_trPr()
    tbl_header = OxmlElement("w:tblHeader")
    tbl_header.set(qn("w:val"), "true")
    tr_pr.append(tbl_header)


def set_paragraph_border(paragraph, color: str, size: int = 8, side: str = "bottom") -> None:
    p_pr = paragraph._p.get_or_add_pPr()
    borders = p_pr.find(qn("w:pBdr"))
    if borders is None:
        borders = OxmlElement("w:pBdr")
        p_pr.append(borders)
    border = OxmlElement(f"w:{side}")
    border.set(qn("w:val"), "single")
    border.set(qn("w:sz"), str(size))
    border.set(qn("w:space"), "4")
    border.set(qn("w:color"), color)
    borders.append(border)


LANGUAGE_TAGS = {"en": "en-US", "ru": "ru-RU"}


def set_run_language(run, language: str) -> None:
    language = LANGUAGE_TAGS.get(language, language)
    r_pr = run._element.get_or_add_rPr()
    lang = r_pr.find(qn("w:lang"))
    if lang is None:
        lang = OxmlElement("w:lang")
        r_pr.append(lang)
    lang.set(qn("w:val"), language)
    lang.set(qn("w:eastAsia"), language)


def set_run_font(run, name: str) -> None:
    run.font.name = name
    r_pr = run._element.get_or_add_rPr()
    fonts = r_pr.find(qn("w:rFonts"))
    if fonts is None:
        fonts = OxmlElement("w:rFonts")
        r_pr.append(fonts)
    for attribute in ("w:ascii", "w:hAnsi", "w:cs", "w:eastAsia"):
        fonts.set(qn(attribute), name)


def shade_paragraph(paragraph, fill: str) -> None:
    p_pr = paragraph._p.get_or_add_pPr()
    shd = OxmlElement("w:shd")
    shd.set(qn("w:val"), "clear")
    shd.set(qn("w:color"), "auto")
    shd.set(qn("w:fill"), fill)
    p_pr.append(shd)


def set_style_language(style, language: str = "en-US") -> None:
    r_pr = style.element.get_or_add_rPr()
    lang = r_pr.find(qn("w:lang"))
    if lang is None:
        lang = OxmlElement("w:lang")
        r_pr.append(lang)
    lang.set(qn("w:val"), language)
    lang.set(qn("w:eastAsia"), language)


def set_style_fonts(style, name: str) -> None:
    style.font.name = name
    r_pr = style.element.get_or_add_rPr()
    fonts = r_pr.find(qn("w:rFonts"))
    if fonts is None:
        fonts = OxmlElement("w:rFonts")
        r_pr.append(fonts)
    for attribute in ("w:ascii", "w:hAnsi", "w:cs", "w:eastAsia"):
        fonts.set(qn(attribute), name)


def configure_styles(document: Document) -> None:
    styles = document.styles
    for style_name in ("Normal", "Heading 1", "Heading 2", "Heading 3", "Title", "Subtitle", "Caption"):
        try:
            styles[style_name].font.name = "Arial"
        except KeyError:
            pass

    title = styles["Title"]
    title.font.Size = Pt(34)
    title.font.bold = True
    title.font.color.rgb = ACCENT
    title.paragraph_format.space_after = Pt(6)
    set_style_fonts(title, "Arial")
    set_style_language(title)

    subtitle = styles["Subtitle"]
    subtitle.font.Size = Pt(15)
    subtitle.font.color.rgb = MUTED
    set_style_fonts(subtitle, "Arial")
    set_style_language(subtitle)

    heading_sizes = {"Heading 1": 20, "Heading 2": 14, "Heading 3": 11.5}
    for name, size in heading_sizes.items():
        style = styles[name]
        style.font.Size = Pt(size)
        style.font.bold = True
        style.font.color.rgb = ACCENT if name == "Heading 2" else INK
        style.paragraph_format.space_before = Pt(16 if name == "Heading 1" else 10)
        style.paragraph_format.space_after = Pt(6)
        style.paragraph_format.keep_with_next = True
        set_style_fonts(style, "Arial")
        set_style_language(style)

    body = styles["Normal"]
    body.font.Size = Pt(10.5)
    body.font.color.rgb = INK
    body.paragraph_format.space_after = Pt(6)
    body.paragraph_format.line_spacing = 1.18
    set_style_fonts(body, "Arial")
    set_style_language(body)

    for style_name in ("FBY Title", "FBY Subtitle", "FBY Heading 1", "FBY Heading 2", "FBY Heading 3",
                       "FBY Body", "FBY Code", "FBY Caption", "FBY Table Header", "FBY Table Body", "FBY Note"):
        if style_name in styles:
            continue
        style = styles.add_style(style_name, 1)
        style.base_style = styles["Normal"]
        set_style_fonts(style, "Arial")
        set_style_language(style)

    code = styles["FBY Code"]
    code.font.name = "Consolas"
    code.font.Size = Pt(8.5)
    code.font.color.rgb = RGBColor(0x24, 0x24, 0x2C)
    code.paragraph_format.space_before = Pt(4)
    code.paragraph_format.space_after = Pt(4)
    code.paragraph_format.line_spacing = 1.0
    code.paragraph_format.left_indent = Cm(0.3)
    set_style_fonts(code, "Consolas")

    caption = styles["FBY Caption"]
    caption.font.Size = Pt(8.5)
    caption.font.italic = True
    caption.font.color.rgb = MUTED
    set_style_language(caption)

    header = styles["FBY Table Header"]
    header.font.Size = Pt(9)
    header.font.bold = True
    header.font.color.rgb = RGBColor(0xFF, 0xFF, 0xFF)

    table_body = styles["FBY Table Body"]
    table_body.font.Size = Pt(9)
    table_body.font.color.rgb = INK


def add_page_field(paragraph, language: str) -> None:
    run = paragraph.add_run()
    fld = OxmlElement("w:fldSimple")
    fld.set(qn("w:instr"), "PAGE")
    run._r.addnext(fld)
    set_run_language(run, language)


def add_header_footer(section, language: str, label_en: str, label_ru: str) -> None:
    section.header.is_linked_to_previous = False
    section.footer.is_linked_to_previous = False
    header = section.header.paragraphs[0]
    header.text = ""
    header.alignment = WD_ALIGN_PARAGRAPH.RIGHT
    run = header.add_run(label_en if language == "en-US" else label_ru)
    run.font.Size = Pt(8)
    run.font.color.rgb = MUTED
    set_run_language(run, language)
    set_paragraph_border(header, "E85D9E", 6)

    footer = section.footer.paragraphs[0]
    footer.text = ""
    footer.alignment = WD_ALIGN_PARAGRAPH.CENTER
    prefix = footer.add_run("FemboyEngine2D  ·  FBY-ARCH-0.1  ·  ")
    prefix.font.Size = Pt(8)
    prefix.font.color.rgb = MUTED
    set_run_language(prefix, language)
    add_page_field(footer, language)


def add_body(document: Document, value: str, language: str) -> None:
    paragraph = document.add_paragraph(style="FBY Body")
    paragraph.paragraph_format.alignment = WD_ALIGN_PARAGRAPH.JUSTIFY
    run = paragraph.add_run(value)
    set_run_language(run, language)
    set_run_font(run, "Arial")


def add_bullet(document: Document, value: str, language: str) -> None:
    paragraph = document.add_paragraph(style="FBY Body")
    paragraph.paragraph_format.left_indent = Cm(0.6)
    paragraph.paragraph_format.first_line_indent = Cm(-0.35)
    paragraph.paragraph_format.space_after = Pt(4)
    run = paragraph.add_run("•  " + value)
    set_run_language(run, language)
    set_run_font(run, "Arial")


def add_code(document: Document, value: str, language: str) -> None:
    paragraph = document.add_paragraph(style="FBY Code")
    shade_paragraph(paragraph, CODE_BG)
    lines = value.splitlines()
    for index, line in enumerate(lines):
        if index:
            paragraph.add_run().add_break(WD_BREAK.LINE)
        run = paragraph.add_run(line)
        set_run_language(run, language)
        set_run_font(run, "Consolas")


def add_table(document: Document, block: dict, language: str) -> None:
    headers, rows = table_rows(block, language)
    table = document.add_table(rows=1, cols=2)
    table.style = "Table Grid"
    table.alignment = WD_TABLE_ALIGNMENT.CENTER
    table.autofit = False
    table.columns[0].width = Cm(5.2)
    table.columns[1].width = Cm(12.2)
    header_cells = table.rows[0].cells
    for index, label in enumerate((headers, "Details" if language == "en" else "Детали")):
        cell = header_cells[index]
        set_cell_shading(cell, HEADER_BG)
        set_cell_margins(cell)
        paragraph = cell.paragraphs[0]
        paragraph.style = document.styles["FBY Table Header"]
        run = paragraph.add_run(label)
        set_run_language(run, language)
    repeat_table_header(table.rows[0])
    for row_index, row in enumerate(rows):
        table_row = table.add_row()
        cant_split = OxmlElement("w:cantSplit")
        table_row._tr.get_or_add_trPr().append(cant_split)
        cells = table_row.cells
        for column_index, value in enumerate(row):
            cell = cells[column_index]
            set_cell_margins(cell)
            if row_index % 2 == 1:
                set_cell_shading(cell, "FAFAFC")
            paragraph = cell.paragraphs[0]
            paragraph.style = document.styles["FBY Table Body"]
            run = paragraph.add_run(value)
            set_run_language(run, language)
    document.add_paragraph().paragraph_format.space_after = Pt(0)


def add_note(document: Document, value: str, language: str) -> None:
    paragraph = document.add_paragraph(style="FBY Note")
    shade_paragraph(paragraph, NOTE_BG)
    paragraph.paragraph_format.left_indent = Cm(0.3)
    paragraph.paragraph_format.right_indent = Cm(0.3)
    paragraph.paragraph_format.space_before = Pt(6)
    paragraph.paragraph_format.space_after = Pt(8)
    run = paragraph.add_run(value)
    set_run_language(run, language)
    set_run_font(run, "Arial")


def add_cover(document: Document, language: str) -> None:
    section = document.sections[0]
    section.top_margin = Cm(2.0)
    section.bottom_margin = Cm(2.0)
    section.left_margin = Cm(2.0)
    section.right_margin = Cm(2.0)

    logo = ROOT / "Logo.png"
    if logo.is_file():
        paragraph = document.add_paragraph()
        paragraph.alignment = WD_ALIGN_PARAGRAPH.CENTER
        run = paragraph.add_run()
        run.add_picture(str(logo), width=Inches(1.9))

    title = document.add_paragraph(style="FBY Title")
    title.alignment = WD_ALIGN_PARAGRAPH.CENTER
    run = title.add_run("FemboyEngine2D")
    set_run_language(run, "en-US")
    set_run_font(run, "Arial")

    subtitle = document.add_paragraph(style="FBY Subtitle")
    subtitle.alignment = WD_ALIGN_PARAGRAPH.CENTER
    run = subtitle.add_run(
        "Architecture Specification" if language == "en" else "Архитектурная спецификация"
    )
    set_run_language(run, "en-US" if language == "en" else "ru-RU")
    set_run_font(run, "Arial")

    meta = document.add_paragraph()
    meta.alignment = WD_ALIGN_PARAGRAPH.CENTER
    meta.paragraph_format.space_before = Pt(18)
    labels = (
        ("Release", f"Version {DOC_VERSION}", TODAY, "FBY-ARCH-0.1")
        if language == "en"
        else ("Релиз", f"Версия {DOC_VERSION}", TODAY, "FBY-ARCH-0.1")
    )
    for label in labels:
        run = meta.add_run(label + "\n")
        run.font.Size = Pt(10)
        run.font.color.rgb = MUTED
        set_run_language(run, "en-US" if language == "en" else "ru-RU")

    document.add_paragraph()
    note = document.add_paragraph(style="FBY Note")
    note.alignment = WD_ALIGN_PARAGRAPH.CENTER
    run = note.add_run(
        "Data-Oriented Design · Vulkan MDI · Compute LBS · EnTT · Box2D v3 · Rollback v3 · ImNodes · AOT"
    )
    run.font.Size = Pt(9)
    run.font.color.rgb = ACCENT
    set_run_language(run, "en-US")

    document.add_page_break()


def add_document_control(document: Document, language: str) -> None:
    heading = document.add_paragraph(style="FBY Heading 1")
    run = heading.add_run("Document control" if language == "en" else "Управление документом")
    set_run_language(run, "en-US" if language == "en" else "ru-RU")

    labels = [
        ("Document ID", "Идентификатор документа", "FBY-ARCH-0.1"),
        ("Version", "Версия", DOC_VERSION),
        ("Status", "Статус", DOC_STATUS),
        ("Date", "Дата", TODAY),
        ("Owner", "Владелец", "FemboyEngine Architecture Group"),
        ("Audience", "Аудитория", "Engine, editor, tools and network engineers"),
        ("Scope", "Область", "Runtime, RHI, ECS, physics, network, tooling, visual graphs"),
    ]
    table = document.add_table(rows=0, cols=2)
    table.style = "Table Grid"
    table.autofit = False
    table.columns[0].width = Cm(4.0)
    table.columns[1].width = Cm(13.4)
    for en, ru, value in labels:
        cells = table.add_row().cells
        set_cell_shading(cells[0], "F4F4F7")
        set_cell_margins(cells[0])
        set_cell_margins(cells[1])
        paragraph = cells[0].paragraphs[0]
        run = paragraph.add_run(en if language == "en" else ru)
        run.bold = True
        run.font.Size = Pt(9)
        set_run_language(run, "en-US" if language == "en" else "ru-RU")
        paragraph = cells[1].paragraphs[0]
        run = paragraph.add_run(value)
        run.font.Size = Pt(9)
        set_run_language(run, "en-US" if language == "en" else "ru-RU")
    document.add_paragraph()


def add_contents(document: Document, language: str) -> None:
    heading = document.add_paragraph(style="FBY Heading 1")
    run = heading.add_run("Contents" if language == "en" else "Содержание")
    set_run_language(run, "en-US" if language == "en" else "ru-RU")
    for section in SECTIONS + APPENDIX:
        paragraph = document.add_paragraph(style="FBY Body")
        paragraph.paragraph_format.space_after = Pt(2)
        run = paragraph.add_run(f"{section['number']}.  {section['title'][language]}")
        set_run_language(run, "en-US" if language == "en" else "ru-RU")
    document.add_paragraph()


def add_language_part(document: Document, language: str) -> None:
    heading = document.add_paragraph(style="FBY Heading 1")
    run = heading.add_run(
        "Architecture Specification" if language == "en" else "Архитектурная спецификация"
    )
    run.font.color.rgb = ACCENT
    set_run_language(run, "en-US" if language == "en" else "ru-RU")
    add_note(
        document,
        "This edition is generated from the same content model as the other language edition. Every "
        "run carries its language tag, so a translation tool or a proofing pass can process the document "
        "independently."
        if language == "en"
        else "Это издание генерируется из той же модели контента, что и издание на другом языке. Каждый "
        "run несёт языковой тег, поэтому инструмент перевода или проверки орфографии может обрабатывать "
        "документ независимо.",
        language,
    )
    add_contents(document, language)

    for section in SECTIONS + APPENDIX:
        heading = document.add_paragraph(style="FBY Heading 1")
        run = heading.add_run(f"{section['number']}. {section['title'][language]}")
        set_run_language(run, "en-US" if language == "en" else "ru-RU")
        for kind, block in section["blocks"]:
            if kind == "p":
                add_body(document, block[language], language)
            elif kind == "bullets":
                add_bullet(document, block[language], language)
            elif kind == "code":
                add_code(document, block[language], language)
            elif kind == "table":
                add_table(document, block, language)


def build_docx(path: pathlib.Path, language: str) -> None:
    document = Document()
    configure_styles(document)
    sections = document.sections
    sections[0].top_margin = Cm(2.0)
    sections[0].bottom_margin = Cm(2.0)
    sections[0].left_margin = Cm(2.0)
    sections[0].right_margin = Cm(2.0)
    add_header_footer(
        sections[0],
        "en-US" if language == "en" else "ru-RU",
        "FemboyEngine2D — Architecture Specification"
        if language == "en"
        else "FemboyEngine2D — Архитектурная спецификация",
        "",
    )

    core = document.core_properties
    core.title = (
        "FemboyEngine2D — Architecture Specification"
        if language == "en"
        else "FemboyEngine2D — Архитектурная спецификация"
    )
    core.subject = "Data-oriented engine architecture, Release"
    core.author = "FemboyEngine Architecture Group"
    core.keywords = "FemboyEngine, DOD, Vulkan, RHI, ECS, Box2D, rollback, ImNodes, AOT"
    core.comments = "Generated by Documentation/Tools/GenerateDocumentation.py"
    core.language = "en-US" if language == "en" else "ru-RU"

    add_cover(document, language)
    add_document_control(document, language)
    add_language_part(document, language)

    document.save(str(path))


def export_pdf(docx_path: pathlib.Path, pdf_path: pathlib.Path, node: str, lo_cli: str) -> bool:
    if not node or not pathlib.Path(node).is_file():
        print("pdf: node executable not found; use --node or FBY_NODE", file=sys.stderr)
        return False
    if not lo_cli or not pathlib.Path(lo_cli).is_file():
        print("pdf: LibreOffice Kit CLI not found; use --lo-cli or FBY_LO_CLI", file=sys.stderr)
        return False
    log_path = pathlib.Path(tempfile.gettempdir()) / "fby_architecture_pdf.log"
    pdf_path.unlink(missing_ok=True)
    with log_path.open("w", encoding="utf-8") as log:
        completed = subprocess.run(
            [node, lo_cli, "convert", "--input", str(docx_path), "--output", str(pdf_path)],
            stdout=log,
            stderr=log,
        )
    if completed.returncode != 0 or not pdf_path.is_file() or pdf_path.stat().st_size == 0:
        print(f"pdf: export failed; see {log_path}", file=sys.stderr)
        return False
    signature = pdf_path.read_bytes()[:5]
    if signature != b"%PDF-":
        print("pdf: output does not have a %PDF- signature", file=sys.stderr)
        return False
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--skip-pdf", action="store_true", help="write Markdown and DOCX only")
    parser.add_argument("--node", default=DEFAULT_NODE, help="node executable for LibreOffice Kit")
    parser.add_argument("--lo-cli", default=DEFAULT_LO_CLI, help="path to the LibreOffice Kit cli.js")
    args = parser.parse_args()

    docpaths.EnsureLayout()
    docpaths.Md("en", "FemboyEngineArchitecture_EN.md").write_text(
        render_markdown(SECTIONS + APPENDIX, "en", "English", True), encoding="utf-8"
    )
    docpaths.Md("ru", "FemboyEngineArchitecture_RU.md").write_text(
        render_markdown(SECTIONS + APPENDIX, "ru", "Русский", True), encoding="utf-8"
    )

    en_docx = docpaths.Docx("en", "FemboyEngineArchitecture_EN.docx")
    ru_docx = docpaths.Docx("ru", "FemboyEngineArchitecture_RU.docx")
    build_docx(en_docx, "en")
    build_docx(ru_docx, "ru")

    if args.skip_pdf:
        print(f"wrote {en_docx.name}, {ru_docx.name} and Markdown; PDF skipped")
        return 0

    en_pdf = docpaths.Pdf("en", "FemboyEngineArchitecture_EN.pdf")
    ru_pdf = docpaths.Pdf("ru", "FemboyEngineArchitecture_RU.pdf")
    ok_en = export_pdf(en_docx, en_pdf, args.node, args.lo_cli)
    ok_ru = export_pdf(ru_docx, ru_pdf, args.node, args.lo_cli)
    if not (ok_en and ok_ru):
        return 1
    docconvert.BuildDjvu(en_pdf, docpaths.Djvu("en", "FemboyEngineArchitecture_EN.djvu"))
    docconvert.BuildDjvu(ru_pdf, docpaths.Djvu("ru", "FemboyEngineArchitecture_RU.djvu"))
    print(f"wrote {en_pdf.name}, {ru_pdf.name} and Markdown")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
