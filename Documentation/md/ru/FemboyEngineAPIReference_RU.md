# FemboyEngine2D — Справочник API

**Движок на C++26 в парадигме data-oriented: полная публичная поверхность, контракты и пошаговый пример**


Version 0.5.0 (Release) · 2026-11-13 · Russian


---


## Содержание


1. Область действия, соответствие стандарту и правила движка

2. Сборка, цели и флаги компилятора

3. Базовые типы, FORCEINLINE и семейство приведений

4. FrameArena: единственный кадровый аллокатор

5. JobSystem и потоковая диагностика

6. ECS, планирование и снапшоты

7. RHI, элементы отрисовки и статистика кадра

8. Материалы: кук графов, кэш пайплайнов и продакшн-путь

9. Физика: Box2D v3, суставы, ткань и SdfCollisionSystem

10. NetRollbackSession: детерминированный роллбэк

11. Аудио: микшер, DSP-граф, лимитер

12. PinkUI и WidgetCanvasSystem

13. Ядро доступности (BlindMode, DeafMode, FlashGuard)

14. Конвейер ассетов, VFS и локализация

15. Платформенные сервисы: Steam, Epic Games Store, GOG

16. Редактор: дашборд, шрифты, LogBoy, Scene Diagnostics

17. Пошагово: игра с пустой папки

18. Тесты, стилевой гейт и чек-лист релиза

19. Открытая работа

20. Приложение A - сгенерированный индекс символов

21. Приложение B - макросы и платформенные define



## 1. Область действия, соответствие стандарту и правила движка

Этот справочник описывает публичную поверхность FemboyEngine2D: все заголовки из Source/Engine и инфраструктуру редактора, с которой линкуется хост. Текст написан по дереву Release, поэтому описанный символ существует в поставляемых заголовках, а индекс в приложении генерируется из этих заголовков при каждом запуске.

Движок рассчитан на C++26 с GCC 16.2 или новее и Clang 23.1.4 или новее; релиз-кандидат под Windows собирается clang-cl со стандартной библиотекой MSVC. Четыре правила являются несущими, и каждый публичный API спроектирован вокруг них:

- Состояние симуляции — это простые trivially copyable данные. Ни виртуальных вызовов, ни владеющих указателей, ни std::string в компонентах, снапшотах и сетевых пакетах.
- Горячие пути не выделяют память. Кадровая память берётся из FrameArena, логи пишутся в фиксированные char-буферы, а колбэки — это указатель на функцию плюс void*, никогда std::function.
- Детерминизм — свойство данных, а не планировщика. Каждая подсистема отдаёт образ с контрольной суммой, и NetRollbackSession может воспроизвести только то, что воспроизводится байт в байт.
- Никакого RTTI в коде движка: dynamic_cast и typeid запрещены стилевым гейтом. Cast<> вместо этого обходит стабильные идентификаторы типов (см. раздел 3).

> Стилевой гейт (Source/Tools/Dev/CheckStyle.py) проверяет правила именования и структуры: PascalCase имена файлов, Allman-скобки, обязательные фигурные скобки у управляющих конструкций, префикс In у параметров, префикс Scoped у локальных переменных, отсутствие комментариев в конце строки и кириллицы в исходниках движка.


## 2. Сборка, цели и флаги компилятора

Одно дерево CMake производит три вида артефактов: статическую библиотеку движка (FemboyEngine2D::Engine), библиотеку и приложение редактора (FemboyEngine2D::Editor) и офлайн-инструменты. Поставляемая игра линкует только движок; Dear ImGui, ImGuizmo, ImPlot и TOML-писатель не попадают в поставочные цели.

| Опция CMake | По умолчанию | Действие |
|---|---|---|
| FBY_BUILD_EDITOR | ON | Собирает fby_editor, приложение редактора и плагины панелей. |
| FBY_BUILD_TESTS | OFF | Собирает набор ctest в Source/Tests. |
| FBY_BUILD_TOOLS | ON | FempakCooker, LocTool, SceneTool, ShaderCompiler, AotCompiler. |
| FBY_BUILD_PLATFORM_PROVIDERS | ON | Плагины провайдеров Steam и Epic Games Store. |
| FBY_RHI_BACKEND | auto | auto, vulkan, metal или null. auto выбирается по платформе. |

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DFBY_BUILD_TESTS=ON -DFBY_BUILD_EDITOR=ON
cmake --build build --target fby_editor
ctest --test-dir build --output-on-failure
python Source/Tools/Dev/CheckStyle.py
```

Обёртка компилятора (cmake/FbyCompiler.cmake) задаёт флаг C++26, набор предупреждений и переключатели санитайзеров. Не добавляйте флаги отдельным целям: цель, которой нужен другой диалект, не должна существовать.


## 3. Базовые типы, FORCEINLINE и семейство приведений

Definitions.hpp — контрактный заголовок движка. Он содержит макросы экспорта, макросы утверждений (FBY_ASSERT, FBY_VERIFY, FBY_UNREACHABLE), механизм стабильных TypeId, семейство приведений и макросы регистрации компонентов. Остальные заголовки включают его прямо или транзитивно.

```cpp
#if defined(_MSC_VER)
#define FORCEINLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define FORCEINLINE inline __attribute__((always_inline))
#else
#define FORCEINLINE inline
#endif
```

FORCEINLINE — единственное написание инлайна в публичных заголовках. Он применяется к аксессорам, горячей арифметике и шаблонным приведениям и никогда не заменяет измеренную оптимизацию. Существуют три семейства:

| Символ | Контракт |
|---|---|
| FemCast<To>(value) | Непроверяемый static_cast со сворачиванием ссылок. Используется, когда конкретный тип доказан конструкцией (выход фабрики, тег хранилища). Нулевая цена в рантайме. |
| Cast<To>(object) | Проверяемое приведение. Апкаст — compile-time static_cast; даункаст сравнивает GetTypeId() объекта с StableTypeId<To>() и цепочкой родителей; несвязанные типы не компилируются. При несовпадении возвращается nullptr, исключений и RTTI нет. |
| CastComponent<To>(registry, entity) | ECS-написание Cast<>: система типов проверяет тип компонента на этапе компиляции, а реестр проверяет сущность. Возвращает nullptr, если компонента у сущности нет. |

```cpp
// Downcast through the stable type table, not through RTTI.
Fby::TypeObject* base = factory.Create("Sprite");
if (Fby::Sprite* sprite = Fby::Cast<Fby::Sprite>(base))
{
    sprite->Animate(dt);
}

// Registration binds Sprite to TypeObject and derives its stable id.
FBY_REGISTER_TYPE(Fby::Sprite, Fby::TypeObject);
```

StableTypeId<T>() — consteval FNV-1a от зарегистрированного имени типа (или сигнатуры компилятора, если имя не закреплено). Смена зарегистрированного имени меняет id, поэтому имена компонентов входят в сериализованную схему, а Snapshot v5 хранит LayoutHash рядом с именем.


## 4. FrameArena: единственный кадровый аллокатор

FrameArena — это bump-указатель по памяти, которой владеет вызывающий код. Он сбрасывается в начале кадра и никогда не освобождает отдельные выделения. Системы, которым нужны временные массивы, берут его из SystemContext::frameArena; система, вызывающая operator new в кадровом пути, — дефект, а не стилистическое предпочтение.

```cpp
Fby::FrameArena arena(storage);          // caller owns storage
arena.Reset();                          // once per frame
auto* items = arena.AllocateArray<MeshRenderItem>(count);
if (items == nullptr)
{
    // Arena exhausted: degrade, never allocate on the heap.
    return;
}
const std::size_t used = arena.Used();
const std::size_t capacity = arena.Capacity();
```

- Allocate() возвращает nullptr вместо исключения при исчерпании арены; вызывающий код обязан обработать отказ.
- AllocateArray<T>() проверяет std::is_trivially_copyable_v<T>: арена хранит только POD.
- Для типов с повышенным выравниванием нужно явно передать выравнивание; по умолчанию используется alignof(std::max_align_t).


## 5. JobSystem и потоковая диагностика

JobSystem владеет пулом std::jthread. Задача — это указатель на функцию, указатель на контекст и полуоткрытый диапазон, поэтому отправка задачи не выделяет память. ParallelFor делит диапазон на куски, позволяет каждому участнику брать следующий кусок из атомарного счётчика и всегда выполняет хотя бы один кусок на вызывающем потоке — так однопроцессорные машины остаются корректными, а повторный вход из воркера безопасным.

```cpp
jobs.ParallelFor(entityCount, 4096, [&](std::uint32_t begin, std::uint32_t end)
{
    for (std::uint32_t i = begin; i < end; ++i)
    {
        Integrate(entities[i], dt);
    }
});
jobs.WaitIdle();

// Diagnostics: one row per thread, slot 0 is the main thread.
std::array<Fby::JobSystem::ThreadLoad, Fby::JobSystem::kMaxThreadSlots> loads{};
const std::uint32_t slots = jobs.SampleThreadLoad(loads);
```

ThreadLoad содержит busyNanoseconds (время внутри работы движка), taskCount и 16-байтное имя. Счётчики выровнены по кэш-линии на поток, поэтому два одновременно завершившихся воркера не конфликтуют. SceneDiagnosticsPanel превращает два замера в процент занятости каждого потока.

> ThreadSlotCount() равен ThreadCount() + 1 и ограничен фиксированным массивом MCounters; устройство с более чем 64 воркерами сообщает первые 64 слота.


## 6. ECS, планирование и снапшоты

Реестр — это FCS. Системы регистрируются в SystemGraph в одной из пяти фаз (PreUpdate, FixedUpdate, Update, PostUpdate, Render). SystemContext — единственный параметр системы; он содержит реестр, JobSystem, кадровую арену, рендерер, окно, VFS, AssetDatabase, аудио, физику, суставы, ткань, жидкость, стек камер, ядро доступности и атласы шрифтов.

```cpp
Fby::Ecs::Schedule schedule;
schedule.Add(Fby::Ecs::Phase::FixedUpdate, "physics", &PhysicsStepSystem, &physics);
schedule.Add(Fby::Ecs::Phase::Update, "movement", &MovementSystem, nullptr);
schedule.Add(Fby::Ecs::Phase::PostUpdate, "platform", &GogAchievementSystem, &gog);

Fby::Ecs::SystemContext context{registry, jobs};
context.physics = &physics;
context.window = &window;
schedule.Run(Fby::Ecs::Phase::FixedUpdate, context);
```

Snapshot v5 — канонический образ реестра с контрольной суммой. Компоненты с рантайм-хэндлами (b2BodyId, b2ShapeId) пишутся рукописными кодеками; снапшот никогда не сериализует эти хэндлы, а восстановление пересоздаёт тела из полей компонента. Схема хранит Mirror-имя и LayoutHash каждого компонента, поэтому изменение поля отклоняется, а не читается молча неверно.

| API | Контракт |
|---|---|
| GetOrAdd<T>(registry, entity) | Добавляет компонент при отсутствии и возвращает стабильную ссылку. |
| ecs::SnapshotWriter / SnapshotReader | Пишет и валидирует канонический образ; контрольная сумма — FNV-1a по payload. |
| Fby::Net::NetRollbackSession | Использует snapshot v5 и воспроизводит тики при опоздании ввода (раздел 10). |


## 7. RHI, элементы отрисовки и статистика кадра

RHI — рукописный слой Vulkan/Metal. Кадры записываются в постоянные кольцевые буферы; спрайты — это 48-байтные POD; меши рисуются одним indirect-вызовом на материал/слой; linear-blend skinning — один compute-диспатч. FrameStats показывает, что реально отправил проход, и именно это рисует SceneDiagnosticsPanel.

| Поле FrameStats | Значение |
|---|---|
| items / batches / drawCalls | Спрайт-элементы, батчи и отправленные вызовы. |
| meshItems / meshIndirectCalls | Количество MeshRenderItem и indirect-вызовов. |
| skinDispatches / skinRanges | Диспатчи и диапазоны compute-скиннинга. |
| uploadedBytes | Байты, загруженные в этом кадре. |

> Viewport редактора рендерит в offscreen-цель, а не в swapchain. EnsureViewportTarget() задаёт размер цели в физических пикселях; цель, заданная в точках, — обычная причина размытого Viewport на HiDPI-мониторе.


## 8. Материалы: кук графов, кэш пайплайнов и продакшн-путь

Авторинг материала и его компиляция находятся по разные стороны границы движка. `Render::MaterialGraph` — авторский DAG с фиксированными ёмкостями (128 узлов, 384 пина, 64 параметра), trivially copyable, хешируется без позиций в редакторе. `Render::MaterialGraphCompiler` превращает прошедший валидацию граф в текст GLSL 450. `fby_matc` запускает этот компилятор и glslang на этапе сборки и пишет payload `.femmat`, поэтому поставленная игра не линкует glslang и не разбирает GLSL. Полное обоснование — в [ADR 0002](../../adr/0002-materials-in-engine.md).

| Компонент | Заголовок | Контракт |
|---|---|---|
| `Render::MaterialGraph` | `Engine/RHI/MaterialGraph.hpp` | Авторский DAG; `Hash()` — идентификатор пайплайна, позиции в UI не входят |
| `MaterialGraphCompiler` | `Engine/RHI/MaterialGraphCompiler.hpp` | DAG в GLSL 450 и флаги контракта шейдера |
| `EncodeMaterialGraph` / `DecodeMaterialGraph` | `Engine/RHI/MaterialAsset.hpp` | Wire-формат `.fgraph`: явные little-endian поля, без memcpy структуры |
| `EncodeCookedMaterial` / `DecodeCookedMaterial` | `Engine/RHI/MaterialAsset.hpp` | Payload `.femmat`: 32-байтный заголовок, SPIR-V вершинного и фрагментного этапов, декодирование без копирования |
| `Render::MaterialPipelineCache` | `Engine/RHI/MaterialPipelineCache.hpp` | 128 пайплайнов по хешу графа, с подсчётом ссылок, LRU-эвикцией и без обращений к куче |
| `Rhi::RHI::SupportsRuntimeShaderPipelines` | `Engine/RHI/RHI.hpp` | Запрос возможности: Vulkan возвращает true, Null и Metal — false |

```bash
# Author the graph once, cook it in the build step
build/bin/fby_matc assets/materials/tinted.fgraph -o assets/materials/tinted.femmat

```

```cpp
// `InBytes` is the .femmat payload from the VFS or a package entry.
Render::CookedMaterialView ScopedMaterial;
if (!Render::DecodeCookedMaterial(InBytes, &ScopedMaterial))
{
    return false;
}

// One cache per device: acquire at load time, release at shutdown.
const Rhi::ShaderPipelineHandle ScopedPipeline = InMaterials.Acquire(InRhi, ScopedMaterial);

Rhi::MeshRenderItem ScopedItem;
ScopedItem.materialPipeline =
    ScopedPipeline.IsValid() ? ScopedPipeline.id : Rhi::kDefaultMaterialPipeline;

```

- Хеш графа — идентификатор пайплайна. Правка графа даёт новый хеш, поэтому старый пайплайн становится записью кэша без ссылок, а не меняется на месте.
- Маска контракта едет вместе с payload: сгенерированный вершинный этап, view projection во фрагменте, push-константы материала, varying world/vertex position. Живая запись, чей хеш приходит с другим контрактом, отклоняется: это другой шейдер.
- `Acquire()` добавляет ссылку, `Release()` снимает. Пайплайн с нулём ссылок остаётся резидентным, поэтому материал, берущийся раз в кадр, не пересоздаётся; он уничтожается только при LRU-эвикции, `ReleaseAll()` или `Shutdown()`.
- Кэш не аллоцирует и не форматирует текст на кадровом пути; `LastError()` возвращает фиксированный буфер ошибки.
- Бэкенды без рантайм-пайплайнов остаются на шейдерах движка. Спрашивайте `SupportsRuntimeShaderPipelines()`, а не определяйте это по невалидному хендлу.

| Payload | Поля | Проверяется при загрузке |
|---|---|---|
| `.fgraph` | magic `FBYG`, версия, счётчики узлов/пинов/параметров, узлы, пины, параметры | magic, версия, счётчики против ёмкости, точный размер, виды узлов, индексы пинов и параметров |
| `.femmat` | magic `FBYM`, версия, маска контракта, хеш графа, длины вершинного и фрагментного этапов | magic, версия, размер против длин, оба SPIR-V модуля, согласованность контракта |

Редактор сохраняет живое редактирование поверх того же кэша: `Fby::Editor::MaterialGraphRuntime` перекомпилирует граф при смене хеша, отдаёт ошибки по вкладкам и передаёт владение пайплайном в `MaterialPipelineCache`. `MaterialGraphRuntime::SetDomain` переключает живую компиляцию между mesh- и sprite-контрактом, а `SaveGraph`/`LoadGraph` панели пишут и читают payload `.fgraph`, который потребляет cooker. Поставленная сборка обходится без glslang и вызывает только `DecodeCookedMaterial` и `Acquire`.

Спрайты получают материалы через третий артефакт: `MaterialCompileOptions::domain` выбирает спрайтовый домен, где сгенерированный фрагментный этап совпадает с varying-ами `quad.vert` (`vUv`, `vColor`, `vLayer` и `vWorld`, если узел его требует), сэмплирует слой атласа элемента через `vLayer`. `Time` доступен: спрайтовый push-блок несёт часы материала по смещению 80, поэтому сгенерированный этап читает `pc.materialParams.x` так же, как меш-шейдер. `fby_matc --sprite` кукает такой вариант. `DrawOrder::MaterialBucket` помещает спрайт в бакет, `ChangeMaskRenderExtract::SetMaterialPipelines` сопоставляет бакеты с id кэшированных пайплайнов, а экстракт выдаёт один `DrawBatch` на бакет с уже разрешённым `DrawBatch::pipeline`. 64-битный ключ несёт бакет (16 слой, 8 материал, 32 глубина, 8 последовательность), поэтому материальный батч не разрывается глубиной. Узлы, которым нужна меш-проекция (`Fresnel`, `VertexPosition`), отклоняются в спрайтовом домене с понятной причиной.


## 9. Физика: Box2D v3, суставы, ткань и SdfCollisionSystem

PhysicsSystem владеет одним b2WorldId и отдаёт его расширениям как упакованный uint32, поэтому box2d.h не попадает в публичные заголовки. Тела зеркалируются из компонентов RigidBody2D/Collider2D в Box2D, шагают с фиксированной частотой и читаются обратно перед интерполяцией. PhysicsJointSystem строит фиксированный граф соединений: цепь, верёвка и пружины с пределами прочности, накоплением повреждений, разрезами из геймплея, ремонтом, пересборкой на месте и связями. ClothSystem интегрирует Verlet-сетку и проецирует её на те же коллайдеры Box2D.

Контракт SdfCollisionSystem:

| Член | Контракт |
|---|---|
| SetField(const SdfField&) | Принимает невладеющий mdspan на row-major сетку. Отрицательные значения — твёрдое тело; память поля принадлежит рендереру/рельефу, а не системе. |
| Step(registry, physics, fixedDt) | Сэмплирует сетку в углах и центре тела, превращает градиент в нормаль, применяет импульс выталкивания с учётом массы, отражает внутрь направленную скорость и пишет исправленное состояние обратно. Возвращает число исправленных тел. |
| Update(SystemContext&) | ECS-обёртка вокруг Step(); вызов без физики ничего не делает. |
| Contacts() | Фиксированный массив SdfContact {entity, point, normal, penetration, impulse, sequence}. |
| SdfCollisionDesc | pushOut, restitution, maxImpulse, subSamples, dynamicOnly. |

```cpp
Fby::SdfCollisionDesc desc;
desc.pushOut = 0.65f;
desc.restitution = 0.05f;
desc.subSamples = 2;
sdf.Initialize(desc);
sdf.SetField(terrain.Field());
sdf.Register(graph);                    // Phase::FixedUpdate

// Viewport overlay: red vectors whose length is the penetration.
debug.Build(context, Fby::Editor::PhysicsDebugSdf, &sdf);
```

> SdfCollisionSystem не входит в SystemContext. Хост, которому нужна SDF-физика, регистрирует систему явно через Register(graph) и хранит экземпляр живым; отладочный проход Viewport принимает тот же указатель.


## 10. NetRollbackSession: детерминированный роллбэк

Сессия связывает snapshot v5, CommandLog, Pcg32 и геймплейные компоненты (теги, атрибуты, эффекты и скиллы) в один контур роллбэка с фиксированной ёмкостью. После Initialize() аллокаций нет: одно кольцо снапшотов, одно кольцо ввода на игрока, фиксированная карта сущностей и фиксированные метаданные кадров. Колбэк симуляции — указатель на функцию плюс void*.

| Метод | Контракт |
|---|---|
| Initialize(registry, schema, config, arena) | Настройка без аллокаций с ареной снапшотов, принадлежащей вызывающему. |
| Initialize(registry, schema, config) | Удобная перегрузка; выделяет одну арену (единственная аллокация сессии). |
| SetSimulation(fn, user) / SetCommandLog(log) | Привязывает чистую функцию симуляции и лог структурных команд. |
| Start(rng) | Фиксирует базовый тик и сохраняет RNG сессии. Обязателен до Step(). |
| Step(localInput) | Симулирует ровно один тик с предсказанием отсутствующего удалённого ввода и фиксирует снапшот. |
| SubmitRemoteInput(player, frame) | Сохраняет опоздавший или текущий ввод; кадр для уже симулированного тика помечает ожидающий роллбэк. |
| ReplayPending(report) | Восстанавливает снапшот перед самым ранним исправленным тиком, восстанавливает RNG и курсор CommandLog, пересимулирует, фиксирует новые снапшоты и сообщает изменённые тики. |
| CompareChecksums(tick, snap, state, out) | Сравнивает удалённый отчёт с сохранённым кадром; две контрольные суммы отделяют побайтовый desync от desync игрового состояния. |

```cpp
Fby::Net::NetRollbackSession::Config cfg;
cfg.playerCount = 2;
cfg.localPlayer = 0;
cfg.rollbackTicks = 64;
session.Initialize(registry, schema, cfg, arena);
session.SetSimulation(&SimulateTick, &game);
session.Start(pcg);

while (running)
{
    session.SubmitRemoteInput(remote, received);
    session.Step(localInput);
    if (session.RollbackPending())
    {
        Fby::Net::RollbackReport report;
        session.ReplayPending(&report);
        if (report.changedTicks > 0)
        {
            LogBoy().Log(Fby::Editor::LogBoySeverity::Desync, "net",
                         "rollback {}..{} changed {}", report.fromTick, report.toTick,
                         report.changedTicks);
        }
    }
}
```

- NetInputFrame — 24 байта целочисленных осей (int16 с фиксированной точкой). Float запрещён: битовый шаблон float не гарантированно переживает другой компилятор.
- Result перечисляет все режимы отказа (NotInitialized, BufferFull, RollbackTooOld, ChecksumMismatch, ...); ResultName() — написание для логов.
- Сессия некопируемая и владеет ареной, если использована удобная перегрузка.


## 11. Аудио: микшер, DSP-граф, лимитер

AudioSystem владеет устройством miniaudio и фиксированным микшером. DspGraph маршрутизирует узлы в шины, поддерживает дакинг и реверберацию и принимает команды через lock-free кольцо, поэтому игровой поток не блокирует аудиоколбэк. Limiter.hpp — финальная стадия true-peak.

- DeafMode глушит граф на корне Master и превращает звуковые триггеры в строки субтитров; это политика аудиосистемы, а не пост-эффект.
- Ogg/Opus декодируется вне аудиопотока; микшер видит только приготовленные .fpcm буферы.


## 12. PinkUI и WidgetCanvasSystem

PinkUI — игровой интерфейс: канвас фиксированной ёмкости, mdspan-раскладка сетки, разрешение якорей, hit-testing и упаковка в один батч. WidgetCanvasSystem превращает WidgetCanvas в FrameOutput (элементы отрисовки) без аллокаций; глифы берутся из атласа .fbyfont, сматпленного через VFS, поэтому поставочный рантайм не содержит TTF-растеризатора. Глифы существуют только в атласе: встроенного 8x8-фолбэка нет, а текстовый кадр без смонтированного массива шрифтов отклоняется, а не рисуется неверным слоем.

| Член | Контракт |
|---|---|
| WidgetCanvasSystem::MountFonts(RHI&) | Обязательный этап холодного старта в главном потоке. Собирает GPU-массив из набора .fbyfont в VFS; ошибка — это PANIC в лог плюс FBY_ASSERT_MSG, а не фолбэк. |
| BuildFrame(context, canvas, output) | Раскладывает и отсекает один канвас, возвращая BuildStatus и заполняя FrameOutput. Текстовый канвас без актуального смонтированного атласа возвращает FontsNotMounted и ничего не пишет. Без аллокаций; безопасно вызывать из Phase::Render. |
| Update(context, canvases, output) | Собирает все активные канвасы в один вывод; хост отправляет результат одним батчем. Нарушение шрифтового контракта останавливает проход и ставит fontContractViolated. |
| WidgetCanvas::SetDisplayScale | Плотность из CameraStackSystem::DisplayScale; размеры шрифтов округляются до целых физических пикселей. |
| OledShiftState | Целочисленный 2-пиксельный сдвиг для защиты OLED; ApplyOledShift() привязывает его к пулу канвасов. |

WidgetCanvasSystem — ещё и поверхность доступности: BlindMode читает подписи виджетов для нарративного лога, DeafMode получает виджеты субтитров, а матрицы цветокоррекции и FlashGuard применяются к цветам канваса перед упаковкой.


## 13. Ядро доступности (BlindMode, DeafMode, FlashGuard)

Доступность — это политика кадра, а не оверлей. BlindMode пропускает игровой проход, indirect-вызовы и диспатч скиннинга и публикует ограниченный нарративный лог. DeafMode глушит шину Master и превращает звуковые триггеры в SubtitleLines. FlashGuard сжимает резкие изменения яркости. Цветокоррекция использует опубликованные матрицы Machado 2009, а SrgbRelativeLuminance()/ContrastRatio() реализуют WCAG 2.2 для проверки при авторинге.

| Элемент | Контракт |
|---|---|
| NarrativeEvent | SPSC-кольцо на 128 слотов с текстовым payload 160 байт; поток симуляции пишет, поток речи читает. |
| ColorVisionMatrix(deficiency) | Возвращает матрицу 3x3 для протанопии, дейтеранопии или тританопии. |
| ApplyColorVision(color, matrix, strength) | Применяет матрицу в линейном свете к sRGB Color32. |
| FlashGuard::Evaluate(luminance, dt) | Возвращает усиление для текущего кадра; большое изменение сжимается до порога. |
| EditorSettings::ide* | Те же политики применяются к самому окну редактора: коррекция Machado, высокий контраст WCAG, FlashGuard, навигация с клавиатуры и LogBoy как приёмник нарратива. |


## 14. Конвейер ассетов, VFS и локализация

Ассеты адресуются относительным путём внутри VFS. Архив .fempak монтируется с приоритетом; каталог-монитор может его перекрывать — так редактор пишет loose-файл до того, как кукер вернёт его в архив. Read() возвращает span на mmap, который валиден до размонтирования или горячей перезагрузки файла.

| Формат | Владелец | Назначение |
|---|---|---|
| .fempak | FempakCooker | Неизменяемый поставочный архив с индексом и опциональным шифрованием. |
| .fbyfont | FontAtlas (PinkUI) | Испечённый атлас глифов: [header][glyph records][pixels]; без TTF-растеризатора в рантайме. |
| .femeta | AssetDatabase | Типизированные метаданные, настройки импорта и стабильный AssetId. |
| .fbin | LocTool | Скомпилированная таблица локализации; сам интерфейс редактора — только английский. |

> Интерфейс редактора не локализуется: меню, заголовки панелей, ошибки и инспектор — только на английском. Локализация относится к игровому контенту и документации, где таблица .fbin и руководство EN/RU покрывают поставляемые языки.


## 15. Платформенные сервисы: Steam, Epic Games Store, GOG

PlatformService — мост на стороне движка; PlatformAPI.h — плоский чистый C ABI, который реализует плагин SDK магазина. Провайдер загружается динамически, резолвит только документированные точки входа и преобразует все типы магазина на границе, поэтому игра никогда не включает steam_api.h, EOSSDK или заголовки Galaxy.

| Провайдер | Статус в этом дереве | Примечания |
|---|---|---|
| SteamProvider.cpp | Реализован | Плоский API Steamworks через таблицу функций. |
| EgsProvider.cpp | Реализован | Провайдер Epic Online Services через тот же C ABI. |
| GogProvider.cpp | Реализован (шим) | Загружает galaxy64.dll / libgalaxy64.so / libgalaxy64.dylib во время работы и резолвит плоский ABI шима fby_gog_*. Шим поставляет игра, собранная под свою версию SDK. Galaxy — библиотека C++-интерфейсов, поэтому шим обязателен. GogAchievementSystem и GogStorageSystem пишут бинарный PlatformCommandLog в Phase::PostUpdate. |
| fby_fontc | Реализован | Офлайн-кукер TTF -> .fbyfont (stb_truetype, RGBA8-атлас, проверяемый заголовок). |

```cpp
// Pure C ABI: the provider is a shared library, not a C++ dependency.
typedef struct FbyPlatformApi {
    uint32_t version;
    void* user;
    bool (*init)(void* user);
    void (*shutdown)(void* user);
    void (*unlockAchievement)(void* user, const char* id);
    bool (*writeCloudFile)(void* user, const char* name, const uint8_t* data, uint32_t size);
} FbyPlatformApi;
```


## 16. Редактор: дашборд, шрифты, LogBoy, Scene Diagnostics

Дашборд — единственная поверхность редактора, существующая до открытия проекта. Он устанавливает набор шрифтов редактора до первого кадра, поэтому первое окно уже отрисовано Inter с учётом физического DPI. Новый проект создаётся в Projects/ в корне движка, путь определяет DefaultProjectsRoot() — никогда рабочий каталог процесса и никогда каталог установки.

| Компонент | Контракт |
|---|---|
| EditorFonts | Загружает Inter и JetBrains Mono из mmap VFS. Субпиксельные продвижения (PixelSnapH=false), авто-оверсэмплинг, без bitmap-fallback, динамический атлас печётся под плотность пикселей вьюпорта. Устаревшие бэкенды получают RasterizerDensity + обратный FontGlobalScale. |
| ProjectDashboard | Initialize(paths, config, visible, window, vfs, fonts, uiScale): устанавливает типографику, определяет расположение Projects/ по умолчанию и создаёт его. |
| LogBoySystem / LogBoyPanel | Фиксированное кольцо на 4096 записей с текстом 192 байта, уровнями Trace/Info/Warning/Critical/Desync, FNV-1a хэшами систем и фильтрацией по цвету и хэшу. |
| SceneDiagnosticsPanel | Столбцы и тепловая карта занятости потоков, байты FrameArena, количество MeshRenderItem, indirect-вызовы, draw calls и точное время симуляции в микросекундах с avg/p99/peak. |
| PhysicsDebugVisualizer | Геометрия линий и точек в фиксированных буферах для тел Box2D, верёвок, ткани и нормалей SDF, рисуется поверх Viewport через карту мир→экран, заданную вызывающим. |

> Атлас .fbyfont — испечённый ассет PinkUI и не может питать векторный растеризатор ImGui. Поэтому редактор загружает TTF Inter из того же монтирования VFS; заголовок атласа всё равно валидируется при старте, чтобы испорченный атлас игрового UI обнаруживал редактор, а не игрок.


## 17. Пошагово: игра с пустой папки

Этот пример создаёт играбельный проект со спрайтом, физикой, вводом с клавиатуры, HUD на PinkUI и детерминированной rollback-сессией. Каждый шаг соответствует реальному заголовку из индекса.

- 1. Запустите редактор и создайте проект через New Project. Дашборд создаёт <engineRoot>/Projects/<name> с assets/{textures,audio,scenes}, configs/ и project.femproj.
- 2. Импортируйте спрайт в assets/textures. Наблюдатель импортирует файл, пишет .femeta и готовит слой текстурного массива .ftex.
- 3. Создайте assets/scenes/main.toml с одним SpriteRenderer, одним Transform2D и одним RigidBody2D + Collider2D.
- 4. Зарегистрируйте системы: ввод в PreUpdate, физика в FixedUpdate, движение в Update, PinkUI в Render.
- 5. Дайте камере CameraStackSystem и вызовите SetDisplayScale() из SDL_GetWindowDisplayScale, чтобы глифы UI были чёткими на HiDPI.
- 6. Подключите AccessibilityKernel из настроек проекта; BlindMode и DeafMode читаются из project.femproj, а не зашиты в код.
- 7. Создайте NetRollbackSession на матч, привяжите чистый колбэк SimulateTick и подавайте значения NetInputFrame.
- 8. Логируйте через LogBoy: уровень + хэш системы + сообщение в фиксированном формате. Откройте панель LogBoy и фильтруйте по Desync при тестировании сети.
- 9. Включите Physics Debug Draw во Viewport при настройке коллайдеров и ткани.
- 10. Соберите архив через FempakCooker, прогоните ctest и стилевой гейт, затем поставляйте цель только с движком.

```cpp
// 4-7 in code: systems, accessibility and rollback for one match.
Fby::Ecs::Schedule schedule;
schedule.Add(Fby::Ecs::Phase::PreUpdate, "input", &InputSystem);
schedule.Add(Fby::Ecs::Phase::FixedUpdate, "physics", &PhysicsStepSystem, &physics);
schedule.Add(Fby::Ecs::Phase::Update, "movement", &MovementSystem);
schedule.Add(Fby::Ecs::Phase::Render, "ui", &UiSystem, &ui);

