// FemboyEngine2D - .feskl skeleton/skin asset.
#pragma once

#include "Engine/Core/Types.hpp"
#include "Engine/ECS/Skeleton.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Fby
{
class VirtualFileSystem;
}

namespace Fby::Skeleton
{

inline constexpr std::uint32_t kFesklVersion = 1;
inline constexpr std::uint32_t kFesklHeaderSize = 128;
inline constexpr std::uint32_t kFesklMaxBytes = 64u * 1024u;
inline constexpr std::uint32_t kFesklMaxBoneNameLength = 63;
inline constexpr std::uint32_t kFesklMaxAssetPath = 96;

inline constexpr std::uint32_t kFesklFlagHasSkin = 1u << 0;
inline constexpr std::uint32_t kFesklFlagHasSprite = 1u << 1;

// "FESKL001" - eight bytes so a hex dump is self-describing and the version is
inline constexpr std::array<char, 8> kFesklMagic{'F', 'E', 'S', 'K', 'L', '0', '0', '1'};

// On-disk PODs.
struct FesklHeader
{
    // "FESKL001"
    char magic[8];
    // KFesklVersion
    std::uint32_t version;
    // KFesklHeaderSize
    std::uint32_t headerSize;
    // KFesklFlag*
    std::uint32_t flags;
    // 1..kMaxBones
    std::uint32_t boneCount;
    // 0..kMaxSkinVertices
    std::uint32_t vertexCount;
    // Total UTF-8 bytes in the trailing name blob
    std::uint32_t nameBytes;
    // Exact size of the asset
    std::uint64_t fileSize;
    // FNV-1a of [headerSize, fileSize)
    std::uint64_t contentHash;
    // Optional authoring revision, 0 when unknown
    std::uint64_t sourceHash;

    // FesklNameRef[boneCount]
    std::uint32_t nameRefsOffset;
    // Int16_t[boneCount]
    std::uint32_t parentsOffset;
    // Vec2[boneCount]
    std::uint32_t positionsOffset;
    // Float[boneCount]
    std::uint32_t rotationsOffset;
    // Float[boneCount]
    std::uint32_t scalesOffset;
    // Uint64_t[boneCount]
    std::uint32_t hashesOffset;
    // Vec2[vertexCount]
    std::uint32_t skinPositionsOffset;
    // Uint16_t[vertexCount * 2]
    std::uint32_t skinIndicesOffset;
    // Float[vertexCount * 2]
    std::uint32_t skinWeightsOffset;
    // FesklSprite, when kFesklFlagHasSprite
    std::uint32_t spriteOffset;
    // Char[nameBytes]
    std::uint32_t nameBlobOffset;

    // Zero; keeps the header at 128 bytes
    std::uint32_t reserved[7];
};

struct FesklNameRef
{
    // Into the name blob
    std::uint32_t offset = 0;
    // Bytes, no terminator
    std::uint16_t length = 0;
    // Reserved, zero
    std::uint16_t flags = 0;
};

struct FesklSprite
{
    Vector2D uvMin{0.0f, 0.0f};
    Vector2D uvMax{1.0f, 1.0f};
    std::uint32_t layer = 0;
    std::uint32_t color = 0xFFFFFFFFu;
    // Rhi::RenderFlags
    std::uint32_t flags = 0;
    float depth = 0.0f;
    std::uint16_t sortLayer = 0;
    std::uint16_t reserved0 = 0;
    std::uint32_t reserved1 = 0;
};

static_assert(std::is_trivially_copyable_v<FesklHeader>);
static_assert(std::is_standard_layout_v<FesklHeader>);
static_assert(sizeof(FesklHeader) == kFesklHeaderSize, ".feskl header is fixed at 128 bytes");
static_assert(offsetof(FesklHeader, fileSize) == 32);
static_assert(offsetof(FesklHeader, nameRefsOffset) == 56);
static_assert(offsetof(FesklHeader, reserved) == 100);
static_assert(sizeof(FesklNameRef) == 8, "a name reference is two uint32s");
static_assert(sizeof(FesklSprite) == 40, "the sprite binding block is fixed at 40 bytes");

// Non-owning decoded view.
struct SkeletonAssetView
{
    const FesklHeader* header = nullptr;
    std::span<const FesklNameRef> nameRefs;
    std::span<const std::int16_t> parents;
    std::span<const Vector2D> positions;
    std::span<const float> rotations;
    std::span<const float> scales;
    std::span<const std::uint64_t> nameHashes;
    std::span<const Vector2D> skinPositions;
    std::span<const std::uint16_t> skinIndices;
    std::span<const float> skinWeights;
    const FesklSprite* sprite = nullptr;
    std::span<const char> nameBlob;

