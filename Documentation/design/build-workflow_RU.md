# FemboyEngine2D: сборка и редактор

[English](build-workflow.md) | [Русский](build-workflow_RU.md)

## Рекомендуемый редактор

- VS Code + clangd + CMake Tools;
- альтернатива — CLion.

Встроенный IntelliSense VS Code нужно отключить: проект использует clangd и
`compile_commands.json`.

```json
{
  "C_Cpp.intelliSenseEngine": "disabled",
  "clangd.arguments": [
    "--compile-commands-dir=${workspaceFolder}/build-audit"
  ]
}
```

## Пресеты

```bat
cmake --preset dev-clang
cmake --build --preset dev-clang
ctest --preset dev-clang
```

Доступны:

- `dev-clang` — Debug, clang++, tests;
- `release-clang` — RelWithDebInfo;
- `dev-clangcl` — clang-cl;
- `audit-clang` — существующий `build-audit`;
- `msvc-dev` — Debug, MSVC `cl.exe`, из Developer Prompt;
- `windows-release` — clang-cl, Vulkan, редактор и тесты;
- `linux-release` — clang++, Vulkan, редактор и тесты;
- `macos-release` — clang++, Metal, редактор и тесты.

Проверочные команды:

```text
cmake --preset dev-clang
cmake --build --preset dev-clang --target fby_frame_composer_test
cmake --build --preset dev-clang --target fby_editor_app

cmake --preset dev-clangcl
cmake --build --preset dev-clangcl --target fby_frame_composer_test
```

Оба `fby_frame_composer_test` проходят.

**Build & Pack Game** готовит и упаковывает проект, а с включённым
переключателем "Stage runnable game" копирует sandbox/server, runtime-DLL и
приготовленный пак в `<release>/<project>-game/`. Это и есть one-click-экспорт:
папка запускается без редактора и без build-дерева. Vendor-рантаймы апскейлеров
по умолчанию не копируются; CLI упаковки выбирает их по фичам.

New Project предлагает шаблоны **Empty / 2D Platformer / Top-Down / UI App**:
валидная `scenes/main.toml` только из отражённых компонентов, README и
сгенерированный `CMakeLists.txt` с CMake 3.22. Существующие файлы не
перезаписываются. Дистрибуция: `fby_fempak --exclude`,
`PackageDesktop.py --game --features ... --assets ...` и `--max-size-mb`.

Visual Script компилирует граф в байткод, нативизирует его в библиотеку и
пишет манифест `<module>.fbyaot`. **Plugin Manager** подключает библиотеку к
проекту (только внутри корня проекта), загружает, **Reload/Unload** без
перезапуска редактора. Раздел **Compatibility** показывает ABI хоста и число
компонентов, проверяет layout-hash при загрузке, а **Write plugin report**
пишет `Saved/Plugins/plugin-report.md`.

**Accessibility QA** проверяет опубликованное семантическое дерево и профиль
проекта: узлы, интерактивные роли без имени, неизвестные роли, вырожденные
границы и устаревший focus id, а также Blind mode без нарратора, Deaf mode без
субтитров, выключенный flash guard и пустой язык озвучки. **Write QA report**
пишет Markdown-отчёт в `Saved/Accessibility/qa-<stamp>.md`.

В **Network Diagnostics** есть клиент лобби: укажите host/port и 32-байтовый
Ed25519-ключ (64 hex), затем создайте комнату, войдите по id или начните матч.
Панель показывает ready/room/player/started/error и подписанный ticket; игра
вызывает `NetLobbyClient::ApplyToSessionConfig()` до создания `NetSession`, а
ниже видны rollback-диагностики матча. Лобби использует тот же UDP-транспорт,
что и игра.

Панель **SDF Lighting** создаёт и редактирует единственный `SdfSunComponent`
(направление, softness, ambient, радиус/сила окклюзии, шаги) и добавляет
box/circle `Light2DOccluderComponent` в центре камеры. Рендер собирает не более
`Rhi::kMaxSdfGpuOccluders` динамических окклюдеров за кадр; панель показывает
бюджет и предупреждает о превышении. Компоненты сохраняются вместе со сценой.

Профайлер (**F7**) показывает график времени кадра и строки по категориям,
**Write trace** сохраняет `Profiler/trace.json`. **Crash Diagnostics** показывает,
установлен ли обработчик, текущую сессию отчёта и последние сессии в
`Saved/Crashes/`; **Write report now** пишет тот же текстовый отчёт и minidump,
что и необработанное падение, с именем и корнем проекта — тестировщик может
приложить файл без воспроизведения краша.

