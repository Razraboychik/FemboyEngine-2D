#!/usr/bin/env python3
"""Generate the FemboyEngine2D Application Guide (EN/RU, Markdown + PDF).

The guide is intentionally written as data, not as a one-off document: adding a
section means adding one title and one translation entry to SECTIONS. The same blocks render to
Markdown and to a python-docx document, and the PDF is exported through the
LibreOffice Kit, exactly like the architecture specification.

Usage:
  python Documentation/Tools/GenerateApplicationGuide.py
  python Documentation/Tools/GenerateApplicationGuide.py --skip-pdf
  python Documentation/Tools/GenerateApplicationGuide.py --node <node> --lo-cli <cli.js>

Outputs under Documentation/:
  md/en/FemboyEngineApplicationGuide_EN.md
  md/ru/FemboyEngineApplicationGuide_RU.md
  docx/en/FemboyEngineApplicationGuide_EN.docx
  docx/ru/FemboyEngineApplicationGuide_RU.docx
  pdf/en/FemboyEngineApplicationGuide_EN.pdf
  pdf/ru/FemboyEngineApplicationGuide_RU.pdf
"""

from __future__ import annotations

import argparse
import datetime as dt
import os
import pathlib
import subprocess
import sys
import tempfile

from docx import Document
from docx.enum.section import WD_SECTION
from docx.enum.text import WD_ALIGN_PARAGRAPH
from docx.oxml import OxmlElement
from docx.oxml.ns import qn
from docx.shared import Cm, Pt, RGBColor

sys.dont_write_bytecode = True
import DocPaths as docpaths
import DocumentConvert as docconvert
from DocVersion import VERSION as DOC_VERSION

ROOT = pathlib.Path(__file__).resolve().parents[2]
DOCUMENTATION = ROOT / "Documentation"
TODAY = dt.date.today().isoformat()

ACCENT = RGBColor(0xE8, 0x5D, 0x9E)
INK = RGBColor(0x1E, 0x1E, 0x24)
MUTED = RGBColor(0x6B, 0x6B, 0x76)
CODE_BG = "F4F4F7"

DEFAULT_NODE = os.environ.get("FBY_NODE", "node")
DEFAULT_LO_CLI = os.environ.get("FBY_LO_CLI", "")


def B(en, ru):
    return {"en": en, "ru": ru}


def code(text: str) -> dict:
    return {"en": text, "ru": text}


