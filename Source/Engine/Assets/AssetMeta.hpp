// FemboyEngine2D - transactional asset metadata (.femeta).
#pragma once

#include "Engine/Assets/TextureFormat.hpp"
#include "Engine/Core/AssetId.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

namespace Fby::Assets
{

using AssetGuid = std::uint64_t;
inline constexpr AssetGuid kInvalidAssetGuid = 0;
inline constexpr std::uint32_t kMaxAssetDependencies = 16;
inline constexpr std::uint32_t kMaxAssetMeta = 256;

// A handle is a slot + the slot's allocation generation. 8 bytes, trivially
struct AssetHandle
{
    static constexpr std::uint32_t kInvalidIndex = 0xFFFFFFFFu;

    std::uint32_t index = kInvalidIndex;
    std::uint32_t generation = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return index != kInvalidIndex;
    }
    [[nodiscard]] constexpr explicit operator bool() const noexcept
    {
        return IsValid();
    }
    [[nodiscard]] friend constexpr bool operator==(AssetHandle, AssetHandle) = default;
};

static_assert(sizeof(AssetHandle) == 8);
static_assert(std::is_trivially_copyable_v<AssetHandle>);
static_assert(std::is_standard_layout_v<AssetHandle>);

// Import settings the cooker consumes. Kept POD so a .femeta can be hashed and
struct AssetImportSettings
{
    std::uint32_t textureFormat = static_cast<std::uint32_t>(TextureFormat::Bc7Srgb);
    std::uint32_t maxSize = 2048;
    // Future: swizzle, wrap mode, compression quality
    std::uint32_t flags = 0;
    // TTF/OTF import: style slot, raster size, coverage oversampling and SDF.
    std::uint32_t fontStyle = 0;
    std::uint32_t fontPixels = 32;
    std::uint32_t fontOversample = 1;
    bool srgb = true;
    bool generateMips = true;
    bool fontSdf = false;
    bool reserved[2] = {};
};

struct AssetMeta
{
    AssetGuid guid = kInvalidAssetGuid;
    // Deterministic identity used by the runtime
    AssetId id{};
    AssetKind kind = AssetKind::Unknown;
    char sourcePath[kAssetPathCapacity]{};
    char cookedPath[kAssetPathCapacity]{};
    // Hash of the authored bytes (watch debounce)
    std::uint64_t sourceHash = 0;
    // Hash of the cooked payload
    std::uint64_t cookedHash = 0;
    AssetImportSettings import{};
    // Bumped on every committed reload
    std::uint32_t contentGeneration = 0;
    std::uint32_t flags = 0;
    // Index into AssetDatabase dependencies
    std::uint16_t dependencyFirst = 0;
    std::uint16_t dependencyCount = 0;
    bool valid = false;
};

struct AssetDependency
{
    AssetGuid guid = kInvalidAssetGuid;
    // AssetKind
    std::uint16_t kind = 0;
    std::uint16_t flags = 0;
};

// Deterministic GUID for assets that predate .femeta (FNV-1a of kind + path).
[[nodiscard]] AssetGuid ComputeAssetGuid(std::string_view InSourcePath, AssetKind InKind) noexcept;

[[nodiscard]] AssetHandle MakeAssetHandle(std::uint32_t InIndex, std::uint32_t InGeneration) noexcept;

// Cooker-side parameter mapping.
[[nodiscard]] std::string_view AssetKindName(AssetKind InKind) noexcept;
[[nodiscard]] bool ParseAssetKind(std::string_view InText, AssetKind* Out) noexcept;
[[nodiscard]] std::string_view CompressionFormatName(TextureFormat InFormat) noexcept;
[[nodiscard]] bool ParseCompressionFormat(std::string_view InText, TextureFormat* Out) noexcept;

// TOML <-> AssetMeta. `out` must point at `metaCapacity` AssetDependency slots;
[[nodiscard]] bool ParseAssetMetaFile(std::string_view InText, AssetMeta* InMeta, AssetDependency* InDependencies,
                                      std::uint32_t InDependencyCapacity, std::uint32_t* OutDependencyCount,
                                      std::string* InError = nullptr);
[[nodiscard]] bool WriteAssetMetaFile(const AssetMeta& InMeta, std::span<const AssetDependency> InDependencies,
                                      std::string* Out, std::string* InError = nullptr);

// Convenience for the hot-reload path: does a .femeta extension belong to us?
[[nodiscard]] bool IsAssetMetaPath(std::string_view InPath) noexcept;
}
