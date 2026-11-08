# Vehicle wheels

`WheelSystem` is a small vehicle base built on Box2D wheel joints.
It gives a game suspension, per-wheel drive and braking without pretending to be
a full powertrain, tire or steering solver.

## Scope

The engine owns:

- `Vehicle2D` — throttle, brake, optional arcade steering and torque limits.
- `Wheel2D` — chassis relation, suspension anchor/axis, spring, drive and brake
  weights, runtime joint handle.
- `WheelSystem` — lazy joint creation and per-step motor/spring updates.

The game owns:

- engine RPM, gears, clutch, differential and torque curves;
- tire slip, wear, surface temperature and custom friction models;
- steering geometry (kingpin, Ackermann, steering knuckles);
- vehicle-specific stability control.

This boundary is intentional: a 2D sprite game usually needs a stable base, not a
hidden assumption that every car is a side-view rally car.

## Components

### Vehicle2D (chassis)

| Field | Meaning |
|---|---|
| `Throttle` | `-1..1`, reverse to forward |
| `Brake` | `0..1` |
| `Steering` | `-1..1`, only applied with `VehicleFlagArcadeSteering` |
| `MaxMotorTorque` | N·m cap per driven wheel |
| `MaxSpeed` | pixels/s, translated to wheel angular speed by radius |
| `SteeringTorque` | N·m yaw assist |
| `Flags` | `VehicleFlagArcadeSteering`, `VehicleFlagHandbrake` |

### Wheel2D (wheel entity)

| Field | Meaning |
|---|---|
| `Chassis` | runtime chassis entity |
| `LocalAnchor` | chassis-local anchor, pixels |
| `SuspensionAxis` | chassis-local suspension axis |
| `Radius` | pixels; converts `MaxSpeed` to angular speed |
| `SuspensionHertz` | Box2D wheel-joint spring frequency |
| `SuspensionDamping` | Box2D wheel-joint damping ratio |
| `DriveWeight` | multiplier on chassis torque; `0` is a free wheel |
| `BrakeWeight` | multiplier on brake torque |
| `Flags` | `kWheelFlagDrive`, `kWheelFlagSteer` |
| `Joint` | runtime packed Box2D wheel joint; never author |

The wheel entity itself is a normal dynamic `RigidBody2D` with a `Collider2D`.
The system does not auto-create or auto-place it: the game decides whether the
wheel is a circle collider, a visual-only radius, or a compound body.

## System order

```text
WheelSystem::Initialize(physics)
WheelSystem::Update(registry, physics, dt)
PhysicsSystem::Step(registry, dt)
```

`Update` creates missing wheel joints lazily and applies motor/spring settings
before the solver step. It does not run after the step and does not overwrite a
body's linear velocity.

## Drive and brake

- `Throttle` maps to signed wheel angular velocity
  `throttle * MaxSpeed / Radius`.
- Torque is `abs(throttle) * MaxMotorTorque * DriveWeight`.
- `Brake` disables the wheel motor, applies the brake motor torque and an
  explicit angular-velocity damping pass so braking is deterministic even on a
  free-spinning wheel.
- `VehicleFlagHandbrake` applies brake to all wheels with a nonzero
  `BrakeWeight`.
- **No differential is implicit.** Front/rear/all-wheel drive is expressed by
  setting `DriveWeight` per wheel.
- **No automatic reverse gear.** Negative throttle is reverse; the game can
  clamp it.

## Steering

Box2D v3 wheel joints do not expose a steering angle setter. Steering geometry
is therefore a game/plugin concern. Two supported options:

1. **Arcade yaw assist** — set `VehicleFlagArcadeSteering`, set
   `kWheelFlagSteer` on one or more wheels, and let `Vehicle2D.Steering` apply a
   speed-scaled chassis torque. It is opt-in and disabled by default.
2. **Physical steering** — build a steering knuckle body and a revolute joint
   between chassis and knuckle, then attach the wheel joint to the knuckle.
   The engine does not hide this in a wheel-joint flag because it changes the
   body graph and needs authored geometry.

## DOD

DOD has no joints. `WheelSystem::Initialize` returns `false`, and
`Available()` reports the limitation. A project using wheels must select the
Box2D backend; the transport capability query already reports the same
boundary.

## Example

```cpp
WheelSystem wheels;
if (!wheels.Initialize(physics))
{
    // DOD or no world: keep the vehicle kinematic or switch to Box2D.
}

vehicle.Throttle = input.Throttle;
vehicle.Brake = input.Brake;
vehicle.Steering = input.Steering;

wheels.Update(registry, physics, fixedDt);
physics.Step(registry, fixedDt);
```

## Tests

`wheel_system` checks:

- two wheel joints are created lazily;
- throttle spins a wheel through the motor;
- brake stops it;
- DOD reports no wheel support.
