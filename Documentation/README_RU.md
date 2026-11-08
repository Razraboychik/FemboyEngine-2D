# Документация

[English](README.md) | [Русский перевод](README_RU.md)

Этот каталог — точка входа в документацию FemboyEngine2D. Текущая публичная
линия — **0.3.0-alpha.1**. Движок представляет собой data-oriented 2D-рантайм на
C++26: ECS-хранилища фиксированной ёмкости, явные системы, окно SDL3,
интерфейс рендеринга Vulkan или null, физика Box2D v3 и детерминированный
роллбэк.

Сгенерированные издания лежат в `md/`, `docx/`, `pdf/` и `djvu/`; исходные
рукописные документы — в этом каталоге, `design/`, `style/` и `adr/`.

## Что покрывает движок

- **Рантайм и ECS** — реестр EnTT, декларативный `SystemGraph`, контракты
  снапшотов и роллбэка, кадровый арена-аллокатор и командные буферы.
- **Рендер** — Vulkan и null RHI, спрайтовый и меш-путь, 2D-освещение с SDF-
  тенями, OIT, запекание статических слоёв, виртуальные текстуры, батчинг
  вызовов отрисовки, апскейлинг (классы FSR/DLSS) и генерация кадров.
- **Физика** — Box2D v3 и data-oriented AABB-бэкенд под одной политикой,
  настраиваемые снаряды, цепочки, ткань и вода, гибридные SDF-коллизии,
  SDF-разрушение с профилями материалов и связями, транспортные группы для
  лифтов, поездов и движущихся платформ, а также базу колёсного транспорта на
  wheel-джойнтах. Транспорт поддерживает вложенные носители, приваренные
  rigid-члены, контактную опору и конвейеры, движение по пути, динамические
  корни с фиксированным вращением или вращением физикой, сборку в редакторе,
  управление из скрипта и CCD Box2D на больших скоростях. Колёсный транспорт
  даёт подвеску, привод и тормоза по колёсам на Box2D; см.
  [Физика](md/ru/Physics_RU.md), [физика транспорта](design/transport-physics.md)
  и [колёса транспорта](design/vehicle-wheels.md).
- **Текст и документы** — Unicode-границы строк, слов и графем, шейпинг, стек
  раскладки текста, TXT-читатель и потоковый формат книг `.fbdoc`/`.fbybook`;
  см. [текстовый стек](design/text-stack.md), [документы](design/documents.md)
  и [потоковые книги](design/books.md).
- **Геймплей и скрипты** — байткод FemboyScript 2.0, LogicGraph, StateTree,
  текстовое подмножество `.fbs`, отладчик VM и cook-time AOT-бэкенд на C++26.
- **Сеть** — надёжный транспорт, протокол v2, репликация, сетки релевантности,
  лобби и детерминированный роллбэк.
- **Аудио и голос** — микшер и DSP-граф, пространственное аудио, захват, AEC,
  Opus, RNNoise и голосовые эффекты с условиями.
- **Ассеты и редактор** — база ассетов, `.femeta`, горячая перезагрузка, кукинг
  текстур и материалов, `.fempak`/`.femarc`, PinkUI, доступность, панели
  редактора и рабочий процесс проекта/сцены.

## Начните здесь

