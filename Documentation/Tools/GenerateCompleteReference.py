#!/usr/bin/env python3
"""Generate the FemboyEngine2D Complete Reference Manual.

This is the extended edition of the generated API reference. It imports the
English and Russian content model and the Markdown/DOCX/PDF renderers from
GenerateAPIReference.py, appends the integration chapters that are not part of a
pure API surface, and writes a separate document set:

  FemboyEngineCompleteReference_EN.md / .docx / .pdf
  FemboyEngineCompleteReference_RU.md / .docx / .pdf

The extra chapters are:

  19. GOG Galaxy provider, shim ABI and the PostUpdate systems
  20. Font pipeline: fby_fontc, .fbyfont and the stable-buffer contract
  21. Cloth render path: the hand-off that keeps cloth out of skinning
  22. Atmosphere: weather context, fBm wind and the GPU rain/splash dispatch
  23. ECS by composition: a 2D shooter without inheritance
  24. Multi-Draw Indirect and Texture Cooking in production
  25. Repository layout, CMake targets and the style gate
  26. Threading contract: RHI, the OS main loop and TTS
  27. The 32-bit DrawBatch pipeline: end of the 65,536-texture ceiling
  28. Atomic VFS font hot reload and the strict MountFonts contract
  29. TTS: the main-thread contract and localized voice fallback
  30. State-driven gameplay: FemAnimGraph and the StateTree AI systems
  31. Advanced AAA DOD pipelines: virtual texturing, change masks, relevance grids and audio buses
  32. Production distribution: encryption, LZMA, key baking and the store release checklist
  33. SystemGraph parallel scheduling and the blend-cycle contract
  34. Box2D rollback teardown and the PIE animation hand-off
  35. Global audit: release-blocking findings and the gate
  36. Profiling cache misses and vectorisation toward 8000+ FPS

Outputs are written below Documentation/md, Documentation/docx and
Documentation/pdf, with `en` and `ru` language subdirectories.

Requirements: python-docx for DOCX/PDF. PDF export uses LibreOffice Kit through
Node (same flags as the base generator).
"""

from __future__ import annotations

import argparse
import pathlib
import sys

TOOLS = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS))

# Importing the base module must not leave a __pycache__ inside Documentation/:
# the style gate treats a Python cache directory as committed junk.
sys.dont_write_bytecode = True
import GenerateAPIReference as base  # noqa: E402
import DocPaths as docpaths
import DocumentConvert as docconvert  # noqa: E402


B = base.B
DOCUMENTATION = base.DOCUMENTATION
TODAY = base.TODAY


