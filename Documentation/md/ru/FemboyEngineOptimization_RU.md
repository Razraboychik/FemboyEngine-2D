# Оптимизация FemboyEngine2D

[English](../en/FemboyEngineOptimization_EN.md) | [Русский](FemboyEngineOptimization_RU.md)

Это единая карта всех средств оптимизации движка: бюджеты редактора, runtime-
переключатели рендера, кукинг ассетов, SDF-освещение, упаковка и инструменты
профилирования. Читайте после [build workflow](../../design/build-workflow_RU.md) и перед
релизом.

Числа ниже измерены на эталонной машине: Windows 11, RTX 4060 Laptop, 24
логических ядра, Vulkan, дашборд редактора 2560x1600. Это отправные точки, а не
обещание для другого железа.

## 1. Сначала измеряйте

| Инструмент | На какой вопрос отвечает | Где |
|---|---|---|
| Profiler (F7) | время кадра, строки по категориям, trace | панель `Profiler`, **Write trace** -> `Profiler/trace.json` |
| Crash Diagnostics | было ли падение и где отчёт | `Saved/Crashes/`, **Write report now** |
| Network Diagnostics | задержка rollback, потери, desync | панель сессии |
| Editor Settings **Query hardware** | CPU/RAM/GPU/VRAM машины | Settings > System |
| Accessibility QA | публикуется ли пригодное дерево | панель Accessibility QA |
| Отчёт `PackageDesktop` | размер бандла и пять крупнейших файлов | запуск с `--max-size-mb` |

Меняйте одну переменную за раз. Выигрыш по времени кадра, который просто
переносит работу в кукер или в скачивание, — не выигрыш.

## 2. Редактор: переключаемые профили и бюджеты памяти

Editor Settings > **Memory and performance** содержит профили. `Custom`
сохраняет отдельные значения; три пресета задают их вместе.

| Профиль | Арена undo | Команд undo | Дескрипторов превью | Лимит кадров | Throttle без фокуса |
|---|---|---|---|---|---|
| Lite | 2 МиБ | 128 | 64 | 30 FPS | вкл |
| Balanced | 4 МиБ | 256 | 128 | 60 FPS | вкл |
| High | 16 МиБ | 512 | 256 | без лимита | выкл |
| Custom | как задано | как задано | как задано | 0-240 | свободно |

Отдельные ключи:

| Ключ | По умолчанию | Действие |
|---|---|---|
| `undo_memory_mb` | 4 | арена undo; уменьшение перераспределяет её и очищает историю |
| `undo_command_limit` | 512 | максимум команд в окне undo |
| `thumbnail_cache_limit` | 256 | дескрипторы превью текстур Content Browser |
| `thumbnail_cache_enabled` | true | главный выключатель; освобождает дескрипторы |
| `frame_rate_limit` | 0 | усыпляет главный цикл; 0 — без лимита |
| `pause_when_unfocused` | false | ограничивает цикл 5 FPS без фокуса |
| `static_bake_budget_mb` (проект) | 64 | tiled-кэш вьюпорта |
| `viewport_complexity_budget` | 8 | опорная точка heat ramp сложности |

**Free undo history and thumbnails** освобождает оба кэша сразу. Снижение
бюджета undo намеренно очищает историю; данные проекта не меняются.

Замер дашборда: 411 МиБ working set / 675 МиБ private, 345 МиБ dedicated VRAM /
393 МиБ local, около 1.05 ядра, около 21% GPU. Постоянные затраты — swapchain,
SDF shadow mask, instance ring и static mesh arena. Lite помогает авторингу;
лимит кадров и throttle без фокуса снижают простой CPU/GPU.

### Переключатели рендера и авторинга

- **VSync**: выкл — меньше задержка ввода, вкл — меньше разрывов и энергопотребления.
- **Viewport mode**: диагностические режимы (complexity, overdraw, lighting)
  добавляют работу; используйте их для профилирования, а не для оценки игры.
- **Static bake**: повторно использует вьюпорт, пока сцена, камера и свет не
  изменились. Уменьшайте бюджет или выключайте на машинах с малым объёмом памяти.
- **Asset watcher/auto-refresh**: удобно, но на сетевых дисках дорого. Выключайте
  на батарее.
- **Шрифты и UI scale**: крупные шрифты перестраивают атлас; стабильный масштаб
  дешевле, чем смена каждый сеанс.
- **Логи**: держите `log_max_files`/`log_max_lines` ограниченными; непрерывное
  уплотнение журнала конкурирует с потоком ассетов.
- **История Profiler**: кольцо фиксированной ёмкости; меньше ёмкость — меньше
  кэша, график менее гладкий, но память не растёт бесконечно.

## 3. Игра: настройки, которые реально влияют

