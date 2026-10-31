// FemboyEngine2D - procedural Pink UI primitives (no texture memory).
#pragma once

#include "Engine/Core/Types.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/Schedule.hpp"

#include <cstdint>
#include <span>
#include <type_traits>

namespace Fby::Ui
{

enum class PinkSdfShape : std::uint32_t
{
    Circle = 0,
    RoundedBox = 1,
    Capsule = 2,
    Card = 3,
    Ring = 4,
};

enum PinkSdfFlags : std::uint32_t
{
    PinkSdfNone = 0,
    PinkSdfShadow = 1u << 0,
    PinkSdfInnerShadow = 1u << 1,
    PinkSdfGradient = 1u << 2,
    PinkSdfSelected = 1u << 3,
};

struct PinkSdfPrimitive
{
    Vector2D center{};
    Vector2D size{32.0f, 32.0f};
    float radius = 8.0f;
    float border = 1.5f;
    float rotation = 0.0f;
    float shadow = 6.0f;
    PinkSdfShape shape = PinkSdfShape::RoundedBox;
    std::uint32_t flags = 0;
    Color32 fill = Color32::White();
    Color32 borderColor = Color32::Black();
    Color32 shadowColor = Color32::Black();
};

static_assert(std::is_trivially_copyable_v<PinkSdfPrimitive>);

// Matches the shader's std430 buffer: four vec4 records per primitive.
struct PinkSdfGpuPrimitive
{
    // xy centre, zw size
    Vector4D rect{};
    // x radius, y border, z rotation, w shadow
    Vector4D params{};
    // rgb + alpha
    Vector4D fill{};
    // rgb + alpha, w = flags as uint bits
    Vector4D border{};
};

static_assert(sizeof(PinkSdfGpuPrimitive) == 64);

// Packs primitives into GPU records. Returns the number written, or 0 when the
[[nodiscard]] std::uint32_t PackPinkSdfPrimitives(std::span<const PinkSdfPrimitive> InPrimitives,
                                                  std::span<PinkSdfGpuPrimitive> OutGpu) noexcept;

// Builds the visible primitives of one card board from SlotComponent data. The
[[nodiscard]] std::uint32_t BuildCardBoardPrimitives(Ecs::Registry& InRegistry, std::uint32_t InBoardId,
                                                     std::span<PinkSdfPrimitive> OutPrimitives) noexcept;

// The RHI draw description for a packed batch. One instanced draw, no texture.
struct PinkSdfDraw
{
    const PinkSdfGpuPrimitive* primitives = nullptr;
    std::uint32_t count = 0;
    std::uint32_t layer = 0;
    std::uint32_t pipeline = 0;
};
}
