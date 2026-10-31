// FemboyEngine2D - presentation policy components: collision modes, render
// masks, per-entity time scale and the sleeping-static lifecycle.
#pragma once

#include "Engine/Core/Mirror.hpp"
#include "Engine/Core/Types.hpp"
#include "Engine/ECS/Schedule.hpp"
#include "Engine/Physics/Physics.hpp"

#include <cstdint>
#include <span>
#include <type_traits>

namespace Fby
{

// Participation level of an entity in collision and physics. The names form a
// ladder: NoCollision never touches the broad phase, QueryOnly and TriggerOnly
// report overlaps without a response, Simple modes use the Box2D polygon alone
// and Complex modes additionally test the SDF field.
enum class CollisionMode : std::uint8_t
{
    NoCollision = 0,
    QueryOnly = 1,
    TriggerOnly = 2,
    SimpleStatic = 3,
    SimpleDynamic = 4,
    ComplexStatic = 5,
    ComplexDynamic = 6,
};

inline constexpr std::uint32_t kCollisionModeCount = 7;

[[nodiscard]] constexpr bool CollisionModeIsValid(CollisionMode InMode) noexcept
{
    return static_cast<std::uint32_t>(InMode) < kCollisionModeCount;
}

// Static modes never integrate a velocity even when a RigidBody2D is present.
// NoCollision keeps the authored body type and only disables the shape filter,
// so a collision probe can still fall through geometry.
[[nodiscard]] constexpr bool CollisionModeIsStatic(CollisionMode InMode) noexcept
{
    return InMode == CollisionMode::SimpleStatic || InMode == CollisionMode::ComplexStatic;
}

// Sensors report overlap but produce no impulse.
[[nodiscard]] constexpr bool CollisionModeIsSensor(CollisionMode InMode) noexcept
{
    return InMode == CollisionMode::QueryOnly || InMode == CollisionMode::TriggerOnly;
}

// The modes below are resolved by Box2D and therefore need a body type.
[[nodiscard]] constexpr bool CollisionModeResolvesContacts(CollisionMode InMode) noexcept
{
    return InMode == CollisionMode::SimpleStatic || InMode == CollisionMode::SimpleDynamic ||
           InMode == CollisionMode::ComplexStatic || InMode == CollisionMode::ComplexDynamic;
}

// Complex modes also test the baked SDF field, so slopes and thin walls work.
[[nodiscard]] constexpr bool CollisionModeUsesSdf(CollisionMode InMode) noexcept
{
    return InMode == CollisionMode::ComplexStatic || InMode == CollisionMode::ComplexDynamic;
}

// Modes that feed the contact event queue. The editor's collision overlay and
// the gameplay layer both read the same queue.
[[nodiscard]] constexpr bool CollisionModeEmitsEvents(CollisionMode InMode) noexcept
{
    return InMode == CollisionMode::QueryOnly || InMode == CollisionMode::TriggerOnly ||
           CollisionModeResolvesContacts(InMode);
}

[[nodiscard]] constexpr const char* CollisionModeName(CollisionMode InMode) noexcept
{
    switch (InMode)
    {
        case CollisionMode::NoCollision:
            return "NoCollision";
        case CollisionMode::QueryOnly:
            return "QueryOnly";
        case CollisionMode::TriggerOnly:
            return "TriggerOnly";
        case CollisionMode::SimpleStatic:
            return "SimpleStatic";
        case CollisionMode::SimpleDynamic:
            return "SimpleDynamic";
        case CollisionMode::ComplexStatic:
            return "ComplexStatic";
        case CollisionMode::ComplexDynamic:
            return "ComplexDynamic";
    }
    return "NoCollision";
}

// The authored collision policy of one entity. Kept separate from Collider2D so
// a designer can change the participation level without rewriting the shape or
// the layer matrix, and so snapshots keep both. Mode is stored as UInt32 so the
// Mirror reflection, TOML scene format and inspector all handle it without an
// enum codec.
struct CollisionPolicyComponent
{
    std::uint32_t Mode = static_cast<std::uint32_t>(CollisionMode::SimpleDynamic);
    std::uint32_t Category = 1u;
    std::uint32_t Mask = 0xFFFFFFFFu;
    std::uint32_t Flags = 0;

