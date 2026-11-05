// FemboyEngine2D - fixed-capacity Enhanced-style input mapping for ECS/DOD.
#include "Engine/Input/InputSystem.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Fby
{
namespace
{
[[nodiscard]] std::uint16_t FindAction(const std::array<InputActionDesc, kMaxInputActions>& InActions,
                                       std::uint32_t InCount, std::string_view InName) noexcept
{
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < InCount; ++ScopedIndex)
    {
        if (InName == InActions[ScopedIndex].Name.data())
        {
            return static_cast<std::uint16_t>(ScopedIndex);
        }
    }
    return kInvalidInputId;
}

[[nodiscard]] std::uint16_t FindContext(const std::array<InputContextDesc, kMaxInputContexts>& InContexts,
                                        std::uint32_t InCount, std::string_view InName) noexcept
{
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < InCount; ++ScopedIndex)
    {
        if (InName == InContexts[ScopedIndex].Name.data())
        {
            return static_cast<std::uint16_t>(ScopedIndex);
        }
    }
    return kInvalidInputId;
}

void CopyName(std::array<char, 32>& OutName, std::string_view InName) noexcept
{
    const std::size_t ScopedLength = std::min(InName.size(), OutName.size() - 1u);
    std::memcpy(OutName.data(), InName.data(), ScopedLength);
    OutName[ScopedLength] = '\0';
}
}

void InputSystem::Reset() noexcept
{
    *this = InputSystem{};
}

std::uint16_t InputSystem::RegisterAction(std::string_view InName, InputActionType InType) noexcept
{
    const std::uint16_t ScopedExisting = FindAction(MActions, MActionCount, InName);
    if (ScopedExisting != kInvalidInputId)
    {
        return ScopedExisting;
    }
    if (MActionCount >= kMaxInputActions)
    {
        return kInvalidInputId;
    }
    InputActionDesc& ScopedAction = MActions[MActionCount];
    CopyName(ScopedAction.Name, InName);
    ScopedAction.Type = InType;
    return static_cast<std::uint16_t>(MActionCount++);
}

std::uint16_t InputSystem::RegisterContext(std::string_view InName, std::int32_t InPriority,
                                           bool InConsume) noexcept
{
    const std::uint16_t ScopedExisting = FindContext(MContexts, MContextCount, InName);
    if (ScopedExisting != kInvalidInputId)
    {
        return ScopedExisting;
    }
    if (MContextCount >= kMaxInputContexts)
    {
        return kInvalidInputId;
    }
    InputContextDesc& ScopedContext = MContexts[MContextCount];
    CopyName(ScopedContext.Name, InName);
    ScopedContext.Priority = InPriority;
    ScopedContext.Consume = InConsume;
    return static_cast<std::uint16_t>(MContextCount++);
}

bool InputSystem::AddMapping(const InputMappingDesc& InMapping) noexcept
{
    if (MMappingCount >= kMaxInputMappings || InMapping.Action >= MActionCount ||
        InMapping.Context >= MContextCount || InMapping.ModifierCount > kMaxInputModifiers)
    {
        return false;
    }
    MMappings[MMappingCount++] = InMapping;
    return true;
}

bool InputSystem::Rebind(std::uint32_t InMappingIndex, const InputSource& InSource) noexcept
{
    if (InMappingIndex >= MMappingCount)
    {
        return false;
    }
    MMappings[InMappingIndex].Source = InSource;
    return true;
}

const InputMappingDesc* InputSystem::Mapping(std::uint32_t InMappingIndex) const noexcept
{
    return InMappingIndex < MMappingCount ? &MMappings[InMappingIndex] : nullptr;
}