SECTIONS: list[dict] = [
    {
        "title": B("1. What this guide is for", "1. Для чего это руководство"),
        "blocks": [
            ("p", B(
                "FemboyEngine2D is a data-oriented 2D engine. This guide is the practical companion to the "
                "architecture specification: it starts from a playable 2D shooter, shows how entities are composed "
                "from independent data, then follows the same project through texture cooking, GPU batching, "
                "storefront integration, hybrid SDF physics, the weather pipeline and the encrypted distribution "
                "archive.",
                "FemboyEngine2D — двумерный движок, построенный вокруг data-oriented design. Это практическое "
                "дополнение к архитектурной спецификации: мы начинаем с играбельного 2D-шутера, показываем, как "
                "сущности собираются из независимых данных, а затем проводим тот же проект через кукинг текстур, "
                "GPU-батчинг, интеграцию магазинов, гибридную SDF-физику, погодный конвейер и шифрованный "
                "дистрибутивный архив.",
            )),
            ("p", B(
                "The running project is deliberately small. A player, three enemy archetypes, a boss with phases, "
                "bullets, pickups and a card board are enough to demonstrate every architectural rule. Every code "
                "snippet compiles against the headers shipped in Source/Engine; paths and type names are real.",
                "Проект намеренно небольшой. Игрок, три архетипа врагов, босс с фазами, пули, подбираемые предметы и "
                "карточная доска — этого достаточно, чтобы показать все архитектурные правила. Каждый фрагмент кода "
                "компилируется против заголовков из Source/Engine; пути и имена типов настоящие.",
            )),
            ("bullets", B(
                [
                    "Audience: gameplay programmers, technical designers and tools engineers.",
                    "Assumed knowledge: C++20 basics, an ECS at the level of EnTT, and one graphics API.",
                    "Not covered: Vulkan synchronization details, console certification and legal storefront setup.",
                ],
                [
                    "Аудитория: геймплей-программисты, технические дизайнеры и tools-инженеры.",
                    "Требуемые знания: основы C++20, ECS на уровне EnTT и один графический API.",
                    "Не рассматривается: детали синхронизации Vulkan, сертификация консолей и юридическое подключение "
                    "магазинов.",
                ],
            )),
            ("note", B(
                "The engine is validated by 26 CTest executables. The new suites cover the SDK profile and "
                "crypto, the storefront bridge, SDF physics, the render/weather stack and the data-driven "
                "prefab/DPI/theme path. Run ctest --test-dir build --output-on-failure before trusting a local change.",
                "Движок проверяется 26 исполняемыми CTest-тестами. Новые наборы покрывают SDK-профиль и "
                "криптографию, мост магазинов, SDF-физику, стек рендера/погоды и путь data-driven "
                "префабов/DPI/темы. Перед тем как доверять локальному изменению, запустите ctest --test-dir build --output-on-failure.",
            )),
        ],
    },
    {
        "title": B("2. Setup and the reproducible toolchain", "2. Установка и воспроизводимый тулчейн"),
        "blocks": [
            ("p", B(
                "The one-click setup scripts install the pinned dependencies and materialise the exact sources under "
                "third_party/. CMake prefers those local checkouts, so the first configure after setup does not need "
                "network access. Versions are fixed by the scripts and written to third_party/dependencies.lock.",
                "Скрипты установки в один клик ставят зафиксированные зависимости и раскладывают точные исходники в "
                "third_party/. CMake предпочитает эти локальные копии, поэтому первый configure после установки не "
                "требует сети. Версии фиксируются скриптами и записываются в third_party/dependencies.lock.",
            )),
            ("code", code(r"""# Windows 10/11 (run from a Visual Studio developer prompt)
SetupScripts/Setup.bat

# Ubuntu / Debian
./SetupScripts/Setup.sh

# macOS (double-click in Finder also works)
./SetupScripts/Setup.command""")),
            ("p", B(
                "Pinned versions: SDL3 release-3.4.18, Vulkan-Headers v1.4.366, glslang 15.3.0, miniaudio 0.11.23, "
                "Box2D v3.0.0 and LZMA SDK 25.01. Setup installs system packages when possible; use --skip-system "
                "(or -SkipSystem on Windows) on a CI image that already has CMake, Ninja, Clang/GCC and the Vulkan "
                "loader.",
                "Зафиксированные версии: SDL3 release-3.4.18, Vulkan-Headers v1.4.366, glslang 15.3.0, miniaudio "
                "0.11.23, Box2D v3.0.0 и LZMA SDK 25.01. Setup по возможности ставит системные пакеты; используйте "
                "--skip-system (или -SkipSystem в Windows) на CI-образе, где уже есть CMake, Ninja, Clang/GCC и "
                "Vulkan loader.",
            )),
            ("code", code(r"""cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_C_COMPILER=clang-cl \
  -DCMAKE_CXX_COMPILER=clang-cl \
  -DFBY_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure""")),
            ("p", B(
                "The compiler is part of the contract: C++26 is required, Clang 19+ is the reference toolchain and "
                "GCC 15+ is supported. The build defines FBY_HAS_MDSPAN, FBY_HAS_FORMAT and FBY_HAS_JTHREAD by probing "
                "the compiler, not by version number. LZMA SDK presence is also probed; without it the .femarc writer "
                "uses the deterministic FbyLZ codec and reports it in the cook log.",
                "Компилятор — часть контракта: требуется C++26, эталонный тулчейн — Clang 19+, поддерживается GCC 15+. "
                "Сборка определяет FBY_HAS_MDSPAN, FBY_HAS_FORMAT и FBY_HAS_JTHREAD пробой компилятора, а не по "
                "номеру версии. Наличие LZMA SDK тоже проверяется; без него писатель .femarc использует "
                "детерминированный кодек FbyLZ и сообщает об этом в логе кукера.",
            )),
            ("note", B(
                "Setup never modifies a source file. It is safe to delete third_party/<name>-src and re-run with "
                "--force; the lock file is regenerated and the CMake cache does not embed a machine-specific path.",
                "Setup никогда не изменяет исходники. Можно удалить third_party/<name>-src и повторить запуск с "
                "--force; lock-файл перегенерируется, а CMake-кэш не содержит машинно-зависимого пути.",
            )),
        ],
    },
    {
        "title": B("3. How to think in ECS", "3. Как мыслить в ECS"),
        "blocks": [
            ("p", B(
                "An ECS is not a different syntax for a class hierarchy. It is a change of the central question. In "
                "OOP the question is 'what is this object?'. In ECS the questions are 'what data does this entity "
                "own?' and 'which system transforms that data this tick?'. The answer to the first question is a set "
                "of component types; the answer to the second is a system that iterates every entity with that set.",
                "ECS — это не другой синтаксис для иерархии классов. Это смена главного вопроса. В ООП вопрос "
                "звучит как «что это за объект?». В ECS вопросы другие: «какими данными владеет сущность?» и «какая "
                "система преобразует эти данные на этом тике?». Ответ на первый — набор типов компонентов; ответ на "
                "второй — система, обходящая все сущности с этим набором.",
            )),
            ("h2", B("3.1 Rules that keep the model fast", "3.1 Правила, которые сохраняют скорость модели")),
            ("bullets", B(
                [
                    "A component is trivially copyable and standard layout: no string, no vector, no virtual method. "
                    "Transform2D, HealthComponent and WeaponComponent are values, not objects.",
                    "Behaviour lives in a system. A component never has an Update() method; a system never owns "
                    "gameplay state that belongs to an entity.",
                    "Identity is data. A player is not a Player class; it is an entity with InputComponent, "
                    "HealthComponent and WeaponComponent. Anyone with those three can be controlled.",
                    "Variation is data. An enemy archetype is a row in a definition table, and a boss phase is a "
                    "component value, not a derived class.",
                    "The hot path allocates nothing. Archetype capacity is reserved at load; per-frame work uses "
                    "views, spans and the frame arena.",
                ],
                [
                    "Компонент trivially copyable и standard layout: без string, vector и виртуальных методов. "
                    "Transform2D, HealthComponent и WeaponComponent — значения, а не объекты.",
                    "Поведение живёт в системе. У компонента нет метода Update(); система не хранит геймплейное "
                    "состояние, принадлежащее сущности.",
                    "Идентичность — это данные. Игрок не класс Player, а сущность с InputComponent, HealthComponent "
                    "и WeaponComponent. Управлять можно любой сущностью с этими тремя компонентами.",
                    "Вариативность — это данные. Архетип врага — строка таблицы определений, фаза босса — значение "
                    "компонента, а не производный класс.",
                    "Горячий путь не выделяет память. Ёмкость архетипов резервируется при загрузке; кадровая работа "
                    "использует view, span и frame arena.",
                ],
            )),
            ("h2", B("3.2 The component vocabulary", "3.2 Словарь компонентов")),
            ("p", B(
                "Start with a small vocabulary and resist adding a component until two systems genuinely need to "
                "share the data. The shooter uses the following independent components.",
                "Начните с небольшого словаря и не добавляйте компонент, пока двум системам действительно не "
                "понадобится совместно использовать данные. Шутер использует следующие независимые компоненты.",
            )),
            ("code", code(r"""// Game/Components.hpp - trivially copyable, no behaviour.
struct HealthComponent
{
    float current = 100.0f;
    float maximum = 100.0f;
    float invulnerableSeconds = 0.0f;
    std::uint32_t team = 0;
};

struct WeaponComponent
{
    std::uint64_t projectileArchetype = 0;
    float cooldownSeconds = 0.0f;
    float fireInterval = 0.12f;
    Vector2D muzzleOffset{18.0f, 0.0f};
    std::uint32_t pellets = 1;
};

struct InputComponent
{
    Vector2D move{};
    Vector2D aim{1.0f, 0.0f};
    std::uint32_t buttons = 0; // bit 0 fire, bit 1 dash, bit 2 interact
};

struct AIBehaviorComponent
{
    std::uint32_t behavior = 0; // 0 chase, 1 keep distance, 2 patrol, 3 boss
    Entity target = entt::null;
    float thinkSeconds = 0.0f;
    float preferredRange = 220.0f;
};

struct ProjectileComponent
{
    Entity owner = entt::null;
    Vector2D velocity{};
    float damage = 10.0f;
    float ttl = 4.0f;
    std::uint32_t pierce = 0;
};""")),
            ("p", B(
                "Notice what is absent: no Enemy base class, no PlayerController, no BossHealthBar. Those are "
                "behaviours and presentation assembled from the same data. The compiler enforces the rule with "
                "static_assert(std::is_trivially_copyable_v<T>) in the component header.",
                "Обратите внимание, чего здесь нет: базового класса Enemy, PlayerController, BossHealthBar. Это "
                "поведение и представление, собранные из тех же данных. Компилятор обеспечивает правило через "
                "static_assert(std::is_trivially_copyable_v<T>) в заголовке компонентов.",
            )),
        ],
    },
    {
        "title": B("4. Building a 2D shooter by composition", "4. Сборка 2D-шутера через композицию"),
        "blocks": [
            ("p", B(
                "The player is four components and a transform. Spawning it is a function, not a constructor. The "
                "function is also the prefab loader: a scene file produces the same component set through the same "
                "code path.",
                "Игрок — это четыре компонента и трансформ. Его спавн — функция, а не конструктор. Эта же функция "
                "является загрузчиком префаба: сценический файл создаёт тот же набор компонентов через тот же код.",
            )),
            ("code", code(r"""Entity SpawnPlayer(ecs::Registry& registry, Vector2D spawn)
{
    const Entity entity = registry.create();
    registry.emplace<Transform2D>(entity, Transform2D{spawn, {1.0f, 1.0f}, 0.0f});
    registry.emplace<Velocity>(entity);
    registry.emplace<HealthComponent>(entity, HealthComponent{100.0f, 100.0f, 0.0f, 0});
    registry.emplace<InputComponent>(entity, InputComponent{});
    registry.emplace<WeaponComponent>(entity, WeaponComponent{0x51A7u, 0.0f, 0.12f, {18.0f, 0.0f}, 1});
    registry.emplace<GameplayState>(entity, GameplayState{});
    return entity;
}

// Enemy archetypes are data. A boss is the same composition with different values.
struct EnemyArchetype
{
    std::uint64_t id;
    float health;
    float speed;
    std::uint32_t behavior;
    float preferredRange;
    std::uint32_t weaponArchetype;
    Color32 tint;
};

Entity SpawnEnemy(ecs::Registry& registry, const EnemyArchetype& archetype, Vector2D spawn)
{
    const Entity entity = registry.create();
    registry.emplace<Transform2D>(entity, Transform2D{spawn, {1.0f, 1.0f}, 0.0f});
    registry.emplace<Velocity>(entity);
    registry.emplace<HealthComponent>(entity, HealthComponent{archetype.health, archetype.health, 0.0f, 1});
    AIBehaviorComponent behaviorComponent{};
    behaviorComponent.behavior = archetype.behavior;
    behaviorComponent.preferredRange = archetype.preferredRange;
    registry.emplace<AIBehaviorComponent>(entity, behaviorComponent);
    Sprite2D sprite{};
    sprite.Size = {32.0f, 32.0f};
    sprite.color = archetype.tint;
    registry.emplace<Sprite2D>(entity, sprite);
    return entity;
}""")),
            ("p", B(
                "Mobs and bosses share one spawn path. A 'Boss' is an archetype with more health, a phase table and "
                "a BossPhaseComponent; it is not a separate C++ type. Adding a new enemy is a data row and, when its "
                "behaviour is genuinely new, one branch in AISystem.",
                "Мобы и боссы используют один путь спавна. «Босс» — это архетип с большим здоровьем, таблицей фаз и "
                "компонентом BossPhaseComponent, а не отдельный C++ тип. Новый враг — это строка данных и, если "
                "поведение действительно новое, одна ветка в AISystem.",
            )),
            ("h2", B("4.1 Systems, in order", "4.1 Системы по порядку")),
            ("bullets", B(
                [
                    "InputSystem (PreUpdate): read devices into InputComponent. It never moves anything.",
                    "AISystem (FixedUpdate): write desired velocity and aim into the same InputComponent that a "
                    "player would write. This is the key trick: one movement system handles players and enemies.",
                    "MovementSystem (FixedUpdate): integrate Velocity into Transform2D; clamp to level bounds.",
                    "WeaponSystem (FixedUpdate): cooldown, spawn projectile intents through the CommandBuffer.",
                    "ProjectileSystem (FixedUpdate): integrate projectiles, TTL, broad-phase candidate query.",
                    "DamageSystem (FixedUpdate, after ProjectileSystem): apply hits, invulnerability and death flags.",
                    "CameraStackSystem (PostUpdate): follow, shake, tilt and post-FX policy.",
                    "Extract/Render (Render): read-only pass that emits RenderItems and MeshRenderItems.",
                ],
                [
                    "InputSystem (PreUpdate): читает устройства в InputComponent. Ничего не двигает.",
                    "AISystem (FixedUpdate): пишет желаемую скорость и прицел в тот же InputComponent, в который "
                    "писал бы игрок. Это ключевой приём: одна система движения обслуживает игроков и врагов.",
                    "MovementSystem (FixedUpdate): интегрирует Velocity в Transform2D; ограничивает границами уровня.",
                    "WeaponSystem (FixedUpdate): перезарядка, спавн интентов снарядов через CommandBuffer.",
                    "ProjectileSystem (FixedUpdate): интегрирует снаряды, TTL, запрос кандидатов broad-phase.",
                    "DamageSystem (FixedUpdate, после ProjectileSystem): применяет попадания, неуязвимость и флаг "
                    "смерти.",
                    "CameraStackSystem (PostUpdate): следование, тряска, наклон и политика пост-эффектов.",
                    "Extract/Render (Render): read-only проход, выдающий RenderItem и MeshRenderItem.",
                ],
            )),
            ("code", code(r"""void AISystem(ecs::SystemContext& context)
{
    auto view = context.registry.view<AIBehaviorComponent, InputComponent, Transform2D, HealthComponent>();
    for (const Entity entity : view)
    {
        const AIBehaviorComponent& behaviorComponent = view.get<AIBehaviorComponent>(entity);
        const Transform2D& transform = view.get<Transform2D>(entity);
        const HealthComponent& health = view.get<HealthComponent>(entity);
        if (health.current <= 0.0f)
        {
            continue;
        }
        InputComponent& input = view.get<InputComponent>(entity);
        if (behaviorComponent.target == entt::null || !context.registry.valid(behaviorComponent.target))
        {
            continue;
        }
        const Transform2D& target = context.registry.get<Transform2D>(behaviorComponent.target);
        const Vector2D delta = target.Position - transform.Position;
        const float distance = std::sqrt(delta.LengthSquared());
        if (distance > behaviorComponent.preferredRange)
        {
            input.move = delta / std::max(distance, 0.001f);
        }
        else if (distance < behaviorComponent.preferredRange * 0.8f)
        {
            input.move = delta / std::max(distance, 0.001f) * -1.0f;
        }
        else
        {
            input.move = Vector2D{};
        }
        input.aim = delta / std::max(distance, 0.001f);
        input.buttons = distance < behaviorComponent.preferredRange * 1.2f ? 1u : 0u;
    }
}""")),
            ("p", B(
                "The system is deliberately boring. There is no state machine object and no per-enemy allocation; "
                "the archetype row supplies the numbers, and the same nine lines cover a chaser, a turret and a "
                "ranged mob. A boss with phases adds a second small system that changes preferredRange and behavior "
                "when its health crosses a threshold.",
                "Система намеренно скучная. Здесь нет объекта-автомата и аллокации на врага; строка архетипа задаёт "
                "числа, и те же девять строк покрывают преследователя, турель и дальнего моба. Босс с фазами "
                "добавляет вторую маленькую систему, которая меняет preferredRange и поведение при переходе "
                "здоровья через порог.",
            )),
            ("h2", B("4.2 Bosses without inheritance", "4.2 Боссы без наследования")),
            ("code", code(r"""struct BossPhaseComponent
{
    std::uint32_t currentPhase = 0;
    std::uint32_t phaseCount = 0;
    float healthThresholds[4] = {0.66f, 0.33f, 0.12f, 0.0f};
    std::uint32_t behaviors[4] = {0u, 1u, 2u, 3u};
    float preferredRanges[4] = {220.0f, 160.0f, 90.0f, 40.0f};
};

void BossPhaseSystem(ecs::SystemContext& context)
{
    auto view = context.registry.view<BossPhaseComponent, HealthComponent, AIBehaviorComponent>();
    for (const Entity entity : view)
    {
        BossPhaseComponent& phase = view.get<BossPhaseComponent>(entity);
        const HealthComponent& health = view.get<HealthComponent>(entity);
        AIBehaviorComponent& behaviorComponent = view.get<AIBehaviorComponent>(entity);
        const float ratio = health.current / std::max(health.maximum, 0.001f);
        while (phase.currentPhase + 1 < phase.phaseCount &&
               ratio <= phase.healthThresholds[phase.currentPhase])
        {
            ++phase.currentPhase;
            behaviorComponent.behavior = phase.behaviors[phase.currentPhase];
            behaviorComponent.preferredRange = phase.preferredRanges[phase.currentPhase];
        }
    }
}""")),
            ("p", B(
                "This is the entire boss hierarchy. A 'fire boss' and a 'charge boss' differ by component values and, "
                "at most, by a spawn table. Behaviour that cannot be expressed as data belongs in a new small "
                "system; it almost never justifies a class with a vtable.",
                "Это вся иерархия боссов. «Огненный босс» и «босс-рывок» отличаются значениями компонентов и, в "
                "крайнем случае, таблицей спавна. Поведение, которое нельзя выразить данными, заслуживает новой "
                "маленькой системы; оно почти никогда не оправдывает класс с vtable.",
            )),
            ("h2", B("4.3 Spawning without a frame hitch", "4.3 Спавн без фриза кадра")),
            ("p", B(
                "Structural changes during iteration are the classic ECS bug. The shooter routes every spawn and "
                "despawn through the CommandBuffer and applies it at a phase boundary. The binary CommandLog records "
                "the same commands for rollback, so a replay sees the identical entity ids.",
                "Структурные изменения во время обхода — классическая ошибка ECS. Шутер направляет любой спавн и "
                "удаление через CommandBuffer и применяет его на границе фазы. Бинарный CommandLog записывает те же "
                "команды для роллбэка, поэтому реплей видит идентичные id сущностей.",
            )),
            ("code", code(r"""if (context.commands != nullptr)
{
    context.commands->Spawn(projectileEntity);
    context.commands->AddComponent(projectileEntity, ProjectileComponent{owner, direction * 900.0f, 12.0f, 4.0f, 0});
}
// At the end of FixedUpdate the host calls commands->Apply(registry).""")),
        ],
    },
    {
        "title": B("5. Scheduling, determinism and tests", "5. Планирование, детерминизм и тесты"),
        "blocks": [
            ("p", B(
                "SystemGraph is the production scheduler. Dependencies are explicit; Add() stores a phase, a "
                "function pointer and after/before bitmasks. The graph is built once, not every frame. A cycle or a "
                "cross-phase dependency fails Build() and is reported instead of silently reordering gameplay.",
                "SystemGraph — production-планировщик. Зависимости явные; Add() хранит фазу, указатель на функцию и "
                "битмаски after/before. Граф строится один раз, а не каждый кадр. Цикл или межфазная зависимость "
                "приводит к ошибке Build() и диагностируется, а не молча меняет порядок геймплея.",
            )),
            ("code", code(r"""ecs::SystemGraph graph;
graph.Add(ecs::Phase::PreUpdate, "Input", &InputSystem);
graph.Add(ecs::Phase::FixedUpdate, "AI", &AISystem);
graph.Add(ecs::Phase::FixedUpdate, "Movement", &MovementSystem);
graph.Add(ecs::Phase::FixedUpdate, "Weapon", &WeaponSystem);
graph.Add(ecs::Phase::FixedUpdate, "Projectile", &ProjectileSystem);
graph.Add(ecs::Phase::FixedUpdate, "Damage", &DamageSystem);
graph.Add(ecs::Phase::PostUpdate, "Camera", &CameraTick);
if (!graph.Build())
{
    LogError("schedule", "system graph has a cycle or a cross-phase dependency");
}""")),
            ("p", B(
                "Determinism is a property of the data, not of a hope. FixedUpdate receives a constant dt, the RNG is "
                "Pcg32 seeded per run, and structural commands are logged. Snapshot v4 captures the ECS plus the "
                "world extension (joints, cloth, fluid) and restores it with a b2 body remap. The replay test "
                "resimulates the same input log on a fresh registry and compares the checksum.",
                "Детерминизм — свойство данных, а не надежда. FixedUpdate получает постоянный dt, RNG — Pcg32 с "
                "сидом на запуск, структурные команды логируются. Snapshot v4 захватывает ECS плюс расширение мира "
                "(суставы, ткань, жидкость) и восстанавливает его с ремапом b2-тел. Тест реплея пересимулирует тот "
                "же лог ввода на чистом реестре и сравнивает контрольную сумму.",
            )),
            ("bullets", B(
                [
                    "Headless first: a gameplay system takes SystemContext and must not require a window or a GPU.",
                    "Test the data, not the pixels: assert component values and command-log bytes.",
                    "One test executable per concern; no test framework and no dynamic registration.",
                    "Run the whole suite with ctest; a single failure prints file and line and returns non-zero.",
                ],
                [
                    "Сначала headless: геймплейная система принимает SystemContext и не должна требовать окно или GPU.",
                    "Тестируйте данные, а не пиксели: проверяйте значения компонентов и байты командного лога.",
                    "Один исполняемый тест на задачу; без фреймворка и динамической регистрации.",
                    "Запускайте весь набор через ctest; одно падение печатает файл и строку и возвращает ненулевой код.",
                ],
            )),
        ],
    },
    {
        "title": B("6. Memory and draw-call compression", "6. Сжатие памяти и вызовов отрисовки"),
        "blocks": [
            ("p", B(
                "The second half of an ECS project is not gameplay; it is what reaches the GPU. Two tools do most of "
                "the work: cooked block-compressed textures and indirect mesh batching. Both are configured per "
                "project and both are verifiable from FrameStats.",
                "Вторая половина ECS-проекта — не геймплей, а то, что доходит до GPU. Основную работу делают два "
                "инструмента: запечённые блочно-сжатые текстуры и indirect-батчинг мешей. Оба настраиваются на "
                "проект и оба проверяемы через FrameStats.",
            )),
            ("h2", B("6.1 Texture cooking: BC7 on desktop, ASTC on mobile", "6.1 Кукинг текстур: BC7 на десктопе, ASTC на мобильных")),
            ("bullets", B(
                [
                    "BC7 keeps a real alpha channel and is the desktop default. BC7 sRGB is selected for colour "
                    "sprites; a normal map or a mask uses the linear (UNORM) variant.",
                    "ASTC 4x4 is the quality mobile target; 6x6 is the size target. Pick it in Project Settings -> "
                    "Cooker -> Texture format.",
                    "The cooker generates the full mip chain once, at import. The runtime uploads the blocks "
                    "verbatim; there is no CPU decode and no runtime re-compression.",
                    "A texture array is the batching unit. Keep sprites of one material in one layer set; a layer "
                    "switch is a new descriptor, not a new draw call.",
                    "Never ship a JPEG: the cooker rejects it by extension and by magic. A lossy source makes a "
                    "shipped texture differ from what the artist approved.",
                ],
                [
                    "BC7 сохраняет настоящий альфа-канал и является десктопным дефолтом. BC7 sRGB выбирается для "
                    "цветных спрайтов; карта нормалей или маска использует линейный (UNORM) вариант.",
                    "ASTC 4x4 — мобильный таргет качества; 6x6 — таргет размера. Выбирается в Project Settings -> "
                    "Cooker -> Texture format.",
                    "Кукер один раз при импорте строит полную цепочку мипов. Рантайм загружает блоки как есть; нет "
                    "CPU-декодирования и повторного сжатия в рантайме.",
                    "Единица батчинга — texture array. Держите спрайты одного материала в одном наборе слоёв; "
                    "переключение слоя — это новый дескриптор, а не новый draw call.",
                    "Никогда не поставляйте JPEG: кукер отвергает его по расширению и по magic. Lossy-источник "
                    "делает поставленную текстуру отличной от утверждённой художником.",
                ],
            )),
            ("code", code(r"""# project.femproj - the same values the panel writes
[render]
texture_format = "bc7-srgb"
max_texture_size = 2048

[build]
optimization = "O2"
lto = true
strip_debug_info = true
unity_build = true
native_arch = false""")),
            ("h2", B("6.2 Multi-draw indirect batching", "6.2 Multi-draw indirect батчинг")),
            ("p", B(
                "The sprite path is already one instanced call per batch. The mesh path is where indirect drawing "
                "matters: static geometry is uploaded once into a device-local arena and referenced by MeshHandle; "
                "deformable geometry is streamed into the transient ring; every draw is a compact MeshDrawData "
                "record, and the backend records them through one drawIndexedIndirect call per material layer. The "
                "CPU never binds a vertex buffer per mesh.",
                "Спрайтовый путь уже является одним instanced-вызовом на батч. Indirect-отрисовка важна на пути "
                "мешей: статическая геометрия один раз загружается в device-local арену и адресуется через "
                "MeshHandle; деформируемая геометрия стримится в transient ring; каждый draw — компактная запись "
                "MeshDrawData, а бэкенд проигрывает их одним drawIndexedIndirect на слой материала. CPU не биндит "
                "vertex buffer на каждый меш.",
            )),
            ("bullets", B(
                [
                    "One material per layer: a new layer is a new indirect batch but not a pipeline switch.",
                    "Static props use CreateMesh and a MeshHandle; a per-frame upload is wasted PCIe bandwidth.",
                    "Skinned meshes are skinned by one compute dispatch into the skinned vertex buffer, then drawn "
                    "through the same indirect path.",
                    "Watch FrameStats: items, batches, drawCalls, meshIndirectCalls. A batch count that grows with "
                    "entity count means the authoring data is fragmented, not that the GPU is slow.",
                    "The render graph orders passes; it does not reorder draws inside a pass. Keep the draw list "
                    "sorted by pipeline/layer once per frame.",
                ],
                [
                    "Один материал на слой: новый слой — новый indirect-батч, но не переключение пайплайна.",
                    "Статические объекты используют CreateMesh и MeshHandle; кадровая загрузка — потерянная "
                    "пропускная способность PCIe.",
                    "Скининговые меши скинятся одним compute-диспатчем в skinned vertex buffer, затем рисуются тем же "
                    "indirect-путём.",
                    "Следите за FrameStats: items, batches, drawCalls, meshIndirectCalls. Рост числа батчей с числом "
                    "сущностей означает фрагментацию данных, а не медленный GPU.",
                    "Render graph упорядочивает проходы; он не переупорядочивает draw внутри прохода. Держите "
                    "список отрисовки отсортированным по pipeline/слою один раз за кадр.",
                ],
            )),
            ("note", B(
                "The procedural Pink UI path removes texture memory entirely: circles, rounded boxes and cards are "
                "evaluated from a 64-byte instance record in pink_sdf.vert/frag. A card board of 200 slots is two "
                "instanced draws (cards and shadows), not 200 texture switches.",
                "Процедурный путь Pink UI полностью убирает текстурную память: круги, скруглённые прямоугольники и "
                "карты вычисляются из 64-байтной записи экземпляра в pink_sdf.vert/frag. Доска из 200 слотов — это "
                "два instanced-вызова (карты и тени), а не 200 переключений текстуры.",
            )),
        ],
    },
    {
        "title": B("7. Storefront integration: Steam and Epic", "7. Интеграция магазинов: Steam и Epic"),
        "blocks": [
            ("p", B(
                "PlatformService is a bridge, not a wrapper around a wrapper. The engine owns a single C-ABI function "
                "table loaded from a provider shared library (fby_platform_steam or fby_platform_egs). Gameplay "
                "enqueues commands; Pump() is the only place a vendor SDK is called, and it runs after the frame has "
                "been recorded.",
                "PlatformService — это мост, а не обёртка над обёрткой. Движок владеет одной C-ABI таблицей "
                "функций, загруженной из провайдерской разделяемой библиотеки (fby_platform_steam или "
                "fby_platform_egs). Геймплей ставит команды в очередь; Pump() — единственное место вызова "
                "вендорского SDK, и он работает после записи кадра.",
            )),
            ("code", code(r"""PlatformService platform;
std::string error;
if (!platform.Load(FbyPlatformKind_Steam, "fby_platform_steam.dll", &error))
{
    LogWarn("platform", "Steam provider unavailable: {}", error);
}
FbyPlatformInitDesc desc{};
desc.kind = FbyPlatformKind_Steam;
desc.applicationId = "480";
if (platform.Initialize(desc, &error))
{
    PlatformServiceCallbacks callbacks{};
    callbacks.onAchievement = &OnAchievement;
    platform.SetGameCallbacks(callbacks);
}
// Once per PreUpdate, after gameplay has enqueued this frame's intents:
platform.Pump();""")),
            ("h2", B("7.1 Achievements as deterministic commands", "7.1 Достижения как детерминированные команды")),
            ("p", B(
                "SteamAchievementSystem reads AchievementRequest components, sorts them by achievement id (so ECS "
                "iteration order cannot change the log), pushes one command per request and appends a "
                "PlatformCommandLog record. The log is binary, checksummed and replayable; it answers 'why did this "
                "unlock?' without trusting a vendor dashboard. EgsStoreSystem does the same for cloud saves, with a "
                "fixed staging arena and a bounded write queue.",
                "SteamAchievementSystem читает компоненты AchievementRequest, сортирует их по id достижения (чтобы "
                "порядок обхода ECS не менял лог), ставит одну команду на запрос и добавляет запись в "
                "PlatformCommandLog. Лог бинарный, с контрольной суммой и воспроизводимый; он отвечает на вопрос "
                "«почему это разблокировалось?», не доверяя дашборду магазина. EgsStoreSystem делает то же для "
                "облачных сохранений, с фиксированной staging-ареной и ограниченной очередью записи.",
            )),
            ("code", code(r"""SteamAchievementSystem achievements;
achievements.Initialize(platform, commandLog);
EgsStoreSystem store;
store.Initialize(platform, commandLog);

ecs::SystemGraph graph;
SteamAchievementSystem::Register(graph, achievements);
EgsStoreSystem::Register(graph, store);
graph.Build();

// Gameplay only writes data; the systems do the rest.
AchievementRequest request{};
request.achievementId = 0xA17E;
request.Flags = AchievementPending;
std::strcpy(request.apiName, "ACH_FIRST_BLOOD");
registry.emplace<AchievementRequest>(player, request);

// Cloud save: stage once, then mark the component dirty.
store.StageWrite(0x51u, "slot1.sav", std::span<const std::byte>(saveBytes));
CloudSaveComponent save{};
save.saveId = 0x51u;
save.Flags = CloudSaveDirty;
std::strcpy(save.fileName, "slot1.sav");
registry.emplace<CloudSaveComponent>(player, save);""")),
            ("p", B(
                "A frame never blocks. If the provider is absent, QueueAchievement returns false, the request is "
                "marked failed and the game keeps running. The C ABI is versioned; a provider built for another ABI "
                "is rejected before a call is made. Steam is resolved through the flat API at runtime, so the game "
                "does not link steam_api; EGS uses EOS_Platform_Create with an options blob supplied by the game "
                "and can be built with the official headers when FBY_EOS_SDK_INCLUDE_DIR is set.",
                "Кадр никогда не блокируется. Если провайдера нет, QueueAchievement возвращает false, запрос "
                "помечается failed, игра продолжает работать. C ABI версионирован; провайдер, собранный под другой "
                "ABI, отвергается до вызова. Steam резолвится через flat API в рантайме, поэтому игра не линкует "
                "steam_api; EGS использует EOS_Platform_Create с блобом опций от игры и может собираться с "
                "официальными заголовками при заданном FBY_EOS_SDK_INCLUDE_DIR.",
            )),
        ],
    },
    {
        "title": B("8. Hybrid SDF physics, rays and post effects", "8. Гибридная SDF-физика, лучи и пост-эффекты"),
        "blocks": [
            ("p", B(
                "Box2D resolves body-vs-body contacts. Terrain is not a body: it is a distance field the renderer "
                "already samples. SdfCollisionSystem closes the gap once per fixed step. It reads a non-owning "
                "SdfField over std::mdspan, bilinearly samples it at rotated collider corners and the centre, takes "
                "the gradient as the normal, applies a mass-scaled push impulse and a positional correction, then "
                "writes the corrected state back to RigidBody2D.",
                "Box2D решает контакты тело-тело. Ландшафт — не тело: это поле расстояний, которое рендерер уже "
                "сэмплирует. SdfCollisionSystem закрывает разрыв раз за фиксированный шаг. Он читает невладеющий "
                "SdfField над std::mdspan, билинейно сэмплирует его в повёрнутых углах коллайдера и центре, берёт "
                "градиент как нормаль, применяет импульс выталкивания с учётом массы и позиционную коррекцию, затем "
                "записывает исправленное состояние обратно в RigidBody2D.",
            )),
            ("code", code(r"""// A height field is filled by the level loader once.
std::vector<float> samples(width * height);
for (std::uint32_t y = 0; y < height; ++y)
{
    for (std::uint32_t x = 0; x < width; ++x)
    {
        samples[y * width + x] = DistanceToTerrain(x, y); // negative inside
    }
}
SdfField field{};
field.samples = samples.data();
field.width = width;
field.height = height;
field.origin = levelOrigin;
field.cellSize = {2.0f, 2.0f};

SdfCollisionSystem sdf;
sdf.SetField(field);
sdf.Register(graph); // FixedUpdate, after the Box2D step
// Each Step produces up to 1024 SdfContact records for audio/VFX/decal reuse.
for (const SdfContact& contact : sdf.Contacts())
{
    SpawnImpactVfx(contact.point, contact.normal, contact.impulse);
}""")),
            ("h2", B("8.1 SDF shadows and camera-owned post FX", "8.1 SDF-тени и пост-эффекты, принадлежащие камере")),
            ("p", B(
                "SdfShadowPass raymarches the same field in a compute shader and produces a soft light mask; the "
                "composite multiplies the scene by it. PostFxPass adds a bloom prefilter, a separable blur and one "
                "composite for CRT curvature, scanlines, vignette, chromatic aberration and grain. The settings live "
                "in PostFxSettings on the top CameraStack entry, so a terminal close-up and a gameplay camera can "
                "have different looks without rebuilding the graph.",
                "SdfShadowPass марширует лучами по тому же полю в compute-шейдере и создаёт мягкую маску света; "
                "композит умножает на неё сцену. PostFxPass добавляет bloom-prefilter, разделяемое размытие и один "
                "композит для CRT-кривизны, scanline, виньетки, хроматической аберрации и зерна. Настройки живут в "
                "PostFxSettings на верхней записи CameraStack, поэтому крупный план терминала и геймплейная камера "
                "могут выглядеть по-разному без перестройки графа.",
            )),
            ("code", code(r"""CameraStackSystem cameras;
CameraStackDesc camera{};
cameras.Push(registry, camera);

PostFxSettings terminal{};
terminal.Flags = PostFxCrt | PostFxVignette | PostFxBloom | PostFxChromaticAberration;
terminal.crtCurvature = 0.12f;
terminal.scanlineStrength = 0.18f;
cameras.SetPostFx(terminal);

SdfShadowPass shadows;
SdfShadowDesc shadowDesc{};
shadowDesc.fieldWidth = width;
shadowDesc.fieldHeight = height;
shadowDesc.fieldCellSize = {2.0f, 2.0f};
shadows.Initialize(shadowDesc);
shadows.Build(graph);

PostFxPass postFx;
postFx.Build(graph, cameras);""")),
            ("p", B(
                "For UI, the SDF path removes texture memory entirely. pink_sdf.frag evaluates circles, rounded boxes, "
                "capsules and rings analytically; anti-aliasing comes from fwidth. SlotComponent and "
                "CardBoardComponent lay out a card board without a scene, and BuildCardBoardPrimitives packs each "
                "slot into two instances (card and shadow).",
                "Для UI SDF-путь полностью убирает текстурную память. pink_sdf.frag аналитически вычисляет круги, "
                "скруглённые прямоугольники, капсулы и кольца; сглаживание берётся из fwidth. SlotComponent и "
                "CardBoardComponent раскладывают карточную доску без сцены, а BuildCardBoardPrimitives упаковывает "
                "каждый слот в два экземпляра (карта и тень).",
            )),
        ],
    },
    {
        "title": B("9. The weather pipeline", "9. Погодный конвейер"),
        "blocks": [
            ("p", B(
                "Weather is one component: WeatherContextComponent stores atmosphere density, viscosity, gravity "
                "scale, cloud coverage, fog density, rain/snow/fire intensity and the global wind. WeatherSystem "
                "advances it in PreUpdate, derives a wind vector from fBm noise, writes the resolved vector back and "
                "pushes it into ClothSystem. Every downstream consumer consumes that one resolved vector, so cloth, "
                "foliage and rain never disagree.",
                "Погода — один компонент: WeatherContextComponent хранит плотность атмосферы, вязкость, множитель "
                "гравитации, облачность, плотность тумана, интенсивность дождя/снега/огня и глобальный ветер. "
                "WeatherSystem обновляет его в PreUpdate, выводит вектор ветра из fBm-шума, записывает результат "
                "обратно и передаёт в ClothSystem. Все последующие потребители используют один и тот же вектор, "
                "поэтому ткань, растительность и дождь не расходятся.",
            )),
            ("code", code(r"""WeatherSystem weather;
weather.Initialize(WeatherDesc{0x51ED270Bu, Vector2D{1.0f, 0.0f}, 14.0f, 1.0f});
WeatherContextComponent context{};
context.atmosphereDensity = 0.82f;   // thinner air on an alien map
context.gravityScale = 0.65f;
context.windSpeed = 22.0f;
context.turbulence = 0.55f;
context.rainIntensity = 0.9f;
context.cloudCoverage = 0.7f;
context.fogDensity = 0.025f;
registry.emplace<WeatherContextComponent>(weatherEntity, context);

weather.Update(systemContext); // PreUpdate
// WindField() is now the shared block consumed by cloth, plants and GPU weather.""" )),
            ("h2", B("9.1 GPU rain with SDF splashes", "9.1 GPU-дождь со всплесками по SDF")),
            ("p", B(
                "WeatherPreconciliatedSystem owns no particle array on the CPU. It prepares WeatherGpuConstants, "
                "computes a dispatch size from intensity and records four graph passes. weather_rain.comp owns the "
                "particle SSBO, integrates wind and gravity, samples the global terrain SDF and appends a splash "
                "record with atomicAdd when a drop crosses the surface. A second compute ages splashes, and two "
                "graphics passes render lines and ring SDF sprites. Stats.cpuParticleReads stays zero by "
                "construction.",
                "WeatherPreconciliatedSystem не владеет массивом частиц на CPU. Он готовит WeatherGpuConstants, "
                "считает размер диспатча из интенсивности и записывает четыре прохода графа. weather_rain.comp "
                "владеет SSBO частиц, интегрирует ветер и гравитацию, сэмплирует глобальную SDF-сетку ландшафта и "
                "добавляет запись всплеска через atomicAdd, когда капля пересекает поверхность. Второй compute "
                "старит всплески, два graphics-прохода рисуют линии и кольца. Stats.cpuParticleReads по построению "
                "остаётся нулём.",
            )),
            ("code", code(r"""WeatherPreconciliatedDesc rain{};
rain.particleCapacity = 262144;
rain.splashCapacity = 65536;
rain.areaWidth = 4096.0f;
rain.areaHeight = 2304.0f;

WeatherPreconciliatedSystem gpuRain;
gpuRain.Initialize(rain);
gpuRain.SetSdfField(field);
gpuRain.BuildRenderGraph(graph);

WeatherAtmospherePass atmosphere;
atmosphere.Build(graph, weather); // clouds, cloud shadows, fog, fire, composite

// Per frame, once:
gpuRain.Update(systemContext); // constants + dispatch size, zero particle reads""")),
            ("bullets", B(
                [
                    "Clouds: two-octave simplex advected by the resolved wind, shaped by coverage and a Worley edge.",
                    "Cloud shadows: the same field sampled with a drifted Poisson kernel; shadows move with wind.",
                    "Fog: 24-step vertical raymarch through exponential height fog with value-noise detail.",
                    "Fire/smoke: simplex flame plus slow smoke; intensity comes from the weather component.",
                    "All four are compute passes writing transient textures; no heavy source textures are loaded.",
                    "The Metal kernel weather_rain.metal mirrors the Vulkan compute contract for Apple targets.",
                ],
                [
                    "Облака: двухоктавный simplex, адвектируемый разрешённым ветром, с формой из coverage и краем "
                    "Worley.",
                    "Тени облаков: то же поле, сэмплируемое сдвинутым ядром Пуассона; тени движутся с ветром.",
                    "Туман: 24-шаговый вертикальный raymarch через экспоненциальный высотный туман с value-noise.",
                    "Огонь/дым: simplex-пламя плюс медленный дым; интенсивность берётся из компонента погоды.",
                    "Все четыре — compute-проходы, пишущие transient-текстуры; тяжёлые исходные текстуры не грузятся.",
                    "Metal-ядро weather_rain.metal повторяет контракт Vulkan compute для Apple-таргетов.",
                ],
            )),
        ],
    },
    {
        "title": B("10. Distribution: .femarc, LZMA and AEAD", "10. Дистрибуция: .femarc, LZMA и AEAD"),
        "blocks": [
            ("p", B(
                ".fempak is the mmap-friendly runtime archive: no compression, deterministic bytes, sorted paths. "
                ".femarc is the distribution envelope: per-entry LZMA (or the in-tree FbyLZ fallback) plus "
                "AES-256-GCM or ChaCha20-Poly1305. The AEAD tag is verified before a single entry offset is trusted. "
                "The reader works on caller-provided buffers, so the launcher decrypts into a mapped file without a "
                "heap allocation.",
                ".fempak — mmap-дружественный рантайм-архив: без сжатия, детерминированные байты, сортированные "
                "пути. .femarc — дистрибутивная обёртка: LZMA на запись (или встроенный fallback FbyLZ) плюс "
                "AES-256-GCM или ChaCha20-Poly1305. Тег AEAD проверяется до того, как будет прочитан хоть один "
                "смещённый указатель. Читатель работает на буферах вызывающего, поэтому лаунчер расшифровывает в "
                "mapped-файл без аллокаций в куче.",
            )),
            ("code", code(r"""fby_fempak secure --in build/release/assets.fempak --out archives/game.femarc \
  --entry game/assets.fempak --codec lzma --cipher aes \
  --key-env FBY_ARCHIVE_KEY""")),
            ("p", B(
                "The key is supplied through an environment variable or a 64-hex argument; the nonce is generated "
                "from SecureRng when omitted. Build is idempotent for a fixed key/nonce/entry set: the table is "
                "sorted, the string table is stable and the compressed stream is deterministic. Tampering with one "
                "byte makes Open() return AuthenticationFailed, not a half-parsed archive.",
                "Ключ передаётся через переменную окружения или 64-hex аргумент; nonce генерируется из SecureRng, "
                "если не задан. Build идемпотентен для фиксированного набора ключ/nonce/записи: таблица "
                "сортирована, строковая таблица стабильна, сжатый поток детерминирован. Изменение одного байта "
                "даёт Open() = AuthenticationFailed, а не полуразобранный архив.",
            )),
            ("note", B(
                "LZMA SDK is optional. When third_party/lzma-sdk/LzmaLib.h exists, CMake compiles the 7-Zip encoder "
                "and decoder; otherwise the build log says 'FbyLZ fallback is active' and the archive remains valid "
                "but larger. A shipped build should pin and audit one codec, not switch per run.",
                "LZMA SDK опционален. Если third_party/lzma-sdk/LzmaLib.h существует, CMake собирает энкодер и "
                "декодер 7-Zip; иначе лог сборки сообщает 'FbyLZ fallback is active', и архив остаётся валидным, но "
                "больше. Поставка должна фиксировать и проверять один кодек, а не переключать его каждый запуск.",
            )),
        ],
    },
    {
        "title": B("11. Performance playbook and CI", "11. Плейбук производительности и CI"),
        "blocks": [
            ("bullets", B(
                [
                    "Profile fixed-step time separately from frame time. Physics, cloth and AI belong to the first; "
                    "post FX and weather to the second.",
                    "Keep fixed-step work allocation-free. Reserve archetype capacity at scene load; use the frame "
                    "arena for transient spans.",
                    "Watch three counters before changing code: FrameStats.drawCalls, meshIndirectCalls and "
                    "transient capacity. A rising drawCalls with a flat item count means fragmented batches.",
                    "Use the O2 profile for rollback and CI. Ofast can change float comparison results and breaks "
                    "replay determinism; keep it for a visual-only pass.",
                    "LTO and unity builds are release-only. They cut ship time but multiply link time and make "
                    "incremental cooking unpleasant.",
                    "Never let a vendor callback run inside gameplay. Enqueue, return, Pump after the frame.",
                ],
                [
                    "Профилируйте fixed-step и кадровое время отдельно. Физика, ткань и AI относятся к первому; "
                    "пост-эффекты и погода — ко второму.",
                    "Держите fixed-step работу без аллокаций. Резервируйте ёмкость архетипов при загрузке сцены; "
                    "для временных span используйте frame arena.",
                    "Следите за тремя счётчиками до изменения кода: FrameStats.drawCalls, meshIndirectCalls и "
                    "transient capacity. Рост drawCalls при неизменном числе элементов означает фрагментацию батчей.",
                    "Для роллбэка и CI используйте профиль O2. Ofast может изменить результаты сравнения float и "
                    "сломать детерминизм реплея; оставьте его для визуального прохода.",
                    "LTO и unity build — только для релиза. Они сокращают время поставки, но увеличивают время "
                    "линковки и делают инкрементальный кукинг неприятным.",
                    "Никогда не допускайте вендорский колбэк внутрь геймплея. Ставьте в очередь, возвращайтесь, "
                    "вызывайте Pump после кадра.",
                ],
            )),
            ("h2", B("11.1 Release checklist", "11.1 Чек-лист релиза")),
            ("p", B(
                "Run the whole CTest slate, then the cook and the secure step on a clean machine. Verify that the "
                "game starts without Steam/EOS (provider absent is a warning, not a crash), that a tampered .femarc "
                "is rejected, and that the SDF physics suite still passes with the exact shipping profile. Record "
                "the profile and the dependency lock in the build metadata.",
                "Запустите весь набор CTest, затем кукинг и secure-шаг на чистой машине. Проверьте, что игра "
                "запускается без Steam/EOS (отсутствие провайдера — предупреждение, а не падение), что изменённый "
                ".femarc отвергается и что набор SDF-физики проходит с точным релизным профилем. Запишите профиль и "
                "lock зависимостей в метаданные сборки.",
            )),
            ("code", code(r"""ctest --test-dir build --output-on-failure
fby_fempak cook --root assets --out build/release/assets.fempak
fby_fempak secure --in build/release/assets.fempak --out archives/game.femarc \
  --codec lzma --cipher chacha --key-env FBY_ARCHIVE_KEY
# ReleaseCooker then stages archives/game.femarc and the plugin into
# stores/steam, stores/epic and stores/gog with a store.txt manifest.
# Launch the game with FBY_PLATFORM_LIB pointing at fby_platform_steam.dll
# or omit it and verify the NotAvailable path.""")),
        ],
    },
    {
        "title": B("12. Materials: author, cook and bind to a mesh", "12. Материалы: автор, кук и привязка к мешу"),
        "blocks": [
            ("p", B(
                "A material is a small graph that replaces the fragment shader of a mesh: base colour, opacity and "
                "emissive, plus math nodes, atlas samples and the 2.5D helpers. The graph is authored in the "
                "editor, cooked at build time and loaded by the game. Nothing in the game compiles GLSL.",
                "Материал — небольшой граф, заменяющий фрагментный шейдер меша: базовый цвет, прозрачность и "
                "эмиссия, плюс математические узлы, выборки из атласа и 2.5D-помощники. Граф создаётся в "
                "редакторе, кукается на этапе сборки и загружается игрой. В игре GLSL не компилируется нигде.")),
            ("h2", B("The three artefacts", "Три артефакта")),
            ("table", B(
                [
                    ["File", "Produced by", "Loaded by"],
                    ["`<name>.fgraph`", "The editor panel (Save .fgraph) or `EncodeMaterialGraph`", "`fby_matc`, and the editor panel (Load .fgraph)"],
                    ["`<name>.femmat`", "`fby_matc` in the build step", "`Render::DecodeCookedMaterial` in the game"],
                    ["`MaterialPipelineCache`", "The game, one per device", "The render extract via `MeshRenderItem`"],
                ],
                [
                    ["Файл", "Кем создаётся", "Кем загружается"],
                    ["`<name>.fgraph`", "Панель редактора (Save .fgraph) или `EncodeMaterialGraph`", "`fby_matc` и панель редактора (Load .fgraph)"],
                    ["`<name>.femmat`", "`fby_matc` на шаге сборки", "`Render::DecodeCookedMaterial` в игре"],
                    ["`MaterialPipelineCache`", "Игра, один на устройство", "Извлечение кадра через `MeshRenderItem`"],
                ],
            )),
            ("p", B(
                "The build step runs `fby_matc <graph>.fgraph -o <graph>.femmat` (add `--sprite` for the quad.vert "
                "contract). The tool validates the graph, "
                "exits non-zero on failure and writes nothing, so a failed cook cannot ship a stale payload.",
                "Шаг сборки запускает `fby_matc <graph>.fgraph -o <graph>.femmat` (добавьте `--sprite` для контракта "
                "quad.vert). Инструмент валидирует граф, "
                "возвращает ненулевой код при ошибке и ничего не пишет, поэтому неудачный кук не оставит "
                "устаревший payload.",
            )),
            ("h2", B("Binding it in the game", "Привязка в игре")),
            ("code", B(
                """// Once, when the device comes up.
Render::MaterialPipelineCache Materials;

// Decode the payload, then borrow a pipeline reference.
Render::CookedMaterialView Tinted;
if (!Render::DecodeCookedMaterial(InBytes, &Tinted))
{
    return false;
}
const Rhi::ShaderPipelineHandle TintedPipeline = Materials.Acquire(gpu, Tinted);

// The id means use this material; the default keeps the engine shader.
MeshRenderItem Item;
Item.materialPipeline = TintedPipeline.IsValid() ? TintedPipeline.id : kDefaultMaterialPipeline;

// Shutdown: release while the device is still alive.
Materials.Release(TintedPipeline);
Materials.Shutdown(gpu);""",
                """// Один раз, при старте устройства.
Render::MaterialPipelineCache Materials;

// Декодируйте payload и возьмите ссылку на пайплайн.
Render::CookedMaterialView Tinted;
if (!Render::DecodeCookedMaterial(InBytes, &Tinted))
{
    return false;
}
const Rhi::ShaderPipelineHandle TintedPipeline = Materials.Acquire(gpu, Tinted);

// Id означает использовать этот материал; значение по умолчанию — шейдер движка.
MeshRenderItem Item;
Item.materialPipeline = TintedPipeline.IsValid() ? TintedPipeline.id : kDefaultMaterialPipeline;

// Остановка: отпускайте, пока устройство ещё живо.
Materials.Release(TintedPipeline);
Materials.Shutdown(gpu);""",
            )),
            ("bullets", B(
                [
                    "Do not cook per frame or per level load: the `.femmat` is a build artefact, like a compressed texture.",
                    "One cache per device; `Acquire` is safe per item and only bumps the count for the same hash.",
                    "Check `SupportsRuntimeShaderPipelines()` once at startup instead of probing for an invalid handle.",
                    "Sprites use a separate cook: `fby_matc <graph> --sprite` builds the quad.vert contract, "
                    "`DrawOrder::MaterialBucket` sorts the sprite into a material run, and "
                    "`SetMaterialPipelines` maps that bucket to the cached pipeline id.",
                ],
                [
                    "Не кукайте на кадр или на загрузку уровня: `.femmat` — артефакт сборки, как сжатая текстура.",
                    "Один кэш на устройство; `Acquire` безопасен на элемент и лишь увеличивает счётчик для того же хеша.",
                    "Проверьте `SupportsRuntimeShaderPipelines()` при старте, а не угадывайте по невалидному хендлу.",
                    "Спрайты кукаются отдельно: `fby_matc <graph> --sprite` собирает контракт quad.vert, "
                    "`DrawOrder::MaterialBucket` сортирует спрайт в материальный прогон, а "
                    "`SetMaterialPipelines` сопоставляет бакет с id кэшированного пайплайна.",
                ],
            )),
            ("note", B(
                "Reference: API reference section 8 lists every type and payload field; Complete Reference "
                "chapter 38 walks the cook, the cache and the frame; ADR 0002 records why the runtime does not "
                "link glslang.",
                "Справка: раздел 8 API reference перечисляет все типы и поля payload; глава 38 Complete "
                "Reference проходит кук, кэш и кадр; ADR 0002 фиксирует, почему рантайм не линкует glslang.")),
        ],
    },
    {
        "title": B("13. Appendix: component and API map", "13. Приложение: карта компонентов и API"),
        "blocks": [
            ("p", B(
                "Use this table as a starting checklist. A component that does not fit one row is probably two "
                "components, and a behaviour that needs a new row is a new system, not a new class.",
                "Используйте таблицу как стартовый чек-лист. Компонент, не помещающийся в одну строку, вероятно, "
                "является двумя компонентами, а поведение, требующее новой строки, — это новая система, а не новый "
                "класс.",
            )),
            ("table", B(
                [
                    ["Component", "Owner", "Written by", "Consumed by"],
                    ["Transform2D", "every visible entity", "Movement, Physics", "Render, Camera"],
                    ["Velocity", "moving entity", "AI, Movement", "Physics, Movement"],
                    ["HealthComponent", "damageable", "DamageSystem", "Damage, UI, BossPhase"],
                    ["WeaponComponent", "armed entity", "Loadout", "WeaponSystem"],
                    ["InputComponent", "player or AI", "InputSystem / AISystem", "Movement, Weapon"],
                    ["AIBehaviorComponent", "AI entity", "Spawner, BossPhase", "AISystem"],
                    ["ProjectileComponent", "bullet", "WeaponSystem", "Projectile, Damage"],
                    ["AchievementRequest", "player", "Gameplay trigger", "SteamAchievementSystem"],
                    ["CloudSaveComponent", "account", "Save system", "EgsStoreSystem"],
                    ["WeatherContextComponent", "level", "Designer / weather curve", "WeatherSystem, render"],
                    ["SlotComponent", "card", "Deck logic", "CardBoardSystem, PinkSdf"],
                ],
                [
                    ["Компонент", "Владелец", "Пишет", "Читает"],
                    ["Transform2D", "любая видимая сущность", "Movement, Physics", "Render, Camera"],
                    ["Velocity", "движущаяся сущность", "AI, Movement", "Physics, Movement"],
                    ["HealthComponent", "получающая урон", "DamageSystem", "Damage, UI, BossPhase"],
                    ["WeaponComponent", "вооружённая сущность", "Loadout", "WeaponSystem"],
                    ["InputComponent", "игрок или AI", "InputSystem / AISystem", "Movement, Weapon"],
                    ["AIBehaviorComponent", "AI-сущность", "Spawner, BossPhase", "AISystem"],
                    ["ProjectileComponent", "пуля", "WeaponSystem", "Projectile, Damage"],
                    ["AchievementRequest", "игрок", "Геймплейный триггер", "SteamAchievementSystem"],
                    ["CloudSaveComponent", "аккаунт", "Система сохранений", "EgsStoreSystem"],
                    ["WeatherContextComponent", "уровень", "Дизайнер / кривая погоды", "WeatherSystem, рендер"],
                    ["SlotComponent", "карта", "Логика колоды", "CardBoardSystem, PinkSdf"],
                ],
            )),
            ("h2", B("11.1 Where to look in the repository", "11.1 Где смотреть в репозитории")),
            ("bullets", B(
                [
                    "Source/Engine/ECS: Components.hpp, Schedule.hpp, SystemGraph.hpp, CommandLog.hpp.",
                    "Source/Engine/Physics: SdfCollision.hpp/.cpp, Cloth.hpp/.cpp.",
                    "Source/Engine/Platform: PlatformService, PlatformSystems; Source/Plugins/PlatformService: C ABI and providers.",
                    "Source/Engine/RHI: RenderGraph, RenderPasses, PostProcess, CameraStack, shaders/.",
                    "Source/Engine/Weather: Weather, WeatherPreconciliated.",
                    "Source/Engine/UI: CardBoard, PinkSdf, PinkUI.",
                    "Source/Engine/Assets: FemArchiver, FemCrypto, FempakWriter.",
                    "Source/Tests: the executable specification; start with SdkCryptoTest, PlatformBridgeTest, "
                    "SdfPhysicsTest and RenderWeatherTest.",
                    "Documentation/Tools/GenerateApplicationGuide.py: this document's source.",
                ],
                [
                    "Source/Engine/ECS: Components.hpp, Schedule.hpp, SystemGraph.hpp, CommandLog.hpp.",
                    "Source/Engine/Physics: SdfCollision.hpp/.cpp, Cloth.hpp/.cpp.",
                    "Source/Engine/Platform: PlatformService, PlatformSystems; Source/Plugins/PlatformService: C ABI и провайдеры.",
                    "Source/Engine/RHI: RenderGraph, RenderPasses, PostProcess, CameraStack, Shaders/.",
                    "Source/Engine/Weather: Weather, WeatherPreconciliated.",
                    "Source/Engine/UI: CardBoard, PinkSdf, PinkUI.",
                    "Source/Engine/Assets: FemArchiver, FemCrypto, FempakWriter.",
                    "Source/Tests: исполняемая спецификация; начните с SdkCryptoTest, PlatformBridgeTest, "
                    "SdfPhysicsTest и RenderWeatherTest.",
                    "Documentation/Tools/GenerateApplicationGuide.py: исходник этого документа.",
                ],
            )),
            ("p", B(
                "The guide is generated from the same Python file that writes both languages. Add a section to "
                "SECTIONS, run the generator, and the Markdown and PDF stay in lockstep.",
                "Руководство генерируется из того же Python-файла, который пишет оба языка. Добавьте секцию в "
                "SECTIONS, запустите генератор — и Markdown с PDF останутся синхронными.",
            )),
        ],
    },
]


