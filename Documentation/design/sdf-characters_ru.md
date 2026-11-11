# SDF-персонажи

[English](sdf-characters.md) | [Русский](sdf-characters_ru.md)

Деформируемые 2D-персонажи используют SDF-поверхность, привязанную к костям, а
не цепочку distance LOD. Персонаж — фиксированный набор примитивов; каждый
примитив привязан к одной кости скелета и вычисляется через текущую глобальную
матрицу этой кости. Один и тот же путь работает на любом зуме и расстоянии.

## Данные

`Source/Engine/Physics/SdfCharacter.hpp`:

- `SdfBonePart` — `SdfShape`, индекс кости, локальный offset/rotation и радиус
  smooth-union;
- `SdfCharacterComponent` — до 64 `SdfBonePart`, trivially copyable;
- `SdfCharacterField` — плотная сетка сэмплов, полученная запеканием.

Компонент не хранит указатели. Он сериализуется и попадает в снапшоты как
остальные компоненты с фиксированным layout.

## Вычисление

`SdfCharacterDistance` переводит мировую точку через обратную матрицу кости,
вычисляет аналитическую форму и объединяет части через `SdfSmoothMin`.
`SdfCharacterGradient` считает центральные разности, `SdfCharacterCoversPoint`
проверяет знак. Стоимость — `O(parts)` на запрос и не зависит от расстояния до
камеры; переключения distance LOD нет.

`BakeSdfCharacterField` сэмплирует тот же расчёт в плотную сетку (не больше
512x512). `BuildSdfCharacterBrickMap` упаковывает её в существующее sparse-brick
представление, а `PackSdfAtlas` — в GPU-атлас. `BuildSdfCharacterMesh` выдаёт
triangle-мост из solid-ячеек для рендереров, использующих существующий mesh-путь.
`Rhi::SdfCharacterRenderCache::Build` обходит ECS в поисках
`SkeletonComponent + SdfCharacterComponent`, вычисляет позу, печёт поле
фиксированного разрешения и делает по одному мешу на персонажа. Передача
`cache.Meshes()` в `FrameComposer::BuildFramePacket` копирует их в unskinned
mesh-items пакета. Brick-атлас остаётся SDF/рендер-путём. Плотный bake — путь
авторинга и превью.

## Опциональный skinned mesh

`SkeletalAnimation::Deform` остаётся для частей, где меш удобнее (например,
текстура лица). SDF-путь предпочтителен для силуэта, коллизий и деформируемой
ткани/кожи, потому что не требует CPU-пересэмплирования вершин. Оба пути читают
один `SkeletonComponent`.

## Редактор

`Editor/Panels/SdfCharacterPanel` редактирует `PartCount`, индекс кости каждой
части, семейство формы и поля `SdfShape` у выбранной сущности. Панель печёт
фиксированное превью 48x48 и рисует его через ImGui draw list редактора, поэтому
разрешение превью не зависит от зума вьюпорта.

## Проверка

`fby_sdf_character_test` проверяет точки внутри/снаружи, направление градиента,
деформацию от позы кости, запекание поля, упаковку в brick/atlas, лимит сэмплов
и отсутствие переключения distance LOD.
