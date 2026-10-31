// FemboyEngine2D - Box2D v3 joint chains: ropes, cables and rigid-link chains.
#pragma once

#include "Engine/Core/Types.hpp"
#include "Engine/ECS/StableIds.hpp"
#include "Engine/Physics/Physics.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <type_traits>

namespace Fby
{

// A chain link entity carries this marker so a renderer can draw the rope with
struct JointLinkComponent
{
    // Stable identity, never a b2JointId
    ChainId ChainID{};
    std::uint16_t LinkIndex = 0;
    std::uint16_t Flags = 0;
    // Metres, capsule half segment length
    float HalfLength = 0.0f;
    // Metres
    float Radius = 0.0f;
    // Runtime pool slot, not serialized
    std::uint32_t ChainSlot = 0;
    // Runtime slot generation
    std::uint32_t ChainGeneration = 0;
};

static_assert(std::is_trivially_copyable_v<JointLinkComponent>);
static_assert(std::is_standard_layout_v<JointLinkComponent>);
static_assert(sizeof(JointLinkComponent) == 24);

enum class JointChainKind : std::uint8_t
{
    // B2DistanceJoint between links, soft and stretchable
    Rope = 0,
    // B2RevoluteJoint between links, rigid pins with angle limits
    Chain = 1,
};

enum JointChainFlags : std::uint32_t
{
    JointChainNone = 0,
    // Create a static anchor body at the first/last path point when no valid
    JointChainPinStart = 1u << 0,
    JointChainPinEnd = 1u << 1,
    // Let directly connected links collide. Off by default: a rope that
    JointChainCollideConnected = 1u << 2,
    // Use a spring for every rope distance joint instead of a rigid distance.
    JointChainSpringRope = 1u << 3,
    // Emit contact/hit events for the link shapes. Off by default; a long rope
    JointChainContactEvents = 1u << 4,
};

[[nodiscard]] constexpr std::uint32_t operator|(JointChainFlags InA, JointChainFlags InB) noexcept
{
    return static_cast<std::uint32_t>(InA) | static_cast<std::uint32_t>(InB);
}

enum class ChainError : std::uint8_t
{
    None = 0,
    NotInitialized,
    InvalidDesc,
    OutOfChains,
    AnchorBodyMissing,
    OutOfLinks,
    Box2DRejected,
};

[[nodiscard]] constexpr const char* ChainErrorName(ChainError InError) noexcept
{
    switch (InError)
    {
        case ChainError::None:
            return "None";
        case ChainError::NotInitialized:
            return "NotInitialized";
        case ChainError::InvalidDesc:
            return "InvalidDesc";
        case ChainError::OutOfChains:
            return "OutOfChains";
        case ChainError::AnchorBodyMissing:
            return "AnchorBodyMissing";
        case ChainError::OutOfLinks:
            return "OutOfLinks";
        case ChainError::Box2DRejected:
            return "Box2DRejected";
    }
    return "?";
}

// Description of one rope/chain. The path form is for authored cables that
struct JointChainDesc
{
    JointChainKind kind = JointChainKind::Rope;
    std::uint32_t flags = JointChainPinStart | JointChainPinEnd;

    // Pixels; path.size() >= 2
    std::span<const Vector2D> path{};
    Vector2D start{};
    Vector2D end{0.0f, 128.0f};
    // Used only when path is empty
    std::uint32_t segmentCount = 8;

    Entity anchorA = entt::null;
    Entity anchorB = entt::null;

    // Pixels
    float radius = 3.0f;
    // Kg / m^2
    float density = 1.0f;
    float friction = 0.5f;
    float restitution = 0.0f;
    // Engine chain category
    std::uint64_t categoryBits = 0x0008u;
    std::uint64_t maskBits = 0xFFFFFFFFu;

    // Rope spring (JointChainSpringRope). Ignored for a rigid rope.
    float hertz = 4.0f;
    float dampingRatio = 0.7f;

    // Chain angle limits around the straight rest angle.
    float lowerAngle = -0.65f;
    float upperAngle = 0.65f;
    bool enableLimit = true;
    bool enableMotor = false;
    float motorSpeed = 0.0f;
    float maxMotorTorque = 0.0f;

