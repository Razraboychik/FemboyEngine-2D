# Реестр настроек

Реестр настроек — это конструктор, с помощью которого игра объявляет все
настройки, которые она показывает. Движок не владеет категориями, значениями по
умолчанию, цветами или звуковыми шинами; это данные и callbacks разработчика,
зарегистрированные при старте.

## 1. Схема

```cpp
Fby::Settings::Registry Registry;
int Applied = 0;

Registry.Float("audio.master")
    .Category("Audio")
    .Label("Master")
    .Unit("dB")
    .Default(Fby::Settings::Value::MakeFloat(-3.0))
    .Range(-60.0, 0.0)
    .Scope(Fby::Settings::Scope::User)
    .Apply([&Applied](const Fby::Settings::Value& Value)
    {
        (void)Value;
        ++Applied;
    });

Registry.Bool("render.world")
    .Category("Rendering")
    .Default(Fby::Settings::Value::MakeBool(true));

Registry.Freeze();
```

`Freeze()` строит индекс стабильных id. После него нельзя добавить настройку.
Список дескрипторов остаётся неизменным до конца процесса.

## 2. Типизированные значения

Ядро поддерживает boolean, целое, число с плавающей точкой, цвет, ограниченную
строку и enum. Дескриптор объявляет тип, значение по умолчанию и диапазон.
Значение другого типа заменяется default; числа зажимаются; цвет зажимается по
каналам; enum обязан совпасть с объявленным вариантом.

## 3. Scope

```text
Project  значение и политика разработчика
User     per-install override игрока, только при UserEditable
Session  несохраняемое debug или cheat-значение
```

Эффективное значение выбирается так:

```text
session > user (если editable) > project > default дескриптора
```

Project-значение авторитетно, если настройка не редактируется игроком. Опасная
настройка не применяется без явного подтверждения.

## 4. Метаданные

Дескрипторы хранят категорию, label, tooltip, единицу, enum-варианты, диапазон,
scope и флаги:

```text
UserEditable
RequiresRestart
Dangerous
RequiresConfirmation
Advanced
Hidden
ServerAuthoritative
```

У `Dangerous` также есть текст подтверждения и hotkey восстановления. Реестр не
придумывает порядок отображения и цвета. UI игры решает, как показывать
категории.

Каждый дескриптор может хранить `LocalizationKey`; label — fallback разработчика,
а не переведённый текст. `MakeSnapshot()` фиксирует эффективные значения в
порядке дескрипторов для hot path. Runtime-система хранит целочисленные индексы
и читает `Snapshot::Float(index)`, `Bool(index)` или `ColorValue(index)` без
строкового поиска на кадр.

## 5. Apply callbacks

Setter сохраняет значение и для неопасной настройки сразу вызывает apply
callback. `Apply(id)` повторно применяет эффективное значение; `ApplyAll()`
проходит по замороженному списку. Опасные настройки требуют подтверждения и
применяются тем же callback после него.

## 6. Звуковые шины

Игра регистрирует настройку на каждую шину, а не полагается на фиксированные
master, SFX и music:

```cpp
for (const BusDef& Bus : GameBuses)
{
    Registry.Float("audio.bus." + Bus.Id)
        .Category("Audio")
        .Default(Fby::Settings::Value::MakeFloat(Bus.DefaultDb))
        .Range(-60.0, 6.0)
        .Scope(Fby::Settings::Scope::User)
        .Apply([Bus](const Fby::Settings::Value& Value)
        {
            Audio.SetBusVolumeDb(Bus.Id, static_cast<float>(Value.FloatValue));
        });
}
```

`ProjectSettings.audioBuses` хранит ту же топологию в `project.femproj`:

```toml
[[audio.buses]]
id = "voice"
name = "Dialogue"
parent = "master"
default_db = -2.0
```

Список шин — данные разработчика. Движок даёт реестр и DSP-граф, но не
навязывает фиксированный набор категорий. `Audio::RegisterAudioBusSettings`
регистрирует одну user-editable dB-настройку на каждую объявленную шину и
связывает её с `AudioSystem::SetBusVolumeDb`.

## 7. Цвета темы

Цветовые роли — обычные color-настройки:

```cpp
Registry.ColorSetting("theme.ui.accent")
    .Category("Theme")
    .Default(Fby::Settings::Value::MakeColor({0.3f, 0.6f, 1.0f, 1.0f}))
    .Scope(Fby::Settings::Scope::User);
```

Игра объявляет роли и темы. Игрок меняет только те роли, которые открыл
разработчик. Палитра движка не является авторитетной.

## 8. Safe mode

`RestoreSafeMode()` сбрасывает каждую опасную настройку к safe-значению и
удаляет user/session overrides для них. Это путь для запуска с `--safe-mode`,
hotkey восстановления или fallback после crash-loop. Обычные настройки не
затрагиваются.

## 9. Модель сгенерированной панели

`Editor::SettingsPanelModel` строит строки из замороженного span дескрипторов.
Он фильтрует hidden и advanced, применяет поиск по id, label и категории,
отдаёт `LocalizationKey`, confirmation text и recovery hotkey, а правки
направляет в выбранный scope. `Reset` удаляет override и повторно применяет
эффективное значение. `SettingsRegistryPanel` — ImGui-рендерер: группирует
строки по категориям, рисует типизированный widget, использует confirmation
modal для опасных значений и не хардкодит id или тип настройки. Микшер Project
Settings уже использует эту сгенерированную панель для master/SFX/music и
синхронизирует поля проекта после undo.

## 10. Хранение и UI

Project defaults, per-install user overrides, сгенерированная панель настроек и
данные звуковых шин — отдельные слои. Реестр — единая схема для них.
`SaveSettingsToml` и `LoadSettingsToml` пишут значения только одного scope и
игнорируют неизвестные ключи для forward compatibility. Редактор и UI игры
должны строить контролы из `Descriptors()`.

## 11. Текущие ограничения

- Реестр не создаёт звуковые шины; игра связывает bus id со своим DSP-графом.
- Живая панель настроек должна строиться поверх span дескрипторов.
- Настройка, требующая перезапуска, должна это объявлять; реестр отвергает
  позднюю запись только после первого apply.
- Опасные значения, загруженные из user-файла, отвергаются; только safe mode
  восстанавливает их.
