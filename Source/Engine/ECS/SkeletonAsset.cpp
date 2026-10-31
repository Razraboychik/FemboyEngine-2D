#include "Engine/ECS/SkeletonAsset.hpp"

#include "Engine/Assets/VFS.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <utility>

namespace Fby::Skeleton
{
namespace
{
static_assert(std::endian::native == std::endian::little,
              ".feskl is a little-endian format; a big-endian target needs an explicit byte-swap layer");

constexpr std::uint64_t kFnvOffset = 1469598103934665603ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

[[nodiscard]] std::uint64_t HashBytes(std::span<const std::byte> InBytes, std::uint64_t InHash = kFnvOffset) noexcept
{
    for (const std::byte ScopedByte : InBytes)
    {
        InHash ^= static_cast<std::uint64_t>(std::to_integer<unsigned char>(ScopedByte));
        InHash *= kFnvPrime;
    }
    return InHash;
}

[[nodiscard]] bool Fail(std::string* InError, std::string InMessage)
{
    if (InError != nullptr)
    {
        *InError = std::move(InMessage);
    }
    return false;
}

[[nodiscard]] bool Aligned(std::uint32_t InOffset, std::size_t InAlignment) noexcept
{
    return InAlignment == 0 || (static_cast<std::uint64_t>(InOffset) % InAlignment) == 0;
}

[[nodiscard]] bool SectionInside(std::span<const std::byte> InBytes, std::uint32_t InOffset, std::size_t InSize,
                                 std::size_t InAlignment, std::string_view InName, std::string* InError)
{
    if (!Aligned(InOffset, InAlignment))
    {
        return Fail(InError, std::string(InName) + ": section offset " + std::to_string(InOffset) +
                                 " is not aligned to " + std::to_string(InAlignment));
    }
    const std::uint64_t ScopedTotal = InBytes.size();
    if (static_cast<std::uint64_t>(InOffset) > ScopedTotal ||
        static_cast<std::uint64_t>(InSize) > ScopedTotal - InOffset)
    {
        return Fail(InError, std::string(InName) + ": section [" + std::to_string(InOffset) + ", " +
                                 std::to_string(static_cast<std::uint64_t>(InOffset) + InSize) + ") is outside the " +
                                 std::to_string(ScopedTotal) + "-byte file");
    }
    return true;
}

[[nodiscard]] bool Finite(Vector2D InValue) noexcept
{
    return std::isfinite(InValue.X) && std::isfinite(InValue.Y);
}

[[nodiscard]] bool ValidName(std::string_view InName) noexcept
{
    return !InName.empty() && InName.size() <= kFesklMaxBoneNameLength && InName.find('\0') == std::string_view::npos;
}

// Deterministic section placement: each section is aligned from the end of the
struct AssetLayout
{
    std::uint32_t nameRefs = 0;
    std::uint32_t parents = 0;
    std::uint32_t positions = 0;
    std::uint32_t rotations = 0;
    std::uint32_t scales = 0;
    std::uint32_t hashes = 0;
    std::uint32_t skinPositions = 0;
    std::uint32_t skinIndices = 0;
    std::uint32_t skinWeights = 0;
    std::uint32_t sprite = 0;
    std::uint32_t nameBlob = 0;
    std::uint32_t total = 0;
};
}

// Name table
void SkeletonNameTableReset(SkeletonNameTable& InTable) noexcept
{
    InTable = SkeletonNameTable{};
}

bool SkeletonNameTableSet(SkeletonNameTable& InTable, std::uint32_t InBone, std::string_view InName) noexcept
{
    if (InBone >= kMaxBones || !ValidName(InName))
    {
        return false;
    }
    SkeletonBoneName& ScopedEntry = InTable.names[InBone];
    std::memcpy(ScopedEntry.text.data(), InName.data(), InName.size());
    ScopedEntry.text[InName.size()] = '\0';
    ScopedEntry.length = static_cast<std::uint8_t>(InName.size());
    InTable.count = static_cast<std::uint16_t>(std::max<std::uint32_t>(InTable.count, InBone + 1u));
    return true;
}

std::string_view SkeletonNameTableGet(const SkeletonNameTable& InTable, std::uint32_t InBone) noexcept
{
    if (InBone >= InTable.count || InBone >= kMaxBones)
    {
        return {};
    }
    const SkeletonBoneName& ScopedEntry = InTable.names[InBone];
    return std::string_view(ScopedEntry.text.data(), ScopedEntry.length);
}

void SkeletonNameTablePopulateFromIndices(SkeletonNameTable& InTable, std::uint32_t InBoneCount) noexcept
{
    const std::uint32_t ScopedCount = std::min(InBoneCount, static_cast<std::uint32_t>(kMaxBones));
    for (std::uint32_t ScopedBone = 0; ScopedBone < ScopedCount; ++ScopedBone)
    {
        if (!SkeletonNameTableGet(InTable, ScopedBone).empty())
        {
            continue;
        }
        char ScopedBuffer[kFesklMaxBoneNameLength + 1]{};
        std::snprintf(ScopedBuffer, sizeof(ScopedBuffer), "bone_%u", ScopedBone);
        (void)SkeletonNameTableSet(InTable, ScopedBone, ScopedBuffer);
    }
    InTable.count = static_cast<std::uint16_t>(std::max<std::uint32_t>(InTable.count, ScopedCount));
}

// Decoder
std::expected<SkeletonAssetView, std::string> DecodeSkeletonAsset(std::span<const std::byte> InBytes)
{
    std::string ScopedError;
    const auto ScopedReject = [](std::string InMessage) -> std::expected<SkeletonAssetView, std::string>
    { return std::unexpected(std::move(InMessage)); };

    if (InBytes.size() < sizeof(FesklHeader))
    {
        return ScopedReject("skeleton asset is smaller than its 128-byte header");
    }
    if (InBytes.size() > kFesklMaxBytes)
    {
        return ScopedReject("skeleton asset is larger than the 64 KiB format limit");
    }

    FesklHeader ScopedHeader{};
    std::memcpy(&ScopedHeader, InBytes.data(), sizeof(ScopedHeader));
    if (std::memcmp(ScopedHeader.magic, kFesklMagic.data(), kFesklMagic.size()) != 0)
    {
        return ScopedReject("bad .feskl magic (expected FESKL001)");
    }
    if (ScopedHeader.version != kFesklVersion)
    {
        return ScopedReject("unsupported .feskl version " + std::to_string(ScopedHeader.version) + " (engine reads " +
                            std::to_string(kFesklVersion) + ")");
    }
    if (ScopedHeader.headerSize != kFesklHeaderSize)
    {
        return ScopedReject("unsupported .feskl header size " + std::to_string(ScopedHeader.headerSize));
    }
    if (ScopedHeader.fileSize != InBytes.size())
    {
        return ScopedReject(".feskl declares " + std::to_string(ScopedHeader.fileSize) + " bytes, file has " +
                            std::to_string(InBytes.size()));
    }
    if (ScopedHeader.boneCount == 0 || ScopedHeader.boneCount > kMaxBones)
    {
        return ScopedReject(".feskl bone count " + std::to_string(ScopedHeader.boneCount) + " is outside 1.." +
                            std::to_string(kMaxBones));
    }
    if (ScopedHeader.vertexCount > kMaxSkinVertices)
    {
        return ScopedReject(".feskl vertex count " + std::to_string(ScopedHeader.vertexCount) + " exceeds " +
                            std::to_string(kMaxSkinVertices));
    }
    const bool ScopedHasSkinFlag = (ScopedHeader.flags & kFesklFlagHasSkin) != 0;
    if (ScopedHasSkinFlag != (ScopedHeader.vertexCount > 0))
    {
        return ScopedReject("skin flag and vertex count disagree");
    }
    if ((ScopedHeader.flags & ~(kFesklFlagHasSkin | kFesklFlagHasSprite)) != 0)
    {
        return ScopedReject(".feskl header has unknown flags");
    }
    if (ScopedHeader.nameBytes == 0 || ScopedHeader.nameBytes > kFesklMaxBytes)
    {
        return ScopedReject("invalid .feskl name blob size");
    }

    // The content hash covers every byte after the header. It is checked before
    const std::uint64_t ScopedExpectedHash =
        HashBytes(InBytes.subspan(ScopedHeader.headerSize, InBytes.size() - ScopedHeader.headerSize));
    if (ScopedExpectedHash != ScopedHeader.contentHash)
    {
        return ScopedReject("skeleton content hash mismatch (file " + std::to_string(ScopedHeader.contentHash) +
                            ", computed " + std::to_string(ScopedExpectedHash) + ")");
    }

    const std::size_t ScopedBoneCount = ScopedHeader.boneCount;
    const std::size_t ScopedVertexCount = ScopedHeader.vertexCount;
    const std::size_t ScopedInfluenceCount = ScopedVertexCount * kMaxBoneInfluences;

    if (!SectionInside(InBytes, ScopedHeader.nameRefsOffset, ScopedBoneCount * sizeof(FesklNameRef),
                       alignof(FesklNameRef), "name refs", &ScopedError) ||
        !SectionInside(InBytes, ScopedHeader.parentsOffset, ScopedBoneCount * sizeof(std::int16_t),
                       alignof(std::int16_t), "parents", &ScopedError) ||
        !SectionInside(InBytes, ScopedHeader.positionsOffset, ScopedBoneCount * sizeof(Vector2D), alignof(Vector2D),
                       "positions", &ScopedError) ||
        !SectionInside(InBytes, ScopedHeader.rotationsOffset, ScopedBoneCount * sizeof(float), alignof(float),
                       "rotations", &ScopedError) ||
        !SectionInside(InBytes, ScopedHeader.scalesOffset, ScopedBoneCount * sizeof(float), alignof(float), "scales",
                       &ScopedError) ||
        !SectionInside(InBytes, ScopedHeader.hashesOffset, ScopedBoneCount * sizeof(std::uint64_t),
                       alignof(std::uint64_t), "name hashes", &ScopedError) ||
        !SectionInside(InBytes, ScopedHeader.skinPositionsOffset, ScopedVertexCount * sizeof(Vector2D),
                       alignof(Vector2D), "skin positions", &ScopedError) ||
        !SectionInside(InBytes, ScopedHeader.skinIndicesOffset, ScopedInfluenceCount * sizeof(std::uint16_t),
                       alignof(std::uint16_t), "skin indices", &ScopedError) ||
        !SectionInside(InBytes, ScopedHeader.skinWeightsOffset, ScopedInfluenceCount * sizeof(float), alignof(float),
                       "skin weights", &ScopedError) ||
        !SectionInside(InBytes, ScopedHeader.nameBlobOffset, ScopedHeader.nameBytes, 1, "name blob", &ScopedError))
    {
        return ScopedReject(std::move(ScopedError));
    }

    SkeletonAssetView ScopedView;
    ScopedView.header = reinterpret_cast<const FesklHeader*>(InBytes.data());
    ScopedView.nameRefs = {reinterpret_cast<const FesklNameRef*>(InBytes.data() + ScopedHeader.nameRefsOffset),
                           ScopedBoneCount};
    ScopedView.parents = {reinterpret_cast<const std::int16_t*>(InBytes.data() + ScopedHeader.parentsOffset),
                          ScopedBoneCount};
    ScopedView.positions = {reinterpret_cast<const Vector2D*>(InBytes.data() + ScopedHeader.positionsOffset),
                            ScopedBoneCount};
    ScopedView.rotations = {reinterpret_cast<const float*>(InBytes.data() + ScopedHeader.rotationsOffset),
                            ScopedBoneCount};
    ScopedView.scales = {reinterpret_cast<const float*>(InBytes.data() + ScopedHeader.scalesOffset), ScopedBoneCount};
    ScopedView.nameHashes = {reinterpret_cast<const std::uint64_t*>(InBytes.data() + ScopedHeader.hashesOffset),
                             ScopedBoneCount};
    ScopedView.skinPositions = {reinterpret_cast<const Vector2D*>(InBytes.data() + ScopedHeader.skinPositionsOffset),
                                ScopedVertexCount};
    ScopedView.skinIndices = {reinterpret_cast<const std::uint16_t*>(InBytes.data() + ScopedHeader.skinIndicesOffset),
                              ScopedInfluenceCount};
    ScopedView.skinWeights = {reinterpret_cast<const float*>(InBytes.data() + ScopedHeader.skinWeightsOffset),
                              ScopedInfluenceCount};
    ScopedView.nameBlob = {reinterpret_cast<const char*>(InBytes.data() + ScopedHeader.nameBlobOffset),
                           ScopedHeader.nameBytes};

    if ((ScopedHeader.flags & kFesklFlagHasSprite) != 0)
    {
        if (!SectionInside(InBytes, ScopedHeader.spriteOffset, sizeof(FesklSprite), alignof(FesklSprite),
                           "sprite binding", &ScopedError))
        {
            return ScopedReject(std::move(ScopedError));
        }
        ScopedView.sprite = reinterpret_cast<const FesklSprite*>(InBytes.data() + ScopedHeader.spriteOffset);
        if (!Finite(ScopedView.sprite->uvMin) || !Finite(ScopedView.sprite->uvMax) ||
            !std::isfinite(ScopedView.sprite->depth))
        {
            return ScopedReject("sprite binding contains a non-finite value");
        }
    }

    for (std::size_t ScopedBone = 0; ScopedBone < ScopedBoneCount; ++ScopedBone)
    {
        const FesklNameRef& ScopedRef = ScopedView.nameRefs[ScopedBone];
        if (ScopedRef.length == 0 || ScopedRef.length > kFesklMaxBoneNameLength)
        {
            return ScopedReject("bone " + std::to_string(ScopedBone) + " has an empty or oversized name");
        }
        if (ScopedRef.offset > ScopedView.nameBlob.size() ||
            ScopedRef.length > ScopedView.nameBlob.size() - ScopedRef.offset)
        {
            return ScopedReject("bone " + std::to_string(ScopedBone) + " name is outside the name blob");
        }
        const std::string_view ScopedName(ScopedView.nameBlob.data() + ScopedRef.offset, ScopedRef.length);
        if (ScopedName.find('\0') != std::string_view::npos)
        {
            return ScopedReject("bone " + std::to_string(ScopedBone) + " name contains a NUL byte");
        }
        if (BoneNameHash(ScopedName) != ScopedView.nameHashes[ScopedBone])
        {
            return ScopedReject("bone " + std::to_string(ScopedBone) + " name hash does not match its name");
        }

        const std::int16_t ScopedParent = ScopedView.parents[ScopedBone];
        if (ScopedParent < -1 || ScopedParent >= static_cast<std::int16_t>(ScopedBone))
        {
            // Parents must precede children. Rejecting this here means skeletal animation's
            return ScopedReject("bone " + std::to_string(ScopedBone) + " has a non-topological parent index " +
                                std::to_string(ScopedParent));
        }
        if (!Finite(ScopedView.positions[ScopedBone]) || !std::isfinite(ScopedView.rotations[ScopedBone]) ||
            !std::isfinite(ScopedView.scales[ScopedBone]) || !(ScopedView.scales[ScopedBone] > 0.0f))
        {
            return ScopedReject("bone " + std::to_string(ScopedBone) + " has an invalid rest transform");
        }
    }

    for (std::size_t ScopedVertex = 0; ScopedVertex < ScopedVertexCount; ++ScopedVertex)
    {
        if (!Finite(ScopedView.skinPositions[ScopedVertex]))
        {
            return ScopedReject("skin vertex " + std::to_string(ScopedVertex) + " has a non-finite position");
        }
        float ScopedTotalWeight = 0.0f;
        for (std::uint32_t ScopedInfluence = 0; ScopedInfluence < kMaxBoneInfluences; ++ScopedInfluence)
        {
            const std::size_t ScopedSlot = ScopedVertex * kMaxBoneInfluences + ScopedInfluence;
            const float ScopedWeight = ScopedView.skinWeights[ScopedSlot];
            if (!std::isfinite(ScopedWeight) || ScopedWeight < 0.0f)
            {
                return ScopedReject("skin vertex " + std::to_string(ScopedVertex) + " has an invalid weight");
            }
            if (ScopedWeight > 0.0f && ScopedView.skinIndices[ScopedSlot] >= ScopedBoneCount)
            {
                return ScopedReject("skin vertex " + std::to_string(ScopedVertex) + " references a missing bone");
            }
            ScopedTotalWeight += ScopedWeight;
        }
        if (!(ScopedTotalWeight > 0.0f))
        {
            return ScopedReject("skin vertex " + std::to_string(ScopedVertex) + " has no influence");
        }
    }

    return ScopedView;
}

// Encoder
bool EncodeSkeletonAsset(const SkeletonAssetSource& InSource, std::vector<std::byte>& Out, std::uint64_t* OutHash,
                         std::string* InError)
{
    Out.clear();
    if (InSource.skeleton == nullptr)
    {
        return Fail(InError, "skeleton source has no SkeletonComponent");
    }
    const SkeletonComponent& ScopedSkeleton = *InSource.skeleton;
    if (ScopedSkeleton.BoneCount == 0 || ScopedSkeleton.BoneCount > kMaxBones)
    {
        return Fail(InError, "skeleton bone count is outside 1..128");
    }
    if (InSource.names == nullptr)
    {
        return Fail(InError, "skeleton source has no name table");
    }
    const SkeletonNameTable& ScopedNames = *InSource.names;
    if (ScopedNames.count < ScopedSkeleton.BoneCount)
    {
        return Fail(InError, "name table has " + std::to_string(ScopedNames.count) + " entries for " +
                                 std::to_string(ScopedSkeleton.BoneCount) + " bones");
    }

    const std::uint32_t ScopedBoneCount = ScopedSkeleton.BoneCount;
    if (InSource.includeSkin && InSource.skin == nullptr)
    {
        return Fail(InError, "skin serialization was requested but SkeletonAssetSource::skin is null");
    }
    if (InSource.includeSprite && !InSource.includeSkin)
    {
        return Fail(InError, "a sprite binding requires a skin section");
    }
    if (InSource.includeSkin && InSource.skin->VertexCount > kMaxSkinVertices)
    {
        return Fail(InError, "skin vertex count exceeds the 64-vertex format limit");
    }
    const SkinComponent* ScopedSkin = InSource.includeSkin ? InSource.skin : nullptr;
    const std::uint32_t ScopedVertexCount = ScopedSkin != nullptr ? ScopedSkin->VertexCount : 0u;

    std::array<FesklNameRef, kMaxBones> ScopedNameRefs{};
    std::uint32_t ScopedNameBytes = 0;
    for (std::uint32_t ScopedBone = 0; ScopedBone < ScopedBoneCount; ++ScopedBone)
    {
        const std::string_view ScopedName = SkeletonNameTableGet(ScopedNames, ScopedBone);
        if (!ValidName(ScopedName))
        {
            return Fail(InError, "bone " + std::to_string(ScopedBone) + " has an invalid name");
        }
        if (ScopedNameBytes > std::numeric_limits<std::uint32_t>::max() - ScopedName.size())
        {
            return Fail(InError, "name blob overflows its 32-bit size");
        }
        ScopedNameRefs[ScopedBone].offset = ScopedNameBytes;
        ScopedNameRefs[ScopedBone].length = static_cast<std::uint16_t>(ScopedName.size());
        ScopedNameBytes += static_cast<std::uint32_t>(ScopedName.size());
    }

    for (std::uint32_t ScopedBone = 0; ScopedBone < ScopedBoneCount; ++ScopedBone)
    {
        const std::int16_t ScopedParent = ScopedSkeleton.ParentIndices[ScopedBone];
        if (ScopedParent < -1 || ScopedParent >= static_cast<std::int16_t>(ScopedBone))
        {
            return Fail(InError, "bone " + std::to_string(ScopedBone) + " is not in parent-before-child order");
        }
        if (!Finite(ScopedSkeleton.LocalPositions[ScopedBone]) ||
            !std::isfinite(ScopedSkeleton.LocalRotations[ScopedBone]) ||
            !std::isfinite(ScopedSkeleton.LocalScales[ScopedBone]) || !(ScopedSkeleton.LocalScales[ScopedBone] > 0.0f))
        {
            return Fail(InError, "bone " + std::to_string(ScopedBone) + " has an invalid rest transform");
        }
    }

    for (std::uint32_t ScopedVertex = 0; ScopedVertex < ScopedVertexCount; ++ScopedVertex)
    {
        float ScopedTotalWeight = 0.0f;
        for (std::uint32_t ScopedInfluence = 0; ScopedInfluence < kMaxBoneInfluences; ++ScopedInfluence)
        {
            const std::uint32_t ScopedSlot = ScopedVertex * kMaxBoneInfluences + ScopedInfluence;
            const float ScopedWeight = ScopedSkin->BoneWeights[ScopedSlot];
            if (!std::isfinite(ScopedWeight) || ScopedWeight < 0.0f)
            {
                return Fail(InError, "skin vertex " + std::to_string(ScopedVertex) + " has an invalid weight");
            }
            if (ScopedWeight > 0.0f && ScopedSkin->BoneIndices[ScopedSlot] >= ScopedBoneCount)
            {
                return Fail(InError, "skin vertex " + std::to_string(ScopedVertex) + " references a missing bone");
            }
            ScopedTotalWeight += ScopedWeight;
        }
        if (!(ScopedTotalWeight > 0.0f))
        {
            return Fail(InError, "skin vertex " + std::to_string(ScopedVertex) + " has no influence");
        }
    }

    AssetLayout ScopedLayout;
    std::uint32_t ScopedCursor = sizeof(FesklHeader);
    const auto ScopedPlace = [&ScopedCursor](std::uint32_t InSize, std::uint32_t InAlignment) -> std::uint32_t
    {
        ScopedCursor = (ScopedCursor + InAlignment - 1u) & ~(InAlignment - 1u);
        const std::uint32_t ScopedAt = ScopedCursor;
        ScopedCursor += InSize;
        return ScopedAt;
    };

    ScopedLayout.nameRefs = ScopedPlace(static_cast<std::uint32_t>(ScopedBoneCount * sizeof(FesklNameRef)), 8);
    ScopedLayout.parents = ScopedPlace(static_cast<std::uint32_t>(ScopedBoneCount * sizeof(std::int16_t)), 4);
    ScopedLayout.positions = ScopedPlace(static_cast<std::uint32_t>(ScopedBoneCount * sizeof(Vector2D)), 8);
    ScopedLayout.rotations = ScopedPlace(static_cast<std::uint32_t>(ScopedBoneCount * sizeof(float)), 4);
    ScopedLayout.scales = ScopedPlace(static_cast<std::uint32_t>(ScopedBoneCount * sizeof(float)), 4);
    ScopedLayout.hashes = ScopedPlace(static_cast<std::uint32_t>(ScopedBoneCount * sizeof(std::uint64_t)), 8);
    ScopedLayout.skinPositions = ScopedPlace(static_cast<std::uint32_t>(ScopedVertexCount * sizeof(Vector2D)), 8);
    ScopedLayout.skinIndices =
        ScopedPlace(static_cast<std::uint32_t>(ScopedVertexCount * kMaxBoneInfluences * sizeof(std::uint16_t)), 4);
    ScopedLayout.skinWeights =
        ScopedPlace(static_cast<std::uint32_t>(ScopedVertexCount * kMaxBoneInfluences * sizeof(float)), 8);
    ScopedLayout.sprite = InSource.includeSprite ? ScopedPlace(sizeof(FesklSprite), 8) : 0;
    ScopedLayout.nameBlob = ScopedPlace(ScopedNameBytes, 1);
    ScopedLayout.total = ScopedCursor;

    if (ScopedLayout.total > kFesklMaxBytes)
    {
        return Fail(InError, "encoded skeleton exceeds the 64 KiB format limit");
    }

    FesklHeader ScopedHeader{};
    std::memcpy(ScopedHeader.magic, kFesklMagic.data(), kFesklMagic.size());
    ScopedHeader.version = kFesklVersion;
    ScopedHeader.headerSize = kFesklHeaderSize;
    ScopedHeader.flags =
        (ScopedVertexCount > 0 ? kFesklFlagHasSkin : 0u) | (InSource.includeSprite ? kFesklFlagHasSprite : 0u);
    ScopedHeader.boneCount = ScopedBoneCount;
    ScopedHeader.vertexCount = ScopedVertexCount;
    ScopedHeader.nameBytes = ScopedNameBytes;
    ScopedHeader.fileSize = ScopedLayout.total;
    ScopedHeader.sourceHash = InSource.sourceHash;
    ScopedHeader.nameRefsOffset = ScopedLayout.nameRefs;
    ScopedHeader.parentsOffset = ScopedLayout.parents;
    ScopedHeader.positionsOffset = ScopedLayout.positions;
    ScopedHeader.rotationsOffset = ScopedLayout.rotations;
    ScopedHeader.scalesOffset = ScopedLayout.scales;
    ScopedHeader.hashesOffset = ScopedLayout.hashes;
    ScopedHeader.skinPositionsOffset = ScopedLayout.skinPositions;
    ScopedHeader.skinIndicesOffset = ScopedLayout.skinIndices;
    ScopedHeader.skinWeightsOffset = ScopedLayout.skinWeights;
    ScopedHeader.spriteOffset = ScopedLayout.sprite;
    ScopedHeader.nameBlobOffset = ScopedLayout.nameBlob;

    Out.assign(ScopedLayout.total, std::byte{0});
    const auto ScopedWriteAt = [&Out](std::uint32_t InOffset, const void* InData, std::size_t InSize)
    { std::memcpy(Out.data() + InOffset, InData, InSize); };

    ScopedWriteAt(ScopedLayout.nameRefs, ScopedNameRefs.data(), ScopedBoneCount * sizeof(FesklNameRef));
    ScopedWriteAt(ScopedLayout.parents, ScopedSkeleton.ParentIndices.data(), ScopedBoneCount * sizeof(std::int16_t));
    ScopedWriteAt(ScopedLayout.positions, ScopedSkeleton.LocalPositions.data(), ScopedBoneCount * sizeof(Vector2D));
    ScopedWriteAt(ScopedLayout.rotations, ScopedSkeleton.LocalRotations.data(), ScopedBoneCount * sizeof(float));
    ScopedWriteAt(ScopedLayout.scales, ScopedSkeleton.LocalScales.data(), ScopedBoneCount * sizeof(float));

    std::array<std::uint64_t, kMaxBones> ScopedHashes{};
    for (std::uint32_t ScopedBone = 0; ScopedBone < ScopedBoneCount; ++ScopedBone)
    {
        ScopedHashes[ScopedBone] = BoneNameHash(SkeletonNameTableGet(ScopedNames, ScopedBone));
    }
    ScopedWriteAt(ScopedLayout.hashes, ScopedHashes.data(), ScopedBoneCount * sizeof(std::uint64_t));

    if (ScopedVertexCount > 0)
    {
        ScopedWriteAt(ScopedLayout.skinPositions, ScopedSkin->BindPositions.data(),
                      ScopedVertexCount * sizeof(Vector2D));
        ScopedWriteAt(ScopedLayout.skinIndices, ScopedSkin->BoneIndices.data(),
                      ScopedVertexCount * kMaxBoneInfluences * sizeof(std::uint16_t));
        ScopedWriteAt(ScopedLayout.skinWeights, ScopedSkin->BoneWeights.data(),
                      ScopedVertexCount * kMaxBoneInfluences * sizeof(float));
    }
    if (InSource.includeSprite)
    {
        ScopedWriteAt(ScopedLayout.sprite, &InSource.sprite, sizeof(FesklSprite));
    }

    std::uint32_t ScopedNameCursor = 0;
    for (std::uint32_t ScopedBone = 0; ScopedBone < ScopedBoneCount; ++ScopedBone)
    {
        const std::string_view ScopedName = SkeletonNameTableGet(ScopedNames, ScopedBone);
        std::memcpy(Out.data() + ScopedLayout.nameBlob + ScopedNameCursor, ScopedName.data(), ScopedName.size());
        ScopedNameCursor += static_cast<std::uint32_t>(ScopedName.size());
    }

    ScopedHeader.contentHash = HashBytes(
        std::span<const std::byte>(Out.data() + ScopedHeader.headerSize, Out.size() - ScopedHeader.headerSize));
    ScopedWriteAt(0, &ScopedHeader, sizeof(ScopedHeader));

    if (OutHash != nullptr)
    {
        *OutHash = ScopedHeader.contentHash;
    }
    return true;
}

// Runtime bridge
void InstantiateSkeletonAsset(const SkeletonAssetView& InView, SkeletonComponent& InSkeleton, SkinComponent& InSkin,
                              SkeletonNameTable& InNames, FesklSprite& InSprite) noexcept
{
    SkeletonReset(InSkeleton);
    const std::uint32_t ScopedBoneCount = std::min(InView.BoneCount(), static_cast<std::uint32_t>(kMaxBones));
    for (std::uint32_t ScopedBone = 0; ScopedBone < ScopedBoneCount; ++ScopedBone)
    {
        InSkeleton.LocalPositions[ScopedBone] = InView.positions[ScopedBone];
        InSkeleton.LocalRotations[ScopedBone] = InView.rotations[ScopedBone];
        InSkeleton.LocalScales[ScopedBone] = InView.scales[ScopedBone];
        InSkeleton.ParentIndices[ScopedBone] = InView.parents[ScopedBone];
        InSkeleton.BoneNameHashes[ScopedBone] = InView.nameHashes[ScopedBone];
        InSkeleton.InverseBindMatrices[ScopedBone] = Mat3Identity();
        InSkeleton.GlobalMatrices[ScopedBone] = Mat3Identity();
    }
    InSkeleton.BoneCount = static_cast<std::uint16_t>(ScopedBoneCount);
    InSkeleton.BindPoseValid = false;
    SkeletonBindPose(InSkeleton);

    InSkin = SkinComponent{};
    if (InView.HasSkin())
    {
        const std::uint32_t ScopedVertexCount = std::min(InView.VertexCount(), kMaxSkinVertices);
        InSkin.VertexCount = ScopedVertexCount;
        for (std::uint32_t ScopedVertex = 0; ScopedVertex < ScopedVertexCount; ++ScopedVertex)
        {
            InSkin.BindPositions[ScopedVertex] = InView.skinPositions[ScopedVertex];
            for (std::uint32_t ScopedInfluence = 0; ScopedInfluence < kMaxBoneInfluences; ++ScopedInfluence)
            {
                const std::uint32_t ScopedSlot = ScopedVertex * kMaxBoneInfluences + ScopedInfluence;
                InSkin.BoneIndices[ScopedSlot] = InView.skinIndices[ScopedSlot];
                InSkin.BoneWeights[ScopedSlot] = InView.skinWeights[ScopedSlot];
            }
            InSkin.SkinnedPositions[ScopedVertex] = InSkin.BindPositions[ScopedVertex];
        }
    }

    SkeletonNameTableReset(InNames);
    for (std::uint32_t ScopedBone = 0; ScopedBone < ScopedBoneCount; ++ScopedBone)
    {
        (void)SkeletonNameTableSet(InNames, ScopedBone, InView.BoneName(ScopedBone));
    }

    InSprite = InView.sprite != nullptr ? *InView.sprite : FesklSprite{};
}

bool LoadSkeletonAsset(VirtualFileSystem& InVfs, std::string_view InPath, SkeletonComponent& InSkeleton,
                       SkinComponent& InSkin, SkeletonNameTable& InNames, FesklSprite& InSprite, std::uint64_t* OutHash,
                       std::string* InError)
{
    const auto ScopedBytes = InVfs.Read(InPath);
    if (!ScopedBytes)
    {
        return Fail(InError, "skeleton asset not found in the VFS: " + std::string(InPath));
    }
    const auto ScopedDecoded = DecodeSkeletonAsset(*ScopedBytes);
    if (!ScopedDecoded)
    {
        return Fail(InError, "skeleton asset '" + std::string(InPath) + "': " + ScopedDecoded.error());
    }
    InstantiateSkeletonAsset(*ScopedDecoded, InSkeleton, InSkin, InNames, InSprite);
    if (OutHash != nullptr)
    {
        *OutHash = ScopedDecoded->header->contentHash;
    }
    return true;
}

bool SaveSkeletonAsset(VirtualFileSystem& InVfs, std::string_view InPath, const SkeletonAssetSource& InSource,
                       std::uint64_t* OutHash, std::string* InError)
{
    std::vector<std::byte> ScopedBytes;
    ScopedBytes.reserve(16u * 1024u);
    std::uint64_t ScopedHash = 0;
    if (!EncodeSkeletonAsset(InSource, ScopedBytes, &ScopedHash, InError))
    {
        return false;
    }
    if (!InVfs.WriteFile(InPath, ScopedBytes, InError))
    {
        return false;
    }
    if (OutHash != nullptr)
    {
        *OutHash = ScopedHash;
    }
    return true;
}
}
