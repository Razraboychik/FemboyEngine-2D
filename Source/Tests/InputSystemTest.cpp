// FemboyEngine2D - fixed-capacity input actions, contexts and triggers.
#include "Engine/Core/WindowManager.hpp"
#include "Engine/Input/InputSystem.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <span>

using namespace Fby;

namespace
{
void Check(bool InCondition, const char* InMessage)
{
    if (!InCondition)
    {
        std::fprintf(stderr, "FAIL: %s\n", InMessage);
        std::exit(1);
    }
}

InputMappingDesc KeyMapping(std::uint16_t InAction, std::uint16_t InContext, Key InKey,
                            InputTriggerKind InTrigger = InputTriggerKind::Pressed)
{
    InputMappingDesc ScopedMapping;
    ScopedMapping.Action = InAction;
    ScopedMapping.Context = InContext;
    ScopedMapping.Source.Kind = InputSourceKind::Key;
    ScopedMapping.Source.Code = static_cast<std::uint16_t>(InKey);
    ScopedMapping.Trigger = InTrigger;
    return ScopedMapping;
}
}

int main()
{
    InputSystem ScopedInput;
    const std::uint16_t ScopedJump = ScopedInput.RegisterAction("Jump", InputActionType::Bool);
    const std::uint16_t ScopedMove = ScopedInput.RegisterAction("Move", InputActionType::Axis2D);
    const std::uint16_t ScopedHold = ScopedInput.RegisterAction("Hold", InputActionType::Bool);
    const std::uint16_t ScopedTap = ScopedInput.RegisterAction("Tap", InputActionType::Bool);
    const std::uint16_t ScopedGameplay = ScopedInput.RegisterContext("Gameplay", 0, false);
    const std::uint16_t ScopedMenu = ScopedInput.RegisterContext("Menu", 10, true);
    Check(ScopedJump != kInvalidInputId && ScopedMove != kInvalidInputId, "actions register");
    Check(ScopedGameplay != kInvalidInputId && ScopedMenu != kInvalidInputId, "contexts register");

    InputMappingDesc ScopedJumpMapping = KeyMapping(ScopedJump, ScopedGameplay, Key::Space);
    Check(ScopedInput.AddMapping(ScopedJumpMapping), "jump mapping registers");
    InputMappingDesc ScopedMoveMapping = KeyMapping(ScopedMove, ScopedGameplay, Key::D);
    ScopedMoveMapping.Trigger = InputTriggerKind::Pressed;
    Check(ScopedInput.AddMapping(ScopedMoveMapping), "move mapping registers");
    InputMappingDesc ScopedHoldMapping = KeyMapping(ScopedHold, ScopedGameplay, Key::H, InputTriggerKind::Hold);
    ScopedHoldMapping.TriggerA = 0.2f;
    Check(ScopedInput.AddMapping(ScopedHoldMapping), "hold mapping registers");
    InputMappingDesc ScopedTapMapping = KeyMapping(ScopedTap, ScopedGameplay, Key::T, InputTriggerKind::Tap);
    ScopedTapMapping.TriggerA = 0.2f;
    Check(ScopedInput.AddMapping(ScopedTapMapping), "tap mapping registers");

    Check(ScopedInput.BindUser(Entity{0}, 0, 0), "user binds");
    Check(ScopedInput.PushContext(0, ScopedGameplay), "gameplay context pushes");

    InputState ScopedRaw;
    ScopedRaw.BeginFrame();
    ScopedRaw.held[static_cast<std::size_t>(Key::Space)] = true;
    ScopedRaw.pressed[static_cast<std::size_t>(Key::Space)] = true;
    ScopedRaw.held[static_cast<std::size_t>(Key::D)] = true;
    ScopedRaw.pressed[static_cast<std::size_t>(Key::D)] = true;
    ScopedInput.Update(ScopedRaw, 10, 1000, 1.0f / 60.0f);
    const InputActionState* ScopedJumpState = ScopedInput.Action(0, ScopedJump);
    Check(ScopedJumpState != nullptr && ScopedJumpState->Triggered, "pressed trigger fires");
    Check(ScopedJumpState->Started, "pressed action starts");
    const std::span<const InputEvent> ScopedEvents = ScopedInput.Events(0);
    bool ScopedSawTriggered = false;
    for (const InputEvent& ScopedEvent : ScopedEvents)
    {
        ScopedSawTriggered = ScopedSawTriggered || (ScopedEvent.Action == ScopedJump &&
                                                    ScopedEvent.Type == InputEventType::Triggered);
    }
    Check(ScopedSawTriggered, "triggered event is queued");
    const InputActionState* ScopedMoveState = ScopedInput.Action(0, ScopedMove);
    Check(ScopedMoveState != nullptr && ScopedMoveState->X > 0.9f, "axis value maps");

    // A consuming menu context blocks the lower gameplay mapping.
    Check(ScopedInput.PushContext(0, ScopedMenu), "menu context pushes");
    InputState ScopedMenuRaw;
    ScopedMenuRaw.BeginFrame();
    ScopedMenuRaw.held[static_cast<std::size_t>(Key::Space)] = true;
    ScopedMenuRaw.pressed[static_cast<std::size_t>(Key::Space)] = true;
    ScopedInput.Update(ScopedMenuRaw, 11, 2000, 1.0f / 60.0f);
    Check(!ScopedInput.Action(0, ScopedJump)->Triggered, "consuming context blocks gameplay");
    Check(ScopedInput.PopContext(0, ScopedMenu), "menu context pops");

    // Hold crosses its threshold on the third tick and emits once.
    InputSystem ScopedHoldInput;
    const std::uint16_t ScopedHoldAction = ScopedHoldInput.RegisterAction("Hold", InputActionType::Bool);
    const std::uint16_t ScopedGameplayContext = ScopedHoldInput.RegisterContext("Gameplay", 0, false);
    InputMappingDesc ScopedHoldDesc = KeyMapping(ScopedHoldAction, ScopedGameplayContext, Key::H,
                                                 InputTriggerKind::Hold);
    ScopedHoldDesc.TriggerA = 0.25f;
    Check(ScopedHoldInput.AddMapping(ScopedHoldDesc), "hold mapping registers standalone");
    Check(ScopedHoldInput.BindUser(Entity{1}, 0, 0xFFFFFFFFu), "hold user binds");
    Check(ScopedHoldInput.PushContext(0, ScopedGameplayContext), "hold context pushes");
    InputState ScopedHoldRaw;
    ScopedHoldRaw.BeginFrame();
    ScopedHoldRaw.held[static_cast<std::size_t>(Key::H)] = true;
    ScopedHoldInput.Update(ScopedHoldRaw, 20, 3000, 0.1f);
    Check(!ScopedHoldInput.Action(0, ScopedHoldAction)->Triggered, "hold waits for threshold");
    ScopedHoldRaw.BeginFrame();
    ScopedHoldRaw.held[static_cast<std::size_t>(Key::H)] = true;
    ScopedHoldInput.Update(ScopedHoldRaw, 21, 3100, 0.1f);
    Check(!ScopedHoldInput.Action(0, ScopedHoldAction)->Triggered, "hold still below threshold");
    ScopedHoldRaw.BeginFrame();
    ScopedHoldRaw.held[static_cast<std::size_t>(Key::H)] = true;
    ScopedHoldInput.Update(ScopedHoldRaw, 22, 3200, 0.1f);
    Check(ScopedHoldInput.Action(0, ScopedHoldAction)->Triggered, "hold crosses threshold");

    // Deadzone and gamepad button/axis mapping.
    InputSystem ScopedPadInput;
    const std::uint16_t ScopedPad = ScopedPadInput.RegisterAction("Pad", InputActionType::Axis2D);
    const std::uint16_t ScopedPadContext = ScopedPadInput.RegisterContext("Gameplay", 0, false);
    InputMappingDesc ScopedPadDesc;
    ScopedPadDesc.Action = ScopedPad;
    ScopedPadDesc.Context = ScopedPadContext;
    ScopedPadDesc.Source.Kind = InputSourceKind::GamepadAxis;
    ScopedPadDesc.Source.Code = 0;
    ScopedPadDesc.Trigger = InputTriggerKind::Pressed;
    ScopedPadDesc.ModifierCount = 1;
    ScopedPadDesc.Modifiers[0].Kind = InputModifierKind::Deadzone;
    ScopedPadDesc.Modifiers[0].A = 0.2f;
    Check(ScopedPadInput.AddMapping(ScopedPadDesc), "gamepad mapping registers");
    Check(ScopedPadInput.BindUser(Entity{2}, 0, 0), "gamepad user binds");
    Check(ScopedPadInput.PushContext(0, ScopedPadContext), "gamepad context pushes");
    InputState ScopedPadRaw;
    ScopedPadRaw.BeginFrame();
    ScopedPadRaw.gamepads[0].connected = true;
    ScopedPadRaw.gamepads[0].axes[0] = 0.5f;
    ScopedPadInput.Update(ScopedPadRaw, 30, 4000, 1.0f / 60.0f);
    const InputActionState* ScopedPadState = ScopedPadInput.Action(0, ScopedPad);
    Check(ScopedPadState != nullptr && ScopedPadState->X > 0.3f && ScopedPadState->X < 0.4f,
          "gamepad deadzone remaps the axis");

    std::printf("input_system: ok\n");
    return 0;
}
