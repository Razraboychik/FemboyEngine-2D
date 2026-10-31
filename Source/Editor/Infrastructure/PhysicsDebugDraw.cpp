// FemboyEngine2D - Viewport physics debug draw implementation.
#include "Editor/Infrastructure/PhysicsDebugDraw.hpp"

#include "Editor/Infrastructure/EditorTheme.hpp"
#include "Engine/Physics/Cloth.hpp"
#include "Engine/Physics/Physics.hpp"
#include "Engine/Physics/PhysicsJoints.hpp"
#include "Engine/Physics/SdfCollision.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace Fby::Editor
{
namespace
{
constexpr Color32 kColorStatic{0xFF938A8Au};
constexpr Color32 kColorKinematic{0xFFF7C34Fu};
constexpr Color32 kColorDynamic{0xFF92D97Cu};
constexpr Color32 kColorSensor{0xFF3DB3F2u};
constexpr Color32 kColorVelocity{0xFFFFB662u};
constexpr Color32 kColorRope{0xFF9E5DE8u};
constexpr Color32 kColorCloth{0xFFFF7D9Cu};
constexpr Color32 kColorSdfNormal{0xFF7A5CFFu};
constexpr Color32 kColorContact{0xFF66D1FFu};

[[nodiscard]] Color32 BodyColor(BodyType InType, bool InSensor) noexcept
{
    if (InSensor)
    {
        return kColorSensor;
    }
    switch (InType)
    {
        case BodyType::Static:
            return kColorStatic;
        case BodyType::Kinematic:
            return kColorKinematic;
        case BodyType::Dynamic:
            return kColorDynamic;
    }
    return kColorStatic;
}

[[nodiscard]] Vector2D Rotate(Vector2D InValue, float InRadians) noexcept
{
    const float ScopedCos = std::cos(InRadians);
    const float ScopedSin = std::sin(InRadians);
    return Vector2D{InValue.X * ScopedCos - InValue.Y * ScopedSin, InValue.X * ScopedSin + InValue.Y * ScopedCos};
}

[[nodiscard]] std::uint32_t ToImGuiColor(Color32 InColor) noexcept
{
    const std::uint32_t ScopedRgb = (static_cast<std::uint32_t>(InColor.B()) << 16) |
                                    (static_cast<std::uint32_t>(InColor.G()) << 8) |
                                    static_cast<std::uint32_t>(InColor.R());
    const ImVec4 ScopedLinear = HexColor(ScopedRgb, static_cast<float>(InColor.A()) / 255.0f);
    return ImGui::ColorConvertFloat4ToU32(ScopedLinear);
}
}

void PhysicsDebugVisualizer::Clear() noexcept
{
    MLineCount = 0;
    MPointCount = 0;
    MTruncated = false;
    MFrame = PhysicsDebugFrame{};
}

void PhysicsDebugVisualizer::AddLine(Vector2D InA, Vector2D InB, Color32 InColor, float InThickness) noexcept
{
    if (MLineCount >= kMaxLines)
    {
        MTruncated = true;
        return;
    }
    MLines[MLineCount] = PhysicsDebugLine{InA, InB, InColor, InThickness};
    ++MLineCount;
}

void PhysicsDebugVisualizer::AddPoint(Vector2D InPosition, Color32 InColor, float InRadius) noexcept
{
    if (MPointCount >= kMaxPoints)
    {
        MTruncated = true;
        return;
    }
    MPoints[MPointCount] = PhysicsDebugPoint{InPosition, InColor, InRadius};
    ++MPointCount;
}

void PhysicsDebugVisualizer::AddBox(Vector2D InCenter, float InRotation, Vector2D InHalfExtents,
                                    Color32 InColor) noexcept
{
    Vector2D ScopedCorners[4];
    ScopedCorners[0] = InCenter + Rotate(Vector2D{-InHalfExtents.X, -InHalfExtents.Y}, InRotation);
    ScopedCorners[1] = InCenter + Rotate(Vector2D{InHalfExtents.X, -InHalfExtents.Y}, InRotation);
    ScopedCorners[2] = InCenter + Rotate(Vector2D{InHalfExtents.X, InHalfExtents.Y}, InRotation);
    ScopedCorners[3] = InCenter + Rotate(Vector2D{-InHalfExtents.X, InHalfExtents.Y}, InRotation);
    for (std::uint32_t ScopedEdge = 0; ScopedEdge < 4; ++ScopedEdge)
    {
        AddLine(ScopedCorners[ScopedEdge], ScopedCorners[(ScopedEdge + 1) % 4], InColor);
    }
}

void PhysicsDebugVisualizer::AddCircle(Vector2D InCenter, float InRadius, Color32 InColor,
                                       std::uint32_t InSegments) noexcept
{
    const std::uint32_t ScopedSegments = std::max<std::uint32_t>(InSegments, 4);
    Vector2D ScopedPrevious = InCenter + Vector2D{InRadius, 0.0f};
    for (std::uint32_t ScopedSegment = 1; ScopedSegment <= ScopedSegments; ++ScopedSegment)
    {
        const float ScopedAngle =
            6.28318530718f * static_cast<float>(ScopedSegment) / static_cast<float>(ScopedSegments);
        const Vector2D ScopedCurrent =
            InCenter + Vector2D{InRadius * std::cos(ScopedAngle), InRadius * std::sin(ScopedAngle)};
        AddLine(ScopedPrevious, ScopedCurrent, InColor);
        ScopedPrevious = ScopedCurrent;
    }
}

void PhysicsDebugVisualizer::Build(Ecs::SystemContext& InContext, std::uint32_t InFlags,
                                   const SdfCollisionSystem* InSdf)
{
    Clear();
    if (InFlags == PhysicsDebugNone)
    {
        return;
    }
    BuildBodies(InContext, InFlags);
    BuildRopes(InContext, InFlags);
    BuildCloth(InContext, InFlags);
    BuildSdf(InFlags, InSdf);
    BuildContacts(InContext, InFlags);

    MFrame.lines = std::span<const PhysicsDebugLine>(MLines.data(), MLineCount);
    MFrame.points = std::span<const PhysicsDebugPoint>(MPoints.data(), MPointCount);
    MFrame.truncated = MTruncated;
}

void PhysicsDebugVisualizer::BuildBodies(Ecs::SystemContext& InContext, std::uint32_t InFlags) noexcept
{
    if ((InFlags & (PhysicsDebugBodies | PhysicsDebugVelocity)) == 0)
    {
        return;
    }
    auto ScopedView = InContext.registry.view<RigidBody2D, Collider2D>();
    for (const Entity ScopedEntity : ScopedView)
    {
        const RigidBody2D& ScopedBody = InContext.registry.get<RigidBody2D>(ScopedEntity);
        const Collider2D& ScopedCollider = InContext.registry.get<Collider2D>(ScopedEntity);
        const Color32 ScopedColor = BodyColor(ScopedBody.Type, ScopedCollider.Sensor);

        if ((InFlags & PhysicsDebugBodies) != 0)
        {
            AddBox(ScopedBody.Position, ScopedBody.Rotation, ScopedCollider.HalfExtents, ScopedColor);
            // A small cross marks the body origin, which is what the physics
            constexpr float kCross = 3.0f;
            AddLine(ScopedBody.Position + Vector2D{-kCross, 0.0f}, ScopedBody.Position + Vector2D{kCross, 0.0f},
                    ScopedColor, 1.0f);
            AddLine(ScopedBody.Position + Vector2D{0.0f, -kCross}, ScopedBody.Position + Vector2D{0.0f, kCross},
                    ScopedColor, 1.0f);
            ++MFrame.bodies;
        }

        if ((InFlags & PhysicsDebugVelocity) != 0)
        {
            const Vector2D ScopedTip = ScopedBody.Position + ScopedBody.LinearVelocity * 0.10f;
            AddLine(ScopedBody.Position, ScopedTip, kColorVelocity, 2.0f);
            AddPoint(ScopedTip, kColorVelocity, 2.0f);
        }
    }
}

void PhysicsDebugVisualizer::BuildRopes(Ecs::SystemContext& InContext, std::uint32_t InFlags) noexcept
{
    if ((InFlags & PhysicsDebugRopes) == 0)
    {
        return;
    }
    const float ScopedPixelsPerMeter =
        InContext.physics != nullptr ? InContext.physics->PixelsPerMeter() : 50.0f;
    auto ScopedView = InContext.registry.view<JointLinkComponent, RigidBody2D>();
    for (const Entity ScopedEntity : ScopedView)
    {
        const JointLinkComponent& ScopedLink = InContext.registry.get<JointLinkComponent>(ScopedEntity);
        const RigidBody2D& ScopedBody = InContext.registry.get<RigidBody2D>(ScopedEntity);
        const Vector2D ScopedAxis =
            Rotate(Vector2D{1.0f, 0.0f}, ScopedBody.Rotation) * (ScopedLink.HalfLength * ScopedPixelsPerMeter);
        const float ScopedRadius = std::max(1.0f, ScopedLink.Radius * ScopedPixelsPerMeter);
        // Capsule: a spine line plus a circle at both ends is the cheapest
        AddLine(ScopedBody.Position - ScopedAxis, ScopedBody.Position + ScopedAxis, kColorRope, 2.0f);
        AddCircle(ScopedBody.Position - ScopedAxis, ScopedRadius, kColorRope, 8);
        AddCircle(ScopedBody.Position + ScopedAxis, ScopedRadius, kColorRope, 8);
        ++MFrame.ropes;
    }
}

void PhysicsDebugVisualizer::BuildCloth(Ecs::SystemContext& InContext, std::uint32_t InFlags) noexcept
{
    if ((InFlags & PhysicsDebugCloth) == 0 || InContext.cloth == nullptr)
    {
        return;
    }
    auto ScopedView = InContext.registry.view<ClothComponent>();
    for (const Entity ScopedEntity : ScopedView)
    {
        const ClothComponent& ScopedComponent = InContext.registry.get<ClothComponent>(ScopedEntity);
        const ClothHandle ScopedHandle{ScopedComponent.ClothIndex, ScopedComponent.Generation,
                                       ScopedComponent.ClothID};
        if (!InContext.cloth->IsValid(ScopedHandle))
        {
            continue;
        }
        const ClothInfo ScopedInfo = InContext.cloth->Info(ScopedHandle);
        const std::span<const Vector2D> ScopedParticles = InContext.cloth->ParticlePositions(ScopedHandle);
        if (ScopedInfo.columns < 2 || ScopedInfo.rows < 2 ||
            ScopedParticles.size() < static_cast<std::size_t>(ScopedInfo.columns) * ScopedInfo.rows)
        {
            continue;
        }
        const auto ScopedAt = [&](std::uint32_t InX, std::uint32_t InY) -> Vector2D
        {
            return ScopedParticles[static_cast<std::size_t>(InY) * ScopedInfo.columns + InX];
        };
        for (std::uint32_t ScopedY = 0; ScopedY < ScopedInfo.rows; ++ScopedY)
        {
            for (std::uint32_t ScopedX = 0; ScopedX + 1 < ScopedInfo.columns; ++ScopedX)
            {
                AddLine(ScopedAt(ScopedX, ScopedY), ScopedAt(ScopedX + 1, ScopedY), kColorCloth, 1.0f);
            }
        }
        for (std::uint32_t ScopedX = 0; ScopedX < ScopedInfo.columns; ++ScopedX)
        {
            for (std::uint32_t ScopedY = 0; ScopedY + 1 < ScopedInfo.rows; ++ScopedY)
            {
                AddLine(ScopedAt(ScopedX, ScopedY), ScopedAt(ScopedX, ScopedY + 1), kColorCloth, 1.0f);
            }
        }
        ++MFrame.cloths;
    }
}

void PhysicsDebugVisualizer::BuildSdf(std::uint32_t InFlags, const SdfCollisionSystem* InSdf) noexcept
{
    if ((InFlags & PhysicsDebugSdf) == 0 || InSdf == nullptr)
    {
        return;
    }
    for (const SdfContact& ScopedContact : InSdf->Contacts())
    {
        // The push-out vector is drawn from the contact point along the field
        const float ScopedLength = std::max(6.0f, ScopedContact.penetration);
        const Vector2D ScopedTip = ScopedContact.point + ScopedContact.normal * ScopedLength;
        AddLine(ScopedContact.point, ScopedTip, kColorSdfNormal, 2.0f);
        AddCircle(ScopedContact.point, 2.5f, kColorSdfNormal, 6);
        ++MFrame.sdfContacts;
    }
}

void PhysicsDebugVisualizer::BuildContacts(Ecs::SystemContext& InContext, std::uint32_t InFlags) noexcept
{
    if ((InFlags & PhysicsDebugContacts) == 0 || InContext.physics == nullptr)
    {
        return;
    }
    for (const PhysicsContactEvent& ScopedEvent : InContext.physics->Contacts())
    {
        if (ScopedEvent.type != PhysicsEventType::BeginTouch)
        {
            continue;
        }
        AddPoint(ScopedEvent.point, ScopedEvent.speed > 0.0f ? kColorContact : kColorStatic, 3.0f);
        AddLine(ScopedEvent.point, ScopedEvent.point + ScopedEvent.normal * 14.0f, kColorContact, 1.5f);
    }
}

void PhysicsDebugVisualizer::DrawImGui(void* InDrawList, void* InUser, ScreenMapper InMapper) const
{
    if (InDrawList == nullptr || InMapper == nullptr)
    {
        return;
    }
    auto* ScopedDrawList = static_cast<ImDrawList*>(InDrawList);
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MLineCount; ++ScopedIndex)
    {
        const PhysicsDebugLine& ScopedLine = MLines[ScopedIndex];
        const Vector2D ScopedA = InMapper(InUser, ScopedLine.a);
        const Vector2D ScopedB = InMapper(InUser, ScopedLine.b);
        ScopedDrawList->AddLine(ImVec2(ScopedA.X, ScopedA.Y), ImVec2(ScopedB.X, ScopedB.Y),
                                ToImGuiColor(ScopedLine.color), ScopedLine.thickness);
    }
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MPointCount; ++ScopedIndex)
    {
        const PhysicsDebugPoint& ScopedPoint = MPoints[ScopedIndex];
        const Vector2D ScopedScreen = InMapper(InUser, ScopedPoint.position);
        ScopedDrawList->AddCircleFilled(ImVec2(ScopedScreen.X, ScopedScreen.Y), ScopedPoint.radius,
                                        ToImGuiColor(ScopedPoint.color));
    }
}
}
