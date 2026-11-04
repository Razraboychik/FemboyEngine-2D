// FemboyEngine2D - world distance field owner: bake, incremental edits, version.
#pragma once

#include "Engine/Core/Types.hpp"
#include "Engine/ECS/Schedule.hpp"
#include "Engine/Physics/SdfBrickMap.hpp"
#include "Engine/Physics/SdfCollision.hpp"

#include <cstdint>
#include <vector>

namespace Fby
{

struct TilemapComponent;
class SdfDestructible;

// A foreign field merged into the world. Destructible objects imprint their
// carved field here once they settle, so world rebakes keep the damage.
struct SdfWorldImprint
{
    Vector2D Origin{};
    Vector2D CellSize{1.0f, 1.0f};
    std::uint32_t Width = 0;
    std::uint32_t Height = 0;
    std::vector<float> Samples;
};

// Grid description of one world field. The cell size is the authoring unit: a
// 2048x2048 grid at four world units per cell covers an 8192-unit world.
struct SdfWorldDesc
{
    Vector2D Origin{};
    Vector2D CellSize{4.0f, 4.0f};
    std::uint32_t Width = 0;
    std::uint32_t Height = 0;
    // Distance band stored per brick, in world units.
    float NarrowBand = 32.0f;
    float OutsideDistance = 1000.0f;
};

static_assert(std::is_trivially_copyable_v<SdfWorldDesc>);

// One analytic source contributed by gameplay code rather than the registry.
struct SdfWorldBox
{
    Vector2D Center{};
    Vector2D HalfExtents{1.0f, 1.0f};
};

struct SdfWorldCircle
{
    Vector2D Center{};
    float Radius = 1.0f;
};

// Owns the dense field the engine's consumers already parse plus the sparse
// brick map that feeds the GPU. A bake is either full or restricted to the
// bricks marked dirty by an edit.
class SdfWorld
{
public:
    inline static constexpr std::uint32_t kMaxBoxes = 4096;
    inline static constexpr std::uint32_t kMaxCircles = 4096;

    [[nodiscard]] bool Configure(const SdfWorldDesc& InDesc) noexcept;
    [[nodiscard]] const SdfWorldDesc& Desc() const noexcept
    {
        return MDesc;
    }
    [[nodiscard]] bool IsConfigured() const noexcept
    {
        return MDesc.Width > 1 && MDesc.Height > 1 && !MSamples.empty();
    }

    void ClearSources() noexcept;
    // Returns false when the fixed source capacity is exhausted.
    [[nodiscard]] bool AddBox(Vector2D InCenter, Vector2D InHalfExtents) noexcept;
    [[nodiscard]] bool AddCircle(Vector2D InCenter, float InRadius) noexcept;

    // Recomputes every cell from the analytic sources.
    [[nodiscard]] bool BakeAll() noexcept;
    // Recomputes every cell from the reflected SdfShapeComponent entities.
    [[nodiscard]] bool BakeRegistry(Ecs::Registry& InRegistry) noexcept;
    // Appends the solid cells of a tilemap as box sources. Horizontal runs of
    // solid tiles merge into one box, so a full 64x64 map costs 64 sources
    // instead of 4096. Returns false when the source capacity is exhausted.
    [[nodiscard]] bool AddTilemap(const TilemapComponent& InTilemap) noexcept;
    // Copies a destructible field into the world and rebakes the bricks it
    // covers. The imprint survives later rebakes and saves with the world.
    [[nodiscard]] bool Imprint(const SdfDestructible& InObject) noexcept;
    // Removes every imprint and rebakes the affected bricks.
    void ClearImprints() noexcept;

    // Marks the bricks that overlap the rect as dirty. The rect is clipped to
    // the grid; an empty rect is ignored.
    void MarkDirtyRect(Vector2D InMin, Vector2D InMax) noexcept;
    // Rebakes up to `InMaxBricks` dirty bricks from the analytic sources and
    // commits the result. Returns true when the queue was drained.
    [[nodiscard]] bool ApplyDirtyBake(std::uint32_t InMaxBricks) noexcept;

    [[nodiscard]] std::uint32_t PendingDirtyBricks() const noexcept
    {
        return static_cast<std::uint32_t>(MDirtyBricks.size());
    }
    [[nodiscard]] std::uint64_t FieldVersion() const noexcept
    {
        return MFieldVersion;
    }
    [[nodiscard]] const SdfField& Field() const noexcept
    {
        return MField;
    }
    [[nodiscard]] const SdfBrickMap& Bricks() const noexcept
    {
        return MBricks;
    }

    // Serializes the current field into a `.fsdf` image.
    [[nodiscard]] bool SaveAsset(std::vector<std::byte>* OutBytes) const;

private:
    [[nodiscard]] float EvaluateSources(Vector2D InWorld) const noexcept;
    // True when any analytic source overlaps the rect expanded by the narrow
    // band. Bricks that no source touches bake as a uniform outside value.
    [[nodiscard]] bool BrickHasSources(Vector2D InMin, Vector2D InMax) const noexcept;
    void MarkAllBricksDirty() noexcept;
    void RebuildBrickMap() noexcept;

    SdfWorldDesc MDesc{};
    SdfField MField{};
    std::vector<float> MSamples;
    std::vector<SdfWorldBox> MBoxes;
    std::vector<SdfWorldCircle> MCircles;
    std::vector<SdfWorldImprint> MImprints;
    SdfBrickMap MBricks;
    std::vector<std::uint32_t> MDirtyBricks;
    std::vector<std::uint8_t> MDirtyFlags;
    std::uint64_t MFieldVersion = 0;
};

}