bool InputSystem::BindUser(Entity InEntity, std::uint32_t InUserId, std::uint32_t InGamepadSlot) noexcept
{
    if (InUserId >= kMaxInputUsers)
    {
        return false;
    }
    UserSlot& ScopedUser = MUsers[InUserId];
    ScopedUser.Bound = true;
    ScopedUser.Owner = InEntity;
    ScopedUser.GamepadSlot = InGamepadSlot;
    MUserCount = std::max(MUserCount, InUserId + 1u);
    return true;
}

bool InputSystem::PushContext(std::uint32_t InUserId, std::uint16_t InContext) noexcept
{
    if (InUserId >= kMaxInputUsers || InContext >= MContextCount || !MUsers[InUserId].Bound)
    {
        return false;
    }
    UserSlot& ScopedUser = MUsers[InUserId];
    if (ScopedUser.ContextCount >= kMaxInputContextStack ||
        IsContextActive(ScopedUser, InContext))
    {
        return false;
    }
    ScopedUser.Contexts[ScopedUser.ContextCount++] = InContext;
    return true;
}

bool InputSystem::PopContext(std::uint32_t InUserId, std::uint16_t InContext) noexcept
{
    if (InUserId >= kMaxInputUsers || !MUsers[InUserId].Bound)
    {
        return false;
    }
    UserSlot& ScopedUser = MUsers[InUserId];
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedUser.ContextCount; ++ScopedIndex)
    {
        if (ScopedUser.Contexts[ScopedIndex] != InContext)
        {
            continue;
        }
        for (std::uint32_t ScopedShift = ScopedIndex + 1u; ScopedShift < ScopedUser.ContextCount; ++ScopedShift)
        {
            ScopedUser.Contexts[ScopedShift - 1u] = ScopedUser.Contexts[ScopedShift];
        }
        --ScopedUser.ContextCount;
        ScopedUser.Contexts[ScopedUser.ContextCount] = kInvalidInputId;
        return true;
    }
    return false;
}

void InputSystem::SetGamepadSlot(std::uint32_t InUserId, std::uint32_t InGamepadSlot) noexcept
{
    if (InUserId < kMaxInputUsers)
    {
        MUsers[InUserId].GamepadSlot = InGamepadSlot;
    }
}

bool InputSystem::IsContextActive(const UserSlot& InUser, std::uint16_t InContext) const noexcept
{
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < InUser.ContextCount; ++ScopedIndex)
    {
        if (InUser.Contexts[ScopedIndex] == InContext)
        {
            return true;
        }
    }
    return false;
}

bool InputSystem::MappingAllowed(const UserSlot& InUser, std::uint16_t InContext) const noexcept
{
    if (!IsContextActive(InUser, InContext))
    {
        return false;
    }
    std::int32_t ScopedConsumePriority = -0x7FFFFFFF;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < InUser.ContextCount; ++ScopedIndex)
    {
        const InputContextDesc& ScopedContext = MContexts[InUser.Contexts[ScopedIndex]];
        if (ScopedContext.Consume)
        {
            ScopedConsumePriority = std::max(ScopedConsumePriority, ScopedContext.Priority);
        }
    }
    return ScopedConsumePriority == -0x7FFFFFFF || MContexts[InContext].Priority >= ScopedConsumePriority;
}

