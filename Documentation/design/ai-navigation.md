# AI, navigation and paths

This document describes the AI runtime that connects authored state trees to
movement and skills, and the navigation/path-shaping tools it uses.

## Runtime pipeline

```
StateTreeSystem
    -> AICommandComponent (buffered)
    -> CommandBuffer::Play
    -> AIControllerSystem
    -> NavAgentSettings / GameplaySkillSystem
    -> NavigationSystem
    -> Transform2D
```

`AIPerceptionSystem` runs before `StateTreeSystem` and writes a perceived
`StateTreeComponent::Target`. Perception is tag-driven: `TargetTagId` names the
tag that marks a valid target, so the project decides whether that is
`Team.Player`, `Faction.Enemy` or something else. A zero tag accepts any entity
with a `Transform2D`.

`StateTreeSystem` never moves an entity and never applies damage. It emits one
intent:

- `MoveTo` sets a navigation target;
- `Stop` clears the active path;
- `Attack` approaches a target and stops at the task range; it deliberately emits
  no combat event, so a project without AI combat simply never uses it;
- `ActivateSkill` calls `GameplaySkillSystem::TryActivate` once per tree leaf,
  or at `AIAgentSettings::SkillRetryInterval` when that is non-zero.

Damage, hit scans and projectiles are skills/effects owned by the game. The AI
layer has no attack queue, no damage type and no combat cooldown of its own.

## Developer actions

Anything game-specific is a developer action, not an engine enum value. The
StateTree leaf stores an `ActionId`; `StateTreeSystem` writes a normal
`AICommandComponent`:

```cpp
AICommandComponent{Kind = Action, ActionId = 1, Target, Position, Flags, ...};
```

`AIControllerSystem` does not dispatch it and the engine has no callback table.
The game builds its own infrastructure over the stable data:

```cpp
// Game system, FixedUpdate after AI.
auto View = Registry.view<AICommandComponent>();
for (Entity E : View) {
    const AICommandComponent& Cmd = View.get<AICommandComponent>(E);
    if (Cmd.Kind != Action || Cmd.ActionId != AttackActionId) continue;
    // The game decides: cooldown, hit scan, projectile, damage, animation.
}
```

This is deliberately data, not a function pointer: actions survive snapshot,
replay, PIE, network and hot reload the same way as any ECS component. A game
can implement a table, a behavior graph, a script VM or a data asset on top of
`ActionId`; the engine does not choose. The editor exposes only the numeric
`Action id`; its meaning belongs to the game system. `Sequence` and `TaskIndex`
let a handler deduplicate a command that is re-emitted every fixed tick, and the
handler owns its own cooldown/timing.

## Navigation map

`NavMap` is a fixed 128x128 cost grid. `BuildFromTilemap` copies solid tiles as
walls, and `PagedNavGrid::BakeFromWorld` bakes an SDF world with an agent radius
and a near-wall cost. Large worlds can use the paged grid plus `FlowField`
instead of the single `NavMap`.

`NavMesh` is the polygon representation used for smooth movement. It builds a
non-overlapping greedy rectangle decomposition of the walkable cells and a
portal graph between adjacent rectangles. A query runs A* over rectangles and a
funnel over portals, producing a taut point path. A blocked or degenerate
funnel falls back to portal midpoints plus line-of-sight simplification.

Both representations are built from the same cost grid, so SDF baking, tilemap
walls and authored costs behave identically.

## Path shaping

Every agent can carry a `NavAgentPathSettings` component:

| Field | Meaning |
|---|---|
| `Smoothing` | `None` (raw A* polyline), `LineOfSight`, `Chaikin`, `CatmullRom` |
| `SmoothingIterations` | Curve passes, 1..8 |
| `SmoothingStrength` | Blend between the raw corners and the full curve |
| `RandomizeRoute` | Adds deterministic edge-cost noise to A* |
| `RandomCost` | Strength of that noise |
| `RandomWaypointCells` | Jitter of interior points in cell units |
| `RandomSeed` | Makes the random route repeatable for replay |

`None` is the shortest cell polyline. `LineOfSight` removes redundant stair
steps. `Chaikin` and `CatmullRom` round the remaining corners. Every generated
segment is validated against the walkable grid; a curve that would cut a wall
falls back to the validated polyline. Jitter is validated the same way.

Random route noise is an A* edge cost, not a post-process, so different seeds
produce different but still sensible corridors. The same seed and query produce
the same path on every peer.

## Agent components

| Component | Purpose |
|---|---|
| `NavAgentSettings` | Target, speed, arrival radius, repath interval/distance, active flag |
| `NavAgentComponent` | Runtime path, path index and blocks; created by the system |
| `NavAgentPathSettings` | Optional smoothing/randomization policy |
| `AIAgentSettings` | Skill retry interval (combat timing belongs to the game) |
| `AIPerceptionSettings` | Radius, interval and target tag |
| `AIControllerComponent` | Runtime command history and timers |

An entity becomes an AI agent with `StateTreeComponent`, `NavAgentSettings` and
`AIAgentSettings`; the controller creates its runtime components on the first
command. A perception agent additionally gets `AIPerceptionSettings`.

## Project tag database

The tag tree is authored once in **Project Settings -> Gameplay Tags** and saved
in `project.femproj` as `gameplay_tags`. Each project has one database; the
editor rebuilds `GameplayTagRegistry` from it on Apply, and PIE shares the same
registry.

Definitions are global because every system must agree on ids. Values are not:
`GameplayTagsComponent` and `GameplayAttributesComponent` are fixed-size
per-entity records, so systems iterate packed arrays instead of chasing a
per-tag object. This is the ECS/DOD choice: project data in the registry, entity
state in components, and SDF/world data in the world asset.

The AI and animation canvas inspectors author tags by path. The project tag
editor resolves those paths to ids when a scene is loaded; a missing path is
reported instead of silently creating a different tag.

## Limits

- `NavMap`: 128x128 cells.
- `NavMesh`: 1024 rectangles, 4096 portals.
- `NavPath`: 128 points/cells.
- AI attack queue: 128 events per fixed step.
- Skill definitions: 64, slots per entity: 4.
