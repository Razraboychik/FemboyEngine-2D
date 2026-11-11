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
- `windows-release` — clang-cl, Vulkan, редактор и тесты;
- `linux-release` — clang++, Vulkan, редактор и тесты;
- `macos-release` — clang++, Metal, редактор и тесты.

Проверены на этой машине:

```text
cmake --preset dev-clang
cmake --build --preset dev-clang --target fby_frame_composer_test
cmake --build --preset dev-clang --target fby_editor_app

cmake --preset dev-clangcl
cmake --build --preset dev-clangcl --target fby_frame_composer_test
```

Оба `fby_frame_composer_test` проходят.

Матрица desktop использует `windows-release`, `linux-release` и `macos-release`.
Каждый пресет собирает редактор, sandbox, инструменты и упаковщик игры. Редактор
создаёт `bin/fby_editor`; Build & Pack Game вызывает `bin/tools/fby_fempak` на
всех трёх системах. Статус проверки — в
[platform support](platform-support.md).

## Выбор компилятора

0.4 alpha проверяет четыре компилятора. Минимальные версии — это проверенные
релизы текущей линии, а не старые compatibility-цели.

| Компилятор | Платформа | Минимум | Для чего |
|---|---|---|---|
| clang-cl | Windows | 23.1.4 | основной Windows-билд, PDB и отладка Visual Studio |
| MSVC (`cl.exe`) | Windows | 19.44 | команды, стандартизированные на Microsoft-тулчейне |
| clang++ | Linux/macOS/Windows | 23.1.4 | кросс-платформенный паритет, основной Unix-билд |
| GCC | Linux/MinGW | 16.2.0 | второй компилятор и проверка переносимости |
| AppleClang | macOS | 21.0 (Xcode 26.4) | нативная macOS/Metal-сборка |

- **Windows:** clang-cl для эталонной сборки. MSVC — если проект должен
  собираться в нативном Visual Studio-тулчейне. Оба используют Windows SDK и
  стандартную библиотеку MSVC.
- **Linux:** clang++ — эталон; GCC 16.2.0 — второе мнение.
- **macOS:** AppleClang 21+ с Metal-бэкендом.
- **Один компилятор везде:** Clang. Только он покрывает Windows (clang-cl или
  clang++), Linux и macOS одним фронтендом.

Ручная конфигурация MSVC:

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
