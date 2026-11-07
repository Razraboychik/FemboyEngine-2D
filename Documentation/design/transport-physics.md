# Transport physics

The transport layer is a moving-reference-frame system, not a second rigid-body
solver. It is split into three concerns:

1. **Motion** — how a carrier root moves (`TransportPath`, `TransportMotion`).
2. **Carrying** — how a member follows the carrier (`CarrierMemberComponent`).
3. **Surface interaction** — how a dynamic member stands, slides or rides a
   conveyor (`TransportSurface`).

`TransportGroupSystem::PrePhysics` advances motion, computes carrier deltas and
seeds member velocities. `PhysicsSystem::Step` runs the solver.
`TransportGroupSystem::PostPhysics` refreshes local poses, relative velocities,
support flags and detach conditions.

## Components

### TransportPath

A fixed polyline of up to 64 world-space points. `Closed` loops the path. The
system samples by arc length, so speed is in pixels per second and the result is
independent of point spacing.

### TransportMotion

A kinematic motion profile:

| Field | Meaning |
|---|---|
| `Speed` | pixels per second |
| `Acceleration` / `Deceleration` | reserved for controller ramps |
| `DwellSeconds` | stop time at an endpoint |
| `Distance` | runtime distance along the path |
| `Direction` | `1` forward, `-1` reverse |
| `Axis` | linear direction when no path is supplied |
| `Flags` | `kTransportMotionPath`, `PingPong`, `StopAtEnds`, `FaceDirection` |

A root without `RigidBody2D` is moved by `Transform2D`. A root with a body is
also marked for a physics teleport so the solver sees the new pose.

### TransportSurface

Attach to the carrier root:

```text
Friction         0..4, default 0.6
TangentSpeed     conveyor belt velocity in pixels/s
OneWay           support only from above
AllowJump        preserve upward velocity when supported
```

### Carrier member modes

| Mode | Behaviour |
|---|---|
| `Rigid` | welded/teleported local pose; Box2D weld when available |
| `CarriedDynamic` | full carrier velocity injection, for cargo |
| `Support` | standing contact; friction-limited tangent velocity and normal support |
| `Conveyor` | surface tangent velocity; no feedback accumulation |

`Support` and `Conveyor` query the carrier/member AABB geometry each pre-step,
so the same rule works for Box2D and DOD. The engine stores the grounded runtime
bit in `kCarrierFlagGrounded`; scripts and gameplay should treat it as diagnostic,
not authored state.

## Backend capabilities

`TransportGroupSystem::CapabilitiesFor(PhysicsSystem)` reports joints, CCD,
weld and contact support. DOD has no joints, weld or CCD:

- `Rigid` falls back to local-pose teleport;
- `CarriedDynamic`/`Support`/`Conveyor` still work through velocity/carry;
- a project that needs welded trains must select Box2D or set
  `PhysicsDesc::requireJoints`.

## Determinism and snapshots

Motion is evaluated from a fixed-step distance and a path, not from wall-clock
time. `TransportMotion.Distance`, path points and the carrier pose are authoring
state. The transport authoring components are registered in the component
registry and visible in the editor, but `TransportPath`, `TransportMotion` and
`TransportSurface` are deliberately not in `SnapshotSchema::BuiltIns` (the
snapshot schema has a fixed capacity and runtime group/joint handles are
transient). A rollback or networked game must replicate or recreate them.
`TransportPath` exposes only `Count` and `Closed` to reflection; its points are
authored programmatically or by an asset tool.

## Editor

The Transport Groups panel can add a motion profile and a surface to a root,
select the rotation policy, edit bounds, and choose `Support` or `Conveyor` for a
member. Path points are authored through code/assets; the panel shows the path
count and closed flag.

## Tests

- `transport_groups` — rigid, nested, bounds, requested detach, Box2D carry,
  dynamic root.
- `transport_physics` — path sampling, conveyor support and DOD capabilities.
