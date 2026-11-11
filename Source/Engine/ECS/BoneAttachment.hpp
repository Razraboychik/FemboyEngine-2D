// FemboyEngine2D - sprite and effect attachment to a skeleton bone.
#pragma once

#include "Engine/Core/Mirror.hpp"
#include "Engine/Core/Types.hpp"
#include "Engine/ECS/Entity.hpp"
#include "Engine/ECS/Schedule.hpp"

#include <cstdint>
#include <type_traits>

namespace Fby
{

// The owning entity's Transform2D follows one bone of another entity's
// skeleton. A flipbook sprite attached this way mixes with a skeletal pose
// without becoming a skinned mesh: the skeleton drives the bones, the sprite
// is a normal Sprite2D child.
struct BoneAttachment2D
{
    Entity Skeleton = kNullEntity;
    std::uint16_t Bone = 0;
    // Bone-local offset and rotation
    Vector2D Offset{};
    float RotationOffset = 0.0f;
    // Additional scale on top of the bone's uniform scale
    Vector2D Scale{1.0f, 1.0f};
};

static_assert(std::is_trivially_copyable_v<BoneAttachment2D>);
static_assert(std::is_standard_layout_v<BoneAttachment2D>);

// Writes Transform2D from the bone's global matrix. It must run after the
// animation graph so the global matrices are current.
class BoneAttachmentSystem
{
public:
    void Run(Ecs::Registry& InRegistry) const;
    void Run(const Ecs::CoreSystemView& InView) const;
};

void RegisterBoneAttachmentSystem(Ecs::Schedule& InSchedule);
void RegisterBoneAttachmentSystem(Ecs::SystemGraph& InGraph);
}

FBY_REFLECT_COMPONENT(Fby::BoneAttachment2D, "BoneAttachment2D", FBY_FIELD(Fby::BoneAttachment2D, Skeleton, Entity),
                      FBY_FIELD(Fby::BoneAttachment2D, Bone, UInt32),
                      FBY_FIELD(Fby::BoneAttachment2D, Offset, Vector2D),
                      FBY_FIELD(Fby::BoneAttachment2D, RotationOffset, Float),
                      FBY_FIELD(Fby::BoneAttachment2D, Scale, Vector2D))
