// FemboyEngine2D - Box2D v3 joint graph: ropes, chains, springs and ties.
#pragma once

#include "Engine/Core/Types.hpp"
#include "Engine/ECS/StableIds.hpp"
#include "Engine/Physics/Physics.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <type_traits>

namespace Fby
{

// A connection endpoint without a chain or link, used by anchor pins and ties.
inline constexpr std::uint32_t kNoChain = 0xFFFFFFFFu;
inline constexpr std::uint16_t kNoLink = 0xFFFFu;

// Link flags a renderer reads without querying the joint system.
enum JointLinkFlags : std::uint16_t
{
    JointLinkNone = 0,
    // No connection to the previous link: the chain has a cut end here.
    JointLinkFreeStart = 1u << 0,
    // No connection to the next link.
    JointLinkFreeEnd = 1u << 1,
};

// A chain link entity carries this marker so a renderer can draw the link
// without querying the joint system.
struct JointLinkComponent
{
    // Stable identity, never a b2JointId
    ChainId ChainID{};
    std::uint16_t LinkIndex = 0;
    // JointLinkFlags
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

// Runtime connection kind. Rope and Hinge both pin two points together; a
// hinge also owns angle limits and a motor, a rope has no angular state.
enum class JointConnectionKind : std::uint8_t
{
    Hinge = 0,
    Rope = 1,
    SpringRope = 2,
    SpringHinge = 3,
};

enum class JointBreakReason : std::uint8_t
{
    Force = 0,
    Torque = 1,
    Cut = 2,
    Damage = 3,
};

enum JointConnectionFlags : std::uint32_t
{
    JointConnectionNone = 0,
    // Created by TieChains rather than by a chain builder
    JointConnectionTie = 1u << 0,
    // A distance joint between a chain link and a static anchor
    JointConnectionPinned = 1u << 1,
    // Poll the joint reaction every Update and publish Stress
    JointConnectionTrackStress = 1u << 2,
};

enum JointChainFlags : std::uint32_t
{
    JointChainNone = 0,
    // Create a static anchor body at the first/last path point when no valid
    // anchor entity was supplied.
    JointChainPinStart = 1u << 0,
    JointChainPinEnd = 1u << 1,
    // Let directly connected links collide. Off by default: a rope whose
    // segments collided would push itself apart.
    JointChainCollideConnected = 1u << 2,
    // Use a spring for every rope distance joint instead of a rigid distance.
    JointChainSpringRope = 1u << 3,
    // Emit contact/hit events for the link shapes. Off by default; a long rope
    // would otherwise flood the contact event queue.
    JointChainContactEvents = 1u << 4,
    // Use a spring on every hinge connection.
    JointChainSpringHinge = 1u << 5,
    // Poll joint reactions and publish Stress on every connection.
    JointChainTrackStress = 1u << 6,
};

[[nodiscard]] constexpr std::uint32_t operator|(JointChainFlags InA, JointChainFlags InB) noexcept
{
    return static_cast<std::uint32_t>(InA) | static_cast<std::uint32_t>(InB);
}

// One authored run of links. A chain is a sequence of sections, so a rope can
// be spliced between two rigid runs without a second subsystem.
struct JointChainSection
{
    JointChainKind Kind = JointChainKind::Chain;
    // Subset of JointChainFlags: JointChainSpringRope and JointChainSpringHinge
    std::uint32_t Flags = 0;
    // Links in this run; the chain has sum(LinkCount) links
    std::uint32_t LinkCount = 1;
    // Pixels; zero reuses JointChainDesc::Radius
    float Radius = 0.0f;
    // Relative link length for a generated path; ignored when a path is given
    float LinkLengthScale = 1.0f;
    // Newtons; zero reuses JointChainDesc::BreakForce
    float BreakForce = 0.0f;
    // Newton metres; zero reuses JointChainDesc::BreakTorque
    float BreakTorque = 0.0f;
    // Newtons; zero leaves the Box2D spring force range unbounded
    float LowerSpringForce = 0.0f;
    float UpperSpringForce = 0.0f;
    // Damage per second at full overstress; zero breaks instantly at the limit
    float DamageRate = 0.0f;
    // A stress ratio below one makes the run accumulate damage before it snaps
    float DamageThreshold = 1.0f;
};

static_assert(std::is_trivially_copyable_v<JointChainSection>);
static_assert(std::is_standard_layout_v<JointChainSection>);

enum class ChainError : std::uint8_t
{
    None = 0,
    NotInitialized,
    InvalidDesc,
    OutOfChains,
    AnchorBodyMissing,
    OutOfLinks,
    Box2DRejected,
    InvalidConnection,
    OutOfConnections,
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
        case ChainError::InvalidConnection:
            return "InvalidConnection";
        case ChainError::OutOfConnections:
            return "OutOfConnections";
    }
    return "?";
}

// Description of one rope or chain. The path form is for authored cables that
// need exact geometry; otherwise the builder distributes links along Start/End.
struct JointChainDesc
{
    JointChainKind Kind = JointChainKind::Rope;
    std::uint32_t Flags = JointChainPinStart | JointChainPinEnd;

