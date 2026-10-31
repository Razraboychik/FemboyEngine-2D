// FemboyEngine2D - mesh render packets and the GPU skinning contract.
#pragma once

#include "Engine/Core/Mat4.hpp"
#include "Engine/Core/Types.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace Fby::Rhi
{

using Fby::Mat4;

struct MeshVertex
{
    // Bind-pose position, flat 2D
    Vector2D position{};
    Vector2D uv{0.0f, 0.0f};
    std::uint32_t color = 0xFFFFFFFFu;
    // Dynamic skinning: the RHI overwrites this with the index of the owned
    std::uint32_t padding = 0;
    std::uint32_t boneIndices[2] = {0, 0};
    float boneWeights[2] = {0.0f, 0.0f};
};

// Output of the compute pass and the input of the mesh vertex shader. The
struct SkinnedVertex
{
    // Full Mat4 LBS result
    Vector4D position{};
    Vector2D uv{0.0f, 0.0f};
    std::uint32_t color = 0xFFFFFFFFu;
    std::uint32_t padding = 0;
};

static_assert(sizeof(MeshVertex) == 40, "MeshVertex must match std430 in mesh_skinning.comp");
static_assert(offsetof(MeshVertex, boneIndices) == 24);
static_assert(offsetof(MeshVertex, boneWeights) == 32);
static_assert(sizeof(SkinnedVertex) == 32, "SkinnedVertex must match std430 in mesh_skinning.comp/mesh.vert");
static_assert(alignof(SkinnedVertex) == 4);

// One palette range for a dynamic mesh. All vertices of an item share it; the
struct MeshSkinRange
{
    std::uint32_t boneOffset = 0;
    std::uint32_t boneCount = 0;
};

static_assert(sizeof(MeshSkinRange) == 8);

enum MeshDrawFlags : std::uint32_t
{
    // Vertices come from the static arena, not the skinned SSBO
    MeshDrawStatic = 1u << 0,
    // Read the bind-pose ring directly; no skinning dispatch
    MeshDrawUnskinned = 1u << 1,
};

enum MeshItemFlags : std::uint16_t
{
    MeshItemNone = 0,
    // Cloth and other CPU-deformed geometry: vertices are already in world/local
    MeshItemUnskinned = 1u << 0,
};

// Per-draw constants, uploaded once per frame. gl_InstanceIndex (Vulkan) /
struct MeshDrawData
{
    // Full 4x4, not a 2D affine shortcut
    Mat4 model = Mat4::Identity();
    // Index into the selected SkinnedVertex buffer
    std::uint32_t vertexBase = 0;
    // Texture-array slice
    std::uint32_t layer = 0;
    // MeshDrawFlags
    std::uint32_t flags = 0;
    std::uint32_t padding = 0;
};

static_assert(sizeof(MeshDrawData) == 80);
static_assert(offsetof(MeshDrawData, vertexBase) == 64);

// Static geometry cache
struct MeshHandle
{
    static constexpr std::uint32_t kInvalid = 0xFFFFFFFFu;
    std::uint32_t id = kInvalid;

    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return id != kInvalid;
    }
    [[nodiscard]] friend constexpr bool operator==(MeshHandle InA, MeshHandle InB) noexcept
    {
        return InA.id == InB.id;
    }
};

struct MeshDesc
{
    std::span<const MeshVertex> vertices;
    std::span<const std::uint16_t> indices;
    // Debug label only; the RHI never dereferences it after the call.
    const char* debugName = nullptr;
};

// A material pipeline id of kDefaultMaterialPipeline means "use the engine's
inline constexpr std::uint32_t kDefaultMaterialPipeline = 0xFFFFFFFFu;

// One mesh draw. Either `mesh` is valid (static, cached in device-local memory)
struct MeshRenderItem
{
    std::span<const MeshVertex> vertices;
    std::span<const std::uint16_t> indices;
    std::uint32_t boneOffset = 0;
    std::uint32_t boneCount = 0;
    // Full 32-bit texture-array handle id (Rhi::TextureArrayHandle::id).
    std::uint32_t textureArray = 0xFFFFFFFFu;
    // Texture-array slice inside `textureArray`
    std::uint32_t layer = 0;
    Mat4 model = Mat4::Identity();
    Color32 color = Color32::White();
    // 16-bit on purpose: this is a sprite-sort bucket, not a resource id. It is
    std::uint16_t sortLayer = 0;
    // MeshItemFlags; MeshItemUnskinned bypasses skinning
    std::uint16_t flags = 0;
    float depth = 0.0f;
    // MaterialGraph pipeline (Rhi::ShaderPipelineHandle::id). kDefaultMaterialPipeline
    std::uint32_t materialPipeline = kDefaultMaterialPipeline;
    // Valid => static cached geometry
    MeshHandle mesh{};
};

static_assert(std::is_trivially_copyable_v<MeshRenderItem>);
static_assert(std::is_standard_layout_v<MeshRenderItem>);
// The 32-bit resource ids are the contract that DrawBatch now shares. A
static_assert(sizeof(MeshRenderItem::textureArray) == sizeof(std::uint32_t));
static_assert(sizeof(MeshRenderItem::layer) == sizeof(std::uint32_t));
static_assert(sizeof(MeshRenderItem::materialPipeline) == sizeof(std::uint32_t));
static_assert(sizeof(MeshHandle::id) == sizeof(std::uint32_t));

// Single source of truth for the bind-pose path. Cloth and any other
[[nodiscard]] inline bool IsUnskinnedMeshItem(const MeshRenderItem& InItem) noexcept
{
    return (InItem.flags & MeshItemUnskinned) != 0 || InItem.boneCount == 0;
}

static_assert(std::is_trivially_copyable_v<MeshDrawData>);
static_assert(std::is_standard_layout_v<MeshDrawData>);
}