Particle FX панель настраивает `ParticleEffectSettings` и сохраняет текущий вид
в `.fbyparticle`: **Preset id** и **Display name**, затем **Save preset** пишет
таблицу `[[presets]]` по указанному пути. **Load pack** читает её тем же
валидатором, что и рантайм, поэтому авторский пресет не может содержать
значения, которые игра отклонит.

Выбранный prefab-инстанс показывает в Inspector секцию **Prefab overrides**:
для каждой сущности prefab-а — переключатели position/scale/rotation/color.
В сцену пишутся только включённые поля (маски в `PrefabInstanceComponent`),
дочерние сущности пересобираются из prefab-а, а не копируются; **Clear all
overrides** сбрасывает маски. Сам prefab-ассет не меняется.

В Content Browser у выбранного ассета редактируются import settings (формат
текстуры, максимальный размер, sRGB, мипы; для шрифтов — стиль, размер,
oversample, SDF). **Apply and reimport** пишет их в `.femeta` и ставит обычный
фоновый кукинг; старая текстура остаётся, пока воркер не загрузит новый слой,
поэтому редактор не блокируется. Счётчики очереди/кукинга/коммитов показывают
зависший hot reload.

Профили производительности Lite/Balanced/High ограничивают арену undo, кэш
превью и частоту кадров; Custom сохраняет отдельные значения. Лимит кадров и
throttle без фокуса — два переключателя, которые меняют задержку на процессор и
GPU без изменения проекта.

Матрица desktop использует `windows-release`, `linux-release` и `macos-release`.
Каждый пресет собирает редактор, sandbox, инструменты и упаковщик игры. Редактор
создаёт `bin/fby_editor`; Build & Pack Game вызывает `bin/tools/fby_fempak` на
всех трёх системах. Статус проверки — в
[platform support](platform-support.md).

## Выбор компилятора

0.7.0-alpha.1 поддерживает Clang, AppleClang, GCC и MSVC. clang-cl — эталонный
Windows-тулчейн и release gate; CI также прогоняет clang, GCC, AppleClang и
MSVC. Любой компилятор — поддерживаемая альтернатива, но никогда не обязанность.
Минимальные версии — самый старый поддерживаемый выпуск, а не верхняя граница;
hosted CI закрепляет LLVM 23.1.4 на Windows, LLVM 23.1.x на Linux, GCC 16.2 и
текущий Visual Studio 2022 MSVC.

| Компилятор | Платформа | Минимум | Для чего |
|---|---|---|---|
| clang-cl | Windows | 19.1 | основной Windows-билд, PDB и отладка Visual Studio |
| MSVC (`cl.exe`) | Windows | 19.40 | поддерживаемая альтернатива с отдельной Windows CI-джобой |
| clang++ | Linux/macOS/Windows | 19.1 | кросс-платформенный паритет, основной Unix-билд |
| GCC | Linux/MinGW | 14.1 | второй компилятор и проверка переносимости |
| AppleClang | macOS | 16.0 (Xcode 16) | нативная macOS/Metal-сборка |

- **Windows:** clang-cl — эталонная сборка. Он использует Windows SDK и
  стандартную библиотеку MSVC, работает с отладчиком Visual Studio и PDB.
  MSVC (`cl.exe`) — поддерживаемая альтернатива для нативного Visual Studio-
  тулчейна.
- **Linux:** clang++ — эталон; GCC 14.1 — второе мнение.
- **macOS:** AppleClang 16+ с Metal-бэкендом.
- **Один компилятор везде:** Clang. Только он покрывает Windows (clang-cl или
  clang++), Linux и macOS одним фронтендом.

### Цель по доступности

Движок рассчитан на **C++23**, последний ратифицированный стандарт.
Поддерживаемый минимум: Clang 19.1, GCC 14.1, MSVC 19.40 и AppleClang 16.
Прямой `cmake -S . -B` требует CMake 3.22; встроенные пресеты используют схему
v6 и поэтому требуют CMake 3.25.

Минимум задан обязательным набором библиотеки C++23 (`std::mdspan`,
`std::format`, `std::jthread`, `std::expected`), а не только режимом языка.
Более старые GCC 13 или Clang 17 могут не иметь `std::mdspan`, поэтому они не
являются целями. Ubuntu 22.04 требует более нового компилятора из репозиториев
или LLVM apt; Ubuntu 24.04 поставляет GCC 13 и Clang 18 и обычно требует
`g++-14` или `clang-19` по той же причине.

### Режимы стандарта C++

`-DFBY_CXX_STANDARD=23` — режим по умолчанию и публичный контракт.
`-DFBY_CXX_STANDARD=26` — экспериментальный режим: те же исходники с флагами
C++26 и те библиотечные возможности C++26, которые доказывают feature-пробы.
Публичный API не использует синтаксис C++26, поэтому оба режима дают один и тот
же движок, а сборка на C++23 не является урезанной.