    // Pixels; Path.size() >= 2
    std::span<const Vector2D> Path{};
    Vector2D Start{};
    Vector2D End{0.0f, 128.0f};
    // Used only when Path and Sections are both empty
    std::uint32_t SegmentCount = 8;

    // Optional runs of link materials. LinkCount sums to the link count and
    // replaces SegmentCount when Path is empty.
    std::span<const JointChainSection> Sections{};

    Entity AnchorA = Fby::kNullEntity;
    Entity AnchorB = Fby::kNullEntity;

    // Pixels
    float Radius = 3.0f;
    // Kg / m^2
    float Density = 1.0f;
    float Friction = 0.5f;
    float Restitution = 0.0f;
    // Engine chain category
    std::uint64_t CategoryBits = 0x0008u;
    std::uint64_t MaskBits = 0xFFFFFFFFu;

    // Rope spring (JointChainSpringRope). Ignored for a rigid rope.
    float Hertz = 4.0f;
    float DampingRatio = 0.7f;

    // Chain angle limits around the straight rest angle.
    float LowerAngle = -0.65f;
    float UpperAngle = 0.65f;
    bool EnableLimit = true;
    bool EnableMotor = false;
    float MotorSpeed = 0.0f;
    float MaxMotorTorque = 0.0f;

    // Newtons and newton metres; zero keeps the connection unbreakable
    float BreakForce = 0.0f;
    float BreakTorque = 0.0f;
    // Tension model. BaseTension is preload, TensionPerDistance adds N per
    // pixel of stretch, and TensionDistance caps the growth (0 = uncapped).
    float BaseTension = 0.0f;
    float MaxTension = 0.0f;
    float TensionPerDistance = 0.0f;
    float TensionDistance = 0.0f;
    bool StrengthEnabled = true;
    // Spring force range for rope sections that leave it at zero
    float LowerSpringForce = 0.0f;
    float UpperSpringForce = 0.0f;
    // Damage defaults for pins and section boundaries
    float DamageRate = 0.0f;
    float DamageThreshold = 1.0f;

    // Visual/authoring filter. The link entities always receive a collider in
    // this layer.
    std::uint16_t LinkLayer = 0;
};

struct JointChainHandle
{
    static constexpr std::uint32_t kInvalid = 0xFFFFFFFFu;

    std::uint32_t Index = kInvalid;
    std::uint32_t Generation = 0;
    ChainId StableId{};

    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return Index != kInvalid;
    }
    [[nodiscard]] friend constexpr bool operator==(JointChainHandle InA, JointChainHandle InB) noexcept
    {
        return InA.Index == InB.Index && InA.Generation == InB.Generation;
    }
};

struct JointConnectionHandle
{
    static constexpr std::uint32_t kInvalid = 0xFFFFFFFFu;

    std::uint32_t Index = kInvalid;
    std::uint32_t Generation = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return Index != kInvalid;
    }
    [[nodiscard]] friend constexpr bool operator==(JointConnectionHandle InA, JointConnectionHandle InB) noexcept
    {
        return InA.Index == InB.Index && InA.Generation == InB.Generation;
    }
};

