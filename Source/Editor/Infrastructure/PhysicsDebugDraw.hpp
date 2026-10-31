// FemboyEngine2D - Viewport physics debug draw.
#pragma once

#include "Engine/Core/Types.hpp"
#include "Engine/ECS/Schedule.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace Fby
{
class SdfCollisionSystem;
}

namespace Fby::Editor
{

enum PhysicsDebugFlags : std::uint32_t
{
    PhysicsDebugNone = 0,
    PhysicsDebugBodies = 1u << 0,
    PhysicsDebugVelocity = 1u << 1,
    PhysicsDebugRopes = 1u << 2,
    PhysicsDebugCloth = 1u << 3,
    PhysicsDebugSdf = 1u << 4,
    PhysicsDebugContacts = 1u << 5,
    PhysicsDebugAll = PhysicsDebugBodies | PhysicsDebugVelocity | PhysicsDebugRopes | PhysicsDebugCloth |
                      PhysicsDebugSdf | PhysicsDebugContacts,
};

struct PhysicsDebugLine
{
    Vector2D a{};
    Vector2D b{};
    Color32 color{};
    float thickness = 1.0f;
};

struct PhysicsDebugPoint
{
    Vector2D position{};
    Color32 color{};
    float radius = 2.0f;
};

struct PhysicsDebugFrame
{
    std::span<const PhysicsDebugLine> lines{};
    std::span<const PhysicsDebugPoint> points{};
    std::uint32_t bodies = 0;
    std::uint32_t ropes = 0;
    std::uint32_t cloths = 0;
    std::uint32_t sdfContacts = 0;
    bool truncated = false;
};

class PhysicsDebugVisualizer
{
public:
    static constexpr std::uint32_t kMaxLines = 12000;
    static constexpr std::uint32_t kMaxPoints = 512;

    // World pixels -> screen pixels. `InUser` is the caller's camera/rect state.
    using ScreenMapper = Vector2D (*)(void* InUser, Vector2D InWorldPixels);

    void Build(Ecs::SystemContext& InContext, std::uint32_t InFlags = PhysicsDebugAll,
               const SdfCollisionSystem* InSdf = nullptr);
    void Clear() noexcept;

    [[nodiscard]] const PhysicsDebugFrame& Frame() const noexcept
    {
        return MFrame;
    }
    [[nodiscard]] std::uint32_t LineCount() const noexcept
    {
        return MLineCount;
    }
    [[nodiscard]] std::uint32_t PointCount() const noexcept
    {
        return MPointCount;
    }

    // `InDrawList` is an ImDrawList*. Kept as void* so this header does not pull
    void DrawImGui(void* InDrawList, void* InUser, ScreenMapper InMapper) const;

private:
    void AddLine(Vector2D InA, Vector2D InB, Color32 InColor, float InThickness = 1.0f) noexcept;
    void AddPoint(Vector2D InPosition, Color32 InColor, float InRadius = 2.0f) noexcept;
    void AddBox(Vector2D InCenter, float InRotation, Vector2D InHalfExtents, Color32 InColor) noexcept;
    void AddCircle(Vector2D InCenter, float InRadius, Color32 InColor, std::uint32_t InSegments = 12) noexcept;

    void BuildBodies(Ecs::SystemContext& InContext, std::uint32_t InFlags) noexcept;
    void BuildRopes(Ecs::SystemContext& InContext, std::uint32_t InFlags) noexcept;
    void BuildCloth(Ecs::SystemContext& InContext, std::uint32_t InFlags) noexcept;
    void BuildSdf(std::uint32_t InFlags, const SdfCollisionSystem* InSdf) noexcept;
    void BuildContacts(Ecs::SystemContext& InContext, std::uint32_t InFlags) noexcept;

    std::array<PhysicsDebugLine, kMaxLines> MLines{};
    std::array<PhysicsDebugPoint, kMaxPoints> MPoints{};
    PhysicsDebugFrame MFrame{};
    std::uint32_t MLineCount = 0;
    std::uint32_t MPointCount = 0;
    bool MTruncated = false;
};
}
