# Сборка и распространение игры

[English](game-distribution.md) | [Русский](game-distribution_RU.md)

Это руководство покрывает shipping-сборку и точки интеграции со Steam, Epic
Games Store (EOS) и GOG. Оно отдельно от сборки редактора: в shipped-игру не
входят редактор, ассет-инструменты и тесты.

## Shipping-сборка

```bat
cmake -S . -B build-ship -G Ninja ^
      -DCMAKE_BUILD_TYPE=Release ^
      -DFBY_BUILD_EDITOR=OFF ^
      -DFBY_BUILD_TESTS=OFF ^
      -DFBY_BUILD_TOOLS=OFF ^
      -DFBY_BUILD_SANDBOX=OFF ^
      -DFBY_BUILD_SERVER=OFF
cmake --build build-ship --parallel 8
```

Рантайм линкует `FemboyEngine2D::Engine`. Редактор, кукер ассетов и тестовые
цели в shipped-архив не входят.

## Упаковка

`Source/Tools/Dev/PackageDesktop.py` собирает рантайм, ассеты, notices и плагин
платформенного сервиса в один каталог. Кукленный контент пишет `fby_fempak`;
шаг упаковки никогда не запускает редактор.

- Windows: подписать исполняемый файл и DLL платформенного сервиса после упаковки.
- Linux: зависимость от Vulkan loader указывать в манифесте магазина, а не в архиве.
- macOS: подписать и нотаризовать `.app`; Metal-бэкенд не требует Vulkan runtime.

## Steam

- Steamworks SDK подключается через `Fby::Platform::PlatformService`; движок не
  линкует SDK в рантайм напрямую.
- `SteamAppId`, достижения, статистика, cloud saves и Steam Input отображаются
  на интерфейс платформенного сервиса.
- Steamworks redistributable не хранится в дереве исходников и не входит в
  MPL-архив; он указывается в `THIRD_PARTY_NOTICES.md`.
- Один депот на платформу; null RHI остаётся доступным для headless-серверов.

## Epic Games Store / EOS

- EOS используется для достижений, сессий, лобби и cloud saves.
- EOS SDK — опциональный платформенный сервис; сборка без него должна
  запускаться и играть офлайн.
- Оверлей EOS и жизненный цикл окна движка разделены: оверлей принадлежит
  платформенному сервису.

## GOG

- Интеграция GOG Galaxy SDK идёт через тот же интерфейс: достижения, cloud
  saves и playtime.
- DRM-free-сборка не должна требовать клиент магазина в рантайме; Galaxy-сервис
  опционален.
- Офлайн-сохранения используют тот же путь, что и остальные магазины.

## Матрица магазинов

| Возможность | Steam | EGS / EOS | GOG |
|---|---|---|---|
| Достижения | Steamworks | EOS | Galaxy |
| Cloud saves | Steam Cloud | EOS Player Data Storage | Galaxy Cloud |
| Оверлей | Steam overlay | EOS overlay | Galaxy overlay |
| Ввод | Steam Input | SDL/gamepad | SDL/gamepad |
| DRM-free сборка | да | да | да |

## Лицензии

Движок распространяется по MPL 2.0. В shipped-архив входят `LICENSE` и
`THIRD_PARTY_NOTICES.md`; SDK магазинов опциональны и указываются в notices,
если линкуются или распространяются.