// Public view of one connection. It survives a break so gameplay can inspect or
// repair the cut instead of chasing a handle that vanished.
struct JointConnectionInfo
{
    JointConnectionHandle Handle{};
    JointConnectionKind Kind = JointConnectionKind::Rope;
    std::uint32_t Flags = 0;
    // Chain slots; kNoChain when the endpoint is a free body
    std::uint32_t ChainA = kNoChain;
    std::uint32_t ChainB = kNoChain;
    // Link indices inside their chains; kNoLink for anchor pins
    std::uint16_t LinkA = kNoLink;
    std::uint16_t LinkB = kNoLink;
    // Newtons and newton metres
    float BreakForce = 0.0f;
    float BreakTorque = 0.0f;
    // Current tension from the authored model and its cap state
    float CurrentTension = 0.0f;
    float MaxTension = 0.0f;
    // Zero to one when the connection or its chain tracks stress
    float Stress = 0.0f;
    // Zero to one when the connection accumulates damage
    float Damage = 0.0f;
    bool StrengthEnabled = true;
    bool TensionLocked = false;
    bool Broken = false;
};

// One break, in the order the joint system observed it.
struct ChainBreakEvent
{
    JointChainHandle ChainA{};
    JointChainHandle ChainB{};
    JointConnectionHandle Connection{};
    Vector2D Point{};
    float Force = 0.0f;
    float Torque = 0.0f;
    std::uint16_t LinkA = kNoLink;
    std::uint16_t LinkB = kNoLink;
    std::uint32_t Sequence = 0;
    JointBreakReason Reason = JointBreakReason::Force;
};

struct JointTieDesc
{
    // Rope, SpringRope, Hinge or SpringHinge
    JointConnectionKind Kind = JointConnectionKind::Rope;
    // Pixels; zero ties the links at their current distance
    float Length = 0.0f;
    float Hertz = 4.0f;
    float DampingRatio = 0.7f;
    // Newtons and newton metres; zero keeps the tie unbreakable
    float BreakForce = 0.0f;
    float BreakTorque = 0.0f;
    // Spring force range for a spring tie
    float LowerSpringForce = 0.0f;
    float UpperSpringForce = 0.0f;
    // Damage per second at full overstress; zero breaks instantly
    float DamageRate = 0.0f;
    float DamageThreshold = 1.0f;
    bool CollideConnected = false;
    bool TrackStress = false;
};

// Serializable snapshot of one cross-chain tie. A game stores these next to its
// world save and feeds them back through RestoreTies once the chains exist.
struct JointTieSave
{
    ChainId ChainA{};
    ChainId ChainB{};
    std::uint16_t LinkA = 0;
    std::uint16_t LinkB = 0;
    JointConnectionKind Kind = JointConnectionKind::Rope;
    // Pixels; zero ties at the current distance
    float Length = 0.0f;
    float Hertz = 4.0f;
    float DampingRatio = 0.7f;
    float BreakForce = 0.0f;
    float BreakTorque = 0.0f;
    float LowerSpringForce = 0.0f;
    float UpperSpringForce = 0.0f;
    float DamageRate = 0.0f;
    float DamageThreshold = 1.0f;
    bool CollideConnected = false;
    bool TrackStress = false;
    // A restored tie is cut again when the saved one was broken
    bool Broken = false;
};

static_assert(std::is_trivially_copyable_v<JointTieSave>);

// Persistent authoring record. Unlike JointChainDesc it owns its path bytes and
// its section array, so gameplay and snapshots can rebuild the chain later.
struct JointChainComponent
{
    ChainId ChainID{};
    std::uint32_t Kind = static_cast<std::uint32_t>(JointChainKind::Rope);
    std::uint32_t Flags = JointChainPinStart | JointChainPinEnd;
    Entity AnchorA = Fby::kNullEntity;
    Entity AnchorB = Fby::kNullEntity;
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
    // Appended fields keep the earlier offsets stable for older payloads
    std::uint32_t SectionCount = 0;
    std::array<JointChainSection, 8> Sections{};
    float DefaultBreakForce = 0.0f;
    float DefaultBreakTorque = 0.0f;
    // Rope tension model. MaxTension overrides DefaultBreakForce when non-zero;
    // StrengthEnabled=false makes the chain unbreakable regardless of values.
    float BaseTension = 0.0f;
    float MaxTension = 0.0f;
    float TensionPerDistance = 0.0f;
    float TensionDistance = 0.0f;
    bool StrengthEnabled = true;
    float DefaultLowerSpringForce = 0.0f;
    float DefaultUpperSpringForce = 0.0f;
    float DefaultDamageRate = 0.0f;
    float DefaultDamageThreshold = 1.0f;
    // Bit per chain connection: internal links first, then the start and end pin
    std::uint64_t BrokenMaskLow = 0;
    std::uint64_t BrokenMaskHigh = 0;
};