def get(block_value, language: str) -> str:
    return block_value[language]


def render_markdown(title: str, language: str, subtitle: str) -> str:
    lines = [f"# {title}", ""]
    lines.append(f"*{subtitle} — {TODAY}*")
    lines.append("")
    for section in SECTIONS:
        lines.append(f"## {get(section['title'], language)}")
        lines.append("")
        for kind, value in section["blocks"]:
            if kind == "p":
                lines.append(get(value, language))
                lines.append("")
            elif kind == "h2":
                lines.append(f"### {get(value, language)}")
                lines.append("")
            elif kind == "code":
                lines.append("```cpp")
                lines.append(get(value, language))
                lines.append("```")
                lines.append("")
            elif kind == "bullets":
                for item in get(value, language):
                    lines.append(f"- {item}")
                lines.append("")
            elif kind == "note":
                lines.append(f"> **Note / Примечание.** {get(value, language)}")
                lines.append("")
            elif kind == "table":
                rows = get(value, language)
                if rows:
                    lines.append("| " + " | ".join(rows[0]) + " |")
                    lines.append("| " + " | ".join("---" for _ in rows[0]) + " |")
                    for row in rows[1:]:
                        lines.append("| " + " | ".join(row) + " |")
                    lines.append("")
        lines.append("---")
        lines.append("")
    return "\n".join(lines)