    // Visual/authoring filter. The link entities always receive a
    std::uint16_t linkLayer = 0;
};

struct JointChainHandle
{
    static constexpr std::uint32_t kInvalid = 0xFFFFFFFFu;

    std::uint32_t index = kInvalid;
    std::uint32_t generation = 0;
    ChainId stableId{};

    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return index != kInvalid;
    }
    [[nodiscard]] friend constexpr bool operator==(JointChainHandle InA, JointChainHandle InB) noexcept
    {
        return InA.index == InB.index && InA.generation == InB.generation;
    }
};

// Persistent authoring record. Unlike JointChainDesc it owns its path bytes and
struct JointChainComponent
{
    ChainId ChainID{};
    std::uint32_t Kind = static_cast<std::uint32_t>(JointChainKind::Rope);
    std::uint32_t Flags = JointChainPinStart | JointChainPinEnd;
    Entity AnchorA = entt::null;
    Entity AnchorB = entt::null;
    Vector2D Start{};
    Vector2D End{0.0f, 128.0f};
    std::uint32_t SegmentCount = 8;
    std::uint32_t PathCount = 0;
    std::array<Vector2D, 16> Path{};
    float Radius = 3.0f;
    float Density = 1.0f;
    float Friction = 0.5f;
    float Restitution = 0.0f;
    float Hertz = 4.0f;
    float DampingRatio = 0.7f;
    float LowerAngle = -0.65f;
    float UpperAngle = 0.65f;
    float MotorSpeed = 0.0f;
    float MaxMotorTorque = 0.0f;
    std::uint64_t CategoryBits = 0x0008u;
    std::uint64_t MaskBits = 0xFFFFFFFFu;
    std::uint16_t LinkLayer = 0;
    bool EnableLimit = true;
    bool EnableMotor = false;
};

static_assert(std::is_trivially_copyable_v<JointChainComponent>);
static_assert(std::is_standard_layout_v<JointChainComponent>);

struct JointChainInfo
{
    ChainId chainId{};
    JointChainKind kind = JointChainKind::Rope;
    std::uint32_t flags = 0;
    std::uint32_t generation = 0;
    std::uint32_t linkCount = 0;
    std::uint32_t jointCount = 0;
    Entity anchorA = entt::null;
    Entity anchorB = entt::null;

    [[nodiscard]] std::uint32_t LinkCount() const noexcept
    {
        return linkCount;
    }
};

// PhysicsJointSystem
class PhysicsJointSystem
{
public:
    static constexpr std::uint32_t kMaxChains = 32;
    static constexpr std::uint32_t kMaxLinksPerChain = 64;
    static constexpr std::uint32_t kMaxJointsPerChain = (kMaxLinksPerChain - 1) + 2;

    PhysicsJointSystem() = default;
    ~PhysicsJointSystem();

    PhysicsJointSystem(const PhysicsJointSystem&) = delete;
    PhysicsJointSystem& operator=(const PhysicsJointSystem&) = delete;

    // Binds to an initialized PhysicsSystem. Does not create a world of its own.
    [[nodiscard]] bool Initialize(PhysicsSystem& InPhysics) noexcept;
    // Destroys every chain and its link entities. Must run before the physics
    void Shutdown(Ecs::Registry& InRegistry) noexcept;

    // Creates a rope/chain and returns an invalid handle on failure; `error`
    [[nodiscard]] JointChainHandle CreateChain(Ecs::Registry& InRegistry, const JointChainDesc& InDesc,
                                               ChainError* InError = nullptr) noexcept;
    // Authoring path: persists the component and allocates a stable ChainId when
    [[nodiscard]] JointChainHandle CreateChain(Ecs::Registry& InRegistry, JointChainComponent& InComponent,
                                               ChainError* InError = nullptr) noexcept;

    // Destroys joints, anchors it owns, link bodies and the link entities.
    bool DestroyChain(Ecs::Registry& InRegistry, JointChainHandle InHandle) noexcept;

    [[nodiscard]] bool IsValid(JointChainHandle InHandle) const noexcept;
    [[nodiscard]] JointChainInfo Info(JointChainHandle InHandle) const noexcept;
    [[nodiscard]] std::uint32_t ChainCount() const noexcept;
    [[nodiscard]] std::uint32_t LinkCount() const noexcept;
    [[nodiscard]] std::uint32_t JointCount() const noexcept;