static_assert(std::is_trivially_copyable_v<JointChainComponent>);
static_assert(std::is_standard_layout_v<JointChainComponent>);

struct JointChainInfo
{
    ChainId ChainID{};
    JointChainKind Kind = JointChainKind::Rope;
    std::uint32_t Flags = 0;
    std::uint32_t Generation = 0;
    std::uint32_t LinkCount = 0;
    // Connections that still hold; broken ones are counted separately
    std::uint32_t JointCount = 0;
    std::uint32_t BrokenCount = 0;
    Entity AnchorA = Fby::kNullEntity;
    Entity AnchorB = Fby::kNullEntity;
};

// PhysicsJointSystem
class PhysicsJointSystem
{
public:
    static constexpr std::uint32_t kMaxChains = 32;
    static constexpr std::uint32_t kMaxLinksPerChain = 64;
    static constexpr std::uint32_t kMaxJointsPerChain = (kMaxLinksPerChain - 1) + 2;
    static constexpr std::uint32_t kMaxChainSections = 8;
    // Chain connections plus cross ties share one fixed pool
    static constexpr std::uint32_t kMaxConnections = 4096;
    static constexpr std::uint32_t kMaxBreakEvents = 256;

    PhysicsJointSystem() = default;
    ~PhysicsJointSystem();

    PhysicsJointSystem(const PhysicsJointSystem&) = delete;
    PhysicsJointSystem& operator=(const PhysicsJointSystem&) = delete;

    // Binds to an initialized PhysicsSystem. Does not create a world of its own.
    [[nodiscard]] bool Initialize(PhysicsSystem& InPhysics) noexcept;
    // Destroys every chain and its link entities. Must run before the physics
    // world is destroyed, because joint ids become invalid with it.
    void Shutdown(Ecs::Registry& InRegistry) noexcept;

    // Creates a rope or chain and returns an invalid handle on failure; `error`
    // receives the reason.
    [[nodiscard]] JointChainHandle CreateChain(Ecs::Registry& InRegistry, const JointChainDesc& InDesc,
                                               ChainError* InError = nullptr) noexcept;
    // Authoring path: persists the component, allocates a stable ChainId and
    // writes the id back into the component.
    [[nodiscard]] JointChainHandle CreateChain(Ecs::Registry& InRegistry, JointChainComponent& InComponent,
                                               ChainError* InError = nullptr) noexcept;

    // Destroys joints, anchors it owns, link bodies and the link entities.
    bool DestroyChain(Ecs::Registry& InRegistry, JointChainHandle InHandle) noexcept;

    // Extra connection between two chain links. This is what turns a chain into
    // a graph: branch, tie two ropes, or tie a link to itself.
    [[nodiscard]] JointConnectionHandle TieChains(Ecs::Registry& InRegistry, JointChainHandle InChainA,
                                                  std::uint32_t InLinkA, JointChainHandle InChainB,
                                                  std::uint32_t InLinkB, const JointTieDesc& InDesc = {},
                                                  ChainError* InError = nullptr) noexcept;
    // Ties two rigid bodies that are not chain links. Such a tie is runtime-only:
    // entity ids are not stable across a scene reload, so CaptureTies skips it.
    [[nodiscard]] JointConnectionHandle TieBodies(Ecs::Registry& InRegistry, Entity InEntityA, Entity InEntityB,
                                                  const JointTieDesc& InDesc = {},
                                                  ChainError* InError = nullptr) noexcept;
    // Removes a tie created by TieChains or TieBodies; chain-owned connections
    // are cut instead.
    bool Untie(JointConnectionHandle InHandle) noexcept;

