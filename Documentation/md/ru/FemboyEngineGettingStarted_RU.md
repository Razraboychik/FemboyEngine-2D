# FemboyEngine 0.7.0-alpha.1 — как сделать игру

Этот документ описывает, что нужно установить и какие файлы должны лежать рядом с движком, чтобы запустить редактор, рантайм и инструменты разработки на Windows, Linux и macOS.

## Поддерживаемые платформы

| Платформа | Редактор | Рантайм игр | Сборка |
|---|---|---|---|
| Windows 10/11 x64 | да | да | clang-cl 19.1+ (основной), MSVC 19.40+, GCC 14.1+ |
| Linux x64 | да | да | Clang 19.1+, GCC 14.1+ |
| macOS 14+ (Apple Silicon и Intel) | да | да | AppleClang 16+ (Xcode 16+), Metal-бэкенд |

Редактор и рантайм игр собираются для трёх настольных платформ выше. SDL3 предоставляет окно, ввод и жизненный цикл приложения.

## Требуемое программное обеспечение

**MSVC 19.40+ поддерживается как альтернатива.** На Windows проект собирается clang-cl из состава LLVM как эталонный тулчейн или MSVC (`cl.exe`), если команда стандартизируется на нём. Оба используют Windows SDK и стандартную библиотеку MSVC; clang-cl остаётся release gate. Setup-скрипт ставит **последний стабильный LLVM**, последний Vulkan SDK и архиватор, если их нет; CI закрепляет эталонные версии ниже для воспроизводимости.

| Инструмент | Минимальная версия | Эталон CI | Зачем |
|---|---|---|---|
| CMake | 3.22 | 3.31.6 | конфигурация и сборка |
| Ninja | 1.11 | 1.12.1 | генератор сборки |
| LLVM / clang-cl | 19.1 | 23.1.4 | основной компилятор Windows; setup ставит последний стабильный |
| MSVC (`cl.exe`) | 19.40 | 19.44 | компилятор Visual Studio с отдельной CI-джобой |
| Clang (Linux) | 19.1 | 23.1.x | основной компилятор Linux; пакет дистрибутива обычно свежий |
| GCC | 14.1 | 16.2 | второй компилятор для проверки переносимости |
| Python | 3.11 | 3.13 | генерация документации и notices |
| Git | 2.40 | — | клонирование и версионирование |
| Vulkan Loader | 1.4 | 1.4 | рантайм на Windows и Linux |
| MoltenVK | 1.2 | — | Vulkan поверх Metal на macOS (либо Metal-бэкенд напрямую) |
| 7-Zip | 21 | 25.01 | распаковка LZMA SDK (иначе используется встроенный FbyLZ) |
| Xcode | 16.0 | 16.0 | сборка под macOS (AppleClang 16; Homebrew LLVM новее) |

Стандарт языка — C++23, поэтому старые компиляторы не подойдут, даже если версия CMake совпадает.

## Быстрый старт: скрипты установки

Скрипты лежат в `SetupScripts/<OS>/` и разделены по ОС и группам ПО. Скрипты
«всё сразу» ставят всё; компонентные скрипты — только одну группу.

| Группа | Windows | macOS | Linux |
|---|---|---|---|
| C++ toolchain, CMake и зависимости | `SetupScripts\Windows\SetupCpp.bat` | `./SetupScripts/macOS/SetupCpp.command` | `./SetupScripts/Linux/SetupCpp.sh` |
| Архиватор 7-Zip | `SetupScripts\Windows\SetupArchiver.bat` | `./SetupScripts/macOS/SetupArchiver.command` | `./SetupScripts/Linux/SetupArchiver.sh` |
| Опциональные SDK DLSS/FidelityFX/XeSS | `SetupScripts\Windows\Setup.bat -WithVendorSdks -AcceptVendorLicenses` | `./SetupScripts/macOS/Setup.command --with-vendor-sdks --accept-vendor-licenses` | `./SetupScripts/Linux/Setup.sh --with-vendor-sdks --accept-vendor-licenses` |

Общие точки входа: `SetupScripts/Windows/Setup.bat`,
`SetupScripts/macOS/Setup.command` и `SetupScripts/Linux/Setup.sh`.
C++-скрипты ставят компилятор, CMake, Ninja, Vulkan-заголовки и закреплённые
зависимости. На Windows `SetupScripts/Windows/SetupCpp.bat` ставит официальный MSI LLVM 19.1 и
проверяет версию `clang-cl`; Unix-скрипты ставят Clang из пакетного менеджера
(19+). Скрипты архиватора ставят 7-Zip. Vendor SDK скачиваются только общим
Setup после явных licence-флагов; в репозиторий они не входят.