Политика релиза живёт в `ProjectSettings`/`project.femproj`. Настройки
редактора в игру не попадают.

| Ключ | По умолчанию | Стоимость / эффект |
|---|---|---|
| `render_scale` | 1.0 | внутреннее разрешение сцены; UI остаётся нативным. 0.75 — типичный выигрыш 1440p -> 1080p |
| `upscale_mode` | Bilinear | Nearest для pixel art, FSR1 для edge-aware апскейла |
| `upscaler_backend` | Auto | FSR/DLSS/MetalFX, неподдерживаемые падают в fallback |
| `upscale_policy` | Auto | Auto/Universal/Off; Off убирает проход апскейла |
| `allow_temporal_upscale` | false | временные апскейлеры и их motion vectors |
| `allow_frame_generation` | false | генерация кадров при наличии vendor-бэкенда |
| `oit_mode` | Off | Off дешевле; Weighted/Accurate добавляют проходы |
| `opaque_mode` | Auto | front-to-back opaque path; Auto только если все спрайты непрозрачны |
| `opaque_art_cutout` | false | жёсткая альфа; убирает блендинг, требует корректного арта |
| `static_bake_enabled` | false | кэширует неизменный вид; стоит памяти вьюпорта |
| `virtual_texture` | off | потоковая подгрузка страниц; стоит таблицы и I/O |
| `texture_format` | BC7 sRGB | BC7 desktop, ASTC mobile; RGBA8 — без сжатия и самый большой |
| `max_texture_size` | 2048 | жёсткий предел при импорте/кукинге |
| `vsync` | true | политика презентации |

Правила:

1. Сначала пробуйте `render_scale`: он масштабирует всю сцену равномерно и легко
   проверяется.
2. `opaque_mode=Auto` и корректный `opaque_art_cutout` для жёсткой альфы. Мягкая
   альфа остаётся в blend-пути.
3. OIT только для эмиттеров, которым нужен порядок; сцена, которую можно
   отсортировать по глубине, не должна за него платить.
4. Static bake — для паузы/неподвижной камеры, не для движущейся.
5. Virtual texturing — только если набор текстур превышает бюджет VRAM; иначе он
   добавляет индирекцию в каждый sample.
6. `max_texture_size` равен целевому разрешению. Источник 4K, импортированный в
   2048, экономит три четверти памяти и обычно неотличим на 1080p.

## 4. SDF-освещение и тени

- В сцене один `SdfSunComponent`; панель задаёт направление, softness, ambient,
  радиус/силу окклюзии и **steps**.
- `steps` — бюджет лучей (1-64). Уменьшение вдвое — наибольшая экономия SDF и
  более жёсткая полутень.
- Динамические окклюдеры собираются до `Rhi::kMaxSdfGpuOccluders` (16); лишние
  игнорируются, панель предупреждает.
- Статический свет кэшируется, пока сцена, окклюдеры или свет не изменились. Не
  помечайте движущийся факел статическим.
- Снижение разрешения/слоёв SDF shadow mask — бюджет проекта/бэкенда; используйте
  его раньше, чем удалять солнце.

## 5. ECS, фиксированный шаг и геймплей

- Частота FixedUpdate — политика проекта. 120 Гц детерминированы, но стоят вдвое
  дороже симуляции 60 Гц; `max_fixed_steps` ограничивает догоняющую работу.
- Сущности и компоненты — SoA фиксированной ёмкости. Переполнение сообщается, а не
  молча аллоцирует память.
- Physics backend `Auto` выбирает DOD для больших миров без джойнтов по
  `physics_body_budget`. Замеряйте оба варианта.
- Render extract не аллоцирует и ограничен бюджетом проекта. Увеличивайте его
  осознанно; overflow — не штатный путь.
- AOT-скрипты убирают VM из релиза и dispatch интерпретатора; измеряйте AOT до
  микрооптимизаций скрипта.

## 6. Кукинг ассетов и размер пакета

- Текстуры: BC7 (desktop) или ASTC (mobile), мипы и минимально приемлемый
  `max_texture_size`.
- Аудио: Opus; речь с меньшим битрейтом, чем музыка. Неиспользуемые WAV в релиз
  не нужны.
- `fby_fempak cook --exclude <substring>` убирает editor-only сэмплы, исходники и
  тестовые фикстуры. Совпадение ищется и по пути пака, и по source-пути.
- Локализация: только языки, которые игра показывает; таблица локали мала, а
  42-языковые голосовые паки — нет.
- `PackageDesktop.py --game` собирает runtime-only бандл. `--features` выбирает
  FSR/DLSS/XeSS; `--assets` кукит `game/assets.fempak`; `--max-size-mb` — жёсткий
  лимит с отчётом пяти крупнейших файлов.
