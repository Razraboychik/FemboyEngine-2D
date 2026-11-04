# Physics connections: chains, breakage and springs

This document defines the joint graph that `Source/Engine/Physics/PhysicsJoints.hpp`
builds on top of Box2D v3. A chain is a builder that creates links plus
connections, and a connection is the unit of material, strength, damage and
repair. The contract below is what gameplay, tools and tests rely on.

## 1. Data model

The system owns three kinds of state:

- **Links** are ECS entities with `RigidBody2D`, `Collider2D`, `Transform2D` and a
  `JointLinkComponent` marker. A link is a capsule that spans one path segment.
- **Connections** live in one fixed pool (`PhysicsJointSystem::kMaxConnections`,
  4096 slots). A slot holds the packed `b2JointId`, its endpoints, its material,
  its break and damage limits, its spring parameters and its runtime values. A
  connection outlives a break so it can be repaired.
- **Chains** own a slot, a stable `ChainId`, a list of links and a list of
  connection slots. Ties are system-owned connections between two chain links or
  two plain rigid bodies.

`JointChainHandle` identifies a chain; `JointConnectionHandle` identifies a
connection. Both carry a generation, so a stale handle fails instead of
addressing a recycled slot. `FindChain(ChainId)` resolves the stable id back to a
live handle after a save or a script round trip.

### Materials

`JointConnectionKind` is the material of one connection:

| Kind | Box2D joint | Typical use |
| --- | --- | --- |
| `Hinge` | revolute, angle limits and motor | rigid chain link |
| `Rope` | distance, rigid | rope span or knot |
| `SpringRope` | distance with `enableSpring` | elastic rope, bungee |
| `SpringHinge` | revolute with `enableSpring` | spring-loaded joint |

`JointChainKind` (`Rope` or `Chain`) remains the authored default for a chain
that has no explicit sections.

### Sections

`JointChainSection` describes a run of links:

```cpp
struct JointChainSection
{
    JointChainKind Kind = JointChainKind::Chain;
    // JointChainSpringRope or JointChainSpringHinge
    std::uint32_t Flags = 0;
    // Links in this run; the chain has sum(LinkCount) links
    std::uint32_t LinkCount = 1;
    // Pixels; zero reuses JointChainDesc::Radius
    float Radius = 0.0f;
    // Relative link length for a generated path; ignored when a path is given
    float LinkLengthScale = 1.0f;
    // Newtons; zero reuses JointChainDesc::BreakForce
    float BreakForce = 0.0f;
    // Newton metres; zero reuses JointChainDesc::BreakTorque
    float BreakTorque = 0.0f;
    // Newtons; zero leaves the Box2D spring force range unbounded
    float LowerSpringForce = 0.0f;
    float UpperSpringForce = 0.0f;
    // Damage per second at full overstress; zero breaks instantly
    float DamageRate = 0.0f;
    // Stress ratio that starts accumulating damage
    float DamageThreshold = 1.0f;
};
```

A connection between two adjacent links takes the softer material of the two
sections, so the boundary between a chain run and a rope run is a rope knot and
stays free to rotate. Its break limit is the weakest **positive** limit of the
two sections, falling back to the chain defaults. Spring range and damage
parameters follow the same rule: inside one section the section value wins, on a
boundary the chain default wins. `LinkLengthScale` is applied only when the chain
is generated from `Start`/`End`; a `Path` supplies the exact geometry.

## 2. Building a chain

A uniform rope:

```cpp
JointChainDesc Desc;
Desc.Kind = JointChainKind::Rope;
Desc.Flags = JointChainPinStart;
Desc.Start = Vector2D{0.0f, -64.0f};
Desc.End = Vector2D{0.0f, 64.0f};
Desc.SegmentCount = 8;
Desc.Radius = 2.0f;
Desc.BreakForce = 400.0f;

const JointChainHandle Handle = Joints.CreateChain(Registry, Desc, &Error);
```

A mixed object splices a rope between two rigid runs:

```cpp
const JointChainSection Sections[] = {
    // Kind, flags, links, radius, length scale, break force, break torque
    {JointChainKind::Chain, 0, 4, 4.0f, 1.0f, 4000.0f, 500.0f},
    {JointChainKind::Rope, JointChainSpringRope, 6, 1.5f, 0.5f, 900.0f, 0.0f},
    {JointChainKind::Chain, 0, 4, 4.0f, 1.0f, 4000.0f, 500.0f},
};

JointChainDesc Desc;
Desc.Kind = JointChainKind::Chain;
Desc.Flags = JointChainPinStart | JointChainPinEnd;
Desc.Start = Vector2D{-128.0f, 0.0f};
Desc.End = Vector2D{128.0f, 0.0f};
Desc.Sections = Sections;
Desc.Radius = 4.0f;

const JointChainHandle Handle = Joints.CreateChain(Registry, Desc, &Error);
```

`sum(LinkCount)` replaces `SegmentCount` when `Path` is empty. When `Path` is
present, its point count must equal `sum(LinkCount) + 1`.

