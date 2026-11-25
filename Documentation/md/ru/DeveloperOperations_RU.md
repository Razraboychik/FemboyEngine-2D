# Инструменты разработчика: Doctor, инциденты, provenance и replay

**Language section:** Русский

**Version:** 0.6.0-alpha.1
**Status:** Alpha

## 1. Project Doctor

`Ops::ProjectDoctor::Run` — read-only проверка проекта:

- VFS смонтирован и не пуст;
- файлы базовых шрифтов и их license id существуют;
- legal-манифест и документы загружаются;
- каждый `*.femeta` парсится;
- GUID ассетов уникальны;
- license id ассетов разрешаются.

Doctor ничего не исправляет и не пишет. Результаты: `Info`, `Warning`, `Error`.
`DoctorOptions::Strict` превращает предупреждения в ошибки.

## 2. Incident bundle

`Ops::WriteIncidentBundle` пишет локальный каталог:

```text
Incidents/<name>/
  README.txt
  manifest.toml
  build.txt
  engine.log
  settings.toml
  replay.fbyrply
  Licenses/
  Legal/
```

Запрос задаёт каталог, причину, build info, лимит логов и что включать.
`ArchivePath` дополнительно пишет store-only ZIP тех же файлов. Сетевой
отправки и скрытой телеметрии нет. При `incident_on_crash` редактор ставит signal-safe hook, который пишет только
`incident-request.txt` рядом с crash-отчётом. Следующий запуск редактора
сканирует `Saved/Crashes` и собирает полный bundle в безопасной точке.

## 3. Provenance и credits

`AssetMeta` хранит необязательные поля:

```toml
license = "CC-BY-4.0"
author = "Example Artist"
source_url = "https://example.invalid/source"
attribution = "Example Studio"
modified = true
```

`Ops::BuildCreditsMarkdown` формирует таблицу credits и точные тексты лицензий.
Отсутствующий license id даёт явную предупреждающую строку.

## 4. Deterministic replay

В движке уже есть `Core::ReplaySession`. `Ops::ReplayArchive` сохраняет и
загружает `.fbyrply`, `Ops::ReplayPolicy` задаёт бюджет кадров, интервал
keyframe и проверку при остановке. Политика хранится в проекте; запись
останавливается на бюджете, а не растёт без ограничений.

## 5. Настройки проекта

В `project.femproj` есть секция `[ops]`:

```toml
[ops]
doctor_on_open = true
doctor_strict = false
doctor_check_licenses = true
doctor_check_legal = true
doctor_check_fonts = false
incident_on_crash = false
incident_include_settings = true
incident_include_logs = true
incident_include_replay = true
incident_include_licenses = true
incident_archive = true
incident_max_log_bytes = 4194304
incident_directory = "Incidents"
replay_auto_record = false
replay_keyframe_interval = 60
replay_max_frames = 216000
replay_verify_on_stop = true
replay_directory = "Replays"
```

Project Settings показывает те же поля в разделе **Developer Operations**, с
кнопками **Run Project Doctor** и **Create Incident Bundle**. Дефолты
консервативные: Doctor при открытии только читает, crash bundles выключены,
запись replay выключена.

## 6. Правила универсальности

- все пути project-relative и настраиваемые;
- нет фиксированного места для incident/replay;
- нет сети и автоматической отправки;
- отсутствующие файлы репортятся, а не выдумываются;
- редактор — клиент Ops API, а не владелец;
- реальный signal-probe и synthetic crash-marker тест входят в CTest;
- игра может вызывать те же API без ImGui.

## 7. Ограничения

ZIP — store-only (без сжатия) и является необязательной копией каталога.
Crash-marker пишется через `CreateFileA`/`open` без аллокаций. Полный bundle
собирается только при следующем запуске, поэтому фатальный путь не аллоцирует.
Session lock покрывает процесс, убитый до crash-обработчика: обычный выход
удаляет lock, устаревший lock превращается в bundle «unclean shutdown».
Автосборка crash/unclean-shutdown создаёт и каталог, и ZIP. Doctor проверяет только видимые данные проекта
и не может доказать соблюдение внешних условий магазина или юридическую
корректность.