    // Reads the current transform of a link. Used by debug draw and by the
    [[nodiscard]] bool LinkTransform(Ecs::Registry& InRegistry, JointChainHandle InHandle, std::uint32_t InLink,
                                     Transform2D* Out) const noexcept;

private:
    struct ChainRuntime
    {
        bool active = false;
        std::uint32_t index = 0;
        std::uint32_t generation = 1;
        ChainId stableId{};
        JointChainKind kind = JointChainKind::Rope;
        std::uint32_t flags = 0;
        std::uint32_t linkCount = 0;
        std::uint32_t jointCount = 0;
        Entity anchorA = entt::null;
        Entity anchorB = entt::null;
        // Packed b2BodyId (owned or borrowed)
        std::uint64_t anchorBodyA = 0;
        std::uint64_t anchorBodyB = 0;
        bool ownsAnchorBodyA = false;
        bool ownsAnchorBodyB = false;
        float radius = 0.0f;
        std::array<Entity, kMaxLinksPerChain> links{};
        std::array<std::uint64_t, kMaxLinksPerChain> linkBodies{};
        std::array<std::uint64_t, kMaxJointsPerChain> joints{};
    };

    [[nodiscard]] ChainRuntime* Runtime(JointChainHandle InHandle) noexcept;
    [[nodiscard]] const ChainRuntime* Runtime(JointChainHandle InHandle) const noexcept;
    void DestroyRuntime(Ecs::Registry& InRegistry, ChainRuntime& InChain) noexcept;
    void ResetRuntime(ChainRuntime& InChain) noexcept;

    PhysicsSystem* MPhysics = nullptr;
    std::array<ChainRuntime, kMaxChains> MChains{};
    std::uint32_t MLiveChains = 0;
    StableIdAllocator<ChainId> MStableIds{};
};
}

FBY_REFLECT_COMPONENT(Fby::JointLinkComponent, "JointLinkComponent",
                      FBY_FIELD(Fby::JointLinkComponent, ChainID, UInt32),
                      FBY_FIELD(Fby::JointLinkComponent, LinkIndex, UInt32),
                      FBY_FIELD(Fby::JointLinkComponent, Flags, UInt32),
                      FBY_FIELD(Fby::JointLinkComponent, HalfLength, Float),
                      FBY_FIELD(Fby::JointLinkComponent, Radius, Float))

FBY_REFLECT_COMPONENT(
    Fby::JointChainComponent, "JointChainComponent", FBY_FIELD(Fby::JointChainComponent, ChainID, UInt32),
    FBY_FIELD(Fby::JointChainComponent, Kind, UInt32), FBY_FIELD(Fby::JointChainComponent, Flags, UInt32),
    FBY_FIELD(Fby::JointChainComponent, AnchorA, Entity), FBY_FIELD(Fby::JointChainComponent, AnchorB, Entity),
    FBY_FIELD(Fby::JointChainComponent, Start, Vector2D), FBY_FIELD(Fby::JointChainComponent, End, Vector2D),
    FBY_FIELD(Fby::JointChainComponent, SegmentCount, UInt32), FBY_FIELD(Fby::JointChainComponent, PathCount, UInt32),
    FBY_FIELD(Fby::JointChainComponent, Radius, Float), FBY_FIELD(Fby::JointChainComponent, Density, Float),
    FBY_FIELD(Fby::JointChainComponent, Friction, Float), FBY_FIELD(Fby::JointChainComponent, Restitution, Float),
    FBY_FIELD(Fby::JointChainComponent, Hertz, Float), FBY_FIELD(Fby::JointChainComponent, DampingRatio, Float),
    FBY_FIELD(Fby::JointChainComponent, LowerAngle, Float), FBY_FIELD(Fby::JointChainComponent, UpperAngle, Float),
    FBY_FIELD(Fby::JointChainComponent, EnableLimit, Bool), FBY_FIELD(Fby::JointChainComponent, EnableMotor, Bool),
    FBY_FIELD(Fby::JointChainComponent, MotorSpeed, Float), FBY_FIELD(Fby::JointChainComponent, MaxMotorTorque, Float))