Fby::AccessibilityKernel kernel;
kernel.Configure(settings);              // blind/deaf/colour/flash

Fby::Net::NetRollbackSession session;
if (session.Initialize(registry, schema, cfg, arena) != Fby::Net::NetRollbackSession::Result::Ok)
{
    LogBoy().Log(Fby::Editor::LogBoySeverity::Critical, "net", "rollback init failed");
}
```


## 18. Тесты, стилевой гейт и чек-лист релиза

Набор не использует фреймворк: каждый тест — исполняемый файл с main(), возвращающим ненулевой код при ошибке, поэтому одинаково работает на headless CI-агенте, консольном devkit и рабочей станции разработчика. На релиз-кандидате проходит весь набор.

```bash
ctest --test-dir build --output-on-failure          # the full suite
python Source/Tools/Dev/CheckStyle.py               # style gate
python Documentation/Tools/GenerateAPIReference.py  # this manual
python Documentation/Tools/GenerateDocumentation.py
python Documentation/Tools/GenerateApplicationGuide.py
```

- Релизная сборка включает утверждения и один раз прогоняет весь набор с FBY_ENABLE_ASSERTS=1, потому что на RC существовали две скрытые ошибки компиляции debug-сборки в JobSystem.
- POSIX-ветка WriteFileAtomically() и LogLine() должна компилироваться на Linux/macOS в CI; Windows-only сборка их не видела.
- Стилевой гейт должен сообщать ноль ошибок, прежде чем коммит может считаться релизным.


## 19. Открытая работа

Открытая работа отслеживается в трекере задач проекта.


## 20. Приложение A - сгенерированный индекс символов

Генерируется из заголовков при каждом запуске. Символ из этого списка объявлен в поставляемом дереве; заголовок и строка — авторитетное расположение.


### Editor/Infrastructure (119)

| Символ | Вид | Заголовок | Строка |
|---|---|---|---|
| `AccessibilityEffectRegistry` | Infrastructure | `Source/Editor/Infrastructure/EditorSystem.hpp` | 47 |
| `AIStateTreeCanvasPanel` | Infrastructure | `Source/Editor/Infrastructure/EditorSystem.hpp` | 54 |
| `AnimationGraphCanvasPanel` | Infrastructure | `Source/Editor/Infrastructure/EditorSystem.hpp` | 55 |
| `AnimationTimelinePanel` | Infrastructure | `Source/Editor/Infrastructure/EditorSystem.hpp` | 56 |
| `AssetDatabaseWatchSystem` | Infrastructure | `Source/Editor/Infrastructure/AssetDatabaseWatchSystem.hpp` | 20 |
| `AssetDatabaseWatchSystem` | Infrastructure | `Source/Editor/Infrastructure/EditorSystem.hpp` | 57 |
| `AssetImportService` | Infrastructure | `Source/Editor/Infrastructure/AssetImportService.hpp` | 30 |
| `AssetImportService` | Infrastructure | `Source/Editor/Infrastructure/EditorSystem.hpp` | 58 |
| `Callbacks` | Infrastructure | `Source/Editor/Infrastructure/AssetDatabaseWatchSystem.hpp` | 38 |
| `Change` | Infrastructure | `Source/Editor/Infrastructure/AssetDatabaseWatchSystem.hpp` | 126 |
| `Codec` | Infrastructure | `Source/Editor/Infrastructure/CommandStack.hpp` | 230 |
| `Command` | Infrastructure | `Source/Editor/Infrastructure/CommandStack.hpp` | 244 |
| `CommandKind` | Infrastructure | `Source/Editor/Infrastructure/CommandStack.hpp` | 33 |
| `CommandStack` | Infrastructure | `Source/Editor/Infrastructure/CommandStack.hpp` | 30 |
| `CommandStack` | Infrastructure | `Source/Editor/Infrastructure/EditorSystem.hpp` | 59 |
| `Config` | Infrastructure | `Source/Editor/Infrastructure/AssetDatabaseWatchSystem.hpp` | 27 |
| `Config` | Infrastructure | `Source/Editor/Infrastructure/AssetImportService.hpp` | 39 |
| `Config` | Infrastructure | `Source/Editor/Infrastructure/CommandStack.hpp` | 90 |
| `ContentBrowserPanel` | Infrastructure | `Source/Editor/Infrastructure/EditorSystem.hpp` | 60 |
| `CookRequest` | Infrastructure | `Source/Editor/Infrastructure/AssetImportService.hpp` | 141 |
| `CookResult` | Infrastructure | `Source/Editor/Infrastructure/AssetImportService.hpp` | 152 |
| `EditorCamera` | Infrastructure | `Source/Editor/Infrastructure/EditorSystem.hpp` | 101 |
| `EditorDesc` | Infrastructure | `Source/Editor/Infrastructure/EditorSystem.hpp` | 72 |
| `EditorFontRole` | Infrastructure | `Source/Editor/Infrastructure/EditorFonts.hpp` | 22 |
| `EditorFonts` | Infrastructure | `Source/Editor/Infrastructure/EditorFonts.hpp` | 84 |
| `EditorFonts` | Infrastructure | `Source/Editor/Infrastructure/EditorTypography.hpp` | 7 |
| `EditorFontSpec` | Infrastructure | `Source/Editor/Infrastructure/EditorFonts.hpp` | 38 |
| `EditorFontStatus` | Infrastructure | `Source/Editor/Infrastructure/EditorFonts.hpp` | 67 |
| `EditorPalette` | Infrastructure | `Source/Editor/Infrastructure/EditorTheme.hpp` | 77 |
| `EditorSettings` | Infrastructure | `Source/Editor/Infrastructure/EditorSettings.hpp` | 20 |
| `EditorSettings` | Infrastructure | `Source/Editor/Infrastructure/EditorTheme.hpp` | 14 |
| `EditorSettingsPanel` | Infrastructure | `Source/Editor/Infrastructure/EditorSystem.hpp` | 61 |
| `EditorSymbolColors` | Infrastructure | `Source/Editor/Infrastructure/EditorTheme.hpp` | 61 |
| `EditorSymbolRole` | Infrastructure | `Source/Editor/Infrastructure/EditorTheme.hpp` | 42 |
| `EditorSystem` | Infrastructure | `Source/Editor/Infrastructure/EditorSystem.hpp` | 110 |
| `EditorThemeId` | Infrastructure | `Source/Editor/Infrastructure/EditorTheme.hpp` | 31 |
| `Entry` | Infrastructure | `Source/Editor/Infrastructure/TextureThumbnails.hpp` | 52 |
| `ExternalClientDesc` | Infrastructure | `Source/Editor/Infrastructure/PlayInEditor.hpp` | 37 |
| `FrameGenerationMode` | Infrastructure | `Source/Editor/Infrastructure/ProjectConfig.hpp` | 139 |
| `FrameGenerationPolicySetting` | Infrastructure | `Source/Editor/Infrastructure/ProjectConfig.hpp` | 132 |
| `HudTargetMode` | Infrastructure | `Source/Editor/Infrastructure/ProjectConfig.hpp` | 148 |
| `ImFont` | Infrastructure | `Source/Editor/Infrastructure/EditorFonts.hpp` | 10 |
| `ImGuiIO` | Infrastructure | `Source/Editor/Infrastructure/EditorFonts.hpp` | 11 |
| `Impl` | Infrastructure | `Source/Editor/Infrastructure/EditorSystem.hpp` | 244 |
| `InteractionTransaction` | Infrastructure | `Source/Editor/Infrastructure/EditTransactions.hpp` | 16 |
| `LoadedSource` | Infrastructure | `Source/Editor/Infrastructure/EditorFonts.hpp` | 138 |
| `MaterialGraphPanel` | Infrastructure | `Source/Editor/Infrastructure/EditorSystem.hpp` | 62 |
| `MaterialGraphRuntime` | Infrastructure | `Source/Editor/Infrastructure/MaterialGraphRuntime.hpp` | 41 |
| `MaterialRuntimeStats` | Infrastructure | `Source/Editor/Infrastructure/MaterialGraphRuntime.hpp` | 25 |
| `MotionComfortSettings` | Infrastructure | `Source/Editor/Infrastructure/ProjectConfig.hpp` | 171 |
| `NetSession` | Infrastructure | `Source/Editor/Infrastructure/EditorSystem.hpp` | 32 |
| `NetworkDiagnosticsPanel` | Infrastructure | `Source/Editor/Infrastructure/EditorSystem.hpp` | 63 |
| `OitMode` | Infrastructure | `Source/Editor/Infrastructure/ProjectConfig.hpp` | 99 |
| `OpaqueRenderMode` | Infrastructure | `Source/Editor/Infrastructure/ProjectConfig.hpp` | 92 |
| `PendingEdit` | Infrastructure | `Source/Editor/Infrastructure/CommandStack.hpp` | 270 |
| `PendingSpawn` | Infrastructure | `Source/Editor/Infrastructure/CommandStack.hpp` | 282 |
| `PhysicsDebugFlags` | Infrastructure | `Source/Editor/Infrastructure/PhysicsDebugDraw.hpp` | 19 |
| `PhysicsDebugFrame` | Infrastructure | `Source/Editor/Infrastructure/PhysicsDebugDraw.hpp` | 47 |
| `PhysicsDebugLine` | Infrastructure | `Source/Editor/Infrastructure/PhysicsDebugDraw.hpp` | 32 |
| `PhysicsDebugPoint` | Infrastructure | `Source/Editor/Infrastructure/PhysicsDebugDraw.hpp` | 40 |
| `PhysicsDebugVisualizer` | Infrastructure | `Source/Editor/Infrastructure/PhysicsDebugDraw.hpp` | 58 |
| `PiePlayerInput` | Infrastructure | `Source/Editor/Infrastructure/PlayInEditor.hpp` | 27 |
| `PlaybackIcon` | Infrastructure | `Source/Editor/Infrastructure/EditorWidgets.hpp` | 12 |
| `PlayInEditor` | Infrastructure | `Source/Editor/Infrastructure/PlayInEditor.hpp` | 44 |
| `PluginHost` | Infrastructure | `Source/Editor/Infrastructure/EditorSystem.hpp` | 42 |
| `Profiler` | Infrastructure | `Source/Editor/Infrastructure/EditorSystem.hpp` | 27 |
| `ProjectConfig` | Infrastructure | `Source/Editor/Infrastructure/EditorSystem.hpp` | 67 |
| `ProjectConfig` | Infrastructure | `Source/Editor/Infrastructure/ProjectConfig.hpp` | 286 |
| `ProjectConfigStore` | Infrastructure | `Source/Editor/Infrastructure/EditorSystem.hpp` | 64 |
| `ProjectConfigStore` | Infrastructure | `Source/Editor/Infrastructure/ProjectConfig.hpp` | 350 |
| `ProjectDashboard` | Infrastructure | `Source/Editor/Infrastructure/EditorSystem.hpp` | 65 |
| `ProjectPaths` | Infrastructure | `Source/Editor/Infrastructure/EditorSystem.hpp` | 68 |
| `ProjectPaths` | Infrastructure | `Source/Editor/Infrastructure/ProjectConfig.hpp` | 308 |
| `ProjectPaths` | Infrastructure | `Source/Editor/Infrastructure/ReleaseCooker.hpp` | 21 |
| `ProjectPluginEntry` | Infrastructure | `Source/Editor/Infrastructure/ProjectConfig.hpp` | 163 |
| `ProjectSettings` | Infrastructure | `Source/Editor/Infrastructure/ProjectConfig.hpp` | 182 |
| `ProjectSettings` | Infrastructure | `Source/Editor/Infrastructure/ReleaseCooker.hpp` | 22 |
| `ProjectSettingsPanel` | Infrastructure | `Source/Editor/Infrastructure/EditorSystem.hpp` | 66 |
| `Range` | Infrastructure | `Source/Editor/Infrastructure/CommandStack.hpp` | 262 |
| `ReleaseCookConfig` | Infrastructure | `Source/Editor/Infrastructure/ReleaseCooker.hpp` | 70 |
| `ReleaseCooker` | Infrastructure | `Source/Editor/Infrastructure/ReleaseCooker.hpp` | 158 |
| `ReleaseCookProgress` | Infrastructure | `Source/Editor/Infrastructure/ReleaseCooker.hpp` | 121 |
| `ReleaseCookResult` | Infrastructure | `Source/Editor/Infrastructure/ReleaseCooker.hpp` | 142 |
| `ReleaseCookStage` | Infrastructure | `Source/Editor/Infrastructure/ReleaseCooker.hpp` | 28 |
| `ResolvedHudSettings` | Infrastructure | `Source/Editor/Infrastructure/ResolvedRenderSettings.hpp` | 35 |
| `ResolvedRenderSettings` | Infrastructure | `Source/Editor/Infrastructure/ResolvedRenderSettings.hpp` | 14 |
| `Result` | Infrastructure | `Source/Editor/Infrastructure/CommandStack.hpp` | 45 |
| `Result` | Infrastructure | `Source/Editor/Infrastructure/PlayInEditor.hpp` | 47 |
| `SdfCollisionSystem` | Infrastructure | `Source/Editor/Infrastructure/PhysicsDebugDraw.hpp` | 13 |
| `Services` | Infrastructure | `Source/Editor/Infrastructure/PlayInEditor.hpp` | 224 |
| `SettingDesc` | Infrastructure | `Source/Editor/Infrastructure/SettingsSchema.hpp` | 28 |
| `SettingKind` | Infrastructure | `Source/Editor/Infrastructure/SettingsSchema.hpp` | 20 |
| `SettingScope` | Infrastructure | `Source/Editor/Infrastructure/SettingsSchema.hpp` | 11 |
| `Slot` | Infrastructure | `Source/Editor/Infrastructure/MaterialGraphRuntime.hpp` | 118 |
| `Source` | Infrastructure | `Source/Editor/Infrastructure/EditTransactions.hpp` | 19 |
| `SpawnComponent` | Infrastructure | `Source/Editor/Infrastructure/CommandStack.hpp` | 296 |
| `SpawnHeader` | Infrastructure | `Source/Editor/Infrastructure/CommandStack.hpp` | 289 |
| `SpawnTransaction` | Infrastructure | `Source/Editor/Infrastructure/EditTransactions.hpp` | 190 |
| `StableSource` | Infrastructure | `Source/Editor/Infrastructure/EditorFonts.hpp` | 145 |
| `Stats` | Infrastructure | `Source/Editor/Infrastructure/AssetDatabaseWatchSystem.hpp` | 88 |
| `Stats` | Infrastructure | `Source/Editor/Infrastructure/AssetImportService.hpp` | 55 |
| `Stats` | Infrastructure | `Source/Editor/Infrastructure/CommandStack.hpp` | 97 |
| `TemporalUpscaleMode` | Infrastructure | `Source/Editor/Infrastructure/ProjectConfig.hpp` | 155 |
| `TextureArrayAsset` | Infrastructure | `Source/Editor/Infrastructure/AssetImportService.hpp` | 23 |
| `TextureBinding` | Infrastructure | `Source/Editor/Infrastructure/AssetImportService.hpp` | 168 |
| `TextureThumbnailCache` | Infrastructure | `Source/Editor/Infrastructure/TextureThumbnails.hpp` | 17 |
| `UpscaleMode` | Infrastructure | `Source/Editor/Infrastructure/ProjectConfig.hpp` | 106 |
| `UpscalePolicy` | Infrastructure | `Source/Editor/Infrastructure/ProjectConfig.hpp` | 115 |
| `UpscalerBackendSetting` | Infrastructure | `Source/Editor/Infrastructure/ProjectConfig.hpp` | 122 |
| `UserSettings` | Infrastructure | `Source/Editor/Infrastructure/EditorSystem.hpp` | 69 |
| `UserSettings` | Infrastructure | `Source/Editor/Infrastructure/UserSettings.hpp` | 17 |
| `VirtualFileSystem` | Infrastructure | `Source/Editor/Infrastructure/AssetImportService.hpp` | 20 |
| `VirtualFileSystem` | Infrastructure | `Source/Editor/Infrastructure/EditorFonts.hpp` | 15 |
| `VirtualFileSystem` | Infrastructure | `Source/Editor/Infrastructure/EditorSystem.hpp` | 18 |
| `VirtualTextureSettings` | Infrastructure | `Source/Editor/Infrastructure/EditorSystem.hpp` | 37 |
| `VirtualTextureSettings` | Infrastructure | `Source/Editor/Infrastructure/ProjectConfig.hpp` | 32 |
| `VirtualTextureSystem` | Infrastructure | `Source/Editor/Infrastructure/EditorSystem.hpp` | 22 |
| `VisualScriptPanel` | Infrastructure | `Source/Editor/Infrastructure/EditorSystem.hpp` | 70 |
| `WindowManager` | Infrastructure | `Source/Editor/Infrastructure/EditorFonts.hpp` | 16 |

### Editor/Panels (90)

| Символ | Вид | Заголовок | Строка |
|---|---|---|---|
| `Action` | Panels | `Source/Editor/Panels/ProjectDashboard.hpp` | 25 |
| `AIStateTreeCanvasPanel` | Panels | `Source/Editor/Panels/AIStateTreeCanvasPanel.hpp` | 55 |
| `AIStateTreeCompiled` | Panels | `Source/Editor/Panels/AIStateTreeCanvasPanel.hpp` | 35 |
| `AIStateTreeDocument` | Panels | `Source/Editor/Panels/AIStateTreeCanvasPanel.hpp` | 24 |
| `AnimationGraphCanvasPanel` | Panels | `Source/Editor/Panels/AnimationGraphCanvasPanel.hpp` | 125 |
| `AnimationGraphDocument` | Panels | `Source/Editor/Panels/AnimationGraphCanvasPanel.hpp` | 88 |
| `AnimationGraphLink` | Panels | `Source/Editor/Panels/AnimationGraphCanvasPanel.hpp` | 74 |
| `AnimationGraphLinkKind` | Panels | `Source/Editor/Panels/AnimationGraphCanvasPanel.hpp` | 37 |
| `AnimationGraphNode` | Panels | `Source/Editor/Panels/AnimationGraphCanvasPanel.hpp` | 49 |
| `AnimationGraphNodeKind` | Panels | `Source/Editor/Panels/AnimationGraphCanvasPanel.hpp` | 29 |
| `AnimationTimelinePanel` | Panels | `Source/Editor/Panels/AnimationTimelinePanel.hpp` | 34 |
| `AssetImportService` | Panels | `Source/Editor/Panels/ContentBrowserPanel.hpp` | 24 |
| `AudioSystem` | Panels | `Source/Editor/Panels/AnimationTimelinePanel.hpp` | 15 |
| `AudioSystem` | Panels | `Source/Editor/Panels/VoiceDevicePanel.hpp` | 11 |
| `CanvasModelOps` | Panels | `Source/Editor/Panels/NodeCanvasPanel.hpp` | 21 |
| `CommandStack` | Panels | `Source/Editor/Panels/ContentBrowserPanel.hpp` | 25 |
| `CommandStack` | Panels | `Source/Editor/Panels/SettingsPanels.hpp` | 30 |
| `ComponentRegistry` | Panels | `Source/Editor/Panels/ScriptDebuggerPanel.hpp` | 18 |
| `Config` | Panels | `Source/Editor/Panels/ContentBrowserPanel.hpp` | 35 |
| `ContentBrowserPanel` | Panels | `Source/Editor/Panels/ContentBrowserPanel.hpp` | 27 |
| `DirectionalBlendSpacePanel` | Panels | `Source/Editor/Panels/DirectionalBlendSpacePanel.hpp` | 24 |
| `DragPayload` | Panels | `Source/Editor/Panels/ContentBrowserPanel.hpp` | 60 |
| `EditorFonts` | Panels | `Source/Editor/Panels/ProjectDashboard.hpp` | 20 |
| `EditorFonts` | Panels | `Source/Editor/Panels/VisualScriptPanel.hpp` | 20 |
| `EditorSettingsPanel` | Panels | `Source/Editor/Panels/SettingsPanels.hpp` | 91 |
| `EditSource` | Panels | `Source/Editor/Panels/AIStateTreeCanvasPanel.hpp` | 58 |
| `EditSource` | Panels | `Source/Editor/Panels/AnimationGraphCanvasPanel.hpp` | 128 |
| `Entry` | Panels | `Source/Editor/Panels/ContentBrowserPanel.hpp` | 43 |
| `FileDialog` | Panels | `Source/Editor/Panels/ContentBrowserPanel.hpp` | 140 |
| `ImNodesContext` | Panels | `Source/Editor/Panels/AIStateTreeCanvasPanel.hpp` | 13 |
| `ImNodesContext` | Panels | `Source/Editor/Panels/AnimationGraphCanvasPanel.hpp` | 15 |
| `LevelSettingsPanel` | Panels | `Source/Editor/Panels/LevelSettingsPanel.hpp` | 12 |
| `LicensesPanel` | Panels | `Source/Editor/Panels/LicensesPanel.hpp` | 11 |
| `LogBoyPanel` | Panels | `Source/Editor/Panels/LogBoyPanel.hpp` | 394 |
| `LogBoyRecord` | Panels | `Source/Editor/Panels/LogBoyPanel.hpp` | 105 |
| `LogBoyRing` | Panels | `Source/Editor/Panels/LogBoyPanel.hpp` | 121 |
| `LogBoySeverity` | Panels | `Source/Editor/Panels/LogBoyPanel.hpp` | 33 |
| `LogBoySystem` | Panels | `Source/Editor/Panels/LogBoyPanel.hpp` | 186 |
| `LogicGraphDocument` | Panels | `Source/Editor/Panels/VisualScriptPanel.hpp` | 23 |
| `MaterialGraphPanel` | Panels | `Source/Editor/Panels/MaterialGraphPanel.hpp` | 20 |
| `Mode` | Panels | `Source/Editor/Panels/ProjectDashboard.hpp` | 84 |
| `NetSession` | Panels | `Source/Editor/Panels/NetworkDiagnosticsPanel.hpp` | 6 |
| `NetworkDiagnosticsPanel` | Panels | `Source/Editor/Panels/NetworkDiagnosticsPanel.hpp` | 15 |
| `NodeCanvasPanel` | Panels | `Source/Editor/Panels/NodeCanvasPanel.hpp` | 54 |
| `ParticleFxPanel` | Panels | `Source/Editor/Panels/ParticleFxPanel.hpp` | 16 |
| `PIEAnimFrame` | Panels | `Source/Editor/Panels/AnimationGraphCanvasPanel.hpp` | 110 |
| `PieClientViewport` | Panels | `Source/Editor/Panels/PieClientViewport.hpp` | 15 |
| `PIEFrameSlot` | Panels | `Source/Editor/Panels/AnimationGraphCanvasPanel.hpp` | 302 |
| `PlaneView` | Panels | `Source/Editor/Panels/DirectionalBlendSpacePanel.hpp` | 122 |
| `ProjectConfigStore` | Panels | `Source/Editor/Panels/SettingsPanels.hpp` | 31 |
| `ProjectConfigStore` | Panels | `Source/Editor/Panels/VoiceDevicePanel.hpp` | 24 |
| `ProjectDashboard` | Panels | `Source/Editor/Panels/ProjectDashboard.hpp` | 22 |
| `ProjectSettingsPanel` | Panels | `Source/Editor/Panels/SettingsPanels.hpp` | 33 |
| `QueryKind` | Panels | `Source/Editor/Panels/LogBoyPanel.hpp` | 430 |
| `SceneDiagnosticsPanel` | Panels | `Source/Editor/Panels/SceneDiagnosticsPanel.hpp` | 56 |
| `SceneDiagnosticsSample` | Panels | `Source/Editor/Panels/SceneDiagnosticsPanel.hpp` | 15 |
| `ScriptDebuggerPanel` | Panels | `Source/Editor/Panels/ScriptDebuggerPanel.hpp` | 28 |
| `ScriptRuntime` | Panels | `Source/Editor/Panels/AnimationTimelinePanel.hpp` | 16 |
| `SdfCharacterPanel` | Panels | `Source/Editor/Panels/SdfCharacterPanel.hpp` | 9 |
| `SinkUser` | Panels | `Source/Editor/Panels/AnimationTimelinePanel.hpp` | 88 |
| `SkeletonInspectorState` | Panels | `Source/Editor/Panels/SkeletonAssetPanel.hpp` | 19 |
| `SpawnContext` | Panels | `Source/Editor/Panels/ContentBrowserPanel.hpp` | 68 |
| `SpriteAnimationInspectorState` | Panels | `Source/Editor/Panels/SpriteAnimationPanel.hpp` | 21 |
| `SystemContext` | Panels | `Source/Editor/Panels/SettingsPanels.hpp` | 24 |
| `SystemContext` | Panels | `Source/Editor/Panels/SkeletonAssetPanel.hpp` | 13 |
| `SystemContext` | Panels | `Source/Editor/Panels/SpriteAnimationPanel.hpp` | 13 |
| `SystemName` | Panels | `Source/Editor/Panels/LogBoyPanel.hpp` | 189 |
| `TextPreviewPanel` | Panels | `Source/Editor/Panels/TextPreviewPanel.hpp` | 24 |
| `TextureThumbnailCache` | Panels | `Source/Editor/Panels/SpriteAnimationPanel.hpp` | 19 |
| `TilemapEditorPanel` | Panels | `Source/Editor/Panels/TilemapEditorPanel.hpp` | 20 |
| `TimelinePanelContext` | Panels | `Source/Editor/Panels/AnimationTimelinePanel.hpp` | 22 |
| `TransportGroupPanel` | Panels | `Source/Editor/Panels/TransportGroupPanel.hpp` | 14 |
| `UserSettingsPanel` | Panels | `Source/Editor/Panels/UserSettingsPanel.hpp` | 16 |
| `VirtualFileSystem` | Panels | `Source/Editor/Panels/ContentBrowserPanel.hpp` | 18 |
| `VirtualFileSystem` | Panels | `Source/Editor/Panels/ProjectDashboard.hpp` | 13 |
| `VirtualFileSystem` | Panels | `Source/Editor/Panels/TextPreviewPanel.hpp` | 15 |
| `VirtualTexturePanel` | Panels | `Source/Editor/Panels/VirtualTexturePanel.hpp` | 18 |
| `VirtualTextureSystem` | Panels | `Source/Editor/Panels/VirtualTexturePanel.hpp` | 10 |
| `VisualScriptPanel` | Panels | `Source/Editor/Panels/ScriptDebuggerPanel.hpp` | 24 |
| `VisualScriptPanel` | Panels | `Source/Editor/Panels/VisualScriptPanel.hpp` | 32 |
| `VoiceDevicePanel` | Panels | `Source/Editor/Panels/VoiceDevicePanel.hpp` | 26 |
| `VoiceEffectPolicy` | Panels | `Source/Editor/Panels/VoiceDevicePanel.hpp` | 17 |
| `VoicePresetChoice` | Panels | `Source/Editor/Panels/VoiceDevicePanel.hpp` | 15 |
| `VoicePresetRegistry` | Panels | `Source/Editor/Panels/VoiceDevicePanel.hpp` | 14 |
| `VoicePresetSelection` | Panels | `Source/Editor/Panels/VoiceDevicePanel.hpp` | 16 |
| `VoiceReactiveSfxSettings` | Panels | `Source/Editor/Panels/VoiceDevicePanel.hpp` | 18 |
| `VoiceSession` | Panels | `Source/Editor/Panels/VoiceDevicePanel.hpp` | 12 |
| `VoiceSystem` | Panels | `Source/Editor/Panels/VoiceDevicePanel.hpp` | 13 |
| `WidgetCanvas` | Panels | `Source/Editor/Panels/SettingsPanels.hpp` | 18 |
| `WindowManager` | Panels | `Source/Editor/Panels/ProjectDashboard.hpp` | 14 |

### Engine/AI (7)

| Символ | Вид | Заголовок | Строка |
|---|---|---|---|
| `AIAgentSettings` | AI | `Source/Engine/AI/AIController.hpp` | 17 |
| `AIControllerComponent` | AI | `Source/Engine/AI/AIController.hpp` | 32 |
| `AIControllerSystem` | AI | `Source/Engine/AI/AIController.hpp` | 96 |
| `AIPerceptionComponent` | AI | `Source/Engine/AI/AIController.hpp` | 60 |
| `AIPerceptionSettings` | AI | `Source/Engine/AI/AIController.hpp` | 49 |
| `AIPerceptionSystem` | AI | `Source/Engine/AI/AIController.hpp` | 68 |
| `GridEntry` | AI | `Source/Engine/AI/AIController.hpp` | 83 |

### Engine/Assets (64)

| Символ | Вид | Заголовок | Строка |
|---|---|---|---|
| `AeadAlgorithm` | Assets | `Source/Engine/Assets/FemCrypto.hpp` | 18 |
| `AeadKey` | Assets | `Source/Engine/Assets/FemCrypto.hpp` | 39 |
| `AeadNonce` | Assets | `Source/Engine/Assets/FemCrypto.hpp` | 46 |
| `AeadTag` | Assets | `Source/Engine/Assets/FemCrypto.hpp` | 54 |
| `ArchiveBuildOptions` | Assets | `Source/Engine/Assets/FemArchiver.hpp` | 102 |
| `ArchiveCipher` | Assets | `Source/Engine/Assets/FemArchiver.hpp` | 29 |
| `ArchiveCodec` | Assets | `Source/Engine/Assets/FemArchiver.hpp` | 22 |
| `ArchiveEntryView` | Assets | `Source/Engine/Assets/FemArchiver.hpp` | 118 |
| `ArchiveInput` | Assets | `Source/Engine/Assets/FemArchiver.hpp` | 72 |
| `ArchiveProgress` | Assets | `Source/Engine/Assets/FemArchiver.hpp` | 85 |
| `ArchiveProgressStage` | Assets | `Source/Engine/Assets/FemArchiver.hpp` | 78 |
| `ArchiveReader` | Assets | `Source/Engine/Assets/FemArchiver.hpp` | 129 |
| `ArchiveReader` | Assets | `Source/Engine/Assets/FemArchiver.hpp` | 148 |
| `ArchiveStatus` | Assets | `Source/Engine/Assets/FemArchiver.hpp` | 36 |
| `AssetDatabase` | Assets | `Source/Engine/Assets/AssetDatabase.hpp` | 36 |
| `AssetDependency` | Assets | `Source/Engine/Assets/AssetMeta.hpp` | 83 |
| `AssetHandle` | Assets | `Source/Engine/Assets/AssetMeta.hpp` | 22 |
| `AssetImportSettings` | Assets | `Source/Engine/Assets/AssetMeta.hpp` | 45 |
| `AssetMeta` | Assets | `Source/Engine/Assets/AssetMeta.hpp` | 61 |
| `AssetRecord` | Assets | `Source/Engine/Assets/AssetDatabase.hpp` | 27 |
| `AstcBlock` | Assets | `Source/Engine/Assets/TextureCook.hpp` | 25 |
| `AtlasDesc` | Assets | `Source/Engine/Assets/TextureArray.hpp` | 31 |
| `AtlasLayer` | Assets | `Source/Engine/Assets/TextureArray.hpp` | 21 |
| `AtlasPackError` | Assets | `Source/Engine/Assets/SpriteAtlas.hpp` | 57 |
| `AtlasSprite` | Assets | `Source/Engine/Assets/SpriteAtlas.hpp` | 26 |
| `AtlasSpriteSource` | Assets | `Source/Engine/Assets/SpriteAtlas.hpp` | 18 |
| `AtlasUv` | Assets | `Source/Engine/Assets/SpriteAtlas.hpp` | 41 |
| `AudioClipHeader` | Assets | `Source/Engine/Assets/AudioClipFormat.hpp` | 87 |
| `BlockExtent` | Assets | `Source/Engine/Assets/TextureFormat.hpp` | 103 |
| `CleanRenderExtract` | Assets | `Source/Engine/Assets/ProjectScaffold.hpp` | 134 |
| `CookedAudioView` | Assets | `Source/Engine/Assets/AudioClipFormat.hpp` | 116 |
| `CookedTexture` | Assets | `Source/Engine/Assets/TextureCook.hpp` | 41 |
| `CookedTextureView` | Assets | `Source/Engine/Assets/TextureFormat.hpp` | 203 |
| `CookProgressFlags` | Assets | `Source/Engine/Assets/CookProgress.hpp` | 31 |
| `CookProgressRecord` | Assets | `Source/Engine/Assets/CookProgress.hpp` | 38 |
| `CookProgressStage` | Assets | `Source/Engine/Assets/CookProgress.hpp` | 18 |
| `CookTarget` | Assets | `Source/Engine/Assets/TextureCook.hpp` | 17 |
| `FanmHeader` | Assets | `Source/Engine/Assets/AnimationClipAsset.hpp` | 22 |
| `FemArchiver` | Assets | `Source/Engine/Assets/FemArchiver.hpp` | 131 |
| `FempakInput` | Assets | `Source/Engine/Assets/FempakWriter.hpp` | 15 |
| `FempakWriter` | Assets | `Source/Engine/Assets/FempakWriter.hpp` | 22 |
| `LocaleTable` | Assets | `Source/Engine/Assets/Localization.hpp` | 14 |
| `LocalizationGender` | Assets | `Source/Engine/Assets/Localization.hpp` | 21 |
| `LocalizationPack` | Assets | `Source/Engine/Assets/Localization.hpp` | 13 |
| `LocalizationTable` | Assets | `Source/Engine/Assets/Localization.hpp` | 28 |
| `MappedFile` | Assets | `Source/Engine/Assets/VFS.hpp` | 20 |
| `MountRecord` | Assets | `Source/Engine/Assets/VFS.hpp` | 134 |
| `PakEntry` | Assets | `Source/Engine/Assets/PakFormat.hpp` | 26 |
| `PakHeader` | Assets | `Source/Engine/Assets/PakFormat.hpp` | 16 |
| `PcmFormat` | Assets | `Source/Engine/Assets/AudioClipFormat.hpp` | 21 |
| `PinnedFile` | Assets | `Source/Engine/Assets/VFS.hpp` | 67 |
| `ReloadNotice` | Assets | `Source/Engine/Assets/AssetDatabase.hpp` | 94 |
| `SpriteAtlas` | Assets | `Source/Engine/Assets/SpriteAtlas.hpp` | 47 |
| `SpriteAtlasAsset` | Assets | `Source/Engine/Assets/SpriteAtlas.hpp` | 77 |
| `StagedRow` | Assets | `Source/Engine/Assets/AssetDatabase.hpp` | 116 |
| `StringEntry` | Assets | `Source/Engine/Assets/Localization.hpp` | 15 |
| `TextureAlphaMode` | Assets | `Source/Engine/Assets/TextureFormat.hpp` | 43 |
| `TextureArrayAsset` | Assets | `Source/Engine/Assets/TextureArray.hpp` | 51 |
| `TextureCookOptions` | Assets | `Source/Engine/Assets/TextureCook.hpp` | 31 |
| `TextureFormat` | Assets | `Source/Engine/Assets/TextureFormat.hpp` | 22 |
| `TextureHeader` | Assets | `Source/Engine/Assets/TextureFormat.hpp` | 184 |
| `VfsEntry` | Assets | `Source/Engine/Assets/VFS.hpp` | 59 |
| `VirtualFileSystem` | Assets | `Source/Engine/Assets/TextureArray.hpp` | 16 |
| `VirtualFileSystem` | Assets | `Source/Engine/Assets/VFS.hpp` | 73 |

### Engine/Audio (122)

| Символ | Вид | Заголовок | Строка |
|---|---|---|---|
| `Applied` | Audio | `Source/Engine/Audio/VoicePreset.hpp` | 154 |
| `AudioBus` | Audio | `Source/Engine/Audio/Audio.hpp` | 27 |
| `AudioBusComponent` | Audio | `Source/Engine/Audio/Audio.hpp` | 44 |
| `AudioCaptureMonitor` | Audio | `Source/Engine/Audio/AudioDevices.hpp` | 144 |
| `AudioClip` | Audio | `Source/Engine/Audio/Audio.hpp` | 75 |
| `AudioCommand` | Audio | `Source/Engine/Audio/DspGraph.hpp` | 138 |
| `AudioCommandType` | Audio | `Source/Engine/Audio/DspGraph.hpp` | 121 |
| `AudioContextComponent` | Audio | `Source/Engine/Audio/AudioZones.hpp` | 69 |
| `AudioDeviceEnumerator` | Audio | `Source/Engine/Audio/AudioDevices.hpp` | 93 |
| `AudioDeviceInfo` | Audio | `Source/Engine/Audio/AudioDevices.hpp` | 54 |
| `AudioDeviceKind` | Audio | `Source/Engine/Audio/AudioDevices.hpp` | 15 |
| `AudioDeviceList` | Audio | `Source/Engine/Audio/AudioDevices.hpp` | 66 |
| `AudioDevicePreferences` | Audio | `Source/Engine/Audio/AudioDevices.hpp` | 73 |
| `AudioDeviceStatus` | Audio | `Source/Engine/Audio/AudioDevices.hpp` | 21 |
| `AudioLimiter` | Audio | `Source/Engine/Audio/Limiter.hpp` | 23 |
| `AudioListener` | Audio | `Source/Engine/Audio/Audio.hpp` | 100 |
| `AudioSource` | Audio | `Source/Engine/Audio/Audio.hpp` | 85 |
| `AudioSourceComponent` | Audio | `Source/Engine/Audio/Audio.hpp` | 57 |
| `AudioSystem` | Audio | `Source/Engine/Audio/Audio.hpp` | 106 |
| `AudioSystem` | Audio | `Source/Engine/Audio/VoiceReactiveSfx.hpp` | 15 |
| `AudioZoneComponent` | Audio | `Source/Engine/Audio/AudioZones.hpp` | 34 |
| `AudioZoneKind` | Audio | `Source/Engine/Audio/AudioZones.hpp` | 14 |
| `AudioZoneSystem` | Audio | `Source/Engine/Audio/AudioZones.hpp` | 93 |
| `BiquadState` | Audio | `Source/Engine/Audio/VoiceEffects.hpp` | 369 |
| `BusAtomics` | Audio | `Source/Engine/Audio/DspGraph.hpp` | 370 |
| `BusFilterParams` | Audio | `Source/Engine/Audio/DspGraph.hpp` | 108 |
| `BusRuntime` | Audio | `Source/Engine/Audio/Audio.hpp` | 227 |
| `BusShadow` | Audio | `Source/Engine/Audio/DspGraph.hpp` | 291 |
| `BusState` | Audio | `Source/Engine/Audio/DspGraph.hpp` | 338 |
| `CommandQueue` | Audio | `Source/Engine/Audio/DspGraph.hpp` | 170 |
| `CookedClipRecord` | Audio | `Source/Engine/Audio/Audio.hpp` | 202 |
| `DspClipView` | Audio | `Source/Engine/Audio/DspGraph.hpp` | 205 |
| `DspGraph` | Audio | `Source/Engine/Audio/DspGraph.hpp` | 216 |
| `DspGraph` | Audio | `Source/Engine/Audio/VoiceChannel.hpp` | 16 |
| `DspResult` | Audio | `Source/Engine/Audio/DspGraph.hpp` | 34 |
| `DspResult` | Audio | `Source/Engine/Audio/VoiceChannel.hpp` | 19 |
| `DuckingParams` | Audio | `Source/Engine/Audio/DspGraph.hpp` | 80 |
| `DuckingState` | Audio | `Source/Engine/Audio/DspGraph.hpp` | 298 |
| `Entry` | Audio | `Source/Engine/Audio/VoicePreset.hpp` | 83 |
| `FilterState` | Audio | `Source/Engine/Audio/DspGraph.hpp` | 327 |
| `FloatRing` | Audio | `Source/Engine/Audio/Audio.hpp` | 25 |
| `FloatRing` | Audio | `Source/Engine/Audio/AudioStream.hpp` | 16 |
| `FloatRing` | Audio | `Source/Engine/Audio/DspGraph.hpp` | 32 |
| `JitterSlot` | Audio | `Source/Engine/Audio/VoiceChannel.hpp` | 119 |
| `LimiterDesc` | Audio | `Source/Engine/Audio/Limiter.hpp` | 11 |
| `OggOpusStream` | Audio | `Source/Engine/Audio/OggOpus.hpp` | 131 |
| `OggPacket` | Audio | `Source/Engine/Audio/OggOpus.hpp` | 76 |
| `OggPacketReader` | Audio | `Source/Engine/Audio/OggOpus.hpp` | 86 |
| `OggPage` | Audio | `Source/Engine/Audio/OggOpus.hpp` | 36 |
| `OggPageReader` | Audio | `Source/Engine/Audio/OggOpus.hpp` | 57 |
| `OpusHeadInfo` | Audio | `Source/Engine/Audio/OggOpus.hpp` | 106 |
| `OwnedPcmRecord` | Audio | `Source/Engine/Audio/Audio.hpp` | 209 |
| `Peer` | Audio | `Source/Engine/Audio/VoiceChannel.hpp` | 131 |
| `PlayerState` | Audio | `Source/Engine/Audio/VoiceReactiveSfx.hpp` | 88 |
| `RateBucket` | Audio | `Source/Engine/Audio/VoiceNetworkBridge.hpp` | 62 |
| `ResolvedZone` | Audio | `Source/Engine/Audio/AudioZones.hpp` | 115 |
| `ReverbParams` | Audio | `Source/Engine/Audio/DspGraph.hpp` | 96 |
| `ReverbState` | Audio | `Source/Engine/Audio/DspGraph.hpp` | 311 |
| `SlotRuntime` | Audio | `Source/Engine/Audio/VoiceEffects.hpp` | 385 |
| `SourceRuntime` | Audio | `Source/Engine/Audio/Audio.hpp` | 219 |
| `State` | Audio | `Source/Engine/Audio/AudioDevices.hpp` | 164 |
| `Stats` | Audio | `Source/Engine/Audio/AudioZones.hpp` | 96 |
| `VoiceAec` | Audio | `Source/Engine/Audio/VoiceAec.hpp` | 44 |
| `VoiceAecSettings` | Audio | `Source/Engine/Audio/VoiceAec.hpp` | 22 |
| `VoiceCapture` | Audio | `Source/Engine/Audio/AudioStream.hpp` | 64 |
| `VoiceChannel` | Audio | `Source/Engine/Audio/VoiceChannel.hpp` | 65 |
| `VoiceChannelConfig` | Audio | `Source/Engine/Audio/VoiceChannel.hpp` | 36 |
| `VoiceChannelStats` | Audio | `Source/Engine/Audio/VoiceChannel.hpp` | 46 |
| `VoiceCodecConfig` | Audio | `Source/Engine/Audio/VoiceCodec.hpp` | 15 |
| `VoiceConditionOutput` | Audio | `Source/Engine/Audio/VoiceEffects.hpp` | 333 |
| `VoiceConditionSettings` | Audio | `Source/Engine/Audio/VoiceEffects.hpp` | 284 |
| `VoiceConditionState` | Audio | `Source/Engine/Audio/VoiceEffects.hpp` | 324 |
| `VoiceDecoder` | Audio | `Source/Engine/Audio/VoiceCodec.hpp` | 51 |
| `VoiceDenoiseTier` | Audio | `Source/Engine/Audio/VoiceDsp.hpp` | 10 |
| `VoiceDistortionSettings` | Audio | `Source/Engine/Audio/VoiceEffects.hpp` | 157 |
| `VoiceDspChain` | Audio | `Source/Engine/Audio/VoiceDsp.hpp` | 99 |
| `VoiceDspSettings` | Audio | `Source/Engine/Audio/VoiceDsp.hpp` | 43 |
| `VoiceEffectChain` | Audio | `Source/Engine/Audio/VoiceEffects.hpp` | 345 |
| `VoiceEffectChainSettings` | Audio | `Source/Engine/Audio/VoiceEffects.hpp` | 221 |
| `VoiceEffectKind` | Audio | `Source/Engine/Audio/VoiceEffects.hpp` | 20 |
| `VoiceEffectPolicy` | Audio | `Source/Engine/Audio/VoicePreset.hpp` | 116 |
| `VoiceEffectPresetKind` | Audio | `Source/Engine/Audio/VoiceEffects.hpp` | 235 |
| `VoiceEffectProfile` | Audio | `Source/Engine/Audio/VoiceEffects.hpp` | 310 |
| `VoiceEffectRack` | Audio | `Source/Engine/Audio/VoiceEffects.hpp` | 433 |
| `VoiceEffectSlot` | Audio | `Source/Engine/Audio/VoiceEffects.hpp` | 200 |
| `VoiceEncoder` | Audio | `Source/Engine/Audio/VoiceCodec.hpp` | 27 |
| `VoiceFormantSettings` | Audio | `Source/Engine/Audio/VoiceEffects.hpp` | 146 |
| `VoiceMuffleSettings` | Audio | `Source/Engine/Audio/VoiceEffects.hpp` | 80 |
| `VoiceMuteSettings` | Audio | `Source/Engine/Audio/VoiceEffects.hpp` | 67 |
| `VoiceNetworkBridge` | Audio | `Source/Engine/Audio/VoiceNetworkBridge.hpp` | 18 |
| `VoiceParticipant` | Audio | `Source/Engine/Audio/VoiceSession.hpp` | 46 |
| `VoicePitchSettings` | Audio | `Source/Engine/Audio/VoiceEffects.hpp` | 133 |
| `VoicePresetApplier` | Audio | `Source/Engine/Audio/VoicePreset.hpp` | 146 |
| `VoicePresetCategory` | Audio | `Source/Engine/Audio/VoicePreset.hpp` | 22 |
| `VoicePresetChoice` | Audio | `Source/Engine/Audio/VoicePreset.hpp` | 95 |
| `VoicePresetDesc` | Audio | `Source/Engine/Audio/VoicePreset.hpp` | 47 |
| `VoicePresetFlags` | Audio | `Source/Engine/Audio/VoicePreset.hpp` | 35 |
| `VoicePresetKind` | Audio | `Source/Engine/Audio/VoiceDsp.hpp` | 68 |
| `VoicePresetRegistry` | Audio | `Source/Engine/Audio/VoicePreset.hpp` | 58 |
| `VoicePresetResolution` | Audio | `Source/Engine/Audio/VoicePreset.hpp` | 124 |
| `VoicePresetSelection` | Audio | `Source/Engine/Audio/VoicePreset.hpp` | 105 |
| `VoiceRadioSettings` | Audio | `Source/Engine/Audio/VoiceEffects.hpp` | 115 |
| `VoiceReactiveSfx` | Audio | `Source/Engine/Audio/VoiceReactiveSfx.hpp` | 79 |
| `VoiceReactiveSfxInput` | Audio | `Source/Engine/Audio/VoiceReactiveSfx.hpp` | 53 |
| `VoiceReactiveSfxPlayer` | Audio | `Source/Engine/Audio/VoiceReactiveSfx.hpp` | 43 |
| `VoiceReactiveSfxSettings` | Audio | `Source/Engine/Audio/VoiceReactiveSfx.hpp` | 58 |
| `VoiceRingModulationSettings` | Audio | `Source/Engine/Audio/VoiceEffects.hpp` | 171 |
| `VoiceRouting` | Audio | `Source/Engine/Audio/VoiceChannel.hpp` | 30 |
| `VoiceSession` | Audio | `Source/Engine/Audio/VoiceChannel.hpp` | 15 |
| `VoiceSession` | Audio | `Source/Engine/Audio/VoicePreset.hpp` | 15 |
| `VoiceSession` | Audio | `Source/Engine/Audio/VoiceSession.hpp` | 65 |
| `VoiceSfxEvent` | Audio | `Source/Engine/Audio/VoiceReactiveSfx.hpp` | 31 |
| `VoiceSfxKind` | Audio | `Source/Engine/Audio/VoiceReactiveSfx.hpp` | 17 |
| `VoiceSpatialPosition` | Audio | `Source/Engine/Audio/VoiceSession.hpp` | 37 |
| `VoiceSpatialSettings` | Audio | `Source/Engine/Audio/VoiceSession.hpp` | 22 |
| `VoiceState` | Audio | `Source/Engine/Audio/DspGraph.hpp` | 350 |
| `VoiceSystem` | Audio | `Source/Engine/Audio/VoicePreset.hpp` | 16 |
| `VoiceSystem` | Audio | `Source/Engine/Audio/VoiceSystem.hpp` | 39 |
| `VoiceSystemConfig` | Audio | `Source/Engine/Audio/VoiceSystem.hpp` | 22 |
| `VoiceUnderwaterSettings` | Audio | `Source/Engine/Audio/VoiceEffects.hpp` | 98 |
| `VoiceWhisperSettings` | Audio | `Source/Engine/Audio/VoiceEffects.hpp` | 185 |
| `VorbisIdentInfo` | Audio | `Source/Engine/Audio/OggOpus.hpp` | 121 |

### Engine/Core (113)

| Символ | Вид | Заголовок | Строка |
|---|---|---|---|
| `AssetId` | Core | `Source/Engine/Core/AssetId.hpp` | 43 |
| `AssetKind` | Core | `Source/Engine/Core/AssetId.hpp` | 15 |
| `AudioBus` | Core | `Source/Engine/Core/Pause.hpp` | 11 |
| `AudioSystem` | Core | `Source/Engine/Core/Pause.hpp` | 10 |
| `BigIntT` | Core | `Source/Engine/Core/BigInt.hpp` | 179 |
| `BigNumberStore` | Core | `Source/Engine/Core/BigNumberStore.hpp` | 41 |
| `BinaryKind` | Core | `Source/Engine/Core/BigNumberStore.hpp` | 259 |
| `BinaryKind` | Core | `Source/Engine/Core/DecimalStore.hpp` | 206 |
| `BufferTag` | Core | `Source/Engine/Core/Types.hpp` | 301 |
| `BuildInfo` | Core | `Source/Engine/Core/BuildInfo.hpp` | 12 |
| `BuildProfile` | Core | `Source/Engine/Core/BuildProfile.hpp` | 135 |
| `CameraView` | Core | `Source/Engine/Core/Grid.hpp` | 337 |
| `Cell` | Core | `Source/Engine/Core/Grid.hpp` | 74 |
| `CivilDate` | Core | `Source/Engine/Core/Calendar.hpp` | 23 |
| `Clock` | Core | `Source/Engine/Core/Time.hpp` | 28 |
| `ClockTime` | Core | `Source/Engine/Core/Calendar.hpp` | 34 |
| `Color32` | Core | `Source/Engine/Core/Types.hpp` | 203 |
| `ComponentDesc` | Core | `Source/Engine/Core/Mirror.hpp` | 51 |
| `ComponentRegistry` | Core | `Source/Engine/Core/PluginHost.hpp` | 43 |
| `ComponentType` | Core | `Source/Engine/Core/PluginHost.hpp` | 29 |
| `DateTime` | Core | `Source/Engine/Core/Calendar.hpp` | 46 |
| `DecimalStore` | Core | `Source/Engine/Core/DecimalStore.hpp` | 17 |
| `DecimalT` | Core | `Source/Engine/Core/Decimal.hpp` | 34 |
| `DurationStyle` | Core | `Source/Engine/Core/Time.hpp` | 578 |
| `EddsaKeyPair` | Core | `Source/Engine/Core/CryptoAsymmetric.hpp` | 31 |
| `Error` | Core | `Source/Engine/Core/Error.hpp` | 23 |
| `ErrorCode` | Core | `Source/Engine/Core/Error.hpp` | 11 |
| `FieldDesc` | Core | `Source/Engine/Core/Mirror.hpp` | 40 |
| `FieldKind` | Core | `Source/Engine/Core/Mirror.hpp` | 17 |
| `FileFilter` | Core | `Source/Engine/Core/WindowManager.hpp` | 203 |
| `FormatSpec` | Core | `Source/Engine/Core/BigInt.hpp` | 36 |
| `FrameArena` | Core | `Source/Engine/Core/FrameArena.hpp` | 12 |
| `FrameTimer` | Core | `Source/Engine/Core/Time.hpp` | 508 |
| `GamepadState` | Core | `Source/Engine/Core/WindowManager.hpp` | 27 |
| `GridDesc` | Core | `Source/Engine/Core/Grid.hpp` | 91 |
| `Handle` | Core | `Source/Engine/Core/BigNumberStore.hpp` | 20 |
| `Handle` | Core | `Source/Engine/Core/Types.hpp` | 275 |
| `Handle` | Core | `Source/Engine/Core/WindowHost.hpp` | 20 |
| `Host` | Core | `Source/Engine/Core/Plugin.hpp` | 19 |
| `InputState` | Core | `Source/Engine/Core/WindowManager.hpp` | 126 |
| `JobSystem` | Core | `Source/Engine/Core/JobSystem.hpp` | 25 |
| `Key` | Core | `Source/Engine/Core/WindowManager.hpp` | 58 |
| `KeyLess` | Core | `Source/Engine/Core/FlatMap.hpp` | 197 |
| `KeyUpperLess` | Core | `Source/Engine/Core/FlatMap.hpp` | 209 |
| `LoadedPlugin` | Core | `Source/Engine/Core/PluginHost.hpp` | 80 |
| `LogLevel` | Core | `Source/Engine/Core/Log.hpp` | 13 |
| `LogRetention` | Core | `Source/Engine/Core/Log.hpp` | 31 |
| `Mat4` | Core | `Source/Engine/Core/Mat4.hpp` | 15 |
| `NoiseGenerator` | Core | `Source/Engine/Core/NoiseGenerator.hpp` | 10 |
| `Notation` | Core | `Source/Engine/Core/BigInt.hpp` | 30 |
| `NoticeEntry` | Core | `Source/Engine/Core/ThirdPartyNotices.hpp` | 12 |
| `OptimizationAdvice` | Core | `Source/Engine/Core/BuildProfile.hpp` | 154 |
| `OptimizationLevel` | Core | `Source/Engine/Core/BuildProfile.hpp` | 10 |
| `PauseController` | Core | `Source/Engine/Core/Pause.hpp` | 27 |
| `PauseSettings` | Core | `Source/Engine/Core/Pause.hpp` | 19 |
| `Pcg32` | Core | `Source/Engine/Core/Pcg32.hpp` | 20 |
| `PipelineTag` | Core | `Source/Engine/Core/Types.hpp` | 303 |
| `PluginHost` | Core | `Source/Engine/Core/PluginHost.hpp` | 92 |
| `PluginSystem` | Core | `Source/Engine/Core/PluginHost.hpp` | 70 |
| `Profiler` | Core | `Source/Engine/Core/Profiler.hpp` | 45 |
| `ProfileRow` | Core | `Source/Engine/Core/ProfilerPanel.hpp` | 26 |
| `ProfilerPanel` | Core | `Source/Engine/Core/ProfilerPanel.hpp` | 39 |
| `ProfileSample` | Core | `Source/Engine/Core/Profiler.hpp` | 30 |
| `ProfileSortMode` | Core | `Source/Engine/Core/ProfilerPanel.hpp` | 16 |
| `ProfileStats` | Core | `Source/Engine/Core/Profiler.hpp` | 18 |
| `ProjectionMode` | Core | `Source/Engine/Core/Grid.hpp` | 17 |
| `Rect` | Core | `Source/Engine/Core/Types.hpp` | 173 |
| `ReducedAngle` | Core | `Source/Engine/Core/SimMath.hpp` | 29 |
| `Reflection` | Core | `Source/Engine/Core/Mirror.hpp` | 69 |
| `Reflection` | Core | `Source/Engine/Core/Mirror.hpp` | 298 |
| `Reflection` | Core | `Source/Engine/Core/Mirror.hpp` | 314 |
| `ReplayFrame` | Core | `Source/Engine/Core/Replay.hpp` | 245 |
| `ReplayKeyframe` | Core | `Source/Engine/Core/Replay.hpp` | 16 |
| `ReplaySession` | Core | `Source/Engine/Core/Replay.hpp` | 25 |
| `RngState` | Core | `Source/Engine/Core/Pcg32.hpp` | 11 |
| `RoundingMode` | Core | `Source/Engine/Core/Decimal.hpp` | 22 |
| `SamplerTag` | Core | `Source/Engine/Core/Types.hpp` | 304 |
| `Scope` | Core | `Source/Engine/Core/Profiler.hpp` | 151 |
| `SecureRng` | Core | `Source/Engine/Core/SecureRng.hpp` | 11 |
| `Sha256Context` | Core | `Source/Engine/Core/Sha256.hpp` | 17 |
| `Slot` | Core | `Source/Engine/Core/BigNumberStore.hpp` | 252 |
| `Slot` | Core | `Source/Engine/Core/DecimalStore.hpp` | 199 |
| `SplashClock` | Core | `Source/Engine/Core/Splash.hpp` | 10 |
| `Status` | Core | `Source/Engine/Core/BigInt.hpp` | 18 |
| `Stopwatch` | Core | `Source/Engine/Core/Time.hpp` | 215 |
| `Task` | Core | `Source/Engine/Core/JobSystem.hpp` | 108 |
| `TextureTag` | Core | `Source/Engine/Core/Types.hpp` | 302 |
| `TextWriter` | Core | `Source/Engine/Core/Calendar.hpp` | 204 |
| `ThreadCounter` | Core | `Source/Engine/Core/JobSystem.hpp` | 121 |
| `ThreadLoad` | Core | `Source/Engine/Core/JobSystem.hpp` | 31 |
| `ThreadRole` | Core | `Source/Engine/Core/ThreadAffinity.hpp` | 12 |
| `TimerFired` | Core | `Source/Engine/Core/Time.hpp` | 261 |
| `TimerHandle` | Core | `Source/Engine/Core/Time.hpp` | 246 |
| `TimerPool` | Core | `Source/Engine/Core/Time.hpp` | 271 |
| `TouchPoint` | Core | `Source/Engine/Core/WindowManager.hpp` | 38 |
| `TypeNameOverride` | Core | `Source/Engine/Core/Definitions.hpp` | 42 |
| `TypeNameOverride` | Core | `Source/Engine/Core/Definitions.hpp` | 285 |
| `TypeObject` | Core | `Source/Engine/Core/Definitions.hpp` | 187 |
| `TypeRecord` | Core | `Source/Engine/Core/Definitions.hpp` | 87 |
| `TypeTable` | Core | `Source/Engine/Core/Definitions.hpp` | 95 |
| `TypeTraits` | Core | `Source/Engine/Core/Definitions.hpp` | 172 |
| `TypeTraits` | Core | `Source/Engine/Core/Definitions.hpp` | 269 |
| `Unregistered` | Core | `Source/Engine/Core/Mirror.hpp` | 62 |
| `Vector2D` | Core | `Source/Engine/Core/Types.hpp` | 13 |
| `Vector3D` | Core | `Source/Engine/Core/Types.hpp` | 90 |
| `Vector4D` | Core | `Source/Engine/Core/Types.hpp` | 139 |
| `WindowDesc` | Core | `Source/Engine/Core/WindowManager.hpp` | 224 |
| `WindowExtent` | Core | `Source/Engine/Core/WindowManager.hpp` | 49 |
| `WindowHost` | Core | `Source/Engine/Core/WindowHost.hpp` | 17 |
| `WindowManager` | Core | `Source/Engine/Core/WindowManager.hpp` | 235 |
| `WindowMode` | Core | `Source/Engine/Core/WindowManager.hpp` | 215 |
| `WindowRecord` | Core | `Source/Engine/Core/WindowHost.hpp` | 30 |
| `X25519KeyPair` | Core | `Source/Engine/Core/CryptoAsymmetric.hpp` | 20 |

### Engine/Documents (6)

| Символ | Вид | Заголовок | Строка |
|---|---|---|---|
| `DocumentError` | Documents | `Source/Engine/Documents/Document.hpp` | 25 |
| `DocumentHandle` | Documents | `Source/Engine/Documents/Document.hpp` | 37 |
| `DocumentInfo` | Documents | `Source/Engine/Documents/Document.hpp` | 48 |
| `DocumentSystem` | Documents | `Source/Engine/Documents/Document.hpp` | 59 |
| `Slot` | Documents | `Source/Engine/Documents/Document.hpp` | 105 |
| `VirtualFileSystem` | Documents | `Source/Engine/Documents/Document.hpp` | 14 |

### Engine/ECS (292)

| Символ | Вид | Заголовок | Строка |
|---|---|---|---|
| `AccessibilityKernel` | ECS | `Source/Engine/ECS/SystemContext.hpp` | 16 |
| `AccessibilityServices` | ECS | `Source/Engine/ECS/SystemContext.hpp` | 100 |
| `AccessibilitySystemView` | ECS | `Source/Engine/ECS/SystemContext.hpp` | 123 |
| `AccessibilitySystemView` | ECS | `Source/Engine/ECS/SystemContext.hpp` | 351 |
| `AccessValidation` | ECS | `Source/Engine/ECS/FCS/AccessValidation.hpp` | 17 |
| `AccessValidation` | ECS | `Source/Engine/ECS/FCS/AccessValidation.hpp` | 84 |
| `AICommandComponent` | ECS | `Source/Engine/ECS/StateTree.hpp` | 175 |
| `AICommandKind` | ECS | `Source/Engine/ECS/StateTree.hpp` | 163 |
| `AnimationClipBinding` | ECS | `Source/Engine/ECS/AnimationClipPose.hpp` | 22 |
| `AnimationPose` | ECS | `Source/Engine/ECS/AnimationClipPose.hpp` | 41 |
| `AnimationTimeline` | ECS | `Source/Engine/ECS/AnimationTimeline.hpp` | 90 |
| `AnimBoneKey` | ECS | `Source/Engine/ECS/AnimGraph.hpp` | 75 |
| `AnimClip` | ECS | `Source/Engine/ECS/AnimGraph.hpp` | 87 |
| `AnimConditionFlags` | ECS | `Source/Engine/ECS/AnimGraph.hpp` | 29 |
| `AnimGraphDefinitionComponent` | ECS | `Source/Engine/ECS/AnimGraph.hpp` | 117 |
| `AnimGraphSystem` | ECS | `Source/Engine/ECS/AnimGraph.hpp` | 200 |
| `AnimRestBone` | ECS | `Source/Engine/ECS/AnimGraph.hpp` | 105 |
| `AnimStateComponent` | ECS | `Source/Engine/ECS/AnimGraph.hpp` | 138 |
| `AnimTransition` | ECS | `Source/Engine/ECS/AnimGraph.hpp` | 49 |
| `Archetype` | ECS | `Source/Engine/ECS/FCS/World.hpp` | 294 |
| `AssetDatabase` | ECS | `Source/Engine/ECS/Scene.hpp` | 21 |
| `AssetDatabase` | ECS | `Source/Engine/ECS/SystemContext.hpp` | 28 |
| `AssetServices` | ECS | `Source/Engine/ECS/SystemContext.hpp` | 71 |
| `AudioServices` | ECS | `Source/Engine/ECS/SystemContext.hpp` | 77 |
| `AudioSystem` | ECS | `Source/Engine/ECS/SystemContext.hpp` | 11 |
| `AudioSystemView` | ECS | `Source/Engine/ECS/SystemContext.hpp` | 121 |
| `AudioSystemView` | ECS | `Source/Engine/ECS/SystemContext.hpp` | 306 |
| `BigIntComponent` | ECS | `Source/Engine/ECS/Components.hpp` | 168 |
| `BoneAttachment2D` | ECS | `Source/Engine/ECS/BoneAttachment.hpp` | 19 |
| `BoneAttachmentSystem` | ECS | `Source/Engine/ECS/BoneAttachment.hpp` | 35 |
| `CachedTarget` | ECS | `Source/Engine/ECS/LightSource.hpp` | 234 |
| `Camera2D` | ECS | `Source/Engine/ECS/CoreComponents.hpp` | 90 |
| `CameraStackSystem` | ECS | `Source/Engine/ECS/SystemContext.hpp` | 21 |
| `CarrierMemberComponent` | ECS | `Source/Engine/ECS/Components.hpp` | 139 |
| `ChainIdTag` | ECS | `Source/Engine/ECS/StableIds.hpp` | 12 |
| `ChainSaveV3` | ECS | `Source/Engine/ECS/SnapshotWorld.hpp` | 28 |
| `ChangeMaskBit` | ECS | `Source/Engine/ECS/ChangeMask.hpp` | 20 |
| `CharacterDesc` | ECS | `Source/Engine/ECS/Descriptors.hpp` | 51 |
| `Chunk` | ECS | `Source/Engine/ECS/FCS/World.hpp` | 287 |
| `ChunkCopyRange` | ECS | `Source/Engine/ECS/RenderEcs.hpp` | 90 |
| `ChunkHeader` | ECS | `Source/Engine/ECS/RenderEcs.hpp` | 45 |
| `ChunkStorage` | ECS | `Source/Engine/ECS/FCS/World.hpp` | 159 |
| `ClothIdTag` | ECS | `Source/Engine/ECS/StableIds.hpp` | 13 |
| `ClothSaveV3` | ECS | `Source/Engine/ECS/SnapshotWorld.hpp` | 59 |
| `ClothSystem` | ECS | `Source/Engine/ECS/SystemContext.hpp` | 18 |
| `ColliderSnapshot` | ECS | `Source/Engine/ECS/Snapshot.hpp` | 183 |
| `CollisionMode` | ECS | `Source/Engine/ECS/Presentation.hpp` | 21 |
| `CollisionPolicyComponent` | ECS | `Source/Engine/ECS/Presentation.hpp` | 101 |
| `Command` | ECS | `Source/Engine/ECS/CommandBuffer.hpp` | 30 |
| `Command` | ECS | `Source/Engine/ECS/FCS/CommandBuffer.hpp` | 84 |
| `CommandBuffer` | ECS | `Source/Engine/ECS/CommandBuffer.hpp` | 38 |
| `CommandBuffer` | ECS | `Source/Engine/ECS/FCS/CommandBuffer.hpp` | 20 |
| `CommandBuffer` | ECS | `Source/Engine/ECS/SystemContext.hpp` | 49 |
| `CommandCutChain` | ECS | `Source/Engine/ECS/CommandLog.hpp` | 42 |
| `CommandKind` | ECS | `Source/Engine/ECS/CommandBuffer.hpp` | 20 |
| `CommandKind` | ECS | `Source/Engine/ECS/FCS/CommandBuffer.hpp` | 75 |
| `CommandLog` | ECS | `Source/Engine/ECS/CommandLog.hpp` | 92 |
| `CommandLogHeader` | ECS | `Source/Engine/ECS/CommandLog.hpp` | 66 |
| `CommandRecord` | ECS | `Source/Engine/ECS/CommandLog.hpp` | 78 |
| `CommandType` | ECS | `Source/Engine/ECS/CommandLog.hpp` | 22 |
| `CommandWorldCallbacks` | ECS | `Source/Engine/ECS/CommandLog.hpp` | 52 |
| `ComponentChangeMask` | ECS | `Source/Engine/ECS/ChangeMask.hpp` | 53 |
| `ComponentMeta` | ECS | `Source/Engine/ECS/FCS/World.hpp` | 106 |
| `ComponentOps` | ECS | `Source/Engine/ECS/ComponentRegistry.hpp` | 43 |
| `ComponentOpsFor` | ECS | `Source/Engine/ECS/ComponentRegistry.hpp` | 68 |
| `ComponentRegistry` | ECS | `Source/Engine/ECS/ComponentRegistry.hpp` | 129 |
| `ComponentStorage` | ECS | `Source/Engine/ECS/FCS/Registry.hpp` | 775 |
| `CompressedClip` | ECS | `Source/Engine/ECS/AnimationCompression.hpp` | 47 |
| `CompressedKey` | ECS | `Source/Engine/ECS/AnimationCompression.hpp` | 19 |
| `CompressedTrack` | ECS | `Source/Engine/ECS/AnimationCompression.hpp` | 31 |
| `CompressionReport` | ECS | `Source/Engine/ECS/AnimationCompression.hpp` | 63 |
| `ConstCoreSystemView` | ECS | `Source/Engine/ECS/SystemContext.hpp` | 116 |
| `ConstCoreSystemView` | ECS | `Source/Engine/ECS/SystemContext.hpp` | 192 |
| `ConstRenderSystemView` | ECS | `Source/Engine/ECS/SystemContext.hpp` | 119 |
| `ConstRenderSystemView` | ECS | `Source/Engine/ECS/SystemContext.hpp` | 261 |
| `ConstView` | ECS | `Source/Engine/ECS/FCS/Registry.hpp` | 366 |
| `Context` | ECS | `Source/Engine/ECS/FCS/Registry.hpp` | 607 |
| `CoreServices` | ECS | `Source/Engine/ECS/SystemContext.hpp` | 55 |
| `CoreSystemView` | ECS | `Source/Engine/ECS/SystemContext.hpp` | 115 |
| `CoreSystemView` | ECS | `Source/Engine/ECS/SystemContext.hpp` | 168 |
| `Cursor` | ECS | `Source/Engine/ECS/CommandLog.hpp` | 177 |
| `Cursor` | ECS | `Source/Engine/ECS/FCS/Registry.hpp` | 114 |
| `Cursor` | ECS | `Source/Engine/ECS/FCS/Registry.hpp` | 374 |
| `DecimalComponent` | ECS | `Source/Engine/ECS/Components.hpp` | 179 |
| `DirectionalAnimationPlayer` | ECS | `Source/Engine/ECS/DirectionalAnimationPlayer.hpp` | 29 |
| `DirectionalBlendAnchor` | ECS | `Source/Engine/ECS/DirectionalBlend.hpp` | 52 |
| `DirectionalBlendAnchorEntry` | ECS | `Source/Engine/ECS/DirectionalBlend.hpp` | 84 |
| `DirectionalBlendSample` | ECS | `Source/Engine/ECS/DirectionalBlend.hpp` | 133 |
| `DirectionalBlendSpaceDocument` | ECS | `Source/Engine/ECS/DirectionalBlend.hpp` | 65 |
| `DirectionalBlendTable` | ECS | `Source/Engine/ECS/DirectionalBlend.hpp` | 106 |
| `DirectionalBlendTopology` | ECS | `Source/Engine/ECS/DirectionalBlend.hpp` | 42 |
| `DirectionalBlendTriangle` | ECS | `Source/Engine/ECS/DirectionalBlend.hpp` | 94 |
| `DirectionalBlendWeight` | ECS | `Source/Engine/ECS/DirectionalBlend.hpp` | 125 |
| `DrawOrder` | ECS | `Source/Engine/ECS/CoreComponents.hpp` | 79 |
| `EachIterator` | ECS | `Source/Engine/ECS/FCS/Registry.hpp` | 224 |
| `EachIterator` | ECS | `Source/Engine/ECS/FCS/Registry.hpp` | 484 |
| `EachRange` | ECS | `Source/Engine/ECS/FCS/Registry.hpp` | 261 |
| `EachRange` | ECS | `Source/Engine/ECS/FCS/Registry.hpp` | 521 |
| `EditorSelected` | ECS | `Source/Engine/ECS/CoreComponents.hpp` | 104 |
| `EmitterIdTag` | ECS | `Source/Engine/ECS/StableIds.hpp` | 14 |
| `EmitterSaveV3` | ECS | `Source/Engine/ECS/SnapshotWorld.hpp` | 79 |
| `Entity` | ECS | `Source/Engine/ECS/FCS/World.hpp` | 48 |
| `EntityRecord` | ECS | `Source/Engine/ECS/FCS/World.hpp` | 712 |
| `EntityRemap` | ECS | `Source/Engine/ECS/Scene.hpp` | 59 |
| `EntityStorage` | ECS | `Source/Engine/ECS/FCS/Registry.hpp` | 691 |
| `Entry` | ECS | `Source/Engine/ECS/FCS/Registry.hpp` | 668 |
| `Entry` | ECS | `Source/Engine/ECS/Schedule.hpp` | 53 |
| `Facing2D` | ECS | `Source/Engine/ECS/DirectionalBlend.hpp` | 16 |
| `FesklHeader` | ECS | `Source/Engine/ECS/SkeletonAsset.hpp` | 37 |
| `FesklNameRef` | ECS | `Source/Engine/ECS/SkeletonAsset.hpp` | 87 |
| `FesklSprite` | ECS | `Source/Engine/ECS/SkeletonAsset.hpp` | 97 |
| `FluidSaveV3` | ECS | `Source/Engine/ECS/SnapshotWorld.hpp` | 71 |
| `FluidWaveSystem` | ECS | `Source/Engine/ECS/SystemContext.hpp` | 19 |
| `FontAtlasSet` | ECS | `Source/Engine/ECS/SystemContext.hpp` | 36 |
| `FrameArena` | ECS | `Source/Engine/ECS/SystemContext.hpp` | 12 |
| `GameplayAttributeRegistry` | ECS | `Source/Engine/ECS/SystemContext.hpp` | 23 |
| `GameplayEffectSystem` | ECS | `Source/Engine/ECS/SystemContext.hpp` | 24 |
| `GameplayServices` | ECS | `Source/Engine/ECS/SystemContext.hpp` | 107 |
| `GameplaySkillSystem` | ECS | `Source/Engine/ECS/SystemContext.hpp` | 25 |
| `GameplaySystemView` | ECS | `Source/Engine/ECS/SystemContext.hpp` | 120 |
| `GameplaySystemView` | ECS | `Source/Engine/ECS/SystemContext.hpp` | 284 |
| `GameplayTagRegistry` | ECS | `Source/Engine/ECS/SystemContext.hpp` | 22 |
| `GpuEntity` | ECS | `Source/Engine/ECS/RenderEcs.hpp` | 31 |
| `GpuSpriteInstance` | ECS | `Source/Engine/ECS/RenderEcs.hpp` | 245 |
| `GraphSystem` | ECS | `Source/Engine/ECS/SystemGraph.hpp` | 91 |
| `Hidden` | ECS | `Source/Engine/ECS/CoreComponents.hpp` | 109 |
| `IkChain` | ECS | `Source/Engine/ECS/SkeletonIk.hpp` | 43 |
| `IkComponent` | ECS | `Source/Engine/ECS/SkeletonIk.hpp` | 64 |
| `IkGoalComponent` | ECS | `Source/Engine/ECS/SkeletonIk.hpp` | 77 |
| `IkSolver` | ECS | `Source/Engine/ECS/SkeletonIk.hpp` | 19 |
| `InputUserComponent` | ECS | `Source/Engine/ECS/Components.hpp` | 36 |
| `Interpolation` | ECS | `Source/Engine/ECS/AnimationTimeline.hpp` | 29 |
| `IterationGuard` | ECS | `Source/Engine/ECS/FCS/World.hpp` | 264 |
| `Iterator` | ECS | `Source/Engine/ECS/FCS/Registry.hpp` | 190 |
| `Iterator` | ECS | `Source/Engine/ECS/FCS/Registry.hpp` | 450 |
| `Iterator` | ECS | `Source/Engine/ECS/FCS/Registry.hpp` | 698 |
| `ITextShaper` | ECS | `Source/Engine/ECS/SystemContext.hpp` | 42 |
| `Lifetime` | ECS | `Source/Engine/ECS/CoreComponents.hpp` | 98 |
| `Light2DComponent` | ECS | `Source/Engine/ECS/Lighting.hpp` | 37 |
| `Light2DOccluderComponent` | ECS | `Source/Engine/ECS/Lighting.hpp` | 109 |
| `Light2DResultComponent` | ECS | `Source/Engine/ECS/Lighting.hpp` | 69 |
| `Light2DTargetComponent` | ECS | `Source/Engine/ECS/Lighting.hpp` | 53 |
| `LightGrid2D` | ECS | `Source/Engine/ECS/LightGrid.hpp` | 26 |
| `LightGridInput` | ECS | `Source/Engine/ECS/LightGrid.hpp` | 16 |
| `LightingSettings` | ECS | `Source/Engine/ECS/Lighting.hpp` | 120 |
| `LightingSystem` | ECS | `Source/Engine/ECS/Lighting.hpp` | 147 |
| `LightSourceComponent` | ECS | `Source/Engine/ECS/LightSource.hpp` | 49 |
| `LightSourceFlags` | ECS | `Source/Engine/ECS/LightSource.hpp` | 33 |
| `LightSourceSettings` | ECS | `Source/Engine/ECS/LightSource.hpp` | 150 |
| `LightSourceSystem` | ECS | `Source/Engine/ECS/LightSource.hpp` | 169 |
| `LightSourceType` | ECS | `Source/Engine/ECS/LightSource.hpp` | 23 |
| `LocalizationTable` | ECS | `Source/Engine/ECS/SystemContext.hpp` | 17 |
| `LocalizedTextComponent` | ECS | `Source/Engine/ECS/Components.hpp` | 226 |
| `LocalTransform2D` | ECS | `Source/Engine/ECS/CoreComponents.hpp` | 27 |
| `Mat3` | ECS | `Source/Engine/ECS/Skeleton.hpp` | 32 |
| `NameComponent` | ECS | `Source/Engine/ECS/CoreComponents.hpp` | 116 |
| `NarrativeLabelComponent` | ECS | `Source/Engine/ECS/Components.hpp` | 157 |
| `NativeCleanupHook` | ECS | `Source/Engine/ECS/RegistryOps.hpp` | 12 |
| `PackedTransform2D` | ECS | `Source/Engine/ECS/RenderEcs.hpp` | 229 |
| `Parent2D` | ECS | `Source/Engine/ECS/CoreComponents.hpp` | 35 |
| `Phase` | ECS | `Source/Engine/ECS/Schedule.hpp` | 17 |
| `PhysicsBodyDesc` | ECS | `Source/Engine/ECS/Descriptors.hpp` | 22 |
| `PhysicsJointSystem` | ECS | `Source/Engine/ECS/SystemContext.hpp` | 20 |
| `PhysicsServices` | ECS | `Source/Engine/ECS/SystemContext.hpp` | 82 |
| `PhysicsShapeKind` | ECS | `Source/Engine/ECS/Descriptors.hpp` | 15 |
| `PhysicsSystem` | ECS | `Source/Engine/ECS/SystemContext.hpp` | 13 |
| `PhysicsSystemView` | ECS | `Source/Engine/ECS/SystemContext.hpp` | 117 |
| `PhysicsSystemView` | ECS | `Source/Engine/ECS/SystemContext.hpp` | 216 |
| `PrefabAsset` | ECS | `Source/Engine/ECS/Descriptors.hpp` | 88 |
| `PrefabEntityDesc` | ECS | `Source/Engine/ECS/Descriptors.hpp` | 76 |
| `PrefabEntityFlags` | ECS | `Source/Engine/ECS/Descriptors.hpp` | 98 |
| `PrefabEntityOverride` | ECS | `Source/Engine/ECS/PrefabSystem.hpp` | 18 |
| `PrefabInstance` | ECS | `Source/Engine/ECS/PrefabInstance.hpp` | 50 |
| `PrefabInstanceComponent` | ECS | `Source/Engine/ECS/PrefabInstance.hpp` | 21 |
| `PrefabInstanceMemberComponent` | ECS | `Source/Engine/ECS/PrefabInstance.hpp` | 43 |
| `PrefabOverrides` | ECS | `Source/Engine/ECS/PrefabSystem.hpp` | 31 |
| `PrefabSystem` | ECS | `Source/Engine/ECS/PrefabSystem.hpp` | 36 |
| `QuantizedPose` | ECS | `Source/Engine/ECS/SkeletonIk.hpp` | 165 |
| `Registry` | ECS | `Source/Engine/ECS/FCS/Registry.hpp` | 804 |
| `RenderChunk` | ECS | `Source/Engine/ECS/RenderEcs.hpp` | 62 |
| `RenderChunkArena` | ECS | `Source/Engine/ECS/RenderEcs.hpp` | 304 |
| `RenderEcs` | ECS | `Source/Engine/ECS/RenderEcs.hpp` | 460 |
| `RenderMaskComponent` | ECS | `Source/Engine/ECS/Presentation.hpp` | 139 |
| `RenderPassMask` | ECS | `Source/Engine/ECS/Presentation.hpp` | 126 |
| `RenderServices` | ECS | `Source/Engine/ECS/SystemContext.hpp` | 64 |
| `RenderSystemView` | ECS | `Source/Engine/ECS/SystemContext.hpp` | 118 |
| `RenderSystemView` | ECS | `Source/Engine/ECS/SystemContext.hpp` | 239 |
| `ReplayPlayback` | ECS | `Source/Engine/ECS/ReplayRecorder.hpp` | 103 |
| `ReplayRecorder` | ECS | `Source/Engine/ECS/ReplayRecorder.hpp` | 32 |
| `RetargetMap` | ECS | `Source/Engine/ECS/SkeletonIk.hpp` | 131 |
| `RetargetPair` | ECS | `Source/Engine/ECS/SkeletonIk.hpp` | 118 |
| `RHI` | ECS | `Source/Engine/ECS/SystemContext.hpp` | 32 |
| `RigidBodySnapshot` | ECS | `Source/Engine/ECS/Snapshot.hpp` | 169 |
| `SceneComponent` | ECS | `Source/Engine/ECS/Scene.hpp` | 38 |
| `SceneData` | ECS | `Source/Engine/ECS/Scene.hpp` | 52 |
| `SceneEntity` | ECS | `Source/Engine/ECS/Scene.hpp` | 45 |
| `SceneLoadOptions` | ECS | `Source/Engine/ECS/Scene.hpp` | 76 |
| `SceneMeta` | ECS | `Source/Engine/ECS/Scene.hpp` | 31 |
| `Schedule` | ECS | `Source/Engine/ECS/Schedule.hpp` | 48 |
| `ScopedAccess` | ECS | `Source/Engine/ECS/FCS/AccessValidation.hpp` | 104 |
| `SdfBrickMap` | ECS | `Source/Engine/ECS/Lighting.hpp` | 17 |
| `SdfGiVolume` | ECS | `Source/Engine/ECS/Lighting.hpp` | 18 |
| `SdfLightingFrame` | ECS | `Source/Engine/ECS/Lighting.hpp` | 22 |
| `SdfSunComponent` | ECS | `Source/Engine/ECS/Lighting.hpp` | 84 |
| `SkeletonAssetRef` | ECS | `Source/Engine/ECS/SkeletonAssetRef.hpp` | 20 |
| `SkeletonAssetSource` | ECS | `Source/Engine/ECS/SkeletonAsset.hpp` | 189 |
| `SkeletonAssetView` | ECS | `Source/Engine/ECS/SkeletonAsset.hpp` | 121 |
| `SkeletonBoneName` | ECS | `Source/Engine/ECS/SkeletonAsset.hpp` | 168 |
| `SkeletonComponent` | ECS | `Source/Engine/ECS/Skeleton.hpp` | 122 |
| `SkeletonNameTable` | ECS | `Source/Engine/ECS/SkeletonAsset.hpp` | 175 |
| `SkeletonSystem` | ECS | `Source/Engine/ECS/Skeleton.hpp` | 205 |
| `SkinComponent` | ECS | `Source/Engine/ECS/Skeleton.hpp` | 143 |
| `SkinnedSprite` | ECS | `Source/Engine/ECS/Skeleton.hpp` | 154 |
| `SleepingStaticComponent` | ECS | `Source/Engine/ECS/Presentation.hpp` | 202 |
| `SnapshotBaseline` | ECS | `Source/Engine/ECS/Snapshot.hpp` | 316 |
| `SnapshotComponent` | ECS | `Source/Engine/ECS/Snapshot.hpp` | 78 |
| `SnapshotComponentOp` | ECS | `Source/Engine/ECS/Snapshot.hpp` | 43 |
| `SnapshotExtras` | ECS | `Source/Engine/ECS/Snapshot.hpp` | 267 |
| `SnapshotFlags` | ECS | `Source/Engine/ECS/Snapshot.hpp` | 31 |
| `SnapshotHeader` | ECS | `Source/Engine/ECS/Snapshot.hpp` | 49 |
| `SnapshotRecordFlags` | ECS | `Source/Engine/ECS/Snapshot.hpp` | 37 |
| `SnapshotRestoreExtras` | ECS | `Source/Engine/ECS/Snapshot.hpp` | 272 |
| `SnapshotSchema` | ECS | `Source/Engine/ECS/Snapshot.hpp` | 93 |
| `SnapshotSchema` | ECS | `Source/Engine/ECS/Snapshot.hpp` | 208 |
| `SnapshotView` | ECS | `Source/Engine/ECS/Snapshot.hpp` | 257 |
| `Sprite2D` | ECS | `Source/Engine/ECS/CoreComponents.hpp` | 47 |
| `SpriteAnimationComponent` | ECS | `Source/Engine/ECS/SpriteAnimation.hpp` | 21 |
| `SpriteAnimationEventQueue` | ECS | `Source/Engine/ECS/SpriteAnimation.hpp` | 74 |
| `SpriteAnimationSystem` | ECS | `Source/Engine/ECS/SpriteAnimation.hpp` | 118 |
| `SpriteFrameEvent` | ECS | `Source/Engine/ECS/SpriteAnimation.hpp` | 66 |
| `SpriteFrameRect` | ECS | `Source/Engine/ECS/SpriteAnimation.hpp` | 55 |
| `SpriteOverlay2D` | ECS | `Source/Engine/ECS/CoreComponents.hpp` | 64 |
| `SpriteSheetComponent` | ECS | `Source/Engine/ECS/SpriteAnimation.hpp` | 40 |
| `StableIdAllocator` | ECS | `Source/Engine/ECS/StableIds.hpp` | 25 |
| `StateTreeComponent` | ECS | `Source/Engine/ECS/StateTree.hpp` | 195 |
| `StateTreeDefinition` | ECS | `Source/Engine/ECS/StateTree.hpp` | 152 |
| `StateTreeEvaluator` | ECS | `Source/Engine/ECS/StateTree.hpp` | 55 |
| `StateTreeEvaluatorKind` | ECS | `Source/Engine/ECS/StateTree.hpp` | 28 |
| `StateTreeNode` | ECS | `Source/Engine/ECS/StateTree.hpp` | 115 |
| `StateTreeNodeKind` | ECS | `Source/Engine/ECS/StateTree.hpp` | 105 |
| `StateTreeSystem` | ECS | `Source/Engine/ECS/StateTree.hpp` | 239 |
| `StateTreeTask` | ECS | `Source/Engine/ECS/StateTree.hpp` | 87 |
| `StateTreeTaskKind` | ECS | `Source/Engine/ECS/StateTree.hpp` | 73 |
| `StateTreeTransition` | ECS | `Source/Engine/ECS/StateTree.hpp` | 134 |
| `SystemAccess` | ECS | `Source/Engine/ECS/SystemGraph.hpp` | 65 |
| `SystemContext` | ECS | `Source/Engine/ECS/SystemContext.hpp` | 129 |
| `SystemGraph` | ECS | `Source/Engine/ECS/Schedule.hpp` | 15 |
| `SystemGraph` | ECS | `Source/Engine/ECS/SystemGraph.hpp` | 116 |
| `TargetIndexEntry` | ECS | `Source/Engine/ECS/LightSource.hpp` | 244 |
| `TextComponent` | ECS | `Source/Engine/ECS/Components.hpp` | 198 |
| `TextConditionComponent` | ECS | `Source/Engine/ECS/Components.hpp` | 282 |
| `TextEffectComponent` | ECS | `Source/Engine/ECS/Components.hpp` | 252 |
| `TextFormatComponent` | ECS | `Source/Engine/ECS/Components.hpp` | 264 |
| `TextService` | ECS | `Source/Engine/ECS/SystemContext.hpp` | 40 |
| `TextServices` | ECS | `Source/Engine/ECS/SystemContext.hpp` | 90 |
| `TextShapeCache` | ECS | `Source/Engine/ECS/SystemContext.hpp` | 41 |
| `TextShapeComponent` | ECS | `Source/Engine/ECS/Components.hpp` | 241 |
| `TilemapAssetRefComponent` | ECS | `Source/Engine/ECS/Tilemap.hpp` | 130 |
| `TilemapComponent` | ECS | `Source/Engine/ECS/Tilemap.hpp` | 63 |
| `TilemapRenderSystem` | ECS | `Source/Engine/ECS/Tilemap.hpp` | 150 |
| `TileQuad` | ECS | `Source/Engine/ECS/Tilemap.hpp` | 142 |
| `TimelineEvent` | ECS | `Source/Engine/ECS/AnimationTimeline.hpp` | 67 |
| `TimelineEventKind` | ECS | `Source/Engine/ECS/AnimationTimeline.hpp` | 36 |
| `TimelineKey` | ECS | `Source/Engine/ECS/AnimationTimeline.hpp` | 43 |
| `TimelineTrack` | ECS | `Source/Engine/ECS/AnimationTimeline.hpp` | 56 |
| `TimelineTriggerSink` | ECS | `Source/Engine/ECS/AnimationTimeline.hpp` | 83 |
| `TimeScaleComponent` | ECS | `Source/Engine/ECS/Presentation.hpp` | 168 |
| `TrackKind` | ECS | `Source/Engine/ECS/AnimationTimeline.hpp` | 20 |
| `Transform2D` | ECS | `Source/Engine/ECS/CoreComponents.hpp` | 16 |
| `TransportGroupComponent` | ECS | `Source/Engine/ECS/Components.hpp` | 123 |
| `TransportMotion` | ECS | `Source/Engine/ECS/Components.hpp` | 109 |
| `TransportPath` | ECS | `Source/Engine/ECS/Components.hpp` | 100 |
| `TransportSurface` | ECS | `Source/Engine/ECS/Components.hpp` | 90 |
| `UiSystemView` | ECS | `Source/Engine/ECS/SystemContext.hpp` | 122 |
| `UiSystemView` | ECS | `Source/Engine/ECS/SystemContext.hpp` | 326 |
| `UvCacheEntry` | ECS | `Source/Engine/ECS/Tilemap.hpp` | 176 |
| `Vehicle2D` | ECS | `Source/Engine/ECS/Components.hpp` | 54 |
| `VehicleFlags` | ECS | `Source/Engine/ECS/Components.hpp` | 46 |
| `Velocity` | ECS | `Source/Engine/ECS/CoreComponents.hpp` | 41 |
| `View` | ECS | `Source/Engine/ECS/CommandLog.hpp` | 187 |
| `View` | ECS | `Source/Engine/ECS/FCS/Registry.hpp` | 106 |
| `ViewIterationGuard` | ECS | `Source/Engine/ECS/FCS/Registry.hpp` | 41 |
| `VirtualFileSystem` | ECS | `Source/Engine/ECS/DirectionalAnimationPlayer.hpp` | 16 |
| `VirtualFileSystem` | ECS | `Source/Engine/ECS/Scene.hpp` | 18 |
| `VirtualFileSystem` | ECS | `Source/Engine/ECS/SkeletonAsset.hpp` | 18 |
| `VirtualFileSystem` | ECS | `Source/Engine/ECS/SystemContext.hpp` | 15 |
| `VirtualFileSystem` | ECS | `Source/Engine/ECS/Tilemap.hpp` | 20 |
| `Wheel2D` | ECS | `Source/Engine/ECS/Components.hpp` | 70 |
| `WindowManager` | ECS | `Source/Engine/ECS/SystemContext.hpp` | 14 |
| `World` | ECS | `Source/Engine/ECS/FCS/World.hpp` | 284 |
| `WorldMemoryStats` | ECS | `Source/Engine/ECS/FCS/World.hpp` | 241 |
| `WorldSnapshotV3` | ECS | `Source/Engine/ECS/SnapshotWorld.hpp` | 86 |

### Engine/Effects (16)

| Символ | Вид | Заголовок | Строка |
|---|---|---|---|
| `Binding` | Effects | `Source/Engine/Effects/ParticleScene.hpp` | 93 |
| `EmitterState` | Effects | `Source/Engine/Effects/ParticleEffects.hpp` | 174 |
| `ParticleEffectComponent` | Effects | `Source/Engine/Effects/ParticleScene.hpp` | 24 |
| `ParticleEffectComponentFlags` | Effects | `Source/Engine/Effects/ParticleScene.hpp` | 17 |
| `ParticleEffectPreset` | Effects | `Source/Engine/Effects/ParticleEffects.hpp` | 120 |
| `ParticleEffectSettings` | Effects | `Source/Engine/Effects/ParticleEffects.hpp` | 70 |
| `ParticleEffectSystem` | Effects | `Source/Engine/Effects/ParticleEffects.hpp` | 140 |
| `ParticleEmissionShape` | Effects | `Source/Engine/Effects/ParticleEffects.hpp` | 42 |
| `ParticlePresetDesc` | Effects | `Source/Engine/Effects/ParticlePreset.hpp` | 26 |
| `ParticlePresetFlags` | Effects | `Source/Engine/Effects/ParticlePreset.hpp` | 20 |
| `ParticlePresetRegistry` | Effects | `Source/Engine/Effects/ParticlePreset.hpp` | 38 |
| `ParticleSceneSystem` | Effects | `Source/Engine/Effects/ParticleScene.hpp` | 74 |
| `ParticleShape` | Effects | `Source/Engine/Effects/ParticleEffects.hpp` | 23 |
| `ParticleShapeAtlas` | Effects | `Source/Engine/Effects/ParticleEffects.hpp` | 58 |
| `ParticleShapeFrame` | Effects | `Source/Engine/Effects/ParticleEffects.hpp` | 51 |
| `ParticleState` | Effects | `Source/Engine/Effects/ParticleEffects.hpp` | 187 |

### Engine/Gameplay (32)

| Символ | Вид | Заголовок | Строка |
|---|---|---|---|
| `ActiveGameplayEffect` | Gameplay | `Source/Engine/Gameplay/GameplayEffects.hpp` | 66 |
| `DefIndexEntry` | Gameplay | `Source/Engine/Gameplay/GameplayEffects.hpp` | 163 |
| `Entry` | Gameplay | `Source/Engine/Gameplay/GameplayAttributes.hpp` | 50 |
| `Entry` | Gameplay | `Source/Engine/Gameplay/GameplayTags.hpp` | 152 |
| `GameplayAttribute` | Gameplay | `Source/Engine/Gameplay/GameplayAttributes.hpp` | 21 |
| `GameplayAttributeModifier` | Gameplay | `Source/Engine/Gameplay/GameplayAttributes.hpp` | 82 |
| `GameplayAttributeRegistry` | Gameplay | `Source/Engine/Gameplay/GameplayAttributes.hpp` | 35 |
| `GameplayAttributesComponent` | Gameplay | `Source/Engine/Gameplay/GameplayAttributes.hpp` | 66 |
| `GameplayAttributeValues` | Gameplay | `Source/Engine/Gameplay/GameplayAttributes.hpp` | 60 |
| `GameplayEffectDef` | Gameplay | `Source/Engine/Gameplay/GameplayEffects.hpp` | 45 |
| `GameplayEffectEvent` | Gameplay | `Source/Engine/Gameplay/GameplayEffects.hpp` | 95 |
| `GameplayEffectEventType` | Gameplay | `Source/Engine/Gameplay/GameplayEffects.hpp` | 88 |
| `GameplayEffectFlags` | Gameplay | `Source/Engine/Gameplay/GameplayEffects.hpp` | 32 |
| `GameplayEffectsComponent` | Gameplay | `Source/Engine/Gameplay/GameplayEffects.hpp` | 78 |
| `GameplayEffectStackPolicy` | Gameplay | `Source/Engine/Gameplay/GameplayEffects.hpp` | 22 |
| `GameplayEffectSystem` | Gameplay | `Source/Engine/Gameplay/GameplayEffects.hpp` | 107 |
| `GameplayModifierOp` | Gameplay | `Source/Engine/Gameplay/GameplayAttributes.hpp` | 75 |
| `GameplaySkillDef` | Gameplay | `Source/Engine/Gameplay/GameplaySkills.hpp` | 55 |
| `GameplaySkillEvent` | Gameplay | `Source/Engine/Gameplay/GameplaySkills.hpp` | 98 |
| `GameplaySkillEventType` | Gameplay | `Source/Engine/Gameplay/GameplaySkills.hpp` | 33 |
| `GameplaySkillFailReason` | Gameplay | `Source/Engine/Gameplay/GameplaySkills.hpp` | 41 |
| `GameplaySkillsComponent` | Gameplay | `Source/Engine/Gameplay/GameplaySkills.hpp` | 89 |
| `GameplaySkillSlot` | Gameplay | `Source/Engine/Gameplay/GameplaySkills.hpp` | 77 |
| `GameplaySkillState` | Gameplay | `Source/Engine/Gameplay/GameplaySkills.hpp` | 27 |
| `GameplaySkillSystem` | Gameplay | `Source/Engine/Gameplay/GameplaySkills.hpp` | 110 |
| `GameplaySkillTargetKind` | Gameplay | `Source/Engine/Gameplay/GameplaySkills.hpp` | 19 |
| `GameplayTag` | Gameplay | `Source/Engine/Gameplay/GameplayTags.hpp` | 22 |
| `GameplayTagContainer` | Gameplay | `Source/Engine/Gameplay/GameplayTags.hpp` | 170 |
| `GameplayTagDatabase` | Gameplay | `Source/Engine/Gameplay/GameplayTagDatabase.hpp` | 18 |
| `GameplayTagMask` | Gameplay | `Source/Engine/Gameplay/GameplayTags.hpp` | 38 |
| `GameplayTagRegistry` | Gameplay | `Source/Engine/Gameplay/GameplayTags.hpp` | 132 |
| `GameplayTagsComponent` | Gameplay | `Source/Engine/Gameplay/GameplayTags.hpp` | 211 |

### Engine/Input (14)

| Символ | Вид | Заголовок | Строка |
|---|---|---|---|
| `InputActionDesc` | Input | `Source/Engine/Input/InputSystem.hpp` | 83 |
| `InputActionState` | Input | `Source/Engine/Input/InputSystem.hpp` | 110 |
| `InputActionType` | Input | `Source/Engine/Input/InputSystem.hpp` | 23 |
| `InputContextDesc` | Input | `Source/Engine/Input/InputSystem.hpp` | 89 |
| `InputEvent` | Input | `Source/Engine/Input/InputSystem.hpp` | 122 |
| `InputEventType` | Input | `Source/Engine/Input/InputSystem.hpp` | 62 |
| `InputMappingDesc` | Input | `Source/Engine/Input/InputSystem.hpp` | 96 |
| `InputModifier` | Input | `Source/Engine/Input/InputSystem.hpp` | 76 |
| `InputModifierKind` | Input | `Source/Engine/Input/InputSystem.hpp` | 41 |
| `InputSource` | Input | `Source/Engine/Input/InputSystem.hpp` | 70 |
| `InputSourceKind` | Input | `Source/Engine/Input/InputSystem.hpp` | 30 |
| `InputSystem` | Input | `Source/Engine/Input/InputSystem.hpp` | 134 |
| `InputTriggerKind` | Input | `Source/Engine/Input/InputSystem.hpp` | 52 |
| `UserSlot` | Input | `Source/Engine/Input/InputSystem.hpp` | 180 |

### Engine/Navigation (25)

| Символ | Вид | Заголовок | Строка |
|---|---|---|---|
| `CrowdAgent` | Navigation | `Source/Engine/Navigation/CrowdPool.hpp` | 16 |
| `CrowdPool` | Navigation | `Source/Engine/Navigation/CrowdPool.hpp` | 46 |
| `CrowdRenderSettings` | Navigation | `Source/Engine/Navigation/CrowdRender.hpp` | 14 |
| `CrowdSettings` | Navigation | `Source/Engine/Navigation/CrowdPool.hpp` | 27 |
| `Entry` | Navigation | `Source/Engine/Navigation/FlowField.hpp` | 79 |
| `FlowField` | Navigation | `Source/Engine/Navigation/FlowField.hpp` | 33 |
| `FlowFieldDesc` | Navigation | `Source/Engine/Navigation/FlowField.hpp` | 22 |
| `NavAgentComponent` | Navigation | `Source/Engine/Navigation/Navigation.hpp` | 242 |
| `NavAgentPathSettings` | Navigation | `Source/Engine/Navigation/Navigation.hpp` | 206 |
| `NavAgentSettings` | Navigation | `Source/Engine/Navigation/Navigation.hpp` | 184 |
| `NavigationSystem` | Navigation | `Source/Engine/Navigation/Navigation.hpp` | 267 |
| `NavMap` | Navigation | `Source/Engine/Navigation/Navigation.hpp` | 27 |
| `NavMesh` | Navigation | `Source/Engine/Navigation/NavMesh.hpp` | 49 |
| `NavMesh` | Navigation | `Source/Engine/Navigation/Navigation.hpp` | 18 |
| `NavMeshPortal` | Navigation | `Source/Engine/Navigation/NavMesh.hpp` | 39 |
| `NavMeshRect` | Navigation | `Source/Engine/Navigation/NavMesh.hpp` | 19 |
| `NavPath` | Navigation | `Source/Engine/Navigation/Navigation.hpp` | 99 |
| `NavPathOptions` | Navigation | `Source/Engine/Navigation/Navigation.hpp` | 80 |
| `NavSmoothingMode` | Navigation | `Source/Engine/Navigation/Navigation.hpp` | 72 |
| `Page` | Navigation | `Source/Engine/Navigation/PagedNavGrid.hpp` | 105 |
| `PagedNavGrid` | Navigation | `Source/Engine/Navigation/PagedNavGrid.hpp` | 32 |
| `PagedNavGridDesc` | Navigation | `Source/Engine/Navigation/PagedNavGrid.hpp` | 23 |
| `PathFinder` | Navigation | `Source/Engine/Navigation/Navigation.hpp` | 127 |
| `PathStats` | Navigation | `Source/Engine/Navigation/Navigation.hpp` | 116 |
| `TilemapComponent` | Navigation | `Source/Engine/Navigation/PagedNavGrid.hpp` | 14 |

### Engine/Network (91)

| Символ | Вид | Заголовок | Строка |
|---|---|---|---|
| `CellCoordinate` | Network | `Source/Engine/Network/RelevanceGrid.hpp` | 168 |
| `ChecksumComparison` | Network | `Source/Engine/Network/NetRollback.hpp` | 457 |
| `Config` | Network | `Source/Engine/Network/NetRollback.hpp` | 316 |
| `DatagramSocket` | Network | `Source/Engine/Network/Transport.hpp` | 32 |
| `Endpoint` | Network | `Source/Engine/Network/NetLink.hpp` | 92 |
| `Frame` | Network | `Source/Engine/Network/NetLink.hpp` | 84 |
| `FrameInfo` | Network | `Source/Engine/Network/NetRollback.hpp` | 221 |
| `FrameMeta` | Network | `Source/Engine/Network/NetRollback.hpp` | 473 |
| `InputChannel` | Network | `Source/Engine/Network/NetProtocol.hpp` | 56 |
| `InputChannelStats` | Network | `Source/Engine/Network/NetProtocol.hpp` | 44 |
| `InputPacket` | Network | `Source/Engine/Network/NetProtocol.hpp` | 24 |
| `InputRingBuffer` | Network | `Source/Engine/Network/NetRollback.hpp` | 123 |
| `LobbyError` | Network | `Source/Engine/Network/Lobby.hpp` | 37 |
| `LobbyMessageType` | Network | `Source/Engine/Network/Lobby.hpp` | 25 |
| `LobbyRoomInfo` | Network | `Source/Engine/Network/Lobby.hpp` | 73 |
| `LobbyServer` | Network | `Source/Engine/Network/Lobby.hpp` | 93 |
| `LobbyServerConfig` | Network | `Source/Engine/Network/Lobby.hpp` | 82 |
| `LocalEntry` | Network | `Source/Engine/Network/NetRollback.hpp` | 205 |
| `Member` | Network | `Source/Engine/Network/Lobby.hpp` | 126 |
| `MemoryEndpoint` | Network | `Source/Engine/Network/NetLink.hpp` | 46 |
| `MemoryEndpoint` | Network | `Source/Engine/Network/NetLink.hpp` | 113 |
| `MemoryNetwork` | Network | `Source/Engine/Network/NetLink.hpp` | 53 |
| `NetAddress` | Network | `Source/Engine/Network/Transport.hpp` | 19 |
| `NetDiagnosticsSnapshot` | Network | `Source/Engine/Network/Diagnostics.hpp` | 12 |
| `NetDisconnectReason` | Network | `Source/Engine/Network/Session.hpp` | 65 |
| `NetEntityId` | Network | `Source/Engine/Network/NetRollback.hpp` | 39 |
| `NetEntityMap` | Network | `Source/Engine/Network/NetRollback.hpp` | 167 |
| `NetEntry` | Network | `Source/Engine/Network/NetRollback.hpp` | 200 |
| `NetIdentity` | Network | `Source/Engine/Network/Identity.hpp` | 32 |
| `NetInputFrame` | Network | `Source/Engine/Network/NetRollback.hpp` | 70 |
| `NetInputStatus` | Network | `Source/Engine/Network/NetRollback.hpp` | 99 |
| `NetLink` | Network | `Source/Engine/Network/NetLink.hpp` | 19 |
| `NetLobbyClient` | Network | `Source/Engine/Network/Lobby.hpp` | 160 |
| `NetLobbyClientConfig` | Network | `Source/Engine/Network/Lobby.hpp` | 151 |
| `NetPlayerId` | Network | `Source/Engine/Network/NetRollback.hpp` | 59 |
| `NetPlayerSlot` | Network | `Source/Engine/Network/Session.hpp` | 134 |
| `NetRole` | Network | `Source/Engine/Network/Session.hpp` | 32 |
| `NetRollbackSession` | Network | `Source/Engine/Network/NetRollback.hpp` | 240 |
| `NetSession` | Network | `Source/Engine/Network/Session.hpp` | 189 |
| `NetSessionCallbacks` | Network | `Source/Engine/Network/Session.hpp` | 178 |
| `NetSessionConfig` | Network | `Source/Engine/Network/Lobby.hpp` | 15 |
| `NetSessionConfig` | Network | `Source/Engine/Network/Session.hpp` | 149 |
| `NetSessionMode` | Network | `Source/Engine/Network/Session.hpp` | 22 |
| `NetSessionStatus` | Network | `Source/Engine/Network/Session.hpp` | 46 |
| `NetSlotState` | Network | `Source/Engine/Network/Session.hpp` | 58 |
| `NetTicket` | Network | `Source/Engine/Network/Ticket.hpp` | 35 |
| `NetTickInputs` | Network | `Source/Engine/Network/NetRollback.hpp` | 110 |
| `NetTickMode` | Network | `Source/Engine/Network/Session.hpp` | 40 |
| `NetTopology` | Network | `Source/Engine/Network/NetTopology.hpp` | 11 |
| `NetTopologyPlan` | Network | `Source/Engine/Network/NetTopology.hpp` | 24 |
| `NetTransport` | Network | `Source/Engine/Network/Transport.hpp` | 130 |
| `Outstanding` | Network | `Source/Engine/Network/Transport.hpp` | 103 |
| `Peer` | Network | `Source/Engine/Network/SecureSessionTransport.hpp` | 87 |
| `PendingEvent` | Network | `Source/Engine/Network/Session.hpp` | 279 |
| `PendingState` | Network | `Source/Engine/Network/Replication.hpp` | 129 |
| `Phase` | Network | `Source/Engine/Network/SecureChannel.hpp` | 263 |
| `PhysicsSystem` | Network | `Source/Engine/Network/NetRollback.hpp` | 20 |
| `RateEntry` | Network | `Source/Engine/Network/SecureSessionTransport.hpp` | 120 |
| `Reassembly` | Network | `Source/Engine/Network/Transport.hpp` | 115 |
| `RelevanceBand` | Network | `Source/Engine/Network/RelevanceGrid.hpp` | 32 |
| `RelevanceCell` | Network | `Source/Engine/Network/RelevanceGrid.hpp` | 71 |
| `RelevanceConfig` | Network | `Source/Engine/Network/NetRollback.hpp` | 34 |
| `RelevanceConfig` | Network | `Source/Engine/Network/RelevanceGrid.hpp` | 56 |
| `RelevanceEntity` | Network | `Source/Engine/Network/RelevanceGrid.hpp` | 81 |
| `RelevanceGrid` | Network | `Source/Engine/Network/NetRollback.hpp` | 36 |
| `RelevanceGrid` | Network | `Source/Engine/Network/RelevanceGrid.hpp` | 131 |
| `RelevanceGridStats` | Network | `Source/Engine/Network/RelevanceGrid.hpp` | 121 |
| `RelevancePlan` | Network | `Source/Engine/Network/NetRollback.hpp` | 35 |
| `RelevancePlan` | Network | `Source/Engine/Network/RelevanceGrid.hpp` | 93 |
| `ReplicationStatus` | Network | `Source/Engine/Network/Replication.hpp` | 28 |
| `Result` | Network | `Source/Engine/Network/NetRollback.hpp` | 243 |
| `RollbackReport` | Network | `Source/Engine/Network/NetRollback.hpp` | 229 |
| `Room` | Network | `Source/Engine/Network/Lobby.hpp` | 132 |
| `SecureChannel` | Network | `Source/Engine/Network/SecureChannel.hpp` | 198 |
| `SecureChannelConfig` | Network | `Source/Engine/Network/SecureChannel.hpp` | 151 |
| `SecureCookieIssuer` | Network | `Source/Engine/Network/SecureChannel.hpp` | 131 |
| `SecureEndpoint` | Network | `Source/Engine/Network/SecureEndpoint.hpp` | 16 |
| `SecureSessionAuthConfig` | Network | `Source/Engine/Network/SecureSessionTransport.hpp` | 24 |
| `SecureSessionTransport` | Network | `Source/Engine/Network/SecureSessionTransport.hpp` | 38 |
| `SecureStatus` | Network | `Source/Engine/Network/SecureChannel.hpp` | 65 |
| `Slot` | Network | `Source/Engine/Network/NetRollback.hpp` | 155 |
| `SnapshotReceiver` | Network | `Source/Engine/Network/Replication.hpp` | 177 |
| `SnapshotReceiverConfig` | Network | `Source/Engine/Network/Replication.hpp` | 166 |
| `SnapshotSender` | Network | `Source/Engine/Network/Replication.hpp` | 94 |
| `SnapshotSenderConfig` | Network | `Source/Engine/Network/Replication.hpp` | 70 |
| `SnapshotSenderStats` | Network | `Source/Engine/Network/Replication.hpp` | 80 |
| `SortableEntity` | Network | `Source/Engine/Network/RelevanceGrid.hpp` | 195 |
| `TicketStatus` | Network | `Source/Engine/Network/Ticket.hpp` | 54 |
| `TransportChannel` | Network | `Source/Engine/Network/Transport.hpp` | 63 |
| `TransportPeer` | Network | `Source/Engine/Network/Transport.hpp` | 85 |
| `TransportStats` | Network | `Source/Engine/Network/Transport.hpp` | 69 |

### Engine/Physics (142)

| Символ | Вид | Заголовок | Строка |
|---|---|---|---|
| `BodyColumns` | Physics | `Source/Engine/Physics/DodPhysicsBackend.hpp` | 59 |
| `BodyRecord` | Physics | `Source/Engine/Physics/Physics.hpp` | 307 |
| `BodyType` | Physics | `Source/Engine/Physics/Physics.hpp` | 53 |
| `ChainBreakEvent` | Physics | `Source/Engine/Physics/PhysicsJoints.hpp` | 314 |
| `ChainError` | Physics | `Source/Engine/Physics/PhysicsJoints.hpp` | 146 |
| `ChainRuntime` | Physics | `Source/Engine/Physics/PhysicsJoints.hpp` | 618 |
| `ClothComponent` | Physics | `Source/Engine/Physics/Cloth.hpp` | 31 |
| `ClothDesc` | Physics | `Source/Engine/Physics/Cloth.hpp` | 63 |
| `ClothFlags` | Physics | `Source/Engine/Physics/Cloth.hpp` | 20 |
| `ClothHandle` | Physics | `Source/Engine/Physics/Cloth.hpp` | 46 |
| `ClothInfo` | Physics | `Source/Engine/Physics/Cloth.hpp` | 99 |
| `ClothRenderSystem` | Physics | `Source/Engine/Physics/ClothRenderSystem.hpp` | 15 |
| `ClothSystem` | Physics | `Source/Engine/Physics/Cloth.hpp` | 110 |
| `Collider2D` | Physics | `Source/Engine/Physics/Physics.hpp` | 90 |
| `CollisionFilterMatrix` | Physics | `Source/Engine/Physics/CollisionFilter.hpp` | 48 |
| `CollisionLayer` | Physics | `Source/Engine/Physics/CollisionFilter.hpp` | 15 |
| `ConnectionRuntime` | Physics | `Source/Engine/Physics/PhysicsJoints.hpp` | 590 |
| `ContactRecord` | Physics | `Source/Engine/Physics/DodPhysicsBackend.hpp` | 130 |
| `DodPhysicsBackend` | Physics | `Source/Engine/Physics/DodPhysicsBackend.hpp` | 16 |
| `DodPhysicsBackend` | Physics | `Source/Engine/Physics/Physics.hpp` | 20 |
| `FluidDesc` | Physics | `Source/Engine/Physics/Fluid.hpp` | 114 |
| `FluidInteractorComponent` | Physics | `Source/Engine/Physics/Fluid.hpp` | 210 |
| `FluidWaveSystem` | Physics | `Source/Engine/Physics/Fluid.hpp` | 221 |
| `GerstnerField` | Physics | `Source/Engine/Physics/Fluid.hpp` | 49 |
| `GerstnerSample` | Physics | `Source/Engine/Physics/Fluid.hpp` | 39 |
| `GerstnerWave` | Physics | `Source/Engine/Physics/Fluid.hpp` | 25 |
| `GravityBodyComponent` | Physics | `Source/Engine/Physics/Gravity.hpp` | 79 |
| `GravityBodyMode` | Physics | `Source/Engine/Physics/Gravity.hpp` | 33 |
| `GravityBodyShape` | Physics | `Source/Engine/Physics/Gravity.hpp` | 41 |
| `GravityDirection` | Physics | `Source/Engine/Physics/Gravity.hpp` | 15 |
| `GravityFalloff` | Physics | `Source/Engine/Physics/Gravity.hpp` | 25 |
| `GravitySystem` | Physics | `Source/Engine/Physics/Gravity.hpp` | 124 |
| `GridEntry` | Physics | `Source/Engine/Physics/DodPhysicsBackend.hpp` | 121 |
| `GroupIndexEntry` | Physics | `Source/Engine/Physics/TransportGroups.hpp` | 127 |
| `JointBreakReason` | Physics | `Source/Engine/Physics/PhysicsJoints.hpp` | 72 |
| `JointChainComponent` | Physics | `Source/Engine/Physics/PhysicsJoints.hpp` | 378 |
| `JointChainDesc` | Physics | `Source/Engine/Physics/PhysicsJoints.hpp` | 187 |
| `JointChainFlags` | Physics | `Source/Engine/Physics/PhysicsJoints.hpp` | 91 |
| `JointChainHandle` | Physics | `Source/Engine/Physics/PhysicsJoints.hpp` | 250 |
| `JointChainInfo` | Physics | `Source/Engine/Physics/PhysicsJoints.hpp` | 429 |
| `JointChainKind` | Physics | `Source/Engine/Physics/PhysicsJoints.hpp` | 54 |
| `JointChainSection` | Physics | `Source/Engine/Physics/PhysicsJoints.hpp` | 119 |
| `JointConnectionFlags` | Physics | `Source/Engine/Physics/PhysicsJoints.hpp` | 80 |
| `JointConnectionHandle` | Physics | `Source/Engine/Physics/PhysicsJoints.hpp` | 268 |
| `JointConnectionInfo` | Physics | `Source/Engine/Physics/PhysicsJoints.hpp` | 287 |
| `JointConnectionKind` | Physics | `Source/Engine/Physics/PhysicsJoints.hpp` | 64 |
| `JointConnectionParams` | Physics | `Source/Engine/Physics/PhysicsJoints.hpp` | 559 |
| `JointLinkComponent` | Physics | `Source/Engine/Physics/PhysicsJoints.hpp` | 33 |
| `JointLinkFlags` | Physics | `Source/Engine/Physics/PhysicsJoints.hpp` | 22 |
| `JointTieDesc` | Physics | `Source/Engine/Physics/PhysicsJoints.hpp` | 328 |
| `JointTieSave` | Physics | `Source/Engine/Physics/PhysicsJoints.hpp` | 351 |
| `LevelGravityComponent` | Physics | `Source/Engine/Physics/Gravity.hpp` | 55 |
| `ParticleEmitterComponent` | Physics | `Source/Engine/Physics/Particles.hpp` | 34 |
| `ParticleEmitterFlags` | Physics | `Source/Engine/Physics/Particles.hpp` | 18 |
| `ParticleEmitterGpu` | Physics | `Source/Engine/Physics/Particles.hpp` | 69 |
| `ParticleSeedPush` | Physics | `Source/Engine/Physics/Particles.hpp` | 92 |
| `ParticleStateGpu` | Physics | `Source/Engine/Physics/Particles.hpp` | 100 |
| `PhysicsBackendKind` | Physics | `Source/Engine/Physics/Physics.hpp` | 32 |
| `PhysicsBodyRemapEntry` | Physics | `Source/Engine/Physics/SnapshotRemap.hpp` | 17 |
| `PhysicsBodyRemapTable` | Physics | `Source/Engine/Physics/SnapshotRemap.hpp` | 24 |
| `PhysicsCapabilities` | Physics | `Source/Engine/Physics/Physics.hpp` | 39 |
| `PhysicsContactEvent` | Physics | `Source/Engine/Physics/Physics.hpp` | 118 |
| `PhysicsDesc` | Physics | `Source/Engine/Physics/Physics.hpp` | 154 |
| `PhysicsEventType` | Physics | `Source/Engine/Physics/Physics.hpp` | 111 |
| `PhysicsFixedStepSystem` | Physics | `Source/Engine/Physics/Physics.hpp` | 340 |
| `PhysicsJointSystem` | Physics | `Source/Engine/Physics/PhysicsJoints.hpp` | 444 |
| `PhysicsRayHit` | Physics | `Source/Engine/Physics/Physics.hpp` | 129 |
| `PhysicsSystem` | Physics | `Source/Engine/Physics/Physics.hpp` | 168 |
| `PhysicsSystem` | Physics | `Source/Engine/Physics/TransportGroups.hpp` | 13 |
| `Projectile2D` | Physics | `Source/Engine/Physics/Physics.hpp` | 143 |
| `ProjectileHitEvent` | Physics | `Source/Engine/Physics/ProjectileSystem.hpp` | 12 |
| `ProjectileSystem` | Physics | `Source/Engine/Physics/ProjectileSystem.hpp` | 24 |
| `RigidBody2D` | Physics | `Source/Engine/Physics/Physics.hpp` | 73 |
| `RigidBodyFlags` | Physics | `Source/Engine/Physics/Physics.hpp` | 60 |
| `Runtime` | Physics | `Source/Engine/Physics/Cloth.hpp` | 156 |
| `SdfAtlasImage` | Physics | `Source/Engine/Physics/SdfAtlas.hpp` | 18 |
| `SdfBonePart` | Physics | `Source/Engine/Physics/SdfCharacter.hpp` | 24 |
| `SdfBrickGpu` | Physics | `Source/Engine/Physics/SdfBrickMap.hpp` | 48 |
| `SdfBrickHeader` | Physics | `Source/Engine/Physics/SdfBrickMap.hpp` | 32 |
| `SdfBrickHeader` | Physics | `Source/Engine/Physics/SdfCollision.hpp` | 107 |
| `SdfBrickMap` | Physics | `Source/Engine/Physics/SdfBrickMap.hpp` | 79 |
| `SdfBrickMap` | Physics | `Source/Engine/Physics/SdfCollision.hpp` | 106 |
| `SdfBrickMapDesc` | Physics | `Source/Engine/Physics/SdfBrickMap.hpp` | 63 |
| `SdfBrickState` | Physics | `Source/Engine/Physics/SdfBrickMap.hpp` | 24 |
| `SdfCarveOp` | Physics | `Source/Engine/Physics/SdfDestructible.hpp` | 25 |
| `SdfCarveReplayContext` | Physics | `Source/Engine/Physics/SdfDestructionCommands.hpp` | 37 |
| `SdfCarveShape` | Physics | `Source/Engine/Physics/SdfDestructible.hpp` | 17 |
| `SdfCharacterComponent` | Physics | `Source/Engine/Physics/SdfCharacter.hpp` | 41 |
| `SdfCharacterField` | Physics | `Source/Engine/Physics/SdfCharacter.hpp` | 78 |
| `SdfCharacterMeshData` | Physics | `Source/Engine/Physics/SdfCharacter.hpp` | 110 |
| `SdfCollisionDesc` | Physics | `Source/Engine/Physics/SdfCollision.hpp` | 49 |
| `SdfCollisionSystem` | Physics | `Source/Engine/Physics/SdfCollision.hpp` | 109 |
| `SdfComposeLayer` | Physics | `Source/Engine/Physics/SdfComposer.hpp` | 25 |
| `SdfComposeOp` | Physics | `Source/Engine/Physics/SdfComposer.hpp` | 16 |
| `SdfComposer` | Physics | `Source/Engine/Physics/SdfComposer.hpp` | 37 |
| `SdfComposerSession` | Physics | `Source/Engine/Physics/SdfComposerSession.hpp` | 15 |
| `SdfConnection` | Physics | `Source/Engine/Physics/SdfDamage.hpp` | 67 |
| `SdfContact` | Physics | `Source/Engine/Physics/SdfCollision.hpp` | 66 |
| `SdfContactManifold` | Physics | `Source/Engine/Physics/SdfCollision.hpp` | 97 |
| `SdfContactPoint` | Physics | `Source/Engine/Physics/SdfCollision.hpp` | 86 |
| `SdfDamageModel` | Physics | `Source/Engine/Physics/SdfDamage.hpp` | 189 |
| `SdfDebrisPiece` | Physics | `Source/Engine/Physics/SdfDebris.hpp` | 20 |
| `SdfDebrisSettings` | Physics | `Source/Engine/Physics/SdfDebris.hpp` | 31 |
| `SdfDestructible` | Physics | `Source/Engine/Physics/SdfDestructible.hpp` | 56 |
| `SdfDestructible` | Physics | `Source/Engine/Physics/SdfWorld.hpp` | 16 |
| `SdfDestructibleDesc` | Physics | `Source/Engine/Physics/SdfDestructible.hpp` | 36 |
| `SdfField` | Physics | `Source/Engine/Physics/SdfCollision.hpp` | 19 |
| `SdfGiAsset` | Physics | `Source/Engine/Physics/SdfGiAsset.hpp` | 65 |
| `SdfGiAssetHeader` | Physics | `Source/Engine/Physics/SdfGiAsset.hpp` | 45 |
| `SdfGiBaked` | Physics | `Source/Engine/Physics/SdfGiBake.hpp` | 45 |
| `SdfGiBakedProbe` | Physics | `Source/Engine/Physics/SdfGiBake.hpp` | 33 |
| `SdfGiBakeSettings` | Physics | `Source/Engine/Physics/SdfGiBake.hpp` | 17 |
| `SdfGiProbe` | Physics | `Source/Engine/Physics/SdfGi.hpp` | 22 |
| `SdfGiVolume` | Physics | `Source/Engine/Physics/SdfGi.hpp` | 41 |
| `SdfImpact` | Physics | `Source/Engine/Physics/SdfDamage.hpp` | 86 |
| `SdfImpactResult` | Physics | `Source/Engine/Physics/SdfDamage.hpp` | 101 |
| `SdfMaterialProfile` | Physics | `Source/Engine/Physics/SdfDamage.hpp` | 26 |
| `SdfPart` | Physics | `Source/Engine/Physics/SdfDamage.hpp` | 48 |
| `SdfPrimitiveInfo` | Physics | `Source/Engine/Physics/SdfPrimitives.hpp` | 137 |
| `SdfShape` | Physics | `Source/Engine/Physics/SdfPrimitives.hpp` | 82 |
| `SdfShapeComponent` | Physics | `Source/Engine/Physics/SdfPrimitives.hpp` | 127 |
| `SdfShapeType` | Physics | `Source/Engine/Physics/SdfPrimitives.hpp` | 16 |
| `SdfSmokeDesc` | Physics | `Source/Engine/Physics/SdfSmoke.hpp` | 16 |
| `SdfSmokeField` | Physics | `Source/Engine/Physics/SdfSmoke.hpp` | 33 |
| `SdfVolumeAsset` | Physics | `Source/Engine/Physics/SdfVolumeAsset.hpp` | 52 |
| `SdfVolumeHeader` | Physics | `Source/Engine/Physics/SdfVolumeAsset.hpp` | 26 |
| `SdfWorld` | Physics | `Source/Engine/Physics/SdfWorld.hpp` | 60 |
| `SdfWorldBox` | Physics | `Source/Engine/Physics/SdfWorld.hpp` | 45 |
| `SdfWorldCircle` | Physics | `Source/Engine/Physics/SdfWorld.hpp` | 51 |
| `SdfWorldDesc` | Physics | `Source/Engine/Physics/SdfWorld.hpp` | 31 |
| `SdfWorldImprint` | Physics | `Source/Engine/Physics/SdfWorld.hpp` | 20 |
| `ShallowWaterGrid` | Physics | `Source/Engine/Physics/Fluid.hpp` | 134 |
| `Storage` | Physics | `Source/Engine/Physics/Cloth.hpp` | 155 |
| `Storage` | Physics | `Source/Engine/Physics/PhysicsJoints.hpp` | 642 |
| `SystemGraph` | Physics | `Source/Engine/Physics/Physics.hpp` | 24 |
| `TilemapComponent` | Physics | `Source/Engine/Physics/SdfWorld.hpp` | 15 |
| `Tracked` | Physics | `Source/Engine/Physics/Fluid.hpp` | 255 |
| `TransportCapabilities` | Physics | `Source/Engine/Physics/TransportGroups.hpp` | 31 |
| `TransportGroupState` | Physics | `Source/Engine/Physics/TransportGroups.hpp` | 41 |
| `TransportGroupSystem` | Physics | `Source/Engine/Physics/TransportGroups.hpp` | 63 |
| `WaterView` | Physics | `Source/Engine/Physics/Fluid.hpp` | 85 |
| `WheelSystem` | Physics | `Source/Engine/Physics/VehicleWheels.hpp` | 16 |

### Engine/Platform (21)

| Символ | Вид | Заголовок | Строка |
|---|---|---|---|
| `AchievementFlags` | Platform | `Source/Engine/Platform/PlatformSystems.hpp` | 95 |
| `AchievementRequest` | Platform | `Source/Engine/Platform/PlatformSystems.hpp` | 104 |
| `CloudSaveComponent` | Platform | `Source/Engine/Platform/PlatformSystems.hpp` | 124 |
| `CloudSaveFlags` | Platform | `Source/Engine/Platform/PlatformSystems.hpp` | 115 |
| `EgsStoreSystem` | Platform | `Source/Engine/Platform/PlatformSystems.hpp` | 178 |
| `GogAchievementSystem` | Platform | `Source/Engine/Platform/PlatformSystems.hpp` | 224 |
| `GogStorageSystem` | Platform | `Source/Engine/Platform/PlatformSystems.hpp` | 258 |
| `PlatformAccountComponent` | Platform | `Source/Engine/Platform/PlatformSystems.hpp` | 137 |
| `PlatformCommand` | Platform | `Source/Engine/Platform/PlatformService.hpp` | 35 |
| `PlatformCommandLog` | Platform | `Source/Engine/Platform/PlatformSystems.hpp` | 46 |
| `PlatformCommandType` | Platform | `Source/Engine/Platform/PlatformService.hpp` | 24 |
| `PlatformLogEvent` | Platform | `Source/Engine/Platform/PlatformSystems.hpp` | 35 |
| `PlatformLogType` | Platform | `Source/Engine/Platform/PlatformSystems.hpp` | 26 |
| `PlatformService` | Platform | `Source/Engine/Platform/PlatformService.hpp` | 76 |
| `PlatformServiceCallbacks` | Platform | `Source/Engine/Platform/PlatformService.hpp` | 51 |
| `PlatformServiceDesc` | Platform | `Source/Engine/Platform/PlatformService.hpp` | 71 |
| `PlatformServiceStats` | Platform | `Source/Engine/Platform/PlatformService.hpp` | 59 |
| `PlatformState` | Platform | `Source/Engine/Platform/PlatformService.hpp` | 132 |
| `StagedSave` | Platform | `Source/Engine/Platform/PlatformSystems.hpp` | 204 |
| `StagedSave` | Platform | `Source/Engine/Platform/PlatformSystems.hpp` | 291 |
| `SteamAchievementSystem` | Platform | `Source/Engine/Platform/PlatformSystems.hpp` | 148 |

### Engine/RHI (194)

| Символ | Вид | Заголовок | Строка |
|---|---|---|---|
| `AccessibilityCompositePush` | RHI | `Source/Engine/RHI/Vulkan/VulkanRHIInternal.hpp` | 282 |
| `Api` | RHI | `Source/Engine/RHI/Vulkan/VulkanLoader.hpp` | 123 |
| `Backend` | RHI | `Source/Engine/RHI/RHI.hpp` | 19 |
| `BackendCapabilities` | RHI | `Source/Engine/RHI/UpscalerCapabilities.hpp` | 28 |
| `BackendSelection` | RHI | `Source/Engine/RHI/UpscalerBackend.hpp` | 28 |
| `CameraData` | RHI | `Source/Engine/RHI/RenderItem.hpp` | 255 |
| `CameraDistortionFlags` | RHI | `Source/Engine/RHI/CameraStack.hpp` | 30 |
| `CameraDistortionSettings` | RHI | `Source/Engine/RHI/CameraStack.hpp` | 40 |
| `CameraStackDesc` | RHI | `Source/Engine/RHI/CameraStack.hpp` | 56 |
| `CameraStackFlags` | RHI | `Source/Engine/RHI/CameraStack.hpp` | 17 |
| `CameraStackSystem` | RHI | `Source/Engine/RHI/CameraStack.hpp` | 81 |
| `CameraStackSystem` | RHI | `Source/Engine/RHI/RenderGraph.hpp` | 17 |
| `CameraStackSystem` | RHI | `Source/Engine/RHI/RenderPasses.hpp` | 12 |
| `ChangeMaskRenderExtract` | RHI | `Source/Engine/RHI/ChangeMaskExtract.hpp` | 33 |
| `ClusterFrame` | RHI | `Source/Engine/RHI/Vulkan/VulkanRHIInternal.hpp` | 914 |
| `ClusterGridConstants` | RHI | `Source/Engine/RHI/GpuLighting.hpp` | 46 |
| `ClusterHeader` | RHI | `Source/Engine/RHI/GpuLighting.hpp` | 36 |
| `ColorGradingConstants` | RHI | `Source/Engine/RHI/RenderPasses.hpp` | 141 |
| `ColorGradingPass` | RHI | `Source/Engine/RHI/RenderPasses.hpp` | 149 |
| `ColorGradingSettings` | RHI | `Source/Engine/RHI/RenderPasses.hpp` | 129 |
| `CookedMaterialView` | RHI | `Source/Engine/RHI/MaterialAsset.hpp` | 152 |
| `CullFrame` | RHI | `Source/Engine/RHI/Vulkan/VulkanRHIInternal.hpp` | 944 |
| `CustomFrameGenerator` | RHI | `Source/Engine/RHI/CustomFrameGenerator.hpp` | 10 |
| `DeviceDesc` | RHI | `Source/Engine/RHI/RHI.hpp` | 49 |
| `DeviceInfo` | RHI | `Source/Engine/RHI/RHI.hpp` | 112 |
| `DirtyRange` | RHI | `Source/Engine/RHI/RenderItem.hpp` | 236 |
| `DlssAdapter` | RHI | `Source/Engine/RHI/DlssAdapter.hpp` | 33 |
| `DlssQualityMode` | RHI | `Source/Engine/RHI/DlssAdapter.hpp` | 9 |
| `DlssRuntimeAvailability` | RHI | `Source/Engine/RHI/DlssRuntime.hpp` | 9 |
| `DlssRuntimeProbe` | RHI | `Source/Engine/RHI/DlssRuntimeProbe.hpp` | 14 |
| `DrawBatch` | RHI | `Source/Engine/RHI/RenderItem.hpp` | 216 |
| `DrawIndexedIndirectCommand` | RHI | `Source/Engine/RHI/GpuVisibility.hpp` | 15 |
| `DynamicResolution` | RHI | `Source/Engine/RHI/DynamicResolution.hpp` | 19 |
| `DynamicResolutionSettings` | RHI | `Source/Engine/RHI/DynamicResolution.hpp` | 9 |
| `Entry` | RHI | `Source/Engine/RHI/MaterialPipelineCache.hpp` | 74 |
| `ExportStatus` | RHI | `Source/Engine/RHI/FrameExporter.hpp` | 100 |
| `Extent2D` | RHI | `Source/Engine/RHI/RHI.hpp` | 40 |
| `ExtractStats` | RHI | `Source/Engine/RHI/ChangeMaskExtract.hpp` | 21 |
| `FidelityFxRuntimeAvailability` | RHI | `Source/Engine/RHI/FidelityFxRuntime.hpp` | 25 |
| `FrameAccessibility` | RHI | `Source/Engine/RHI/RenderItem.hpp` | 277 |
| `FrameAccessibilityFlags` | RHI | `Source/Engine/RHI/RenderItem.hpp` | 264 |
| `FrameComposer` | RHI | `Source/Engine/RHI/FrameComposer.hpp` | 99 |
| `FrameComposerStats` | RHI | `Source/Engine/RHI/FrameComposer.hpp` | 80 |
| `FrameExportDesc` | RHI | `Source/Engine/RHI/FrameExporter.hpp` | 53 |
| `FrameExporter` | RHI | `Source/Engine/RHI/FrameExporter.hpp` | 112 |
| `FrameGenBackend` | RHI | `Source/Engine/RHI/UpscalerBackend.hpp` | 19 |
| `FrameGenerationInput` | RHI | `Source/Engine/RHI/FrameGeneration.hpp` | 10 |
| `FrameGenerationPolicy` | RHI | `Source/Engine/RHI/FrameGenerationPolicy.hpp` | 19 |
| `FrameGenerationStatus` | RHI | `Source/Engine/RHI/FrameGenerationPolicy.hpp` | 9 |
| `FramePacingConfig` | RHI | `Source/Engine/RHI/FramePacing.hpp` | 10 |
| `FramePacingState` | RHI | `Source/Engine/RHI/FramePacing.hpp` | 17 |
| `FramePacket` | RHI | `Source/Engine/RHI/RenderItem.hpp` | 387 |
| `FrameSink` | RHI | `Source/Engine/RHI/FrameExporter.hpp` | 85 |
| `FrameStats` | RHI | `Source/Engine/RHI/RHI.hpp` | 277 |
| `FrameTimestampIndex` | RHI | `Source/Engine/RHI/Vulkan/VulkanRHIInternal.hpp` | 76 |
| `Fsr2Adapter` | RHI | `Source/Engine/RHI/Fsr2Adapter.hpp` | 86 |
| `Fsr2Pass` | RHI | `Source/Engine/RHI/Fsr2Pass.hpp` | 10 |
| `Fsr2QualityMode` | RHI | `Source/Engine/RHI/Fsr2Adapter.hpp` | 10 |
| `Fsr3FrameGenerator` | RHI | `Source/Engine/RHI/Fsr3FrameGenerator.hpp` | 10 |
| `GiFrame` | RHI | `Source/Engine/RHI/Vulkan/VulkanRHIInternal.hpp` | 960 |
| `GpuCullingConstants` | RHI | `Source/Engine/RHI/GpuVisibility.hpp` | 28 |
| `GpuGiProbe` | RHI | `Source/Engine/RHI/GpuLighting.hpp` | 67 |
| `GpuGiVolumeHeader` | RHI | `Source/Engine/RHI/GpuLighting.hpp` | 57 |
| `GpuLight` | RHI | `Source/Engine/RHI/GpuLighting.hpp` | 18 |
| `GpuVendor` | RHI | `Source/Engine/RHI/UpscalerCapabilities.hpp` | 19 |
| `HashEntry` | RHI | `Source/Engine/RHI/VirtualTextureSystem.hpp` | 218 |
| `HiZPushConstants` | RHI | `Source/Engine/RHI/Vulkan/VulkanRHIInternal.hpp` | 66 |
| `IFrameGenerator` | RHI | `Source/Engine/RHI/FrameGeneration.hpp` | 27 |
| `ImageLayout` | RHI | `Source/Engine/RHI/RHI.hpp` | 238 |
| `ITemporalUpscaler` | RHI | `Source/Engine/RHI/TemporalUpscaler.hpp` | 28 |
| `JobSystem` | RHI | `Source/Engine/RHI/FrameComposer.hpp` | 24 |
| `KeySorter` | RHI | `Source/Engine/RHI/Sort.hpp` | 14 |
| `LoadRequest` | RHI | `Source/Engine/RHI/VirtualTextureSystem.hpp` | 241 |
| `LoadResult` | RHI | `Source/Engine/RHI/VirtualTextureSystem.hpp` | 248 |
| `LookupEntry` | RHI | `Source/Engine/RHI/ChangeMaskExtract.hpp` | 261 |
| `MaterialAssetError` | RHI | `Source/Engine/RHI/MaterialAsset.hpp` | 42 |
| `MaterialCompileOptions` | RHI | `Source/Engine/RHI/MaterialGraphCompiler.hpp` | 27 |
| `MaterialContract` | RHI | `Source/Engine/RHI/MaterialAsset.hpp` | 83 |
| `MaterialDomain` | RHI | `Source/Engine/RHI/MaterialGraphCompiler.hpp` | 21 |
| `MaterialGraph` | RHI | `Source/Engine/RHI/MaterialGraph.hpp` | 555 |
| `MaterialGraphCompiler` | RHI | `Source/Engine/RHI/MaterialGraphCompiler.hpp` | 58 |
| `MaterialGraphRequirements` | RHI | `Source/Engine/RHI/MaterialGraph.hpp` | 542 |
| `MaterialNode` | RHI | `Source/Engine/RHI/MaterialGraph.hpp` | 117 |
| `MaterialNodeFlags` | RHI | `Source/Engine/RHI/MaterialGraph.hpp` | 111 |
| `MaterialNodeKind` | RHI | `Source/Engine/RHI/MaterialGraph.hpp` | 29 |
| `MaterialParam` | RHI | `Source/Engine/RHI/MaterialGraph.hpp` | 99 |
| `MaterialParamKind` | RHI | `Source/Engine/RHI/MaterialGraph.hpp` | 88 |
| `MaterialPin` | RHI | `Source/Engine/RHI/MaterialGraph.hpp` | 129 |
| `MaterialPipeline` | RHI | `Source/Engine/RHI/Vulkan/VulkanRHIInternal.hpp` | 239 |
| `MaterialPipelineCache` | RHI | `Source/Engine/RHI/MaterialPipelineCache.hpp` | 38 |
| `MaterialPipelineCacheStats` | RHI | `Source/Engine/RHI/MaterialPipelineCache.hpp` | 18 |
| `MaterialShaderProgram` | RHI | `Source/Engine/RHI/MaterialGraphCompiler.hpp` | 39 |
| `MaterialValueType` | RHI | `Source/Engine/RHI/MaterialGraph.hpp` | 78 |
| `MeshArenaRange` | RHI | `Source/Engine/RHI/Vulkan/VulkanRHIInternal.hpp` | 207 |
| `MeshComputePush` | RHI | `Source/Engine/RHI/Vulkan/VulkanRHIInternal.hpp` | 132 |
| `MeshDesc` | RHI | `Source/Engine/RHI/Mesh.hpp` | 102 |
| `MeshDrawData` | RHI | `Source/Engine/RHI/Mesh.hpp` | 70 |
| `MeshDrawFlags` | RHI | `Source/Engine/RHI/Mesh.hpp` | 54 |
| `MeshDrawRecord` | RHI | `Source/Engine/RHI/Vulkan/VulkanRHIInternal.hpp` | 183 |
| `MeshFrameBuffers` | RHI | `Source/Engine/RHI/Vulkan/VulkanRHIInternal.hpp` | 150 |
| `MeshGraphicsPush` | RHI | `Source/Engine/RHI/Vulkan/VulkanRHIInternal.hpp` | 122 |
| `MeshHandle` | RHI | `Source/Engine/RHI/Mesh.hpp` | 87 |
| `MeshIndirectGroup` | RHI | `Source/Engine/RHI/Vulkan/VulkanRHIInternal.hpp` | 198 |
| `MeshItemFlags` | RHI | `Source/Engine/RHI/Mesh.hpp` | 62 |
| `MeshRenderItem` | RHI | `Source/Engine/RHI/Mesh.hpp` | 114 |
| `MeshSkinRange` | RHI | `Source/Engine/RHI/Mesh.hpp` | 46 |
| `MeshVertex` | RHI | `Source/Engine/RHI/Mesh.hpp` | 17 |
| `MotionHistory2D` | RHI | `Source/Engine/RHI/FrameComposer.hpp` | 33 |
| `MotionVectorItem` | RHI | `Source/Engine/RHI/RenderItem.hpp` | 369 |
| `NativeDeviceHandles` | RHI | `Source/Engine/RHI/RHI.hpp` | 726 |
| `NativeTextureView` | RHI | `Source/Engine/RHI/RHI.hpp` | 244 |
| `OitPass` | RHI | `Source/Engine/RHI/OitPass.hpp` | 11 |
| `PageSlot` | RHI | `Source/Engine/RHI/VirtualTextureSystem.hpp` | 209 |
| `ParallelChunkRef` | RHI | `Source/Engine/RHI/FrameComposer.hpp` | 301 |
| `PlatformKind` | RHI | `Source/Engine/RHI/UpscalerCapabilities.hpp` | 11 |
| `PostFxAccessibilityEffect` | RHI | `Source/Engine/RHI/PostProcess.hpp` | 33 |
| `PostFxConstants` | RHI | `Source/Engine/RHI/RenderPasses.hpp` | 97 |
| `PostFxFlags` | RHI | `Source/Engine/RHI/PostProcess.hpp` | 11 |
| `PostFxPass` | RHI | `Source/Engine/RHI/RenderPasses.hpp` | 170 |
| `PostFxSettings` | RHI | `Source/Engine/RHI/PostProcess.hpp` | 56 |
| `PreUploadedPacket` | RHI | `Source/Engine/RHI/Vulkan/VulkanRHIInternal.hpp` | 1042 |
| `PushConstants` | RHI | `Source/Engine/RHI/Vulkan/VulkanRHIInternal.hpp` | 47 |
| `RenderBlendMode` | RHI | `Source/Engine/RHI/RHI.hpp` | 190 |
| `RenderCommandRecord` | RHI | `Source/Engine/RHI/RenderGraph.hpp` | 58 |
| `RenderFlags` | RHI | `Source/Engine/RHI/RenderItem.hpp` | 43 |
| `RenderGraph` | RHI | `Source/Engine/RHI/RenderGraph.hpp` | 135 |
| `RenderGraphContext` | RHI | `Source/Engine/RHI/RenderGraph.hpp` | 80 |
| `RenderGraphPass` | RHI | `Source/Engine/RHI/RenderGraph.hpp` | 122 |
| `RenderItem` | RHI | `Source/Engine/RHI/RenderItem.hpp` | 94 |
| `RenderPassDesc` | RHI | `Source/Engine/RHI/RenderGraph.hpp` | 110 |
| `RenderPassKind` | RHI | `Source/Engine/RHI/RenderGraph.hpp` | 27 |
| `RenderResourceKind` | RHI | `Source/Engine/RHI/RenderGraph.hpp` | 34 |
| `RenderTarget` | RHI | `Source/Engine/RHI/Vulkan/VulkanRHIInternal.hpp` | 250 |
| `RenderTargetDesc` | RHI | `Source/Engine/RHI/RHI.hpp` | 207 |
| `RenderTargetFormat` | RHI | `Source/Engine/RHI/RHI.hpp` | 199 |
| `RenderTargetHandle` | RHI | `Source/Engine/RHI/RHI.hpp` | 227 |
| `RHI` | RHI | `Source/Engine/RHI/RHI.hpp` | 348 |
| `SdfCharacterRenderCache` | RHI | `Source/Engine/RHI/SdfCharacterRender.hpp` | 17 |
| `SdfField` | RHI | `Source/Engine/RHI/FrameComposer.hpp` | 22 |
| `SdfGiVolume` | RHI | `Source/Engine/RHI/FrameComposer.hpp` | 23 |
| `SdfGpuBlock` | RHI | `Source/Engine/RHI/Vulkan/VulkanRHIInternal.hpp` | 98 |
| `SdfLightingFrame` | RHI | `Source/Engine/RHI/RenderItem.hpp` | 325 |
| `SdfLightingOccluder` | RHI | `Source/Engine/RHI/RenderItem.hpp` | 317 |
| `SdfShadowConstants` | RHI | `Source/Engine/RHI/RenderPasses.hpp` | 37 |
| `SdfShadowDesc` | RHI | `Source/Engine/RHI/RenderPasses.hpp` | 18 |
| `SdfShadowPass` | RHI | `Source/Engine/RHI/RenderPasses.hpp` | 66 |
| `SequenceFooter` | RHI | `Source/Engine/RHI/FrameExporter.hpp` | 42 |
| `SequenceHeader` | RHI | `Source/Engine/RHI/FrameExporter.hpp` | 31 |
| `ShaderPipelineDesc` | RHI | `Source/Engine/RHI/RHI.hpp` | 167 |
| `ShaderPipelineDomain` | RHI | `Source/Engine/RHI/RHI.hpp` | 161 |
| `ShaderPipelineHandle` | RHI | `Source/Engine/RHI/RHI.hpp` | 178 |
| `SkinnedVertex` | RHI | `Source/Engine/RHI/Mesh.hpp` | 30 |
| `Slot` | RHI | `Source/Engine/RHI/CameraStack.hpp` | 146 |
| `SpatialUpscaleMode` | RHI | `Source/Engine/RHI/RHI.hpp` | 90 |
| `StaticBakeCache` | RHI | `Source/Engine/RHI/StaticBake.hpp` | 58 |
| `StaticBakeRenderer` | RHI | `Source/Engine/RHI/StaticBakeRenderer.hpp` | 14 |
| `StaticBakeSettings` | RHI | `Source/Engine/RHI/StaticBake.hpp` | 10 |
| `StaticBakeStats` | RHI | `Source/Engine/RHI/StaticBake.hpp` | 47 |
| `StaticBakeTileKey` | RHI | `Source/Engine/RHI/StaticBake.hpp` | 17 |
| `StaticMeshRecord` | RHI | `Source/Engine/RHI/Vulkan/VulkanRHIInternal.hpp` | 213 |
| `TemporalResolvePass` | RHI | `Source/Engine/RHI/TemporalResolvePass.hpp` | 11 |
| `TemporalUpscaleInput` | RHI | `Source/Engine/RHI/TemporalUpscaler.hpp` | 11 |
| `TextureArray` | RHI | `Source/Engine/RHI/Vulkan/VulkanRHIInternal.hpp` | 222 |
| `TextureArrayDesc` | RHI | `Source/Engine/RHI/RHI.hpp` | 146 |
| `TextureArrayHandle` | RHI | `Source/Engine/RHI/RHI.hpp` | 324 |
| `Tile` | RHI | `Source/Engine/RHI/StaticBake.hpp` | 90 |
| `TintEntry` | RHI | `Source/Engine/RHI/ViewportPresenter.hpp` | 81 |
| `TransientRing` | RHI | `Source/Engine/RHI/RingBuffer.hpp` | 15 |
| `UiBackendInfo` | RHI | `Source/Engine/RHI/RHI.hpp` | 254 |
| `UpscalerBackend` | RHI | `Source/Engine/RHI/UpscalerBackend.hpp` | 9 |
| `UpscalerCaps` | RHI | `Source/Engine/RHI/RHI.hpp` | 98 |
| `ViewportMode` | RHI | `Source/Engine/RHI/ViewportModes.hpp` | 15 |
| `ViewportModeDescriptor` | RHI | `Source/Engine/RHI/ViewportModes.hpp` | 46 |
| `ViewportModeLegend` | RHI | `Source/Engine/RHI/ViewportModes.hpp` | 37 |
| `ViewportPresenter` | RHI | `Source/Engine/RHI/ViewportPresenter.hpp` | 29 |
| `ViewportPresenterStats` | RHI | `Source/Engine/RHI/ViewportPresenter.hpp` | 17 |
| `VirtualFileSystem` | RHI | `Source/Engine/RHI/FrameExporter.hpp` | 15 |
| `VirtualFileSystem` | RHI | `Source/Engine/RHI/VirtualTextureSystem.hpp` | 22 |
| `VirtualTextureAddress` | RHI | `Source/Engine/RHI/VirtualTextureSystem.hpp` | 74 |
| `VirtualTextureCamera` | RHI | `Source/Engine/RHI/VirtualTextureSystem.hpp` | 65 |
| `VirtualTextureDesc` | RHI | `Source/Engine/RHI/VirtualTextureSystem.hpp` | 39 |
| `VirtualTextureHandle` | RHI | `Source/Engine/RHI/RenderItem.hpp` | 294 |
| `VirtualTexturePageLocation` | RHI | `Source/Engine/RHI/VirtualTextureGpu.hpp` | 11 |
| `VirtualTextureResource` | RHI | `Source/Engine/RHI/Vulkan/VulkanRHIInternal.hpp` | 424 |
| `VirtualTextureResourceDesc` | RHI | `Source/Engine/RHI/RHI.hpp` | 336 |
| `VirtualTextureRhiTarget` | RHI | `Source/Engine/RHI/VirtualTextureGpu.hpp` | 118 |
| `VirtualTextureSample` | RHI | `Source/Engine/RHI/VirtualTextureGpu.hpp` | 58 |
| `VirtualTextureStats` | RHI | `Source/Engine/RHI/VirtualTextureSystem.hpp` | 51 |
| `VirtualTextureSystem` | RHI | `Source/Engine/RHI/VirtualTextureSystem.hpp` | 117 |
| `VirtualTextureTableUpdate` | RHI | `Source/Engine/RHI/VirtualTextureSystem.hpp` | 107 |
| `VirtualTextureTileId` | RHI | `Source/Engine/RHI/VirtualTextureSystem.hpp` | 98 |
| `VirtualTextureTileUpload` | RHI | `Source/Engine/RHI/VirtualTextureSystem.hpp` | 83 |
| `VisibleSprite` | RHI | `Source/Engine/RHI/ViewportPresenter.hpp` | 87 |
| `VulkanRHI` | RHI | `Source/Engine/RHI/Vulkan/VulkanRHIInternal.hpp` | 440 |

### Engine/Script (81)

| Символ | Вид | Заголовок | Строка |
|---|---|---|---|
| `ActiveScript` | Script | `Source/Engine/Script/ScriptSystem.hpp` | 195 |
| `AotBuilder` | Script | `Source/Engine/Script/AotBuilder.hpp` | 71 |
| `AotBuildOptions` | Script | `Source/Engine/Script/AotBuilder.hpp` | 43 |
| `AotBuildResult` | Script | `Source/Engine/Script/AotBuilder.hpp` | 60 |
| `AotBuildState` | Script | `Source/Engine/Script/AotBuilder.hpp` | 19 |
| `AotGraphTranspileOptions` | Script | `Source/Engine/Script/AotGraphCompiler.hpp` | 13 |
| `AotHost` | Script | `Source/Engine/Script/AotHost.hpp` | 32 |
| `AotHost` | Script | `Source/Engine/Script/ScriptSystem.hpp` | 25 |
| `AotModule` | Script | `Source/Engine/Script/AotModule.hpp` | 22 |
| `AotProgramRef` | Script | `Source/Engine/Script/AotHost.hpp` | 22 |
| `AotTranspileOptions` | Script | `Source/Engine/Script/AotTranspiler.hpp` | 12 |
| `AudioSystem` | Script | `Source/Engine/Script/Vm.hpp` | 21 |
| `BoundProgram` | Script | `Source/Engine/Script/EcsBridge.hpp` | 101 |
| `CameraStackSystem` | Script | `Source/Engine/Script/ScriptSystem.hpp` | 20 |
| `CameraStackSystem` | Script | `Source/Engine/Script/Vm.hpp` | 20 |
| `Code` | Script | `Source/Engine/Script/Vm.hpp` | 35 |
| `CompiledDebug` | Script | `Source/Engine/Script/Bytecode.hpp` | 559 |
| `CompiledProgram` | Script | `Source/Engine/Script/Bytecode.hpp` | 574 |
| `ComponentAccessor` | Script | `Source/Engine/Script/EcsBridge.hpp` | 17 |
| `ComponentBinding` | Script | `Source/Engine/Script/Bytecode.hpp` | 501 |
| `Context` | Script | `Source/Engine/Script/AotAbi.hpp` | 177 |
| `CoroutineFrame` | Script | `Source/Engine/Script/AotAbi.hpp` | 158 |
| `CoroutineSlot` | Script | `Source/Engine/Script/AotHost.hpp` | 115 |
| `CoroutineSlot` | Script | `Source/Engine/Script/ScriptSystem.hpp` | 207 |
| `CoroutineState` | Script | `Source/Engine/Script/AotAbi.hpp` | 166 |
| `DebugField` | Script | `Source/Engine/Script/ScriptDebug.hpp` | 123 |
| `DebugFrame` | Script | `Source/Engine/Script/ScriptDebug.hpp` | 103 |
| `DebugSectionHeader` | Script | `Source/Engine/Script/Bytecode.hpp` | 215 |
| `DebugSpan` | Script | `Source/Engine/Script/Bytecode.hpp` | 193 |
| `DebugSpanFlags` | Script | `Source/Engine/Script/Bytecode.hpp` | 184 |
| `DebugVariable` | Script | `Source/Engine/Script/Bytecode.hpp` | 206 |
| `EcsBridge` | Script | `Source/Engine/Script/EcsBridge.hpp` | 26 |
| `FieldBinding` | Script | `Source/Engine/Script/Bytecode.hpp` | 511 |
| `FunctionEntry` | Script | `Source/Engine/Script/Bytecode.hpp` | 524 |
| `HostApi` | Script | `Source/Engine/Script/AotAbi.hpp` | 78 |
| `Instruction` | Script | `Source/Engine/Script/Bytecode.hpp` | 481 |
| `LogicCompareOp` | Script | `Source/Engine/Script/GraphCompiler.hpp` | 22 |
| `LogicCompileOptions` | Script | `Source/Engine/Script/GraphCompiler.hpp` | 775 |
| `LogicCompileResult` | Script | `Source/Engine/Script/GraphCompiler.hpp` | 782 |
| `LogicFieldBinding` | Script | `Source/Engine/Script/GraphCompiler.hpp` | 90 |
| `LogicGraph` | Script | `Source/Engine/Script/GraphCompiler.hpp` | 362 |
| `LogicLink` | Script | `Source/Engine/Script/GraphCompiler.hpp` | 351 |
| `LogicMathOp` | Script | `Source/Engine/Script/GraphCompiler.hpp` | 56 |
| `LogicNode` | Script | `Source/Engine/Script/GraphCompiler.hpp` | 333 |
| `LogicNodeKind` | Script | `Source/Engine/Script/GraphCompiler.hpp` | 111 |
| `MapEntry` | Script | `Source/Engine/Script/ScriptHeap.hpp` | 137 |
| `Module` | Script | `Source/Engine/Script/AotAbi.hpp` | 526 |
| `NativeId` | Script | `Source/Engine/Script/Bytecode.hpp` | 18 |
| `Object` | Script | `Source/Engine/Script/ScriptHeap.hpp` | 115 |
| `ObjectKind` | Script | `Source/Engine/Script/ScriptHeap.hpp` | 108 |
| `OpCode` | Script | `Source/Engine/Script/Bytecode.hpp` | 279 |
| `PendingField` | Script | `Source/Engine/Script/Bytecode.hpp` | 678 |
| `PhysicsSystem` | Script | `Source/Engine/Script/ScriptSystem.hpp` | 19 |
| `PhysicsSystem` | Script | `Source/Engine/Script/Vm.hpp` | 19 |
| `PinType` | Script | `Source/Engine/Script/GraphCompiler.hpp` | 318 |
| `ProgramBuilder` | Script | `Source/Engine/Script/Bytecode.hpp` | 633 |
| `ProgramHeader` | Script | `Source/Engine/Script/Bytecode.hpp` | 534 |
| `ResolvedComponent` | Script | `Source/Engine/Script/EcsBridge.hpp` | 93 |
| `ScriptComponent` | Script | `Source/Engine/Script/ScriptComponent.hpp` | 16 |
| `ScriptEvent` | Script | `Source/Engine/Script/Bytecode.hpp` | 249 |
| `ScriptHeap` | Script | `Source/Engine/Script/ScriptHeap.hpp` | 60 |
| `ScriptRuntime` | Script | `Source/Engine/Script/ScriptSystem.hpp` | 24 |
| `ScriptRuntime` | Script | `Source/Engine/Script/ScriptSystem.hpp` | 28 |
| `ScriptValue` | Script | `Source/Engine/Script/ScriptHeap.hpp` | 26 |
| `ScriptVm` | Script | `Source/Engine/Script/Vm.hpp` | 153 |
| `ScriptVm` | Script | `Source/Engine/Script/Vm.hpp` | 305 |
| `Slot` | Script | `Source/Engine/Script/AotHost.hpp` | 103 |
| `Slot` | Script | `Source/Engine/Script/ScriptSystem.hpp` | 173 |
| `SourceLocation` | Script | `Source/Engine/Script/ScriptDebug.hpp` | 17 |
| `TextCompileOptions` | Script | `Source/Engine/Script/TextScript.hpp` | 14 |
| `TransportGroupSystem` | Script | `Source/Engine/Script/Vm.hpp` | 18 |
| `ValueTag` | Script | `Source/Engine/Script/ScriptHeap.hpp` | 15 |
| `Vector2D` | Script | `Source/Engine/Script/AotAbi.hpp` | 52 |
| `VmCallFrame` | Script | `Source/Engine/Script/Vm.hpp` | 193 |
| `VmContext` | Script | `Source/Engine/Script/Vm.hpp` | 124 |
| `VmDebugState` | Script | `Source/Engine/Script/Vm.hpp` | 204 |
| `VmFault` | Script | `Source/Engine/Script/Vm.hpp` | 33 |
| `VmHalt` | Script | `Source/Engine/Script/Vm.hpp` | 165 |
| `VmPauseReason` | Script | `Source/Engine/Script/Vm.hpp` | 156 |
| `VmStopInfo` | Script | `Source/Engine/Script/Vm.hpp` | 173 |
| `VmSuspendState` | Script | `Source/Engine/Script/Vm.hpp` | 292 |

### Engine/Text (84)

| Символ | Вид | Заголовок | Строка |
|---|---|---|---|
| `AssetSlot` | Text | `Source/Engine/Text/TextStore.hpp` | 218 |
| `BookAnchor` | Text | `Source/Engine/Text/BookAsset.hpp` | 107 |
| `BookAssetError` | Text | `Source/Engine/Text/BookAsset.hpp` | 140 |
| `BookAssetView` | Text | `Source/Engine/Text/BookAsset.hpp` | 127 |
| `BookBlockRecord` | Text | `Source/Engine/Text/BookAsset.hpp` | 60 |
| `BookBlockType` | Text | `Source/Engine/Text/BookAsset.hpp` | 22 |
| `BookBlockView` | Text | `Source/Engine/Text/BookAsset.hpp` | 117 |
| `BookCookError` | Text | `Source/Engine/Text/BookCook.hpp` | 13 |
| `BookHeader` | Text | `Source/Engine/Text/BookAsset.hpp` | 35 |
| `BookInline` | Text | `Source/Engine/Text/BookAsset.hpp` | 97 |
| `BookPage` | Text | `Source/Engine/Text/BookSession.hpp` | 25 |
| `BookPageSegment` | Text | `Source/Engine/Text/BookSession.hpp` | 35 |
| `BookReader` | Text | `Source/Engine/Text/BookAsset.hpp` | 159 |
| `BookSession` | Text | `Source/Engine/Text/BookSession.hpp` | 46 |
| `BookSpan` | Text | `Source/Engine/Text/BookAsset.hpp` | 77 |
| `BookStyle` | Text | `Source/Engine/Text/BookAsset.hpp` | 86 |
| `BookViewport` | Text | `Source/Engine/Text/BookSession.hpp` | 19 |
| `ChainedShaper` | Text | `Source/Engine/Text/TextShaper.hpp` | 133 |
| `CodepointShaper` | Text | `Source/Engine/Text/TextShaper.hpp` | 99 |
| `Entry` | Text | `Source/Engine/Text/TextShaper.hpp` | 247 |
| `FontCandidate` | Text | `Source/Engine/Text/TextFontStack.hpp` | 24 |
| `FontSlot` | Text | `Source/Engine/Text/TextFontStack.hpp` | 59 |
| `FormatEntry` | Text | `Source/Engine/Text/TextService.hpp` | 583 |
| `FormattedLayoutEntry` | Text | `Source/Engine/Text/TextService.hpp` | 566 |
| `FormattedLayoutKey` | Text | `Source/Engine/Text/TextService.hpp` | 556 |
| `HarfBuzzShaper` | Text | `Source/Engine/Text/TextShaper.hpp` | 110 |
| `Impl` | Text | `Source/Engine/Text/TextShaper.hpp` | 128 |
| `ITextShaper` | Text | `Source/Engine/Text/TextShaper.hpp` | 89 |
| `LayoutEntry` | Text | `Source/Engine/Text/TextService.hpp` | 548 |
| `LayoutKey` | Text | `Source/Engine/Text/TextService.hpp` | 528 |
| `MappedTextAsset` | Text | `Source/Engine/Text/TextAssetFile.hpp` | 17 |
| `MarkupStackEntry` | Text | `Source/Engine/Text/TextMarkup.hpp` | 71 |
| `MarkupVariantEntry` | Text | `Source/Engine/Text/TextService.hpp` | 574 |
| `ScriptBackend` | Text | `Source/Engine/Text/TextShapeSystem.hpp` | 124 |
| `ScriptShaper` | Text | `Source/Engine/Text/TextShaper.hpp` | 207 |
| `ShapedGlyph` | Text | `Source/Engine/Text/TextShaper.hpp` | 56 |
| `ShapedGlyphHit` | Text | `Source/Engine/Text/TextShapedLayout.hpp` | 153 |
| `ShapedLayoutResult` | Text | `Source/Engine/Text/TextShapedLayout.hpp` | 26 |
| `ShapedLine` | Text | `Source/Engine/Text/TextShapedLayout.hpp` | 18 |
| `StyledAsset` | Text | `Source/Engine/Text/TextService.hpp` | 595 |
| `TextAction` | Text | `Source/Engine/Text/TextStyle.hpp` | 61 |
| `TextActionKind` | Text | `Source/Engine/Text/TextStyle.hpp` | 51 |
| `TextAnimationParams` | Text | `Source/Engine/Text/TextRender.hpp` | 29 |
| `TextAssetBlock` | Text | `Source/Engine/Text/TextAssetFormat.hpp` | 34 |
| `TextAssetHeader` | Text | `Source/Engine/Text/TextAssetFormat.hpp` | 20 |
| `TextAssetRef` | Text | `Source/Engine/Text/TextAsset.hpp` | 14 |
| `TextAssetStatus` | Text | `Source/Engine/Text/TextAssetFormat.hpp` | 63 |
| `TextAssetView` | Text | `Source/Engine/Text/TextAssetFormat.hpp` | 42 |
| `TextBlock` | Text | `Source/Engine/Text/TextAsset.hpp` | 37 |
| `TextDirection` | Text | `Source/Engine/Text/TextShaper.hpp` | 21 |
| `TextFontStack` | Text | `Source/Engine/Text/TextFontStack.hpp` | 54 |
| `TextFormatArg` | Text | `Source/Engine/Text/TextFormat.hpp` | 45 |
| `TextFormatStatus` | Text | `Source/Engine/Text/TextFormat.hpp` | 33 |
| `TextGradientParams` | Text | `Source/Engine/Text/TextRender.hpp` | 22 |
| `TextInlineHit` | Text | `Source/Engine/Text/TextLayout.hpp` | 204 |
| `TextInlineKind` | Text | `Source/Engine/Text/TextStyle.hpp` | 73 |
| `TextInlineObject` | Text | `Source/Engine/Text/TextStyle.hpp` | 83 |
| `TextInlineSprite` | Text | `Source/Engine/Text/TextStyle.hpp` | 99 |
| `TextLayoutParams` | Text | `Source/Engine/Text/TextLayout.hpp` | 54 |
| `TextLayoutResult` | Text | `Source/Engine/Text/TextLayout.hpp` | 72 |
| `TextLine` | Text | `Source/Engine/Text/TextLayout.hpp` | 22 |
| `TextMarkupStatus` | Text | `Source/Engine/Text/TextMarkup.hpp` | 16 |
| `TextRenderEffects` | Text | `Source/Engine/Text/TextRender.hpp` | 44 |
| `TextRenderParams` | Text | `Source/Engine/Text/TextRender.hpp` | 57 |
| `TextRenderStats` | Text | `Source/Engine/Text/TextRender.hpp` | 76 |
| `TextRenderSystemStats` | Text | `Source/Engine/Text/TextRenderSystem.hpp` | 19 |
| `TextRun` | Text | `Source/Engine/Text/TextShaper.hpp` | 47 |
| `TextRuntime` | Text | `Source/Engine/Text/TextRuntime.hpp` | 17 |
| `TextScript` | Text | `Source/Engine/Text/TextShaper.hpp` | 30 |
| `TextService` | Text | `Source/Engine/Text/TextService.hpp` | 18 |
| `TextShapeCache` | Text | `Source/Engine/Text/TextShapeCache.hpp` | 59 |
| `TextShapeHandle` | Text | `Source/Engine/Text/TextShapeCache.hpp` | 19 |
| `TextShapeKey` | Text | `Source/Engine/Text/TextShapeCache.hpp` | 32 |
| `TextShapeParams` | Text | `Source/Engine/Text/TextShaper.hpp` | 71 |
| `TextShapeResult` | Text | `Source/Engine/Text/TextShaper.hpp` | 82 |
| `TextShapeSystemStats` | Text | `Source/Engine/Text/TextShapeSystem.hpp` | 18 |
| `TextShapeView` | Text | `Source/Engine/Text/TextShapeCache.hpp` | 50 |
| `TextSpan` | Text | `Source/Engine/Text/TextStyle.hpp` | 40 |
| `TextStore` | Text | `Source/Engine/Text/TextStore.hpp` | 19 |
| `TextStyle` | Text | `Source/Engine/Text/TextStyle.hpp` | 27 |
| `TextStyledBlock` | Text | `Source/Engine/Text/TextMarkup.hpp` | 30 |
| `TextStyleFlags` | Text | `Source/Engine/Text/TextStyle.hpp` | 12 |
| `UnicodeBoundary` | Text | `Source/Engine/Text/UnicodeBreak.hpp` | 13 |
| `VerticalFormEntry` | Text | `Source/Engine/Text/TextVertical.hpp` | 16 |

### Engine/UI (115)

| Символ | Вид | Заголовок | Строка |
|---|---|---|---|
| `AccessibilityEditorSettings` | UI | `Source/Engine/UI/Accessibility.hpp` | 251 |
| `AccessibilityEffectKind` | UI | `Source/Engine/UI/AccessibilityEffects.hpp` | 26 |
| `AccessibilityEffectProfile` | UI | `Source/Engine/UI/AccessibilityEffects.hpp` | 84 |
| `AccessibilityEffectRegistry` | UI | `Source/Engine/UI/AccessibilityEffects.hpp` | 122 |
| `AccessibilityEffectTag` | UI | `Source/Engine/UI/AccessibilityEffects.hpp` | 58 |
| `AccessibilityExtensionRegistry` | UI | `Source/Engine/UI/AccessibilityEffects.hpp` | 103 |
| `AccessibilityFlags` | UI | `Source/Engine/UI/Accessibility.hpp` | 209 |
| `AccessibilityKernel` | UI | `Source/Engine/UI/Accessibility.hpp` | 305 |
| `AccessibilityOverlay` | UI | `Source/Engine/UI/AccessibilityOverlay.hpp` | 30 |
| `AccessibilityPaletteExtension` | UI | `Source/Engine/UI/AccessibilityEffects.hpp` | 42 |
| `AccessibilityPresentationSettings` | UI | `Source/Engine/UI/Accessibility.hpp` | 234 |
| `AccessibilityProfile` | UI | `Source/Engine/UI/Accessibility.hpp` | 273 |
| `AccessibilitySettings` | UI | `Source/Engine/UI/Accessibility.hpp` | 218 |
| `AccessibilitySlot` | UI | `Source/Engine/UI/AccessibilityTheme.hpp` | 61 |
| `AccessibilityTemperatureBand` | UI | `Source/Engine/UI/AccessibilityEffects.hpp` | 49 |
| `AccessibilityTextStyle` | UI | `Source/Engine/UI/AccessibilityTheme.hpp` | 82 |
| `AccessibilityTheme` | UI | `Source/Engine/UI/AccessibilityTheme.hpp` | 92 |
| `AccessibilityThemeRegistry` | UI | `Source/Engine/UI/AccessibilityTheme.hpp` | 116 |
| `AlcoholEffect` | UI | `Source/Engine/UI/AccessibilityEffects.hpp` | 63 |
| `AlcoholEffectLayer` | UI | `Source/Engine/UI/AccessibilityEffects.hpp` | 77 |
| `AudioSystem` | UI | `Source/Engine/UI/Accessibility.hpp` | 27 |
| `AvSpeechTts` | UI | `Source/Engine/UI/Tts.hpp` | 60 |
| `BuildStatus` | UI | `Source/Engine/UI/PinkUI.hpp` | 1446 |
| `CardBoardComponent` | UI | `Source/Engine/UI/CardBoard.hpp` | 49 |
| `CardBoardSystem` | UI | `Source/Engine/UI/CardBoard.hpp` | 74 |
| `ColorMatrix3` | UI | `Source/Engine/UI/Accessibility.hpp` | 43 |
| `ColorVisionDeficiency` | UI | `Source/Engine/UI/Accessibility.hpp` | 35 |
| `Dropdown` | UI | `Source/Engine/UI/PinkUI.hpp` | 535 |
| `FlashGuard` | UI | `Source/Engine/UI/Accessibility.hpp` | 60 |
| `FocusDirection` | UI | `Source/Engine/UI/PinkUI.hpp` | 212 |
| `FocusPolicy` | UI | `Source/Engine/UI/PinkUI.hpp` | 188 |
| `FontAtlas` | UI | `Source/Engine/UI/FontAtlas.hpp` | 101 |
| `FontAtlasHeader` | UI | `Source/Engine/UI/FontAtlas.hpp` | 45 |
| `FontAtlasSet` | UI | `Source/Engine/UI/FontAtlas.hpp` | 167 |
| `FontBuildOptions` | UI | `Source/Engine/UI/FontAtlas.hpp` | 25 |
| `FontBuildOptions` | UI | `Source/Engine/UI/FontBuilder.hpp` | 15 |
| `FontCoverage` | UI | `Source/Engine/UI/FontCoverage.hpp` | 21 |
| `FontFamilyFile` | UI | `Source/Engine/UI/FontAtlas.hpp` | 74 |
| `FontFamilyManifest` | UI | `Source/Engine/UI/FontFamilyAsset.hpp` | 17 |
| `FontPixelFormat` | UI | `Source/Engine/UI/FontAtlas.hpp` | 37 |
| `FontStyle` | UI | `Source/Engine/UI/FontAtlas.hpp` | 65 |
| `FrameOutput` | UI | `Source/Engine/UI/PinkUI.hpp` | 1421 |
| `GlyphRecord` | UI | `Source/Engine/UI/FontAtlas.hpp` | 80 |
| `GridView` | UI | `Source/Engine/UI/PinkUI.hpp` | 425 |
| `HyperlinkRange` | UI | `Source/Engine/UI/Hypertext.hpp` | 51 |
| `HypertextComponent` | UI | `Source/Engine/UI/Hypertext.hpp` | 32 |
| `HypertextFlags` | UI | `Source/Engine/UI/Hypertext.hpp` | 21 |
| `HypertextPool` | UI | `Source/Engine/UI/Hypertext.hpp` | 74 |
| `HypertextView` | UI | `Source/Engine/UI/Hypertext.hpp` | 67 |
| `ITextShaper` | UI | `Source/Engine/UI/PinkUI.hpp` | 34 |
| `LayoutCache` | UI | `Source/Engine/UI/PinkUI.hpp` | 741 |
| `LazyFontFamily` | UI | `Source/Engine/UI/FontFamilyAsset.hpp` | 101 |
| `ListView` | UI | `Source/Engine/UI/PinkUI.hpp` | 561 |
| `LocalizationTable` | UI | `Source/Engine/UI/Accessibility.hpp` | 25 |
| `LocalizationTable` | UI | `Source/Engine/UI/Hypertext.hpp` | 16 |
| `LocalizationTable` | UI | `Source/Engine/UI/PinkUI.hpp` | 40 |
| `NarrativeArchetype` | UI | `Source/Engine/UI/NarrativeTree.hpp` | 23 |
| `NarrativeEvent` | UI | `Source/Engine/UI/Accessibility.hpp` | 84 |
| `NarrativeGroup` | UI | `Source/Engine/UI/NarrativeTree.hpp` | 37 |
| `NarrativeLocale` | UI | `Source/Engine/UI/NarrativeTree.hpp` | 17 |
| `NarrativeLog` | UI | `Source/Engine/UI/Accessibility.hpp` | 100 |
| `NarrativeNode` | UI | `Source/Engine/UI/NarrativeTree.hpp` | 47 |
| `NarrativeTree` | UI | `Source/Engine/UI/NarrativeTree.hpp` | 55 |
| `NvgPattern` | UI | `Source/Engine/UI/AccessibilityEffects.hpp` | 33 |
| `OledShiftState` | UI | `Source/Engine/UI/PinkUI.hpp` | 464 |
| `PinkSdfDraw` | UI | `Source/Engine/UI/PinkSdf.hpp` | 74 |
| `PinkSdfFlags` | UI | `Source/Engine/UI/PinkSdf.hpp` | 24 |
| `PinkSdfGpuPrimitive` | UI | `Source/Engine/UI/PinkSdf.hpp` | 51 |
| `PinkSdfPrimitive` | UI | `Source/Engine/UI/PinkSdf.hpp` | 33 |
| `PinkSdfShape` | UI | `Source/Engine/UI/PinkSdf.hpp` | 15 |
| `RadialProgress` | UI | `Source/Engine/UI/PinkUI.hpp` | 585 |
| `RetainedFont` | UI | `Source/Engine/UI/FontAtlas.hpp` | 293 |
| `SapiTts` | UI | `Source/Engine/UI/Tts.hpp` | 30 |
| `ScrollView` | UI | `Source/Engine/UI/PinkUI.hpp` | 492 |
| `SlotComponent` | UI | `Source/Engine/UI/CardBoard.hpp` | 27 |
| `SlotFlags` | UI | `Source/Engine/UI/CardBoard.hpp` | 15 |
| `SlotLayout` | UI | `Source/Engine/UI/CardBoard.hpp` | 65 |
| `SoundCueCategory` | UI | `Source/Engine/UI/SoundCues.hpp` | 26 |
| `SoundCueDef` | UI | `Source/Engine/UI/SoundCues.hpp` | 62 |
| `SoundCueRegistry` | UI | `Source/Engine/UI/SoundCues.hpp` | 71 |
| `SoundTrigger` | UI | `Source/Engine/UI/Accessibility.hpp` | 129 |
| `StateTrack` | UI | `Source/Engine/UI/Accessibility.hpp` | 424 |
| `SubtitleLine` | UI | `Source/Engine/UI/Accessibility.hpp` | 140 |
| `SubtitleQueue` | UI | `Source/Engine/UI/Accessibility.hpp` | 164 |
| `SystemContext` | UI | `Source/Engine/UI/PinkUI.hpp` | 29 |
| `TabBar` | UI | `Source/Engine/UI/PinkUI.hpp` | 516 |
| `TextAlign` | UI | `Source/Engine/UI/PinkUI.hpp` | 223 |
| `TextShapeParams` | UI | `Source/Engine/UI/PinkUI.hpp` | 35 |
| `TextVerticalAlign` | UI | `Source/Engine/UI/PinkUI.hpp` | 230 |
| `TimedSoundCue` | UI | `Source/Engine/UI/Accessibility.hpp` | 153 |
| `TooltipPlacer` | UI | `Source/Engine/UI/PinkUI.hpp` | 596 |
| `TtsBackend` | UI | `Source/Engine/UI/Accessibility.hpp` | 28 |
| `TtsBackend` | UI | `Source/Engine/UI/Tts.hpp` | 19 |
| `TtsPriority` | UI | `Source/Engine/UI/Tts.hpp` | 11 |
| `UiPalette` | UI | `Source/Engine/UI/UiTheme.hpp` | 41 |
| `UiPaletteSlot` | UI | `Source/Engine/UI/UiTheme.hpp` | 18 |
| `UiThemeAsset` | UI | `Source/Engine/UI/UiTheme.hpp` | 46 |
| `VirtualFileSystem` | UI | `Source/Engine/UI/FontAtlas.hpp` | 19 |
| `VoiceCaptionEvent` | UI | `Source/Engine/UI/Accessibility.hpp` | 194 |
| `VoiceCaptionProvider` | UI | `Source/Engine/UI/Accessibility.hpp` | 202 |
| `WidgetCanvas` | UI | `Source/Engine/UI/Accessibility.hpp` | 31 |
| `WidgetCanvas` | UI | `Source/Engine/UI/PinkUI.hpp` | 291 |
| `WidgetCanvas` | UI | `Source/Engine/UI/PinkUI.hpp` | 484 |
| `WidgetCanvas` | UI | `Source/Engine/UI/PinkUI.hpp` | 608 |
| `WidgetCanvasSystem` | UI | `Source/Engine/UI/PinkUI.hpp` | 1459 |
| `WidgetComponent` | UI | `Source/Engine/UI/PinkUI.hpp` | 252 |
| `WidgetComponentFlags` | UI | `Source/Engine/UI/PinkUI.hpp` | 264 |
| `WidgetDesc` | UI | `Source/Engine/UI/PinkUI.hpp` | 310 |
| `WidgetFlags` | UI | `Source/Engine/UI/PinkUI.hpp` | 146 |
| `WidgetHandle` | UI | `Source/Engine/UI/PinkUI.hpp` | 81 |
| `WidgetHyperlink` | UI | `Source/Engine/UI/PinkUI.hpp` | 301 |
| `WidgetKind` | UI | `Source/Engine/UI/PinkUI.hpp` | 100 |
| `WidgetLayoutAsset` | UI | `Source/Engine/UI/WidgetLayoutAsset.hpp` | 15 |
| `WidgetPaintContext` | UI | `Source/Engine/UI/PinkUI.hpp` | 292 |
| `WidgetPaintContext` | UI | `Source/Engine/UI/PinkUI.hpp` | 392 |

### Engine/Video (28)

| Символ | Вид | Заголовок | Строка |
|---|---|---|---|
| `AssetDatabase` | Video | `Source/Engine/Video/VideoStream.hpp` | 28 |
| `AudioPumpStatus` | Video | `Source/Engine/Video/VideoAudioStream.hpp` | 15 |
| `ClusterIndexEntry` | Video | `Source/Engine/Video/WebmDemuxer.hpp` | 210 |
| `ClusterScan` | Video | `Source/Engine/Video/WebmDemuxer.hpp` | 150 |
| `ContainerKind` | Video | `Source/Engine/Video/WebmDemuxer.hpp` | 225 |
| `DecodeStatus` | Video | `Source/Engine/Video/VideoTypes.hpp` | 33 |
| `DemuxStatus` | Video | `Source/Engine/Video/WebmDemuxer.hpp` | 74 |
| `Element` | Video | `Source/Engine/Video/WebmDemuxer.hpp` | 127 |
| `FrameBuffer` | Video | `Source/Engine/Video/VideoPlayer.hpp` | 160 |
| `FrameSlot` | Video | `Source/Engine/Video/VideoPlayer.hpp` | 167 |
| `LacedAudioPacket` | Video | `Source/Engine/Video/WebmDemuxer.hpp` | 201 |
| `MappedFile` | Video | `Source/Engine/Video/VideoPlayer.hpp` | 24 |
| `RHI` | Video | `Source/Engine/Video/VideoPlayer.hpp` | 29 |
| `SystemGraph` | Video | `Source/Engine/Video/VideoStream.hpp` | 24 |
| `VideoAudioDecoder` | Video | `Source/Engine/Video/VideoAudioDecoder.hpp` | 15 |
| `VideoAudioStream` | Video | `Source/Engine/Video/VideoAudioStream.hpp` | 27 |
| `VideoPlayer` | Video | `Source/Engine/Video/VideoPlayer.hpp` | 35 |
| `VideoStreamComponent` | Video | `Source/Engine/Video/VideoStream.hpp` | 36 |
| `VideoStreamFlags` | Video | `Source/Engine/Video/VideoTypes.hpp` | 20 |
| `VideoSystem` | Video | `Source/Engine/Video/VideoStream.hpp` | 56 |
| `VirtualFileSystem` | Video | `Source/Engine/Video/VideoStream.hpp` | 20 |
| `Vp9Backend` | Video | `Source/Engine/Video/Vp9Backend.hpp` | 30 |
| `WebmAudioPacket` | Video | `Source/Engine/Video/WebmDemuxer.hpp` | 68 |
| `WebmAudioTrackInfo` | Video | `Source/Engine/Video/WebmDemuxer.hpp` | 43 |
| `WebmDemuxer` | Video | `Source/Engine/Video/WebmDemuxer.hpp` | 82 |
| `WebmFrame` | Video | `Source/Engine/Video/WebmDemuxer.hpp` | 54 |
| `WebmTrackInfo` | Video | `Source/Engine/Video/WebmDemuxer.hpp` | 15 |
| `YuvFrameView` | Video | `Source/Engine/Video/Vp9Backend.hpp` | 12 |

### Engine/Weather (12)

| Символ | Вид | Заголовок | Строка |
|---|---|---|---|
| `ClothSystem` | Weather | `Source/Engine/Weather/Weather.hpp` | 13 |
| `WeatherAtmospherePass` | Weather | `Source/Engine/Weather/WeatherPreconciliated.hpp` | 128 |
| `WeatherContextComponent` | Weather | `Source/Engine/Weather/Weather.hpp` | 32 |
| `WeatherDesc` | Weather | `Source/Engine/Weather/Weather.hpp` | 91 |
| `WeatherFlags` | Weather | `Source/Engine/Weather/Weather.hpp` | 19 |
| `WeatherGpuConstants` | Weather | `Source/Engine/Weather/WeatherPreconciliated.hpp` | 46 |
| `WeatherGpuResources` | Weather | `Source/Engine/Weather/WeatherPreconciliated.hpp` | 18 |
| `WeatherGpuStats` | Weather | `Source/Engine/Weather/WeatherPreconciliated.hpp` | 68 |
| `WeatherPreconciliatedDesc` | Weather | `Source/Engine/Weather/WeatherPreconciliated.hpp` | 32 |
| `WeatherPreconciliatedSystem` | Weather | `Source/Engine/Weather/WeatherPreconciliated.hpp` | 78 |
| `WeatherSystem` | Weather | `Source/Engine/Weather/Weather.hpp` | 99 |
| `WeatherWindField` | Weather | `Source/Engine/Weather/Weather.hpp` | 75 |

## 21. Приложение B - макросы и платформенные define

| Макрос | Заголовок | Строка |
|---|---|---|
| `FBY_ALCOHOL_EFFECTS` | `Source/Engine/Core/PluginAPI.h` | 105 |
| `FBY_AOT_EXPORT` | `Source/Engine/Script/AotAbi.hpp` | 543 |
| `FBY_AOT_EXPORT` | `Source/Engine/Script/AotAbi.hpp` | 545 |
| `FBY_ASSERT` | `Source/Engine/Core/Assert.hpp` | 13 |
| `FBY_ASSERT` | `Source/Engine/Core/Assert.hpp` | 31 |
| `FBY_ASSERT_MSG` | `Source/Engine/Core/Assert.hpp` | 22 |
| `FBY_ASSERT_MSG` | `Source/Engine/Core/Assert.hpp` | 32 |
| `FBY_DEFINE_PLUGIN` | `Source/Engine/Core/Plugin.hpp` | 55 |
| `FBY_DETAIL_CONCAT` | `Source/Engine/Core/Definitions.hpp` | 263 |
| `FBY_DETAIL_CONCAT_INNER` | `Source/Engine/Core/Definitions.hpp` | 262 |
| `FBY_EFFECT_TAGS` | `Source/Engine/Core/PluginAPI.h` | 104 |
| `FBY_FIELD` | `Source/Engine/Core/Mirror.hpp` | 288 |
| `FBY_FLUID_HAS_MDSPAN` | `Source/Engine/Physics/Fluid.hpp` | 16 |
| `FBY_FLUID_HAS_MDSPAN` | `Source/Engine/Physics/Fluid.hpp` | 18 |
| `FBY_HAS_MDSPAN` | `Source/Engine/ECS/Skeleton.hpp` | 18 |
| `FBY_HAS_MDSPAN` | `Source/Engine/ECS/Skeleton.hpp` | 20 |
| `FBY_HAS_STATIC_REFLECTION` | `Source/Engine/Core/Mirror.hpp` | 105 |
| `FBY_HAS_STATIC_REFLECTION` | `Source/Engine/Core/Mirror.hpp` | 108 |
| `FBY_HAS_STATIC_REFLECTION` | `Source/Engine/Core/Mirror.hpp` | 112 |
| `FBY_HAS_STD_FLAT_MAP` | `Source/Engine/Core/FlatMap.hpp` | 19 |
| `FBY_PALETTE_EXTENSION_COLORS` | `Source/Engine/Core/PluginAPI.h` | 103 |
| `FBY_PLATFORM_ABI_VERSION` | `Source/Plugins/PlatformService/PlatformAPI.h` | 11 |
| `FBY_PLATFORM_CALL` | `Source/Plugins/PlatformService/PlatformAPI.h` | 15 |
| `FBY_PLATFORM_CALL` | `Source/Plugins/PlatformService/PlatformAPI.h` | 18 |
| `FBY_PLATFORM_EXPORT` | `Source/Plugins/PlatformService/PlatformAPI.h` | 14 |
| `FBY_PLATFORM_EXPORT` | `Source/Plugins/PlatformService/PlatformAPI.h` | 17 |
| `FBY_PLUGIN_ABI_VERSION` | `Source/Engine/Core/PluginAPI.h` | 30 |
| `FBY_PLUGIN_API_H` | `Source/Engine/Core/PluginAPI.h` | 18 |
| `FBY_PLUGIN_COMPONENT` | `Source/Engine/Core/Plugin.hpp` | 52 |
| `FBY_PLUGIN_ENTRY` | `Source/Engine/Core/PluginAPI.h` | 214 |
| `FBY_PLUGIN_EXPORT` | `Source/Engine/Core/PluginAPI.h` | 33 |
| `FBY_PLUGIN_EXPORT` | `Source/Engine/Core/PluginAPI.h` | 35 |
| `FBY_PLUGIN_EXPORT` | `Source/Engine/Core/PluginAPI.h` | 37 |
| `FBY_PLUGIN_EXPORT` | `Source/Engine/Core/PluginApi.hpp` | 11 |
| `FBY_PLUGIN_EXPORT` | `Source/Engine/Core/PluginApi.hpp` | 13 |
| `FBY_PLUGIN_INFO_SYMBOL` | `Source/Engine/Core/PluginAPI.h` | 206 |
| `FBY_PLUGIN_LOAD_SYMBOL` | `Source/Engine/Core/PluginAPI.h` | 207 |
| `FBY_PLUGIN_UNLOAD_SYMBOL` | `Source/Engine/Core/PluginAPI.h` | 208 |
| `FBY_PROJECT_ASSETS` | `Source/Engine/Assets/ProjectScaffold.hpp` | 130 |
| `FBY_PROJECT_MAX_RENDER_ITEMS` | `Source/Engine/Assets/ProjectScaffold.hpp` | 123 |
| `FBY_REFLECT_COMPONENT` | `Source/Engine/Core/Mirror.hpp` | 295 |
| `FBY_REFLECT_COMPONENT_EMPTY` | `Source/Engine/Core/Mirror.hpp` | 311 |
| `FBY_REGISTER_TYPE` | `Source/Engine/Core/Definitions.hpp` | 266 |
| `FBY_RELEVANCE_HAS_MDSPAN` | `Source/Engine/Network/RelevanceGrid.hpp` | 19 |
| `FBY_RELEVANCE_HAS_MDSPAN` | `Source/Engine/Network/RelevanceGrid.hpp` | 21 |
| `FBY_RESTRICT` | `Source/Engine/Core/Definitions.hpp` | 26 |
| `FBY_RESTRICT` | `Source/Engine/Core/Definitions.hpp` | 28 |
| `FBY_SDF_SHAPE_FIELD` | `Source/Engine/Physics/SdfPrimitives.hpp` | 172 |
| `FBY_TEMPERATURE_BANDS` | `Source/Engine/Core/PluginAPI.h` | 106 |
| `FBY_THEME_COLOR_COUNT` | `Source/Engine/Core/PluginAPI.h` | 84 |
| `FBY_TYPE_NAME` | `Source/Engine/Core/Definitions.hpp` | 282 |
| `FBY_UI_HAS_MDSPAN` | `Source/Engine/UI/PinkUI.hpp` | 22 |
| `FBY_UI_HAS_MDSPAN` | `Source/Engine/UI/PinkUI.hpp` | 24 |
| `FBY_UNREACHABLE` | `Source/Engine/Core/Assert.hpp` | 45 |
| `FBY_VERIFY` | `Source/Engine/Core/Assert.hpp` | 36 |
| `FBY_VK_DECLARE` | `Source/Engine/RHI/Vulkan/VulkanLoader.hpp` | 127 |
| `FBY_VK_DEVICE_FUNCTIONS` | `Source/Engine/RHI/Vulkan/VulkanLoader.hpp` | 36 |
| `FBY_VK_GLOBAL_FUNCTIONS` | `Source/Engine/RHI/Vulkan/VulkanLoader.hpp` | 15 |
| `FBY_VK_INSTANCE_FUNCTIONS` | `Source/Engine/RHI/Vulkan/VulkanLoader.hpp` | 20 |
| `FBY_VOICE_API_H` | `Source/Engine/Core/VoiceAPI.h` | 12 |
| `FBY_VOICE_API_VERSION` | `Source/Engine/Core/VoiceAPI.h` | 21 |
| `FBY_VOICE_FLAG_ALLOW_DOWNLINK` | `Source/Engine/Core/VoiceAPI.h` | 53 |
| `FBY_VOICE_FLAG_ALLOW_UPLINK` | `Source/Engine/Core/VoiceAPI.h` | 52 |
| `FBY_VOICE_FLAG_COSMETIC` | `Source/Engine/Core/VoiceAPI.h` | 50 |
| `FBY_VOICE_FLAG_MODERATION` | `Source/Engine/Core/VoiceAPI.h` | 51 |
| `FBY_VOICE_SLOT_COUNT` | `Source/Engine/Core/VoiceAPI.h` | 22 |
| `FORCEINLINE` | `Source/Engine/Core/Definitions.hpp` | 17 |
| `FORCEINLINE` | `Source/Engine/Core/Definitions.hpp` | 19 |
| `FORCEINLINE` | `Source/Engine/Core/Definitions.hpp` | 21 |

---

Сгенерировано Documentation/Tools/GenerateAPIReference.py. Не редактируйте этот файл вручную: измените генератор и перегенерируйте.
