// FemboyEngine2D - destructible object: a local SDF carved by subtraction ops.
#pragma once

#include "Engine/Core/Types.hpp"
#include "Engine/Physics/SdfBrickMap.hpp"
#include "Engine/Physics/SdfCollision.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Fby
{

enum class SdfCarveShape : std::uint8_t
{
    Box = 0,
    Circle = 1,
};

// One authored piece of a destructible object. The base shape is the intact
// object; every operation subtracts material from it.
struct SdfCarveOp
{
    SdfCarveShape Shape = SdfCarveShape::Circle;
    Vector2D Center{};
    Vector2D HalfExtents{1.0f, 1.0f};
    float Radius = 1.0f;
    float Angle = 0.0f;
};

static_assert(std::is_trivially_copyable_v<SdfCarveOp>);

struct SdfDestructibleDesc
{
    Vector2D Origin{};
    Vector2D CellSize{2.0f, 2.0f};
    std::uint32_t Width = 0;
    std::uint32_t Height = 0;
    // Intact object shape. The centre defaults to the grid centre; a fractured
    // piece keeps the parent's centre so its base shape stays in world place.
    SdfCarveShape BaseShape = SdfCarveShape::Box;
    Vector2D BaseCenter{};
    bool HasBaseCenter = false;
    Vector2D HalfExtents{32.0f, 32.0f};
    float Radius = 32.0f;
    float NarrowBand = 16.0f;
    float OutsideDistance = 1000.0f;
};

// A small local field that belongs to one destructible object. The op list is the
// source of truth: the dense samples and the brick map are caches of it, so a
// save stores the operations and a load replays them.
class SdfDestructible
{
public:
    inline static constexpr std::uint32_t kMaxOps = 64;

    [[nodiscard]] bool Configure(const SdfDestructibleDesc& InDesc) noexcept;
    [[nodiscard]] bool IsConfigured() const noexcept
    {
        return MDesc.Width > 1 && MDesc.Height > 1 && !MSamples.empty();
    }
    [[nodiscard]] const SdfDestructibleDesc& Desc() const noexcept
    {
        return MDesc;
    }

    // Appends one subtraction and rebakes only the bricks it touches. Returns
    // false when the op capacity is exhausted or the object is not configured.
    [[nodiscard]] bool Subtract(const SdfCarveOp& InOp) noexcept;
    // Replays the whole op list from the intact shape.
    void Rebuild() noexcept;

    [[nodiscard]] float Sample(Vector2D InWorld) const noexcept;
    [[nodiscard]] const SdfField& Field() const noexcept
    {
        return MField;
    }
    [[nodiscard]] const SdfBrickMap& Bricks() const noexcept
    {
        return MBricks;
    }
    [[nodiscard]] std::span<const SdfCarveOp> Ops() const noexcept
    {
        return {MOps.data(), MOpCount};
    }
    [[nodiscard]] std::uint64_t Version() const noexcept
    {
        return MVersion;
    }
    [[nodiscard]] std::uint32_t DirtyBricksRebaked() const noexcept
    {
        return MDirtyBricksRebaked;
    }

    // The op list is what a save stores: the object is rebuilt from it on load.
    [[nodiscard]] bool SaveOps(std::vector<std::byte>* OutBytes) const;
    [[nodiscard]] bool LoadOps(std::span<const std::byte> InBytes, std::string* InError = nullptr) noexcept;

private:
    [[nodiscard]] float Evaluate(Vector2D InWorld) const noexcept;
    void RebuildBricks() noexcept;

    SdfDestructibleDesc MDesc{};
    SdfField MField{};
    std::vector<float> MSamples;
    SdfBrickMap MBricks;
    std::vector<SdfCarveOp> MOps;
    std::uint32_t MOpCount = 0;
    std::uint32_t MDirtyBricksRebaked = 0;
    std::uint64_t MVersion = 0;
};

}