    [[nodiscard]] std::uint32_t BoneCount() const noexcept
    {
        return header != nullptr ? header->boneCount : 0;
    }
    [[nodiscard]] std::uint32_t VertexCount() const noexcept
    {
        return header != nullptr ? header->vertexCount : 0;
    }
    [[nodiscard]] bool HasSkin() const noexcept
    {
        return header != nullptr && (header->flags & kFesklFlagHasSkin) != 0 && header->vertexCount > 0;
    }
    [[nodiscard]] bool HasSprite() const noexcept
    {
        return sprite != nullptr;
    }
    [[nodiscard]] std::string_view BoneName(std::uint32_t InBone) const noexcept
    {
        if (InBone >= nameRefs.size())
        {
            return {};
        }
        const FesklNameRef& ScopedRef = nameRefs[InBone];
        if (ScopedRef.offset > nameBlob.size() || ScopedRef.length > nameBlob.size() - ScopedRef.offset)
        {
            return {};
        }
        return std::string_view(nameBlob.data() + ScopedRef.offset, ScopedRef.length);
    }
};

// Authoring names.
struct SkeletonBoneName
{
    // NUL terminated
    std::array<char, kFesklMaxBoneNameLength + 1> text{};
    std::uint8_t length = 0;
};

struct SkeletonNameTable
{
    std::array<SkeletonBoneName, kMaxBones> names{};
    std::uint16_t count = 0;
};

void SkeletonNameTableReset(SkeletonNameTable& InTable) noexcept;
[[nodiscard]] bool SkeletonNameTableSet(SkeletonNameTable& InTable, std::uint32_t InBone,
                                        std::string_view InName) noexcept;
[[nodiscard]] std::string_view SkeletonNameTableGet(const SkeletonNameTable& InTable, std::uint32_t InBone) noexcept;
// Fills missing names with "bone_<index>" (snprintf into the fixed buffer) and
void SkeletonNameTablePopulateFromIndices(SkeletonNameTable& InTable, std::uint32_t InBoneCount) noexcept;

// Writer input.
struct SkeletonAssetSource
{
    const SkeletonComponent* skeleton = nullptr;
    // Required when skin is serialized
    const SkinComponent* skin = nullptr;
    const SkeletonNameTable* names = nullptr;
    FesklSprite sprite{};
    bool includeSkin = false;
    bool includeSprite = false;
    std::uint64_t sourceHash = 0;
};

// Codec.
[[nodiscard]] std::expected<SkeletonAssetView, std::string> DecodeSkeletonAsset(std::span<const std::byte> InBytes);

// Serializes rest-pose locals, hierarchy, names and skin binding. `out` is
[[nodiscard]] bool EncodeSkeletonAsset(const SkeletonAssetSource& InSource, std::vector<std::byte>& Out,
                                       std::uint64_t* OutHash = nullptr, std::string* InError = nullptr);

// Fills a runtime SkeletonComponent/SkinComponent from a validated view and
void InstantiateSkeletonAsset(const SkeletonAssetView& InView, SkeletonComponent& InSkeleton, SkinComponent& InSkin,
                              SkeletonNameTable& InNames, FesklSprite& InSprite) noexcept;

// VFS convenience: the span returned by Read() is decoded in place, then the
[[nodiscard]] bool LoadSkeletonAsset(VirtualFileSystem& InVfs, std::string_view InPath, SkeletonComponent& InSkeleton,
                                     SkinComponent& InSkin, SkeletonNameTable& InNames, FesklSprite& InSprite,
                                     std::uint64_t* OutHash = nullptr, std::string* InError = nullptr);

// Encodes and writes through the VFS writable overlay (a directory mount; a
[[nodiscard]] bool SaveSkeletonAsset(VirtualFileSystem& InVfs, std::string_view InPath,
                                     const SkeletonAssetSource& InSource, std::uint64_t* OutHash = nullptr,
                                     std::string* InError = nullptr);

[[nodiscard]] constexpr FesklSprite DefaultFesklSprite() noexcept
{
    return FesklSprite{};
}
}