Полезные флаги: `--skip-system` — не трогать системные пакеты (образ CI уже
содержит инструменты); `--force` — заново скачать C++-архивы. C++-скрипт
создаёт `third_party/dependencies.lock` с версиями и хешами архивов. В конце
он печатает сводку установленных инструментов.

### Из архива GitHub

1. Открыть страницу репозитория и выбрать **Code > Download ZIP**.
2. Распаковать архив в каталог без пробелов и не-ASCII символов.
3. Запустить `SetupCpp` и `SetupArchiver` для своей ОС из таблицы выше.
4. При необходимости передать licence-флаги vendor SDK в общий `Setup` для своей ОС.
5. Настроить и собрать:

```
cmake --preset dev-clang
cmake --build --preset dev-clang
ctest --preset dev-clang --output-on-failure
```

В архиве нет vendor SDK. C++-скрипт ставит toolchain и раскладывает
закреплённые исходники; archiver-скрипт ставит 7-Zip. Скрипты апскейлеров
нужны только при явном включении DLSS/FidelityFX. Без них FSR1, core-путь
FSR2 и собственная генерация кадров собираются и работают с runtime-fallback.

## Ручная установка

Если скрипт запускать не хочется, набор действий одинаков: поставить компилятор, CMake, Ninja, Vulkan-заголовки и разложить сторонние библиотеки.

### Windows

1. Установить LLVM 19.1 (clang-cl) и добавить `bin` в `PATH`.
2. Установить Visual Studio Build Tools с рабочей нагрузкой «C++ Build Tools» (Windows SDK + заголовки стандартной библиотеки). clang-cl использует STL MSVC; компилятор MSVC нужен только для отдельной MSVC-сборки.
3. Установить CMake 3.31 и Ninja 1.12 (можно из поставки Build Tools).
4. Установить Vulkan SDK 1.4 — он даёт `vulkan-1.dll` и `VULKAN_SDK`.
5. Установить 7-Zip (по желанию).
6. Скачать архивы, которые обычно качает скрипт: SDL3 `release-3.4.18`, Vulkan-Headers `v1.4.366`, glslang `15.3.0`, miniaudio `0.11.23`, Box2D `v3.2.0`, RNNoise `0.1.1`, Monocypher `4.0.3` и распаковать их в те же каталоги `third_party/`, что создаёт скрипт. HarfBuzz и SheenBidi скачивать не нужно: они уже лежат в репозитории.

### Linux

```
sudo apt install build-essential clang cmake ninja-build git pkg-config libvulkan-dev vulkan-tools p7zip-full
```

Дополнительно раскладываются те же архивы, что и на Windows. GCC 14.1 ставится из дистрибутива или собирается отдельно; для основной сборки достаточно Clang.

### macOS

```
xcode-select --install
brew install cmake ninja git p7zip glslang vulkan-headers molten-vk llvm
```

Xcode Command Line Tools обязательны: они дают `clang++`, Metal-фреймворки и заголовки. Для Metal-бэкенда Vulkan SDK не нужен.

## Сборка

Из корня репозитория:

```
cmake -S . -B build-dev -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DFBY_BUILD_TESTS=ON
cmake --build build-dev
ctest --test-dir build-dev --output-on-failure
```

На Windows те же команды выполняются в окружении `vcvars64`; зависимости и инструменты подготавливает `SetupScripts/Windows/SetupWindows.ps1`. Основные опции CMake: `FBY_BUILD_EDITOR`, `FBY_BUILD_TESTS`, `FBY_BUILD_TOOLS`, `FBY_BUILD_SANDBOX`, `FBY_BUILD_SERVER`, `FBY_BUILD_LOBBY`, `FBY_WITH_ASAN`, `FBY_RHI_BACKEND` (`auto`, `vulkan`, `metal`, `null`).

Готовые исполняемые файлы раскладываются по назначению:

| Каталог | Что внутри |
|---|---|
| `build-dev/bin` | приложения: `fby_editor`, `fby_sandbox`, `fby_server`, `fby_lobby`, плагины платформ и `plugins/` |
| `build-dev/bin/tools` | консольные инструменты: `fby_fontc`, `fby_textc`, `fby_fempak`, `fby_scene_tool`, `fby_template_gen`, `fby_aotc`, `fby_loc_tool`, `fby_shaderc` |
| `build-dev/bin/tests` | тестовые исполняемые файлы (`fby_*_test`), их запускает `ctest` |
| `build-dev/bin/lib` | статические библиотеки |
| `build-dev/bin/plugins` | нативные плагины движка |

Файлы `*.pdb` рядом с исполняемыми файлами — это отладочные символы, которые создаёт компилятор. Они нужны только отладчику и не требуются для запуска; в релизной поставке их можно не распространять.

## Редактор кода и IDE

Собственный текстовый редактор движку не нужен. Для разработки используется внешний редактор, который умеет читать CMake-проект и `compile_commands.json`.

Рекомендуемый вариант:

- **VS Code**;
- расширение **clangd**;
- расширение **CMake Tools**;
- **CodeLLDB** или `cppdbg` для отладки;
- RenderDoc для отладки GPU.

CLion тоже подходит: он нативно понимает CMake и умеет запускать `fby_editor` и тесты. Visual Studio можно использовать как IDE и как MSVC-сборку; подробности ниже.

### VS Code: настройка

1. Установить VS Code.
2. Установить расширения:
   - `llvm-vs-code-extensions.vscode-clangd`;
   - `ms-vscode.cmake-tools`;
   - `vadimcn.vscode-lldb` либо `ms-vscode.cpptools` для `cppdbg`.
3. Открыть корень репозитория (папку, в которой лежит `CMakePresets.json`).
4. Отключить встроенный IntelliSense, иначе он будет конфликтовать с clangd:

```json
{
  "C_Cpp.intelliSenseEngine": "disabled"
}
```

5. Выбрать CMake-пресет `dev-clang` или `dev-clangcl`.
6. Сконфигурировать проект:

```bat
cmake --preset dev-clang
```

7. Собрать:

```bat
cmake --build --preset dev-clang
```

8. Запустить тесты:

```bat
ctest --preset dev-clang
```

В репозитории уже лежат:

- `CMakePresets.json` — пресеты;
- `.clangd` — путь к базе компиляции;
- `.vscode/settings.json` — настройки clangd и CMake Tools;
- `.vscode/launch.json` — запуск редактора и `frame_composer` теста.

### clangd

clangd должен использовать тот же Clang, которым собирается проект. В настройках:

```json
{
  "clangd.arguments": [
    "--compile-commands-dir=${workspaceFolder}/build-audit",
    "--query-driver=C:/Users/<user>/AppData/Local/Programs/LLVM/bin/clang++.exe"
  ]
}
```

`--query-driver` нужен, чтобы clangd нашёл системные заголовки Windows SDK. Если используется другой build-каталог, заменить путь в `--compile-commands-dir` или в `.clangd`.

Нельзя держать одновременно включёнными C/C++ IntelliSense и clangd: получится два разных набора диагностик и переходов.

### Пресеты сборки

| Пресет | Компилятор | Назначение |
|---|---|---|
| `dev-clang` | `clang++` | Debug, тесты, `compile_commands.json` |
| `release-clang` | `clang++` | RelWithDebInfo, тесты |
| `dev-clangcl` | `clang-cl` | Debug через проверенный Windows-toolchain |
| `audit-clang` | `clang++` | существующий `build-audit` для clangd |

Разные компиляторы должны использовать **разные build-каталоги**. Нельзя переконфигурировать `build-audit` с clang++ на clang-cl: CMakeCache останется от старого toolchain.

### Отладка

Для `cppdbg` используется gdb из MinGW/LLVM либо CodeLLDB с LLVM. Пример запуска:

```json
{
  "name": "Editor (Debug)",
  "type": "cppdbg",
  "request": "launch",
  "program": "${workspaceFolder}/build-dev/bin/fby_editor.exe",
  "cwd": "${workspaceFolder}",
  "MIMode": "gdb",
  "miDebuggerPath": "gdb.exe"
}
```

Для GPU-отладки использовать RenderDoc: он подключается к `fby_editor.exe` и снимает кадр без встраивания отладчика в редактор.

## Выбор компилятора