- Замер: редакторский бандл 199.9 МиБ (в основном vendor-апскейлеры), игровой без
  vendor — 15.4 МиБ до ассетов.

## 7. Параметры сборки

| Параметр | По умолчанию | Для чего |
|---|---|---|
| `CMAKE_BUILD_TYPE=Release`/`RelWithDebInfo` | - | Release для игроков, RelWithDebInfo для тестеров |
| `FBY_ENABLE_LTO` | OFF | межмодульный инлайн; медленнее линк, меньше/быстрее код |
| `FBY_PGO_GENERATE`/`FBY_PGO_PROFILE` | OFF | profile-guided layout под известную нагрузку |
| `build.optimization` (`O2`/`O3`/`Ofast`) | O2 | O3 для горячих сборок; Ofast обязан соблюдать детерминизм |
| `build.lto` | true в сгенерированных проектах | то же для проекта |
| `build.strip_debug_info` | false | убрать PDB/символы из публичной сборки |
| `build.unity_build` | false | меньше TU, быстрее сборка, хуже инкрементальность |
| `build.native_arch` | false | `-march=native`; не поставлять на неизвестные CPU |
| `FBY_CXX_STANDARD` | 23 | 26 экспериментален; runtime тот же |

Не поставляйте `native_arch`, если целевой CPU неизвестен. Не смешивайте
санитайзеры и release-объекты в одной игре.

## 8. Генерация кадров и апскейлеры

- Пространственные апскейлеры (FSR1, bilinear) не имеют temporal-истории и
  motion vectors; они дешёвы и устойчивы.
- Временные апскейлеры требуют motion vectors и стабильной камеры; стоят полосы и
  могут давать ghosting на pixel art — тестируйте на реальном арте.
- Frame generation — vendor-функция по возможностям. Она улучшает плавность, но не
  отзывчивость симуляции. Сетевая игра не должна зависеть от сгенерированных
  кадров для задержки ввода.
- `upscaler_backend=Auto`, если нет причины иначе: выбор фильтрует
  неподдерживаемые бэкенды и делает fallback.

## 9. Чеклисты бюджетов

**Lite / ноутбук редактора**

- Профиль Lite, лимит 30 FPS, throttle без фокуса.
- VSync вкл, UI scale фиксирован, watcher выкл на батарее.
- Static bake выкл, если сцена не статична.
- Undo 2 МиБ/128, превью 64 или выкл.

**Balanced / настольный редактор**

- Профиль Balanced, лимит 60 FPS.
- Превью 128, undo 4 МиБ/256.
- Static bake вкл для PIE, выкл при редактировании движущейся сцены.

**Релизная 2D-игра**

- `render_scale` 0.75-1.0, FSR1 или native, VSync по политике платформы.
- BC7 + мипы, max texture size = целевое разрешение, Opus.
- `game`-бандл без редактора/инструментов; только используемые апскейлеры.
- `--max-size-mb` в CI; смотреть пять крупнейших файлов каждый релиз.
- Профилировать на минимальной машине, а не на рабочей станции разработчика.
- Проверять время кадра и аллокации в RelWithDebInfo, затем подтверждать в
  Release; Debug-цифры не относятся к релизу.



## 10. Варианты sprite pipeline

Спрайты батчатся по явному shader-варианту, а не по per-pixel проверке режима:

- base;
- normal map;
- SDF text;
- SDF outline;
- SDF glow.

Вариант входит в render key. `FrameComposer` сортирует по нему, поэтому batch не
смешивает base-спрайт с normal-mapped или SDF-спрайтом. D3D11 и D3D12 используют
отдельные base/normal pipelines; D3D12 и Vulkan — SDF pipeline без записи глубины;
Metal использует то же разделение на уровне исходников.

D3D12 SDF можно отключить через `FBY_D3D12_DISABLE_SDF=1`; тогда используется
обычный sprite pipeline. SDF smoke-тест рендерит реальный кадр и проверяет центр
глифа, поэтому несовпадение pipeline не проходит молча.

## 11. Связанные документы

- [Editor settings reference](EditorSettingsReference_RU.md)
- [Project settings recommendations](ProjectSettingsRecommendations_RU.md)
- [Viewport modes](ViewportModes_RU.md)
- [Virtual texturing](../../design/virtual-texturing.md)
- [Upscaler selection](../../design/upscaler-selection.md)
- [Custom frame generation](../../design/custom-frame-generation.md)
- [Pause and static rendering](../../design/pause-and-static-rendering.md)
- [Risky RHI optimizations](../../design/risky-rhi-optimizations.md)
- [Performance baseline](../../design/performance-baseline.md)
- [High-resolution benchmark](../../design/high-resolution-benchmark.md)
- [FCS storage](../../design/fcs.md)
- [Physics backends](../../design/physics-backends.md)
