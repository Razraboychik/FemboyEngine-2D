# Gameplay tags, attributes, effects and skills

This document describes the manual gameplay layer. There is no engine-owned tag
enum: every project authors its own tag tree in Project Settings and keeps the
returned ids.

## Project database and storage

Project Settings -> Gameplay Tags edits one `GameplayTagDatabase`. The database
is saved in `project.femproj` as `gameplay_tags`, and the editor rebuilds the
runtime `GameplayTagRegistry` from it on Apply. The canvas inspectors store tag
paths; the loader resolves them to ids against the same registry.

Definitions stay project-global. Per-entity state stays on the entity:

- `GameplayTagsComponent` stores the effective and base masks;
- `GameplayAttributesComponent` stores fixed base/current arrays;
- `GameplayEffectsComponent` and `GameplaySkillsComponent` store fixed records.

This keeps ECS iteration packed and DOD-friendly: no entity owns a tag object,
and SDF/world data is baked separately from gameplay values.

## Tags

Tags are registered by path and numbered densely from one:

```cpp
Fby::GameplayTagRegistry Tags;
const Fby::GameplayTag Character = Tags.Register("Character");
const Fby::GameplayTag Stunned = Tags.Register("Character.State.Stunned");
// Registering a path also registers every parent path.
```

`GameplayTagContainer` stores an exact bitset. Queries go through the registry
so a parent tag matches its descendants:

```cpp
Fby::GameplayTagContainer Mask;
Mask.Add(Stunned);
Fby::GameplayTagContainerHas(Mask, Tags, Character); // true
```

`GameplayTagsComponent` keeps two containers. `BaseTags` is authored gameplay
state; `Tags` is the effective set after active effects granted their tags. A
task only changes `BaseTags`; the effect system rebuilds `Tags`.

## Attributes

`GameplayAttributeRegistry` registers names such as `Health`, `Mana` or
`MoveSpeed`. `GameplayAttributesComponent` stores 32 base and current values.
`RebuildGameplayAttributes` computes:

```
current = (base + sum(Add)) * product(Multiply)
```

An `Override` modifier wins last and is independent of modifier order. Instant
effects write `Base`; duration effects contribute modifiers while active.

## Effects

`GameplayEffectSystem` owns a fixed table of `GameplayEffectDef` records:

- instant effects apply their modifiers once and disappear;
- duration effects keep an active record with a remaining time;
- periodic effects apply their modifiers once per `Period`;
- stack policies are `Refresh`, `Stack` and `Independent`;
- each effect grants up to four tags while active.

Active records are 20 bytes and the component has no padding, so the snapshot
codec is a plain copy. Events (applied, removed, periodic) are published into a
fixed queue for audio, UI, AI and network code.

## Skills

`GameplaySkillSystem` owns up to 64 skill definitions and four runtime slots per
entity. A definition carries:

- cooldown and cast time;
- an optional attribute cost;
- required and blocked tag containers;
- up to three self effects and three target effects;
- target kind, animation tag and cue id.

`TryActivate` validates tags, target and cost, pays the cost through the
attribute base, starts a cast or completes immediately. `Update` advances
cooldowns and casts, cancels a cast when a blocked tag appears, and applies the
effect ids on completion. Every result is reported through
`GameplaySkillEvent`.

## StateTree integration

`StateTree` reads tags from `GameplayTagsComponent` instead of copying a mask
into its own component. Tag,
attribute and skill evaluators resolve hierarchy and live values through the
same registry:

- `GameplayTagsAny`, `GameplayTagsAll`, `GameplayTagsNone`;
- `AttributeBelow`, `AttributeAbove`;
- `SkillReady`.

The `ActivateSkill` task emits an `AICommandKind::ActivateSkill` command instead
of executing gameplay directly, so prediction and replay see a single input
stream. `StateTreeSystem::SetTagRegistry` or
`SystemContext::gameplayTags` supplies the registry.

The animation graph transition tags use up to two required and two any-tags per
transition and resolve them through the same registry.

## Fixed-step and snapshot rules

`GameplayEffectSystem::Update` and `GameplaySkillSystem::Update` are
allocation-free and run once per fixed tick. `SnapshotSchema::BuiltIns` registers
raw codecs for all four gameplay components; schema registration and CommandLog
use the component type ids. Rollback checksums fold tag words, current attribute
values, active effects and skill slots.

## Editor note

Both canvases author tags by path. The animation inspector accepts up to two
any-tags and two all-tags per transition; the AI inspector accepts the
evaluator, task and transition tag lists. Paths resolve through the project
database, so renaming a tag in Project Settings and re-saving a scene keeps the
reference valid.