| Если вы хотите... | Читайте |
|---|---|
| Собрать движок и открыть редактор | [Начало работы](md/ru/FemboyEngineGettingStarted_RU.md) |
| Настроить VS Code, clangd и ручную сборку | [Рабочий процесс сборки](design/build-workflow_RU.md) |
| Изучить движок на примере игры | [Практическое руководство](md/ru/FemboyEngineApplicationGuide_RU.md) |
| Найти тип, функцию или поле payload | [Справочник API](md/ru/FemboyEngineAPIReference_RU.md) |
| Понять архитектуру | [Архитектурная спецификация](md/ru/FemboyEngineArchitecture_RU.md) |
| Прочитать полное руководство | [Полное справочное руководство](md/ru/FemboyEngineCompleteReference_RU.md) |
| Собрать физику, SDF-коллизии и разрушение | [Физика](md/ru/Physics_RU.md) |
| Собрать транспортные группы, лифты и поезда | [Физика: транспортные группы](md/ru/Physics_RU.md), [физика транспорта](design/transport-physics.md) |
| Добавить подвеску, привод или тормоза транспорта | [Колёса транспорта](design/vehicle-wheels.md) |
| Настроить движение и искажения камеры | [Эффекты камеры](design/camera-effects.md) |
| Добавить действия, контексты, геймпады и ремап | [Система ввода](design/input-system.md) |
| Построить авторитетный ввод и репликацию | [Практическое руководство: репликация](md/ru/FemboyEngineApplicationGuide_RU.md#51-детерминированный-ввод-и-репликация-при-создании-игры) |
| Использовать FemboyScript, LogicGraph или StateTree | [FemboyScript](md/ru/FemboyScript_RU.md) |
| Разобраться с FSR/DLSS-апскейлингом и генерацией кадров | [Генерация кадров](md/ru/FemboyEngineFrameGeneration_RU.md) |
| Показывать книги и руководства с форматированием | [Потоковые книги](design/books.md), [TXT-документы](design/documents.md) |
| Переносить проект между Windows, Linux и macOS | [Переносимость desktop-проектов](design/desktop-portability.md) |
| Настроить проект или редактор | [Настройки проекта](md/ru/ProjectSettingsRecommendations_RU.md), [Настройки редактора](md/ru/EditorSettingsReference_RU.md) |
| Выбрать диагностический режим вьюпорта | [Режимы вьюпорта](md/ru/ViewportModes_RU.md) |
| Соблюдать стилевой контракт C++ | [Стиль кода](style/CodeStyle.md) |
| Планировать дальнейшую работу | [Дорожная карта](ROADMAP.md) |
| Проектировать игровые теги, эффекты и навыки | [Дизайн навыков](design/skills.md) |
| Настроить ИИ, navmesh и формы путей | [ИИ и навигация](design/ai-navigation.md) |
| Настроить гравитацию, поля, верёвки и цепочки | [Гравитация и верёвки](design/gravity-and-ropes.md) |
| Собрать текстовый и голосовой чат, включая эффекты и условия | [Чат и голосовые эффекты](design/chat.md) |
| Проверить безопасность транспорта | [Безопасность](design/security.md) |
| Работать с голосом, светом, текстом, PIE или сетью | [Дизайн-документы](design) |

## Языки и форматы

Каждый сгенерированный документ выпускается в двух независимых изданиях:

- английский: `<Name>_EN.md`, `<Name>_EN.docx`, `<Name>_EN.pdf`,
  `<Name>_EN.djvu`;
- русский перевод: `<Name>_RU.md`, `<Name>_RU.docx`, `<Name>_RU.pdf`,
  `<Name>_RU.djvu`.

Совмещённого двуязычного вывода нет. Генераторы пишут только в `en/` и `ru/`
внутри каждого каталога форматов. Сгенерированные руководства двуязычны;
рукописные заметки `design/` — в основном англоязычные исходники для
разработчиков и не входят в поставку четырёх форматов; у `build-workflow` есть
английская и русская редакции. `CheckDocs.py` отклоняет устаревший каталог
`bilingual/`, непарный Markdown-файл и любой файл вне этой раскладки.
Английские и русские документы ведутся как отдельные переводы, а не как один
файл со смешанными языками.

Четыре формата получаются из одного источника: Markdown, Word, PDF и DjVu.
DjVu конвертируется из соответствующего PDF. Предпочтительный конвертер —
`pdf2djvu`; если его нет, инструменты растеризуют страницы через `pdftoppm` и
упаковывают их через `c44` и `djvm` из DjVuLibre. Без любого из этих путей
пропускается только шаг DjVu, а остальные три формата сохраняются.

В изданиях DOCX используются именованные языконейтральные стили (`FBY Title`,
`FBY Body`, `FBY Code` и другие), и каждый прогон несёт явный тег `w:lang`. PDF
экспортируется из DOCX соответствующего языка через LibreOffice Kit, поэтому
Word и PDF не расходятся.

## Исходные документы

| Каталог | Содержимое |
|---|---|
| `design/` | Безопасность, протокол v2, голос, чат и голосовые эффекты, сеть, освещение, текст, физические бэкенды, транспорт, колёса, эффекты камеры, ввод, PIE, доступность, время, рабочие процессы редактора, инструменты авторинга, мультимедийные модули, переносимость desktop-проектов, мировые системы и каталог виджетов |
| `style/` | `CodeStyle.md` и контракт именования CMake/C++ в `NAMING.md` |
| `adr/` | Записи архитектурных решений `NNNN-<slug>.md` |
| `Tools/` | Генераторы Markdown, DOCX, PDF и DjVu, проверки ссылок и сверки с кодом |

Сгенерированные руководства по C++ сохраняют исторические имена файлов
`FemboyEngine*`, чтобы существующие ссылки оставались рабочими. Название
продукта внутри документов — FemboyEngine2D.

## Поставка

| Документ | Markdown (EN / RU) | Word (EN / RU) | PDF (EN / RU) | DjVu (EN / RU) |
|---|---|---|---|---|
| Архитектура | [EN](md/en/FemboyEngineArchitecture_EN.md) / [RU](md/ru/FemboyEngineArchitecture_RU.md) | [EN](docx/en/FemboyEngineArchitecture_EN.docx) / [RU](docx/ru/FemboyEngineArchitecture_RU.docx) | [EN](pdf/en/FemboyEngineArchitecture_EN.pdf) / [RU](pdf/ru/FemboyEngineArchitecture_RU.pdf) | [EN](djvu/en/FemboyEngineArchitecture_EN.djvu) / [RU](djvu/ru/FemboyEngineArchitecture_RU.djvu) |
| Практическое руководство | [EN](md/en/FemboyEngineApplicationGuide_EN.md) / [RU](md/ru/FemboyEngineApplicationGuide_RU.md) | [EN](docx/en/FemboyEngineApplicationGuide_EN.docx) / [RU](docx/ru/FemboyEngineApplicationGuide_RU.docx) | [EN](pdf/en/FemboyEngineApplicationGuide_EN.pdf) / [RU](pdf/ru/FemboyEngineApplicationGuide_RU.pdf) | [EN](djvu/en/FemboyEngineApplicationGuide_EN.djvu) / [RU](djvu/ru/FemboyEngineApplicationGuide_RU.djvu) |
| Справочник API | [EN](md/en/FemboyEngineAPIReference_EN.md) / [RU](md/ru/FemboyEngineAPIReference_RU.md) | [EN](docx/en/FemboyEngineAPIReference_EN.docx) / [RU](docx/ru/FemboyEngineAPIReference_RU.docx) | [EN](pdf/en/FemboyEngineAPIReference_EN.pdf) / [RU](pdf/ru/FemboyEngineAPIReference_RU.pdf) | [EN](djvu/en/FemboyEngineAPIReference_EN.djvu) / [RU](djvu/ru/FemboyEngineAPIReference_RU.djvu) |
| Полное справочное руководство | [EN](md/en/FemboyEngineCompleteReference_EN.md) / [RU](md/ru/FemboyEngineCompleteReference_RU.md) | [EN](docx/en/FemboyEngineCompleteReference_EN.docx) / [RU](docx/ru/FemboyEngineCompleteReference_RU.docx) | [EN](pdf/en/FemboyEngineCompleteReference_EN.pdf) / [RU](pdf/ru/FemboyEngineCompleteReference_RU.pdf) | [EN](djvu/en/FemboyEngineCompleteReference_EN.djvu) / [RU](djvu/ru/FemboyEngineCompleteReference_RU.djvu) |
| Управление CMake-проектом | [EN](md/en/CMakeProjectManagement_EN.md) / [RU](md/ru/CMakeProjectManagement_RU.md) | [EN](docx/en/CMakeProjectManagement_EN.docx) / [RU](docx/ru/CMakeProjectManagement_RU.docx) | [EN](pdf/en/CMakeProjectManagement_EN.pdf) / [RU](pdf/ru/CMakeProjectManagement_RU.pdf) | [EN](djvu/en/CMakeProjectManagement_EN.djvu) / [RU](djvu/ru/CMakeProjectManagement_RU.djvu) |
| Начало работы | [EN](md/en/FemboyEngineGettingStarted_EN.md) / [RU](md/ru/FemboyEngineGettingStarted_RU.md) | [EN](docx/en/FemboyEngineGettingStarted_EN.docx) / [RU](docx/ru/FemboyEngineGettingStarted_RU.docx) | [EN](pdf/en/FemboyEngineGettingStarted_EN.pdf) / [RU](pdf/ru/FemboyEngineGettingStarted_RU.pdf) | [EN](djvu/en/FemboyEngineGettingStarted_EN.djvu) / [RU](djvu/ru/FemboyEngineGettingStarted_RU.djvu) |
| FemboyScript | [EN](md/en/FemboyScript_EN.md) / [RU](md/ru/FemboyScript_RU.md) | [EN](docx/en/FemboyScript_EN.docx) / [RU](docx/ru/FemboyScript_RU.docx) | [EN](pdf/en/FemboyScript_EN.pdf) / [RU](pdf/ru/FemboyScript_RU.pdf) | [EN](djvu/en/FemboyScript_EN.djvu) / [RU](djvu/ru/FemboyScript_RU.djvu) |
| Настройки проекта | [EN](md/en/ProjectSettingsRecommendations_EN.md) / [RU](md/ru/ProjectSettingsRecommendations_RU.md) | [EN](docx/en/ProjectSettingsRecommendations_EN.docx) / [RU](docx/ru/ProjectSettingsRecommendations_RU.docx) | [EN](pdf/en/ProjectSettingsRecommendations_EN.pdf) / [RU](pdf/ru/ProjectSettingsRecommendations_RU.pdf) | [EN](djvu/en/ProjectSettingsRecommendations_EN.djvu) / [RU](djvu/ru/ProjectSettingsRecommendations_RU.djvu) |
| Режимы вьюпорта | [EN](md/en/ViewportModes_EN.md) / [RU](md/ru/ViewportModes_RU.md) | [EN](docx/en/ViewportModes_EN.docx) / [RU](docx/ru/ViewportModes_RU.docx) | [EN](pdf/en/ViewportModes_EN.pdf) / [RU](pdf/ru/ViewportModes_RU.pdf) | [EN](djvu/en/ViewportModes_EN.djvu) / [RU](djvu/ru/ViewportModes_RU.djvu) |
| Настройки редактора | [EN](md/en/EditorSettingsReference_EN.md) / [RU](md/ru/EditorSettingsReference_RU.md) | [EN](docx/en/EditorSettingsReference_EN.docx) / [RU](docx/ru/EditorSettingsReference_RU.docx) | [EN](pdf/en/EditorSettingsReference_EN.pdf) / [RU](pdf/ru/EditorSettingsReference_RU.pdf) | [EN](djvu/en/EditorSettingsReference_EN.djvu) / [RU](djvu/ru/EditorSettingsReference_RU.djvu) |
| Физика | [EN](md/en/Physics_EN.md) / [RU](md/ru/Physics_RU.md) | [EN](docx/en/Physics_EN.docx) / [RU](docx/ru/Physics_RU.docx) | [EN](pdf/en/Physics_EN.pdf) / [RU](pdf/ru/Physics_RU.pdf) | [EN](djvu/en/Physics_EN.djvu) / [RU](djvu/ru/Physics_RU.djvu) |
| Генерация кадров | [EN](md/en/FemboyEngineFrameGeneration_EN.md) / [RU](md/ru/FemboyEngineFrameGeneration_RU.md) | [EN](docx/en/FemboyEngineFrameGeneration_EN.docx) / [RU](docx/ru/FemboyEngineFrameGeneration_RU.docx) | [EN](pdf/en/FemboyEngineFrameGeneration_EN.pdf) / [RU](pdf/ru/FemboyEngineFrameGeneration_RU.pdf) | [EN](djvu/en/FemboyEngineFrameGeneration_EN.djvu) / [RU](djvu/ru/FemboyEngineFrameGeneration_RU.djvu) |

## Перегенерация

Запускайте генераторы из корня репозитория в указанном порядке:

```bash
python Documentation/Tools/GenerateDocumentation.py --skip-pdf
python Documentation/Tools/GenerateApplicationGuide.py --skip-pdf
python Documentation/Tools/GenerateAPIReference.py --skip-pdf
python Documentation/Tools/GenerateCompleteReference.py --skip-pdf
python Documentation/Tools/GenerateSupplementaryDocs.py
python Documentation/Tools/GenerateProjectSettingsRecommendations.py
python Documentation/Tools/MarkdownDocs.py --all
python Documentation/Tools/GenerateReferenceDocs.py
```

Чтобы перегенерировать только руководство по физике во всех четырёх форматах и
на двух языках:

```bash
python Documentation/Tools/GenerateReferenceDocs.py --document Physics
```

`BuildDocs.py` выполняет всю последовательность, а затем `CheckDocs.py`:

```bash
python Documentation/Tools/BuildDocs.py --skip-pdf
```

Уберите `--skip-pdf`, чтобы экспортировать DOCX, PDF и DjVu. Для PDF нужны
`python-docx` и установленный LibreOffice Kit. Для DjVu нужен либо `pdf2djvu`,
либо инструменты `pdftoppm`, `c44` и `djvm` для растрового fallback. Скрипты
пропускают только шаг DjVu, если ни один из путей недоступен.

## Проверка

```bash
python Documentation/Tools/CheckDocs.py --strict
python Documentation/Tools/GenerateReferenceDocs.py --check
```

`CheckDocs.py` проверяет ссылки, пары английский/русский, совпадение структуры
глав и таблиц, четыре формата изданий, имена ADR и отсутствие двуязычного
дерева. `GenerateReferenceDocs.py --check` сверяет руководства по настройкам
редактора и режимам вьюпорта с исходниками C++.

## Добавление главы

Для сгенерированного документа добавьте одну запись в `SECTIONS` генератора с
английским заголовком и его русским переводом. Держите нумерацию глав
непрерывной и обновляйте перекрёстные ссылки в том же изменении. Сначала
перегенерируйте Markdown, затем DOCX и PDF, и запустите `CheckDocs.py` перед
коммитом.

Для рукописного Markdown-документа редактируйте пару `_EN.md` и `_RU.md`, затем
запустите соответствующий генератор. Не редактируйте файлы в `md/`, `docx/`,
`pdf/` или `djvu/` вручную; следующий прогон их перезапишет.
