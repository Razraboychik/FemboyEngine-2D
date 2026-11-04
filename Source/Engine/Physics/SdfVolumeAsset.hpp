// FemboyEngine2D - serialized signed distance volume (.fsdf).
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

// Container version. A layout change bumps it; the reader rejects versions it
// cannot parse instead of guessing.
inline constexpr std::uint32_t SdfVolumeVersion = 1;
inline constexpr std::uint32_t SdfVolumeHeaderBytes = 80;
// "FBYFSDF1" as a little-endian 64-bit tag. The engine targets little-endian
// platforms, so the file bytes match this constant on every supported host.
inline constexpr std::uint64_t SdfVolumeMagic = 0x3146445346594246ull;

// Fixed on-disk header. The brick table and the narrow-band payload follow it.
struct SdfVolumeHeader
{
    std::uint64_t Magic = SdfVolumeMagic;
    std::uint32_t Version = SdfVolumeVersion;
    std::uint32_t HeaderBytes = SdfVolumeHeaderBytes;
    std::uint32_t SourceWidth = 0;
    std::uint32_t SourceHeight = 0;
    std::uint32_t BricksX = 0;
    std::uint32_t BricksY = 0;
    float OriginX = 0.0f;
    float OriginY = 0.0f;
    float CellSizeX = 1.0f;
    float CellSizeY = 1.0f;
    float OutsideDistance = 1.0e6f;
    float NarrowBand = 1.0f;
    std::uint64_t BrickBytes = 0;
    std::uint64_t PayloadBytes = 0;
    std::uint64_t ContentHash = 0;
};

static_assert(sizeof(SdfVolumeHeader) == SdfVolumeHeaderBytes, "SdfVolumeHeader layout must stay 80 bytes");
static_assert(offsetof(SdfVolumeHeader, BrickBytes) == 56, "SdfVolumeHeader::BrickBytes offset changed");
static_assert(offsetof(SdfVolumeHeader, ContentHash) == 72, "SdfVolumeHeader::ContentHash offset changed");

// One baked level field: the sparse brick map plus the hash that ties the bake
// to its inputs. Loading validates every offset before the map is restored.
class SdfVolumeAsset
{
public:
    // Bakes a dense field into the sparse representation.
    [[nodiscard]] bool Build(const SdfField& InField, float InNarrowBand = 0.0f) noexcept;

    // Parses a `.fsdf` blob. On failure the asset keeps its previous contents.
    [[nodiscard]] bool Load(std::span<const std::byte> InBytes, std::string* InError = nullptr) noexcept;
    // Serializes the current bake. The hash covers the description, the brick
    // table and the payload, so a byte flip invalidates the file.
    [[nodiscard]] bool Save(std::vector<std::byte>* OutBytes) const;

    [[nodiscard]] const SdfBrickMap& Bricks() const noexcept
    {
        return MBricks;
    }
    [[nodiscard]] const SdfBrickMapDesc& Desc() const noexcept
    {
        return MDesc;
    }
    [[nodiscard]] std::uint64_t ContentHash() const noexcept
    {
        return MContentHash;
    }
    [[nodiscard]] bool IsValid() const noexcept
    {
        return MDesc.SourceWidth > 1 && MDesc.SourceHeight > 1;
    }

    [[nodiscard]] float Sample(Vector2D InWorld) const noexcept
    {
        return MBricks.Sample(InWorld);
    }

    // Expands the sparse bake into a dense field for consumers that still take
    // an SdfField view. Narrow bricks copy their payload exactly; empty and full
    // bricks collapse to their recorded constant.
    [[nodiscard]] bool ExpandToField(std::vector<float>* OutSamples, SdfField* OutField) const;

private:
    SdfBrickMap MBricks;
    SdfBrickMapDesc MDesc{};
    std::uint64_t MContentHash = 0;
};

}
