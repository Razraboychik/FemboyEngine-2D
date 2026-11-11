// FemboyEngine2D - fixed-capacity Enhanced-style input mapping for ECS/DOD.
#pragma once

#include "Engine/Core/WindowManager.hpp"
#include "Engine/ECS/Components.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace Fby
{

inline constexpr std::uint32_t kMaxInputActions = 128;
inline constexpr std::uint32_t kMaxInputContexts = 16;
inline constexpr std::uint32_t kMaxInputMappings = 256;
inline constexpr std::uint32_t kMaxInputUsers = 8;
inline constexpr std::uint32_t kMaxInputEvents = 64;
inline constexpr std::uint32_t kMaxInputModifiers = 4;
inline constexpr std::uint32_t kMaxInputContextStack = 8;
inline constexpr std::uint16_t kInvalidInputId = 0xFFFFu;

enum class InputActionType : std::uint8_t
{
    Bool = 0,
    Axis1D = 1,
    Axis2D = 2,
};

enum class InputSourceKind : std::uint8_t
{
    Key = 0,
    MouseButton = 1,
    MouseAxis = 2,
    GamepadButton = 3,
    GamepadAxis = 4,
    Touch = 5,
    TouchAxis = 6,
};

enum class InputModifierKind : std::uint8_t
{
    Deadzone = 0,
    Scale = 1,
    Invert = 2,
    Swizzle = 3,
    Normalize = 4,
    Clamp = 5,
    Curve = 6,
};

enum class InputTriggerKind : std::uint8_t
{
    Pressed = 0,
    Released = 1,
    Hold = 2,
    Tap = 3,
    Pulse = 4,
    Repeat = 5,
};

enum class InputEventType : std::uint8_t
{
    Started = 0,
    Triggered = 1,
    Completed = 2,
    Canceled = 3,
};

struct InputSource
{
    InputSourceKind Kind = InputSourceKind::Key;
    std::uint16_t Code = 0;
};

struct InputModifier
{
    InputModifierKind Kind = InputModifierKind::Deadzone;
    float A = 0.2f;
    float B = 0.0f;
};

struct InputActionDesc
{
    std::array<char, 32> Name{};
    InputActionType Type = InputActionType::Bool;
};

struct InputContextDesc
{
    std::array<char, 32> Name{};
    std::int32_t Priority = 0;
    bool Consume = false;
};

struct InputMappingDesc
{
    std::uint16_t Action = kInvalidInputId;
    std::uint16_t Context = kInvalidInputId;
    InputSource Source{};
    std::array<InputModifier, kMaxInputModifiers> Modifiers{};
    std::uint8_t ModifierCount = 0;
    InputTriggerKind Trigger = InputTriggerKind::Pressed;
    // Hold/Tap duration, or Pulse/Repeat interval in seconds.
    float TriggerA = 0.0f;
    // Repeat interval; zero reuses TriggerA.
    float TriggerB = 0.0f;
};

struct InputActionState
{
    float X = 0.0f;
    float Y = 0.0f;
    bool Started = false;
    bool Triggered = false;
    bool Completed = false;
    bool Canceled = false;
    float HeldSeconds = 0.0f;
    std::uint64_t LastEventTick = 0;
};

struct InputEvent
{
    std::uint16_t Action = kInvalidInputId;
    InputEventType Type = InputEventType::Triggered;
    float X = 0.0f;
    float Y = 0.0f;
    std::uint64_t Tick = 0;
    std::uint64_t TimestampNs = 0;
};

// Fixed-capacity action mapper. It has no virtual functions and no heap path:
// the same data is valid for the editor, a cooked game and a rollback replay.
class InputSystem
{
public:
    InputSystem() = default;

    void Reset() noexcept;

    [[nodiscard]] std::uint16_t RegisterAction(std::string_view InName, InputActionType InType) noexcept;
    [[nodiscard]] std::uint16_t RegisterContext(std::string_view InName, std::int32_t InPriority,
                                                bool InConsume) noexcept;
    [[nodiscard]] bool AddMapping(const InputMappingDesc& InMapping) noexcept;
    // Runtime/persisted binding change; the game owns the save format.
    [[nodiscard]] bool Rebind(std::uint32_t InMappingIndex, const InputSource& InSource) noexcept;
    [[nodiscard]] const InputMappingDesc* Mapping(std::uint32_t InMappingIndex) const noexcept;

    // ECS binding: the entity carries InputUserComponent, while the system owns
    // the fixed action/event storage for that user slot.
    [[nodiscard]] bool BindUser(Entity InEntity, std::uint32_t InUserId, std::uint32_t InGamepadSlot) noexcept;
    [[nodiscard]] bool PushContext(std::uint32_t InUserId, std::uint16_t InContext) noexcept;
    [[nodiscard]] bool PopContext(std::uint32_t InUserId, std::uint16_t InContext) noexcept;
    void SetGamepadSlot(std::uint32_t InUserId, std::uint32_t InGamepadSlot) noexcept;
    // Call once per fixed tick, before gameplay systems read actions.
    void Update(const InputState& InInput, std::uint64_t InTick, std::uint64_t InTimestampNs, float InDeltaSeconds) noexcept;

    [[nodiscard]] const InputActionState* Action(std::uint32_t InUserId, std::uint16_t InAction) const noexcept;
    [[nodiscard]] std::span<const InputEvent> Events(std::uint32_t InUserId) const noexcept;
    [[nodiscard]] const InputActionDesc* ActionDesc(std::uint16_t InAction) const noexcept;
    [[nodiscard]] const InputContextDesc* ContextDesc(std::uint16_t InContext) const noexcept;
    [[nodiscard]] std::uint32_t ActionCount() const noexcept
    {
        return MActionCount;
    }
    [[nodiscard]] std::uint32_t MappingCount() const noexcept
    {
        return MMappingCount;
    }
    [[nodiscard]] std::uint32_t UserCount() const noexcept
    {
        return MUserCount;
    }
    [[nodiscard]] std::uint32_t DroppedEvents() const noexcept
    {
        return MDroppedEvents;
    }

private:
    struct UserSlot
    {
        bool Bound = false;
        Entity Owner = Fby::kNullEntity;
        std::uint32_t GamepadSlot = 0xFFFFFFFFu;
        std::array<std::uint16_t, kMaxInputContextStack> Contexts{};
        std::uint32_t ContextCount = 0;
        std::array<InputActionState, kMaxInputActions> Actions{};
        std::array<InputEvent, kMaxInputEvents> Events{};
        std::uint32_t EventCount = 0;
    };

    [[nodiscard]] bool IsContextActive(const UserSlot& InUser, std::uint16_t InContext) const noexcept;
    [[nodiscard]] bool MappingAllowed(const UserSlot& InUser, std::uint16_t InContext) const noexcept;
    [[nodiscard]] float ReadSource(const InputState& InInput, const UserSlot& InUser, const InputSource& InSource,
                                   bool* OutPressed, bool* OutReleased, bool* OutHeld) const noexcept;
    [[nodiscard]] Vector2D ApplyModifiers(const InputMappingDesc& InMapping, Vector2D InValue) const noexcept;
    void EmitEvent(UserSlot& InUser, std::uint16_t InAction, InputEventType InType, Vector2D InValue,
                   std::uint64_t InTick, std::uint64_t InTimestampNs) noexcept;

    std::array<InputActionDesc, kMaxInputActions> MActions{};
    std::uint32_t MActionCount = 0;
    std::array<InputContextDesc, kMaxInputContexts> MContexts{};
    std::uint32_t MContextCount = 0;
    std::array<InputMappingDesc, kMaxInputMappings> MMappings{};
    std::uint32_t MMappingCount = 0;
    std::array<UserSlot, kMaxInputUsers> MUsers{};
    std::uint32_t MUserCount = 0;
    std::uint32_t MDroppedEvents = 0;
};

}
