# Политика неиспользуемых алертов CodeQL

Эта заметка для разработчиков, которые видят результаты CodeQL в
`third_party/**` и думают, что это ошибки движка. Короткий ответ: эти файлы не
линкуются в движок и исключены не случайно.

## Что исключено

Текущая конфигурация CodeQL исключает только сгенерированные копии и
upstream-тесты, демо и утилиты:

| Путь | Почему не используется |
|---|---|
| `third_party/miniaudio-master/miniaudio-master/extras/miniaudio_split/**` | сгенерированная split-копия `miniaudio.h`; движок компилирует одну header-реализацию в `Source/Engine/Audio/MiniAudioImpl.c` |
| `third_party/opus-master/opus-main/tests/**` | upstream-тесты Opus |
| `third_party/opus-master/opus-main/celt/tests/**` | upstream unit-тесты Opus |
| `third_party/opus-master/opus-main/celt/opus_custom_demo.c` | демо Opus |
| `third_party/opus-master/opus-main/src/opus_compare.c` | утилита сравнения Opus |
| `third_party/opus-master/opus-main/dnn/adaconvtest.c` | тестовая утилита Opus DNN |
| `third_party/opus-master/opus-main/dnn/parse_lpcnet_weights.c` | утилита разбора весов Opus DNN |
| `third_party/opus-master/opus-main/dnn/dump_data.c` | утилита дампа Opus DNN |
| `third_party/flatbuffers-master/flatbuffers-master/tests/**` | upstream-тесты FlatBuffers |

Исключение лежит в `.github/codeql/codeql-config.yml`. Оно намеренно узкое: оно
не скрывает кодек Opus, HarfBuzz, ImGui, ImPlot, Monocypher, реальный header
miniaudio или любой другой компилируемый вендорный исходник.

## Правила для разработчиков

1. Не добавляй новый путь в исключения только чтобы уменьшить число алертов.
   Путь должен быть доказуемо вне всех build-таргетов.
2. Если код начинает включать один из этих файлов, сначала удали его шаблон и
   заново разбери алерты.
3. Не заменяй miniaudio, чтобы убрать 29 алертов split-копии. Split-копия
   генерируется из того же header; замена библиотеки — отдельный backend-проект.
4. `stb_image_write` больше не используется: инструменты и тесты движка пишут
   PNG через встроенный `Fby::Assets::WritePng`.
5. Используемые вендорные алерты патчатся или обновляются upstream; они не
   исключаются.

## Как проверить исключение

Конфигурацию читает CodeQL-workflow, который сканирует `main`. Проверь анализ и
убедись, что каждый оставшийся результат находится в компилируемом вендорном
файле:

```text
.github/codeql/codeql-config.yml
Documentation/design/codeql-audit.md
```

В audit-документе записаны последний анализ, правило и разделение между
компилируемыми и неиспользуемыми вендорными алертами.

## Когда политику менять

Пересматривай список при обновлении вендорной зависимости, добавлении нового
backend или когда исключённый файл становится частью таргета. Удаляй устаревшие
шаблоны в том же изменении, которое начинает использовать файл.
