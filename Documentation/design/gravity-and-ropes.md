# Gravity, gravity bodies and rope tension

## Level gravity

A side-view level falls down by default. Top-down, iso-rect and iso-hex levels
start weightless. The rule is `DefaultGravityVector(Grid::ProjectionMode)`.

`LevelGravityComponent` overrides the world for one level:

| Field | Meaning |
|---|---|
| `Mode` | Down, Up, Left, Right, Custom, None |
| `Strength` | m/s^2 |
| `CustomDirection` | Direction for Custom mode |
| `MaxFallSpeed` | Optional terminal speed in px/s; 0 disables |
| `Enabled` | Off sets world gravity to zero |
| `ApplyWorldGravity` | Off keeps the existing PhysicsDesc gravity |

The first component in the world wins. It is a **level/scene** component, edited
in the editor's **Level Settings** window, and it saves with the scene; the same
component is available from code and from the generic component inspector.
Without a component, the tilemap projection is used: side gets `{0, 9.8}`,
top-down/iso get `{0, 0}`.

## Gravity bodies

`GravityBodyComponent` is a local field attached to any transform entity.
It never needs its own collider and never applies to itself.

| Field | Meaning |
|---|---|
| `Mode` | Attract, Repel or Directional |
| `Shape` | Circle or Box |
| `Radius` / `HalfExtents` | Influence zone |
| `Offset` | Local offset from the entity transform |
| `Direction` | Local-space push axis for Directional; rotated by the transform |
| `Strength` | Acceleration inside the zone |
| `Falloff` | None, Linear, Quadratic, Exponential |
| `FalloffDepth` | Band from edge to centre; 0 means the whole zone |
| `FalloffPower` | Exponential curve strength, 1..16 |
| `MaxAcceleration` | Optional clamp |
| `AffectsCategoryBits` | Collider category filter |
| `Enabled` | Per-field switch |

The edge-to-centre parameter is the key: at the zone edge the factor is 0, at
the centre it is 1. Exponential fields therefore do not create a hard ring at
the edge. Attract points toward the centre, Repel points away from it. The
system reads body mass from Box2D, so light and heavy bodies receive the same
acceleration at the same point.

Directional ignores the centre vector and pushes every body in the zone along
`Direction`, normalized and rotated by the source transform. `Strength`,
`Falloff`, `MaxAcceleration` and `AffectsCategoryBits` behave the same as in the
radial modes, so a field can author wind, lift, conveyor or side-view
anti-gravity without a centre point. A zero `Direction` disables that field.
All modes are plain component data, so swapping Attract, Repel and Directional
at runtime costs no allocation and takes effect on the next fixed step.

## Rope and chain tension

`JointChainComponent` stores the authored tension model:

| Field | Meaning |
|---|---|
| `BaseTension` | Tension present even without stretch |
| `MaxTension` | Break threshold; 0 falls back to `DefaultBreakForce` |
| `TensionPerDistance` | Added tension per pixel of stretch |
| `TensionDistance` | Stretch distance where tension stops growing; 0 uncapped |
| `StrengthEnabled` | Off makes the chain unbreakable and is the explicit "no strength" flag |

The effective tension is `BaseTension + TensionPerDistance * stretch`, where
stretch is capped at `TensionDistance`. `TensionLocked` reports that the cap was
reached; it does not alter the Box2D solver. `MaxTension` drives the joint force
threshold, so a breakable chain snaps at the authored value. With
`StrengthEnabled=false` both force and torque thresholds are removed, which lets
a game pull a rope through a mechanism without ever breaking it.

`JointConnectionInfo` exposes `CurrentTension`, `MaxTension`, `StrengthEnabled`
and `TensionLocked`, so a game can drive UI, audio or a grab mechanic without
reading Box2D.

All fields are reflected, saved in scene TOML and available in the editor
inspector. The same values can be set in code through `JointChainDesc` before
`CreateChain` or in `JointChainComponent` before `CreateFromComponent`.

## Limits and intent

- `GravityBodyComponent` is a field, not a solver object; fields stack
  additively.
- The tension model is authoring/telemetry plus a break cap. It does not add a
  motor or a preload to Box2D; those would be game mechanics.
- SDF worlds and tilemaps feed the same physics bodies, so gravity and rope
  behaviour does not depend on which collision representation built the level.
