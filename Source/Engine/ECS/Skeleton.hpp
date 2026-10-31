// FemboyEngine2D - "SkeletalAnimation" skeleton 2.0.
#pragma once

#include "Engine/Core/Mat4.hpp"
#include "Engine/Core/Types.hpp"
#include "Engine/ECS/Schedule.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

#if defined(__cpp_lib_mdspan) && __cpp_lib_mdspan >= 202207L
#include <mdspan>
#define FBY_HAS_MDSPAN 1
#else
#define FBY_HAS_MDSPAN 0
#endif

namespace Fby
{

inline constexpr std::uint16_t kMaxBones = 128;
inline constexpr std::uint32_t kMaxSkinVertices = 64;
// 2D: two weights are enough
inline constexpr std::uint32_t kMaxBoneInfluences = 2;

// Mat3 - column-major 2D affine transform.
struct Mat3
{
    std::array<float, 9> m{1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};

    [[nodiscard]] friend constexpr bool operator==(const Mat3&, const Mat3&) = default;
};

[[nodiscard]] constexpr Mat3 Mat3Identity() noexcept
{
    return Mat3{};
}

[[nodiscard]] constexpr Mat3 Mat3Translation(Vector2D InTranslation) noexcept
{
    Mat3 ScopedResult;
    ScopedResult.m[6] = InTranslation.X;
    ScopedResult.m[7] = InTranslation.Y;
    return ScopedResult;
}

[[nodiscard]] inline Mat3 Mat3Rotation(float InRadians) noexcept
{
    const float ScopedCosine = std::cos(InRadians);
    const float ScopedSine = std::sin(InRadians);
    Mat3 ScopedResult;
    ScopedResult.m[0] = ScopedCosine;
    ScopedResult.m[1] = ScopedSine;
    ScopedResult.m[3] = -ScopedSine;
    ScopedResult.m[4] = ScopedCosine;
    return ScopedResult;
}

[[nodiscard]] constexpr Mat3 Mat3Scale(float InScale) noexcept
{
    Mat3 ScopedResult;
    ScopedResult.m[0] = InScale;
    ScopedResult.m[4] = InScale;
    return ScopedResult;
}

[[nodiscard]] constexpr Mat3 Mat3Multiply(const Mat3& InLeft, const Mat3& InRight) noexcept
{
    Mat3 ScopedResult;
    for (std::size_t ScopedColumn = 0; ScopedColumn < 3; ++ScopedColumn)
    {
        for (std::size_t ScopedRow = 0; ScopedRow < 3; ++ScopedRow)
        {
            float ScopedValue = 0.0f;
            for (std::size_t ScopedK = 0; ScopedK < 3; ++ScopedK)
            {
                ScopedValue += InLeft.m[ScopedK * 3 + ScopedRow] * InRight.m[ScopedColumn * 3 + ScopedK];
            }
            ScopedResult.m[ScopedColumn * 3 + ScopedRow] = ScopedValue;
        }
    }
    return ScopedResult;
}

[[nodiscard]] constexpr Vector2D Mat3TransformPoint(const Mat3& InMatrix, Vector2D InPoint) noexcept
{
    return Vector2D{
        InMatrix.m[0] * InPoint.X + InMatrix.m[3] * InPoint.Y + InMatrix.m[6],
        InMatrix.m[1] * InPoint.X + InMatrix.m[4] * InPoint.Y + InMatrix.m[7],
    };
}

// Inverse of an affine 2D matrix. The bottom row is implicitly (0, 0, 1).
[[nodiscard]] inline Mat3 Mat3InverseAffine(const Mat3& InMatrix) noexcept
{
    const float ScopedA = InMatrix.m[0];
    const float ScopedB = InMatrix.m[3];
    const float ScopedC = InMatrix.m[1];
    const float ScopedD = InMatrix.m[4];
    const float ScopedDeterminant = ScopedA * ScopedD - ScopedB * ScopedC;
    if (std::abs(ScopedDeterminant) < 1.0e-8f)
    {
        return Mat3Identity();
    }
    const float ScopedInverseDeterminant = 1.0f / ScopedDeterminant;
    Mat3 ScopedResult;
    ScopedResult.m[0] = ScopedD * ScopedInverseDeterminant;
    ScopedResult.m[1] = -ScopedC * ScopedInverseDeterminant;
    ScopedResult.m[3] = -ScopedB * ScopedInverseDeterminant;
    ScopedResult.m[4] = ScopedA * ScopedInverseDeterminant;
    ScopedResult.m[6] = -(ScopedResult.m[0] * InMatrix.m[6] + ScopedResult.m[3] * InMatrix.m[7]);
    ScopedResult.m[7] = -(ScopedResult.m[1] * InMatrix.m[6] + ScopedResult.m[4] * InMatrix.m[7]);
    return ScopedResult;
}

// Components
struct SkeletonComponent
{
    std::array<Vector2D, kMaxBones> LocalPositions{};
    std::array<float, kMaxBones> LocalRotations{};
    std::array<float, kMaxBones> LocalScales{};
    std::array<std::int16_t, kMaxBones> ParentIndices{};
    std::array<std::uint64_t, kMaxBones> BoneNameHashes{};
    std::array<Mat3, kMaxBones> InverseBindMatrices{};
    std::array<Mat3, kMaxBones> GlobalMatrices{};
    // Final LBS palette, ready for one memcpy into the GPU bone SSBO:
    std::array<Mat4, kMaxBones> SkinningMatrices{};
    std::uint16_t BoneCount = 0;
    bool BindPoseValid = false;
};

struct SkinComponent
{
    std::array<Vector2D, kMaxSkinVertices> BindPositions{};
    std::array<std::uint16_t, kMaxSkinVertices * kMaxBoneInfluences> BoneIndices{};
    std::array<float, kMaxSkinVertices * kMaxBoneInfluences> BoneWeights{};
    // Output of SkeletalAnimation::Deform(): world-space vertices consumed by the extract.
    std::array<Vector2D, kMaxSkinVertices> SkinnedPositions{};
    std::uint32_t VertexCount = 0;
};

// Render-facing data for a skinned quad. Kept separate from SkinComponent so
struct SkinnedSprite
{
    Vector2D UVMin{0.0f, 0.0f};
    Vector2D UVMax{1.0f, 1.0f};
    // Texture-array slice
    std::uint32_t Layer = 0;
    // DrawOrder bucket, independent of the texture
    std::uint16_t SortLayer = 0;
    Color32 Color = Color32::White();
    std::uint32_t Flags = 0;
    float Depth = 0.0f;
};

static_assert(std::is_standard_layout_v<SkeletonComponent>);
static_assert(std::is_trivially_copyable_v<SkeletonComponent>);
static_assert(std::is_standard_layout_v<SkinComponent>);
static_assert(std::is_trivially_copyable_v<SkinComponent>);

// Authoring helpers (free functions, no skeleton object)
[[nodiscard]] constexpr std::uint64_t BoneNameHash(std::string_view InName) noexcept
{
    std::uint64_t ScopedHash = 1469598103934665603ull;
    for (const char ScopedCharacter : InName)
    {
        ScopedHash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(ScopedCharacter));
        ScopedHash *= 1099511628211ull;
    }
    return ScopedHash;
}