    [[nodiscard]] constexpr CollisionMode GetMode() const noexcept
    {
        const std::uint32_t ScopedClamped = Mode < kCollisionModeCount ? Mode : 0u;
        return static_cast<CollisionMode>(ScopedClamped);
    }
    constexpr void SetMode(CollisionMode InMode) noexcept
    {
        Mode = static_cast<std::uint32_t>(InMode);
    }
};

static_assert(std::is_trivially_copyable_v<CollisionPolicyComponent>);
static_assert(std::is_standard_layout_v<CollisionPolicyComponent>);
static_assert(sizeof(CollisionPolicyComponent) == 16);

// Render passes an entity can opt out of. A render mask is a pair of masks so
// an authoring preset can exclude one pass while a later override includes it:
// Include gates the pass, Exclude wins when both are set.
enum RenderPassMask : std::uint32_t
{
    RenderPassNone = 0u,
    RenderPassSprites = 1u << 0,
    RenderPassLighting = 1u << 1,
    RenderPassShadows = 1u << 2,
    RenderPassLightSources = 1u << 3,
    RenderPassColliders = 1u << 4,
    RenderPassOverlays = 1u << 5,
    RenderPassSdf = 1u << 6,
    RenderPassAll = 0xFFFFFFFFu,
};

struct RenderMaskComponent
{
    std::uint32_t Include = RenderPassAll;
    std::uint32_t Exclude = RenderPassNone;

    [[nodiscard]] constexpr bool Allows(std::uint32_t InPass) const noexcept
    {
        return (Include & InPass) != 0 && (Exclude & InPass) == 0;
    }
    constexpr void ExcludePass(std::uint32_t InPass) noexcept
    {
        Exclude |= InPass;
    }
    constexpr void IncludePass(std::uint32_t InPass) noexcept
    {
        Include |= InPass;
        Exclude &= ~InPass;
    }
};

static_assert(std::is_trivially_copyable_v<RenderMaskComponent>);
static_assert(std::is_standard_layout_v<RenderMaskComponent>);
static_assert(sizeof(RenderMaskComponent) == 8);

// Optional per-entity clock. A scale of zero freezes the body: velocity and
// angular velocity are zeroed and the Box2D body is disabled, so a stopped
// entity neither integrates nor receives impulses. A scale in (0, 1) scales
// authored impulses through ApplyImpulse; the global physics clock supplies the
// world-wide slow motion.
struct TimeScaleComponent
{
    float Scale = 1.0f;
    float Reserved = 0.0f;

    [[nodiscard]] constexpr float Clamped() const noexcept
    {
        return Scale > 0.0f ? Scale : 0.0f;
    }
};

static_assert(std::is_trivially_copyable_v<TimeScaleComponent>);
static_assert(std::is_standard_layout_v<TimeScaleComponent>);
static_assert(sizeof(TimeScaleComponent) == 8);

// Why a sleeping-static entity left the sleeping state. Stored as UInt32 so the
// inspector and the diagnostics panel can show the last cause without a codec.
inline constexpr std::uint32_t kStaticWakeNone = 0;
inline constexpr std::uint32_t kStaticWakeContact = 1;
inline constexpr std::uint32_t kStaticWakeTrigger = 2;
inline constexpr std::uint32_t kStaticWakeProgram = 3;
inline constexpr std::uint32_t kStaticWakeSceneEdit = 4;

inline constexpr std::uint32_t kSleepingStaticFlagActive = 1u << 0;
inline constexpr std::uint32_t kSleepingStaticFlagSleeping = 1u << 1;
inline constexpr std::uint32_t kSleepingStaticFlagWakeOnContact = 1u << 2;
inline constexpr std::uint32_t kSleepingStaticFlagWakeOnTrigger = 1u << 3;
// Marks the owner as a light source so the wake also reclassifies its light.
inline constexpr std::uint32_t kSleepingStaticFlagLight = 1u << 4;

// The default authoring state of scenery: static until something touches it.
// Waking is one-way until a scene edit resets the component, which keeps the
// simulation deterministic: a contact on tick N wakes the entity for every
// replay of tick N.
struct SleepingStaticComponent
{
    std::uint32_t Flags = kSleepingStaticFlagActive | kSleepingStaticFlagSleeping | kSleepingStaticFlagWakeOnContact;
    std::uint32_t WakeCount = 0;
    std::uint32_t LastWakeReason = kStaticWakeNone;
    std::uint32_t Reserved = 0;

