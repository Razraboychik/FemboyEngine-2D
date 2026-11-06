# FemboyEngine2D

[English](README.md) | [Русский перевод](README_RU.md)

**Data-oriented 2D-движок для игр, написанный с нуля на C++26.**

[![License: MPL 2.0](https://img.shields.io/badge/License-MPL_2.0-brightgreen.svg)](LICENSE)
[![C++26](https://img.shields.io/badge/C%2B%2B-26-blue.svg)]()
[![CMake 3.28+](https://img.shields.io/badge/CMake-3.28%2B-064F8C.svg)]()
[![Platforms](https://img.shields.io/badge/platforms-Windows%20%7C%20Linux%20%7C%20macOS%20%7C%20Android-lightgrey.svg)]()
[![Status: 0.3.0-alpha.1](https://img.shields.io/badge/status-0.3.0--alpha.1-blue.svg)]()
[![Website](https://img.shields.io/badge/website-FemboyEngine2D-blue.svg)](https://razraboychik.github.io/FemboyEngine-2D/)

FemboyEngine2D — компактный, детерминированный движок для 2D-игр: собственный
RHI на Vulkan/Metal, ECS на EnTT, декларативный параллельный `SystemGraph`,
физика Box2D v3, rollback-сеть, транзакционный ассет-пайплайн, полноценный
редактор и генератор чистых проектов.

В рантайме нет `std::function` на кадровом пути, нет аллокаций на кадровом пути,
нет JSON в поставляемом рантайме и нет скрытого объектного графа. Компоненты —
тривиально копируемые данные; системы — свободные функции; кадр — плоский массив
`RenderItem`, отправляемый одним RHI-пакетом.

**Статус: 0.3.0-alpha.1.** Это текущая публичная alpha-линия. В дереве есть
рабочий отладчик FemboyScript (spans, breakpoints, stepping, locals, self-поля),
изометрические и тайловые карты с редактором, скелетный IK, retargeting и сжатие
клипов (`.fanim`), навигация по сетке, 2D-освещение, UDP-транспорт с надёжностью
и MTU-фрагментацией, живой пространственный звук, превью текстур в редакторе и
backpressure при импорте. Публичные CMake-цели и
C++-пространства имён — PascalCase (`FemboyEngine2D::Engine`, `Fby::Ecs`,
`Fby::Rhi`); legacy-алиасов нет. Текущий гейт проходит полный CTest с Clang
23.1.4 на Windows и Clang на Linux; MSVC не поддерживается как компилятор.
Style gate проходит, сгенерированный Clean Slate проект собирается и
запускается с null RHI. Текущая линия также включает гибридное SDF-разрушение,
транспортные группы для лифтов, поездов и движущихся платформ, апскейлинг
класса FSR/DLSS и генерацию кадров, а также голосовые эффекты с условиями.
Движок строго 2D: в нём нет 3D-рендерера, 3D-физики и 3D-формата сцен.

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
| RHI | Постоянные кольца кадров, 48-байтный `RenderItem`, 64-битные ключи, Vulkan indirect MDI, один compute dispatch для LBS, Metal и null backends, контракты апскейлинга класса FSR/DLSS и генерации кадров |
| ECS | EnTT registry, тривиально копируемые компоненты, четырёхфазный schedule, параллельный `SystemGraph` с семафорами на вызов, Snapshot v4 |
| Ввод | Фиксированный `InputSystem`: действия/контексты/модификаторы/триггеры, состояние по пользователям, тики и timestamps событий, hotplug и dead zone для SDL3-геймпадов; без руля |
| Геймплей | База игровых тегов в Project Settings, иерархические `GameplayTags`, реестр атрибутов, data-driven instant/duration/periodic эффекты со стаками, навыки со стоимостью/кастом/кулдауном |
| ИИ | `StateTree` → command buffer → `AIController` → восприятие, навигация и навыки; `ActionId` как данные для игровых ECS-систем, цели по тегам, grid и convex navmesh, пути raw/LOS/Chaikin/Catmull-Rom, детерминированная рандомизация маршрутов |
| Физика | Два бэкенда под одним интерфейсом: Box2D v3 для соединений, CCD и обычных твёрдых тел и data-oriented AABB-солвер для больших стратегических миров без соединений. Гравитация на уровень, поля attractor/repulsor, верёвочные/цепные/пружинные соединения, урон и разрезы, ткань Verlet, поля мелкой воды, SDF-коллизии/разрушение, транспортные группы и настраиваемые hitscan/swept/dynamic снаряды |
| Сеть | `NetRollbackSession`, кольцо снапшотов на 256 тиков, предсказание ввода, повтор позднего ввода, стабильный `NetEntityId`, фильтрация релевантности, UDP `NetTransport` с ack, ретрансмиссией и MTU-фрагментацией, версионированные `InputPacket` для `SubmitRemoteInput` |
| Ассеты | VFS с владеющим `Pin()`, транзакции `.femeta`, кукер текстур BC7/ASTC, аудио/локализационные кукеры, watcher, сжатые клипы `.fanim` |
| Дистрибуция | `FemArchiver` `.femarc`, опциональный LZMA, AES-256-GCM / ChaCha20-Poly1305, `fby_fempak secure` |
| Платформа | C-ABI `PlatformService`, провайдеры Steam/EGS, детерминированные достижения и облачные сохранения |
| Анимация | 2D-скелет SoA, LBS, blend-циклы, `AnimGraph`, таймлайн анимации, PIE seqlock hand-off, two-bone/FABRIK IK, retargeting, квантование поз и клипов |
| Изометрия | `Grid::ProjectionMode` side/top-down/iso-rect/iso-hex, преобразование клетка/мир, depth-ключи, `TilemapComponent` с culled render bridge и редактором тайлмапов |
| Навигация | A* по cost-grid на прямоугольных и гексагональных сетках, карты из тайлмапов, авторские `NavAgentSettings` отдельно от runtime-путей, бюджет repath на кадр |
| Освещение | Радиальные источники `Light2DComponent`, ambient, emissive-цели, опциональный Reinhard-тонмаппинг, экранная SDF-маска теней (compute-pass графа рендера, статический `SdfField` + динамические `Light2DOccluderComponent`), выборка на пиксель |
| Аудио | DSP-граф с шинами/ducking/reverb, зоны, лимитер, Ogg/Opus, живое пространственное усиление и панорама, захват голоса/AEC/RNNoise и uplink/downlink-эффекты с условиями |
| UI | Процедурные SDF-примитивы PinkUI, DPI-масштабирование, темы, гипертекст, набор виджетов с явным editable text input, Unicode-границы строк/слов/графем через libunibreak, опциональный Tab/arrow-фокус с кольцом, наследуемая видимость, CPU clip rects, host `TextRuntime` с HarfBuzz `TextFontStack` в приложениях, shaped line-window viewports, accessibility kernel |
| Редактор | Транзакционный `CommandStack`, канвасы ImNodes, менеджер проектов, PIE Play/Pause/Stop, Play (Multiplayer) со вторым viewport клиентского мира, отладчик скриптов, редактор тайлмапов, превью текстур и панель Transport Groups; стартует в borderless fullscreen (Windowed / Borderless / Exclusive в Editor Settings) |
| Скрипты | LogicGraph в bytecode VM и LogicGraph в C++26 AOT-плагин, debug info (spans, locals), breakpoints и stepping в VM; `Compile + Nativize` и AOT manifest cook gate, чтобы сборка запускала только нативные модули (`FBY_WITH_SCRIPT_VM=OFF`) |

---

## Физические бэкенды и снаряды

`PhysicsDesc::backend` выбирает `Auto`, `Box2D` или `Dod`. `Auto` оставляет
Box2D, пока бюджет тел проекта меньше 4096 и не запрошен детерминированный
режим; соединения всегда требуют Box2D. Тот же выбор доступен в
**Project Settings > Physics**, из кода через `PhysicsSystem::SelectBackend` /
`SetProjectPolicy` и из FemboyScript (`physics_get_backend`,
`physics_set_backend`, `physics_get_capabilities`).

Data-oriented бэкенд хранит тела в SoA-массивах, использует uniform-grid broadphase и
поддерживает circle/AABB/segment без соединений и CCD. Box2D остаётся
универсальным выбором. Оба бэкенда используют одни ECS-компоненты, segment
queries и форму событий контакта, поэтому игровой код не разветвляется по
бэкенду.

У `Projectile2D` есть режимы `Auto`, `Hitscan`, `Swept` и `Dynamic`. `Auto`
переводит снаряд на непрерывный запрос, когда смещение за шаг больше половины
`MinWallThickness`; разработчик может вручную задать режим, скорость, радиус и
число подшагов. Hitscan — для пуль, swept — для ракет и стрел, dynamic — для
обычного тела с CCD. Это не даёт пулям проходить сквозь тонкие стены, не платя
за CCD для каждого снаряда.

## Движение камеры и искажения

Стек камеры хранит нетрясущуюся **базовую** позу для геймплея и **итоговую**
позу для рендера. Тряска использует seed-шум и масштабируется настройкой
accessibility. Скрипты собирают универсальные эффекты, а не именованное
игровое состояние: двойное зрение, непропорциональное растяжение, радиальное
искажение, хроматическое расщепление и низкочастотное покачивание доступны
через `camera_distort(flags, strength, paramX, paramY, duration)`, а также
`camera_add_trauma`, `camera_offset`, `camera_zoom`, `camera_roll`,
`camera_clear_effects` и `camera_set_motion_scale`. В LogicGraph есть
соответствующие узлы. См. [эффекты камеры](Documentation/design/camera-effects.md).

## Физика транспорта

Транспорт — это слой движущейся системы отсчёта: `TransportPath` и
`TransportMotion` ведут носитель по фиксированному расстоянию,
`TransportSurface` задаёт трение и скорость конвейера, а режимы
`CarrierMemberComponent` покрывают жёсткий груз, переносимые тела, контактную
опору и конвейеры. `Support` и `Conveyor` используют общую AABB-логику опоры и
тангенциальной скорости для Box2D и DOD; DOD-бэкенд сообщает, что сцепленные
поезда и CCD требуют Box2D. См.
[физику транспорта](Documentation/design/transport-physics.md).

## Колёса транспорта

`WheelSystem` даёт подвеску на wheel-джойнтах Box2D, привод и торможение по
колёсам. `Vehicle2D` хранит газ/тормоз/лимиты, `Wheel2D` — крепление к шасси,
подвеску и веса привода/тормоза. Геометрия руля, RPM, коробка и проскальзывание
шин остаются на стороне игры; опционально доступен аркадный момент рыскания.
В DOD нет wheel-джойнтов. См.
[колёса транспорта](Documentation/design/vehicle-wheels.md).

---

## Установка

Скрипты в один клик устанавливают закреплённый toolchain и раскладывают точные
исходники в `third_party/`; CMake предпочитает эти локальные копии. Руководства
публикуются в Markdown, Word, PDF и DjVu; DjVu открывается в Okular (или любом
DjVu-просмотрщике). Инструменты документации локальны для машины публикации и не
являются зависимостью движка — см.
[Documentation/README.md](Documentation/README.md).

```bat
SetupScripts/Windows/Setup.bat
SetupScripts/Windows/SetupCpp.bat
SetupScripts/Windows/SetupArchiver.bat
SetupScripts/Windows/SetupUpscalers.bat
```

```bash
./SetupScripts/Linux/Setup.sh          # Ubuntu/Debian, всё сразу
./SetupScripts/Linux/SetupCpp.sh       # компилятор, CMake, зависимости
./SetupScripts/Linux/SetupArchiver.sh  # 7-Zip
./SetupScripts/Linux/SetupUpscalers.sh # опциональные SDK DLSS/FidelityFX/XeSS
./SetupScripts/macOS/Setup.command     # macOS, работает и из Finder
./SetupScripts/macOS/SetupCpp.command
./SetupScripts/macOS/SetupArchiver.command
./SetupScripts/macOS/SetupUpscalers.command
```

Версии закреплены: SDL3 `release-3.4.18`, Vulkan-Headers `v1.4.366`, glslang
`15.3.0`, miniaudio `0.11.23`, Box2D `v3.2.0`, libunibreak `6.1` и LZMA SDK
`25.01`. Vendor SDK
апскейлеров опциональны и не входят в архив; см.
[Генерацию кадров](Documentation/md/ru/FemboyEngineFrameGeneration_RU.md).

---

## Сборка

Требования: CMake 3.28+, Ninja, Clang 19+ (clang-cl на Windows, clang++ на
Unix) или GCC 16+. На Windows запускайте из оболочки, которая предоставляет
заголовки и библиотеки Windows SDK; сам компилятор MSVC не поддерживается.
Скрипты установки ставят оба семейства компиляторов: clang-cl / clang++ для
проверенной сборки и GCC 16+ (MinGW-w64 UCRT на Windows, Homebrew или пакет
дистрибутива в остальных случаях) для второго мнения. На Windows `SetupScripts/Windows/SetupCpp.bat`
скачивает официальный MSI **LLVM 23.1.4** и проверяет версию `clang-cl`.
Unix-скрипты ставят Clang из пакетного менеджера (19+); CI закрепляет LLVM
23.1.4 для авторитетного Windows-гейта.

Проверенная сборка использует **clang-cl 23.1.4 на Windows и clang++ на других
платформах**; именно её запускают CI и release gate. GCC 16.2.0 (MinGW-W64 UCRT)
устанавливается `SetupScripts/Windows/SetupCpp.bat` и записывается в dependency lock; smoke-программа
`-std=c++2c` и проверка синтаксиса заголовков движка проходят, но полный гейт
под GCC пока не запускался. Честный статус этой версии — `GCC not run` для
полной сборки, пока в CI не появится Linux GCC-runner.

```bat
set PATH=C:\Program Files\LLVM\bin;%PATH%

cmake --preset windows-release
cmake --build --preset windows-release
ctest --preset windows-release --output-on-failure
```

Для Linux и macOS используйте соответствующие пресеты: они выбирают Vulkan и
Metal, собирают редактор, инструменты и тесты:

```bash
cmake --preset linux-release && cmake --build --preset linux-release
cmake --preset macos-release && cmake --build --preset macos-release
```

На Apple выбирается Metal backend; на Windows/Linux — Vulkan; null backend
оставляет CI и серверы без графического окружения. Windows — локально
проверенный гейт; Linux и Windows CI зелёные, а на macOS зелёный сфокусированный
build/package-гейт. Физическое поведение Metal GPU на macOS пока не проверено.

Статус платформ, платформенные швы и путь сборки игры описаны в
[Platform support](Documentation/design/platform-support.md).

Основные исполняемые файлы:

```text
build/bin/fby_sandbox
build/bin/fby_editor
build/bin/tools/fby_fempak
build/bin/tools/fby_scene_tool
build/bin/tools/fby_aotc
```

Опциональные vendor-апскейлеры включаются явно: SDK DLSS и FidelityFX не
коммитятся. Установите SDK локально и передайте
`-DFBY_WITH_DLSS=ON -DFBY_DLSS_ROOT=<path>` или
`-DFBY_WITH_FIDELITYFX=ON -DFBY_FIDELITYFX_ROOT=<path>`; переносимые FSR1/FSR2 и
собственная генерация кадров не требуют ни одного SDK. См.
[Генерацию кадров](Documentation/md/ru/FemboyEngineFrameGeneration_RU.md).

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
транзакции команд редактора, PIE, физику, SDF-коллизии и SDF-разрушение,
транспортные группы, погоду, доступность и data-driven префабы. Тесты редактора/PIE проверяют, что игровая сессия владеет
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
  powershell -ExecutionPolicy Bypass -File SetupScripts/Windows/RegisterFemboyProjects.ps1
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
`Saved/Logs/editor`. Хранятся 20 последних файлов на категорию и 50 000
последних строк в файле (оба лимита меняются в Editor Settings > Logs). В
Hierarchy и на панели Viewport есть `+ Entity`,
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
единицах (`0,0 209 deg 734 u`). Escape не закрывает редактор; выход —
`File > Quit` или Ctrl+Q.

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

Документация доступна на английском и в русском переводе, с независимыми
изданиями Markdown, Word, PDF и DjVu. Начните с
[Documentation](Documentation/README.md); русский перевод этого README —
[README_RU.md](README_RU.md).

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
- [Физика, SDF-разрушение и транспортные группы (EN)](Documentation/md/en/Physics_EN.md)
- [Физика, SDF-разрушение и транспортные группы (RU)](Documentation/md/ru/Physics_RU.md)
- [Генерация кадров и апскейлинг (EN)](Documentation/md/en/FemboyEngineFrameGeneration_EN.md)
- [Генерация кадров и апскейлинг (RU)](Documentation/md/ru/FemboyEngineFrameGeneration_RU.md)
- [Режимы отображения вьюпорта (EN)](Documentation/md/en/ViewportModes_EN.md)
- [Режимы отображения вьюпорта (RU)](Documentation/md/ru/ViewportModes_RU.md)
- [Справочник настроек редактора (EN)](Documentation/md/en/EditorSettingsReference_EN.md)
- [Справочник настроек редактора (RU)](Documentation/md/ru/EditorSettingsReference_RU.md)
- [Рекомендации по настройкам проекта (EN)](Documentation/md/en/ProjectSettingsRecommendations_EN.md)
- [Рекомендации по настройкам проекта (RU)](Documentation/md/ru/ProjectSettingsRecommendations_RU.md)
- [Стиль кода](Documentation/style/CodeStyle.md)
- [Соглашения об именовании](Documentation/style/NAMING.md)
- [Roadmap](Documentation/ROADMAP.md)
- [Контракт виртуальных текстур](Documentation/design/virtual-texturing.md)
- [Workflow редактора и создание сцен](Documentation/design/editor-workflows.md)
- [Play-In-Editor и viewport клиента](Documentation/design/pie-multiplayer.md)
- [Текстовый стек](Documentation/design/text-stack.md)
- [Движение камеры и искажения](Documentation/design/camera-effects.md)
- [Эффекты камеры](Documentation/design/camera-effects.md)
- [Физика транспорта](Documentation/design/transport-physics.md)
- [Колёса транспорта](Documentation/design/vehicle-wheels.md)
- [Система ввода](Documentation/design/input-system.md)
- [Дизайн физических бэкендов](Documentation/design/physics-backends.md)
- [Аудит CodeQL](Documentation/design/codeql-audit.md)
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

Линия 0.2 alpha — текущий пакет фич поверх первого публичного baseline. Она
развивается с учётом отзывов сообщества; версия 1.0 зарезервирована для перехода
на собственные библиотеки.

---

## Железо

Это требования к машине разработчика для редактора и debug/release-сборки, а не
требования к готовой игре: собранная 2D-игра может быть намного легче
инструментов, которыми её делали.

| Компонент | Маленькая 2D-игра (минимум) | Средняя 2D-игра (рекомендуется) |
|---|---|---|
| CPU | 4 ядра / 8 потоков, например Ryzen 3 3100 или Core i3-10100 | 8 ядер / 16 потоков, например Ryzen 7 5800X или Core i7-12700 |
| RAM | 8 ГБ — абсолютный минимум, 16 ГБ — комфортно | 32 ГБ |
| GPU | Драйвер с Vulkan 1.4 или Metal 2, 2 ГБ VRAM, например GTX 1050, RX 560, Iris Xe | Vulkan 1.4 или Metal 3, 6-8 ГБ VRAM, например RTX 3060, RX 6600, Apple M1 Pro |
| Диск | 10 ГБ свободно на SSD | 25 ГБ свободно на NVMe |
| Монитор | 1920x1080 | 2560x1440 |
| ОС | Windows 10 22H2+/11, Ubuntu 22.04+/24.04, macOS 14+ | Windows 11, Ubuntu 24.04, macOS 14+ на Apple Silicon |

Минимальная колонка — для маленького проекта: несколько сотен спрайтов, одна
tilemap, скромное аудио и один viewport. Рекомендованная — для средней 2D-игры:
большие tilemap, тысячи спрайтов, SDF-освещение, частицы, голосовой чат или
multiplayer PIE и редактор на 1440p. Для редактора дискретная видеокарта
предпочтительнее встроенной; 8 ГБ RAM хватает для запуска, но 16 ГБ — это
практический минимум, когда открыты браузер, IDE и отладчик. Сама build-папка
занимает примерно 2-6 ГБ в зависимости от конфигурации и debug-символов.

Сборка использует Vulkan-Headers **1.4.366**. RHI запрашивает максимальную
версию Vulkan, которую предлагает загрузчик, с потолком 1.4, и включает
возможности 1.2/1.3 только если устройство их предоставляет; **минимальный
поддерживаемый драйвер — Vulkan 1.4**. Машина на уровне рекомендованной колонки
запускает редактор и полный набор CTest без аппаратных обходных путей. На
Windows/Linux используется Vulkan, на macOS — Metal; null RHI оставляет
headless CI-машины рабочими.