    // Copies every live tie into Out; returns the number written. The caller
    // stores the records and calls RestoreTies after recreating the chains.
    [[nodiscard]] std::uint32_t CaptureTies(std::span<JointTieSave> Out) const noexcept;
    [[nodiscard]] bool RestoreTies(Ecs::Registry& InRegistry, std::span<const JointTieSave> InTies,
                                   ChainError* InError = nullptr) noexcept;

    // Consumes Box2D joint events and applies breaks and damage. Call after
    // PhysicsSystem::Step in the same fixed step; it never allocates and never
    // destroys link bodies.
    void Update(Ecs::Registry& InRegistry, float InFixedDt) noexcept;

    // Breaks observed since the last ClearBreaks(). The queue is fixed capacity
    // and drops later events once full.
    [[nodiscard]] std::span<const ChainBreakEvent> Breaks() const noexcept;
    void ClearBreaks() noexcept
    {
        MBreakCount = 0;
    }

    // Gameplay cuts. Both go through the same path as a physical break, so the
    // event, the link flags and the repair state are identical.
    bool CutConnection(Ecs::Registry& InRegistry, JointConnectionHandle InHandle) noexcept;
    // Cuts the live connection whose link centres are closest to a world point.
    bool CutAtPoint(Ecs::Registry& InRegistry, JointChainHandle InChain, Vector2D InWorldPoint,
                    float InTolerancePixels = 8.0f) noexcept;
    // Recreates a broken joint from the stored frames at the current pose.
    bool RepairConnection(Ecs::Registry& InRegistry, JointConnectionHandle InHandle,
                          ChainError* InError = nullptr) noexcept;

    // Rebuilds a chain in place from a new description while keeping its slot,
    // generation and stable ChainId, so external handles stay valid.
    [[nodiscard]] bool RebuildChain(Ecs::Registry& InRegistry, JointChainHandle InHandle, const JointChainDesc& InDesc,
                                    ChainError* InError = nullptr) noexcept;

    // Finds a live chain by its stable id, for save/restore and script bindings.
    [[nodiscard]] JointChainHandle FindChain(ChainId InId) const noexcept;

    // Writes the chain's broken-connection bits into the component so the next
    // save carries them. The reverse is ApplyChainState.
    void CaptureChainState(JointChainHandle InHandle, JointChainComponent* InChainState) const noexcept;
    // Applies the component's BrokenMask: cuts bits that are set, repairs bits
    // that are clear. Idempotent, so it is safe after a snapshot restore.
    [[nodiscard]] bool ApplyChainState(Ecs::Registry& InRegistry, JointChainHandle InHandle,
                                       const JointChainComponent& InComponent, ChainError* InError = nullptr) noexcept;

    [[nodiscard]] bool IsValid(JointChainHandle InHandle) const noexcept;
    [[nodiscard]] JointChainInfo Info(JointChainHandle InHandle) const noexcept;
    [[nodiscard]] std::uint32_t ChainCount() const noexcept;
    [[nodiscard]] std::uint32_t LinkCount() const noexcept;
    [[nodiscard]] std::uint32_t JointCount() const noexcept;
    [[nodiscard]] std::uint32_t TieCount() const noexcept;

    // Reads the current transform of a link. Used by debug draw and by tests
    // that need the interpolated pose.
    [[nodiscard]] bool LinkTransform(Ecs::Registry& InRegistry, JointChainHandle InHandle, std::uint32_t InLink,
                                     Transform2D* Out) const noexcept;