    [[nodiscard]] constexpr bool IsSleeping() const noexcept
    {
        return (Flags & kSleepingStaticFlagSleeping) != 0;
    }
    [[nodiscard]] constexpr bool WakesOnContact() const noexcept
    {
        return (Flags & kSleepingStaticFlagWakeOnContact) != 0;
    }
    [[nodiscard]] constexpr bool WakesOnTrigger() const noexcept
    {
        return (Flags & kSleepingStaticFlagWakeOnTrigger) != 0;
    }
};

static_assert(std::is_trivially_copyable_v<SleepingStaticComponent>);
static_assert(std::is_standard_layout_v<SleepingStaticComponent>);
static_assert(sizeof(SleepingStaticComponent) == 16);

// True when the entity has no mask or when the mask includes the pass.
[[nodiscard]] inline bool EntityAllowsRenderPass(const Ecs::Registry& InRegistry, Entity InEntity,
                                                 std::uint32_t InPass) noexcept
{
    const auto* ScopedMask = InRegistry.try_get<RenderMaskComponent>(InEntity);
    return ScopedMask == nullptr || ScopedMask->Allows(InPass);
}

// Missing component means the default clock.
[[nodiscard]] inline float EntityTimeScale(const Ecs::Registry& InRegistry, Entity InEntity) noexcept
{
    const auto* ScopedScale = InRegistry.try_get<TimeScaleComponent>(InEntity);
    return ScopedScale == nullptr ? 1.0f : ScopedScale->Clamped();
}

[[nodiscard]] inline bool EntityTimeFrozen(const Ecs::Registry& InRegistry, Entity InEntity) noexcept
{
    return EntityTimeScale(InRegistry, InEntity) <= 0.0f;
}

// Puts a sleeping-static entity to sleep again after a scene edit. The caller
// is responsible for restoring the authored body type.
void ResetSleepingStatic(Ecs::Registry& InRegistry, Entity InEntity) noexcept;

// Wakes one sleeping-static entity. Waking a rigid body switches it to a
// dynamic body for the next physics step; waking a light clears the static bits
// on its light components so the next solve treats it as dynamic.
void WakeSleepingStatic(Ecs::Registry& InRegistry, Entity InEntity, std::uint32_t InReason) noexcept;

// Scans one contact queue and wakes both participants. Returns the number of
// entities that changed state; the physics bridge calls this right after the
// contact events are collected.
[[nodiscard]] std::uint32_t WakeSleepingStaticFromContacts(Ecs::Registry& InRegistry,
                                                           std::span<const PhysicsContactEvent> InContacts) noexcept;

// Applies a collision policy to the owner's Collider2D during the next physics
// step. The bridge calls it before creating bodies, so a mode change never
// leaves a stale shape filter behind.
void ApplyCollisionPolicyToCollider(Ecs::Registry& InRegistry, Entity InEntity,
                                    CollisionPolicyComponent& InPolicy) noexcept;
}

FBY_REFLECT_COMPONENT(Fby::CollisionPolicyComponent, "CollisionPolicyComponent",
                      FBY_FIELD(Fby::CollisionPolicyComponent, Mode, UInt32),
                      FBY_FIELD(Fby::CollisionPolicyComponent, Category, UInt32),
                      FBY_FIELD(Fby::CollisionPolicyComponent, Mask, UInt32),
                      FBY_FIELD(Fby::CollisionPolicyComponent, Flags, UInt32))

FBY_REFLECT_COMPONENT(Fby::RenderMaskComponent, "RenderMaskComponent",
                      FBY_FIELD(Fby::RenderMaskComponent, Include, UInt32),
                      FBY_FIELD(Fby::RenderMaskComponent, Exclude, UInt32))

FBY_REFLECT_COMPONENT(Fby::TimeScaleComponent, "TimeScaleComponent",
                      FBY_FIELD(Fby::TimeScaleComponent, Scale, Float),
                      FBY_FIELD(Fby::TimeScaleComponent, Reserved, Float))

FBY_REFLECT_COMPONENT(Fby::SleepingStaticComponent, "SleepingStaticComponent",
                      FBY_FIELD(Fby::SleepingStaticComponent, Flags, UInt32),
                      FBY_FIELD(Fby::SleepingStaticComponent, WakeCount, UInt32),
                      FBY_FIELD(Fby::SleepingStaticComponent, LastWakeReason, UInt32),
                      FBY_FIELD(Fby::SleepingStaticComponent, Reserved, UInt32))
