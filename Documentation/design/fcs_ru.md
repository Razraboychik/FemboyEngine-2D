# FCS: Femboy Component System

[English](fcs.md) | [Русский](fcs_ru.md)

FCS — внутреннее chunked-хранилище ECS линии 0.4 alpha и единственный runtime-бэкенд ECS; редактор, плагинный ABI и полный набор тестов используют его.
Рантайм использует собственный 32-битный handle `Fby::Entity`, поэтому сцены,
снапшоты и поля компонентов сохраняют layout без EnTT.

## Устройство

- `Source/Engine/ECS/FCS/World.hpp` — `World`, `Archetype`, `Chunk`, `Entity`.
- `Source/Engine/ECS/FCS/CommandBuffer.hpp` — отложенные структурные команды.
- `Source/Engine/ECS/FCS/Registry.hpp` — адаптер для движка с
  `create/destroy/emplace/remove/try_get/view/ctx/storage`.

Один архетип владеет сигнатурой и списком 16-кибибайтных чанков. У каждого
компонента своя непрерывная колонка; система идёт по колонкам в порядке чанков.
Wire-handle сущности — 20 бит индекса и 12 бит поколения, поэтому
переиспользованный слот отвергается 4096 циклов.

## Сборка с FCS

FCS — основной backend:

```bat
cmake -S . -B build -G Ninja ^
      -DCMAKE_CXX_COMPILER=clang++ ^
      -DFBY_BUILD_TESTS=ON
cmake --build build --parallel 8
```

Рантайм всегда собирает FCS-registry; EnTT удалён из дерева после фиксации
сравнительного baseline.

## Что уже проверено

FCS-бэкенд собирает движок, редактор, инструменты и плагинные фикстуры и проходит все 222
тестов CTest. Последними исправлены свежесть ссылок на компоненты в
геймплейных системах и зануление padding'а в physics-снапшотах, из-за которого
плавал rollback-digest. Основные тесты миграции проходят:

- `fcs_world`, `fcs_registry`;
- `p3_integration` (сцена, снапшот, UI, физика);
- `hierarchy`, `lighting`, `rollback_replay`;
- `net_replication_ecs`, `physics_backend`, `sdf_physics`;
- `text_shape_system`, `frame_composer`, `render_weather`, `anim_graph`,
  `sdf_character`.

Исторический baseline FCS-vs-EnTT на 100k сущностей и 50 проходах: FCS около
4.0 мс, EnTT около 15.1 мс на эталонной машине. Числа сохранены в
[performance baseline](performance-baseline.md); фикстура удалена.

## Что осталось

- рантайм использует `Fby::Entity`; EnTT и его third-party дерево больше не
  входят в сборку.

Редактор и плагинный ABI уже используют alias бэкенда; `FbyHostApi::registry`
остаётся opaque и передаётся без изменений.