0.7.0-alpha.1 поддерживает **Clang 19.1+** (clang-cl на Windows, clang++ на Unix), **AppleClang 16+**, **GCC 14.1+** и **MSVC 19.40+**. clang-cl — эталонный тулчейн, остальные поддерживаются как альтернативы и никогда не обязательны. CI прогоняет clang-cl, clang, GCC и AppleClang полным набором тестов; MSVC 19.40+ собирает то же дерево в отдельной Windows CI-джобе.

- **Windows, основной:** clang-cl. Это Clang с ABI MSVC, Windows SDK и стандартной библиотекой MSVC; работает с отладчиком Visual Studio и PDB.
- **Windows, альтернатива:** MSVC (`cl.exe`). Выбирайте, если команда стандартизируется на Microsoft-тулчейне; CMake включает `/std:c++latest` и набор предупреждений MSVC. RNNoise переходит на spectral-tier, потому что его C-исходники используют variable-length arrays.
- **Linux, основной:** clang++. GCC 14.1 — второй компилятор для проверки переносимости.
- **macOS:** AppleClang 16+ из Xcode 16+ с Metal-бэкендом.
- **Один компилятор на всех платформах:** Clang (clang-cl на Windows, clang++ на остальных). Это единственный тулчейн, покрывающий все три платформы одним фронтендом.

Полная матрица и пресеты по ОС — в [build-workflow_RU.md](../../design/build-workflow_RU.md).

## Запуск редактора

Редактор открывает проект из **текущего рабочего каталога**: он ищет `project.femproj`, поднимает виртуальную файловую систему на `assets/`, пишет настройки проекта в `editor.ini` рядом с `project.femproj`, а раскладку ImGui хранит в `Saved/Config/editor.ini`. Поэтому запускать его нужно из корня проекта (или из корня репозитория для демонстрационного проекта).

```
cd %USERPROFILE%\FemboyEngine
build-dev\bin\fby_editor.exe
```

Linux и macOS:

```
cd /path/to/project
./build-dev/bin/fby_editor
```

Все изменяемые файлы движок складывает в `Saved/` в рабочем каталоге, поэтому каталог проекта остаётся чистым:

| Путь | Содержимое |
|---|---|
| `Saved/Logs/<категория>/<категория>-YYYYMMDD-HHMMSS.log` | полный лог запуска: `engine`, `editor`, `sandbox`, `server`, `lobby`. Хранится 20 последних файлов на категорию, более старые удаляются автоматически |
| `Saved/Crashes/<YYYYMMDD-HHMMSS>-<pid>/report.txt` | отчёт о падении: время, приложение, pid, причина, адрес и, на Linux и macOS, стек вызовов |
| `Saved/Crashes/<...>/crash.dmp` | минидамп Windows для отладчика |
| `Saved/Config/editor.ini` | настройки и раскладка панелей редактора |
| `Saved/Cache/` | кэш промежуточных данных |

Переменные окружения: `FBY_STORAGE_DIR` переносит корень `Saved/` (например на диск с местом), `FBY_LOG_FILE=0` отключает запись лога в файл (консоль продолжает работать).

### Что должно лежать рядом

| Путь | Обязателен | Назначение |
|---|---|---|
| `build-dev/bin/fby_editor` | да | сам редактор |
| `assets/` | да | шрифты, локализация, сцены, атласы, конфиги проекта |
| `assets/fonts/IBMPlexSans-Regular.ttf`, `...-Bold.ttf`, `...-Italic.ttf`, `...-BoldItalic.ttf`, `IBMPlexMono-Regular.ttf` | да | семейство шрифтов редактора и текста сцены |
| `assets/fonts/IBMPlex.family.toml` | да | манифест семейства: пять слотов стилей |
| `assets/fonts/Inter.fbyfont` | да | испечённый атлас для устаревшего пути UI |
| `assets/loc/ui.fbin` | да | таблица локализации интерфейса |
| `assets/scenes/*.toml` | да | сцены проекта |
| `project.femproj` | да | описание проекта: сцена по умолчанию, симуляция, рендер, доступность |
| `build-dev/` | да | дерево сборки CMake: там лежат исполняемые файлы и результаты сборки |
| `build-dev/bin/fby_sandbox` | нет | второй процесс для режима Play-in-Editor по сети |
| `Saved/` | создаётся | логи, отчёты о падениях, кэш и настройки редактора |
| `Saved/Config/editor.ini` | создаётся | раскладка панелей и настройки редактора для этой машины |
| `build-dev/bin/plugins/` | нет | нативные плагины движка |
| Vulkan Loader в системе | да | окно и GPU-рендер на Windows и Linux |
| `Projects/` | нет | каталог по умолчанию для новых проектов |

