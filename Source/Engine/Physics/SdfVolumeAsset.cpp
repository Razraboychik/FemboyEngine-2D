// FemboyEngine2D - serialized signed distance volume (.fsdf) implementation.
#include "Engine/Physics/SdfVolumeAsset.hpp"

#include <algorithm>
#include <cstring>

namespace Fby
{
namespace
{

constexpr std::uint64_t kHashSeed = 14695981039346656037ull;
constexpr std::uint64_t kHashPrime = 1099511628211ull;

void SetError(std::string* InError, const char* InMessage)
{
    if (InError != nullptr)
    {
        *InError = InMessage;
    }
}

void HashMix(std::uint64_t& InHash, const void* InData, std::size_t InBytes) noexcept
{
    const auto* ScopedBytes = static_cast<const std::uint8_t*>(InData);
    for (std::size_t ScopedIndex = 0; ScopedIndex < InBytes; ++ScopedIndex)
    {
        InHash ^= ScopedBytes[ScopedIndex];
        InHash *= kHashPrime;
    }
}

// Hashes the semantic payload only: the description, the brick table and the
// narrow band. The header itself is excluded so the hash can live inside it.
[[nodiscard]] std::uint64_t ComputeContentHash(const SdfBrickMapDesc& InDesc,
                                               std::span<const SdfBrickHeader> InBricks,
                                               std::span<const float> InPayload) noexcept
{
    std::uint64_t ScopedHash = kHashSeed;
    HashMix(ScopedHash, &SdfVolumeVersion, sizeof(SdfVolumeVersion));
    HashMix(ScopedHash, &InDesc.SourceWidth, sizeof(InDesc.SourceWidth));
    HashMix(ScopedHash, &InDesc.SourceHeight, sizeof(InDesc.SourceHeight));
    HashMix(ScopedHash, &InDesc.BricksX, sizeof(InDesc.BricksX));
    HashMix(ScopedHash, &InDesc.BricksY, sizeof(InDesc.BricksY));
    HashMix(ScopedHash, &InDesc.Origin.X, sizeof(float));
    HashMix(ScopedHash, &InDesc.Origin.Y, sizeof(float));
    HashMix(ScopedHash, &InDesc.CellSize.X, sizeof(float));
    HashMix(ScopedHash, &InDesc.CellSize.Y, sizeof(float));
    HashMix(ScopedHash, &InDesc.OutsideDistance, sizeof(float));
    HashMix(ScopedHash, &InDesc.NarrowBand, sizeof(float));
    HashMix(ScopedHash, InBricks.data(), InBricks.size_bytes());
    HashMix(ScopedHash, InPayload.data(), InPayload.size_bytes());
    return ScopedHash;
}

}

bool SdfVolumeAsset::Build(const SdfField& InField, float InNarrowBand) noexcept
{
    if (!MBricks.Build(InField, InNarrowBand))
    {
        return false;
    }
    MDesc = MBricks.Desc();
    MContentHash = ComputeContentHash(MDesc, MBricks.BrickView(), MBricks.PayloadView());
    return true;
}

bool SdfVolumeAsset::Save(std::vector<std::byte>* OutBytes) const
{
    if (OutBytes == nullptr || !IsValid())
    {
        return false;
    }
    const std::span<const SdfBrickHeader> ScopedBricks = MBricks.BrickView();
    const std::span<const float> ScopedPayload = MBricks.PayloadView();
    const std::uint64_t ScopedBrickBytes = static_cast<std::uint64_t>(ScopedBricks.size_bytes());
    const std::uint64_t ScopedPayloadBytes = static_cast<std::uint64_t>(ScopedPayload.size_bytes());

    SdfVolumeHeader ScopedHeader{};
    ScopedHeader.Magic = SdfVolumeMagic;
    ScopedHeader.Version = SdfVolumeVersion;
    ScopedHeader.HeaderBytes = SdfVolumeHeaderBytes;
    ScopedHeader.SourceWidth = MDesc.SourceWidth;
    ScopedHeader.SourceHeight = MDesc.SourceHeight;
    ScopedHeader.BricksX = MDesc.BricksX;
    ScopedHeader.BricksY = MDesc.BricksY;
    ScopedHeader.OriginX = MDesc.Origin.X;
    ScopedHeader.OriginY = MDesc.Origin.Y;
    ScopedHeader.CellSizeX = MDesc.CellSize.X;
    ScopedHeader.CellSizeY = MDesc.CellSize.Y;
    ScopedHeader.OutsideDistance = MDesc.OutsideDistance;
    ScopedHeader.NarrowBand = MDesc.NarrowBand;
    ScopedHeader.BrickBytes = ScopedBrickBytes;
    ScopedHeader.PayloadBytes = ScopedPayloadBytes;
    ScopedHeader.ContentHash = ComputeContentHash(MDesc, ScopedBricks, ScopedPayload);

    const std::size_t ScopedTotal = sizeof(SdfVolumeHeader) + static_cast<std::size_t>(ScopedBrickBytes) +
                                    static_cast<std::size_t>(ScopedPayloadBytes);
    OutBytes->assign(ScopedTotal, std::byte{0});
    std::memcpy(OutBytes->data(), &ScopedHeader, sizeof(ScopedHeader));
    if (ScopedBrickBytes > 0)
    {
        std::memcpy(OutBytes->data() + sizeof(SdfVolumeHeader), ScopedBricks.data(),
                    static_cast<std::size_t>(ScopedBrickBytes));
    }
    if (ScopedPayloadBytes > 0)
    {
        std::memcpy(OutBytes->data() + sizeof(SdfVolumeHeader) + static_cast<std::size_t>(ScopedBrickBytes),
                    ScopedPayload.data(), static_cast<std::size_t>(ScopedPayloadBytes));
    }
    return true;
}

bool SdfVolumeAsset::Load(std::span<const std::byte> InBytes, std::string* InError) noexcept
{
    if (InBytes.size() < sizeof(SdfVolumeHeader))
    {
        SetError(InError, "sdf volume: truncated header");
        return false;
    }
    SdfVolumeHeader ScopedHeader{};
    std::memcpy(&ScopedHeader, InBytes.data(), sizeof(ScopedHeader));
    if (ScopedHeader.Magic != SdfVolumeMagic)
    {
        SetError(InError, "sdf volume: wrong magic");
        return false;
    }
    if (ScopedHeader.Version != SdfVolumeVersion)
    {
        SetError(InError, "sdf volume: unsupported version");
        return false;
    }
    if (ScopedHeader.HeaderBytes != sizeof(SdfVolumeHeader))
    {
        SetError(InError, "sdf volume: header size mismatch");
        return false;
    }
    if (ScopedHeader.SourceWidth < 2 || ScopedHeader.SourceHeight < 2)
    {
        SetError(InError, "sdf volume: grid too small");
        return false;
    }
    const std::uint32_t ScopedBricksX = (ScopedHeader.SourceWidth + SdfBrickSize - 1u) / SdfBrickSize;
    const std::uint32_t ScopedBricksY = (ScopedHeader.SourceHeight + SdfBrickSize - 1u) / SdfBrickSize;
    if (ScopedHeader.BricksX != ScopedBricksX || ScopedHeader.BricksY != ScopedBricksY)
    {
        SetError(InError, "sdf volume: brick grid does not match the source grid");
        return false;
    }
    const std::uint64_t ScopedBrickBytes = static_cast<std::uint64_t>(ScopedBricksX) * ScopedBricksY *
                                           sizeof(SdfBrickHeader);
    if (ScopedHeader.BrickBytes != ScopedBrickBytes)
    {
        SetError(InError, "sdf volume: brick table size mismatch");
        return false;
    }
    if (ScopedHeader.PayloadBytes % sizeof(float) != 0)
    {
        SetError(InError, "sdf volume: payload is not float aligned");
        return false;
    }
    if (ScopedHeader.BrickBytes > InBytes.size() || ScopedHeader.PayloadBytes > InBytes.size())
    {
        SetError(InError, "sdf volume: declared sizes exceed the file");
        return false;
    }
    const std::size_t ScopedExpected = sizeof(SdfVolumeHeader) + static_cast<std::size_t>(ScopedHeader.BrickBytes) +
                                       static_cast<std::size_t>(ScopedHeader.PayloadBytes);
    if (InBytes.size() != ScopedExpected)
    {
        SetError(InError, "sdf volume: file size mismatch");
        return false;
    }

    const std::size_t ScopedBrickCount = static_cast<std::size_t>(ScopedBricksX) * ScopedBricksY;
    const std::size_t ScopedPayloadCount = static_cast<std::size_t>(ScopedHeader.PayloadBytes) / sizeof(float);
    std::vector<SdfBrickHeader> ScopedBricks(ScopedBrickCount);
    std::vector<float> ScopedPayload(ScopedPayloadCount);
    if (ScopedHeader.BrickBytes > 0)
    {
        std::memcpy(ScopedBricks.data(), InBytes.data() + sizeof(SdfVolumeHeader),
                    static_cast<std::size_t>(ScopedHeader.BrickBytes));
    }
    if (ScopedHeader.PayloadBytes > 0)
    {
        std::memcpy(ScopedPayload.data(),
                    InBytes.data() + sizeof(SdfVolumeHeader) + static_cast<std::size_t>(ScopedHeader.BrickBytes),
                    static_cast<std::size_t>(ScopedHeader.PayloadBytes));
    }

    SdfBrickMapDesc ScopedDesc{};
    ScopedDesc.Origin = Vector2D{ScopedHeader.OriginX, ScopedHeader.OriginY};
    ScopedDesc.CellSize = Vector2D{ScopedHeader.CellSizeX, ScopedHeader.CellSizeY};
    ScopedDesc.SourceWidth = ScopedHeader.SourceWidth;
    ScopedDesc.SourceHeight = ScopedHeader.SourceHeight;
    ScopedDesc.BricksX = ScopedHeader.BricksX;
    ScopedDesc.BricksY = ScopedHeader.BricksY;
    ScopedDesc.OutsideDistance = ScopedHeader.OutsideDistance;
    ScopedDesc.NarrowBand = ScopedHeader.NarrowBand;
    if (ComputeContentHash(ScopedDesc, ScopedBricks, ScopedPayload) != ScopedHeader.ContentHash)
    {
        SetError(InError, "sdf volume: content hash mismatch");
        return false;
    }
    if (!MBricks.Restore(ScopedDesc, ScopedBricks, ScopedPayload))
    {
        SetError(InError, "sdf volume: brick map rejected the table");
        return false;
    }
    MDesc = ScopedDesc;
    MContentHash = ScopedHeader.ContentHash;
    return true;
}

bool SdfVolumeAsset::ExpandToField(std::vector<float>* OutSamples, SdfField* OutField) const
{
    if (!IsValid() || OutSamples == nullptr || OutField == nullptr)
    {
        return false;
    }
    const std::uint32_t ScopedWidth = MDesc.SourceWidth;
    const std::uint32_t ScopedHeight = MDesc.SourceHeight;
    OutSamples->assign(static_cast<std::size_t>(ScopedWidth) * ScopedHeight, MDesc.OutsideDistance);

    for (const SdfBrickHeader& ScopedHeader : MBricks.BrickView())
    {
        const std::uint32_t ScopedBaseX = ScopedHeader.BrickX * SdfBrickSize;
        const std::uint32_t ScopedBaseY = ScopedHeader.BrickY * SdfBrickSize;
        for (std::uint32_t ScopedY = 0; ScopedY < SdfBrickSize; ++ScopedY)
        {
            const std::uint32_t ScopedTargetY = ScopedBaseY + ScopedY;
            if (ScopedTargetY >= ScopedHeight)
            {
                break;
            }
            for (std::uint32_t ScopedX = 0; ScopedX < SdfBrickSize; ++ScopedX)
            {
                const std::uint32_t ScopedTargetX = ScopedBaseX + ScopedX;
                if (ScopedTargetX >= ScopedWidth)
                {
                    break;
                }
                // Empty and full bricks are uniform; only narrow bricks carry a
                // per-cell payload.
                float ScopedValue = ScopedHeader.MinDistance;
                if (ScopedHeader.State == SdfBrickState::Full)
                {
                    ScopedValue = ScopedHeader.MaxDistance;
                }
                else if (ScopedHeader.State == SdfBrickState::Narrow)
                {
                    ScopedValue = MBricks.PayloadView()[ScopedHeader.PayloadOffset + ScopedY * SdfBrickSize + ScopedX];
                }
                (*OutSamples)[static_cast<std::size_t>(ScopedTargetY) * ScopedWidth + ScopedTargetX] = ScopedValue;
            }
        }
    }

    SdfField ScopedField;
    ScopedField.samples = OutSamples->data();
    ScopedField.width = ScopedWidth;
    ScopedField.height = ScopedHeight;
    ScopedField.origin = MDesc.Origin;
    ScopedField.cellSize = MDesc.CellSize;
    ScopedField.outsideDistance = MDesc.OutsideDistance;
    *OutField = ScopedField;
    return true;
}

}