Сейчас подключено одно ускорение: `Fby::Simd` для сканирования пика шин аудио.
`Fby::FunctionRef` — парная невыделяющая обёртка для колбэков, аналог
`std::function_ref` в C++23. Сборка проверяет `std::simd`, но ни libc++ 19, ни
libstdc++ 16 пока не поставляют `<simd>`, поэтому в C++23 используется
SSE2/NEON-редукция со скалярным хвостом.
Замер на буфере 1 МиБ при `-O2`/`-O3` на x86-64:

| Кернел | Скалярный цикл | `Fby::Simd` | Результат |
|---|---|---|---|
| поиск пика | 26.8 мс | 6.8 мс | **3.8–4.0x** |
| сложение | 4.1 мс | тот же скалярный цикл | выигрыша нет: автовекторизатор уже справляется |
| масштабирование | 2.5 мс | тот же скалярный цикл | выигрыша нет: автовекторизатор уже справляется |

В масштабе движка это меньше 1% кадра: сокращается сканирование пика аудио-шин
и появляется запас на аудиопотоке. Это не прирост FPS, и сборка его не обещает.
Новые библиотечные ускорения C++26 идут через те же пробы и обёртки `Fby::`;
языковые возможности C++26 сознательно вне области.

Конфигурация MSVC. У тулчейна Microsoft есть отдельная CI-джоба, но он никогда не
является эталонным гейтом. Из Developer Prompt `cmake --preset msvc-dev`
использует ту же конфигурацию:

```bat
cmake -S . -B build-msvc -G Ninja ^
      -DCMAKE_C_COMPILER=cl ^
      -DCMAKE_CXX_COMPILER=cl ^
      -DCMAKE_BUILD_TYPE=RelWithDebInfo ^
      -DFBY_BUILD_TESTS=ON ^
      -DFBY_WITH_RNNOISE=OFF
cmake --build build-msvc --parallel 8
ctest --test-dir build-msvc --output-on-failure
```

RNNoise отключён для MSVC: его C-исходники используют variable-length arrays,
которых нет во фронтенде Microsoft. Остаётся spectral-denoise fallback; Clang,
GCC и clang-cl сохраняют neural tier.


Сборка требует Vulkan 1.4 на Windows и Linux; на macOS используется Metal.
Требования к редактору — в
[системных требованиях](system-requirements_RU.md).

Быстрый профильный gate:

```bat
ctest --preset optimization
```

Нельзя смешивать toolchain’ы в одном build-каталоге. Для clang++ и clang-cl
используются разные каталоги.

## Windows: GUI subsystem

`FBY_CONSOLE_APPS=ON` оставляет консоль у приложений. Для clang-cl проект
собирает GUI-subsystem executable с `/ENTRY:mainCRTStartup`. Для GNU-like
clang++/GCC `WIN32_EXECUTABLE` не включается: эти драйверы ожидают `WinMain`,
а движок использует обычный `int main()`. Поэтому `dev-clang` на Windows
собирает консольные приложения, а `dev-clangcl` — GUI-приложения.

## Ручная сборка

```bat
cmake -S . -B build -G Ninja ^
      -DCMAKE_C_COMPILER=clang-cl ^
      -DCMAKE_CXX_COMPILER=clang-cl ^
      -DCMAKE_BUILD_TYPE=RelWithDebInfo ^
      -DFBY_BUILD_TESTS=ON
cmake --build build --parallel 8
ctest --test-dir build --output-on-failure
```

GNU-like clang++:

```bat
cmake -S . -B build-clang -G Ninja ^
      -DCMAKE_C_COMPILER=clang ^
      -DCMAKE_CXX_COMPILER=clang++ ^
      -DCMAKE_BUILD_TYPE=Debug ^
      -DFBY_BUILD_TESTS=ON ^
      -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-clang --parallel 8
```

## clangd

`.clangd` указывает на `build-audit`. Если используется другой каталог, поменять
`CompilationDatabase` или передать `--compile-commands-dir`.

Для системных заголовков Windows может понадобиться:

```text
--query-driver=C:/Users/<user>/AppData/Local/Programs/LLVM/bin/clang++.exe
```

## Отладка

`launch.json` содержит конфигурации для:

- `fby_editor`;
- `fby_frame_composer_test`.

Для GPU-отладки использовать RenderDoc, а не встраивать отладчик в редактор.

## Интеграция в редактор движка

Не писать собственный текстовый редактор. Интеграция должна быть через:

- `compile_commands.json`;
- CMake-пресеты;
- LSP;
- DAP;
- `project.femproj`.

Этого достаточно для автодополнения, переходов и отладки без создания
собственного аналога IDE.