EXTRA_SECTIONS: list[dict] = [
    {
        "number": "20",
        "title": B("GOG Galaxy: provider, shim ABI and PostUpdate systems",
                   "GOG Galaxy: провайдер, ABI шима и системы PostUpdate"),
        "blocks": [
            {
                "type": "p",
                "text": B(
                    "GOG Galaxy ships a C++ interface library, not a flat C API. The engine "
                    "therefore keeps the same architecture it uses for Steam and Epic: a "
                    "provider shared object speaks only PlatformAPI.h, and a thin Galaxy shim "
                    "(built by the game against the exact SDK revision it ships) exposes a flat "
                    "C surface that the provider resolves with dlopen/LoadLibrary at runtime. "
                    "No Galaxy header enters the engine, and an SDK upgrade replaces the shim "
                    "without recompiling the game.",
                    "GOG Galaxy поставляется как библиотека C++-интерфейсов, а не плоский C API. "
                    "Поэтому движок сохраняет ту же архитектуру, что для Steam и Epic: "
                    "разделяемая библиотека провайдера говорит только через PlatformAPI.h, а "
                    "тонкий шим Galaxy (собирается игрой под конкретную версию SDK) "
                    "экспортирует плоский C-интерфейс, который провайдер резолвит через "
                    "dlopen/LoadLibrary во время работы. Ни один заголовок Galaxy не попадает в "
                    "движок, а обновление SDK заменяет шим без пересборки игры.",
                ),
            },
            {
                "type": "table",
                "headers": [B("Flat shim export", "Экспорт шима"), B("Maps to", "Соответствие")],
                "rows": [
                    [B("fby_gog_initialize(clientId, clientSecret)", "fby_gog_initialize(clientId, clientSecret)"),
                     B("Galaxy::Init with a User/Listener registration.",
                       "Galaxy::Init с регистрацией User/Listener.")],
                    [B("fby_gog_process_data()", "fby_gog_process_data()"),
                     B("Galaxy::ProcessData, called once per Pump().",
                       "Galaxy::ProcessData, вызывается один раз за Pump().")],
                    [B("fby_gog_unlock_achievement(name)", "fby_gog_unlock_achievement(name)"),
                     B("IAchievements::SetAchievement + StoreAchievements.",
                       "IAchievements::SetAchievement + StoreAchievements.")],
                    [B("fby_gog_write_cloud/read_cloud/delete_cloud", "fby_gog_write_cloud/read_cloud/delete_cloud"),
                     B("IStorage file API; BufferTooSmall reports the required size.",
                       "Файловый API IStorage; BufferTooSmall сообщает требуемый размер.")],
                    [B("fby_gog_pending_cloud_writes()", "fby_gog_pending_cloud_writes()"),
                     B("Outstanding Galaxy write listeners.",
                       "Незавершённые listener'ы записи Galaxy.")],
                ],
            },
            {
                "type": "code",
                "lang": "cpp",
                "text": "// The provider resolves the shim once, then every call is a pointer hop.\n"
                        "bool EnsureLibrary() noexcept\n"
                        "{\n"
                        "    GState.library = OpenGalaxyLibrary();          // galaxy64.dll / .so / .dylib\n"
                        "    if (GState.library == nullptr)\n"
                        "    {\n"
                        "        return false;                              // honest NotAvailable\n"
                        "    }\n"
                        "    return ResolveFlat(GState.library, \"fby_gog_unlock_achievement\",\n"
                        "                       GState.api.unlockAchievement);\n"
                        "}\n"
                        "\n"
                        "// Game side: PostUpdate turns components into logged commands.\n"
                        "Fby::Platform::GogAchievementSystem achievements;\n"
                        "achievements.Initialize(platform, commandLog, 1);\n"
                        "Fby::Platform::GogAchievementSystem::Register(graph, achievements);\n"
                        "\n"
                        "Fby::Platform::GogStorageSystem storage;\n"
                        "storage.Initialize(platform, commandLog, 1);\n"
                        "std::span<const std::byte> save = SerializeSlot(slot);\n"
                        "storage.StageWrite(0x51u, \"slot0.sav\", save);\n"
                        "Fby::Platform::GogStorageSystem::Register(graph, storage);",
            },
            {
                "type": "p",
                "text": B(
                    "Both systems run in Phase::PostUpdate. They iterate a bounded view (64 "
                    "entities), sort deterministically by id and entity, append one byte-exact "
                    "record to PlatformCommandLog and queue one fixed-size PlatformCommand. "
                    "PlatformService::Pump() is the only caller of the shim, so a Galaxy callback "
                    "can never run on a simulation thread. When the shim is absent the provider "
                    "returns NotAvailable and the log keeps the intent instead of pretending the "
                    "achievement unlocked.",
                    "Обе системы работают в Phase::PostUpdate. Они обходят ограниченный view (64 "
                    "сущности), детерминированно сортируют по id и сущности, добавляют одну "
                    "байт-точную запись в PlatformCommandLog и ставят в очередь одну "
                    "PlatformCommand фиксированного размера. Только PlatformService::Pump() "
                    "вызывает шим, поэтому колбэк Galaxy не может выполниться в потоке "
                    "симуляции. Если шима нет, провайдер возвращает NotAvailable, и лог "
                    "сохраняет намерение вместо имитации разблокировки.",
                ),
            },
        ],
    },
    {
        "number": "21",
        "title": B("Font pipeline: fby_fontc, .fbyfont and the stable buffer",
                   "Шрифтовой конвейер: fby_fontc, .fbyfont и стабильный буфер"),
        "blocks": [
            {
                "type": "p",
                "text": B(
                    "Pink UI never rasterizes a TTF at runtime. fby_fontc bakes the vector "
                    "outlines into the engine's atlas format, the cooker packs the blob into "
                    ".fempak, and the VFS maps it on demand. The tool is the only target that "
                    "links stb_truetype.",
                    "Pink UI никогда не растеризует TTF в рантайме. fby_fontc печёт векторные "
                    "контуры в формат атласа движка, кукер упаковывает блоб в .fempak, а VFS "
                    "монтирует его по требованию. Только этот инструмент линкует stb_truetype.",
                ),
            },
            {
                "type": "code",
                "lang": "bash",
                "text": "fby_fontc --input assets/fonts/Inter-Regular.ttf \\\n"
                        "          --output assets/fonts/Inter.fbyfont \\\n"
                        "          --size 32 --font-id 0 --ranges latin,cyrillic --verify\n"
                        "\n"
                        "fby_fempak --input assets --output assets.fempak",
            },
            {
                "type": "table",
                "headers": [B("Region", "Область"), B("Contents", "Содержимое")],
                "rows": [
                    [B("Header", "Заголовок"),
                     B("48 bytes: magic FBYFNT01, version, glyph count, atlas size, format, "
                       "offsets, line height, ascent/descent, font id.",
                       "48 байт: magic FBYFNT01, версия, число глифов, размер атласа, формат, "
                       "смещения, высота строки, ascent/descent, id шрифта.")],
                    [B("Glyph table", "Таблица глифов"),
                     B("40 bytes per glyph: codepoint, normalized UVs, advance, bearings, quad "
                       "size. Sorted ascending for the binary search in FontAtlas::Glyph().",
                       "40 байт на глиф: кодовая точка, нормализованные UV, advance, bearings, "
                       "размер квада. Отсортирована по возрастанию для бинарного поиска в "
                       "FontAtlas::Glyph().")],
                    [B("Pixels", "Пиксели"),
                     B("RGBA8: white RGB with the coverage mask in A, so a label tint multiplies "
                       "correctly.",
                       "RGBA8: белый RGB и маска покрытия в A, поэтому тинт подписи умножается "
                       "корректно.")],
                ],
            },
            {
                "type": "note",
                "text": B(
                    "Editor typography is a different consumer: ImGui needs vector outlines to "
                    "bake glyphs at arbitrary DPI and cannot read .fbyfont. EditorFonts copies the "
                    "TTF bytes from the VFS into an EditorFonts-owned buffer before handing the "
                    "pointer to ImGui, so a hot reload that unmaps the file can never leave the "
                    "atlas pointing at freed pages. The atlas header is still validated on "
                    "startup, which is how a broken game-UI font is caught by the editor.",
                    "Типографика редактора — другой потребитель: ImGui нужны векторные контуры, "
                    "чтобы печь глифы под произвольный DPI, и он не читает .fbyfont. EditorFonts "
                    "копирует байты TTF из VFS в собственный буфер до передачи указателя ImGui, "
                    "поэтому hot reload, размонтирующий файл, не оставит атлас ссылаться на "
                    "освобождённые страницы. Заголовок атласа всё равно валидируется при старте — "
                    "так редактор ловит сломанный игровой шрифт.",
                ),
            },
        ],
    },
    {
        "number": "22",
        "title": B("Cloth render path: from Verlet vertices to one indirect draw",
                   "Путь отрисовки ткани: от вершин Verlet до одного indirect-вызова"),
        "blocks": [
            {
                "type": "p",
                "text": B(
                    "ClothSystem integrates a Verlet grid and writes MeshVertex positions that are "
                    "already final. The renderer must not skin them: with boneCount == 0 a "
                    "skinning dispatch would multiply every vertex by an identity-stack default "
                    "and collapse the cloth to a point. The engine therefore has exactly one "
                    "classification helper and one producer:",
                    "ClothSystem интегрирует Verlet-сетку и пишет позиции MeshVertex, которые уже "
                    "финальны. Рендерер не должен их скинить: при boneCount == 0 диспатч "
                    "скиннинга умножил бы каждую вершину на дефолт стека и схлопнул ткань в "
                    "точку. Поэтому в движке ровно один помощник классификации и один "
                    "поставщик данных:",
                ),
            },
            {
                "type": "code",
                "lang": "cpp",
                "text": "// Mesh.hpp: one predicate, used by VulkanRHI and any future backend.\n"
                        "inline bool IsUnskinnedMeshItem(const MeshRenderItem& item) noexcept\n"
                        "{\n"
                        "    return (item.Flags & MeshItemUnskinned) != 0 || item.boneCount == 0;\n"
                        "}\n"
                        "\n"
                        "// VulkanRHI: unskinned vertices bypass the compute dispatch entirely.\n"
                        "const bool unskinned = !isStatic && IsUnskinnedMeshItem(item);\n"
                        "memcpy(targetVertexRing + totalSkinned + unskinnedCursor, item.vertices.data(), ...);\n"
                        "record.unskinned = true;   // mesh.vert reads binding 3, not the skinned SSBO\n"
                        "\n"
                        "// App: ClothRenderSystem is the producer the path was missing.\n"
                        "Fby::ClothRenderSystem clothRender;\n"
                        "clothRender.Initialize(&clothSystem);\n"
                        "Fby::ClothRenderSystem::Register(graph, clothRender);   // Phase::PostUpdate\n"
                        "extract.Build(registry, camera, clear, textureArray, clothRender.Items());",
            },
            {
                "type": "p",
                "text": B(
                    "The bridge fills a fixed 64-item array, forces MeshItemUnskinned and sorts by "
                    "(sortLayer, depth, ClothId), so draw order is deterministic without an "
                    "allocation. The regression test checks the mesh still spans the authored "
                    "64x64 pixels - the exact symptom of a collapsed cloth - and that a zero-bone "
                    "item is classified as unskinned even when its flag is missing.",
                    "Мост заполняет фиксированный массив на 64 элемента, принудительно ставит "
                    "MeshItemUnskinned и сортирует по (sortLayer, depth, ClothId), поэтому порядок "
                    "отрисовки детерминирован без аллокаций. Регрессионный тест проверяет, что "
                    "сетка по-прежнему занимает заданные 64x64 пикселя — ровно симптом "
                    "схлопывания, — и что элемент с нулём костей классифицируется как unskinned, "
                    "даже если флаг потерян.",
                ),
            },
        ],
    },
    {
        "number": "23",
        "title": B("Atmosphere: weather context, fBm wind and GPU rain with SDF splashes",
                   "Атмосфера: погодный контекст, fBm-ветер и GPU-дождь с брызгами по SDF"),
        "blocks": [
            {
                "type": "p",
                "text": B(
                    "WeatherContextComponent is the single authored source of atmosphere constants "
                    "and wind. WeatherSystem resolves one gust-modulated vector per frame with "
                    "fBm noise and writes it back into the component and into the std140-friendly "
                    "WeatherWindField block. Cloth, plant vertices and rain all read that same "
                    "vector, so nothing disagrees about where the wind blows.",
                    "WeatherContextComponent — единственный авторский источник констант атмосферы "
                    "и ветра. WeatherSystem раз в кадр вычисляет вектор с модуляцией порывами "
                    "через fBm-шум и записывает его обратно в компонент и в std140-блок "
                    "WeatherWindField. Ткань, вершины растений и дождь читают один и тот же "
                    "вектор, поэтому нет расхождений в направлении ветра.",
                ),
            },
            {
                "type": "code",
                "lang": "cpp",
                "text": "Fby::Weather::WeatherContextComponent weather;\n"
                        "weather.atmosphereDensity = 1.35f;   // denser alien air\n"
                        "weather.airViscosity = 1.1e-5f;\n"
                        "weather.gravityScale = 0.82f;\n"
                        "weather.windSpeed = 26.0f;\n"
                        "weather.windDirectionX = 0.8f;\n"
                        "weather.windDirectionY = -0.6f;\n"
                        "weather.turbulence = 0.55f;\n"
                        "weather.rainIntensity = 0.85f;\n"
                        "weather.Flags |= Fby::Weather::WeatherRain | Fby::Weather::WeatherWind;\n"
                        "\n"
                        "// WeatherSystem -> ClothSystem -> shader block, one resolved vector.\n"
                        "cloth.SetAmbientWind({weather.resolvedWindX, weather.resolvedWindY});\n"
                        "shaders.SetWeatherBlock(WeatherWindFieldFrom(weather));",
            },
            {
                "type": "p",
                "text": B(
                    "WeatherPreconciliatedSystem owns no particles on the CPU. It uploads one "
                    "WeatherGpuConstants block per frame and records four graph passes: integrate "
                    "droplets (weather_rain.comp), age splashes (weather_rain_splash.comp), draw "
                    "line segments (weather_rain.vert/frag) and draw splash sprites. The rain "
                    "compute shader integrates wind, samples the global terrain SDF and appends a "
                    "splash with atomicAdd when a droplet crosses the implicit surface.",
                    "WeatherPreconciliatedSystem не хранит частицы на CPU. Он загружает один блок "
                    "WeatherGpuConstants за кадр и записывает четыре прохода графа: интеграция "
                    "капель (weather_rain.comp), старение брызг (weather_rain_splash.comp), "
                    "отрисовка линий (weather_rain.vert/frag) и отрисовка спрайтов брызг. "
                    "Compute-шейдер дождя интегрирует ветер, сэмплирует глобальную SDF-карту "
                    "рельефа и добавляет брызгу через atomicAdd, когда капля пересекает "
                    "неявную поверхность.",
                ),
            },
            {
                "type": "code",
                "lang": "glsl",
                "text": "// weather_rain.comp: one invocation per droplet, 262144 drops per dispatch.\n"
                        "const vec2 previous = particle.state0.xy;\n"
                        "particle.state0.xy += velocity * dt;\n"
                        "\n"
                        "const vec2 uv = (particle.state0.xy - weather.fieldTransform.xy) / fieldSize;\n"
                        "const float surface = texture(uSdf, uv).r;          // negative = solid\n"
                        "if (surface <= 0.0 && previousSurface > 0.0)\n"
                        "{\n"
                        "    const uint slot = atomicAdd(splashBuffer.splashCount, 1u);\n"
                        "    splashBuffer.splashes[slot].state0 = vec4(particle.state0.xy, 0.0, 1.0);\n"
                        "    splashBuffer.splashes[slot].color = particle.color;\n"
                        "    spawnParticle(particle, randomState);              // respawn at the top\n"
                        "}\n"
                        "particleBuffer.particles[particleIndex] = particle;",
            },
            {
                "type": "note",
                "text": B(
                    "The Metal mirror embeds the same constants and the same splash arithmetic; "
                    "the MSL source is compiled into the engine at build time. The CPU cost of a "
                    "storm is one constant upload plus the graph records, independent of the drop "
                    "count.",
                    "Metal-версия содержит те же константы и ту же арифметику брызг; MSL-исходник "
                    "встраивается в движок при сборке. Стоимость бури на CPU — одна загрузка "
                    "констант плюс записи графа, независимо от числа капель.",
                ),
            },
        ],
    },
    {
        "number": "24",
        "title": B("ECS by composition: a 2D shooter without inheritance",
                   "ECS через композицию: 2D-шутер без наследования"),
        "blocks": [
            {
                "type": "p",
                "text": B(
                    "A Player and an Enemy are not classes. They are the same entity type with "
                    "different component sets, and the systems that run are selected by data. "
                    "This section builds the canonical shooter from four components and three "
                    "systems; adding a boss means adding components and a spawn row, not a new "
                    "subclass.",
                    "Player и Enemy — не классы. Это один тип сущности с разными наборами "
                    "компонентов, а набор работающих систем определяется данными. Этот раздел "
                    "строит канонический шутер из четырёх компонентов и трёх систем; чтобы "
                    "добавить босса, нужны компоненты и строка спавна, а не новый подкласс.",
                ),
            },
            {
                "type": "code",
                "lang": "cpp",
                "text": "struct HealthComponent   { float health = 100.0f; float maxHealth = 100.0f; float invuln = 0.0f; };\n"
                        "struct WeaponComponent   { float damage = 10.0f; float cooldown = 0.0f; float interval = 0.12f;\n"
                        "                           float projectileSpeed = 900.0f; std::uint32_t archetype = 0; };\n"
                        "struct InputComponent    { std::int16_t moveX = 0; std::int16_t moveY = 0; std::uint32_t buttons = 0; };\n"
                        "struct AIBehaviorComponent { std::uint32_t behaviour = 0; float aggroRadius = 400.0f;\n"
                        "                             float fireRange = 320.0f; std::uint32_t targetNetId = 0; };\n"
                        "\n"
                        "// Player: Transform + PhysicsBodyDesc + Health + Weapon + Input\n"
                        "// Enemy : Transform + PhysicsBodyDesc + Health + Weapon + AIBehavior\n"
                        "// Boss  : same as Enemy + phases[] + WeaponArray; no new class.",
            },
            {
                "type": "table",
                "headers": [B("System", "Система"), B("Phase", "Фаза"), B("Reads / writes", "Читает / пишет")],
                "rows": [
                    [B("InputSystem", "InputSystem"), B("PreUpdate", "PreUpdate"),
                     B("Reads InputComponent, writes it from the tick input packet.",
                       "Читает InputComponent, пишет его из пакета ввода тика.")],
                    [B("AISystem", "AISystem"), B("Update", "Update"),
                     B("Reads AIBehavior + Health, writes InputComponent (AI produces input, not velocity).",
                       "Читает AIBehavior + Health, пишет InputComponent (ИИ создаёт ввод, а не скорость).")],
                    [B("WeaponSystem", "WeaponSystem"), B("FixedUpdate", "FixedUpdate"),
                     B("Reads Weapon + Input, spawns projectiles through CommandBuffer.",
                       "Читает Weapon + Input, спавнит снаряды через CommandBuffer.")],
                    [B("DamageSystem", "DamageSystem"), B("PostUpdate", "PostUpdate"),
                     B("Reads collision events, decrements Health, queues despawn.",
                       "Читает события коллизий, уменьшает Health, ставит despawn в очередь.")],
                ],
            },
            {
                "type": "code",
                "lang": "cpp",
                "text": "void SpawnEnemy(Fby::Ecs::Registry& registry, const Fby::CharacterDesc& desc,\n"
                        "                const Fby::PhysicsBodyDesc& body, Fby::Vector2D at)\n"
                        "{\n"
                        "    const Fby::Entity enemy = registry.create();\n"
                        "    registry.emplace<Fby::Transform2D>(enemy, Fby::Transform2D{at, {}, {1.0f, 1.0f}, 0.0f});\n"
                        "    registry.emplace<Fby::RigidBody2D>(enemy, Fby::RigidBody2D{.Position = at});\n"
                        "    registry.emplace<Fby::Collider2D>(enemy, Fby::Collider2D{body.halfExtents});\n"
                        "    registry.emplace<HealthComponent>(enemy, HealthComponent{desc.maxHealth, desc.maxHealth, 0.0f});\n"
                        "    registry.emplace<WeaponComponent>(enemy,\n"
                        "        WeaponComponent{desc.damage, 0.0f, desc.fireInterval, desc.projectileSpeed, desc.weaponArchetype});\n"
                        "    registry.emplace<AIBehaviorComponent>(enemy, AIBehaviorComponent{0, 420.0f, 300.0f, 0});\n"
                        "}",
            },
            {
                "type": "p",
                "text": B(
                    "Determinism follows from the component set: the same seed, the same integer "
                    "inputs and the same systems produce the same snapshot checksum. A boss that "
                    "needs a phase machine adds a PhaseComponent and a system in the same phase; "
                    "it does not change Player, Enemy, the weapon code or the netcode.",
                    "Детерминизм следует из набора компонентов: те же seed, целочисленный ввод и "
                    "системы дают ту же контрольную сумму снапшота. Боссу с фазами достаточно "
                    "добавить PhaseComponent и систему в той же фазе; Player, Enemy, код оружия "
                    "и сетевой код не меняются.",
                ),
            },
        ],
    },
    {
        "number": "25",
        "title": B("Multi-Draw Indirect and Texture Cooking in production",
                   "Multi-Draw Indirect и кукинг текстур в продакшене"),
        "blocks": [
            {
                "type": "p",
                "text": B(
                    "Sprites and meshes take two different paths through the same FramePacket. "
                    "Sprites are 48-byte RenderItems sorted into batches; meshes are "
                    "MeshRenderItems that become one MeshDrawData record and one indirect command "
                    "each, grouped by (textureArray, layer, materialPipeline). The GPU never sees "
                    "a per-object descriptor bind on this path.",
                    "Спрайты и меши идут двумя разными путями через один FramePacket. Спрайты — "
                    "48-байтные RenderItem, отсортированные в батчи; меши — MeshRenderItem, из "
                    "которых получается одна запись MeshDrawData и одна indirect-команда на "
                    "элемент, сгруппированные по (textureArray, layer, materialPipeline). GPU не "
                    "видит привязку дескриптора на объект на этом пути.",
                ),
            },
            {
                "type": "code",
                "lang": "cpp",
                "text": "Fby::Rhi::MeshRenderItem item;\n"
                        "item.vertices   = skinVertices;              // or MeshSystem::WriteMesh()\n"
                        "item.indices    = skinIndices;\n"
                        "item.boneOffset = paletteOffset;\n"
                        "item.boneCount  = paletteSize;               // 0 => unskinned bind pose\n"
                        "item.textureArray = atlas.Handle().id;       // one binding for the scene\n"
                        "item.Layer        = sprite.Layer;\n"
                        "item.model        = Fby::Mat4::Identity();\n"
                        "\n"
                        "packet.meshItems = renderExtract.MeshItems();\n"
                        "packet.boneMatrices = renderExtract.BonePalette();\n"
                        "rhi.Submit(packet);                              // one indirect call per group",
            },
            {
                "type": "table",
                "headers": [B("Budget", "Бюджет"), B("What to measure", "Что измерять")],
                "rows": [
                    [B("MeshDrawData", "MeshDrawData"),
                     B("80 bytes per draw, uploaded once per frame; firstInstance selects it.",
                       "80 байт на draw, загружается раз в кадр; firstInstance выбирает запись.")],
                    [B("Bone palette", "Палитра костей"),
                     B("One contiguous Mat4 block per frame; never per draw.",
                       "Один непрерывный блок Mat4 на кадр; никогда не на draw.")],
                    [B("Texture array", "Текстурный массив"),
                     B("One sampler2DArray binding for the whole scene; cooking picks BC7 (desktop) "
                       "or ASTC (mobile).",
                       "Одна привязка sampler2DArray на всю сцену; кукер выбирает BC7 (десктоп) "
                       "или ASTC (мобильные).")],
                    [B("SceneDiagnostics", "SceneDiagnostics"),
                     B("Watch MeshRenderItem, indirect calls, draw calls and the frame arena in one panel.",
                       "Следите за MeshRenderItem, indirect-вызовами, draw calls и кадровой ареной "
                       "в одной панели.")],
                ],
            },
            {
                "type": "code",
                "lang": "bash",
                "text": "# Texture cooking: source PNG -> .ftex (BC7/ASTC mips) -> .fempak\n"
                        "fby_fempak --input assets --output assets.fempak --format bc7-srgb\n"
                        "fby_fontc --input assets/fonts/Inter-Regular.ttf --output assets/fonts/Inter.fbyfont\n"
                        "\n"
                        "# Verify the shipping pack through the same VFS the game uses\n"
                        "ctest --test-dir build -R data_driven --output-on-failure",
            },
            {
                "type": "note",
                "text": B(
                    "The editor's Project Settings panel edits the cook format and the collision "
                    "matrix transactionally; both persist into project.femproj and are applied to "
                    "live b2Shape filters on commit. A project that changes its target from "
                    "desktop to mobile keeps the same scene and re-cooks the atlas.",
                    "Панель Project Settings редактирует формат кукинга и матрицу коллизий "
                    "транзакционно; оба значения сохраняются в project.femproj и применяются к "
                    "живым b2Shape-фильтрам при коммите. Проект, меняющий цель с десктопа на "
                    "мобильные, сохраняет ту же сцену и перекукивает атлас.",
                ),
            },
        ],
    },
    {
        "number": "26",
        "title": B("Repository layout, CMake targets and the gates",
                   "Структура репозитория, цели CMake и гейты"),
        "blocks": [
            {
                "type": "p",
                "text": B(
                    "The tree is part of the contract. CheckStyle.py now fails when a "
                    "translation unit appears directly under Source/Engine or Source/Editor, when "
                    "an unexpected module directory exists, or when a build log, object file, "
                    "screenshot or Python cache is committed outside the disposable build tree. "
                    "The module set is fixed: Core, RHI, ECS, Physics, Audio, Network, Assets, "
                    "Script, UI, Video, plus the first-class Weather and Platform modules.",
                    "Дерево — часть контракта. CheckStyle.py теперь падает, если единица "
                    "трансляции появляется прямо в Source/Engine или Source/Editor, если "
                    "существует неожиданный каталог модуля, или если лог сборки, объектный файл, "
                    "скриншот или Python-кэш закоммичены вне одноразового дерева сборки. Набор "
                    "модулей фиксирован: Core, RHI, ECS, Physics, Audio, Network, Assets, Script, "
                    "UI, Video плюс полноценные Weather и Platform.",
                ),
            },
            {
                "type": "table",
                "headers": [B("Target", "Цель"), B("Kind", "Вид"), B("Purpose", "Назначение")],
                "rows": [
                    [B("fby_engine", "fby_engine"), B("static library", "статическая библиотека"),
                     B("Runtime; no ImGui, no TOML writer, no storefront SDK.",
                       "Рантайм; без ImGui, TOML-писателя и SDK магазина.")],
                    [B("fby_editor / fby_editor_app", "fby_editor / fby_editor_app"),
                     B("library + executable", "библиотека + исполняемый файл"),
                     B("ImGui/ImPlot panels and the IDE.", "Панели ImGui/ImPlot и IDE.")],
                    [B("fby_fontc", "fby_fontc"), B("tool", "инструмент"),
                     B("TTF -> .fbyfont atlas; the only stb_truetype consumer.",
                       "TTF -> атлас .fbyfont; единственный потребитель stb_truetype.")],
                    [B("fby_fempak", "fby_fempak"), B("tool", "инструмент"),
                     B("Asset cooker and archive writer.", "Кукер ассетов и писатель архива.")],
                    [B("fby_platform_steam/egs/gog", "fby_platform_steam/egs/gog"),
                     B("shared libraries", "разделяемые библиотеки"),
                     B("Storefront providers behind the C ABI.", "Провайдеры магазинов за C ABI.")],
                ],
            },
            {
                "type": "code",
                "lang": "bash",
                "text": "python Source/Tools/Dev/CheckStyle.py                 # style + layout + hygiene\n"
                        "cmake --build build --target fby_editor fby_fontc fby_platform_gog\n"
                        "ctest --test-dir build --output-on-failure        # the full suite\n"
                        "python Documentation/Tools/GenerateCompleteReference.py",
            },
            {
                "type": "note",
                "text": B(
                    "Shaders still live under Source/Engine/RHI/Shaders because the engine embeds "
                    "their SPIR-V at configure time; the directory is the shader module, not a "
                    "loose asset folder. Moving it to a top-level Shaders/ would require changing "
                    "the embed step and every shader path for no packaging benefit.",
                    "Шейдеры остаются в Source/Engine/RHI/Shaders, потому что движок встраивает "
                    "их SPIR-V на этапе конфигурации; этот каталог — модуль шейдеров, а не "
                    "свободная папка ассетов. Перенос в верхнеуровневый Shaders/ потребовал бы "
                    "менять шаг встраивания и все пути шейдеров без выгоды для поставки.",
                ),
            },
        ],
    },
    {
        "number": "27",
        "title": B("Threading contract: RHI, the OS main loop and TTS",
                   "Контракт потоков: RHI, главный цикл ОС и TTS"),
        "blocks": [
            {
                "type": "p",
                "text": B(
                    "Three subsystems have non-negotiable thread affinity: the RHI command "
                    "stream, the window/ImGui loop and OS speech. Simulation systems may run "
                    "on JobSystem workers, but workers never call the RHI, never touch the "
                    "window and never create an AVSpeechSynthesizer or an ISpVoice. They only "
                    "append to lock-free queues and fixed-size logs.",
                    "У трёх подсистем жёсткая привязка к потоку: командный поток RHI, цикл "
                    "окна/ImGui и системная речь. Симуляционные системы могут исполняться на "
                    "воркерах JobSystem, но воркеры никогда не вызывают RHI, не трогают окно и "
                    "не создают AVSpeechSynthesizer или ISpVoice. Они только пишут в lock-free "
                    "очереди и логи фиксированного размера.",
                ),
            },
            {
                "type": "table",
                "headers": [B("Entry point", "Точка входа"), B("Affinity", "Поток"),
                            B("Rule", "Правило")],
                "rows": [
                    [B("VulkanRHI::DrawPacket", "VulkanRHI::DrawPacket"),
                     B("main render thread", "главный поток рендера"),
                     B("Records vkCmdBindDescriptorSets inside the batch loop; the batch carries full "
                       "32-bit pipeline and texture-array ids, 0 inherits the packet array.",
                       "Пишет vkCmdBindDescriptorSets внутри цикла батчей; батч несёт полные 32-битные "
                       "id пайплайна и текстурного массива, 0 наследует массив пакета.")],
                    [B("WidgetCanvasSystem::MountFonts", "WidgetCanvasSystem::MountFonts"),
                     B("cold path, main thread", "холодный путь, главный поток"),
                     B("Mandatory UI host stage. Creates the GPU texture array and may WaitIdle. A missing "
                       "set, an empty set or a failed mount logs PANIC and trips FBY_ASSERT_MSG: there is "
                       "no fallback.",
                       "Обязательный этап UI-хоста. Создаёт GPU-массив и может вызвать WaitIdle. "
                       "Отсутствующий набор, пустой набор или неудачный монтаж пишут PANIC и вызывают "
                       "FBY_ASSERT_MSG: запасного пути нет.")],
                    [B("WidgetCanvasSystem::BuildFrame", "WidgetCanvasSystem::BuildFrame"),
                     B("main render thread", "главный поток рендера"),
                     B("Reads FontAtlasSet::IsCurrent() and the 32-bit array id. Text without a mounted "
                       "array returns BuildStatus::FontsNotMounted and writes nothing; no GPU creation, "
                       "no WaitIdle, no VFS remap, no allocation after Add().",
                       "Читает FontAtlasSet::IsCurrent() и 32-битный id массива. Текст без смонтированного "
                       "массива возвращает BuildStatus::FontsNotMounted и ничего не пишет; никакого "
                       "создания GPU-ресурсов, WaitIdle, перемапливания VFS и аллокаций после Add().")],
                    [B("FontAtlasSet::MountToTextureArray", "FontAtlasSet::MountToTextureArray"),
                     B("cold path", "холодный путь"),
                     B("Validates the device maxTextureLayers before touching GPU memory, publishes the "
                       "new array only after every layer is built, and calls WaitIdle before destroying "
                       "the previous array (no GPU use-after-free).",
                       "Проверяет maxTextureLayers устройства до выделения GPU-памяти, публикует новый "
                       "массив только после сборки всех слоёв и вызывает WaitIdle перед уничтожением "
                       "предыдущего массива (никакого use-after-free на GPU).")],
                    [B("EditorFonts hot reload", "Горячая перезагрузка EditorFonts"),
                     B("main thread, before NewFrame", "главный поток, до NewFrame"),
                     B("DetectFontChanges() -> RHI::WaitIdle() -> ReloadFonts(). PollForFontChanges() was "
                       "removed: the combined helper destroyed the atlas without the barrier.",
                       "DetectFontChanges() -> RHI::WaitIdle() -> ReloadFonts(). PollForFontChanges() "
                       "удалён: совмещённый помощник уничтожал атлас без барьера.")],
                    [B("AccessibilityKernel::Update", "AccessibilityKernel::Update"),
                     B("simulation / worker", "симуляция / воркер"),
                     B("Builds the Narrative Tree and pushes NarrativeEvents. It never calls SAPI or "
                       "AVSpeech.",
                       "Строит Narrative Tree и пишет NarrativeEvents. Никогда не вызывает SAPI или "
                       "AVSpeech.")],
                    [B("AccessibilityKernel::SetBlindMode / SetTtsBackend",
                       "AccessibilityKernel::SetBlindMode / SetTtsBackend"),
                     B("OS main thread (asserted)", "главный поток ОС (с проверкой)"),
                     B("RequireMainThread() first: a worker call is logged, dropped and trips "
                       "FBY_ASSERT_MSG in an assert build. Enabling BlindMode may attach the native "
                       "backend and select a voice.",
                       "Сначала RequireMainThread(): вызов из воркера логируется, отбрасывается и "
                       "вызывает FBY_ASSERT_MSG в assert-сборке. Включение BlindMode может подключить "
                       "нативный бэкенд и выбрать голос.")],
                    [B("AccessibilityKernel::SetNarrativeLocale",
                       "AccessibilityKernel::SetNarrativeLocale"),
                     B("any thread", "любой поток"),
                     B("Changes the narrative tree language. A worker call only records a pending TTS "
                       "language; the setVoice() is applied by FlushSpeech() on the owner thread.",
                       "Меняет язык Narrative Tree. Вызов из воркера лишь запоминает отложенный язык TTS; "
                       "setVoice() применяется в FlushSpeech() в потоке-владельце.")],
                    [B("AccessibilityKernel::FlushSpeech", "AccessibilityKernel::FlushSpeech"),
                     B("OS main thread", "главный поток ОС"),
                     B("Called from EditorSystem::Tick() / the SDL main loop. Applies a deferred locale, "
                       "then drains the log and calls the backend speak callback. A worker call returns "
                       "before the log is touched.",
                       "Вызывается из EditorSystem::Tick() / главного цикла SDL. Применяет отложенную "
                       "локаль, затем дренирует лог и вызывает speak-колбэк бэкенда. Вызов из воркера "
                       "возвращается до обращения к логу.")],
                    [B("SAPI 5 (Windows)", "SAPI 5 (Windows)"),
                     B("STA main thread", "главный STA-поток"),
                     B("CoInitializeEx(COINIT_APARTMENTTHREADED) is paired with CoUninitialize. "
                       "SetVoice enumerates every installed token and scores it against the BCP-47 "
                       "tag; no match falls back to the system default voice.",
                       "CoInitializeEx(COINIT_APARTMENTTHREADED) парен с CoUninitialize. SetVoice "
                       "перебирает все установленные токены и оценивает их по BCP-47 тегу; при "
                       "отсутствии совпадения используется системный голос по умолчанию.")],
                    [B("AVSpeechSynthesizer (macOS)", "AVSpeechSynthesizer (macOS)"),
                     B("main run loop", "главный run loop"),
                     B("Initialize/Speak/SetVoice assert [NSThread isMainThread]. SetVoice resolves the "
                       "best installed voice language and falls back to the system default.",
                       "Initialize/Speak/SetVoice проверяют [NSThread isMainThread]. SetVoice выбирает "
                       "лучший установленный язык голоса и откатывается к системному по умолчанию.")],
                    [B("JobSystem workers", "Воркеры JobSystem"),
                     B("any worker", "любой воркер"),
                     B("May run ECS systems and append to lock-free rings; may not call RHI, window, VFS "
                       "remap or TTS.",
                       "Могут исполнять ECS-системы и писать в lock-free кольца; не могут вызывать RHI, "
                       "окно, перемапливание VFS или TTS.")],
                ],
            },
            {
                "type": "code",
                "lang": "cpp",
                "text": "// Cold path: app init / scene load, OS main thread. Creates the GPU array once.\n"
                        "ui::WidgetCanvasSystem scopedUi{&fonts};\n"
                        "if (!scopedUi.MountFonts(*gpu))\n"
                        "{\n"
                        "    // Panic already logged; an assert build never reaches this line.\n"
                        "    return Shutdown(ExitCode::UiFontsUnavailable);\n"
                        "}\n"
                        "\n"
                        "// Hot path: Render phase, main thread. No GPU creation, no WaitIdle.\n"
                        "ui::FrameOutput out;\n"
                        "const ui::BuildStatus status = ui::WidgetCanvasSystem::BuildFrame(ctx, canvas, out);\n"
                        "if (status == ui::BuildStatus::FontsNotMounted) { /* refuse the whole UI pass */ }\n"
                        "// out.batches[0].textureArray is the 32-bit font array id and out.items carry the\n"
                        "// real layer (0 = white, 1..N = fonts); DrawPacket rebinds per batch.\n"
                        "\n"
                        "// Font hot reload: the barrier is deliberate and one-shot.\n"
                        "if (fonts.DetectFontChanges())\n"
                        "{\n"
                        "    gpu->WaitIdle();\n"
                        "    fonts.ReloadFonts(ImGui::GetIO());\n"
                        "}\n"
                        "\n"
                        "// Speech: Simulation never flushes. The editor/Main loop owns it.\n"
                        "void EditorSystem::Tick(ecs::SystemContext& ctx)\n"
                        "{\n"
                        "    if (ctx.accessibility) { ctx.accessibility->FlushSpeech(); }  // main thread only\n"
                        "    // ... ImGui frame ...\n"
                        "}\n",
            },
            {
                "type": "note",
                "text": B(
                    "Rule of thumb: if an operation can wait on the GPU, allocate a VkImage, close a VFS "
                    "mapping or talk to a COM/Objective-C UI object, it belongs to the cold path or the OS "
                    "main thread. The render and simulation hot paths only read immutable state and append "
                    "to queues. A debug build turns every one of those boundaries into an FBY_ASSERT_MSG; a "
                    "release build logs and drops the unsafe call instead of issuing it.",
                    "Правило: если операция может ждать GPU, создавать VkImage, закрывать mmap VFS или "
                    "обращаться к COM/Objective-C UI-объекту, она принадлежит холодному пути или главному "
                    "потоку ОС. Горячие пути рендера и симуляции только читают неизменяемое состояние и "
                    "пишут в очереди. Debug-сборка превращает каждую такую границу в FBY_ASSERT_MSG; "
                    "release-сборка логирует и отбрасывает небезопасный вызов, а не выполняет его.",
                ),
            },
        ],
    },
    {
        "number": "28",
        "title": B("The 32-bit DrawBatch pipeline: end of the 65,536-texture ceiling",
                   "32-битный конвейер DrawBatch: конец потолка в 65 536 текстур"),
        "blocks": [
            {
                "type": "p",
                "text": B(
                    "DrawBatch is the only place where a texture-array id and a shader-pipeline id cross "
                    "the render-extract boundary. Until the RC-2 audit both fields were std::uint16_t, "
                    "while every RHI handle (TextureArrayHandle, ShaderPipelineHandle) is std::uint32_t. "
                    "The result was a silent alias: the 65,537th texture array or pipeline was truncated "
                    "to an unrelated low id and the batch sampled the wrong resource - with no error, no "
                    "assert and no way to see it in a log. The fix widens both fields to std::uint32_t "
                    "and reorders them so the struct is still exactly 16 bytes.",
                    "DrawBatch — единственное место, где id текстурного массива и id шейдерного "
                    "пайплайна пересекают границу render-extract. До аудита RC-2 оба поля были "
                    "std::uint16_t, тогда как все RHI-хэндлы (TextureArrayHandle, ShaderPipelineHandle) "
                    "— std::uint32_t. Результат — тихое совпадение id: 65 537-й текстурный массив или "
                    "пайплайн обрезался до чужого младшего id, и батч сэмплировал не тот ресурс — без "
                    "ошибки, без assert и без следа в логе. Исправление расширяет оба поля до "
                    "std::uint32_t и переставляет их так, что структура по-прежнему ровно 16 байт.",
                ),
            },
            {
                "type": "table",
                "headers": [B("Offset", "Смещение"), B("Field", "Поле"), B("Type", "Тип"),
                            B("Contract", "Контракт")],
                "rows": [
                    [B("0", "0"), B("firstItem", "firstItem"), B("uint32_t", "uint32_t"),
                     B("First RenderItem of the run; an index into the frame's item span.",
                       "Первый RenderItem серии; индекс в span элементов кадра.")],
                    [B("4", "4"), B("itemCount", "itemCount"), B("uint32_t", "uint32_t"),
                     B("Number of contiguous items in the run.",
                       "Число непрерывных элементов в серии.")],
                    [B("8", "8"), B("pipeline", "pipeline"), B("uint32_t", "uint32_t"),
                     B("ShaderPipelineHandle::id, 0 = the quad pipeline. No uint16 narrowing.",
                       "ShaderPipelineHandle::id, 0 = пайплайн квадов. Без сужения до uint16.")],
                    [B("12", "12"), B("textureArray", "textureArray"), B("uint32_t", "uint32_t"),
                     B("TextureArrayHandle::id, 0 = inherit FramePacket::textureArray (id 0 is the "
                       "backend's built-in atlas by contract).",
                       "TextureArrayHandle::id, 0 = наследовать FramePacket::textureArray (id 0 по "
                       "контракту — встроенный атлас бэкенда).")],
                ],
            },
            {
                "type": "code",
                "lang": "cpp",
                "text": "// RenderItem.hpp - 16 bytes, no padding, four batches per 64-byte cache line.\n"
                        "struct DrawBatch\n"
                        "{\n"
                        "    std::uint32_t firstItem = 0;\n"
                        "    std::uint32_t itemCount = 0;\n"
                        "    std::uint32_t pipeline = 0;      // ShaderPipelineHandle::id\n"
                        "    std::uint32_t textureArray = 0;  // TextureArrayHandle::id, 0 = inherit\n"
                        "};\n"
                        "static_assert(sizeof(DrawBatch) == 16);\n"
                        "static_assert(alignof(DrawBatch) == 4);\n"
                        "static_assert(offsetof(DrawBatch, textureArray) == 12);\n"
                        "static_assert(std::is_trivially_copyable_v<DrawBatch>);\n"
                        "\n"
                        "// VulkanRHI::DrawPacket - the id is consumed at full width.\n"
                        "const std::uint32_t requested =\n"
                        "    batch.textureArray != 0 ? batch.textureArray : packet.textureArray;\n"
                        "if (requested != boundArray) { BindDescriptorFor(requested); boundArray = requested; }\n"
                        "\n"
                        "// Pink UI writes the same 32-bit id: the old \"<= 0xFFFF\" clamp is gone.\n"
                        "Output.batches[0] = rhi::DrawBatch{start, count, canvas.pipeline, fontArray.id};",
            },
            {
                "type": "bullets",
                "items": [
                    B("No narrowing cast survives on the path: a grep for 0xFFFF in the batch, "
                      "WidgetCanvas and Vulkan batch loop returns nothing.",
                      "На пути не остаётся ни одного сужающего приведения: поиск 0xFFFF в батче, "
                      "WidgetCanvas и цикле батчей Vulkan ничего не находит."),
                    B("WidgetCanvas::textureArray and WidgetCanvas::pipeline are also 32-bit; SetTextureArray "
                      "and SetPipeline take std::uint32_t.",
                      "WidgetCanvas::textureArray и WidgetCanvas::pipeline тоже 32-битные; SetTextureArray "
                      "и SetPipeline принимают std::uint32_t."),
                    B("MeshRenderItem::textureArray was already std::uint32_t; static_asserts now pin its "
                      "width so a future tightening cannot re-introduce the ceiling on the mesh path.",
                      "MeshRenderItem::textureArray уже был std::uint32_t; static_assert'ы теперь фиксируют "
                      "его ширину, чтобы будущее «уплотнение» не вернуло потолок на mesh-путь."),
                    B("A host that indexes its own descriptor table from a batch id must validate it: the "
                      "engine forwards the id, the backend logs and falls back to the built-in atlas for "
                      "an unknown id instead of dereferencing it.",
                      "Хост, индексирующий собственную таблицу дескрипторов по id из батча, обязан его "
                      "проверять: движок передаёт id, а бэкенд для неизвестного id логирует ошибку и "
                      "использует встроенный атлас вместо разыменования."),
                ],
            },
            {
                "type": "note",
                "text": B(
                    "Why the struct did not grow: the two former uint16_t fields plus the 4-byte pad became "
                    "two uint32_t fields. The batch is still 16 bytes, the sorted batch array keeps its "
                    "density, and four consecutive batches share one cache line. Widening a resource id "
                    "must never cost a cache miss in the per-frame batch loop.",
                    "Почему структура не выросла: два бывших uint16_t поля плюс 4-байтный паддинг стали "
                    "двумя uint32_t полями. Батч по-прежнему 16 байт, отсортированный массив батчей "
                    "сохраняет плотность, а четыре подряд идущих батча делят одну кэш-линию. Расширение id "
                    "ресурса не должно стоить промаха кэша в покадровом цикле батчей.",
                ),
            },
        ],
    },
    {
        "number": "29",
        "title": B("Atomic VFS font hot reload and the strict MountFonts contract",
                   "Атомарный хот-релоад шрифтов VFS и строгий контракт MountFonts"),
        "blocks": [
            {
                "type": "p",
                "text": B(
                    "Pink UI text is atlas-only. The VFS mmap supplies cooked .fbyfont blobs, the cold "
                    "path packs them into one GPU texture array (white layer 0, font atlases 1..N), and "
                    "the hot path only reads that array. The RC-2 audit found two holes in this chain: a "
                    "lazy mount that let the first frame silently fall back to an 8x8 bitmap layer, and a "
                    "remount that destroyed the previous array while a frame could still sample it. Both "
                    "are now hard contracts.",
                    "Текст Pink UI существует только в атласе. mmap VFS поставляет приготовленные блобы "
                    ".fbyfont, холодный путь упаковывает их в один GPU-массив (слой 0 — белый, атласы "
                    "шрифтов 1..N), а горячий путь только читает этот массив. Аудит RC-2 нашёл в этой "
                    "цепочке две дыры: ленивый монтаж, позволявший первому кадру тихо откатиться на "
                    "8x8-слой, и повторный монтаж, уничтожавший предыдущий массив, пока кадр ещё мог его "
                    "сэмплировать. Оба места теперь — жёсткие контракты.",
                ),
            },
            {
                "type": "table",
                "headers": [B("Stage", "Этап"), B("Thread / timing", "Поток / момент"),
                            B("Rule", "Правило")],
                "rows": [
                    [B("FontAtlasSet::Add / LoadFromVfs", "FontAtlasSet::Add / LoadFromVfs"),
                     B("cold path", "холодный путь"),
                     B("Validates the .fbyfont header and the shared pixel size immediately, grows the "
                       "mount scratch outside the frame, and bumps the set Generation.",
                       "Сразу проверяет заголовок .fbyfont и общий размер пикселей, растит scratch-буфер "
                       "вне кадра и увеличивает Generation набора.")],
                    [B("WidgetCanvasSystem::MountFonts", "WidgetCanvasSystem::MountFonts"),
                     B("cold start, main thread", "холодный старт, главный поток"),
                     B("Mandatory. Empty/null/failed mount -> PANIC in LogBoy + FBY_ASSERT_MSG (debug). "
                       "Remounts when the set is not current.",
                       "Обязателен. Пустой/нулевой/неудачный монтаж -> PANIC в LogBoy + FBY_ASSERT_MSG "
                       "(debug). Перемонтирует набор, если он не актуален.")],
                    [B("FontAtlasSet::MountToTextureArray", "FontAtlasSet::MountToTextureArray"),
                     B("cold path", "холодный путь"),
                     B("Checks maxTextureLayers before allocating, builds every layer, publishes "
                       "atomically, and calls WaitIdle before releasing the previous array.",
                       "Проверяет maxTextureLayers до выделения, собирает все слои, публикует атомарно и "
                       "вызывает WaitIdle перед освобождением предыдущего массива.")],
                    [B("FontAtlasSet::IsCurrent", "FontAtlasSet::IsCurrent"),
                     B("read from the hot path", "чтение с горячего пути"),
                     B("MountedGeneration == Generation. Adding an atlas invalidates the mount; the next "
                       "frame is refused until MountFonts() runs again.",
                       "MountedGeneration == Generation. Добавление атласа инвалидирует монтаж; "
                       "следующий кадр отклоняется, пока MountFonts() не выполнится снова.")],
                    [B("WidgetCanvasSystem::BuildFrame", "WidgetCanvasSystem::BuildFrame"),
                     B("main render thread", "главный поток рендера"),
                     B("Text with a missing/stale/non-current set, an invalid atlas index or a foreign "
                       "image array returns FontsNotMounted and writes nothing. No mounting, no "
                       "WaitIdle, no allocation.",
                       "Текст с отсутствующим/устаревшим/неактуальным набором, неверным индексом атласа "
                       "или чужим массивом изображений возвращает FontsNotMounted и ничего не пишет. "
                       "Без монтажа, без WaitIdle, без аллокаций.")],
                    [B("EditorFonts::DetectFontChanges", "EditorFonts::DetectFontChanges"),
                     B("main thread, between frames", "главный поток, между кадрами"),
                     B("Detection only. The host then calls RHI::WaitIdle() and ReloadFonts(). "
                       "PollForFontChanges() was deleted because it skipped the barrier.",
                       "Только обнаружение. Затем хост вызывает RHI::WaitIdle() и ReloadFonts(). "
                       "PollForFontChanges() удалён, потому что пропускал барьер.")],
                ],
            },
            {
                "type": "code",
                "lang": "cpp",
                "text": "// 1. Cold start: load -> mount -> build. Order is mandatory.\n"
                        "ui::FontAtlasSet fonts;\n"
                        "if (!fonts.LoadFromVfs(vfs, \"fonts/Inter.fbyfont\")) { return Fail(\"atlas\"); }\n"
                        "ui::WidgetCanvasSystem ui{&fonts};\n"
                        "if (!ui.MountFonts(*gpu)) { return Fail(\"font array\"); }  // PANIC + assert in debug\n"
                        "\n"
                        "// 2. Hot frame: atlas-only text, full 32-bit array id.\n"
                        "ui::FrameOutput out{items, keys, batches, 0};\n"
                        "if (ui::WidgetCanvasSystem::BuildFrame(ctx, canvas, out) == ui::BuildStatus::FontsNotMounted)\n"
                        "{\n"
                        "    frame.fontContractViolated = true;  // release: draw no text, never a bitmap\n"
                        "}\n"
                        "\n"
                        "// 3. Adding a locale atlas at runtime invalidates the mount.\n"
                        "fonts.LoadFromVfs(vfs, \"fonts/NotoSansJP.fbyfont\");\n"
                        "if (!fonts.IsCurrent()) { (void)ui.MountFonts(*gpu); }  // remount, old array waits idle\n"
                        "\n"
                        "// 4. Editor ImGui typography: the only supported hot-reload chain.\n"
                        "if (editorFonts.DetectFontChanges())\n"
                        "{\n"
                        "    gpu->WaitIdle();\n"
                        "    editorFonts.ReloadFonts(ImGui::GetIO());  // between frames, before NewFrame()\n"
                        "}",
            },
            {
                "type": "bullets",
                "items": [
                    B("VFS ownership: EditorFonts copies each TTF into stable storage before ImGui sees it, "
                      "so a VFS remap can never leave the atlas pointing into an unmapped page.",
                      "Владение VFS: EditorFonts копирует каждый TTF в стабильный буфер до того, как его "
                      "увидит ImGui, поэтому перемапливание VFS не может оставить атлас указывающим в "
                      "размонтированную страницу."),
                    B("GPU ownership: FontAtlasSet waits for the device before destroying a texture array, "
                      "because the previous frame may still be sampling its descriptor set.",
                      "Владение GPU: FontAtlasSet ждёт устройство перед уничтожением текстурного массива, "
                      "потому что предыдущий кадр может ещё сэмплировать его descriptor set."),
                    B("Failure policy: a missing atlas is loud. There is no 8x8 fallback in any build; a "
                      "release build refuses the text frame and sets FrameOutput::fontContractViolated.",
                      "Политика отказа: отсутствующий атлас заметен. 8x8-фолбэка нет ни в одной сборке; "
                      "release-сборка отклоняет текстовый кадр и ставит "
                      "FrameOutput::fontContractViolated."),
                ],
            },
            {
                "type": "note",
                "text": B(
                    "Known limitation, now enforced instead of hidden: a canvas that mixes text with its "
                    "own image texture array cannot be one batch, because one batch samples exactly one "
                    "array. BuildFrame rejects it with FontsNotMounted. The follow-up is a per-array batch "
                    "split (sorted runs of items grouped by array), which needs a multi-batch FrameOutput "
                    "contract and is scheduled after Release.",
                    "Известное ограничение, которое теперь проверяется, а не скрывается: канвас, "
                    "смешивающий текст с собственным массивом изображений, не может быть одним батчем, "
                    "потому что один батч сэмплирует ровно один массив. BuildFrame отклоняет его с "
                    "FontsNotMounted. Дальнейшее исправление — разбиение батчей по массивам "
                    "(отсортированные серии элементов, сгруппированные по массиву), что требует "
                    "контракта FrameOutput с несколькими батчами; запланировано после Release.",
                ),
            },
        ],
    },
    {
        "number": "30",
        "title": B("TTS: the main-thread contract and localized voice fallback",
                   "TTS: контракт главного потока и локализованный выбор голоса"),
        "blocks": [
            {
                "type": "p",
                "text": B(
                    "Speech is the one accessibility surface that cannot be simulated on a worker. SAPI 5 "
                    "creates an apartment-threaded COM object, AVSpeechSynthesizer owns a main-run-loop "
                    "object, and neither tolerates being built or driven from a JobSystem thread. The "
                    "kernel now enforces that in three layers: RequireMainThread() on every entry point, "
                    "FBY_ASSERT_MSG in an assert build, and a release fallback that logs and drops the "
                    "unsafe call. A simulation worker can still change the narrative locale; it only "
                    "records the pending language, which FlushSpeech() applies on the owner thread before "
                    "the next utterance.",
                    "Речь — единственная поверхность доступности, которую нельзя симулировать на воркере. "
                    "SAPI 5 создаёт COM-объект с apartment-моделью, AVSpeechSynthesizer владеет объектом "
                    "главного run loop, и ни один из них не терпит создания или управления из потока "
                    "JobSystem. Ядро теперь обеспечивает это тремя слоями: RequireMainThread() на каждой "
                    "точке входа, FBY_ASSERT_MSG в assert-сборке и release-фолбэк, который логирует и "
                    "отбрасывает небезопасный вызов. Воркер симуляции по-прежнему может сменить язык "
                    "повествования; он лишь запоминает отложенный язык, который FlushSpeech() применяет в "
                    "потоке-владельце перед следующей фразой.",
                ),
            },
            {
                "type": "table",
                "headers": [B("Entry point", "Точка входа"), B("Affinity", "Поток"),
                            B("Worker call", "Вызов из воркера")],
                "rows": [
                    [B("SapiTts::Initialize / AvSpeechTts::Initialize",
                       "SapiTts::Initialize / AvSpeechTts::Initialize"),
                     B("OS main thread / main run loop", "главный поток ОС / главный run loop"),
                     B("Returns false after RequireMainThread(); assert build aborts.",
                       "Возвращает false после RequireMainThread(); assert-сборка падает.")],
                    [B("SapiTts::SetVoice / AvSpeechTts::SetVoice",
                       "SapiTts::SetVoice / AvSpeechTts::SetVoice"),
                     B("OS main thread / main run loop", "главный поток ОС / главный run loop"),
                     B("Dropped with a log line; no COM/Objective-C call is issued.",
                       "Отбрасывается с записью в лог; вызова COM/Objective-C не происходит.")],
                    [B("SapiTts::Speak / Stop / SetRate", "SapiTts::Speak / Stop / SetRate"),
                     B("OS main thread", "главный поток ОС"),
                     B("Dropped; SAPI stays usable for the owner thread.",
                       "Отбрасывается; SAPI остаётся рабочим для потока-владельца.")],
                    [B("AccessibilityKernel::SetBlindMode", "AccessibilityKernel::SetBlindMode"),
                     B("OS main thread", "главный поток ОС"),
                     B("Returned before touching settings or attaching the backend.",
                       "Возврат до изменения настроек и подключения бэкенда.")],
                    [B("AccessibilityKernel::SetTtsBackend", "AccessibilityKernel::SetTtsBackend"),
                     B("OS main thread", "главный поток ОС"),
                     B("Returned; the previous backend stays installed.",
                       "Возврат; предыдущий бэкенд остаётся установленным.")],
                    [B("AccessibilityKernel::SetNarrativeLocale",
                       "AccessibilityKernel::SetNarrativeLocale"),
                     B("any thread", "любой поток"),
                     B("Records a pending language; setVoice() runs later in FlushSpeech().",
                       "Запоминает отложенный язык; setVoice() выполняется позже в FlushSpeech().")],
                    [B("AccessibilityKernel::FlushSpeech", "AccessibilityKernel::FlushSpeech"),
                     B("OS main thread", "главный поток ОС"),
                     B("Returns before the narrative log is touched, so no event is stolen.",
                       "Возврат до обращения к логу повествования, поэтому ни одно событие не крадётся.")],
                    [B("NativeTtsBackend", "NativeTtsBackend"),
                     B("OS main thread", "главный поток ОС"),
                     B("Returns nullptr; COM/AVFoundation is never initialized on the worker.",
                       "Возвращает nullptr; COM/AVFoundation никогда не инициализируется на воркере.")],
                ],
            },
            {
                "type": "p",
                "text": B(
                    "Voice selection is a locale search, not a token filter. The old implementation "
                    "mapped only the first letter of the tag to Language=409/419 and returned silently "
                    "for everything else, so a Russian narrative on an English OS kept an English voice "
                    "(or, after a failed filter, kept whatever was already installed). SetVoice() now "
                    "parses the BCP-47 primary language, resolves the full tag through LocaleNameToLCID, "
                    "enumerates every installed SAPI token and scores it.",
                    "Выбор голоса — это поиск по локали, а не фильтр токенов. Старая реализация "
                    "сопоставляла только первую букву тега с Language=409/419 и молча возвращалась для "
                    "всего остального, поэтому русское повествование на английской ОС сохраняло "
                    "английский голос (или после неудачного фильтра — вообще тот, что уже был). Теперь "
                    "SetVoice() разбирает основной язык BCP-47, разрешает полный тег через "
                    "LocaleNameToLCID, перебирает все установленные токены SAPI и оценивает их.",
                ),
            },
            {
                "type": "table",
                "headers": [B("Score", "Оценка"), B("Meaning", "Значение"),
                            B("Example", "Пример")],
                "rows": [
                    [B("4", "4"), B("Exact LCID match", "Точное совпадение LCID"),
                     B("requested ru-RU, installed ru-RU",
                       "запрошен ru-RU, установлен ru-RU")],
                    [B("3", "3"), B("Same language and same sublanguage/region",
                                    "Тот же язык и тот же субъязык/регион"),
                     B("requested ru-RU, installed ru (0x0419)",
                       "запрошен ru-RU, установлен ru (0x0419)")],
                    [B("2", "2"), B("Same primary language, different region",
                                    "Тот же основной язык, другой регион"),
                     B("requested en-GB, installed en-US",
                       "запрошен en-GB, установлен en-US")],
                    [B("0", "0"), B("No shared language -> system default voice",
                                    "Общего языка нет -> системный голос по умолчанию"),
                     B("requested ru-RU on an English-only OS",
                       "запрошен ru-RU на ОС только с английским")],
                ],
            },
            {
                "type": "code",
                "lang": "cpp",
                "text": "// AccessibilityKernel::SetBlindMode - main thread only.\n"
                        "if (!RequireMainThread(\"AccessibilityKernel::SetBlindMode\")) { return; }\n"
                        "MTtsBackend = NativeTtsBackend();   // SAPI COM / AVSpeech created here\n"
                        "ApplyTtsLanguage();                // setVoice on the owner thread\n"
                        "\n"
                        "// A worker that changes the locale never reaches the backend.\n"
                        "void AccessibilityKernel::SetNarrativeLocale(std::string_view locale) noexcept\n"
                        "{\n"
                        "    // ... update MTtsLanguage ...\n"
                        "    if (IsMainThread()) { ApplyTtsLanguage(); } else { MPendingVoiceLanguage = true; }\n"
                        "}\n"
                        "\n"
                        "// The owner thread applies it before draining the log.\n"
                        "void AccessibilityKernel::FlushSpeech() noexcept\n"
                        "{\n"
                        "    if (std::this_thread::get_id() != MMainThread) { return; }  // drop, do not steal\n"
                        "    if (MPendingVoiceLanguage) { ApplyTtsLanguage(); }\n"
                        "    // ... speak NarrativeEvents ...\n"
                        "}\n"
                        "\n"
                        "// SAPI fallback: score 4/3/2, else the system default. Never silent.\n"
                        "LogWarn(\"tts\", \"no installed voice matches '{}'; falling back to LCID 0x{:04X}\",\n"
                        "        tag, defaultLcid);",
            },
            {
                "type": "bullets",
                "items": [
                    B("The SAPI implementation no longer includes sphelper.h: it uses "
                      "ISpObjectTokenCategory directly, so the engine build does not depend on the ATL "
                      "component (a machine without atlbase.h still builds).",
                      "Реализация SAPI больше не включает sphelper.h: она использует "
                      "ISpObjectTokenCategory напрямую, поэтому сборка движка не зависит от компонента "
                      "ATL (машина без atlbase.h всё равно собирается)."),
                    B("AVSpeech enumerates [AVSpeechSynthesisVoice speechVoices], scores the language "
                      "prefix and stores the best installed language; the system default is the fallback.",
                      "AVSpeech перебирает [AVSpeechSynthesisVoice speechVoices], оценивает префикс языка "
                      "и сохраняет лучший установленный язык; системный по умолчанию — фолбэк."),
                    B("Every decision is logged once with the requested tag, the selected LCID/language and "
                      "whether it was a fallback, so a support ticket can say what the player actually "
                      "heard.",
                      "Каждое решение логируется один раз с запрошенным тегом, выбранным LCID/языком и "
                      "признаком фолбэка, поэтому в тикете поддержки видно, что игрок реально слышал."),
                ],
            },
            {
                "type": "note",
                "text": B(
                    "Debug builds compile FBY_ASSERT_MSG into every guard listed above. A call from the "
                    "wrong thread stops in the debugger with the exact function name instead of producing "
                    "an intermittent COM failure or a silent run loop corruption. Release builds keep the "
                    "guard semantics but not the abort: they log and refuse the call.",
                    "Debug-сборки компилируют FBY_ASSERT_MSG в каждую перечисленную проверку. Вызов не из "
                    "того потока останавливается в отладчике с точным именем функции вместо "
                    "плавающего отказа COM или тихой порчи run loop. Release-сборки сохраняют семантику "
                    "проверки, но не abort: они логируют и отказывают в вызове.",
                ),
            },
        ],
    },
    {
        "number": "31",
        "title": B("State-Driven Gameplay: FemAnimGraph, StateTree AI and the Animation Graph Canvas",
                   "Геймплей на состояниях: FemAnimGraph, ИИ StateTree и холст Animation Graph"),
        "blocks": [
            {
                "type": "p",
                "text": B(
                    "The high-level gameplay layer follows the same rule as the renderer: behaviour is a "
                    "function over plain data, not a graph of objects. FemAnimGraph is a flat animation "
                    "state machine and FemStateTree is a flat AI state tree. Neither one has a base class, "
                    "a virtual Evaluate(), a per-node heap allocation or a std::function callback. An "
                    "entity owns a fixed-size POD component; the system owns the shared tables (the "
                    "animation clips and the AI node/transition arrays) and walks only the active path.",
                    "Высокоуровневый геймплейный слой следует тому же правилу, что и рендерер: поведение — "
                    "это функция над простыми данными, а не граф объектов. FemAnimGraph — плоский автомат "
                    "анимаций, FemStateTree — плоское дерево состояний ИИ. Ни у одного нет базового класса, "
                    "виртуального Evaluate(), аллокации на узел или колбэка std::function. Сущность "
                    "владеет POD-компонентом фиксированного размера; система владеет общими таблицами "
                    "(клипами анимации и массивами узлов/переходов ИИ) и обходит только активный путь.",
                ),
            },
            {
                "type": "p",
                "text": B(
                    "Why not a Behaviour Tree: a BT re-walks its hierarchy from the root every tick, so the "
                    "cost of an idle NPC is proportional to the size of its tree. A State Tree stores the "
                    "active path once and re-evaluates one guard; transitions are a flat, trigger-indexed "
                    "array. The same reasoning applies to AnimGraph: transition conditions are bit tests and "
                    "float comparisons on the component, and the expensive part - the bone pose - is "
                    "blended once per tick, never per graph node.",
                    "Почему не Behaviour Tree: BT переобходит иерархию с корня каждый тик, поэтому цена "
                    "простаивающего NPC пропорциональна размеру дерева. State Tree хранит активный путь и "
                    "переоценивает одну проверку; переходы — плоский массив, индексируемый триггерами. Та "
                    "же логика у AnimGraph: условия переходов — это битовые тесты и сравнения float на "
                    "компоненте, а дорогая часть — поза костей — смешивается один раз за тик, а не на "
                    "каждый узел графа.",
                ),
            },
            {
                "type": "table",
                "headers": [B("AnimStateComponent field", "Поле AnimStateComponent"),
                            B("Type", "Тип"), B("Role", "Назначение")],
                "rows": [
                    [B("CurrentState", "CurrentState"), B("uint64_t", "uint64_t"),
                     B("Opaque logical state id. 0 = rest pose; gameplay owns the numbering.",
                       "Непрозрачный логический id состояния. 0 = rest-поза; нумерация принадлежит геймплею.")],
                    [B("PreviousState", "PreviousState"), B("uint64_t", "uint64_t"),
                     B("Source pose of the active cross-fade.",
                       "Исходная поза активного кросс-фейда.")],
                    [B("StateTime", "StateTime"), B("float", "float"),
                     B("Seconds in the current state; reset on every fired transition.",
                       "Секунды в текущем состоянии; сбрасывается при каждом переходе.")],
                    [B("TransitionCount", "TransitionCount"), B("uint32_t", "uint32_t"),
                     B("Diagnostics. Placed at offset 20 to fill the pad before the 8-byte-aligned "
                       "transition array.",
                       "Диагностика. Размещено по смещению 20, чтобы заполнить паддинг перед массивом "
                       "переходов с выравниванием 8.")],
                    [B("Transitions[8]", "Transitions[8]"), B("AnimTransition", "AnimTransition"),
                     B("Authored transitions, evaluated in array order; first match wins. Inert when "
                       "Conditions == AnimConditionNone.",
                       "Авторские переходы, перебираются по порядку; побеждает первое совпадение. Инертны "
                       "при Conditions == AnimConditionNone.")],
                    [B("Clips[8] / ClipCount", "Clips[8] / ClipCount"), B("AnimClip", "AnimClip"),
                     B("Per-entity clip table: bone keys plus the SpriteAnimation frame range for the same state.",
                       "Таблица клипов сущности: ключи костей плюс диапазон кадров SpriteAnimation для того же состояния.")],
                    [B("Floats[8]", "Floats[8]"), B("float", "float"),
                     B("Float parameters (speed, health, aim) read by the transition conditions.",
                       "Float-параметры (скорость, здоровье, прицел), читаемые условиями переходов.")],
                    [B("RestPose[128]", "RestPose[128]"), B("AnimRestBone", "AnimRestBone"),
                     B("Captured once from the skeleton; a clip only authors the bones it moves.",
                       "Снимается один раз со скелета; клип описывает только те кости, которые двигает.")],
                    [B("BlendDuration / BlendTime / BlendWeight",
                       "BlendDuration / BlendTime / BlendWeight"), B("float", "float"),
                     B("Cross-fade timing and the eased weight the SkeletalAnimation bridge applies.",
                       "Тайминг кросс-фейда и сглаженный вес, который применяет мост SkeletalAnimation.")],
                ],
            },
            {
                "type": "table",
                "headers": [B("Condition flag", "Флаг условия"), B("Evaluator", "Проверка")],
                "rows": [
                    [B("AnimConditionStateTimeAtLeast", "AnimConditionStateTimeAtLeast"),
                     B("StateTime >= StateTimeThreshold", "StateTime >= StateTimeThreshold")],
                    [B("AnimConditionStateTimeAtMost", "AnimConditionStateTimeAtMost"),
                     B("StateTime <= StateTimeThreshold", "StateTime <= StateTimeThreshold")],
                    [B("AnimConditionTagsAny", "AnimConditionTagsAny"),
                     B("(GameplayState.Bits & TagsAny) != 0",
                       "(GameplayState.Bits & TagsAny) != 0")],
                    [B("AnimConditionTagsAll", "AnimConditionTagsAll"),
                     B("(GameplayState.Bits & TagsAll) == TagsAll",
                       "(GameplayState.Bits & TagsAll) == TagsAll")],
                    [B("AnimConditionTagsNone", "AnimConditionTagsNone"),
                     B("(GameplayState.Bits & TagsAny) == 0",
                       "(GameplayState.Bits & TagsAny) == 0")],
                    [B("AnimConditionFloatAtLeast / AtMost", "AnimConditionFloatAtLeast / AtMost"),
                     B("Floats[FloatParam] compared with FloatThreshold; a non-finite value fails closed.",
                       "Floats[FloatParam] сравнивается с FloatThreshold; не-конечное значение даёт false.")],
                ],
            },
            {
                "type": "code",
                "lang": "cpp",
                "text": "// AnimGraph.hpp - the component prefix is stable and visible in an inspector.\n"
                        "struct AnimStateComponent\n"
                        "{\n"
                        "    std::uint64_t CurrentState = 0;   // 0  logical state, 0 = rest\n"
                        "    std::uint64_t PreviousState = 0;  // 8  cross-fade source\n"
                        "    float StateTime = 0.0f;           // 16 seconds in state\n"
                        "    std::uint32_t TransitionCount = 0;// 20 diagnostics / pad\n"
                        "    std::array<AnimTransition, 8> Transitions{};  // 24\n"
                        "    std::array<AnimClip, 8> Clips{};\n"
                        "    std::array<float, 8> Floats{};\n"
                        "    std::array<AnimRestBone, kMaxBones> RestPose{};\n"
                        "    float BlendDuration = 0.0f, BlendTime = 0.0f, BlendWeight = 1.0f;\n"
                        "    // ...\n"
                        "};\n"
                        "\n"
                        "// Once per fixed tick: conditions -> state -> two bridges.\n"
                        "for (auto [entity, state] : registry.view<AnimStateComponent>().each())\n"
                        "{\n"
                        "    state.StateTime += dt;\n"
                        "    const std::uint64_t tags = gameplayBits(registry, entity);\n"
                        "    for (auto& t : state.Transitions)\n"
                        "    {\n"
                        "        if (t.FromState != kAnimAnyState && t.FromState != state.CurrentState) continue;\n"
                        "        if (!AnimTransitionMatches(t, state, tags)) continue;\n"
                        "        state.PreviousState = state.CurrentState;\n"
                        "        state.CurrentState = t.ToState;\n"
                        "        state.StateTime = 0.0f;\n"
                        "        state.BlendDuration = t.BlendDuration;\n"
                        "        break;   // first match wins, at most 8 comparisons\n"
                        "    }\n"
                        "    // SkeletalAnimation: blend local TRS, then one hierarchy pass.\n"
                        "    if (auto* skeleton = registry.try_get<SkeletonComponent>(entity))\n"
                        "        AnimGraphApplySkeletal(state, *skeleton);\n"
                        "    // SpriteAnimation: only on a state change, so the frame advance is not fought.\n"
                        "    if (changed) AnimGraphApplySpriteClip(state, spriteAnimation, grid, spritePtr);\n"
                        "}",
            },
            {
                "type": "p",
                "text": B(
                    "The SkeletalAnimation bridge blends in local TRS space - shortest-arc rotation, linear position "
                    "and scale - and then calls SkeletalAnimation::Evaluate() exactly once. That single hierarchy pass "
                    "rebuilds globalMatrices and the Mat4 skinningMatrices palette that the GPU consumes. "
                    "Lerping two composed Mat4 palettes would be cheaper by one pass but produces shear on "
                    "a rotating parent/child chain and is not a rigid transform; the local blend keeps "
                    "every bone rigid. The SpriteAnimation bridge is the opposite trade-off: it is instantaneous by "
                    "design (startFrame, frameCount, duration, currentFrame = 0, UV applied in the same "
                    "tick), because a flipbook has no interpolatable pose.",
                    "Мост SkeletalAnimation смешивает в пространстве локальных TRS — кратчайшая дуга поворота, "
                    "линейные позиция и масштаб — и затем вызывает SkeletalAnimation::Evaluate() ровно один раз. Этот "
                    "единственный проход по иерархии перестраивает globalMatrices и палитру Mat4 "
                    "skinningMatrices, которую потребляет GPU. Лерп двух готовых палитр Mat4 был бы дешевле "
                    "на один проход, но даёт сдвиг (shear) на вращающейся цепочке родитель/ребёнок и не "
                    "является жёстким преобразованием; локальный блендинг сохраняет каждую кость жёсткой. "
                    "Мост SpriteAnimation — обратный компромисс: он мгновенный по замыслу (startFrame, frameCount, "
                    "длительность, currentFrame = 0, UV применяется в том же тике), потому что у флипбука "
                    "нет интерполируемой позы.",
                ),
            },
            {
                "type": "table",
                "headers": [B("StateTreeComponent field", "Поле StateTreeComponent"),
                            B("Type", "Тип"), B("Role", "Назначение")],
                "rows": [
                    [B("ActiveStates[4]", "ActiveStates[4]"), B("uint16_t", "uint16_t"),
                     B("Dense root-first path: [0] root, last = active leaf, unused = kStateTreeNone. "
                       "A State Tree never stores the whole node list per entity.",
                       "Плотный путь от корня: [0] корень, последний = активный лист, свободные = "
                       "kStateTreeNone. State Tree никогда не хранит весь список узлов на сущность.")],
                    [B("CurrentLeaf", "CurrentLeaf"), B("uint32_t", "uint32_t"),
                     B("Node index of the active leaf (also ActiveStates[ActiveDepth - 1]).",
                       "Индекс активного листа (совпадает с ActiveStates[ActiveDepth - 1]).")],
                    [B("ActiveDepth", "ActiveDepth"), B("uint32_t", "uint32_t"),
                     B("1..4; validated by StateTreeAddNode so BuildPath cannot overrun.",
                       "1..4; проверяется StateTreeAddNode, поэтому BuildPath не может выйти за границы.")],
                    [B("Triggers", "Triggers"), B("uint64_t", "uint64_t"),
                     B("Pending change triggers. 0 -> the transition table is not touched at all this tick. "
                       "Bit 63 is reserved for the internal leaf-guard-failed event.",
                       "Ожидающие триггеры изменений. 0 -> таблица переходов в этом тике не трогается "
                       "вообще. Бит 63 зарезервирован под внутреннее событие «guard листа провален».")],
                    [B("Tags", "Tags"), B("uint64_t", "uint64_t"),
                     B("Snapshot of GameplayState.Bits for this tick; every evaluator sees the same mask.",
                       "Снимок GameplayState.Bits на этот тик; все оценщики видят одну маску.")],
                    [B("Target / TargetPosition", "Target / TargetPosition"), B("Entity / Vector2D",
                                                                               "Entity / Vector2D"),
                     B("Blackboard: a live entity target, with a cached position fallback for patrol points.",
                       "Blackboard: живая цель-сущность плюс закэшированная позиция для точек патруля.")],
                    [B("Floats[4]", "Floats[4]"), B("float", "float"),
                     B("Float blackboard read by FloatBelow / FloatAbove (health, threat, ...).",
                       "Float-blackboard для FloatBelow / FloatAbove (здоровье, угроза, ...).")],
                    [B("StateTime / Ticks", "StateTime / Ticks"), B("float / uint32_t", "float / uint32_t"),
                     B("Time in the current leaf and fixed ticks since entry; reset by BuildPath.",
                       "Время в текущем листе и число фиксированных тиков с момента входа; сбрасываются "
                       "BuildPath.")],
                    [B("TreeId", "TreeId"), B("uint32_t", "uint32_t"),
                     B("Selects a StateTreeDefinition owned by the system; many entities share one tree.",
                       "Выбирает StateTreeDefinition, принадлежащее системе; много сущностей делят одно "
                       "дерево.")],
                ],
            },
            {
                "type": "table",
                "headers": [B("Flat table", "Плоская таблица"), B("Contents", "Содержимое"),
                            B("Cost per tick", "Цена за тик")],
                "rows": [
                    [B("StateTreeDefinition::Nodes[64]", "StateTreeDefinition::Nodes[64]"),
                     B("Root/Selector/Leaf kind, parent, intrusive child list, depth, evaluator, task.",
                       "Тип Root/Selector/Leaf, родитель, интрузивный список детей, глубина, оценщик, задача."),
                     B("Not walked in steady state.", "В устойчивом состоянии не обходится.")],
                    [B("StateTreeDefinition::Transitions[32]",
                       "StateTreeDefinition::Transitions[32]"),
                     B("FromLeaf, ToNode, TriggerMask, ConsumeMask, RequireTagsAll, ForbidTagsAny.",
                       "FromLeaf, ToNode, TriggerMask, ConsumeMask, RequireTagsAll, ForbidTagsAny."),
                     B("Scanned only when Triggers != 0.",
                       "Перебирается только при Triggers != 0.")],
                    [B("StateTreeEvaluator", "StateTreeEvaluator"),
                     B("Flat enum + uint64 tag mask + float threshold + float index, switched in one function.",
                       "Плоский enum + uint64-маска тегов + float-порог + индекс параметра, разбирается "
                       "одной функцией."),
                     B("One call for the active leaf.", "Один вызов для активного листа.")],
                    [B("StateTreeTask", "StateTreeTask"),
                     B("None / MoveToTarget / Stop / AttackTarget / SetTag / ClearTag.",
                       "None / MoveToTarget / Stop / AttackTarget / SetTag / ClearTag."),
                     B("One switch; movement and attack go through CommandBuffer.",
                       "Один switch; перемещение и атака идут через CommandBuffer.")],
                ],
            },
            {
                "type": "code",
                "lang": "cpp",
                "text": "// Authoring: a fixed table, no allocations, depth enforced at AddNode().\n"
                        "StateTreeDefinition tree; tree.TreeId = 7;\n"
                        "const auto root  = StateTreeAddNode(tree, StateTreeNodeKind::Root);\n"
                        "const auto group = StateTreeAddNode(tree, StateTreeNodeKind::Selector, root);\n"
                        "const auto attack = StateTreeAddNode(tree, StateTreeNodeKind::Leaf, group);\n"
                        "const auto move   = StateTreeAddNode(tree, StateTreeNodeKind::Leaf, group);\n"
                        "StateTreeSetLeaf(tree, attack,\n"
                        "                 StateTreeEvaluator{StateTreeEvaluatorKind::TargetWithinRadius, 0, 64.0f, 0},\n"
                        "                 StateTreeTask{StateTreeTaskKind::AttackTarget, 0, 0.0f, 48.0f, 0});\n"
                        "StateTreeSetLeaf(tree, move,\n"
                        "                 StateTreeEvaluator{StateTreeEvaluatorKind::AlwaysTrue, 0, 0.0f, 0},\n"
                        "                 StateTreeTask{StateTreeTaskKind::MoveToTarget, 0, 120.0f, 0.0f, 0});\n"
                        "StateTreeTransition spotted{};\n"
                        "spotted.FromLeaf = move;\n"
                        "spotted.ToNode = attack;\n"
                        "spotted.TriggerMask = 1ull << 0;   // \"player spotted\"\n"
                        "spotted.ConsumeMask = 1ull << 0;\n"
                        "StateTreeAddTransition(tree, spotted);\n"
                        "\n"
                        "// Runtime: the path is data, the transition table is consulted only on a trigger.\n"
                        "void StateTreeSystem::Run(ecs::SystemContext& ctx) const noexcept\n"
                        "{\n"
                        "    for (auto [entity, state] : ctx.registry.view<StateTreeComponent>().each())\n"
                        "    {\n"
                        "        const auto* tree = Tree(state.TreeId);\n"
                        "        if (!state.Active || tree == nullptr) continue;\n"
                        "        state.Tags = gameplayBits(ctx.registry, entity);\n"
                        "        ++state.Ticks; state.StateTime += ctx.dt;\n"
                        "        if (!state.Initialized) { BuildPath(*tree, SelectLeaf(...), state); }\n"
                        "        else if (!GuardPasses(state, *tree)) state.Triggers |= kStateTreeTriggerLeafFailed;\n"
                        "        if (state.Triggers != 0) TryTransitions(*tree, state);  // flat, <= 32 entries\n"
                        "        RunTask(ctx, entity, state, *tree);  // emits AICommandComponent\n"
                        "    }\n"
                        "}",
            },
            {
                "type": "bullets",
                "items": [
                    B("No inheritance, no virtual dispatch: StateTreeNodeKind, StateTreeEvaluatorKind and "
                      "StateTreeTaskKind are enums switched in one translation unit. A plugin adds a new "
                      "evaluator by extending the enum and the switch, not by subclassing a node.",
                      "Ни наследования, ни виртуального вызова: StateTreeNodeKind, StateTreeEvaluatorKind и "
                      "StateTreeTaskKind — enum, разбираемые в одной единице трансляции. Плагин добавляет "
                      "оценщик расширением enum и switch, а не подклассом узла."),
                    B("No per-frame tree walk: with no pending trigger the system reads one guard and runs "
                      "one task. A Behaviour Tree tick would touch every branch of the hierarchy.",
                      "Никакого переобхода дерева за кадр: без ожидающего триггера система читает один "
                      "guard и выполняет одну задачу. Тик Behaviour Tree затронул бы каждую ветвь иерархии."),
                    B("No runtime allocation: nodes, transitions, the active path and the command payloads "
                      "are fixed arrays; CommandBuffer copies the tiny AICommandComponent into the frame "
                      "arena and Play() applies it after the fixed steps.",
                      "Никаких рантайм-аллокаций: узлы, переходы, активный путь и payload команд — "
                      "фиксированные массивы; CommandBuffer копирует крошечный AICommandComponent в "
                      "кадровую арену, а Play() применяет его после фиксированных шагов."),
                    B("Determinism: Phase::FixedUpdate + fixed dt + insertion-order command replay. "
                      "Network rollback can reproduce the same ActiveStates path from the same triggers.",
                      "Детерминизм: Phase::FixedUpdate + фиксированный dt + воспроизведение команд в порядке "
                      "вставки. Сетевой rollback воспроизводит тот же путь ActiveStates из тех же триггеров."),
                ],
            },
            {
                "type": "p",
                "text": B(
                    "The runtime tables above are authored by AnimationGraphCanvasPanel, the ImNodes editor in "
                    "Source/Editor/Panels. The panel is intentionally not a second runtime model: its document is a "
                    "trivially copyable AnimationGraphDocument registered as one CommandStack external buffer. A "
                    "clip node compiles to AnimClip, a float-parameter node compiles to "
                    "AnimStateComponent::Floats, a direct clip-to-clip link compiles to AnimTransition, and a blend "
                    "node compiles to the AnimTransition of the Idle -> Attack style cross-fade. The runtime never "
                    "sees ImNodes, positions, names or the editor's blend topology.",
                    "Рантайм-таблицы выше создаются в AnimationGraphCanvasPanel — ImNodes-редакторе из "
                    "Source/Editor/Panels. Панель намеренно не является второй рантайм-моделью: её документ — "
                    "trivially copyable AnimationGraphDocument, зарегистрированный как один external-buffer "
                    "CommandStack. Узел клипа компилируется в AnimClip, узел float-параметра — в "
                    "AnimStateComponent::Floats, прямая связь клип-клип — в AnimTransition, а blend-узел — в "
                    "AnimTransition кросс-фейда вида Idle -> Attack. Рантайм никогда не видит ImNodes, позиции, "
                    "имена и редакторскую топологию блендинга.",
                ),
            },
            {
                "type": "table",
                "headers": [B("Editor node", "Узел редактора"), B("Pins", "Пины"),
                            B("Compiles to", "Во что компилируется")],
                "rows": [
                    [B("Clip", "Clip"), B("In, Out", "In, Out"),
                     B("AnimClip (State, Keys[8], SpriteAnimation start/count/duration/loop/play).",
                       "AnimClip (State, Keys[8], SpriteAnimation start/count/duration/loop/play).")],
                    [B("Float Parameter", "Float Parameter"), B("Float out", "Float out"),
                     B("AnimStateComponent::Floats[ParameterIndex] = ParameterValue; the index is also used by "
                       "AnimConditionFloatAtLeast/AtMost.",
                       "AnimStateComponent::Floats[ParameterIndex] = ParameterValue; индекс также используется "
                       "условиями AnimConditionFloatAtLeast/AtMost.")],
                    [B("Blend", "Blend"), B("Src, Mod, Out", "Src, Mod, Out"),
                     B("One AnimTransition from the source clip to the target clip; BlendDuration and the "
                       "condition bitmask live on the blend node, the optional Mod binding supplies FloatParam.",
                       "Один AnimTransition из исходного клипа в целевой; BlendDuration и битовая маска условий "
                       "хранятся в blend-узле, необязательная связь Mod задаёт FloatParam.")],
                ],
            },
            {
                "type": "table",
                "headers": [B("AnimationGraphDocument field", "Поле AnimationGraphDocument"),
                            B("Type / capacity", "Тип / ёмкость"), B("Role", "Назначение")],
                "rows": [
                    [B("GraphId", "GraphId"), B("uint32_t", "uint32_t"),
                     B("Identity used by the scene TOML block and by the PIE binding.",
                       "Идентификатор для блока TOML сцены и привязки PIE.")],
                    [B("NodeCount / Nodes[24]", "NodeCount / Nodes[24]"),
                     B("uint16_t / AnimationGraphNode", "uint16_t / AnimationGraphNode"),
                     B("Fixed union-like POD node: kind, enabled, name[32], position, AnimClip, AnimTransition "
                       "blend payload, parameter index/value.",
                       "Фиксированный POD-узел: kind, enabled, name[32], position, AnimClip, payload "
                       "AnimTransition для blend, индекс/значение параметра.")],
                    [B("LinkCount / Links[32]", "LinkCount / Links[32]"),
                     B("uint16_t / AnimationGraphLink", "uint16_t / AnimationGraphLink"),
                     B("Kind, endpoints, pins and the direct AnimTransition payload. The four link kinds are "
                       "Transition, BlendSource, BlendParameter and BlendTarget.",
                       "Kind, концы, пины и payload прямого AnimTransition. Четыре вида связи: Transition, "
                       "BlendSource, BlendParameter и BlendTarget.")],
                ],
            },
            {
                "type": "code",
                "lang": "cpp",
                "text": "// CompileGraph(): fixed storage, one flat AnimStateComponent, no allocation.\n"
                        "AnimStateComponent out;\n"
                        "std::array<bool, kMaxAnimParams> paramUsed{};\n"
                        "std::array<std::uint64_t, kMaxAnimClips> stateIds{};\n"
                        "std::uint16_t clipCount = 0, transitionCount = 0;\n"
                        "for (node : document.Nodes)\n"
                        "    if (node.Kind == Clip)\n"
                        "    {\n"
                        "        if (node.Clip.State == 0 || duplicate(stateIds, node.Clip.State)) reject();\n"
                        "        stateIds[clipCount] = node.Clip.State;\n"
                        "        out.Clips[clipCount++] = node.Clip;\n"
                        "    }\n"
                        "    else if (node.Kind == FloatParameter)\n"
                        "        out.Floats[node.ParameterIndex] = node.ParameterValue;\n"
                        "// Isolated-node check: every node needs at least one incident link.\n"
                        "for (node : document.Nodes) if (degree[node] == 0) reject(\"isolated\");\n"
                        "// Direct links become transitions; each Blend needs one source, one target\n"
                        "// and at most one parameter binding.\n"
                        "for (link : document.Links)\n"
                        "    if (link.Kind == Transition && conditionsValid(link.Transition))\n"
                        "        out.Transitions[transitionCount++] = link.Transition;\n"
                        "for (blend : document.Nodes)\n"
                        "    if (blend.Kind == Blend)\n"
                        "        out.Transitions[transitionCount++] = makeTransition(blend, source, target);\n"
                        "for (i = transitionCount; i < 8; ++i)\n"
                        "    out.Transitions[i].Enabled = false;   // inert slots, not garbage\n"
                        "out.ClipCount = clipCount;\n"
                        "out.TransitionCount = 0;                  // runtime diagnostic, not an authored count",
            },
            {
                "type": "p",
                "text": B(
                    "Every gesture is one CommandStack transaction. The document is a single external buffer, so "
                    "a node drag records only the changed position bytes and a condition toggle records only the "
                    "changed condition word. The ImNodes callbacks never call Record() directly: DrawCanvas "
                    "reports a state change, ProcessCanvasEvents opens BeginEdit(), the edit method mutates the "
                    "document, RecordEdit() captures the new bytes, and CommitEdit() commits one undo group. "
                    "Ctrl+Z/Ctrl+Y are handled by EditorSystem, which first commits any open canvas transaction and "
                    "then asks CommandStack to apply the inverse delta. Inspector edits use the Mirror-backed "
                    "AnimationGraphTransitionFieldsEqual()/_ClipFieldsEqual() helpers; both compare only the "
                    "FieldDesc byte ranges through reflect::FieldBytesEqual(), so padding bytes cannot create an "
                    "empty undo command.",
                    "Каждый жест — одна транзакция CommandStack. Документ является одним external buffer, поэтому "
                    "перетаскивание узла записывает только изменившиеся байты позиции, а переключение условия — "
                    "только изменившееся слово условий. Колбэки ImNodes никогда не вызывают Record() напрямую: "
                    "DrawCanvas сообщает об изменении, ProcessCanvasEvents открывает BeginEdit(), метод правки "
                    "меняет документ, RecordEdit() снимает новые байты, CommitEdit() фиксирует одну группу undo. "
                    "Ctrl+Z/Ctrl+Y обрабатывает EditorSystem: сначала завершает открытую транзакцию холста, затем "
                    "просит CommandStack применить обратную дельту. Правки инспектора используют Mirror-хелперы "
                    "AnimationGraphTransitionFieldsEqual()/_ClipFieldsEqual(); оба сравнивают только диапазоны "
                    "байтов FieldDesc через reflect::FieldBytesEqual(), поэтому паддинг не создаёт пустую "
                    "undo-команду.",
                ),
            },
            {
                "type": "table",
                "headers": [B("Condition bit", "Бит условия"), B("Inspector control", "Контрол инспектора"),
                            B("Runtime evaluator", "Рантайм-проверка")],
                "rows": [
                    [B("AnimConditionStateTimeAtLeast", "AnimConditionStateTimeAtLeast"),
                     B("StateTimeAtLeast checkbox + StateTimeThreshold", "Чекбокс StateTimeAtLeast + StateTimeThreshold"),
                     B("StateTime >= threshold", "StateTime >= threshold")],
                    [B("AnimConditionTagsAll", "AnimConditionTagsAll"),
                     B("TagsAll checkbox + TagsAll mask", "Чекбокс TagsAll + маска TagsAll"),
                     B("(tags & TagsAll) == TagsAll; empty mask fails closed",
                       "(tags & TagsAll) == TagsAll; пустая маска даёт false")],
                    [B("AnimConditionFloatAtMost", "AnimConditionFloatAtMost"),
                     B("FloatAtMost checkbox + FloatParam + FloatThreshold",
                       "Чекбокс FloatAtMost + FloatParam + FloatThreshold"),
                     B("Floats[FloatParam] <= FloatThreshold", "Floats[FloatParam] <= FloatThreshold")],
                ],
            },
            {
                "type": "code",
                "lang": "toml",
                "text": "# Appended by \"Compile Animations\"; EditorSystem re-appends it after Ctrl+S.\n"
                        "[anim_graph]\n"
                        "version = 1\n"
                        "graph_id = 12\n"
                        "node_count = 4\n"
                        "link_count = 4\n"
                        "\n"
                        "[[anim_graph.node]]\n"
                        "kind = 0                       # Clip\n"
                        "name = \"Idle\"\n"
                        "position = [-260.0, 40.0]\n"
                        "clip_state = \"0x0000000000000001\"\n"
                        "clip_key_count = 1\n"
                        "clip_sprite_count = 4\n"
                        "[[anim_graph.node.key]]\n"
                        "bone = 1\n"
                        "rotation = 0.5\n"
                        "position = [0.0, 0.0]\n"
                        "scale = 1.0\n"
                        "\n"
                        "[[anim_graph.node]]\n"
                        "kind = 2                       # Blend\n"
                        "blend_duration = 0.2\n"
                        "blend_conditions = 64          # FloatAtMost/AtLeast family\n"
                        "blend_float_param = 0\n"
                        "\n"
                        "[[anim_graph.link]]\n"
                        "kind = 0                       # Transition\n"
                        "from = 1\n"
                        "to = 0\n"
                        "t_conditions = 1               # StateTimeAtLeast\n"
                        "t_state_time = 1.0\n"
                        "t_duration = 0.15",
            },
            {
                "type": "p",
                "text": B(
                    "PIE debugging is a read-only projection of the live component back onto the authored document. "
                    "RefreshPIEState() finds the selected entity (EditorSelected), reads AnimStateComponent::"
                    "CurrentState/PreviousState and maps those 64-bit ids to Clip nodes; LastTransition maps through "
                    "the compiler's transition-to-link table to the exact authored link. The current node is drawn "
                    "with a pulsing neon outline and a ProgressBar for BlendWeight, the PreviousState is drawn in the "
                    "cross-fade source colour, and BlendTime/BlendWeight are shown both on the canvas and in the "
                    "Mirror inspector. No debug draw allocates; it only reads the component and writes ImGui draw "
                    "commands.",
                    "PIE-дебаг — это read-only проекция живого компонента обратно на авторский документ. "
                    "RefreshPIEState() находит выбранную сущность (EditorSelected), читает AnimStateComponent::"
                    "CurrentState/PreviousState и сопоставляет эти 64-битные id с узлами Clip; LastTransition "
                    "отображается через таблицу transition-to-link компилятора в точную авторскую связь. Текущий "
                    "узел рисуется пульсирующим неоновым контуром и ProgressBar для BlendWeight, PreviousState — "
                    "цветом источника кросс-фейда, а BlendTime/BlendWeight показываются и на холсте, и в "
                    "Mirror-инспекторе. Debug-draw ничего не аллоцирует: он только читает компонент и пишет "
                    "команды отрисовки ImGui.",
                ),
            },
            {
                "type": "note",
                "text": B(
                    "Both systems are render-agnostic: AnimStateComponent writes only skeleton/SpriteAnimation data and "
                    "StateTreeComponent writes only AI intent. The Metal backend now consumes the same 32-bit "
                    "DrawBatch contract as Vulkan - per-batch setFragmentTexture: and a vertex-buffer offset "
                    "instead of baseInstance - so the batch a State Tree or AnimGraph frame produces looks "
                    "identical on both APIs.",
                    "Обе системы не зависят от рендера: AnimStateComponent пишет только данные скелета/SpriteAnimation, "
                    "а StateTreeComponent — только намерения ИИ. Metal-бэкенд теперь потребляет тот же "
                    "32-битный контракт DrawBatch, что и Vulkan — per-batch setFragmentTexture: и смещение "
                    "вершинного буфера вместо baseInstance — поэтому батч, который производит кадр State Tree "
                    "или AnimGraph, выглядит одинаково на обоих API.",
                ),
            },
        ],
    },
    {
        "number": "32",
        "title": B(
            "Advanced AAA DOD Pipelines: Virtual Texturing, Component Change Masks, Relevance Grids, and Audio Buses",
            "Продвинутые AAA DOD-конвейеры: виртуальные текстуры, маски изменений компонентов, сетки релевантности и аудио-шины",
        ),
        "blocks": [
            {
                "type": "p",
                "text": B(
                    "Chapter 32 documents the four pipelines added to the Release build. They share one "
                    "constraint: a frame may not allocate, may not walk a scene graph and may not upload a "
                    "resource that did not change. VirtualTextureSystem turns the world into VFS-mapped chunk "
                    "files and a fixed 2048 texture array; ChangeMaskRenderExtract turns a 64-bit per-entity "
                    "word into coalesced SSBO ranges; RelevanceGrid turns a spatial mdspan into a per-player "
                    "snapshot filter; AudioZoneSystem turns a Box2D sensor volume into a lock-free bus filter "
                    "ramp. Each subsystem has a fixed-capacity data layout and a headless acceptance test.",
                    "Глава 31 описывает четыре конвейера, добавленные в сборку Release. Их объединяет одно "
                    "ограничение: кадр не должен выделять память, обходить граф сцены и загружать ресурс, "
                    "который не изменился. VirtualTextureSystem превращает мир в VFS-отображённые файлы "
                    "чанков и фиксированный texture array 2048; ChangeMaskRenderExtract превращает 64-битное "
                    "слово на сущность в слитые диапазоны SSBO; RelevanceGrid превращает пространственный "
                    "mdspan в фильтр снапшотов для игрока; AudioZoneSystem превращает объём Box2D-сенсора в "
                    "lock-free ramp параметров шины. У каждой подсистемы фиксированная раскладка данных и "
                    "headless-тест приёмки.",
                ),
            },
            {
                "type": "table",
                "headers": [
                    B("Pipeline", "Конвейер"),
                    B("Mutable state", "Изменяемое состояние"),
                    B("Bounded by", "Ограничено"),
                    B("Per-frame allocation", "Аллокация за кадр"),
                ],
                "rows": [
                    [B("VirtualTextureSystem", "VirtualTextureSystem"),
                     B("Page residency table + VFS mappings", "Таблица резидентности страниц + отображения VFS"),
                     B("layerCount x 32 x 32 pages", "layerCount x 32 x 32 страниц"), B("0", "0")],
                    [B("ChangeMaskRenderExtract", "ChangeMaskRenderExtract"),
                     B("Stable entity slots + sorted output cache", "Стабильные слоты сущностей + кэш сортировки"),
                     B("Capacity template parameter", "Параметр шаблона Capacity"), B("0", "0")],
                    [B("RelevanceGrid", "RelevanceGrid"),
                     B("mdspan cells + one 64-bit bitset per plan", "mdspan-ячейки + один 64-битный bitset на план"),
                     B("cellsX x cellsY <= 4096", "cellsX x cellsY <= 4096"), B("0", "0")],
                    [B("AudioZoneSystem", "AudioZoneSystem"),
                     B("One context record per listener", "Одна запись контекста на слушателя"),
                     B("kMaxZones = 64 authored zones", "kMaxZones = 64 авторизованных зон"), B("0", "0")],
                ],
            },
            {
                "type": "p",
                "text": B(
                    "31.1 Virtual texturing. The world address space is split into 64x64 tiles. Four tiles per "
                    "axis form one 256x256 RGBA8 chunk file, so a single VFS mapping supplies sixteen tile "
                    "uploads. The GPU owns a fixed 2048x2048 texture array with N layers; a layer is a 32x32 "
                    "page pool and every page is one tile. The resident bytes are therefore exactly",
                    "31.1 Виртуальные текстуры. Адресное пространство мира делится на тайлы 64x64. Четыре тайла "
                    "по оси образуют один chunk-файл 256x256 RGBA8, поэтому одно отображение VFS даёт шестнадцать "
                    "загрузок тайлов. GPU владеет фиксированным texture array 2048x2048 с N слоями; слой — это "
                    "пул из 32x32 страниц, и каждая страница — один тайл. Резидентный объём поэтому строго равен",
                ),
            },
            {
                "type": "code",
                "lang": "cpp",
                "text": "// Constant VRAM, independent of the camera path or the world size.\n"
                        "constexpr std::uint64_t kPageBytes = 2048ull * 2048ull * 4ull;\n"
                        "const std::uint64_t Budget = layerCount * kPageBytes;\n"
                        "\n"
                        "// Residency is a two-pass clock over a fixed page table.\n"
                        "void VirtualTextureSystem::Update(const VirtualTextureCamera& InCamera) noexcept\n"
                        "{\n"
                        "    ++MFrame;\n"
                        "    const float HalfX = InCamera.ViewSize.X * 0.5f / InCamera.Zoom;\n"
                        "    const float HalfY = InCamera.ViewSize.Y * 0.5f / InCamera.Zoom;\n"
                        "    const float X = std::fabs(std::cos(InCamera.Rotation));\n"
                        "    const float Y = std::fabs(std::sin(InCamera.Rotation));\n"
                        "    const float ExtentX = HalfX * X + HalfY * Y;   // rotated AABB\n"
                        "    const float ExtentY = HalfX * Y + HalfY * X;\n"
                        "    for (std::int64_t Ty = MinY; Ty <= MaxY; ++Ty)\n"
                        "        for (std::int64_t Tx = MinX; Tx <= MaxX; ++Tx)\n"
                        "        {\n"
                        "            const std::uint32_t Layer = LayerForTile(Tx, Ty);\n"
                        "            const std::uint32_t Hash = FindHash(Tx, Ty);\n"
                        "            if (Hash != kHashCapacity)\n"
                        "            {\n"
                        "                MSlots[Layer * 1024u + MHash[Hash].slot].lastUsedFrame = MFrame;\n"
                        "                continue;\n"
                        "            }\n"
                        "            std::uint32_t Slot = 0;\n"
                        "            if (LoadTile(Tx, Ty, Layer, &Slot))   // mmap chunk, upload one 64x64 tile\n"
                        "            {\n"
                        "                MSlots[Layer * 1024u + Slot].lastUsedFrame = MFrame;\n"
                        "                ++MStats.uploads;               // one vkCmdCopyBufferToImage\n"
                        "            }\n"
                        "        }\n"
                        "}",
            },
            {
                "type": "table",
                "headers": [B("Chunk header field", "Поле заголовка чанка"), B("Offset", "Смещение"),
                           B("Value", "Значение"), B("Validation", "Проверка")],
                "rows": [
                    [B("magic", "magic"), B("0", "0"), B("FBYVTC01", "FBYVTC01"),
                     B("Rejected before the mapping is indexed", "Отклоняется до индексации отображения")],
                    [B("version", "version"), B("8", "8"), B("1", "1"),
                     B("A future format fails closed", "Будущий формат отклоняется")],
                    [B("tileSize", "tileSize"), B("12", "12"), B("64", "64"),
                     B("Must equal the descriptor", "Должен совпадать с дескриптором")],
                    [B("chunkTiles", "chunkTiles"), B("16", "16"), B("4", "4"),
                     B("Must equal the descriptor", "Должен совпадать с дескриптором")],
                    [B("format", "format"), B("20", "20"), B("0 = RGBA8", "0 = RGBA8"),
                     B("No runtime decode", "Без декодирования в рантайме")],
                    [B("pixels", "pixels"), B("32", "32"), B("256 x 256 x 4", "256 x 256 x 4"),
                     B("Exact size check before subspan", "Точная проверка размера перед subspan")],
                ],
            },
            {
                "type": "bullets",
                "items": [
                    B("Eviction is a per-layer clock. A page whose lastUsedFrame is the current frame is visible "
                      "and is never a victim; if all 1024 pages of a pool are visible the request is counted as a "
                      "spill instead of corrupting a tile that is on screen.",
                      "Вытеснение — часовой алгоритм на слой. Страница, у которой lastUsedFrame равен текущему "
                      "кадру, видима и никогда не становится жертвой; если все 1024 страницы пула видимы, запрос "
                      "считается spill вместо порчи тайла, находящегося на экране."),
                    B("The resident lookup is open addressing with tombstones. A hit costs one hash probe and a "
                      "store into lastUsedFrame; a chunk mapping is never re-read for a resident tile.",
                      "Поиск резидентности — открытая адресация с tombstone. Попадание стоит одного зонда и записи "
                      "lastUsedFrame; отображение чанка никогда не перечитывается для резидентного тайла."),
                    B("VirtualTextureSystem itself is about 2 MiB (page table plus the 65536-entry hash). It is a "
                      "startup object, allocated once on the heap, not a stack local in a frame function.",
                      "Сам VirtualTextureSystem занимает около 2 МиБ (таблица страниц и хеш на 65536 записей). Это "
                      "стартовый объект, один раз выделяемый в куче, а не стековая переменная кадровой функции."),
                ],
            },
            {
                "type": "p",
                "text": B(
                    "31.2 Component change masks. A render-visible entity carries ComponentChangeMask next to "
                    "Transform2D/Sprite2D. A mutating system calls change::MarkTransform(), MarkSprite() or "
                    "MarkDrawOrder(); the mark is one atomic fetch_or on the 64-bit word. The extract peeks the "
                    "word, consumes it with fetch_and(0), and recomputes a RenderItem only when one of the three "
                    "render bits was present. An entity with a zero mask reaches one pointer lookup and one "
                    "relaxed load - no Transform read, no Sprite copy, no RenderKey arithmetic.",
                    "31.2 Маски изменений компонентов. Видимая для рендера сущность несёт ComponentChangeMask "
                    "рядом с Transform2D/Sprite2D. Система, меняющая данные, вызывает change::MarkTransform(), "
                    "MarkSprite() или MarkDrawOrder(); отметка — один атомарный fetch_or по 64-битному слову. "
                    "Extract читает слово, потребляет его через fetch_and(0) и пересчитывает RenderItem только "
                    "если присутствовал один из трёх рендер-битов. Сущность с нулевой маской стоит одного поиска "
                    "указателя и одной relaxed-загрузки — без чтения Transform, копирования Sprite и "
                    "вычисления RenderKey.",
                ),
            },
            {
                "type": "table",
                "headers": [B("Bit", "Бит"), B("Name", "Имя"), B("Consumer", "Потребитель"),
                           B("Effect", "Эффект")],
                "rows": [
                    [B("0", "0"), B("Transform2D", "Transform2D"), B("ChangeMaskRenderExtract", "ChangeMaskRenderExtract"),
                     B("Rebuild position/scale/rotation; key unchanged", "Пересчёт position/scale/rotation; ключ не меняется")],
                    [B("1", "1"), B("Sprite2D", "Sprite2D"), B("ChangeMaskRenderExtract", "ChangeMaskRenderExtract"),
                     B("Rebuild UV/size/colour/layer", "Пересчёт UV/size/colour/layer")],
                    [B("2", "2"), B("DrawOrder", "DrawOrder"), B("ChangeMaskRenderExtract", "ChangeMaskRenderExtract"),
                     B("RenderKey may move: structural rebuild", "RenderKey может измениться: структурная пересборка")],
                    [B("3-9", "3-9"), B("Velocity/GameplayState/Animation/Audio/Physics/Lifetime/Custom",
                                        "Velocity/GameplayState/Animation/Audio/Physics/Lifetime/Custom"),
                     B("Gameplay systems", "Игровые системы"),
                     B("Consumed by the owner; the GPU sees nothing", "Потребляется владельцем; GPU ничего не видит")],
                    [B("16-63", "16-63"), B("GameBitBase + n", "GameBitBase + n"), B("Game code", "Игровой код"),
                     B("Reserved for project aliases", "Зарезервировано под алиасы проекта")],
                ],
            },
            {
                "type": "code",
                "lang": "cpp",
                "text": "struct ComponentChangeMask          // 16 bytes, align 8\n"
                        "{\n"
                        "    ChangeMask   Bits      = 0;     // offset 0, atomic_ref target\n"
                        "    std::uint32_t LastFrame = 0;    // offset 8, diagnostic only\n"
                        "    std::uint32_t Reserved  = 0;    // offset 12, zero\n"
                        "};\n"
                        "static_assert(sizeof(ComponentChangeMask) == 16);\n"
                        "\n"
                        "// A system marks after the write, not before: the extract must never see a bit\n"
                        "// for a value it has not finished writing.\n"
                        "transform.Position = NewPosition;\n"
                        "Fby::Change::MarkTransform(registry, entity, frameIndex);\n"
                        "\n"
                        "// Extract: static items fall through after one relaxed load.\n"
                        "for (auto [e, mask, transform, sprite] : view.each()) {\n"
                        "    const ChangeMask Bits = change::Peek(mask);\n"
                        "    if (Bits == 0 && SlotKnown(e)) { ++stats.skippedStatic; continue; }\n"
                        "    const ChangeMask Hit = change::Consume(mask);\n"
                        "    if ((Hit & kRenderBits) == 0) { continue; }\n"
                        "    BuildItem(registry, e, &items[slot], &keys[slot]);\n"
                        "}",
            },
            {
                "type": "code",
                "lang": "cpp",
                "text": "// Output-relative dirty ranges coalesced in one sorted pass.\n"
                        "struct DirtyRange { std::uint32_t first; std::uint32_t count; };\n"
                        "static_assert(sizeof(DirtyRange) == 8);\n"
                        "\n"
                        "// Transform-only edit: key unchanged, order unchanged, patch one output item.\n"
                        "MOutputItems[MOutputIndex[slot]] = MItems[slot];\n"
                        "MChangedScratch[MChangedCount++] = MOutputIndex[slot];\n"
                        "// ... sort + coalesce into 1..N ranges; the backend uploads only those bytes.\n"
                        "\n"
                        "// DrawOrder edit: the item can move in the sort. The safe upload is the whole\n"
                        "// sorted range, because firstItem in every DrawBatch may have shifted.\n"
                        "if (OldKey != NewKey) { RebuildOutput(); return DirtyRange{0, MOutputCount}; }",
            },
            {
                "type": "p",
                "text": B(
                    "FillPacket() publishes the same ranges as FramePacket::instanceRanges and sets "
                    "instanceRangesAreDeltas. VulkanRHI copies only those ranges into its persistent per-frame "
                    "instance slice once that slice has received one full image; a first use, a resize or a "
                    "structural rebuild falls back to the full copy. MetalRHI keeps the full-copy fallback, so the "
                    "packet contract is portable while the optimisation is explicit.",
                    "FillPacket() публикует те же диапазоны как FramePacket::instanceRanges и выставляет "
                    "instanceRangesAreDeltas. VulkanRHI копирует только эти диапазоны в постоянный срез "
                    "instance-буфера кадра после того, как срез получил один полный образ; первое использование, "
                    "resize или структурная пересборка откатываются к полному копированию. MetalRHI сохраняет "
                    "полное копирование как fallback, поэтому контракт пакета переносим, а оптимизация явная.",
                ),
            },
            {
                "type": "bullets",
                "items": [
                    B("No allocation: every slot, key, batch, dirty range and lookup bucket lives inside the "
                      "Capacity-templated object. The default 4096 covers a large 2D scene; a streaming world "
                      "instantiates ChangeMaskRenderExtract<1u << 18> without changing a line of API.",
                      "Без аллокаций: каждый слот, ключ, батч, диапазон и бакет поиска живут внутри объекта, "
                      "параметризованного Capacity. Значение по умолчанию 4096 покрывает большую 2D-сцену; "
                      "стриминговый мир инстанцирует ChangeMaskRenderExtract<1u << 18> без изменения API."),
                    B("The extract exposes both arrays: Items()/Keys()/Batches() fill the existing FramePacket, "
                      "while DirtyRanges() is the upload plan for a backend that owns a persistent instance SSBO. "
                      "A zero-mask frame with no structural change produces an empty range list.",
                      "Extract отдаёт оба представления: Items()/Keys()/Batches() заполняют существующий "
                      "FramePacket, а DirtyRanges() — план загрузки для бэкенда с постоянным instance SSBO. Кадр "
                      "без изменений и без структурных правок даёт пустой список диапазонов."),
                    B("A layer change is deliberately treated as structural even when the float depth did not "
                      "move: batching groups by the top 16 RenderKey bits, so a stable order is a correctness "
                      "condition, not an optimisation.",
                      "Смена слоя намеренно считается структурной, даже если float-глубина не изменилась: "
                      "батчинг группирует по старшим 16 битам RenderKey, поэтому стабильный порядок — условие "
                      "корректности, а не оптимизация."),
                ],
            },
            {
                "type": "p",
                "text": B(
                    "31.3 Network relevance grid. NetRollbackSession owns the stable NetEntityId map; "
                    "RelevanceGrid owns an mdspan of cells. The grid is rebuilt once per tick: entities are "
                    "sorted by NetEntityId, their Transform2D position selects a cell, and each cell appends a "
                    "fixed number of ids. The plan for one player walks only the square of cells inside "
                    "farRadius. Current and neighbouring cells are immediate; the near ring is gated by "
                    "nearInterval; the far ring by farInterval. The phase is NetEntityId::value % interval, so "
                    "distant objects do not all update on the same tick.",
                    "31.3 Сетка сетевой релевантности. NetRollbackSession владеет стабильной картой NetEntityId; "
                    "RelevanceGrid владеет mdspan ячеек. Сетка пересобирается раз в тик: сущности сортируются по "
                    "NetEntityId, позиция Transform2D выбирает ячейку, и каждая ячейка добавляет фиксированное "
                    "число id. План для игрока обходит только квадрат ячеек внутри farRadius. Текущая и соседние "
                    "ячейки — немедленные; ближнее кольцо ограничено nearInterval; дальнее — farInterval. Фаза "
                    "равна NetEntityId::value % interval, поэтому дальние объекты не обновляются все в один тик.",
                ),
            },
            {
                "type": "table",
                "headers": [B("Band", "Полоса"), B("Chebyshev distance", "Расстояние Чебышёва"),
                           B("Cadence", "Частота"), B("Typical payload", "Типичный payload")],
                "rows": [
                    [B("Immediate", "Немедленная"), B("0..neighborRadius (default 1)", "0..neighborRadius (по умолчанию 1)"),
                     B("every tick", "каждый тик"), B("player, nearby actors, doors", "игрок, ближние актёры, двери")],
                    [B("Near", "Ближняя"), B("neighborRadius+1..nearRadius", "neighborRadius+1..nearRadius"),
                     B("every nearInterval ticks", "каждые nearInterval тиков"),
                     B("mid-distance props", "средние объекты")],
                    [B("Far", "Дальняя"), B("nearRadius+1..farRadius", "nearRadius+1..farRadius"),
                     B("every farInterval ticks", "каждые farInterval тиков"),
                     B("ambient movers, VFX anchors", "фоновые движущиеся объекты, якоря VFX")],
                    [B("Outer", "Внешняя"), B("> farRadius", "> farRadius"), B("never", "никогда"),
                     B("not in the plan", "нет в плане")],
                ],
            },
            {
                "type": "code",
                "lang": "cpp",
                "text": "// The cell grid is an mdspan over one flat vector<RelevanceCell>.\n"
                        "std::mdspan<RelevanceCell, std::dextents<std::size_t, 2>>\n"
                        "    Grid(MCells.data(), MConfig.cellsY, MConfig.cellsX);\n"
                        "\n"
                        "// Rebuild once per tick, deterministically sorted by NetEntityId.\n"
                        "InEntities.ForEach([&](NetEntityId Net, Entity Local) noexcept {\n"
                        "    MSortScratch[Count++] = SortableEntity{Net.Value, Local};\n"
                        "});\n"
                        "std::sort(MSortScratch.begin(), MSortScratch.begin() + Count,\n"
                        "          [](const auto& A, const auto& B) { return A.id < B.id; });\n"
                        "for (const SortableEntity& E : span(MSortScratch.data(), Count)) {\n"
                        "    const Transform2D& T = InRegistry.get<Transform2D>(E.local);\n"
                        "    Grid[CellY(T.Position), CellX(T.Position)].Append(NetEntityId{E.id});\n"
                        "}\n"
                        "\n"
                        "// Plan: walk only cells inside farRadius; the bitset is O(1) membership.\n"
                        "const RelevanceBand Band = BandFor(std::abs(Cx - PlayerCx), std::abs(Cy - PlayerCy));\n"
                        "const std::uint32_t Interval = Band == RelevanceBand::Immediate ? 1u\n"
                        "    : (Band == RelevanceBand::Near ? Config.nearInterval : Config.farInterval);\n"
                        "if (!IsDue(Interval, Id, Tick)) { continue; }\n"
                        "OutPlan.items[OutPlan.count++] = RelevanceEntity{Id, Cx, Cy, Band};\n"
                        "OutPlan.Set(Id);",
            },
            {
                "type": "code",
                "lang": "cpp",
                "text": "// Filtering is a record copy, not a registry walk. The output is a valid\n"
                        "// Snapshot v4 delta against the same base: checksum and counts are rebuilt.\n"
                        "for (std::uint32_t R = 0; R < View.header.entityCount; ++R) {\n"
                        "    const FilteredRecordHeader& Rec = ReadRecord(View.payload, Offset);\n"
                        "    const bool Removed = (Rec.Flags & SnapshotEntityRemoved) != 0;\n"
                        "    const NetEntityId Net = MEntities.IdOf(Rec.entity);\n"
                        "    const bool Keep = Removed ? InKeepRemovals : InPlan.Contains(Net);\n"
                        "    if (Keep) { CopyRecord(Rec, Out); ++Out.header.entityCount; }\n"
                        "}\n"
                        "Out.header.checksum = Fnv1a(OutPayload);\n"
                        "// Client: RestoreDelta(base, filtered) - each record still restores the\n"
                        "// component bytes that the server captured for that tick.",
            },
            {
                "type": "table",
                "headers": [B("RelevancePlan field", "Поле RelevancePlan"), B("Size", "Размер"),
                           B("Purpose", "Назначение")],
                "rows": [
                    [B("player, tick", "player, tick"), B("16 B", "16 Б"),
                     B("Identifies the wire view", "Идентифицирует представление для отправки")],
                    [B("items[4096]", "items[4096]"), B("64 KiB", "64 КиБ"),
                     B("Sorted RelevanceEntity records", "Отсортированные записи RelevanceEntity")],
                    [B("bits[64]", "bits[64]"), B("512 B", "512 Б"),
                     B("One bit per NetEntityId for O(1) delta filtering", "Один бит на NetEntityId для O(1)-фильтрации")],
                    [B("immediate/near/far counts", "счётчики immediate/near/far"), B("12 B", "12 Б"),
                     B("Diagnostics and bandwidth budgeting", "Диагностика и бюджет трафика")],
                ],
            },
            {
                "type": "p",
                "text": B(
                    "31.4 Hierarchical audio buses. AudioZoneComponent is the authored Box2D sensor volume: an "
                    "AABB copied from the shape, a stable zoneId, a priority, a bus mask and the two target "
                    "parameters ReverbGain and LowPassHz. AudioContextComponent is the listener's resolved state. "
                    "AudioZoneSystem picks the highest-priority containing zone, interpolates the context in "
                    "game time with a one-pole ramp and emits at most three fixed commands (Master, SFX, Music). "
                    "The audio thread then ramps the actual coefficient per sample inside ApplyFilter and the "
                    "reverb return gain inside ApplyReverb; neither function allocates, locks or calls into ECS.",
                    "31.4 Иерархические аудио-шины. AudioZoneComponent — авторизованный объём Box2D-сенсора: "
                    "AABB, скопированный из формы, стабильный zoneId, приоритет, маска шин и два целевых "
                    "параметра ReverbGain и LowPassHz. AudioContextComponent — разрешённое состояние слушателя. "
                    "AudioZoneSystem выбирает зону с наибольшим приоритетом, интерполирует контекст в игровом "
                    "времени однополюсным ramp и отправляет не более трёх фиксированных команд (Master, SFX, "
                    "Music). Затем аудиопоток плавно меняет коэффициент по сэмплам в ApplyFilter и gain возврата "
                    "реверба в ApplyReverb; ни одна из функций не аллоцирует, не блокирует и не обращается к ECS.",
                ),
            },
            {
                "type": "table",
                "headers": [B("Bus", "Шина"), B("Flag", "Флаг"), B("Zone parameter", "Параметр зоны"),
                           B("Audio-thread stage", "Стадия аудиопотока")],
                "rows": [
                    [B("Master", "Master"), B("bit 0", "бит 0"), B("ReverbGain, LowPassHz", "ReverbGain, LowPassHz"),
                     B("ApplyFilter -> ApplyReverb -> ApplyBusGain", "ApplyFilter -> ApplyReverb -> ApplyBusGain")],
                    [B("SFX", "SFX"), B("bit 1", "бит 1"), B("ReverbGain, LowPassHz", "ReverbGain, LowPassHz"),
                     B("Same chain, independently", "Та же цепочка, независимо")],
                    [B("Music", "Music"), B("bit 2", "бит 2"), B("ReverbGain, LowPassHz", "ReverbGain, LowPassHz"),
                     B("Same chain, independently", "Та же цепочка, независимо")],
                    [B("Cave (example)", "Cave (пример)"), B("Master+SFX", "Master+SFX"), B("0.45, 900 Hz", "0.45, 900 Гц"),
                     B("A 0.1 s context fade, then a sample ramp", "Затухание контекста 0.1 с, затем ramp по сэмплам")],
                    [B("Underwater (example)", "Underwater (пример)"), B("All", "Все"), B("0.20, 450 Hz", "0.20, 450 Гц"),
                     B("Priority 9 wins over Cave", "Приоритет 9 побеждает Cave")],
                ],
            },
            {
                "type": "code",
                "lang": "cpp",
                "text": "struct AudioZoneComponent                 // authored, trivially copyable\n"
                        "{\n"
                        "    Vector2D min, max;                  // Box2D sensor AABB, world units\n"
                        "    std::uint32_t zoneId, kind, priority, busMask, flags;\n"
                        "    float reverbGain;                   // 0..4, return gain\n"
                        "    float lowPassHz;                    // 20..20000 one-pole cutoff\n"
                        "    float fadeSeconds;                  // context cross-fade time\n"
                        "};\n"
                        "\n"
                        "struct AudioContextComponent             // resolved per listener\n"
                        "{\n"
                        "    std::uint32_t activeZoneId, previousZoneId, activeKind, flags;\n"
                        "    float blend, reverbGain, lowPassHz;\n"
                        "    float targetReverbGain, targetLowPassHz, fadeSeconds;\n"
                        "    Vector2D listenerPosition;\n"
                        "    float lastSentReverbGain, lastSentLowPassHz;\n"
                        "    std::uint32_t lastBusMask;\n"
                        "};\n"
                        "\n"
                        "// Zone system: retarget and ramp the game-side context.\n"
                        "const float Alpha = 1.0f - std::exp(-dt / Fade);\n"
                        "Context.reverbGain += (Context.targetReverbGain - Context.reverbGain) * Alpha;\n"
                        "Context.lowPassHz  += (Context.targetLowPassHz  - Context.lowPassHz)  * Alpha;\n"
                        "Context.blend = Saturate(Context.blend + dt / Fade);\n"
                        "Audio.SetBusZoneParameters(AudioBus::Sfx, Context.reverbGain, Context.lowPassHz, Fade);",
            },
            {
                "type": "code",
                "lang": "cpp",
                "text": "// Audio thread: coefficient and reverb gain are ramped per sample.\n"
                        "const float Start = ScopedCoefficient(CurrentCutoff);\n"
                        "const float End   = ScopedCoefficient(TargetCutoff);\n"
                        "for (std::uint32_t F = 0; F < Frames; ++F) {\n"
                        "    const float C = Start + (End - Start) * T(F);\n"
                        "    State[0] += C * (Buffer[F * 2 + 0] - State[0]);\n"
                        "    State[1] += C * (Buffer[F * 2 + 1] - State[1]);\n"
                        "    Buffer[F * 2 + 0] = State[0];\n"
                        "    Buffer[F * 2 + 1] = State[1];\n"
                        "}\n"
                        "// Reverb return: value = wet * wetParams * zoneGainRamp(F).\n"
                        "// Command ring: SetBusFilter, fixed size, no lock on the producer side.",
            },
            {
                "type": "note",
                "text": B(
                    "The four pipelines are covered by the headless advanced_pipelines test. It mounts a real "
                    "directory, writes real .fvtc chunks and verifies that a second view produces zero uploads; "
                    "it checks that a static entity produces an empty dirty range; it builds a real Snapshot v4 "
                    "delta and verifies that FilterDeltaForPlayer keeps one of two records; and it drives a "
                    "priority-9 underwater zone to 450 Hz and back to open. No GPU or audio device is opened, so "
                    "the test runs on a headless CI agent.",
                    "Четыре конвейера покрыты headless-тестом advanced_pipelines. Он монтирует реальный каталог, "
                    "пишет реальные .fvtc-чанки и проверяет, что повторный вид даёт ноль загрузок; проверяет, что "
                    "статичная сущность даёт пустой dirty range; строит реальную Snapshot v4 дельту и проверяет, "
                    "что FilterDeltaForPlayer сохраняет одну из двух записей; и прогоняет underwater-зону с "
                    "приоритетом 9 до 450 Гц и обратно до открытого фильтра. GPU и аудиоустройство не открываются, "
                    "поэтому тест работает на headless CI-агенте.",
                ),
            },
        ],
    },
    {
        "number": "33",
        "title": B(
            "Production Distribution: Encryption, LZMA Compression, and the Steam/EGS/GOG Release Checklist",
            "Продакшн-дистрибуция: шифрование, сжатие LZMA и чек-лист релиза Steam/EGS/GOG",
        ),
        "blocks": [
            {
                "type": "p",
                "text": B(
                    "Chapter 33 closes the distribution gap between the isolated console tools and the editor. "
                    "Before this chapter the tree had fby_fempak (cook, secure, list, extract), FemArchiver "
                    "(LZMA/FbyLZ plus ChaCha20-Poly1305/AES-256-GCM), FemCrypto, AotBuilder and the per-panel "
                    "'Cook AOT' action, but no single operation that ran them as one release. The ReleaseCooker "
                    "class now owns that operation: one Build & Pack Game button starts the native logic plugin, "
                    "the ordered asset cook, the deterministic .fempak writer and the encrypted .femarc wrapper, "
                    "then publishes progress to SceneDiagnosticsPanel.",
                    "Глава 32 закрывает разрыв между изолированными консольными утилитами и редактором. До неё в "
                    "дереве были fby_fempak (cook, secure, list, extract), FemArchiver (LZMA/FbyLZ и "
                    "ChaCha20-Poly1305/AES-256-GCM), FemCrypto, AotBuilder и кнопка 'Cook AOT' в отдельной панели, "
                    "но не было одной операции, запускающей всё это как релиз. Класс ReleaseCooker теперь владеет "
                    "этой операцией: одна кнопка Build & Pack Game запускает native-плагин логики, "
                    "упорядоченный кук ассетов, детерминированный .fempak и зашифрованный .femarc, а затем "
                    "публикует прогресс в SceneDiagnosticsPanel.",
                ),
            },
            {
                "type": "table",
                "headers": [B("Stage", "Стадия"), B("Command / class", "Команда / класс"),
                           B("Writes", "Пишет"), B("Failure contract", "Контракт ошибки")],
                "rows": [
                    [B("Key", "Ключ"), B("ReleaseCooker::PrepareKey", "ReleaseCooker::PrepareKey"),
                     B(".fbyrelease.seed, release.key.tmp, ReleaseKey.generated.hpp",
                       ".fbyrelease.seed, release.key.tmp, ReleaseKey.generated.hpp"),
                     B("No plaintext key in project.femproj or editor.ini", "Нет открытого ключа в project.femproj или editor.ini")],
                    [B("AOT", "AOT"), B("script::AotBuilder + forceIncludeHeader", "script::AotBuilder + forceIncludeHeader"),
                     B("logic_graph.cpp / .dll", "logic_graph.cpp / .dll"),
                     B("Skipped when no LogicGraph is bound, never faked", "Пропускается без LogicGraph, без имитации")],
                    [B("Cook", "Кук"), B("fby_fempak cook --progress", "fby_fempak cook --progress"),
                     B("assets.fempak + cook.progress", "assets.fempak + cook.progress"),
                     B("Non-zero child exit fails the build", "Ненулевой код дочернего процесса валит сборку")],
                    [B("Secure", "Защита"), B("fby_fempak secure --key-file --progress", "fby_fempak secure --key-file --progress"),
                     B("game.femarc", "game.femarc"),
                     B("AEAD tag failure or missing key fails closed", "Ошибка AEAD-тега или отсутствие ключа отклоняются")],
                    [B("Progress", "Прогресс"), B("CookProgressRecord -> ReleaseCookProgress", "CookProgressRecord -> ReleaseCookProgress"),
                     B("cook.progress / secure.progress", "cook.progress / secure.progress"),
                     B("Torn record rejected by double-read sequence check", "Разорванная запись отклоняется двойным чтением")],
                ],
            },
            {
                "type": "p",
                "text": B(
                    "31.1 ReleaseCooker. The class lives in Source/Editor/Infrastructure/ReleaseCooker.hpp and "
                    "links only the editor's existing services. Start() validates paths, creates the release tree, "
                    "copies the LogicGraph POD and starts one std::jthread. The worker calls AotBuilder, then the "
                    "cooker and the secure step, while the editor thread calls Progress() once per frame. The UI "
                    "copy is a fixed POD under a short mutex; the worker never calls ImGui and the editor never "
                    "blocks on the compiler or the compressor.",
                    "31.1 ReleaseCooker. Класс находится в Source/Editor/Infrastructure/ReleaseCooker.hpp и "
                    "связывает только уже существующие сервисы редактора. Start() проверяет пути, создаёт дерево "
                    "релиза, копирует POD LogicGraph и запускает один std::jthread. Worker вызывает AotBuilder, "
                    "затем кукер и secure-шаг, а поток редактора раз в кадр вызывает Progress(). Копия для UI — "
                    "фиксированный POD под коротким mutex; worker никогда не вызывает ImGui, а редактор не "
                    "блокируется на компиляторе или компрессоре.",
                ),
            },
            {
                "type": "code",
                "lang": "cpp",
                "text": "// One click from the toolbar: Build menu or the Viewport button.\n"
                        "ReleaseCookConfig Config = ReleaseCooker::FromProject(\n"
                        "    projectConfig.Paths(), projectConfig.Project(), EngineRoot, FempakPath);\n"
                        "Config.compileAot = visualScript.Initialized() && visualScript.Graph().nodeCount != 0;\n"
                        "char Error[256]{};\n"
                        "if (!releaseCooker.Start(Config, Graph, Error, sizeof(Error)))\n"
                        "{\n"
                        "    SetStatus(Error);\n"
                        "}\n"
                        "\n"
                        "// SceneDiagnosticsPanel every frame. No allocation while drawing.\n"
                        "releaseCooker.Poll();\n"
                        "sceneDiagnostics.SetReleaseProgress(releaseCooker.Progress());\n"
                        "\n"
                        "// The worker owns the sequence, in this order:\n"
                        "//   PrepareKey -> AotBuilder -> fby_fempak cook -> fby_fempak secure",
            },
            {
                "type": "p",
                "text": B(
                    "32.2 Key baking. The earlier secure command required --key or --key-env and the editor had no "
                    "caller. ReleaseCooker now reads a project-local 32-byte seed from .fbyrelease.seed (created "
                    "with SecureRng when absent), writes a build-scoped binary key file for fby_fempak, and "
                    "force-includes a generated header into the AOT translation unit. The header exports a C ABI "
                    "function that reconstructs the key from two XOR arrays; the plugin carries the key, while the "
                    "project file carries no password. The seed is the only persistent secret, and deleting it "
                    "rotates the release key on the next build.",
                    "32.2 Запекание ключа. Раньше secure требовал --key или --key-env, и в редакторе не было "
                    "вызывающего. Теперь ReleaseCooker читает проектный 32-байтный seed из .fbyrelease.seed "
                    "(создаётся через SecureRng при отсутствии), пишет build-scoped бинарный ключевой файл для "
                    "fby_fempak и force-include'ит сгенерированный заголовок в AOT-единицу трансляции. Заголовок "
                    "экспортирует C-ABI функцию, восстанавливающую ключ из двух XOR-массивов; ключ находится в "
                    "плагине, а в проектном файле нет пароля. Seed — единственный постоянный секрет; его удаление "
                    "меняет release-ключ на следующей сборке.",
                ),
            },
            {
                "type": "code",
                "lang": "cpp",
                "text": "// Generated into build/release/ReleaseKey.generated.hpp and force-included\n"
                        "// into the AOT plugin (clang-cl /FI, clang/g++ -include).\n"
                        "FBY_RELEASE_KEY_EXPORT bool fby_release_archive_key(std::uint8_t* OutKey,\n"
                        "                                                    std::uint32_t InCapacity) noexcept\n"
                        "{\n"
                        "    if (OutKey == nullptr || InCapacity < 32u) return false;\n"
                        "    constexpr std::uint8_t kMasked[32] = { /* key ^ mask */ };\n"
                        "    constexpr std::uint8_t kMask[32]   = { /* build mask */ };\n"
                        "    for (std::uint32_t i = 0; i < 32u; ++i)\n"
                        "        OutKey[i] = static_cast<std::uint8_t>(kMasked[i] ^ kMask[i]);\n"
                        "    return true;\n"
                        "}\n"
                        "\n"
                        "// A launcher can resolve the same plugin and call the export before\n"
                        "// opening game.femarc. No password is read from ini or the environment.",
            },
            {
                "type": "table",
                "headers": [B("File", "Файл"), B("Lifetime", "Время жизни"), B("Contains", "Содержит"),
                           B("On success", "При успехе")],
                "rows": [
                    [B(".fbyrelease.seed", ".fbyrelease.seed"), B("Persistent project secret", "Постоянный секрет проекта"),
                     B("32 random bytes", "32 случайных байта"), B("Kept, mode-restricted by the host", "Хранится, права задаёт хост")],
                    [B("release.key.tmp", "release.key.tmp"), B("One build only", "Только одна сборка"),
                     B("Plaintext AEAD key for fby_fempak", "Открытый AEAD-ключ для fby_fempak"),
                     B("Overwritten with zeroes and deleted", "Перезаписывается нулями и удаляется")],
                    [B("ReleaseKey.generated.hpp", "ReleaseKey.generated.hpp"), B("Compile only", "Только компиляция"),
                     B("Masked key + C ABI export", "Маскированный ключ + C-ABI экспорт"), B("Deleted", "Удаляется")],
                    [B("archives/game.femarc", "archives/game.femarc"), B("Shipped artifact", "Поставляемый артефакт"),
                     B("LZMA/FbyLZ + ChaCha20/AES body", "LZMA/FbyLZ + ChaCha20/AES тело"), B("Kept", "Хранится")],
                ],
            },
            {
                "type": "p",
                "text": B(
                    "32.2a Project-local output tree. A project created by the dashboard owns every artifact it "
                    "produces: build intermediates and the release key live in <root>/build/release, the canonical "
                    "archive is <root>/archives/game.femarc, and the per-store stages are "
                    "<root>/stores/{steam,epic,gog}. ReleaseCooker copies the archive and the AOT plugin into each "
                    "store directory after a successful build and writes a store.txt manifest (project, archive, "
                    "plugin, cipher, key_baked). Moving or deleting the project directory therefore moves the "
                    "build, the archive and all store copies together; nothing is written into the engine tree or "
                    "the process working directory.",
                    "32.2a Project-local дерево вывода. Проект, созданный дашбордом, владеет всеми своими "
                    "артефактами: промежуточные файлы сборки и release-ключ лежат в <root>/build/release, "
                    "канонический архив — <root>/archives/game.femarc, а staging для магазинов — "
                    "<root>/stores/{steam,epic,gog}. После успешной сборки ReleaseCooker копирует архив и "
                    "AOT-плагин в каждый store-каталог и пишет манифест store.txt (project, archive, plugin, "
                    "cipher, key_baked). Перенос или удаление каталога проекта переносит сборку, архив и все "
                    "store-копии вместе; ничего не пишется в дерево движка или рабочий каталог процесса.",
                ),
            },
            {
                "type": "p",
                "text": B(
                    "32.3 Progress protocol. fby_fempak writes a 424-byte CookProgressRecord to --progress. The "
                    "record is a POD: stage, completed/total, current asset, raw and stored bytes, elapsed "
                    "microseconds, bytes/second, cipher and flags. The writer seeks to zero, writes the whole "
                    "record and flushes after every update; ReleaseCooker reads it twice and accepts only a "
                    "matching sequence, so a partially written record can never produce a false 90% bar. "
                    "ArchiveBuildOptions carries a plain function-pointer progress callback, not std::function, "
                    "so FemArchiver can report per-entry compressed bytes inside its noexcept build.",
                    "32.3 Протокол прогресса. fby_fempak пишет 424-байтную CookProgressRecord в --progress. "
                    "Запись — POD: стадия, completed/total, текущий ассет, raw/stored байты, микросекунды, "
                    "байты/сек, шифр и флаги. Writer ищет нулевое смещение, пишет всю запись и делает flush после "
                    "каждого обновления; ReleaseCooker читает её дважды и принимает только совпадающую "
                    "последовательность, поэтому частично записанная запись не даёт ложные 90%. "
                    "ArchiveBuildOptions несёт обычный function-pointer колбэк прогресса, не std::function, "
                    "поэтому FemArchiver сообщает сжатые байты по записям внутри noexcept-сборки.",
                ),
            },
            {
                "type": "code",
                "lang": "cpp",
                "text": "struct CookProgressRecord                     // 424 bytes, ABI-stable\n"
                        "{\n"
                        "    char magic[8];                            // FBYCOOK1\n"
                        "    std::uint32_t version, sequence;\n"
                        "    std::uint32_t stage, completed, total, bytesPerSecond;\n"
                        "    std::uint32_t cipher, flags, reserved;\n"
                        "    std::uint64_t rawBytes, storedBytes, elapsedMicroseconds;\n"
                        "    char asset[192];\n"
                        "    char message[160];\n"
                        "};\n"
                        "\n"
                        "// SceneDiagnosticsPanel: the only UI work per frame.\n"
                        "ImGui::ProgressBar(Progress.Fraction(), ImVec2(-1.0f, 0.0f), Overlay);\n"
                        "ImGui::Text(\"asset: %s\", Progress.asset);\n"
                        "ImGui::Text(\"LZMA %.2f MB/s\", Progress.bytesPerSecond / (1024.0 * 1024.0));\n"
                        "ImGui::Text(\"encrypted: %s\", CookCipherName(Progress.cipher));",
            },
            {
                "type": "bullets",
                "items": [
                    B("The current asset is exact: fby_fempak updates the record before and after every cooked "
                      "file, so the bar shows the texture, audio or localization file being processed, not a "
                      "generic 'cooking' label.",
                      "Текущий ассет точен: fby_fempak обновляет запись до и после каждого кука, поэтому бар "
                      "показывает обрабатываемую текстуру, звук или локализацию, а не общую надпись 'cooking'."),
                    B("Encryption state is exact: cipher 1/2 and the encrypted flag are copied from the secure "
                      "build; a missing key or a rejected AEAD tag ends the stage as Failed, never Succeeded.",
                      "Состояние шифрования точно: cipher 1/2 и флаг encrypted копируются из secure-сборки; "
                      "отсутствующий ключ или отклонённый AEAD-тег завершают стадию как Failed, а не Succeeded."),
                    B("LZMA throughput is measured from the archiver's progress bytes and elapsed time. Because "
                      "the 7-Zip SDK call compresses one .fempak entry as a single operation, the rate is sampled "
                      "at the entry boundary rather than every 64 KiB; the UI labels the value as measured, not "
                      "predicted.",
                      "Скорость LZMA измеряется по байтам прогресса архиватора и прошедшему времени. Поскольку "
                      "вызов 7-Zip SDK сжимает одну запись .fempak целиком, скорость сэмплируется на границе "
                      "записи, а не каждые 64 КиБ; UI помечает значение как измеренное, а не предсказанное."),
                ],
            },
            {
                "type": "p",
                "text": B(
                    "32.4 Release checklist. The same artifacts feed Steam, Epic Games Store and GOG. The cook "
                    "tree is per-project and reproducible: a fixed seed and the deterministic .fempak writer make "
                    "the archive hash a build stamp; the .femarc keeps LZMA and AEAD settings in its header; the "
                    "AOT plugin records graph and component layout hashes. A store submission should attach the "
                    "hash of game.femarc, the plugin, the seed identifier (not the seed itself) and the "
                    "BuildProfile snapshot.",
                    "32.4 Чек-лист релиза. Одни и те же артефакты идут в Steam, Epic Games Store и GOG. Дерево "
                    "кука проектно-специфично и воспроизводимо: фиксированный seed и детерминированный писатель "
                    ".fempak делают хеш архива штампом сборки; .femarc хранит настройки LZMA и AEAD в заголовке; "
                    "AOT-плагин записывает хеши графа и раскладки компонентов. Для сабмита в магазин следует "
                    "приложить хеш game.femarc, плагин, идентификатор seed (не сам seed) и снимок BuildProfile.",
                ),
            },
            {
                "type": "table",
                "headers": [B("Store", "Магазин"), B("Required artifact", "Нужный артефакт"),
                           B("ReleaseCooker check", "Проверка ReleaseCooker"), B("Manual follow-up", "Ручной шаг")],
                "rows": [
                    [B("Steam", "Steam"), B("game.femarc + plugin + depot VDF", "game.femarc + плагин + depot VDF"),
                     B("LZMA/AEAD header, archive hash", "Заголовок LZMA/AEAD, хеш архива"),
                     B("SteamPipe upload, build id", "Загрузка SteamPipe, build id")],
                    [B("Epic Games Store", "Epic Games Store"), B("game.femarc + plugin + EOS manifest", "game.femarc + плагин + EOS manifest"),
                     B("same bytes, EOS artifact hash", "те же байты, хеш EOS-артефакта"), B("EOS BuildPatch, sandbox", "EOS BuildPatch, sandbox")],
                    [B("GOG Galaxy", "GOG Galaxy"), B("game.femarc + plugin + Galaxy shim", "game.femarc + плагин + Galaxy shim"),
                     B("same bytes, shim ABI", "те же байты, ABI шима"), B("Galaxy build, cloud saves", "Сборка Galaxy, облачные сохранения")],
                    [B("All stores", "Все магазины"), B("Seed identity, BuildProfile", "Идентификатор seed, BuildProfile"),
                     B("Graph hash + component LayoutHash", "Хеш графа + LayoutHash компонентов"),
                     B("Keep seed offline, rotate on leak", "Хранить seed офлайн, менять при утечке")],
                ],
            },
            {
                "type": "note",
                "text": B(
                    "The build orchestrator needs the fby_fempak executable in build/bin. If the editor was built "
                    "with FBY_BUILD_TOOLS=OFF, Start() fails before spawning anything and the status bar names "
                    "the missing packer. The AOT stage is optional by construction: a project with no bound "
                    "LogicGraph still cooks and secures its assets, and the result reports keyBaked=false.",
                    "Оркестратору сборки нужен исполняемый файл fby_fempak в build/bin. Если редактор собран с "
                    "FBY_BUILD_TOOLS=OFF, Start() завершается до запуска процессов, а статус-бар называет "
                    "отсутствующий упаковщик. Стадия AOT необязательна по конструкции: проект без привязанного "
                    "LogicGraph всё равно кукает и шифрует ассеты, а результат сообщает keyBaked=false.",
                ),
            },
        ],
    },
    {
        "number": "34",
        "title": B("SystemGraph: the parallel scheduling contract, then the blend-cycle contract",
                   "SystemGraph: контракт параллельного планирования, затем контракт циклов блендинга"),
        "blocks": [
            {
                "type": "p",
                "text": B(
                    "G8 changes SystemGraph::Run from a registration-order walk into a dependency-aware "
                    "dispatcher. Build() already folds every before edge into an after mask; it now also computes "
                    "a longest-path level per node. Two nodes on the same level cannot depend on each other, so a "
                    "level is a safe parallel wave. Run(phase) launches every enabled node of one wave through "
                    "JobSystem, leaves one node for the caller, and joins the wave through a counting_semaphore "
                    "that lives on the caller's stack. There is no std::function, no allocation and no shared "
                    "per-system semaphore; a 24-worker pool therefore executes 24 independent systems in the same "
                    "wave instead of 24 sequential calls.",
                    "G8 превращает SystemGraph::Run из обхода в порядке регистрации в диспетчер, знающий "
                    "зависимости. Build() уже сворачивает каждое before-ребро в after-маску; теперь он ещё "
                    "вычисляет longest-path уровень каждого узла. Два узла одного уровня не могут зависеть друг "
                    "от друга, поэтому уровень — безопасная параллельная волна. Run(phase) запускает все "
                    "включённые узлы волны через JobSystem, оставляет один узел вызывающему и дожидается волны "
                    "через counting_semaphore на стеке вызывающего. Здесь нет std::function, аллокаций и общего "
                    "семафора на систему; пул из 24 воркеров выполняет 24 независимые системы в одной волне, а не "
                    "24 последовательных вызова.",
                ),
            },
            {
                "type": "code",
                "lang": "cpp",
                "text": "// Build(): longest-path level, same-phase edges only.\n"
                        "for (Position in TopologicalOrder) {\n"
                        "    Node = Order[Position];\n"
                        "    Level = 0;\n"
                        "    for (Dep in DependencyMask(Node))\n"
                        "        if (Phase(Dep) == Phase(Node)) Level = max(Level, MLevels[Dep] + 1);\n"
                        "    MLevels[Node] = Level;\n"
                        "}\n"
                        "\n"
                        "// Run(Phase): one wave, one per-call semaphore.\n"
                        "std::counting_semaphore<kMaxGraphSystems> Done{0};\n"
                        "for (I = 1; I < WaveSize; ++I) {\n"
                        "    Wave[I].completed = &Done;\n"
                        "    Jobs.Submit(&InvokeGraphNode, &Wave[I], 0, 0);   // no allocation\n"
                        "}\n"
                        "Wave[0].run(Context, Wave[0].user);                    // caller keeps one node\n"
                        "for (I = 1; I < WaveSize; ++I) Done.acquire();          // join before next level",
            },
            {
                "type": "table",
                "headers": [B("Guarantee", "Гарантия"), B("Why it is enforced", "Почему это выполняется")],
                "rows": [
                    [B("A node never starts before every after/before dependency completed",
                       "Узел никогда не стартует раньше всех after/before зависимостей"),
                     B("Level = max(dep level) + 1; Run joins a wave before opening the next one",
                       "Level = max(level зависимости) + 1; Run дожидается волны до следующего уровня")],
                    [B("Nodes in one wave never depend on each other",
                       "Узлы одной волны не зависят друг от друга"),
                     B("A dependency edge always raises the dependent's level",
                       "Ребро зависимости всегда повышает уровень зависимого узла")],
                    [B("No worker thread is leaked or waited on twice",
                       "Ни один воркер не теряется и не ожидается дважды"),
                     B("One release per submitted thunk, one acquire per submission, semaphore is stack-local",
                       "Один release на отправленный thunk, один acquire на отправку, семафор на стеке")],
                    [B("Render stays on the render thread",
                       "Render остаётся на render-потоке"),
                     B("Run(Render) takes the sequential path: the RHI command stream is single-threaded",
                       "Run(Render) идёт последовательным путём: командный поток RHI однопоточный")],
                    [B("A mutation invalidates the schedule",
                       "Мутация инвалидирует расписание"),
                     B("Add/RemoveUser/Clear set MBuilt = false; Run does nothing until Build() succeeds",
                       "Add/RemoveUser/Clear ставят MBuilt = false; Run ничего не делает до успешного Build()")],
                ],
            },
            {
                "type": "note",
                "text": B(
                    "The dependency mask is the whole data-race contract. Two systems that write the same "
                    "component pool must have an edge; 'they happened to be registered in this order' is not a "
                    "dependency. The editor's FixedUpdate chain (events_clear -> movement -> physics -> "
                    "contacts -> spriteAnimation -> skeletalAnimation_animate) therefore declares five after bits, while independent "
                    "PostUpdate nodes (skeletalAnimation_evaluate, physics_interpolate, cloth collect) form one wave. "
                    "JobSystem is part of the fix: MPending and MRunning are now placed on separate 64-byte lines, "
                    "so submit/completion traffic from different cores does not bounce one cache line, and "
                    "ParallelFor uses a per-call semaphore instead of a shared member.",
                    "Маска зависимостей — это весь контракт гонок данных. Две системы, пишущие один пул "
                    "компонентов, обязаны иметь ребро; «они случайно зарегистрированы в таком порядке» — не "
                    "зависимость. Цепочка FixedUpdate редактора (events_clear -> movement -> physics -> contacts "
                    "-> spriteAnimation -> skeletalAnimation_animate) поэтому объявляет пять after-битов, а независимые узлы PostUpdate "
                    "(skeletalAnimation_evaluate, physics_interpolate, cloth collect) образуют одну волну. JobSystem — часть "
                    "фикса: MPending и MRunning разнесены на разные 64-байтные линии, поэтому трафик submit/"
                    "completion с разных ядер не трясёт одну кэш-линию, а ParallelFor использует семафор на вызов "
                    "вместо общего члена.",
                ),
            },
            {
                "type": "p",
                "text": B(
                    "CompileGraph() is not a serializer: it is the last place where an authored graph can be "
                    "rejected before AnimStateComponent becomes runtime state. The contract has two layers. "
                    "The first is local: a Clip node owns one non-zero state id, a Float node owns one unique "
                    "parameter index, a Blend node owns exactly one source link, exactly one target link and at "
                    "most one parameter link, and the source and target states must differ. The second is global: "
                    "the compiled transition graph must have no cycle that can fire forever without an exit.",
                    "CompileGraph() — не сериализатор, а последнее место, где авторский граф можно отвергнуть до "
                    "того, как AnimStateComponent станет рантайм-состоянием. Контракт двухслойный. Локальный: "
                    "Clip-узел владеет одним ненулевым state id, Float-узел — одним уникальным индексом "
                    "параметра, Blend-узел — ровно одним source-линком, ровно одним target-линком и не более "
                    "чем одним parameter-линком; состояния source и target обязаны различаться. Глобальный: в "
                    "скомпилированном графе переходов не должно быть цикла, который может срабатывать вечно без "
                    "выхода.",
                ),
            },
            {
                "type": "table",
                "headers": [B("Rule", "Правило"), B("Compiler result", "Результат компилятора"),
                           B("Authoring action", "Действие автора")],
                "rows": [
                    [B("Clip state is 0 or kAnimAnyState", "Состояние клипа равно 0 или kAnimAnyState"),
                     B("Rejected: needs a non-zero state id", "Отказ: needs a non-zero state id"),
                     B("Assign a unique 64-bit gameplay id", "Назначить уникальный 64-битный id")],
                    [B("Blend source state == target state", "Состояние source == состояние target"),
                     B("Rejected: cross-fades a state into itself", "Отказ: cross-fades a state into itself"),
                     B("Pick two different clips", "Выбрать два разных клипа")],
                    [B("Blend has 0 or 2 source/target links", "У бленда 0 или 2 source/target линка"),
                     B("Rejected: exactly one source, one target", "Отказ: exactly one source, one target"),
                     B("Delete the extra link", "Удалить лишнюю связь")],
                    [B("Cycle of hot transitions with no exit", "Цикл горячих переходов без выхода"),
                     B("Rejected: dead blend cycle without an exit", "Отказ: dead blend cycle without an exit"),
                     B("Add a gated exit or remove the loop", "Добавить gated-выход или убрать петлю")],
                    [B("Cycle with any enabled exit", "Цикл с любым включённым выходом"),
                     B("Accepted", "Принято"),
                     B("Keep the exit before the hot edge in array order", "Держать выход раньше горячего ребра")],
                ],
            },
            {
                "type": "p",
                "text": B(
                    "A transition is 'hot' when it is Enabled, has no tag/float condition, and its state-time "
                    "predicate is already true at StateTime == 0: StateTimeAtLeast with threshold <= 0, and/or "
                    "StateTimeAtMost with threshold >= 0. AnimGraphSystem resets StateTime to zero on every fired "
                    "transition and adds dt before evaluating the next tick, so a hot edge fires on every entry. "
                    "A hot cycle is a dead loop only when no enabled transition leaves the set of states in that "
                    "cycle; a gated exit keeps the graph legal even if the loop itself is hot.",
                    "Переход «горячий», если он Enabled, не имеет tag/float-условия и его StateTime-предикат уже "
                    "истинен при StateTime == 0: StateTimeAtLeast с порогом <= 0 и/или StateTimeAtMost с порогом "
                    ">= 0. AnimGraphSystem сбрасывает StateTime в ноль на каждом сработавшем переходе и добавляет "
                    "dt до вычисления следующего тика, поэтому горячее ребро срабатывает при каждом входе. "
                    "Горячий цикл — мёртвая петля только если ни один включённый переход не выходит из множества "
                    "его состояний; gated-выход сохраняет граф легальным, даже если сама петля горячая.",
                ),
            },
            {
                "type": "code",
                "lang": "cpp",
                "text": "// Iterative three-colour DFS. Bounded by kMaxAnimClips == 8; no recursion.\n"
                        "std::array<std::uint8_t, kMaxAnimClips> Color{};   // 0 white, 1 gray, 2 black\n"
                        "std::array<std::uint16_t, kMaxAnimClips> Stack{};\n"
                        "for (std::uint16_t Start = 0; Start < N; ++Start) {\n"
                        "    if (Color[Start] != 0) continue;\n"
                        "    std::int32_t Top = 0;\n"
                        "    Stack[0] = Start; Color[Start] = 1;\n"
                        "    while (Top >= 0) {\n"
                        "        const std::uint16_t V = Stack[Top];\n"
                        "        std::uint32_t& Edges = HotEdges[V];\n"
                        "        if (Edges == 0) { Color[V] = 2; --Top; continue; }\n"
                        "        std::uint16_t W = 0;\n"
                        "        while (((Edges >> W) & 1u) == 0) ++W;\n"
                        "        Edges &= ~(1u << W);\n"
                        "        if (Color[W] == 0) { Color[W] = 1; Stack[++Top] = W; continue; }\n"
                        "        if (Color[W] == 1) {\n"
                        "            // W is on the gray stack: reconstruct the cycle, test for an exit.\n"
                        "            if (!HasEnabledExit(Stack, Top, W)) {\n"
                        "                SetStatus(\"Anim compile: dead blend cycle without an exit: %s\", Path);\n"
                        "                LogLine(LogLevel::Warn, \"AnimGraph\", MStatus);\n"
                        "                return false;\n"
                        "            }\n"
                        "        }\n"
                        "    }\n"
                        "}",
            },
            {
                "type": "note",
                "text": B(
                    "The validator runs on the compiled edge set, not on the ImNodes document, so it sees exactly "
                    "what the runtime will evaluate: disabled links are already filtered by AnimTransitionMatches, "
                    "and state ids have been resolved from the links. The warning goes through LogLine (not "
                    "std::format), so the error path stays noexcept and LogBoy still receives the record. A "
                    "fully cyclic document is at most 8 vertices: the explicit stack cannot overflow, and the "
                    "worst case is 64 edge visits.",
                    "Валидатор работает по скомпилированному множеству рёбер, а не по документу ImNodes, поэтому "
                    "видит ровно то, что будет вычислять рантайм: выключенные линки уже отфильтрованы "
                    "AnimTransitionMatches, а state id разрешены из линков. Предупреждение идёт через LogLine "
                    "(не std::format), поэтому путь ошибки остаётся noexcept, и LogBoy всё равно получает запись. "
                    "Полностью циклический документ — максимум 8 вершин: явный стек переполнить нельзя, худший "
                    "случай — 64 прохода по рёбрам.",
                ),
            },
        ],
    },
    {
        "number": "35",
        "title": B("Box2D rollback teardown, then the PIE animation hand-off",
                   "Очистка Box2D при откате, затем PIE-передача анимации"),
        "blocks": [
            {
                "type": "p",
                "text": B(
                    "R1 is an ownership bug, not a Box2D bug. Registry::clear() destroys ECS objects; it cannot "
                    "know that RigidBody2D::body is a native b2BodyId, so the old bodies stay in the world while "
                    "the restored snapshot rows all carry body == 0. The next EnsureBodies()/Step() then creates a "
                    "second body for every entity. The fix is ordered: (1) ask PhysicsSystem to destroy every "
                    "tracked live body and sweep the registry for joint-link bodies it did not create; (2) clear "
                    "and restore the snapshot; (3) recreate bodies from the restored bytes. A body is destroyed "
                    "only after b2Body_IsValid, so a joint system that already tore its anchors down is safe.",
                    "R1 — ошибка владения, а не ошибка Box2D. Registry::clear() уничтожает ECS-объекты; он не "
                    "знает, что RigidBody2D::body — нативный b2BodyId, поэтому старые тела остаются в мире, а "
                    "восстановленные строки снапшота несут body == 0. Следующий EnsureBodies()/Step() создаёт "
                    "второе тело для каждой сущности. Фикс упорядочен: (1) попросить PhysicsSystem уничтожить "
                    "все отслеживаемые живые тела и просканировать реестр на тела joint-цепочек, созданные не им; "
                    "(2) очистить и восстановить снапшот; (3) пересоздать тела из восстановленных байтов. Тело "
                    "уничтожается только после b2Body_IsValid, поэтому joint-система, уже убравшая свои якоря, "
                    "безопасна.",
                ),
            },
            {
                "type": "code",
                "lang": "cpp",
                "text": "NetRollbackSession::Result NetRollbackSession::RestoreSnapshot(\n"
                        "    std::span<const std::byte> InSnapshot) noexcept\n"
                        "{\n"
                        "    if (!MInitialized || MRegistry == nullptr || MSchema == nullptr)\n"
                        "        return Result::NotInitialized;\n"
                        "\n"
                        "    // R1: native bodies die BEFORE Registry::clear().\n"
                        "    if (MPhysics != nullptr)\n"
                        "        (void)MPhysics->DestroyAllBodies(MRegistry);\n"
                        "\n"
                        "    if (!ecs::RestoreSnapshot(*MRegistry, *MSchema, InSnapshot, &MRng))\n"
                        "        return Result::RestoreFailed;\n"
                        "\n"
                        "    // Snapshot bytes never carry a b2 handle: rebuild from the restored rows.\n"
                        "    if (MPhysics != nullptr)\n"
                        "        MPhysics->EnsureBodies(*MRegistry);\n"
                        "    return Result::Ok;\n"
                        "}\n"
                        "\n"
                        "// PhysicsSystem::DestroyAllBodies: tracked pass + registry sweep.\n"
                        "for (Record : MBodies)\n"
                        "    if (b2Body_IsValid(Load(Record.body)))\n"
                        "        { b2DestroyBody(Load(Record.body)); ++Destroyed; }\n"
                        "MBodies.clear();\n"
                        "for (Body : Registry.view<RigidBody2D>())\n"
                        "    if (Body.body != 0) {\n"
                        "        if (b2Body_IsValid(Load(Body.body))) { b2DestroyBody(Load(Body.body)); ++Destroyed; }\n"
                        "        Body.body = 0;                       // no stale handle survives\n"
                        "    }\n"
                        "for (Collider : Registry.view<Collider2D>()) Collider.shape = 0;",
            },
            {
                "type": "table",
                "headers": [B("Restore phase", "Фаза restore"), B("Native state", "Нативное состояние"),
                           B("Invariant after the phase", "Инвариант после фазы")],
                "rows": [
                    [B("DestroyAllBodies(registry)", "DestroyAllBodies(registry)"),
                     B("Every valid tracked body + every registry body destroyed", "Все валидные tracked-тела + все тела реестра уничтожены"),
                     B("World contains no body owned by the old frame", "В мире нет тела, принадлежащего старому кадру")],
                    [B("Registry::clear() + snapshot decode", "Registry::clear() + декод снапшота"),
                     B("No native object is touched", "Нативные объекты не трогаются"),
                     B("Restored RigidBody2D rows have body == 0", "Восстановленные строки RigidBody2D имеют body == 0")],
                    [B("EnsureBodies(registry)", "EnsureBodies(registry)"),
                     B("One b2Body/b2Shape per restored row", "Одно b2Body/b2Shape на восстановленную строку"),
                     B("Body count equals the pre-rollback count, never twice it", "Число тел равно предроллбэковому, а не удвоенному")],
                    [B("Replay to the current tick", "Реплей до текущего тика"),
                     B("Step() finds body != 0 and skips creation", "Step() видит body != 0 и не создаёт тело"),
                     B("No duplicate physics on any replayed tick", "Никакого дублирования физики на реплейных тиках")],
                ],
            },
            {
                "type": "note",
                "text": B(
                    "Play-In-Editor uses the same rule at a larger granularity. The clone owns a separate "
                    "PhysicsSystem, so Play never inserts clone bodies into the editor world. Stop calls "
                    "DestroyAllBodies(clone) and then shuts that world down; the authoring scene was never mutated, "
                    "so Stop is a constant-time rollback with no orphan body. The same path is what makes the "
                    "toolbar Play/Pause/Stop honest: the buttons drive a real simulation, not a preview flag.",
                    "Play-In-Editor применяет то же правило на другом уровне. Клон владеет отдельным "
                    "PhysicsSystem, поэтому Play никогда не вставляет тела клона в мир редактора. Stop вызывает "
                    "DestroyAllBodies(clone), затем гасит этот мир; авторская сцена не менялась, поэтому Stop — "
                    "откат за константное время без осиротевших тел. Именно этот путь делает кнопки Play/Pause/"
                    "Stop честными: они запускают реальную симуляцию, а не флаг предпросмотра.",
                ),
            },
            {
                "type": "p",
                "text": B(
                    "The canvas needs six scalars per frame: CurrentState, PreviousState, BlendWeight, BlendTime, "
                    "StateTime and LastTransition. Reading them from a live AnimStateComponent on the ImGui thread "
                    "is not a benign race: SystemGraph workers may be inside AnimGraphSystem::Run, and the EnTT "
                    "lookup itself is unsafe while component pools are created or destroyed. Atomic reads of the "
                    "six fields would not fix the registry/lifetime part. The panel therefore owns a 64-byte POD "
                    "and a two-slot seqlock: the simulation thread publishes, the editor thread consumes, and the "
                    "live component is never dereferenced from Draw().",
                    "Холсту нужны шесть скаляров на кадр: CurrentState, PreviousState, BlendWeight, BlendTime, "
                    "StateTime и LastTransition. Чтение их из живого AnimStateComponent в потоке ImGui — не "
                    "безобидная гонка: воркеры SystemGraph могут находиться внутри AnimGraphSystem::Run, а сам "
                    "поиск в EnTT небезопасен, пока создаются/уничтожаются пулы компонентов. Атомарное чтение "
                    "шести полей не решило бы часть с реестром и временем жизни. Поэтому панель владеет "
                    "64-байтным POD и двумя слотами seqlock: поток симуляции публикует, поток редактора "
                    "потребляет, а живой компонент никогда не разыменовывается из Draw().",
                ),
            },
            {
                "type": "code",
                "lang": "cpp",
                "text": "struct PIEAnimFrame {                       // exactly 64 bytes\n"
                        "    std::uint64_t CurrentState = 0;\n"
                        "    std::uint64_t PreviousState = 0;\n"
                        "    float BlendWeight = 0.0f, BlendTime = 0.0f, StateTime = 0.0f;\n"
                        "    std::uint16_t LastTransition = kNoAnimTransition;\n"
                        "    std::array<std::byte, 34> Reserved{};\n"
                        "};\n"
                        "\n"
                        "// Producer (simulation thread, after the FixedUpdate join):\n"
                        "void PublishPIEState(const AnimStateComponent& S) noexcept {\n"
                        "    const std::uint32_t Write = MPieWriteSlot.load(std::memory_order_relaxed);\n"
                        "    PIEFrameSlot& Slot = MPieSlots[Write ^ 1u];\n"
                        "    const std::uint64_t Seq = Slot.Sequence.load(std::memory_order_relaxed);\n"
                        "    Slot.Sequence.store(Seq + 1u, std::memory_order_relaxed);\n"
                        "    std::atomic_thread_fence(std::memory_order_release);\n"
                        "    StoreRelaxed(Slot.Frame.CurrentState, S.CurrentState);\n"
                        "    StoreRelaxed(Slot.Frame.PreviousState, S.PreviousState);\n"
                        "    StoreRelaxed(Slot.Frame.BlendWeight, S.BlendWeight);\n"
                        "    StoreRelaxed(Slot.Frame.BlendTime, S.BlendTime);\n"
                        "    StoreRelaxed(Slot.Frame.StateTime, S.StateTime);\n"
                        "    StoreRelaxed(Slot.Frame.LastTransition, S.LastTransition);\n"
                        "    std::atomic_thread_fence(std::memory_order_release);\n"
                        "    Slot.Sequence.store(Seq + 2u, std::memory_order_release);\n"
                        "    MPieWriteSlot.store(Write ^ 1u, std::memory_order_release);\n"
                        "    MPiePublished.store(1u, std::memory_order_release);\n"
                        "}\n"
                        "\n"
                        "// Consumer (ImGui/Render thread): retry only if the sequence changed.\n"
                        "const std::uint64_t Before = Slot.Sequence.load(std::memory_order_acquire);\n"
                        "if ((Before & 1u) == 0) {\n"
                        "    ReadRelaxed(Frame.CurrentState, Slot.Frame.CurrentState);\n"
                        "    /* ... five more ... */\n"
                        "    std::atomic_thread_fence(std::memory_order_acquire);\n"
                        "    if (Before == Slot.Sequence.load(std::memory_order_relaxed)) return true;\n"
                        "}",
            },
            {
                "type": "table",
                "headers": [B("Operation", "Операция"), B("Ordering", "Упорядочивание"),
                           B("Why", "Почему")],
                "rows": [
                    [B("sequence increment (odd)", "инкремент sequence (нечётный)"),
                     B("relaxed + release fence", "relaxed + release fence"),
                     B("Marks the slot as being written; the fence prevents payload stores from moving before it",
                       "Помечает слот как записываемый; fence не даёт payload-записям уехать раньше")],
                    [B("payload stores", "записи payload"),
                     B("relaxed atomic_ref", "relaxed atomic_ref"),
                     B("No data race; ordering is supplied by the sequence brackets",
                       "Нет гонки данных; порядок задают скобки sequence")],
                    [B("sequence increment (even)", "инкремент sequence (чётный)"),
                     B("release store", "release store"),
                     B("Publishes the payload before the reader can accept it",
                       "Публикует payload до того, как читатель сможет его принять")],
                    [B("writer slot publish", "публикация слота writer"),
                     B("release store", "release store"),
                     B("Reader takes the slot index with acquire and sees a complete frame",
                       "Читатель берёт индекс слота с acquire и видит целый кадр")],
                    [B("reader retry", "повтор читателя"),
                     B("acquire + relaxed re-check", "acquire + relaxed re-check"),
                     B("Rejects a torn read after the writer lapped the reader",
                       "Отвергает рваное чтение, если writer обогнал reader")],
                ],
            },
            {
                "type": "bullets",
                "items": [
                    B("BindRuntimeWorld() only invalidates the buffer when the world pointers or the PIE flag "
                      "actually change; EditorSystem calls it every frame, so an unconditional invalidate would "
                      "erase the sample before Draw().",
                      "BindRuntimeWorld() инвалидирует буфер только при реальной смене указателей мира или флага "
                      "PIE; EditorSystem вызывает его каждый кадр, поэтому безусловная инвалидация стирала бы "
                      "sample до Draw()."),
                    B("PublishPIEStateFromWorlds() is the convenience producer: it resolves EditorSelected, looks "
                      "up AnimStateComponent in the runtime world and publishes. It must run on the simulation "
                      "thread, never from Draw().",
                      "PublishPIEStateFromWorlds() — удобный producer: находит EditorSelected, берёт "
                      "AnimStateComponent из runtime-мира и публикует. Он обязан работать на потоке симуляции, "
                      "никогда из Draw()."),
                    B("RefreshPIEState() and DrawPIEInspector() read only the seqlock slot. The inspector no longer "
                      "walks the Mirror table over a live pointer; it prints the published scalar fields.",
                      "RefreshPIEState() и DrawPIEInspector() читают только слот seqlock. Инспектор больше не "
                      "обходит Mirror-таблицу по живому указателю; он печатает опубликованные скаляры."),
                    B("The hand-off adds exactly one frame of latency and one 64-byte copy per published frame. "
                      "It allocates nothing and never takes a mutex.",
                      "Передача добавляет ровно один кадр задержки и одну 64-байтную копию на опубликованный "
                      "кадр. Она ничего не аллоцирует и никогда не берёт мьютекс."),
                ],
            },
            {
                "type": "note",
                "text": B(
                    "If PIE is later stepped on a worker pool, the host must publish after joining the pool and "
                    "before the editor draws. Do not call PublishPIEState() from the render thread while workers "
                    "still run: the producer side touches the registry and a live component, which are not "
                    "protected by the seqlock. The seqlock protects the hand-off, not the capture.",
                    "Если PIE позже начнёт шагать на пуле воркеров, хост обязан публиковать после join пула и до "
                    "отрисовки редактора. Не вызывайте PublishPIEState() из render-потока, пока воркеры ещё "
                    "работают: producer трогает реестр и живой компонент, а они не защищены seqlock. Seqlock "
                    "защищает передачу, а не снятие.",
                ),
            },
        ],
    },
    {
        "number": "36",
        "title": B("Global audit: release-blocking findings and the gate",
                   "Глобальный аудит: блокирующие находки и гейт"),
        "blocks": [
            {
                "type": "note",
                "text": B(
                    "Historical snapshot. This table is frozen at the 2026-10-27 audit: several findings are "
                    "already fixed, and the live status with file/line evidence is Documentation/ROADMAP.md. "
                    "Do not schedule work from this table.",
                    "Исторический снимок. Таблица заморожена на аудите 2026-10-27: часть находок уже исправлена, "
                    "а живой статус с файлами и строками — Documentation/ROADMAP.md. Не планируйте работы по "
                    "этой таблице.",
                ),
            },
            {
                "type": "p",
                "text": B(
                    "The audit covered VulkanRHI/MetalRHI, NetRollbackSession, Snapshot v4, the Box2D remap, the "
                    "weather compute path, VFS and the asset cooker, JobSystem, SystemGraph, hot reload and the "
                    "animation canvas. It confirmed more than fifty defects. This chapter records the frozen "
                    "snapshot for reference, and Documentation/ROADMAP.md is the live gate a reviewer should apply.",
                    "Аудит покрыл VulkanRHI/MetalRHI, NetRollbackSession, Snapshot v4, Box2D-remap, погодный "
                    "compute-путь, VFS и кукер ассетов, JobSystem, SystemGraph, hot reload и анимационный холст. "
                    "Подтверждено более пятидесяти дефектов. Эта глава фиксирует замороженный снимок для "
                    "справки, а живой гейт — Documentation/ROADMAP.md.",
                ),
            },
            {
                "type": "table",
                "headers": [B("ID", "ID"), B("Severity", "Severity"), B("Finding", "Находка"),
                           B("Minimal fix", "Минимальный фикс")],
                "rows": [
                    [B("R1", "R1"), B("Critical", "Critical"),
                     B("Rollback restore never destroys old Box2D bodies; each rollback duplicates the world",
                       "Restore отката не уничтожает старые Box2D-тела; каждый откат дублирует мир"),
                     B("PhysicsSystem::DestroyAllBodies() inside NetRollbackSession::RestoreSnapshot(), before Registry::clear()",
                       "PhysicsSystem::DestroyAllBodies() внутри NetRollbackSession::RestoreSnapshot(), до Registry::clear()")],                    [B("L1", "L1"), B("High", "High"),
                     B("FilterDeltaForPlayer checks the source payload length, not the destination capacity",
                       "FilterDeltaForPlayer проверяет длину источника, а не ёмкость приёмника"),
                     B("Compute capacity from OutFiltered and test before every memcpy",
                       "Считать ёмкость из OutFiltered и проверять до каждого memcpy")],
                    [B("C1/C2", "C1/C2"), B("Critical", "Critical"),
                     B("Readback runs before frame submit; failed frame leaves fence/semaphore signalled",
                       "Readback идёт до submit кадра; неудачный кадр оставляет fence/semaphore сигнализированными"),
                     B("Record copy into the frame CB; reset fence only before a successful submit",
                       "Писать copy в CB кадра; сбрасывать fence только перед успешным submit")],
                    [B("S1", "S1"), B("High", "High"),
                     B("ParticleEmitterComponent loses six non-reflected members on restore",
                       "ParticleEmitterComponent теряет шесть неотражённых полей при restore"),
                     B("Add the six FBY_FIELD entries", "Добавить шесть FBY_FIELD")],
                    [B("V2", "V2"), B("Critical", "Critical"),
                     B("Store codec memcpy uses attacker-controlled stored.size()",
                       "Store-кодек копирует управляемый атакующим stored.size()"),
                     B("Require StoredSize == RawSize for Store", "Требовать StoredSize == RawSize для Store")],
                    [B("V3", "V3"), B("High", "High"),
                     B("Video decoder keeps a VFS mmap span across hot reload",
                       "Видеодекодер держит mmap-span VFS через hot reload"),
                     B("Own the bytes or return a shared mapping + generation",
                       "Владеть байтами или отдавать shared mapping + generation")],
                    [B("G1", "G1"), B("Critical", "Critical"),
                     B("New projects have no CMakeLists/GameMain and cannot be built from the dashboard",
                       "У нового проекта нет CMakeLists/GameMain, его нельзя собрать из дашборда"),
                     B("ProjectScaffold.hpp + CreateProject writes CMakeLists.txt, project.femproj and GameMain.cpp",
                       "ProjectScaffold.hpp + CreateProject пишет CMakeLists.txt, project.femproj и GameMain.cpp")],
                    [B("G2", "G2"), B("Critical", "Critical"),
                     B("Play/Pause/Stop are cosmetic; the editor never snapshots or steps an isolated world",
                       "Play/Pause/Stop декоративны; редактор не делает снапшот и не шагает изолированный мир"),
                     B("EditorSystem toolbar -> PlayInEditor::Start/Step/Stop with a private physics world",
                       "Toolbar EditorSystem -> PlayInEditor::Start/Step/Stop с приватным физическим миром")],
                    [B("G8", "G8"), B("High", "High"),
                     B("SystemGraph::Run is a sequential walk regardless of after/before",
                       "SystemGraph::Run — последовательный обход независимо от after/before"),
                     B("Dependency levels + one wave per level + per-call semaphore + cache-line split",
                       "Уровни зависимостей + волна на уровень + семафор на вызов + разделение кэш-линий")],
                    [B("H1", "H1"), B("High", "High"),
                     B("Nested ParallelFor shares one semaphore and can return before helpers finish",
                       "Вложенный ParallelFor делит один семафор и может вернуться до завершения helper'ов"),
                     B("Per-call semaphore + TLS nesting flag", "Семафор на вызов + TLS-флаг вложенности")],
                    [B("H4", "H4"), B("High", "High"),
                     B("SystemGraph::Run ignores MBuilt; Add/RemoveUser leave a stale or empty order",
                       "SystemGraph::Run игнорирует MBuilt; Add/RemoveUser оставляют старый/пустой порядок"),
                     B("Gate Run on MBuilt; rebuild on mutation", "Гейт Run по MBuilt; rebuild при мутации")],
                    [B("H3 (RHI)", "H3 (RHI)"), B("High", "High"),
                     B("Instance deltas are applied to the N-3 slice", "Instance-дельты применяются к срезу N-3"),
                     B("Full copy when framesInFlight > 1, or timestamp slices",
                       "Полная копия при framesInFlight > 1 или timestamp срезов")],
                    [B("W1", "W1"), B("Critical", "Critical"),
                     B("Splash counter is never reset; splashes stop after capacity",
                       "Splash-счётчик не сбрасывается; всплески прекращаются после capacity"),
                     B("Clear per frame in a pre-pass, or ring modulo capacity",
                       "Сбрасывать каждый кадр в pre-pass или ring по модулю capacity")],
                ],
            },
            {
                "type": "table",
                "headers": [B("Gate", "Гейт"), B("Contents", "Содержимое"), B("Exit criterion", "Критерий выхода")],
                "rows": [
                    [B("A — blocks freeze", "A — блокирует freeze"),
                     B("R1, L1, C1, C2, S1, V2, V3, G1, G2, G8, H1, H3, H4, W1-W3",
                       "R1, L1, C1, C2, S1, V2, V3, G1, G2, G8, H1, H3, H4, W1-W3"),
                     B("Every item has a fix or a signed waiver plus a regression test",
                       "У каждого пункта есть фикс или подписанный waiver плюс регрессионный тест")],
                    [B("B — before RC", "B — до RC"),
                     B("R2, R3, S2-S8, C4-C6, V4-V7, H5/H6, W2/W6, M8-M10",
                       "R2, R3, S2-S8, C4-C6, V4-V7, H5/H6, W2/W6, M8-M10"),
                     B("Fix or documented limitation in the release notes",
                       "Фикс или задокументированное ограничение в release notes")],
                    [B("C — after freeze", "C — после freeze"),
                     B("Low findings, SoA split, remaining SIMD work",
                       "Low-находки, SoA-разделение, оставшаяся SIMD-работа"),
                     B("Scheduled for 1.1 with a profiling baseline",
                       "Запланировано на 1.1 с профилировочным baseline")],
                ],
            },
            {
                "type": "p",
                "text": B(
                    "The audit also verified the parts that are correct and must not be 'fixed' by accident: "
                    "snapshot canonical ordering and header layout, Pcg32 reference compatibility, VFS pak range "
                    "checks, LZ decompressor bounds, fence-before-acquire, per-image present semaphores, "
                    "descriptor accounting, plugin unload order, and the TTS main-thread contract. Several "
                    "findings are latent because the weather records are not consumed by a backend yet; they are "
                    "still release blockers once that consumer lands.",
                    "Аудит также подтвердил корректные части, которые нельзя «чинить» случайно: канонический "
                    "порядок и раскладку заголовков снапшота, совместимость Pcg32 с reference, range-checks "
                    "VFS-пака, границы LZ-декомпрессора, fence-before-acquire, present-семафоры на изображение, "
                    "учёт дескрипторов, порядок выгрузки плагинов и main-thread контракт TTS. Часть находок "
                    "латентна, потому что погодные записи пока не потребляются backend'ом; они всё равно станут "
                    "блокерами, как только этот потребитель появится.",
                ),
            },
        ],
    },
    {
        "number": "37",
        "title": B("Profiling cache misses and vectorisation toward 8000+ FPS",
                   "Профилирование кэш-миссов и векторизация к 8000+ FPS"),
        "blocks": [
            {
                "type": "p",
                "text": B(
                    "8000 FPS is 125 microseconds per frame. That number is meaningful only together with the "
                    "scene, resolution and hardware: a 2D batch of a few thousand sprites at 1080p is a different "
                    "problem from a 4K frame with post-processing. Treat 8000 FPS as a target for the engine's "
                    "own frame cost (simulation + extract + submission) on a Zen 5 machine with a warm cache, and "
                    "always report the 99th percentile frame time, not just the average FPS. A stable 8000+ means "
                    "p99 <= 125 us, not one lucky frame.",
                    "8000 FPS — это 125 микросекунд на кадр. Это число имеет смысл только вместе со сценой, "
                    "разрешением и железом: 2D-батч из нескольких тысяч спрайтов при 1080p — не та же задача, "
                    "что 4K-кадр с постобработкой. Рассматривайте 8000 FPS как цель для собственной стоимости "
                    "кадра движка (симуляция + extract + submission) на Zen 5 с прогретым кэшем и всегда "
                    "сообщайте 99-й процентиль времени кадра, а не только средний FPS. Стабильные 8000+ — это "
                    "p99 <= 125 мкс, а не один удачный кадр.",
                ),
            },
            {
                "type": "bullets",
                "items": [
                    B("Step 1 — baseline. Record CPU and GPU frame time separately. On Windows use WPA/ETW or "
                      "AMD uProf; on Linux use perf stat with cache-references, cache-misses, L1-dcache-load-misses, "
                      "LLC-load-misses and stalled-cycles-frontend. Store the numbers in the repository so a "
                      "regression is visible.",
                      "Шаг 1 — baseline. Записывайте CPU- и GPU-время кадра отдельно. На Windows — WPA/ETW или "
                      "AMD uProf; на Linux — perf stat с cache-references, cache-misses, L1-dcache-load-misses, "
                      "LLC-load-misses и stalled-cycles-frontend. Сохраняйте числа в репозитории, чтобы регрессия "
                      "была видна."),
                    B("Step 2 — find the hot array. The animation tick walks AnimStateComponent per entity (4 KB "
                      "stride); the extract walks RenderItem; the job pool touches one shared cache line. A 4 KB "
                      "stride with 8 transitions and 8 clips per component thrashes L1 and L2 before any math "
                      "runs.",
                      "Шаг 2 — найдите горячий массив. Тик анимации идёт по AnimStateComponent на сущность "
                      "(шаг 4 КБ); extract идёт по RenderItem; пул задач трогает одну общую линию кэша. Шаг 4 КБ "
                      "с 8 переходами и 8 клипами на компонент вымывает L1 и L2 ещё до вычислений."),
                    B("Step 3 — split hot/cold. Keep a 64-byte AnimHot per entity (states, StateTime, Blend*) "
                      "and move Transitions/Clips/RestPose into a shared graph object or a cold side table. The "
                      "system then streams a dense array; the shared tables stay L1-resident.",
                      "Шаг 3 — разделите hot/cold. Держите 64-байтный AnimHot на сущность (состояния, StateTime, "
                      "Blend*), а Transitions/Clips/RestPose вынесите в общий объект графа или холодную "
                      "side-таблицу. Система начинает потоково идти по плотному массиву; общие таблицы остаются "
                      "в L1."),
                    B("Step 4 — vectorise the presentation blend, not the deterministic simulation. Use "
                      "__restrict, SoA float arrays (X[], Y[], Rotation[], Scale[]) and check the vectoriser "
                      "report. Do not reorder floating-point reductions used by rollback or physics.",
                      "Шаг 4 — векторизуйте презентационный blend, а не детерминированную симуляцию. "
                      "Используйте __restrict, SoA-массивы float (X[], Y[], Rotation[], Scale[]) и проверяйте "
                      "отчёт векторизатора. Не меняйте порядок FP-редукций, используемых rollback или физикой."),
                    B("Step 5 — reduce RHI binds. Sort draws by (pipeline, material, texture array), use push "
                      "constants for the draw id, keep one bindless set, and record one indirect draw per batch. "
                      "Descriptor-set churn is usually more expensive than the draw itself at 8k FPS.",
                      "Шаг 5 — сократите RHI-bind'ы. Сортируйте draw'ы по (pipeline, material, texture array), "
                      "используйте push constants для draw id, держите один bindless set и записывайте один "
                      "indirect draw на батч. На 8k FPS churn дескрипторных сетов обычно дороже самого draw."),
                    B("Step 6 — parallelise phases, not just entities. SystemGraph::Build() computes dependency "
                      "levels once; Run() launches each level as one wave through JobSystem and joins it with a "
                      "per-call semaphore before Render. MPending and MRunning sit on separate 64-byte lines, so "
                      "submit/completion traffic does not bounce a shared line between 24 workers. Keep the main "
                      "thread for submission and reserve one physical core for render/UI; 23 SMT workers on 12 "
                      "physical cores often lose to 11-12 dedicated workers on cache-bound work.",
                      "Шаг 6 — параллельте фазы, а не только сущности. SystemGraph::Build() один раз считает "
                      "уровни зависимостей; Run() запускает каждый уровень одной волной через JobSystem и "
                      "дожидается её семафором на вызов до Render. MPending и MRunning лежат на разных 64-байтных "
                      "линиях, поэтому трафик submit/completion не трясёт общую линию между 24 воркерами. "
                      "Оставляйте главный поток для submission и резервируйте одно физическое ядро под render/UI; "
                      "23 SMT-воркера на 12 физических ядрах часто проигрывают 11-12 выделенным на cache-bound "
                      "работе."),
                ],
            },
            {
                "type": "code",
                "lang": "cpp",
                "text": "// SoA blend: one AVX-512 iteration covers 16 bones on Zen 5.\n"
                        "void BlendBones(float* __restrict OutX, float* __restrict OutY,\n"
                        "                const float* __restrict AX, const float* __restrict AY,\n"
                        "                const float* __restrict BX, const float* __restrict BY,\n"
                        "                std::size_t Count, float Weight) noexcept\n"
                        "{\n"
                        "    for (std::size_t I = 0; I < Count; ++I)\n"
                        "    {\n"
                        "        OutX[I] = AX[I] + (BX[I] - AX[I]) * Weight;\n"
                        "        OutY[I] = AY[I] + (BY[I] - AY[I]) * Weight;\n"
                        "    }\n"
                        "}\n"
                        "\n"
                        "// Verify the vectoriser actually fired:\n"
                        "// clang-cl /O2 /Rpass=loop-vectorize /Rpass-missed=loop-vectorize\n"
                        "// clang++  -O2 -Rpass=loop-vectorize -Rpass-missed=loop-vectorize",
            },
            {
                "type": "table",
                "headers": [B("Counter", "Счётчик"), B("Healthy range", "Здоровый диапазон"),
                           B("If it is worse", "Если хуже")],
                "rows": [
                    [B("L1d load misses", "L1d load misses"), B("< 2% of loads", "< 2% загрузок"),
                     B("Hot/cold split, prefetch, reduce stride", "Hot/cold split, prefetch, уменьшить stride")],
                    [B("LLC load misses", "LLC load misses"), B("< 1-2%", "< 1-2%"),
                     B("Shrink working set, huge pages, reorder loops", "Уменьшить working set, huge pages, переупорядочить циклы")],
                    [B("stalled-cycles-frontend", "stalled-cycles-frontend"), B("< 10%", "< 10%"),
                     B("Branch layout, inline hot paths, avoid i-cache thrash", "Layout веток, inline горячих путей, меньше i-cache thrash")],
                    [B("IPC", "IPC"), B("> 2.5", "> 2.5"),
                     B("Check dependency chains, false sharing, lock contention", "Проверить цепочки зависимостей, false sharing, конкуренцию за lock")],
                    [B("p99 frame time", "p99 время кадра"), B("<= 125 us for 8000 FPS", "<= 125 мкс для 8000 FPS"),
                     B("Cap fixed steps, remove WaitIdle from the frame, budget submissions", "Ограничить fixed steps, убрать WaitIdle из кадра, бюджетировать submission")],
                ],
            },
            {
                "type": "note",
                "text": B(
                    "Use RenderDoc or PIX to separate CPU from GPU: an 8k FPS CPU frame with a 2 ms GPU frame is "
                    "still 500 FPS on screen. Do not chase the CPU target by removing correctness work "
                    "(validation, barriers, checksums) — move it to a background job or a less frequent pass. "
                    "And keep a rollback-determinism test green after every SIMD change: vectorised float order "
                    "is a classic source of 'works on my machine' desync.",
                    "Используйте RenderDoc или PIX, чтобы разделить CPU и GPU: CPU-кадр на 8k FPS при GPU-кадре "
                    "2 мс всё равно даёт 500 FPS на экране. Не гонитесь за CPU-целью, удаляя корректную работу "
                    "(валидацию, барьеры, чексуммы) — переносите её в фоновую задачу или более редкий проход. И "
                    "держите зелёным тест детерминизма rollback после каждого SIMD-изменения: "
                    "векторизованный порядок float — классический источник desync «у меня работает».",
                ),
            },
        ],
    },
    {
        "number": "38",
        "title": B("Materials in the engine: cook, cache and the frame",
                   "Материалы в движке: кук, кэш и кадр"),
        "blocks": [
            {
                "type": "p",
                "text": B(
                    "The material system was the last subsystem whose runtime still lived on the editor side. "
                    "The graph model and the GLSL emitter shipped in `fby_engine`, but the driver that turned a "
                    "graph into a pipeline - glslang, SPIR-V, `CreateShaderPipeline` and a private 128-entry table - "
                    "shipped only in `fby_editor`. A game links `FemboyEngine2D::Engine` alone, so it could validate "
                    "a material and had no way to draw with it. [ADR 0002](../../adr/0002-materials-in-engine.md) moves the missing half into the engine and "
                    "leaves the editor with a thin live-recompile adapter.",
                    "Система материалов была последней подсистемой, рантайм которой оставался на стороне "
                    "редактора. Модель графа и эмиттер GLSL поставлялись в `fby_engine`, но драйвер, "
                    "превращающий граф в пайплайн — glslang, SPIR-V, `CreateShaderPipeline` и приватная таблица "
                    "на 128 записей — поставлялся только в `fby_editor`. Игра линкует лишь "
                    "`FemboyEngine2D::Engine`, поэтому могла провалидировать материал и не могла им рисовать. "
                    "[ADR 0002](../../adr/0002-materials-in-engine.md) переносит недостающую половину в движок и оставляет редактору тонкий адаптер живой "
                    "перекомпиляции.",
                ),
            },
            {
                "type": "table",
                "headers": [B("Stage", "Этап"), B("Owner", "Владелец"), B("What it validates", "Что проверяет")],
                "rows": [
                    [
                        B("Author: `MaterialGraph`", "Авторинг: `MaterialGraph`"),
                        B("Editor panel, or a tool that writes `.fgraph`", "Панель редактора или инструмент, "
                                                                          "пишущий `.fgraph`"),
                        B("One output node, connected required pins, type compatibility, acyclicity, capacity limits",
                          "Один выходной узел, подключённые обязательные пины, совместимость типов, "
                          "ацикличность, пределы ёмкости"),
                    ],
                    [
                        B("Cook: `fby_matc`", "Кук: `fby_matc`"),
                        B("Build step, links glslang", "Шаг сборки, линкует glslang"),
                        B("The whole graph, then each generated GLSL stage through glslang, then the payload encoder",
                          "Весь граф, затем каждый сгенерированный GLSL-этап через glslang, затем кодировщик "
                          "payload"),
                    ],
                    [
                        B("Load: `DecodeCookedMaterial`", "Загрузка: `DecodeCookedMaterial`"),
                        B("Shipped runtime, no allocation", "Поставленный рантайм, без аллокаций"),
                        B("Magic, version, size against the stage lengths, both SPIR-V modules, contract "
                          "consistency", "Magic, версия, размер против длин этапов, оба SPIR-V модуля, "
                                         "согласованность контракта"),
                    ],
                    [
                        B("Acquire: `MaterialPipelineCache`", "Acquire: `MaterialPipelineCache`"),
                        B("Shipped runtime, one cache per device", "Поставленный рантайм, один кэш на устройство"),
                        B("Hash and contract against the resident entries, reference count, LRU eviction, backend "
                          "capability", "Хеш и контракт против резидентных записей, счётчик ссылок, LRU-эвикция, "
                                        "возможность бэкенда"),
                    ],
                    [
                        B("Frame: `MeshRenderItem::materialPipeline`", "Кадр: `MeshRenderItem::materialPipeline`"),
                        B("Render extract and the backend", "Извлечение кадра и бэкенд"),
                        B("Vulkan groups mesh records by the id and binds one material pipeline per group; other "
                          "backends keep the engine shader", "Vulkan группирует записи мешей по id и биндит один "
                                                              "материальный пайплайн на группу; остальные "
                                                              "бэкенды остаются на шейдере движка"),
                    ],
                ],
            },
            {
                "type": "p",
                "text": B(
                    "Cooking a material is one command, and the output travels like any other cooked asset:",
                    "Кук материала — одна команда, а результат едет как любой другой кук-ассет:",
                ),
            },
            {
                "type": "code",
                "lang": "bash",
                "text": "# fby_matc <graph.fgraph> [-o <cooked.femmat>]\n"
                        "build/bin/fby_matc assets/materials/tinted.fgraph -o assets/materials/tinted.femmat\n"
                        "\n"
                        "# The tool refuses a graph that does not validate, prints the hash and the SPIR-V word\n"
                        "# counts, and never writes a payload the runtime would reject.\n",
            },
            {
                "type": "code",
                "lang": "cpp",
                "text": "// One cache per device, owned by the game. Acquire at load time.\n"
                        "Render::MaterialPipelineCache ScopedMaterials;\n"
                        "\n"
                        "// `InBytes` comes from the VFS or a package entry.\n"
                        "Render::CookedMaterialView ScopedTinted;\n"
                        "if (!Render::DecodeCookedMaterial(InBytes, &ScopedTinted))\n"
                        "{\n"
                        "    LogError(\"materials\", \"tinted.femmat is not a cooked material\");\n"
                        "    return false;\n"
                        "}\n"
                        "\n"
                        "const Rhi::ShaderPipelineHandle ScopedTintedPipeline =\n"
                        "    ScopedMaterials.Acquire(InRhi, ScopedTinted);\n"
                        "\n"
                        "// A missing pipeline is not a crash: the item falls back to the engine shader.\n"
                        "ScopedItem.materialPipeline = ScopedTintedPipeline.IsValid()\n"
                        "    ? ScopedTintedPipeline.id\n"
                        "    : Rhi::kDefaultMaterialPipeline;\n"
                        "\n"
                        "// At shutdown, while the device is still alive.\n"
                        "ScopedMaterials.Shutdown(InRhi);\n",
            },
            {
                "type": "bullets",
                "items": [
                    B("Retention over churn: a pipeline at zero references stays resident, because a game that "
                      "acquires a material every frame and releases it at the end of the frame would otherwise "
                      "rebuild the pipeline sixty times per second. The 128-entry cap and LRU eviction bound the "
                      "cost instead.",
                      "Удержание вместо пересоздания: пайплайн с нулём ссылок остаётся резидентным, иначе игра, "
                      "берущая материал каждый кадр и отпускающая его в конце кадра, пересобирала бы пайплайн "
                      "шестьдесят раз в секунду. Стоимость ограничивают предел в 128 записей и LRU-эвикция."),
                    B("The contract mask is part of the identity check. A graph hash that arrives with a different "
                      "contract while the entry is live is refused; the same hash with a different contract and no "
                      "references is replaced in place, which is what the editor does when push-constant support "
                      "changes.",
                      "Маска контракта — часть проверки идентичности. Хеш графа, приходящий с другим контрактом, "
                      "пока запись жива, отклоняется; тот же хеш с другим контрактом без ссылок заменяется на "
                      "месте — так редактор обрабатывает смену поддержки push-констант."),
                    B("The backend says whether it can build a runtime pipeline. Vulkan returns true; Null and "
                      "Metal return false and keep the engine shaders, so a game targeting them fails at load with "
                      "a readable error instead of drawing with an unbound pipeline.",
                      "Бэкенд сам говорит, умеет ли он собирать рантайм-пайплайн. Vulkan возвращает true; Null и "
                      "Metal возвращают false и остаются на шейдерах движка, поэтому игра под них падает при "
                      "загрузке с читаемой ошибкой, а не рисует несвязанным пайплайном."),
                    B("Nothing on this path allocates or formats text per frame: the cache is a fixed array, the "
                      "decode is a view, and the error is a fixed buffer.",
                      "На этом пути ничего не аллоцирует и не форматирует текст покадрово: кэш — фиксированный "
                      "массив, декодирование — view, ошибка — фиксированный буфер."),
                ],
            },
            {
                "type": "p",
                "text": B(
                    "The editor keeps its live experience through the same cache. `MaterialGraphRuntime` stores one "
                    "graph pointer per tab, recompiles when `Hash()` changes, keeps the last good pipeline while an "
                    "edit is invalid, and reports the failure per tab. It no longer owns a pipeline table: "
                    "`Pipeline()` returns the handle the engine cache handed out, so an editor session and a game "
                    "share one budget.",
                    "Редактор сохраняет живой опыт через тот же кэш. `MaterialGraphRuntime` хранит указатель на "
                    "граф на вкладку, перекомпилирует при смене `Hash()`, держит последний удачный пайплайн, пока "
                    "правка невалидна, и отдаёт ошибку по вкладке. Своей таблицы пайплайнов у него больше нет: "
                    "`Pipeline()` возвращает хендл, выданный движковым кэшем, поэтому сессия редактора и игра "
                    "делят один бюджет.",
                ),
            },
            {
                "type": "table",
                "headers": [B("Test", "Тест"), B("Covers", "Что покрывает")],
                "rows": [
                    [B("`material_graph`", "`material_graph`"),
                     B("DAG validation, deterministic GLSL, glslang acceptance",
                       "Валидация DAG, детерминированный GLSL, приёмка glslang")],
                    [B("`material_asset`", "`material_asset`"),
                     B("Graph round trip, payload corruption, contract flags",
                       "Round-trip графа, порча payload, флаги контракта")],
                    [B("`material_cook`", "`material_cook`"),
                     B("End-to-end: the tool cooks, an Engine-only binary decodes and checks the hash",
                       "Сквозной: инструмент кука, бинарь только с движком декодирует и проверяет хеш")],
                    [B("`material_pipeline_cache`", "`material_pipeline_cache`"),
                     B("Sharing, reference counts, eviction, exhaustion, refusal paths",
                       "Разделение, счётчики ссылок, эвикция, исчерпание, отказы")],
                    [B("`material_mesh_path`", "`material_mesh_path`"),
                     B("Two meshes share one cached pipeline, a third keeps the engine shader",
                       "Два меша делят один пайплайн, третий остаётся на шейдере движка")],
                ],
            },
            {
                "type": "p",
                "text": B(
                    "The sprite path is now part of the same system. A sprite material is a second shader contract, "
                "not a second compiler: `MaterialCompileOptions::domain` switches the fragment interface to the "
                "`quad.vert` varyings, `vLayer` supplies the item's atlas slice, and the sprite push block carries a "
                "material clock at offset 80, so `Time` works in both domains; only `Fresnel` and "
                "`VertexPosition` are refused because they need a view-projection. The 64-bit render key carries the material bucket "
                "(16 bits layer, 8 bits material, 32 bits depth, 8 bits sequence), so `DrawOrder::MaterialBucket` "
                "both sorts a sprite into its material run and, through "
                "`ChangeMaskRenderExtract::SetMaterialPipelines`, resolves the run to one `DrawBatch` with the "
                "cached pipeline id. The Vulkan sprite loop binds that pipeline per batch and restores the engine "
                "quad pipeline for the next run; a backend without runtime shader pipelines keeps the quad "
                "pipeline and draws the sprite unshaded by the graph, never with an unbound handle. Compile-time "
                "layer composition, if it is ever added, becomes node kinds in the same graph, never a "
                "variable-length closure buffer.",
                "Спрайтовый путь теперь часть той же системы. Спрайтовый материал — второй контракт шейдера, а "
                "не второй компилятор: `MaterialCompileOptions::domain` переключает фрагментный интерфейс на "
                "varying-и `quad.vert`, `vLayer` даёт слой атласа элемента, а спрайтовый push-блок несёт часы "
                "материала по смещению 80, поэтому `Time` работает в обоих доменах; отклоняются только "
                "`Fresnel` и `VertexPosition`, которым нужна view-projection. 64-битный ключ отрисовки несёт материал-бакет "
                "(16 бит слой, 8 бит материал, 32 бита глубина, 8 бит последовательность), поэтому "
                "`DrawOrder::MaterialBucket` и сортирует спрайт в его материальный прогон, и через "
                "`ChangeMaskRenderExtract::SetMaterialPipelines` превращает прогон в один `DrawBatch` с id "
                "кэшированного пайплайна. Спрайтовый цикл Vulkan биндит этот пайплайн на батч и возвращает "
                "пайплайн движка для следующего прогона; бэкенд без рантайм-пайплайнов остаётся на пайплайне "
                "quad и рисует спрайт без эффекта графа, но никогда несвязанным хендлом. Композиция слоёв на "
                "этапе компиляции, если появится, станет видами узлов того же графа, а не буфером замыканий "
                "переменной длины.",
                ),
            },
        ],
    },
]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--skip-pdf", action="store_true")
    parser.add_argument("--node", default=base.DEFAULT_NODE)
    parser.add_argument("--lo-cli", default=base.DEFAULT_LO_CLI)
    args = parser.parse_args()

    base.SECTIONS = base.SECTIONS + EXTRA_SECTIONS
    base.TITLES = {
        "en": "FemboyEngine2D Complete Reference Manual",
        "ru": "FemboyEngine2D — Полное справочное руководство",
    }
    base.SUBTITLES = {
        "en": "Architecture, rendering, ECS, the animation/AI canvases with blend-cycle and PIE "
              "contracts, the global audit, DOD optimization and production distribution",
        "ru": "Архитектура, рендеринг, ECS, холсты анимации/ИИ с контрактами циклов блендинга и "
              "PIE-передачи, глобальный аудит, DOD-оптимизация и продакшн-дистрибуция",
    }

    DOCUMENTATION.mkdir(parents=True, exist_ok=True)
    docpaths.EnsureLayout()

    en_md = base.render_markdown("en")
    ru_md = base.render_markdown("ru")
    docpaths.Md("en", "FemboyEngineCompleteReference_EN.md").write_text(en_md, encoding="utf-8")
    docpaths.Md("ru", "FemboyEngineCompleteReference_RU.md").write_text(ru_md, encoding="utf-8")

    by_module, macros = base.scan_symbols()
    print(
        f"markdown: {len(base.SECTIONS)} sections, "
        f"{sum(len(entries) for entries in by_module.values())} symbols, {len(macros)} macros"
    )

    if not base.HAVE_DOCX:
        print("docx: python-docx is not installed; Markdown written", file=sys.stderr)
        return 1

    en_docx = docpaths.Docx("en", "FemboyEngineCompleteReference_EN.docx")
    ru_docx = docpaths.Docx("ru", "FemboyEngineCompleteReference_RU.docx")
    base.build_docx(en_docx, "en")
    base.build_docx(ru_docx, "ru")
    print(f"docx: {en_docx.name}, {ru_docx.name}")

    if args.skip_pdf:
        print("pdf: skipped by request")
        return 0

    en_pdf = docpaths.Pdf("en", "FemboyEngineCompleteReference_EN.pdf")
    ru_pdf = docpaths.Pdf("ru", "FemboyEngineCompleteReference_RU.pdf")
    ok_en = base.export_pdf(en_docx, en_pdf, args.node, args.lo_cli)
    ok_ru = base.export_pdf(ru_docx, ru_pdf, args.node, args.lo_cli)
    if not (ok_en and ok_ru):
        return 1
    print(f"pdf: {en_pdf.name}, {ru_pdf.name}")
    docconvert.BuildDjvu(en_pdf, docpaths.Djvu("en", "FemboyEngineCompleteReference_EN.djvu"))
    docconvert.BuildDjvu(ru_pdf, docpaths.Djvu("ru", "FemboyEngineCompleteReference_RU.djvu"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
