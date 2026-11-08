# Input system

FemboyEngine2D uses a fixed-capacity action mapper rather than an object graph.
It is intentionally closer to an Enhanced Input model than raw SDL polling, but
it stays data-oriented: no virtual dispatch, no per-action allocation, no UObject.

## Layers

```text
WindowManager::InputState       raw keyboard, mouse and gamepad snapshot
        ↓
InputSystem                     actions, contexts, modifiers, triggers
        ↓
InputUserComponent             ECS binding to a user slot
        ↓
gameplay / UI
```

`InputSystem` owns flat arrays for actions, contexts, mappings and user state.
`InputUserComponent` is the only new ECS component; action values are indexed by
`UserId`, not stored in a component that would have to be replicated per entity.

## Actions and contexts

```cpp
InputSystem input;
const std::uint16_t Move = input.RegisterAction("Move", InputActionType::Axis2D);
const std::uint16_t Jump = input.RegisterAction("Jump", InputActionType::Bool);
const std::uint16_t Gameplay = input.RegisterContext("Gameplay", 0, false);
const std::uint16_t Menu = input.RegisterContext("Menu", 10, true);
```

Mappings connect an action and a context to one device source:

```cpp
InputMappingDesc Mapping;
Mapping.Action = Jump;
Mapping.Context = Gameplay;
Mapping.Source.Kind = InputSourceKind::GamepadButton;
Mapping.Source.Code = 0; // SDL_GAMEPAD_BUTTON_SOUTH
Mapping.Trigger = InputTriggerKind::Pressed;
input.AddMapping(Mapping);
```

A consuming context with a higher priority blocks lower-priority mappings. For
example a `Menu` context can consume input without disabling gameplay code.

## Modifiers

Modifiers are a small fixed pipeline of up to four entries:

- radial/axial dead zone;
- scale and clamp;
- invert;
- swizzle;
- normalize;
- response curve.

Every mapping stores its own `InputModifier` array. A game can represent the same
action with keyboard, mouse, gamepad button and gamepad axis without changing
gameplay code.

## Triggers and rhythm

Built-in triggers:

| Trigger | Use |
|---|---|
| `Pressed` | one-shot button |
| `Released` | release edge |
| `Hold` | hold for `TriggerA` seconds |
| `Tap` | release within `TriggerA` seconds |
| `Pulse` | repeat while held |
| `Repeat` | initial delay, then interval |

`InputActionState` carries `Started`, `Triggered`, `Completed`, `Canceled`,
`HeldSeconds` and `LastEventTick`. `InputEvent` additionally carries a monotonic
`TimestampNs` for audio and rhythm judgement.

The simulation uses the fixed tick. The event timestamp is presentation data:
a rhythm game can compare it with its audio clock, while the deterministic
simulation still advances only on ticks. The engine does not contain BPM, note
charts or judgement windows.

## Gamepad

`WindowManager` initialises `SDL_INIT_GAMEPAD`, opens gamepads from
`SDL_EVENT_GAMEPAD_ADDED`, closes them on removal and publishes up to eight POD
`GamepadState` snapshots. A snapshot contains sixteen buttons, six axes and
edge/pressed/released bits. Hotplug and keyboard fallback are part of the device
layer, not the action mapper.

`sdl3` supplies the desktop mapping database. SDL 3.4.18 has a Windows HID
refcount imbalance when the legacy DirectInput joystick backend fails to
initialise, so `WindowManager` disables that backend with
`SDL_HINT_JOYSTICK_DIRECTINPUT=0` before SDL init. XInput and HIDAPI, which cover
modern Xbox and PlayStation controllers, remain enabled. There is no console SDK
path and no wheel/pedal profile: a wheel is not an input target of this system.
The physics `WheelSystem` remains independent and is not a device API.

## Capacity

| Data | Capacity |
|---|---:|
| Actions | 128 |
| Contexts | 16 |
| Mappings | 256 |
| Users | 8 |
| Events per user | 64 |
| Modifiers per mapping | 4 |
| Context stack | 8 |

All capacities are compile-time constants. The update path never allocates.

## ECS and replication

```cpp
const Entity Player = registry.create();
InputUserComponent User;
User.UserId = 0;
User.GamepadSlot = 0;
registry.emplace<InputUserComponent>(Player, User);
input.BindUser(Player, 0, 0);
input.PushContext(0, Gameplay);
```

The local device layer reads `InputState`; the network sends only an
`InputFrame` (actions, values and event ticks) or the game's own compact input
POD. Raw SDL events never go into a snapshot. See the Application Guide section
on replication for the authoritative-server workflow.

## Tests

`input_system` covers action/value mapping, pressed trigger, a consuming
context, hold threshold, gamepad axis with dead zone, and the fixed capacities.
The test constructs `InputState` directly and does not need a window or hardware.