float InputSystem::ReadSource(const InputState& InInput, const UserSlot& InUser, const InputSource& InSource,
                              bool* OutPressed, bool* OutReleased, bool* OutHeld) const noexcept
{
    *OutPressed = false;
    *OutReleased = false;
    *OutHeld = false;
    switch (InSource.Kind)
    {
        case InputSourceKind::Key:
        {
            if (InSource.Code >= kMaxScancodes)
            {
                return 0.0f;
            }
            *OutPressed = InInput.pressed[InSource.Code];
            *OutReleased = InInput.released[InSource.Code];
            *OutHeld = InInput.held[InSource.Code];
            return *OutHeld ? 1.0f : 0.0f;
        }
        case InputSourceKind::MouseButton:
        {
            if (InSource.Code >= kMaxMouseButtons)
            {
                return 0.0f;
            }
            *OutPressed = InInput.mousePressed[InSource.Code];
            *OutReleased = InInput.mouseReleased[InSource.Code];
            *OutHeld = InInput.mouseHeld[InSource.Code];
            return *OutHeld ? 1.0f : 0.0f;
        }
        case InputSourceKind::MouseAxis:
        {
            const float ScopedValue = InSource.Code == 0 ? InInput.mouseDelta.X : InInput.mouseDelta.Y;
            *OutHeld = std::abs(ScopedValue) > 0.0f;
            return ScopedValue;
        }
        case InputSourceKind::GamepadButton:
        {
            if (InUser.GamepadSlot >= kMaxGamepads || InSource.Code >= kMaxGamepadButtons)
            {
                return 0.0f;
            }
            const GamepadState& ScopedGamepad = InInput.gamepads[InUser.GamepadSlot];
            if (!ScopedGamepad.connected)
            {
                return 0.0f;
            }
            *OutPressed = ScopedGamepad.pressed[InSource.Code];
            *OutReleased = ScopedGamepad.released[InSource.Code];
            *OutHeld = ScopedGamepad.held[InSource.Code];
            return *OutHeld ? 1.0f : 0.0f;
        }
        case InputSourceKind::GamepadAxis:
        {
            if (InUser.GamepadSlot >= kMaxGamepads || InSource.Code >= kMaxGamepadAxes)
            {
                return 0.0f;
            }
            const GamepadState& ScopedGamepad = InInput.gamepads[InUser.GamepadSlot];
            if (!ScopedGamepad.connected)
            {
                return 0.0f;
            }
            const float ScopedValue = ScopedGamepad.axes[InSource.Code];
            *OutHeld = std::abs(ScopedValue) > 0.15f;
            return ScopedValue;
        }
    }
    return 0.0f;
}

Vector2D InputSystem::ApplyModifiers(const InputMappingDesc& InMapping, Vector2D InValue) const noexcept
{
    for (std::uint8_t ScopedIndex = 0; ScopedIndex < InMapping.ModifierCount; ++ScopedIndex)
    {
        const InputModifier& ScopedModifier = InMapping.Modifiers[ScopedIndex];
        const float ScopedA = ScopedModifier.A;
        switch (ScopedModifier.Kind)
        {
            case InputModifierKind::Deadzone:
            {
                const float ScopedLength = std::sqrt(InValue.X * InValue.X + InValue.Y * InValue.Y);
                if (ScopedLength <= ScopedA)
                {
                    InValue = Vector2D{};
                }
                else if (ScopedA < 1.0f)
                {
                    const float ScopedScale = (ScopedLength - ScopedA) / (1.0f - ScopedA) / ScopedLength;
                    InValue = InValue * ScopedScale;
                }
                break;
            }
            case InputModifierKind::Scale:
                InValue = InValue * ScopedA;
                break;
            case InputModifierKind::Invert:
                InValue.X = -InValue.X;
                InValue.Y = -InValue.Y;
                break;
            case InputModifierKind::Swizzle:
                std::swap(InValue.X, InValue.Y);
                break;
            case InputModifierKind::Normalize:
            {
                const float ScopedLength = std::sqrt(InValue.X * InValue.X + InValue.Y * InValue.Y);
                if (ScopedLength > 0.0001f)
                {
                    InValue = InValue / ScopedLength;
                }
                break;
            }
            case InputModifierKind::Clamp:
                InValue.X = std::clamp(InValue.X, -ScopedA, ScopedA);
                InValue.Y = std::clamp(InValue.Y, -ScopedA, ScopedA);
                break;
            case InputModifierKind::Curve:
                InValue.X = std::copysign(std::pow(std::abs(InValue.X), std::max(ScopedA, 0.01f)), InValue.X);
                InValue.Y = std::copysign(std::pow(std::abs(InValue.Y), std::max(ScopedA, 0.01f)), InValue.Y);
                break;
        }
    }
    return InValue;
}

