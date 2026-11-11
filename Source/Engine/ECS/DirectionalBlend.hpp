// FemboyEngine2D - directional blend space: a baked sampler over a facing vector.
#pragma once

#include "Engine/Core/SimMath.hpp"
#include "Engine/Core/Types.hpp"

#include <array>
#include <cstdint>
#include <type_traits>

namespace Fby
{

// Eight-way characters keep the heading in a fixed layout next to the velocity.
// Direction is unit length; Angle is the same heading in radians in [0, 2*pi).
struct Facing2D
{
    Vector2D Direction{1.0f, 0.0f};
    float Angle = 0.0f;
};

static_assert(std::is_trivially_copyable_v<Facing2D>);
static_assert(std::is_standard_layout_v<Facing2D>);

// Overwrites the facing from a raw vector. A zero vector keeps the previous
// direction: an entity that stops moving keeps looking where it looked.
void Facing2DFromVector(Facing2D& InFacing, Vector2D InVector) noexcept;
}

namespace Fby::Animation
{

inline constexpr std::uint32_t kMaxDirectionalAnchors = 16;
// A ring blends two neighbours; a plane triangle needs three barycentric terms.
inline constexpr std::uint32_t kMaxDirectionalWeights = 3;
inline constexpr float kDirectionalBlendEpsilon = 1.0e-6f;

// Ring treats every anchor as a heading and blends the two angular neighbours.
// Plane treats the anchors as points in the blend coordinate space and blends
// the barycentric triangle that contains the sample, so strafe and forward can
// carry different values.
enum class DirectionalBlendTopology : std::uint8_t
{
    Ring = 0,
    Plane,
};

// One authored sample. Ring reads Position as a heading vector; Plane reads it
// as a blend coordinate. ClipId is opaque to the sampler: the caller maps it to
// a clip or a pose source. WeightScale lets one anchor contribute less without
// moving it.
struct DirectionalBlendAnchor
{
    Vector2D Position{1.0f, 0.0f};
    std::uint32_t ClipId = 0;
    float WeightScale = 1.0f;
    bool Enabled = true;
};

static_assert(std::is_trivially_copyable_v<DirectionalBlendAnchor>);

struct DirectionalBlendSpaceDocument
{
    std::array<DirectionalBlendAnchor, kMaxDirectionalAnchors> Anchors{};
    std::uint16_t AnchorCount = 0;
    DirectionalBlendTopology Topology = DirectionalBlendTopology::Ring;
    // Off keeps the authored WeightScale product, so a caller can inspect the
    // raw influence. On guarantees the emitted weights sum to one.
    bool NormalizeWeights = true;
};

static_assert(std::is_trivially_copyable_v<DirectionalBlendSpaceDocument>);
static_assert(std::is_standard_layout_v<DirectionalBlendSpaceDocument>);

// Baked ring anchors are sorted by Angle, so the runtime bracket lookup is a
// binary search over one contiguous array. Plane anchors keep the authored
// order; Angles stay zero because the topology never reads them.
struct DirectionalBlendAnchorEntry
{
    Vector2D Position{};
    float Angle = 0.0f;
    float WeightScale = 1.0f;
    std::uint32_t ClipId = 0;
};

static_assert(std::is_trivially_copyable_v<DirectionalBlendAnchorEntry>);

struct DirectionalBlendTriangle
{
    std::uint16_t AnchorA = 0;
    std::uint16_t AnchorB = 0;
    std::uint16_t AnchorC = 0;
};

static_assert(std::is_trivially_copyable_v<DirectionalBlendTriangle>);

// Self-contained runtime image. Baking copies every enabled anchor, so a sample
// never reaches back into the authoring document and never allocates. The table
// carries no clock: it is sampled when a caller needs a pose, not every frame.
struct DirectionalBlendTable
{
    std::array<DirectionalBlendAnchorEntry, kMaxDirectionalAnchors> Anchors{};
    // Convex hull of the plane anchors and its fan triangulation. A ring leaves
    // both arrays empty because the sorted anchor array is the whole lookup.
    std::array<std::uint16_t, kMaxDirectionalAnchors> Hull{};
    std::array<DirectionalBlendTriangle, kMaxDirectionalAnchors> Triangles{};
    std::uint16_t AnchorCount = 0;
    std::uint16_t HullCount = 0;
    std::uint16_t TriangleCount = 0;
    DirectionalBlendTopology Topology = DirectionalBlendTopology::Ring;
    bool NormalizeWeights = true;
};

static_assert(std::is_trivially_copyable_v<DirectionalBlendTable>);
static_assert(std::is_standard_layout_v<DirectionalBlendTable>);

// One weighted source of a sample. Weights are non-negative and, unless the
// document opts out, sum to one.
struct DirectionalBlendWeight
{
    std::uint32_t ClipId = 0;
    float Weight = 0.0f;
};

static_assert(std::is_trivially_copyable_v<DirectionalBlendWeight>);

struct DirectionalBlendSample
{
    std::array<DirectionalBlendWeight, kMaxDirectionalWeights> Weights{};
    std::uint8_t Count = 0;
};

static_assert(std::is_trivially_copyable_v<DirectionalBlendSample>);
static_assert(std::is_standard_layout_v<DirectionalBlendSample>);

// Wraps any finite angle into [0, 2*pi). A non-finite input maps to zero so a
// malformed velocity never poisons the authored table.
[[nodiscard]] float DirectionalBlendNormalizeAngle(float InRadians) noexcept;

// Heading of a vector in [0, 2*pi). A zero vector maps to zero.
[[nodiscard]] float DirectionalBlendAngleOf(Vector2D InDirection) noexcept;

// Rejects an empty document, a ring anchor without a heading, a non-finite or
// negative weight scale and an anchor count past the fixed capacity.
[[nodiscard]] bool ValidateDirectionalBlendSpace(const DirectionalBlendSpaceDocument& InDocument) noexcept;

// Compiles the document into a self-contained table. Returns false when the
// document does not validate; the output is cleared in that case, so a failed
// bake can never leave a partially valid table behind.
[[nodiscard]] bool BakeDirectionalBlendSpace(const DirectionalBlendSpaceDocument& InDocument,
                                             DirectionalBlendTable* OutTable) noexcept;

// Pure evaluator: heading or blend coordinate to at most three weighted clip
// ids. No state, no clock, no allocation; repeated calls with the same input
// return the same sample, and sampling does not advance any animation.
void EvaluateDirectionalBlend(const DirectionalBlendTable& InTable, Vector2D InDirection,
                              DirectionalBlendSample* OutSample) noexcept;
}