Sections also drive the link shape: a link whose section sets `Radius` gets a
thinner capsule, and `JointLinkComponent::Radius` already stores that per link,
so the renderer needs no change.

### Link gaps

Each capsule stops short of the shared path point by `B2_LINEAR_SLOP`, so two
adjacent tips keep a two-slop gap, and the joint rest length equals the actual
gap between the tips. This keeps a taut cable pinned at both ends at zero
pre-load: the joints carry only the load gameplay puts on them. A slack cable is
authored as a sagging `Path`; the builder does not generate slack geometry from a
straight span.

## 3. Strength and breaks

`JointChainDesc::BreakForce` and `BreakTorque` are in newtons and newton metres;
zero keeps a connection unbreakable. The builder writes those limits into
`b2JointDef::forceThreshold` and `b2JointDef::torqueThreshold`, which is what
makes Box2D report the joint at all. The joint system then converts a report into
a break:

- `Update(Registry, FixedDt)` reads `b2World_GetJointEvents`, maps the joint back
  to its pool slot through `b2JointDef::userData`, destroys the joint, keeps the
  slot, wakes both bodies, updates `JointLinkComponent::Flags` and appends one
  `ChainBreakEvent`.
- **Call `Update` immediately after `PhysicsSystem::Step`** in the same fixed
  step. Box2D events are transient and only cover the step that produced them.
- A break never destroys links. The chain keeps its slot, its links and its
  `ChainId`; the link on each side of the cut receives `JointLinkFreeEnd` or
  `JointLinkFreeStart`, and the renderer can draw the gap without querying the
  system.
- Events are only produced for awake joints. A joint that went to sleep under a
  static load does not break until it wakes again.
- Force and torque comparisons use the values Box2D reports for the last solved
  substep. They are deterministic for a given build and input stream.

### Damage

Setting `DamageRate` above zero makes a connection accumulate damage instead of
snapping at the first overstress. Every fixed step the system computes
`stress = max(force / BreakForce, torque / BreakTorque)`, and when the stress is
above `DamageThreshold` it adds
`(stress - threshold) * DamageRate * FixedDt` to the connection. The joint breaks
with `JointBreakReason::Damage` once the value reaches one. Damage is exposed in
`JointConnectionInfo::Damage`, resets on repair, and is skipped when both limits
are zero.

### Gameplay cuts

Cuts use the same path as a physical break, so they produce the same event and
the same free-end flags:

```cpp
// Cut one known connection
Joints.CutConnection(Registry, ConnectionHandle);

// Cut the live connection nearest to a world point (pixels)
Joints.CutAtPoint(Registry, ChainHandle, WorldPoint, 8.0f);

// Put it back together at the current pose
Joints.RepairConnection(Registry, ConnectionHandle, &Error);
```

A repaired hinge keeps the same angle reference: Box2D 3.2 measures the revolute
angle from the body rotations, and the joint local frames stay rotation-free, so
the limits keep their original meaning.

`Breaks()` returns the events since the last `ClearBreaks()`. The queue has a
fixed capacity (`kMaxBreakEvents`) and drops events once full; clear it at the
start of a frame.

### Deterministic command log

`Ecs::CommandLog::CutChain(Entity, ChainId, Connection, Tick)` records a gameplay
cut as `CommandType::CutChain` with a `CommandCutChain` payload. Replay routes it
to `CommandWorldCallbacks::cutChain`, so a rollback session reproduces the cut
without relying on wall-clock input. Physical breaks are not logged: they follow
from the deterministic step.

## 4. Ties and branching

A chain alone cannot branch. `TieChains` adds a connection between two links,
which may belong to different chains or to the same chain:

```cpp
JointTieDesc Tie;
Tie.Kind = JointConnectionKind::SpringRope;
Tie.BreakForce = 120.0f;

const JointConnectionHandle Rope = Joints.TieChains(Registry, RopeA, 1, RopeB, 1, Tie, &Error);
```

`TieBodies` connects two plain `RigidBody2D` entities instead of chain links. The
tie is a normal connection: it has a break limit, appears in `Breaks()` and can be
cut. `Untie` removes it. Destroying either chain, or destroying one body of a body
tie, removes the connection, so no stale `b2JointId` survives.

Ties always connect link centres (a distance tie is taut at the current
distance; a hinge tie meets at the midpoint). A distance tie with an explicit
`Length` in pixels can be shorter or longer than the current distance.

### Saving ties

Chain ties survive a save through a fixed record array:

```cpp
JointTieSave Saves[16];
const std::uint32_t Count = Joints.CaptureTies(Saves);
// Store the records, recreate the chains on load, then:
Joints.RestoreTies(Registry, std::span<const JointTieSave>(Saves, Count), &Error);
```

`RestoreTies` resolves each chain by its stable `ChainId`; a missing chain fails
the restore with `ChainError::InvalidConnection`. A saved tie that was broken is
recreated already cut. Body ties are runtime-only: entity ids are not stable
across a scene reload, so `CaptureTies` skips them.