void InputSystem::EmitEvent(UserSlot& InUser, std::uint16_t InAction, InputEventType InType, Vector2D InValue,
                            std::uint64_t InTick, std::uint64_t InTimestampNs) noexcept
{
    if (InUser.EventCount >= kMaxInputEvents)
    {
        return;
    }
    InputEvent& ScopedEvent = InUser.Events[InUser.EventCount++];
    ScopedEvent.Action = InAction;
    ScopedEvent.Type = InType;
    ScopedEvent.X = InValue.X;
    ScopedEvent.Y = InValue.Y;
    ScopedEvent.Tick = InTick;
    ScopedEvent.TimestampNs = InTimestampNs;
}

void InputSystem::Update(const InputState& InInput, std::uint64_t InTick, std::uint64_t InTimestampNs,
                         float InDeltaSeconds) noexcept
{
    const float ScopedDelta = std::max(InDeltaSeconds, 0.0f);
    for (std::uint32_t ScopedUserIndex = 0; ScopedUserIndex < MUserCount; ++ScopedUserIndex)
    {
        UserSlot& ScopedUser = MUsers[ScopedUserIndex];
        if (!ScopedUser.Bound)
        {
            continue;
        }
        ScopedUser.EventCount = 0;
        for (InputActionState& ScopedAction : ScopedUser.Actions)
        {
            ScopedAction.X = 0.0f;
            ScopedAction.Y = 0.0f;
            ScopedAction.Started = false;
            ScopedAction.Triggered = false;
            ScopedAction.Completed = false;
            ScopedAction.Canceled = false;
        }
    }

    for (std::uint32_t ScopedMappingIndex = 0; ScopedMappingIndex < MMappingCount; ++ScopedMappingIndex)
    {
        const InputMappingDesc& ScopedMapping = MMappings[ScopedMappingIndex];
        for (std::uint32_t ScopedUserIndex = 0; ScopedUserIndex < MUserCount; ++ScopedUserIndex)
        {
            UserSlot& ScopedUser = MUsers[ScopedUserIndex];
            if (!ScopedUser.Bound || !MappingAllowed(ScopedUser, ScopedMapping.Context))
            {
                continue;
            }
            bool ScopedPressed = false;
            bool ScopedReleased = false;
            bool ScopedHeld = false;
            const float ScopedRaw = ReadSource(InInput, ScopedUser, ScopedMapping.Source, &ScopedPressed, &ScopedReleased,
                                               &ScopedHeld);
            Vector2D ScopedValue{ScopedRaw, 0.0f};
            if (ScopedMapping.Source.Kind == InputSourceKind::MouseAxis)
            {
                ScopedValue.X = 0.0f;
                ScopedValue.Y = ScopedRaw;
            }
            ScopedValue = ApplyModifiers(ScopedMapping, ScopedValue);
            InputActionState& ScopedAction = ScopedUser.Actions[ScopedMapping.Action];
            ScopedAction.X += ScopedValue.X;
            ScopedAction.Y += ScopedValue.Y;
            if (ScopedPressed)
            {
                ScopedAction.Started = true;
                ScopedAction.HeldSeconds = 0.0f;
                EmitEvent(ScopedUser, ScopedMapping.Action, InputEventType::Started, ScopedValue, InTick,
                          InTimestampNs);
            }
            if (ScopedHeld)
            {
                ScopedAction.HeldSeconds += ScopedDelta;
            }
            else if (!ScopedReleased)
            {
                ScopedAction.HeldSeconds = 0.0f;
            }

            bool ScopedTrigger = false;
            switch (ScopedMapping.Trigger)
            {
                case InputTriggerKind::Pressed:
                    ScopedTrigger = ScopedPressed;
                    break;
                case InputTriggerKind::Released:
                    ScopedTrigger = ScopedReleased;
                    break;
                case InputTriggerKind::Hold:
                    ScopedTrigger = ScopedHeld && ScopedAction.HeldSeconds >= ScopedMapping.TriggerA &&
                                    ScopedAction.HeldSeconds - ScopedDelta < ScopedMapping.TriggerA;
                    break;
                case InputTriggerKind::Tap:
                    ScopedTrigger = ScopedReleased && ScopedAction.HeldSeconds > 0.0f &&
                                    ScopedAction.HeldSeconds <= ScopedMapping.TriggerA;
                    break;
                case InputTriggerKind::Pulse:
                {
                    const float ScopedInterval = std::max(ScopedMapping.TriggerA, 0.001f);
                    if (ScopedHeld && std::fmod(ScopedAction.HeldSeconds, ScopedInterval) < ScopedDelta)
                    {
                        ScopedTrigger = true;
                    }
                    break;
                }
                case InputTriggerKind::Repeat:
                {
                    const float ScopedInitial = std::max(ScopedMapping.TriggerA, 0.001f);
                    const float ScopedInterval =
                        std::max(ScopedMapping.TriggerB > 0.0f ? ScopedMapping.TriggerB : ScopedInitial, 0.001f);
                    if (ScopedHeld)
                    {
                        const float ScopedElapsed = ScopedAction.HeldSeconds;
                        const float ScopedPhase = ScopedElapsed < ScopedInitial ? 0.0f : ScopedElapsed - ScopedInitial;
                        ScopedTrigger = ScopedElapsed < ScopedInitial
                                            ? (ScopedElapsed - ScopedDelta < ScopedInitial)
                                            : (std::fmod(ScopedPhase, ScopedInterval) < ScopedDelta);
                    }
                    break;
                }
            }
            if (ScopedTrigger)
            {
                ScopedAction.Triggered = true;
                ScopedAction.LastEventTick = InTick;
                EmitEvent(ScopedUser, ScopedMapping.Action, InputEventType::Triggered, ScopedValue, InTick,
                          InTimestampNs);
            }
            if (ScopedReleased)
            {
                ScopedAction.Completed = true;
                EmitEvent(ScopedUser, ScopedMapping.Action, InputEventType::Completed, ScopedValue, InTick,
                          InTimestampNs);
            }
        }
    }

    for (std::uint32_t ScopedUserIndex = 0; ScopedUserIndex < MUserCount; ++ScopedUserIndex)
    {
        UserSlot& ScopedUser = MUsers[ScopedUserIndex];
        if (!ScopedUser.Bound)
        {
            continue;
        }
        for (InputActionState& ScopedAction : ScopedUser.Actions)
        {
            ScopedAction.X = std::clamp(ScopedAction.X, -1.0f, 1.0f);
            ScopedAction.Y = std::clamp(ScopedAction.Y, -1.0f, 1.0f);
        }
    }
}

const InputActionState* InputSystem::Action(std::uint32_t InUserId, std::uint16_t InAction) const noexcept
{
    if (InUserId >= MUserCount || InAction >= kMaxInputActions || !MUsers[InUserId].Bound)
    {
        return nullptr;
    }
    return &MUsers[InUserId].Actions[InAction];
}

std::span<const InputEvent> InputSystem::Events(std::uint32_t InUserId) const noexcept
{
    if (InUserId >= MUserCount || !MUsers[InUserId].Bound)
    {
        return {};
    }
    return std::span<const InputEvent>(MUsers[InUserId].Events.data(), MUsers[InUserId].EventCount);
}

const InputActionDesc* InputSystem::ActionDesc(std::uint16_t InAction) const noexcept
{
    return InAction < MActionCount ? &MActions[InAction] : nullptr;
}

const InputContextDesc* InputSystem::ContextDesc(std::uint16_t InContext) const noexcept
{
    return InContext < MContextCount ? &MContexts[InContext] : nullptr;
}

}
