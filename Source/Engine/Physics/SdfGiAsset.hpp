// FemboyEngine2D - baked GI sidecar next to the .fsdf world image.
#pragma once

#include "Engine/Core/Types.hpp"
#include "Engine/Physics/SdfGiBake.hpp"
#include "Engine/Physics/SdfWorld.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

namespace Fby
{

inline constexpr std::uint32_t kSdfGiAssetVersion = 1;

// FNV-1a over a distance field's samples. The GI bake is only valid for the exact
// field it was traced against, so the sidecar stores this hash and refuses a
// mismatch.
[[nodiscard]] inline std::uint64_t HashSdfField(const SdfField& InField) noexcept
{
    if (!InField.Valid())
    {
        return 0;
    }
    const std::size_t ScopedBytes = static_cast<std::size_t>(InField.width) * InField.height * sizeof(float);
    const auto* ScopedData = reinterpret_cast<const std::uint8_t*>(InField.samples);
    std::uint64_t ScopedHash = 1469598103934665603ull;
    for (std::size_t ScopedIndex = 0; ScopedIndex < ScopedBytes; ++ScopedIndex)
    {
        ScopedHash ^= ScopedData[ScopedIndex];
        ScopedHash *= 1099511628211ull;
    }
    return ScopedHash;
}

[[nodiscard]] inline std::uint64_t HashSdfWorld(const SdfWorld& InWorld) noexcept
{
    return InWorld.IsConfigured() ? HashSdfField(InWorld.Field()) : 0;
}

struct SdfGiAssetHeader
{
    char Magic[8] = {'F', 'B', 'Y', 'F', 'S', 'D', 'G', 'I'};
    std::uint32_t Version = kSdfGiAssetVersion;
    std::uint32_t Reserved = 0;
    std::uint64_t WorldHash = 0;
    float OriginX = 0.0f;
    float OriginY = 0.0f;
    float CellX = 1.0f;
    float CellY = 1.0f;
    std::uint32_t Width = 0;
    std::uint32_t Height = 0;
    std::uint32_t ProbeCount = 0;
    std::uint32_t ProbeBytes = 0;
};

static_assert(sizeof(SdfGiAssetHeader) == 56, "SdfGiAssetHeader must stay 56 bytes");

// Reader and writer for the baked GI sidecar. The payload is the raw probe array,
// so a load is a bounds-checked copy and a restore, never a re-trace.
class SdfGiAsset
{
public:
    [[nodiscard]] static bool Save(const SdfGiBaked& InBake, std::uint64_t InWorldHash,
                                   std::vector<std::byte>* OutBytes)
    {
        if (OutBytes == nullptr || InBake.Width() == 0 || InBake.Height() == 0)
        {
            return false;
        }
        SdfGiAssetHeader ScopedHeader;
        ScopedHeader.WorldHash = InWorldHash;
        ScopedHeader.OriginX = InBake.Origin().X;
        ScopedHeader.OriginY = InBake.Origin().Y;
        ScopedHeader.CellX = InBake.CellSize().X;
        ScopedHeader.CellY = InBake.CellSize().Y;
        ScopedHeader.Width = InBake.Width();
        ScopedHeader.Height = InBake.Height();
        ScopedHeader.ProbeCount = static_cast<std::uint32_t>(InBake.ProbeCount());
        ScopedHeader.ProbeBytes = ScopedHeader.ProbeCount * static_cast<std::uint32_t>(sizeof(SdfGiBakedProbe));
        OutBytes->assign(sizeof(SdfGiAssetHeader) + ScopedHeader.ProbeBytes, std::byte{0});
        std::memcpy(OutBytes->data(), &ScopedHeader, sizeof(ScopedHeader));
        for (std::uint32_t ScopedY = 0; ScopedY < InBake.Height(); ++ScopedY)
        {
            for (std::uint32_t ScopedX = 0; ScopedX < InBake.Width(); ++ScopedX)
            {
                const SdfGiBakedProbe* ScopedProbe = InBake.ProbeAt(ScopedX, ScopedY);
                const std::size_t ScopedOffset =
                    sizeof(SdfGiAssetHeader) +
                    (static_cast<std::size_t>(ScopedY) * InBake.Width() + ScopedX) * sizeof(SdfGiBakedProbe);
                std::memcpy(OutBytes->data() + ScopedOffset, ScopedProbe, sizeof(SdfGiBakedProbe));
            }
        }
        return true;
    }

    [[nodiscard]] bool Load(std::span<const std::byte> InBytes, std::uint64_t InExpectedWorldHash,
                            std::string* InError = nullptr) noexcept
    {
        const auto ScopedFail = [InError](const char* InMessage)
        {
            if (InError != nullptr)
            {
                *InError = InMessage;
            }
            return false;
        };
        if (InBytes.size() < sizeof(SdfGiAssetHeader))
        {
            return ScopedFail("sdf gi asset: truncated header");
        }
        SdfGiAssetHeader ScopedHeader{};
        std::memcpy(&ScopedHeader, InBytes.data(), sizeof(ScopedHeader));
        static constexpr char kMagic[8] = {'F', 'B', 'Y', 'F', 'S', 'D', 'G', 'I'};
        if (std::memcmp(ScopedHeader.Magic, kMagic, sizeof(kMagic)) != 0)
        {
            return ScopedFail("sdf gi asset: wrong magic");
        }
        if (ScopedHeader.Version != kSdfGiAssetVersion)
        {
            return ScopedFail("sdf gi asset: unsupported version");
        }
        if (ScopedHeader.WorldHash != InExpectedWorldHash)
        {
            return ScopedFail("sdf gi asset: the world changed since the bake");
        }
        if (ScopedHeader.Width == 0 || ScopedHeader.Height == 0)
        {
            return ScopedFail("sdf gi asset: empty probe grid");
        }
        if (ScopedHeader.ProbeCount != ScopedHeader.Width * ScopedHeader.Height ||
            ScopedHeader.ProbeBytes != ScopedHeader.ProbeCount * sizeof(SdfGiBakedProbe))
        {
            return ScopedFail("sdf gi asset: probe count mismatch");
        }
        if (InBytes.size() != sizeof(SdfGiAssetHeader) + ScopedHeader.ProbeBytes)
        {
            return ScopedFail("sdf gi asset: payload size mismatch");
        }
        std::vector<SdfGiBakedProbe> ScopedProbes(ScopedHeader.ProbeCount);
        std::memcpy(ScopedProbes.data(), InBytes.data() + sizeof(SdfGiAssetHeader), ScopedHeader.ProbeBytes);
        if (!MBaked.Restore(Vector2D{ScopedHeader.OriginX, ScopedHeader.OriginY},
                            Vector2D{ScopedHeader.CellX, ScopedHeader.CellY}, ScopedHeader.Width, ScopedHeader.Height,
                            ScopedProbes))
        {
            return ScopedFail("sdf gi asset: probe grid restore failed");
        }
        MWorldHash = ScopedHeader.WorldHash;
        return true;
    }

    [[nodiscard]] const SdfGiBaked& Bake() const noexcept
    {
        return MBaked;
    }
    [[nodiscard]] SdfGiBaked& BakeMutable() noexcept
    {
        return MBaked;
    }
    void SetWorldHash(std::uint64_t InHash) noexcept
    {
        MWorldHash = InHash;
    }
    [[nodiscard]] std::uint64_t WorldHash() const noexcept
    {
        return MWorldHash;
    }

private:
    SdfGiBaked MBaked;
    std::uint64_t MWorldHash = 0;
};

}
