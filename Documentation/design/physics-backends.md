# Physics backends

FemboyEngine2D exposes one `PhysicsSystem` API with two implementations:

| Backend | Implementation | Joints | CCD | Deterministic | Intended workload |
|---|---|---|---|---|---|
| `Box2D` | Box2D v3 | yes | yes | platform float | general rigid bodies, joints, editor |
| `Dod` | in-tree SoA AABB solver | no | swept helper for projectiles; no body CCD | fixed iteration order | large joint-free strategy worlds |
| `Auto` | policy | resolves to Box2D when `requireJoints` is set | not inferred from a body flag; set an explicit backend or requirement | resolves to DOD for deterministic or large worlds | project default |

## Selection

`PhysicsDesc.backend` is applied in `PhysicsSystem::Initialize`. The precedence is:

1. an explicit `SelectBackend` / `PhysicsDesc.backend` value;
2. `Auto` with `requireJoints` or a body budget below 4096: Box2D;
3. `Auto` with `deterministic` or a body budget of 4096 or more: DOD.

The project file stores the policy under `[physics]` as `backend` and
`body_budget`. Play-In-Editor copies the policy into the clone without changing
the editor's reference world. A live world never changes solver in place.

`PhysicsSystem::Capabilities()` is the only supported feature query. Do not infer
joint, CCD or deterministic support from the selected backend name.

## Data ownership

`RigidBody2D`, `Collider2D` and `Projectile2D` are the canonical ECS components.
The DOD backend stores positions, velocities, collider half-extents, flags and
sleep counters in parallel arrays. It assigns `RigidBody2D.Body` an opaque
index-plus-one only for ownership/debug compatibility; no code may pass that value
to Box2D in a DOD world.

Snapshots capture the ECS components, not backend internals. A restored world
recreates its backend state from the canonical components. Deterministic DOD
rollback must also restore contact and sleep state when that mode becomes part of
a networked project.

## Broadphase and contacts

DOD inserts body AABBs into a 64-unit uniform grid, sorts the cell entries by
key and resolves the unique pairs in each cell. AABB overlap
is resolved with inverse-mass positional correction and a normal impulse. Sensors
emit touch events but do not resolve. Joints and CCD are deliberately absent.

Box2D continues to own joints, CCD, sensors, contact hit events and the editor
debug draw.

## Projectiles

`ProjectileSystem` runs before the next physics step. `Auto` picks:

- `Hitscan` when displacement exceeds half `MinWallThickness` and radius is at
  most one pixel;
- `Swept` for larger continuous projectiles;
- `Dynamic` for slow projectiles, with the Box2D bullet flag.

The segment query uses ECS colliders and is shared by both backends. Hits are
reported through `ProjectileHitEvent`; the game decides damage and despawning.

## Benchmarks

`physics_backend_benchmark [bodies]` builds a mixed static/dynamic grid, warms up
one step and reports ms/frame for Box2D and DOD. The default is 2000 bodies; pass
10000 or 50000 for a local capacity check. The test is labelled `optimization` and
is not a hard CI gate.

## Known limits

- DOD has no joints, rotational contact response or body CCD. `Projectile2D`
  swept hits use the shared segment query; that is not continuous collision
  detection for arbitrary bodies.
- Box2D float results are not guaranteed identical across compilers/platforms.
- `Auto` is a policy heuristic; profile before choosing a backend for a project.
- The DOD broadphase is a sorted uniform grid plus candidate pairs, not a GPU solver.