    [[nodiscard]] bool ConnectionInfo(JointConnectionHandle InHandle, JointConnectionInfo* Out) const noexcept;
    // Iterates the connections a chain owns, in build order: links first, then
    // the start pin and the end pin.
    [[nodiscard]] std::uint32_t ChainConnectionCount(JointChainHandle InHandle) const noexcept;
    [[nodiscard]] bool ChainConnection(JointChainHandle InHandle, std::uint32_t InConnection,
                                       JointConnectionInfo* Out) const noexcept;

private:
    struct JointConnectionParams
    {
        // Metres, local to the endpoint bodies
        Vector2D LocalA{};
        Vector2D LocalB{};
        // Metres; distance joints only
        float Length = 0.0f;
        float Hertz = 4.0f;
        float DampingRatio = 0.7f;
        // Radians; hinge joints only
        float LowerAngle = -0.65f;
        float UpperAngle = 0.65f;
        float MotorSpeed = 0.0f;
        float MaxMotorTorque = 0.0f;
        // Newtons; zero leaves the spring force range unbounded
        float LowerSpringForce = 0.0f;
        float UpperSpringForce = 0.0f;
        // Tension model; zero MaxTension falls back to BreakForce
        float BaseTension = 0.0f;
        float MaxTension = 0.0f;
        float TensionPerDistance = 0.0f;
        float TensionDistance = 0.0f;
        // Damage per second at full overstress and the stress ratio that starts it
        float DamageRate = 0.0f;
        float DamageThreshold = 1.0f;
        bool EnableLimit = false;
        bool EnableMotor = false;
        bool CollideConnected = false;
        bool StrengthEnabled = true;
    };

    struct ConnectionRuntime
    {
        // The slot is live; a broken connection keeps its slot for repair
        bool Active = false;
        bool Broken = false;
        JointConnectionKind Kind = JointConnectionKind::Rope;
        std::uint32_t Flags = 0;
        std::uint32_t Generation = 1;
        // Chain slots; kNoChain when the endpoint is a plain body
        std::uint32_t ChainA = kNoChain;
        std::uint32_t ChainB = kNoChain;
        std::uint16_t LinkA = kNoLink;
        std::uint16_t LinkB = kNoLink;
        std::uint32_t Section = 0;
        // Packed b2JointId; zero while broken
        std::uint64_t Joint = 0;
        // Packed b2BodyId
        std::uint64_t BodyA = 0;
        std::uint64_t BodyB = 0;
        float BreakForce = 0.0f;
        float BreakTorque = 0.0f;
        float Stress = 0.0f;
        float Damage = 0.0f;
        float CurrentTension = 0.0f;
        bool TensionLocked = false;
        JointConnectionParams Params{};
    };

    struct ChainRuntime
    {
        bool Active = false;
        std::uint32_t Index = 0;
        std::uint32_t Generation = 1;
        ChainId StableId{};
        JointChainKind Kind = JointChainKind::Rope;
        std::uint32_t Flags = 0;
        std::uint32_t LinkCount = 0;
        std::uint32_t ConnectionCount = 0;
        Entity AnchorA = Fby::kNullEntity;
        Entity AnchorB = Fby::kNullEntity;
        // Packed b2BodyId (owned or borrowed)
        std::uint64_t AnchorBodyA = 0;
        std::uint64_t AnchorBodyB = 0;
        bool OwnsAnchorBodyA = false;
        bool OwnsAnchorBodyB = false;
        float Radius = 0.0f;
        std::array<Entity, kMaxLinksPerChain> Links{};
        std::array<std::uint64_t, kMaxLinksPerChain> LinkBodies{};
        // Slots in the shared connection pool, in build order
        std::array<std::uint32_t, kMaxJointsPerChain> ConnectionSlots{};
    };

    struct Storage
    {
        std::array<ConnectionRuntime, kMaxConnections> Connections{};
        std::array<std::uint32_t, kMaxConnections> FreeStack{};
        // Compact list of live slots so Update does not scan the whole pool.
        std::array<std::uint32_t, kMaxConnections> ActiveSlots{};
        std::array<ChainRuntime, kMaxChains> Chains{};
        std::array<ChainBreakEvent, kMaxBreakEvents> Breaks{};
        std::uint32_t FreeCount = 0;
        std::uint32_t ActiveCount = 0;
    };

    [[nodiscard]] std::array<ChainRuntime, kMaxChains>& Chains() noexcept
    {
        return MStorage->Chains;
    }
    [[nodiscard]] const std::array<ChainRuntime, kMaxChains>& Chains() const noexcept
    {
        return MStorage->Chains;
    }
    [[nodiscard]] ChainRuntime* Runtime(JointChainHandle InHandle) noexcept;
    [[nodiscard]] const ChainRuntime* Runtime(JointChainHandle InHandle) const noexcept;
    [[nodiscard]] ConnectionRuntime* Connection(std::uint32_t InIndex) noexcept;
    [[nodiscard]] const ConnectionRuntime* Connection(std::uint32_t InIndex) const noexcept;
    [[nodiscard]] ConnectionRuntime* Find(JointConnectionHandle InHandle) noexcept;
    [[nodiscard]] const ConnectionRuntime* Find(JointConnectionHandle InHandle) const noexcept;