### Выбор проекта

Проект — это каталог с файлом `project.femproj` и подкаталогами `assets/`, `configs/`. Новые проекты создаются в `Projects/` внутри корня движка; открыть существующий можно через дашборд редактора по пути к `project.femproj` или к папке проекта. Отдельного аргумента командной строки для проекта нет: проект определяется рабочим каталогом, поэтому «ярлык на проект» — это ярлык с другим рабочим каталогом.

## Инструменты разработки

Кроме редактора в `build-dev/bin` собираются отдельные исполняемые файлы. Все они — обычные консольные программы, читают файлы и не требуют GUI.

| Инструмент | Назначение | Что нужно из файлов |
|---|---|---|
| `fby_editor` | редактор: сцены, ассеты, PIE, предпросмотр текста | `assets/`, `project.femproj` |
| `fby_sandbox` | песочница и клиент Play-in-Editor | `assets/` |
| `fby_server` | выделенный сервер | каталог проекта по желанию |
| `fby_lobby` | сервис лобби и матчмейкинга | не требуется |
| `fby_fontc` | сборка `.fbyfont` из TTF/OTF | TTF-файл на входе |
| `fby_textc` | компиляция размеченного текста в `.fbin` | `.toml` текста на входе |
| `fby_fempak` | упаковка ассетов в `.fempak` | каталог ассетов |
| `fby_scene_tool` | проверка и конвертация сцен | `.toml` сцены |
| `fby_template_gen` | создание каркаса нового проекта | каталог назначения |
| `fby_aotc` | нативизация скриптов (AOT) | проект с байткодом |
| `fby_loc_tool` | проверка и сборка таблиц локализации | `.toml` локализации |
| `fby_shaderc` | компиляция шейдеров в SPIR-V | `.vert`/`.frag`; вызывается сборкой автоматически |

Инструменты лежат в `build-dev/bin/tools`, тестовые исполняемые файлы — в `build-dev/bin/tests`.

Сборка и тесты запускаются через пресеты CMake; установка зависимостей — в `SetupScripts/<OS>/*`.

## Ярлыки для быстрого запуска

Рабочий каталог — единственное, что отличает запуск «движка» от запуска «проекта», поэтому ярлык всегда должен задавать его явно.

### Windows

1. Правый щелчок по `build-dev\bin\fby_editor.exe` → «Отправить» → «Рабочий стол (создать ярлык)».
2. Свойства ярлыка: «Рабочая папка» = корень проекта (например `%USERPROFILE%\FemboyEngine`), «Объект» = полный путь к `fby_editor.exe`.
3. Переименовать ярлык в «FemboyEngine» и перетащить на панель задач или в меню «Пуск».
4. Для отдельного проекта сделать второй ярлык с тем же «Объектом», но «Рабочей папкой» = папка проекта. Это и есть «выбор проекта» без диалога.

### macOS

1. Открыть папку проекта в Finder.
2. Перетащить `fby_editor` (или `.app`, если собран бандл) в Dock, удерживая `Command`.
3. Для конкретного проекта создать `.command`-файл с двумя строками: `cd /path/to/project` и `/path/to/fby_editor`; Finder запускает его двойным щелчком, а ярлык можно положить в Dock.
4. `SetupScripts/macOS/Setup.command` запускается так же — двойным щелчком.

### Linux

Файл `~/.local/share/applications/fby-editor.desktop`:

```
[Desktop Entry]
Type=Application
Name=FemboyEngine
Exec=/path/to/fby_editor %f
Path=/path/to/project
Icon=/path/to/Logo.png
Terminal=false
Categories=Development;
```

После сохранения — «Закрепить» в доке или на панели. Второй `.desktop` с другим `Path` даёт ярлык на другой проект.

## Проверка инструментов

Точный список версий можно получить командами:

```
cmake --version
ninja --version
```

На macOS версии Xcode и SDK показывает `xcodebuild -version`.

## Типичные проблемы