## 5. Rebuilding a chain

`RebuildChain(Registry, Handle, NewDesc, &Error)` destroys the links and joints
of a chain and recreates it **under the same slot, generation and stable
ChainId**. External handles stay valid, which makes it the safe way to splice a
section into a live chain. A failed rebuild leaves the chain destroyed; treat the
error as fatal for that object.

## 6. Springs

There is no separate spring component. A spring is a connection with
`enableSpring`:

- `SpringRope` is a distance joint with `hertz`, `dampingRatio` and an optional
  force range. The range maps to Box2D's `lowerSpringForce`/`upperSpringForce`;
  zero leaves it unbounded.
- `SpringHinge` is a revolute joint with a zero target angle and the same
  stiffness parameters. Box2D has no force range for a revolute spring, so only
  the rope variant exposes one.

This keeps springs inside the same break, damage, telemetry and repair model.

## 7. Stress and damage telemetry

`JointChainFlags::JointChainTrackStress` (or `JointTieDesc::TrackStress`) makes
`Update` poll `b2Joint_GetConstraintForce` and `b2Joint_GetConstraintTorque` and
publish:

```text
stress = max(force / BreakForce, torque / BreakTorque)
```

`JointConnectionInfo::Stress` and `::Damage` are read-only diagnostics for HUD,
audio and debug draw. They never feed back into the simulation. Stress polling is
opt-in; damage polling is enabled per connection by `DamageRate`.

## 8. Units and scale

The public API is in pixels and normalised mass units, as the rest of the engine.
`PhysicsSystem::PixelsPerMeter` converts to Box2D metres. Break limits are the
exception: they are physical newtons and newton metres because that is what
Box2D compares. A link's mass follows from its capsule area and `Density`
(kg/m^2), so a break limit must be chosen against the load the chain actually
carries. As a rule of thumb, the weight of one link in newtons is
`mass * 9.8`; a hanging rope of N links carries about `N * linkWeight` at the
pin.

## 9. Saving and determinism

- The connection pool is allocated once in `Initialize`; building, breaking,
  damaging and repairing never allocate on the frame path.
- Events are ordered by the order Box2D reports them, which is deterministic for
  a given step. Stress and damage values are derived state, not inputs.
- `JointChainComponent` stores sections, default break/spring/damage limits and
  the `BrokenMaskLow`/`BrokenMaskHigh` bits. The snapshot codec writes the full
  component as `JointChainComponent/v4`.
- `CaptureChainState(Handle, &Component)` writes the current broken bits into the
  component; `ApplyChainState(Registry, Handle, Component)` cuts and repairs the
  connections to match. The component overload of `CreateChain` applies the mask
  automatically when it is non-zero, so recreating from a saved component
  restores the cuts.
- Scene TOML serializes the fixed `Path` and `Sections` arrays through a
  component-specific codec (`SectionsHex`/`PathHex`); binary scenes already carry
  the raw component bytes. The reflection schema still exposes only the scalar
  fields, so the inspector edits the scalars and gameplay edits the arrays.
- Runtime breaks are saved only when the game stores and reapplies the component;
  the joint system does not write back into an entity automatically.
- Body ties are runtime-only; chain ties are saved through `CaptureTies`.

## 10. Limits and behaviour contracts

- `Update` must run after every `PhysicsSystem::Step`. A skipped update loses the
  Box2D joint events for that step.
- Box2D reports a joint only while it is awake; a sleeping joint breaks when it
  wakes, not while it sleeps.
- A break is applied after the step that exceeded the limit, so it becomes
  visible on the next step.
- Connections are hard-capped: 32 chains, 64 links per chain, 4096 connections
  and 256 break events per frame. The pool is shared by chain connections and
  ties.
- Damage is a linear accumulation model. There is no spatial wear, no plastic
  deformation and no link-level health; the unit of failure is the connection.
- A slack cable is authored as a sagging path. A straight both-ends-pinned span
  is stable, but it cannot hang lower than its path.

## 11. Tests

`Source/Tests/PhysicsFxTest.cpp` covers:

- uniform rope and the legacy descriptor path;
- a mixed chain/rope/chain with per-section kinds, radii and break limits;
- a taut both-ends-pinned rope that stays at rest below its break limit;
- a break below the hanging weight, the break event and the surviving links;
- damage accumulation and the `Damage` break reason;
- cut by handle, cut by world point, repair and stress telemetry;
- broken-mask capture, destroy, recreate and reapply;
- spring ties between chains, tie capture/restore, body ties and body pruning;
- in-place rebuild that keeps the handle and the stable id;
- the scene TOML round trip for `Path` and `Sections`;
- a `CutChain` command that replays through `CommandWorldCallbacks`;
- spring hinges.

`Source/Tests/P3IntegrationTest.cpp` keeps the snapshot path of the chain
component alive.
