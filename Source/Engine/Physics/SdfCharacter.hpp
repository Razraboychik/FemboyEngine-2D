// FemboyEngine2D - bone-bound SDF parts for deformable 2D characters.
#pragma once

#include "Engine/Core/Types.hpp"
#include "Engine/ECS/Skeleton.hpp"
#include "Engine/Physics/SdfBrickMap.hpp"
#include "Engine/Physics/SdfPrimitives.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <vector>

namespace Fby
{

inline constexpr std::uint32_t kMaxSdfCharacterParts = 64;
inline constexpr std::uint32_t kMaxSdfCharacterFieldSamples = 512u * 512u;

// One SDF primitive bound to a skeleton bone. The shape is authored in bone
// space; LocalOffset and LocalRotation place it relative to the bone origin.
struct SdfBonePart
{
    SdfShape Shape;
    Vector2D LocalOffset{};
    float LocalRotation = 0.0f;
    // Polynomial smooth-union radius. Zero is a hard union.
    float BlendRadius = 0.0f;
    std::uint16_t BoneIndex = 0;
    std::uint16_t Flags = 0;
};

static_assert(std::is_trivially_copyable_v<SdfBonePart>);
static_assert(std::is_standard_layout_v<SdfBonePart>);

// A character is a fixed-capacity set of bone-bound parts. It references the
// skeleton by entity only at the system boundary; this component stays plain
// data so it serializes, snapshots and copies without pointers.
struct SdfCharacterComponent
{
    std::array<SdfBonePart, kMaxSdfCharacterParts> Parts{};
    std::uint32_t PartCount = 0;
    // Reserved: atlas policy, render flags.
    std::uint32_t Flags = 0;
};

static_assert(std::is_trivially_copyable_v<SdfCharacterComponent>);
static_assert(std::is_standard_layout_v<SdfCharacterComponent>);

// Polynomial smooth minimum used by the SDF union. k is the blend width in
// world units; k <= 0 falls back to a hard minimum.
[[nodiscard]] float SdfSmoothMin(float InA, float InB, float InBlend) noexcept;

// Exact signed distance from a world-space point to the character surface.
// Every part is transformed through the bone's current global matrix, so the
// field deforms with the skeleton without CPU mesh resampling.
[[nodiscard]] float SdfCharacterDistance(const SkeletonComponent& InSkeleton,
                                         const SdfCharacterComponent& InCharacter, Vector2D InWorldPoint) noexcept;

// Unit outward gradient of SdfCharacterDistance from central differences.
[[nodiscard]] Vector2D SdfCharacterGradient(const SkeletonComponent& InSkeleton,
                                            const SdfCharacterComponent& InCharacter, Vector2D InWorldPoint) noexcept;

// Conservative world-space bounds of the posed parts. Used by the editor
// preview to pick a stable bake region without a distance LOD.
[[nodiscard]] bool SdfCharacterBounds(const SkeletonComponent& InSkeleton,
                                      const SdfCharacterComponent& InCharacter, Vector2D* OutMin,
                                      Vector2D* OutMax) noexcept;

// True when the point lies inside the character surface.
[[nodiscard]] bool SdfCharacterCoversPoint(const SkeletonComponent& InSkeleton,
                                           const SdfCharacterComponent& InCharacter, Vector2D InWorldPoint) noexcept;

// CPU-side field baked from the current pose. This is the fallback/authoring
// path; the renderer may upload the same samples as an SDF atlas texture.
struct SdfCharacterField
{
    std::vector<float> Samples;
    std::uint32_t Width = 0;
    std::uint32_t Height = 0;
    Vector2D Origin{};
    Vector2D CellSize{1.0f, 1.0f};
    std::uint32_t Version = 0;

    [[nodiscard]] bool Valid() const noexcept
    {
        return Width > 1 && Height > 1 && CellSize.X > 0.0f && CellSize.Y > 0.0f &&
               Samples.size() == static_cast<std::size_t>(Width) * Height;
    }
    [[nodiscard]] float At(std::uint32_t InX, std::uint32_t InY) const noexcept;
    [[nodiscard]] float Sample(Vector2D InWorld) const noexcept;
};

// Bakes a field covering InMin..InMax. The sample count is capped by
// kMaxSdfCharacterFieldSamples; the field is refused above the cap.
[[nodiscard]] bool BakeSdfCharacterField(const SkeletonComponent& InSkeleton,
                                         const SdfCharacterComponent& InCharacter, Vector2D InMin, Vector2D InMax,
                                         std::uint32_t InWidth, std::uint32_t InHeight,
                                         SdfCharacterField* OutField) noexcept;

// Packs a baked character field into the sparse brick representation. The
// result can be uploaded directly or passed to PackSdfAtlas for the GPU mirror.
[[nodiscard]] bool BuildSdfCharacterBrickMap(const SdfCharacterField& InField, float InNarrowBand,
                                             SdfBrickMap* OutMap) noexcept;

// Solid-cell mesh bridge for renderers that consume triangles instead of a
// raymarched field. Positions are world-space; indices are 16-bit.
struct SdfCharacterMeshData
{
    std::vector<Vector2D> Positions;
    std::vector<std::uint16_t> Indices;
};

[[nodiscard]] bool BuildSdfCharacterMesh(const SdfCharacterField& InField, float InIso,
                                         SdfCharacterMeshData* OutMesh) noexcept;

}

FBY_REFLECT_COMPONENT(Fby::SdfCharacterComponent, "SdfCharacterComponent",
                      FBY_FIELD(Fby::SdfCharacterComponent, PartCount, UInt32),
                      FBY_FIELD(Fby::SdfCharacterComponent, Flags, UInt32))