def set_cell_shading(cell, color: str) -> None:
    properties = cell._tc.get_or_add_tcPr()
    shading = OxmlElement("w:shd")
    shading.set(qn("w:fill"), color)
    properties.append(shading)


def configure_styles(document: Document) -> None:
    styles = document.styles
    normal = styles["Normal"]
    normal.font.name = "Arial"
    normal.font.Size = Pt(10.5)
    normal.font.color.rgb = INK
    normal.paragraph_format.space_after = Pt(6)
    normal.paragraph_format.line_spacing = 1.15
    for name, size in (("Heading 1", 22), ("Heading 2", 15), ("Heading 3", 12)):
        style = styles[name]
        style.font.name = "Arial"
        style.font.Size = Pt(size)
        style.font.color.rgb = ACCENT if name == "Heading 1" else INK
        style.paragraph_format.space_before = Pt(14)
        style.paragraph_format.space_after = Pt(6)
    code_style = styles.add_style("FbyCode", 1)
    code_style.font.name = "Consolas"
    code_style.font.Size = Pt(8.5)
    code_style.font.color.rgb = RGBColor(0x20, 0x20, 0x28)
    code_style.paragraph_format.space_before = Pt(4)
    code_style.paragraph_format.space_after = Pt(8)
    code_style.paragraph_format.line_spacing = 1.0


