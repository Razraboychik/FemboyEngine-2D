// FemboyEngine2D - persistent prefab instance link and transient children.
#pragma once

#include "Engine/Assets/AssetMeta.hpp"
#include "Engine/Core/AssetId.hpp"
#include "Engine/Core/Mirror.hpp"
#include "Engine/ECS/CommandBuffer.hpp"
#include "Engine/ECS/FCS/Registry.hpp"
#include "Engine/ECS/PrefabSystem.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace Fby
{

// Scene proxy for a prefab. The proxy is serialized; its children are tagged
// with PrefabInstanceMemberComponent and skipped by scene capture, then rebuilt
// from the asset on load.
struct PrefabInstanceComponent
{
    Assets::AssetHandle Prefab{};
    Vector2D Origin{};
    std::uint32_t Flags = 0;
    std::uint32_t Reserved = 0;
    // Per-prefab-entity overrides. Masks select active entries; the arrays keep
    // absolute values so the scene format can round-trip them without reflection
    // support for struct arrays.
    std::uint32_t PositionMask = 0;
    std::uint32_t ScaleMask = 0;
    std::uint32_t RotationMask = 0;
    std::uint32_t ColorMask = 0;
    std::array<Vector2D, kPrefabMaxEntities> PositionOverrides{};
    std::array<Vector2D, kPrefabMaxEntities> ScaleOverrides{};
    std::array<Vector2D, kPrefabMaxEntities> RotationOverrides{};
    // X = (r, g), Y = (b, a), each channel normalised to [0, 1].
    std::array<Vector2D, kPrefabMaxEntities> ColorOverrides{};
};

// Marks a spawned child of a prefab instance. Not serialized on its own: the
// instance proxy owns the link and the child is recreated on load.
struct PrefabInstanceMemberComponent
{
    Entity Instance = kNullEntity;
    std::uint32_t PrefabIndex = 0;
    std::uint32_t Reserved = 0;
};

class PrefabInstance
{
public:
    // Spawns the children for InInstance and tags every child with its prefab
    // index. Components are queued in InCommands; the caller plays the buffer.
    [[nodiscard]] static bool Expand(Ecs::Registry& InRegistry, Ecs::CommandBuffer& InCommands, Entity InInstance,
                                     const PrefabAsset& InAsset, std::span<Entity> OutChildren = {});

    // Destroys the transient children linked to InInstance.
    static void ClearChildren(Ecs::Registry& InRegistry, Entity InInstance);

    // Stores a per-entity override on the instance proxy; Expand applies it.
    [[nodiscard]] static bool SetOverride(Ecs::Registry& InRegistry, Entity InInstance,
                                          const PrefabEntityOverride& InOverride);
    static void ClearOverrides(Ecs::Registry& InRegistry, Entity InInstance);
};

}

FBY_REFLECT_COMPONENT(Fby::PrefabInstanceComponent, "PrefabInstanceComponent",
                      FBY_FIELD(Fby::PrefabInstanceComponent, Prefab, UInt64),
                      FBY_FIELD(Fby::PrefabInstanceComponent, Origin, Vector2D),
                      FBY_FIELD(Fby::PrefabInstanceComponent, Flags, UInt32),
                      FBY_FIELD(Fby::PrefabInstanceComponent, Reserved, UInt32),
                      FBY_FIELD(Fby::PrefabInstanceComponent, PositionMask, UInt32),
                      FBY_FIELD(Fby::PrefabInstanceComponent, ScaleMask, UInt32),
                      FBY_FIELD(Fby::PrefabInstanceComponent, RotationMask, UInt32),
                      FBY_FIELD(Fby::PrefabInstanceComponent, ColorMask, UInt32),
                      FBY_FIELD(Fby::PrefabInstanceComponent, PositionOverrides, Vector2DArray),
                      FBY_FIELD(Fby::PrefabInstanceComponent, ScaleOverrides, Vector2DArray),
                      FBY_FIELD(Fby::PrefabInstanceComponent, RotationOverrides, Vector2DArray),
                      FBY_FIELD(Fby::PrefabInstanceComponent, ColorOverrides, Vector2DArray))

FBY_REFLECT_COMPONENT(Fby::PrefabInstanceMemberComponent, "PrefabInstanceMemberComponent",
                      FBY_FIELD(Fby::PrefabInstanceMemberComponent, Instance, Entity),
                      FBY_FIELD(Fby::PrefabInstanceMemberComponent, PrefabIndex, UInt32),
                      FBY_FIELD(Fby::PrefabInstanceMemberComponent, Reserved, UInt32))