| Симптом | Причина | Решение |
|---|---|---|
| Редактор не видит ассеты | запущен не из корня проекта | запустить из каталога с `assets/` и `project.femproj` |
| Ошибка Vulkan при старте | нет Vulkan Loader в системе | установить Vulkan Runtime или собрать с `-DFBY_RHI_BACKEND=null` |
| Редактор или игра закрылись без сообщения | падение процесса | посмотреть `Saved/Crashes/` в рабочем каталоге: `report.txt` и `crash.dmp` |
| Нужен лог запуска для отчёта | логи теперь в файле | взять последний файл из `Saved/Logs/<категория>/` |
| `glslang` не найден при сборке | не разложен `third_party/glslang-src` | запустить скрипт установки заново |
| Нет шрифтов в редакторе | отсутствуют TTF или манифест семейства | вернуть `assets/fonts/` из репозитория |
| Ошибка CMake про harfbuzz или SheenBidi | повреждён или удалён каталог `third_party/` | восстановить `third_party/harfbuzz-download/` и `third_party/sheenbidi-download/` из репозитория |
| Windows-сборка падает на заголовках | нет Windows SDK | установить Build Tools с рабочей нагрузкой C++ |

## От пустого проекта до играбельной сцены

Новый проект содержит пустой `assets/scenes/main.toml`, профиль по умолчанию и никаких
навязанных актёров. Редактор не решает за вас, какая это игра.

1. Создайте проект в dashboard. Шаблон жанра не применяется.
2. В **Project Settings → Physics** выберите профиль: Default, Side View, Top-Down,
   Isometric или Hex Isometric.
3. Откройте `scenes/main.toml` и добавьте только нужные сущности. Минимальный side-view
   старт — камера, спрайт игрока и земля.

```toml
version = 2
name = "main"
author = "you"
profile = "side-view"

[[entity]]
id = 0
name = "camera"

[[entity.components]]
type = "Camera2D"
Center = [640.0, 360.0]
ViewSize = [1280.0, 720.0]
Zoom = 1.0
Rotation = 0.0

[[entity]]
id = 1
name = "player"

[[entity.components]]
type = "Transform2D"
Position = [320.0, 240.0]
Scale = [1.0, 1.0]
Rotation = 0.0

[[entity.components]]
type = "Sprite2D"
Size = [48.0, 48.0]
UVMin = [0.0, 0.0]
UVMax = [1.0, 1.0]
Layer = 1
Color = "#4FC3F7FF"
Flags = 0
```

4. Назначьте текстуру через инспектор; `UVMin`/`UVMax` выбирают ячейку.
5. Добавляйте `RigidBody2D` и `Collider2D` только если сущность должна симулироваться.
6. Нажмите Play (PIE). Профиль сцены применяется; если он `Default`, берётся профиль проекта.

Для top-down поставьте `profile = "top-down"` — профиль обнуляет гравитацию. Для гексов —
`profile = "hex"`; соседство и occupancy остаются в grid/tilemap слое, а свободные объекты
могут использовать физику.

### Тайлмапы и Tiled

Добавьте `TilemapComponent` на сущность и откройте **Window → Tilemap**. Панель редактирует
компонент напрямую. **Import Tiled** читает `.tmx` с CSV-слоями:

- orthogonal → TopDown;
- isometric → IsoRect;
- hexagonal → IsoHex;
- CSV и несжатый base64 поддерживаются; сжатые слои отвергаются с сообщением;
- карты больше 64×64 нужно делить или стримить;
- object groups дают name/type/позицию/размер и point-объекты для spawn-маркеров.

Auto-tiling доступен через API:

```cpp
Fby::Tilemap::AutoTileRule rule;
rule.Tiles[0] = 1;   // одиночный
rule.Tiles[15] = 16; // окружённый
Fby::Tilemap::AutoTileAll(map, rule);
```

### Анимация Aseprite

Экспортируйте PNG и JSON из Aseprite. В инспекторе Sprite Animation укажите **Aseprite JSON**
и нажмите **Import Aseprite**. Первый frame tag становится клипом; без тегов импортируются
все кадры. Кадры должны иметь одинаковую ячейку, а лист — делиться на её размер. Импорт
заполняет стартовую клетку, число кадров, длительность и сетку `SpriteSheetComponent`.
Non-grid экспорт остаётся доступен инструментам, но не fixed-cell анимации.

### Скрипты и отладка

Откройте **Window → Script Debugger**. Он компилирует FemboyScript в bytecode и даёт:

- breakpoints по строкам;
- step/resume;
- Locals и Self fields;
- watch/eval для `rN`, чисел и именованных локальных переменных;
- условия breakpoint’ов вида `r3 >= 12.5`;
- call stack;
- подсветку узла LogicGraph;
- Compile + Nativize для релиза.

Отладчик владеет отдельным runtime, поэтому пауза не останавливает игровой мир. Используйте
тот же source и component registry, что и игра; ошибки компиляции видны в Status.

### Build & Pack

Нажмите **Build & Pack Game** в dashboard или `Ctrl+B`. Release cooker:

1. валидирует проект;
2. кукает текстуры/аудио/локализацию;
3. собирает игру;
4. пишет архивы в `archives/`;
5. показывает путь в статусе редактора.

Desktop-пайплайн доступен из UI, а не только из терминала. Android/iOS экспорта в этой
сборке ещё нет.

### Профилирование сцены

Используйте **Render Tuning**, **Scene Diagnostics**, **Network Diagnostics** и LogBoy.
Они читают те же компоненты, что игра; отдельная debug-сборка не нужна. При просадке
сравнивайте `FrameStats` и тайминги систем в графе до изменения арта или физики.

## UI-дизайнер

Откройте **Window → UI Layout**. Панель редактирует `WidgetLayoutAsset` напрямую:

- добавление Panel/Label/Button/Image;
- выбор на preview-canvas;
- drag для перемещения и нижний правый маркер для resize;
- сетка и snapping;
- anchors, offsets, текст, tint, fill/text цвета, opacity, focus policy, visible/enabled,
  clip/read-only флаги, UV, NineSlice insets и значения ProgressBar/Slider;
- смена parent через combo или drag в дереве;
- duplicate и delete;
- undo/redo через `Ctrl+Z` / `Ctrl+Y`;
- сохранение в `assets/ui/*.toml` и загрузка обратно.

Preview показывает прямоугольники и selection, а не живой PinkUI-рендер: это инструмент
выравнивания геометрии до запуска игры.

## Command palette

Нажмите `Ctrl+P` и введите фильтр:

- Save scene;
- Build & Pack Game;
- Content Browser;
- Console;
- Material Graph;
- VisualScript;
- Script Debugger;
- Tilemap;
- UI Layout;
- Render Tuning.

## Отладка PIE

Редактор передаёт живой `ScriptRuntime` в Script Debugger. В PIE отладчик работает с
`pie.World()`; если ничего не выбрано — берёт первую сущность с `ScriptComponent`. Пауза
отладчика ставит PIE и граф на паузу; продолжение снимает только эту паузу, ручная пауза PIE
не затрагивается.

## Live-breakpoints и build target

Script Debugger привязывается к live-программе выбранной сущности (`ScriptComponent`) во время
PIE: breakpoints, условия, locals и call stack относятся к тому же bytecode, который исполняет
игра. Coroutine-программы идут тем же debug-путём и сохраняют suspend state; AOT-модули на время
отладки исполняются через bytecode VM; native stepping недоступен.

Build & Pack показывает target `desktop` / `mobile`. `mobile` передаёт `--target mobile` в
`fby_fempak` и кукает ASTC-ассеты; это asset-cook target, а не экспорт Android/iOS executable
и не подпись.

## Surface normals

Освещаемый спрайт может иметь `SurfaceNormal2D`:

```toml
[[entity.components]]
type = "SurfaceNormal2D"
Normal = [0.0, 1.0]
Strength = 1.0
```

Солвер 2D-света делает dot направления на свет с этой нормалью и смешивает вклад по `Strength`.
Это per-sprite surface-normal освещение; texture normal maps используют per-pixel путь ниже.


## Texture normal maps

Normal map — обычная текстура с линейным импортом. Называйте файлы с `_n`, `_normal` или
`.normal`; импортёр сам ставит `srgb = false`. Normal map должен лежать в том же texture array,
что и спрайт. Добавьте `NormalMap2D`:

```toml
[[entity.components]]
type = "NormalMap2D"
Layer = 1
Strength = 1.0
```

`Layer` — слой texture array; слой 0 отключает путь. Sprite shader восстанавливает нормаль из
RGB, применяет `Strength` и считает свет по пикселям. D3D11 использует до 64 point lights,
D3D12 и Metal — четыре, Vulkan — cluster-light путь с фиксированным directional fallback при
выключенном cluster lighting. Inspector показывает выбранный normal layer как превью.

`SurfaceNormal2D` остаётся дешёвым per-sprite fallback.