void SkeletonReset(SkeletonComponent& InSkeleton) noexcept;

// Adds a bone in parent-before-child order. Returns -1 when the skeleton is full.
[[nodiscard]] std::int16_t SkeletonAddBone(SkeletonComponent& InSkeleton, std::string_view InName,
                                           std::int16_t InParent, Vector2D InLocalPosition, float InLocalRotation,
                                           float InLocalScale = 1.0f) noexcept;
[[nodiscard]] std::int16_t SkeletonFindBone(const SkeletonComponent& InSkeleton, std::uint64_t InNameHash) noexcept;

// Stores the current local pose as the bind pose: computes global matrices and
void SkeletonBindPose(SkeletonComponent& InSkeleton) noexcept;

// SkeletalAnimation evaluation
namespace SkeletalAnimation
{
// One linear pass over the bone array. The output is a contiguous
void Evaluate(SkeletonComponent& InSkeleton) noexcept;

// Linear blend skinning with up to two influences per vertex.
void Deform(const SkeletonComponent& InSkeleton, SkinComponent& InSkin) noexcept;
}

class SkeletonSystem
{
public:
    // GPU skinning is the default: Run() evaluates the hierarchy and fills the
    bool deformOnCpu = false;

    // Runs Evaluate (and optionally Deform) for every entity with a skeleton.
    void Run(entt::registry& InRegistry) const;
    void Run(Ecs::SystemContext& InContext) const;
};
}
