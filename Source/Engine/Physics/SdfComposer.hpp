// FemboyEngine2D - SDF composer: layers and boolean operations over the palette.
#pragma once

#include "Engine/Core/Types.hpp"
#include "Engine/Physics/SdfCollision.hpp"
#include "Engine/Physics/SdfPrimitives.hpp"

#include <algorithm>
#include <cmath>
#include <span>
#include <vector>

namespace Fby
{

enum class SdfComposeOp : std::uint8_t
{
    Union = 0,
    Subtract,
    Intersect,
};

// One ordered layer. The list is the source of truth: editing a layer and
// rebaking reproduces the field exactly.
struct SdfComposeLayer
{
    SdfComposeOp Op = SdfComposeOp::Union;
    Vector2D Center{};
    SdfShape Shape{};
};

static_assert(std::is_trivially_copyable_v<SdfComposeLayer>);

// Sequential CSG over the primitive palette: the first layer starts the field and
// every later layer combines with the running distance. Order matters, and that
// is the point: subtract-then-union and union-then-subtract are different tools.
class SdfComposer
{
public:
    inline static constexpr std::uint32_t kMaxLayers = 256;

    void Clear() noexcept
    {
        MLayers.clear();
    }
    [[nodiscard]] bool IsEmpty() const noexcept
    {
        return MLayers.empty();
    }
    [[nodiscard]] std::span<const SdfComposeLayer> Layers() const noexcept
    {
        return MLayers;
    }
    [[nodiscard]] std::uint32_t LayerCount() const noexcept
    {
        return static_cast<std::uint32_t>(MLayers.size());
    }

    [[nodiscard]] bool Add(SdfComposeOp InOp, const SdfShape& InShape, Vector2D InCenter = {}) noexcept
    {
        if (MLayers.size() >= kMaxLayers || !InShape.IsValid())
        {
            return false;
        }
        SdfComposeLayer ScopedLayer;
        ScopedLayer.Op = InOp;
        ScopedLayer.Shape = InShape;
        ScopedLayer.Center = InCenter;
        MLayers.push_back(ScopedLayer);
        return true;
    }

    // Union: min(a, b). Subtract: max(a, -b). Intersect: max(a, b).
    [[nodiscard]] float Evaluate(Vector2D InPoint) const noexcept
    {
        if (MLayers.empty())
        {
            return 1.0f;
        }
        float ScopedDistance = LayerDistance(MLayers.front(), InPoint);
        for (std::size_t ScopedIndex = 1; ScopedIndex < MLayers.size(); ++ScopedIndex)
        {
            const float ScopedLayer = LayerDistance(MLayers[ScopedIndex], InPoint);
            switch (MLayers[ScopedIndex].Op)
            {
            case SdfComposeOp::Union:
                ScopedDistance = std::min(ScopedDistance, ScopedLayer);
                break;
            case SdfComposeOp::Subtract:
                ScopedDistance = std::max(ScopedDistance, -ScopedLayer);
                break;
            case SdfComposeOp::Intersect:
                ScopedDistance = std::max(ScopedDistance, ScopedLayer);
                break;
            }
        }
        return ScopedDistance;
    }

    // Bakes the composition into a fresh dense field. The samples are written at
    // grid points, matching SdfField::Sample.
    [[nodiscard]] bool Bake(Vector2D InOrigin, Vector2D InCellSize, std::uint32_t InWidth, std::uint32_t InHeight,
                            std::vector<float>* OutSamples, SdfField* OutField) const
    {
        if (OutSamples == nullptr || OutField == nullptr || InWidth < 2 || InHeight < 2 || InCellSize.X <= 0.0f ||
            InCellSize.Y <= 0.0f)
        {
            return false;
        }
        OutSamples->assign(static_cast<std::size_t>(InWidth) * InHeight, 0.0f);
        for (std::uint32_t ScopedY = 0; ScopedY < InHeight; ++ScopedY)
        {
            for (std::uint32_t ScopedX = 0; ScopedX < InWidth; ++ScopedX)
            {
                const Vector2D ScopedWorld{InOrigin.X + static_cast<float>(ScopedX) * InCellSize.X,
                                           InOrigin.Y + static_cast<float>(ScopedY) * InCellSize.Y};
                (*OutSamples)[static_cast<std::size_t>(ScopedY) * InWidth + ScopedX] = Evaluate(ScopedWorld);
            }
        }
        OutField->samples = OutSamples->data();
        OutField->width = InWidth;
        OutField->height = InHeight;
        OutField->origin = InOrigin;
        OutField->cellSize = InCellSize;
        return true;
    }

    // Distance from the point to the first layer is also useful on its own.
    [[nodiscard]] float LayerDistance(const SdfComposeLayer& InLayer, Vector2D InPoint) const noexcept
    {
        return SdfDistance(InLayer.Shape, InPoint - InLayer.Center);
    }

private:
    std::vector<SdfComposeLayer> MLayers;
};

}
