# Базовые шрифты и лицензии ассетов

**Language section:** Русский

**Version:** 0.6.0-alpha.1
**Status:** Alpha

## 1. Базовые шрифты движка

Движок поставляет небольшой набор шрифтов, которые можно свободно
распространять. Все лица — SIL Open Font License 1.1. Текст лицензии лежит в
`Assets/Licenses` и попадает в вывод сборки; OFL требует передавать лицензию
вместе со шрифтом, а не уведомлять авторов.

| Id | Шрифт | Скрипты / назначение | Лицензия |
|---|---|---|---|
| `inter.latin` | Inter Regular | латиница, греческий, кириллица | Inter-OFL |
| `inter.cyrillic` | Inter Regular | кириллический стек | Inter-OFL |
| `noto.hebrew` | Noto Sans Hebrew | иврит | Noto-OFL |
| `noto.arabic` | Noto Sans Arabic | арабский | Noto-OFL |
| `noto.devanagari` | Noto Sans Devanagari | деванагари | Noto-OFL |
| `noto.thai` | Noto Sans Thai | тайский | Noto-OFL |
| `noto.han` | Noto Sans SC | хань, paged-атлас | Noto-OFL |
| `press_start` | Press Start 2P | 8-битный display-текст | PressStart2P-OFL |
| `silkscreen` | Silkscreen | компактный пиксельный display | Silkscreen-OFL |
| `plex.mono` | IBM Plex Mono | код и отладка | IBMPlex-OFL |

`Text::MountBaseFonts` монтирует каталог в `TextFontStack`,
`Text::BaseFontCatalog` отдаёт те же данные инструментам. Набор покрывает
латиницу, греческий, кириллицу, иврит, арабский, деванагри и хань, но это не
заявление о поддержке всех письменностей. Для каны и хангыля нужен шрифт
проекта, пока не добавлено соответствующее Noto CJK-лицо.

Игра может поставлять свои TTF/OTF или `*.family.toml` как раньше. Свои лица
монтируются через `TextFontStack::AddFont` или `AddPagedFont`; путь базового
шрифта можно перекрыть ассетами проекта.

## 2. Реестр лицензий

`Assets::LicenseRegistry` читает текстовые файлы из VFS-каталога, по умолчанию
`Licenses`:

```text
Assets/Licenses/
  Inter-OFL.txt
  Noto-OFL.txt
  MyAsset-Author-Notice.txt
```

Поддерживаются `.txt`, `.md`, `.license`. Имя файла без расширения — id
лицензии. Реестр хранит полный текст; движок не вшивает его в код и не
переписывает.

```cpp
Assets::LicenseRegistry licenses;
licenses.LoadFromVfs(vfs, "Licenses");
std::string_view text = licenses.Text("MyAsset-Author-Notice");

licenses.BindAsset("images/hero.png", "MyAsset-Author-Notice");
std::string_view assetLicense = licenses.LicenseForAsset("images/hero.png");
```

`BindBaseFontLicenses` загружает базовые лицензии и привязывает каждый шрифт к
его id. Разработчик может добавить уведомление через `Add(name, text)`, если
текст генерируется или лежит вне обычного файла; менять C++ для обычных файлов
не нужно.

## 3. Вывод сборки

Сборка копирует `Assets/Licenses` в `build/<config>/bin/Licenses`. Игра может
читать эти файлы через тот же VFS и показать их в экране лицензий или вывести в
текстовый файл. Движок не требует конкретного UI: реестр — это data API.

## 4. Ограничения

- Каталог — курируемый набор, а не полный Unicode-шрифт.
- Для больших CJK-покрытий используется paged-атлас: первая страница резидентна,
  остальные грузятся по требованию.
- OFL требует распространять текст лицензии вместе со шрифтом, но не требует
  уведомлять авторов.
- Реестр лицензий поставляет текст, а не юридическую интерпретацию. За лицензии
  добавленных ассетов отвечает проект.