    [[nodiscard]] std::uint32_t AllocateConnection() noexcept;
    void FreeConnection(std::uint32_t InIndex) noexcept;
    // Creates the Box2D joint described by the connection parameters, including
    // the force and torque thresholds that drive break events.
    [[nodiscard]] bool CreateJoint(ConnectionRuntime& InConnection, std::uint32_t InIndex) noexcept;
    // Destroys the joint, keeps the slot, updates the link flags and emits one event.
    bool BreakConnection(Ecs::Registry& InRegistry, std::uint32_t InIndex, JointBreakReason InReason) noexcept;
    // Recomputes JointLinkFlags for every link of a chain from its live connections.
    void RefreshLinkFlags(Ecs::Registry& InRegistry, std::uint32_t InChainSlot) noexcept;
    [[nodiscard]] JointConnectionHandle CreateTie(std::uint64_t InBodyA, std::uint64_t InBodyB, std::uint32_t InChainA,
                                                  std::uint32_t InChainB, std::uint16_t InLinkA, std::uint16_t InLinkB,
                                                  const JointTieDesc& InDesc, ChainError* InError) noexcept;
    // `InRebuild` carries the slot, generation and stable id of a chain that is
    // being recreated under the same handle; null selects a free slot instead.
    [[nodiscard]] JointChainHandle CreateChainInternal(Ecs::Registry& InRegistry, const JointChainDesc& InDesc,
                                                       const ChainRuntime* InRebuild, ChainError* InError) noexcept;
    // `InKeepIdentity` leaves the slot, generation and stable id in place so a
    // rebuild can recreate the chain under the same handle.
    void DestroyRuntime(Ecs::Registry& InRegistry, ChainRuntime& InChain, bool InKeepIdentity = false) noexcept;
    void DestroyConnectionsOf(ChainRuntime& InChain) noexcept;
    void FillConnectionInfo(std::uint32_t InIndex, JointConnectionInfo* Out) const noexcept;

    PhysicsSystem* MPhysics = nullptr;
    // Identity of the bound system/world: Update checks it before dereferencing
    // MPhysics, so a destroyed or re-initialized PhysicsSystem is never used.
    std::uint64_t MPhysicsInstanceId = 0;
    std::uint32_t MPhysicsGeneration = 0;
    std::unique_ptr<Storage> MStorage;
    std::uint32_t MLiveChains = 0;
    StableIdAllocator<ChainId> MStableIds{};
    std::uint32_t MBreakCount = 0;
    std::uint32_t MBreakSequence = 0;
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
    FBY_FIELD(Fby::JointChainComponent, MotorSpeed, Float), FBY_FIELD(Fby::JointChainComponent, MaxMotorTorque, Float),
    FBY_FIELD(Fby::JointChainComponent, SectionCount, UInt32),
    FBY_FIELD(Fby::JointChainComponent, DefaultBreakForce, Float),
    FBY_FIELD(Fby::JointChainComponent, DefaultBreakTorque, Float),
    FBY_FIELD(Fby::JointChainComponent, BaseTension, Float),
    FBY_FIELD(Fby::JointChainComponent, MaxTension, Float),
    FBY_FIELD(Fby::JointChainComponent, TensionPerDistance, Float),
    FBY_FIELD(Fby::JointChainComponent, TensionDistance, Float),
    FBY_FIELD(Fby::JointChainComponent, StrengthEnabled, Bool),
    FBY_FIELD(Fby::JointChainComponent, DefaultLowerSpringForce, Float),
    FBY_FIELD(Fby::JointChainComponent, DefaultUpperSpringForce, Float),
    FBY_FIELD(Fby::JointChainComponent, DefaultDamageRate, Float),
    FBY_FIELD(Fby::JointChainComponent, DefaultDamageThreshold, Float),
    FBY_FIELD(Fby::JointChainComponent, BrokenMaskLow, UInt64),
    FBY_FIELD(Fby::JointChainComponent, BrokenMaskHigh, UInt64))