def add_code(document: Document, text: str) -> None:
    paragraph = document.add_paragraph(style="FbyCode")
    properties = paragraph._p.get_or_add_pPr()
    shading = OxmlElement("w:shd")
    shading.set(qn("w:fill"), CODE_BG)
    properties.append(shading)
    run = paragraph.add_run(text)
    run.font.name = "Consolas"


def add_header_footer(section, label: str) -> None:
    header = section.header.paragraphs[0]
    header.text = label
    header.alignment = WD_ALIGN_PARAGRAPH.RIGHT
    for run in header.runs:
        run.font.Size = Pt(8)
        run.font.color.rgb = MUTED
    footer = section.footer.paragraphs[0]
    footer.text = "FemboyEngine2D Application Guide"
    footer.alignment = WD_ALIGN_PARAGRAPH.CENTER
    for run in footer.runs:
        run.font.Size = Pt(8)
        run.font.color.rgb = MUTED


def build_docx(path: pathlib.Path, language: str, title: str, subtitle: str) -> None:
    document = Document()
    configure_styles(document)
    section = document.sections[0]
    section.top_margin = Cm(2.0)
    section.bottom_margin = Cm(2.0)
    section.left_margin = Cm(2.0)
    section.right_margin = Cm(2.0)
    add_header_footer(section, "FemboyEngine2D Application Guide")
    core = document.core_properties
    core.title = title
    core.subject = subtitle
    core.author = "FemboyEngine Developer Documentation Group"
    core.keywords = "FemboyEngine, ECS, DOD, texture cooking, BC7, ASTC, indirect draw, SDF, weather"
    core.language = "en-US" if language == "en" else "ru-RU"

    cover = document.add_paragraph()
    cover.alignment = WD_ALIGN_PARAGRAPH.CENTER
    cover.paragraph_format.space_before = Pt(140)
    run = cover.add_run("FemboyEngine2D")
    run.font.name = "Arial"
    run.font.Size = Pt(34)
    run.font.bold = True
    run.font.color.rgb = ACCENT
    title_paragraph = document.add_paragraph()
    title_paragraph.alignment = WD_ALIGN_PARAGRAPH.CENTER
    run = title_paragraph.add_run("Application Guide\n" + subtitle)
    run.font.name = "Arial"
    run.font.Size = Pt(18)
    run.font.color.rgb = INK
    date_paragraph = document.add_paragraph()
    date_paragraph.alignment = WD_ALIGN_PARAGRAPH.CENTER
    date_run = date_paragraph.add_run(f"{DOC_VERSION}  ·  {TODAY}")
    date_run.font.color.rgb = MUTED
    document.add_page_break()

    for section_data in SECTIONS:
        document.add_heading(get(section_data["title"], language), level=1)
        for kind, value in section_data["blocks"]:
            if kind == "p":
                document.add_paragraph(get(value, language))
            elif kind == "h2":
                document.add_heading(get(value, language), level=2)
            elif kind == "code":
                add_code(document, get(value, language))
            elif kind == "bullets":
                for item in get(value, language):
                    document.add_paragraph(item, style="List Bullet")
            elif kind == "note":
                paragraph = document.add_paragraph()
                paragraph.paragraph_format.left_indent = Cm(0.4)
                run = paragraph.add_run("Note. ")
                run.bold = True
                run.font.color.rgb = ACCENT
                paragraph.add_run(get(value, language))
            elif kind == "table":
                rows = get(value, language)
                if not rows:
                    continue
                table = document.add_table(rows=len(rows), cols=len(rows[0]))
                table.style = "Table Grid"
                for row_index, row in enumerate(rows):
                    for column_index, cell_text in enumerate(row):
                        cell = table.cell(row_index, column_index)
                        cell.text = cell_text
                        if row_index == 0:
                            set_cell_shading(cell, "E85D9E")
                            for paragraph in cell.paragraphs:
                                for run in paragraph.runs:
                                    run.font.bold = True
                                    run.font.color.rgb = RGBColor(0xFF, 0xFF, 0xFF)
        document.add_page_break()
    document.save(str(path))


def export_pdf(docx_path: pathlib.Path, pdf_path: pathlib.Path, node: str, lo_cli: str) -> bool:
    if not node or not pathlib.Path(node).is_file():
        print("pdf: node executable not found; pass --node or set FBY_NODE", file=sys.stderr)
        return False
    if not lo_cli or not pathlib.Path(lo_cli).is_file():
        print("pdf: LibreOffice Kit CLI not found; pass --lo-cli or set FBY_LO_CLI", file=sys.stderr)
        return False
    pdf_path.unlink(missing_ok=True)
    log_path = pathlib.Path(tempfile.gettempdir()) / "fby_application_guide_pdf.log"
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


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--skip-pdf", action="store_true", help="write Markdown only")
    parser.add_argument("--node", default=DEFAULT_NODE, help="node executable for LibreOffice Kit")
    parser.add_argument("--lo-cli", default=DEFAULT_LO_CLI, help="LibreOffice Kit cli.js")
    args = parser.parse_args()

    docpaths.EnsureLayout()
    en_title = "FemboyEngine2D Application Guide"
    ru_title = "FemboyEngine2D — Практическое руководство"
    en_subtitle = "ECS in practice, production optimisation and the distribution SDK"
    ru_subtitle = "Практика ECS, оптимизация производства и SDK дистрибуции"

    en_md = render_markdown(en_title, "en", en_subtitle)
    ru_md = render_markdown(ru_title, "ru", ru_subtitle)
    docpaths.Md("en", "FemboyEngineApplicationGuide_EN.md").write_text(en_md, encoding="utf-8")
    docpaths.Md("ru", "FemboyEngineApplicationGuide_RU.md").write_text(ru_md, encoding="utf-8")

    en_docx = docpaths.Docx("en", "FemboyEngineApplicationGuide_EN.docx")
    ru_docx = docpaths.Docx("ru", "FemboyEngineApplicationGuide_RU.docx")
    build_docx(en_docx, "en", en_title, en_subtitle)
    build_docx(ru_docx, "ru", ru_title, ru_subtitle)

    if args.skip_pdf:
        print("wrote Markdown and DOCX; PDF skipped")
        return 0
    en_pdf = docpaths.Pdf("en", "FemboyEngineApplicationGuide_EN.pdf")
    ru_pdf = docpaths.Pdf("ru", "FemboyEngineApplicationGuide_RU.pdf")
    ok_en = export_pdf(en_docx, en_pdf, args.node, args.lo_cli)
    ok_ru = export_pdf(ru_docx, ru_pdf, args.node, args.lo_cli)
    if not (ok_en and ok_ru):
        return 1
    docconvert.BuildDjvu(en_pdf, docpaths.Djvu("en", "FemboyEngineApplicationGuide_EN.djvu"))
    docconvert.BuildDjvu(ru_pdf, docpaths.Djvu("ru", "FemboyEngineApplicationGuide_RU.djvu"))
    print("wrote Application Guide: EN/RU Markdown, DOCX and PDF")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
