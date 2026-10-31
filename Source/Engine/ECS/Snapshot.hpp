// FemboyEngine2D - deterministic ECS snapshots for rollback.
#pragma once

#include "Engine/Core/DeterministicRng.hpp"
#include "Engine/Core/Types.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/SpriteAnimation.hpp"
#include "Engine/Physics/Physics.hpp"
#include "Engine/Core/Mirror.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <type_traits>

namespace Fby::Ecs
{

inline constexpr std::uint32_t kSnapshotVersion = 4;
inline constexpr std::uint32_t kMaxSnapshotEntities = 16384;
inline constexpr std::uint32_t kMaxSnapshotComponents = 32;
inline constexpr std::uint32_t kMaxSnapshotComponentBytes = 2048;
inline constexpr char kSnapshotMagic[8] = {'F', 'B', 'S', 'N', 'A', 'P', '0', '3'};

enum SnapshotFlags : std::uint32_t
{
    SnapshotFull = 1u << 0,
    SnapshotDelta = 1u << 1,
};

enum SnapshotRecordFlags : std::uint16_t
{
    SnapshotEntityRemoved = 1u << 0,
    SnapshotEntityAdded = 1u << 1,
};

enum SnapshotComponentOp : std::uint8_t
{
    SnapshotComponentSet = 1,
    SnapshotComponentRemove = 2,
};

struct SnapshotHeader
{
    char magic[8] = {};
    std::uint32_t version = kSnapshotVersion;
    std::uint32_t flags = SnapshotFull;
    std::uint32_t entityCount = 0;
    std::uint32_t componentCount = 0;
    std::uint64_t tick = 0;
    std::uint64_t baseTick = 0;
    // Records only; does not include this header
    std::uint64_t payloadBytes = 0;
    // FNV-1a over the payload bytes
    std::uint64_t checksum = 0;
    // Delta only: checksum of the base snapshot
    std::uint64_t baseChecksum = 0;
    // SnapshotSchema::Hash(); rejects silent layout drift
    std::uint64_t schemaHash = 0;
    RngState rng{};
    // v3 world extension: chains, cloth, water and emitter clocks. The bytes
    std::uint32_t extensionBytes = 0;
    std::uint32_t extensionOffset = 0;
    std::uint64_t extensionChecksum = 0;
};

static_assert(sizeof(SnapshotHeader) == 104);
static_assert(std::endian::native == std::endian::little,
              "Snapshot wire format is little-endian; build on a little-endian target or add a swapper");

// One registered component type. The codec is deliberately function pointers:
struct SnapshotComponent
{
    std::string_view name;
    // FNV-1a(name), stable across runs and compilers
    std::uint64_t typeId = 0;
    // Mirror::LayoutHash, catches same-size field changes
    std::uint64_t layoutHash = 0;
    // Canonical byte count (0 for a marker)
    std::uint32_t size = 0;
    bool (*has)(const Registry&, Entity) = nullptr;
    bool (*capture)(const Registry&, Entity, std::span<std::byte>) = nullptr;
    bool (*restore)(Registry&, Entity, std::span<const std::byte>) = nullptr;
    void (*remove)(Registry&, Entity) = nullptr;
};

class SnapshotSchema;

namespace Detail
{
template <typename T> [[nodiscard]] constexpr std::uint64_t ComponentTypeId() noexcept
{
    return Reflect::Fnv1a(Reflect::Reflection<T>::kName);
}

template <typename T> [[nodiscard]] bool HasComponent(const Registry& InRegistry, Entity InEntity)
{
    return InRegistry.all_of<T>(InEntity);
}

template <typename T>
[[nodiscard]] bool CaptureComponent(const Registry& InRegistry, Entity InEntity, std::span<std::byte> InDestination)
{
    const T* ScopedComponent = InRegistry.try_get<T>(InEntity);
    if (ScopedComponent == nullptr)
    {
        return false;
    }
    if constexpr (std::is_empty_v<T>)
    {
        (void)InDestination;
        return true;
    }
    else
    {
        if (InDestination.size() < sizeof(T))
        {
            return false;
        }
        // Zero first: padding bytes inside the component must not leak into
        std::memset(InDestination.data(), 0, sizeof(T));
        const auto* ScopedSource = reinterpret_cast<const std::byte*>(ScopedComponent);
        for (const Reflect::FieldDesc& ScopedField : Reflect::Fields<T>())
        {
            std::memcpy(InDestination.data() + ScopedField.offset, ScopedSource + ScopedField.offset, ScopedField.size);
        }
        return true;
    }
}

template <typename T>
[[nodiscard]] bool RestoreComponent(Registry& InRegistry, Entity InEntity, std::span<const std::byte> InSource)
{
    if constexpr (std::is_empty_v<T>)
    {
        (void)InSource;
        InRegistry.emplace_or_replace<T>(InEntity);
        return true;
    }
    else
    {
        if (InSource.size() < sizeof(T))
        {
            return false;
        }
        T ScopedComponent{};
        auto* ScopedDestination = reinterpret_cast<std::byte*>(&ScopedComponent);
        for (const Reflect::FieldDesc& ScopedField : Reflect::Fields<T>())
        {
            std::memcpy(ScopedDestination + ScopedField.offset, InSource.data() + ScopedField.offset, ScopedField.size);
        }
        InRegistry.emplace_or_replace<T>(InEntity, ScopedComponent);
        return true;
    }
}

template <typename T> void RemoveComponent(Registry& InRegistry, Entity InEntity)
{
    InRegistry.remove<T>(InEntity);
}

// Physics codecs: runtime handles are excluded.
struct RigidBodySnapshot
{
    Vector2D position{};
    Vector2D linearVelocity{};
    float rotation = 0.0f;
    float angularVelocity = 0.0f;
    float gravityScale = 1.0f;
    // Damping is authored state: restoring without it changes every replay.
    float linearDamping = 0.0f;
    float angularDamping = 0.0f;
    std::uint32_t flags = 0;
    BodyType type = BodyType::Dynamic;
};

struct ColliderSnapshot
{
    Vector2D halfExtents{};
    float density = 1.0f;
    float friction = 0.3f;
    float restitution = 0.0f;
    std::uint32_t categoryBits = 1u;
    std::uint32_t maskBits = 0xFFFFFFFFu;
    bool sensor = false;
    bool contactEvents = true;
};

[[nodiscard]] bool HasRigidBody(const Registry& InRegistry, Entity InEntity);
[[nodiscard]] bool CaptureRigidBody(const Registry& InRegistry, Entity InEntity, std::span<std::byte> InDestination);
[[nodiscard]] bool RestoreRigidBody(Registry& InRegistry, Entity InEntity, std::span<const std::byte> InSource);
void RemoveRigidBody(Registry& InRegistry, Entity InEntity);

[[nodiscard]] bool HasCollider(const Registry& InRegistry, Entity InEntity);
[[nodiscard]] bool CaptureCollider(const Registry& InRegistry, Entity InEntity, std::span<std::byte> InDestination);
[[nodiscard]] bool RestoreCollider(Registry& InRegistry, Entity InEntity, std::span<const std::byte> InSource);
void RemoveCollider(Registry& InRegistry, Entity InEntity);

void AddPhysicsCodecs(SnapshotSchema& InSchema);
}

class SnapshotSchema
{
public:
    [[nodiscard]] static SnapshotSchema BuiltIns();

    template <typename T> [[nodiscard]] bool Add()
    {
        static_assert(Reflect::IsRegistered<T>(), "SnapshotSchema::Add<T> requires FBY_REFLECT_COMPONENT");
        static_assert(std::is_trivially_copyable_v<T>, "snapshot components must be trivially copyable");
        static_assert(sizeof(T) <= kMaxSnapshotComponentBytes, "component exceeds kMaxSnapshotComponentBytes");

        SnapshotComponent ScopedCodec;
        ScopedCodec.name = Reflect::Reflection<T>::kName;
        ScopedCodec.typeId = Detail::ComponentTypeId<T>();
        ScopedCodec.layoutHash = Reflect::LayoutHash<T>();
        ScopedCodec.size = std::is_empty_v<T> ? 0u : static_cast<std::uint32_t>(sizeof(T));
        ScopedCodec.has = &Detail::HasComponent<T>;
        if constexpr (std::is_empty_v<T>)
        {
            ScopedCodec.capture = +[](const Registry&, Entity, std::span<std::byte>) { return true; };
        }
        else
        {
            ScopedCodec.capture = &Detail::CaptureComponent<T>;
        }
        ScopedCodec.restore = &Detail::RestoreComponent<T>;
        ScopedCodec.remove = &Detail::RemoveComponent<T>;
        return AddCodec(ScopedCodec);
    }

    [[nodiscard]] bool AddCodec(const SnapshotComponent& InCodec);
    [[nodiscard]] const SnapshotComponent* Find(std::uint64_t InTypeId) const noexcept;
    [[nodiscard]] const SnapshotComponent* Find(std::string_view InName) const noexcept;
    [[nodiscard]] std::span<const SnapshotComponent> All() const noexcept
    {
        return {MComponents.data(), MCount};
    }
    [[nodiscard]] std::size_t Size() const noexcept
    {
        return MCount;
    }
    [[nodiscard]] std::uint64_t Hash() const noexcept;

private:
    std::array<SnapshotComponent, kMaxSnapshotComponents> MComponents{};
    std::size_t MCount = 0;
};

// A parsed, non-owning view for tools/netcode inspection.
struct SnapshotView
{
    SnapshotHeader header{};
    std::span<const std::byte> payload;
    std::span<const std::byte> extension;

    [[nodiscard]] static SnapshotView Open(std::span<const std::byte> InBytes, std::string_view* InError = nullptr);
};

// Optional v3 world bytes. Capture copies them verbatim; Restore copies them
struct SnapshotExtras
{
    std::span<const std::byte> extension{};
};

struct SnapshotRestoreExtras
{
    std::span<std::byte> extension{};
};

// Upper bound for the current registry: header + every live entity + every
[[nodiscard]] std::size_t SnapshotSize(const Registry& InRegistry, const SnapshotSchema& InSchema,
                                       std::size_t InExtensionBytes = 0) noexcept;

// Full snapshot. `rng` is the simulation RNG at the start of `tick`; its 128-bit
[[nodiscard]] bool CaptureSnapshot(const Registry& InRegistry, const SnapshotSchema& InSchema,
                                   std::span<std::byte> InDestination, std::uint64_t InTick,
                                   const DeterministicRng& InRng, std::size_t* OutBytes = nullptr,
                                   SnapshotExtras InExtras = {});

// Delta against a previously captured full snapshot. Only changed components,
[[nodiscard]] bool CaptureDelta(const Registry& InRegistry, const SnapshotSchema& InSchema,
                                std::span<const std::byte> InBaseSnapshot, std::span<std::byte> InDestination,
                                std::uint64_t InTick, const DeterministicRng& InRng, std::size_t* OutBytes = nullptr,
                                SnapshotExtras InExtras = {});

// Full restore clears the registry and recreates every entity with its exact
[[nodiscard]] bool RestoreSnapshot(Registry& InRegistry, const SnapshotSchema& InSchema,
                                   std::span<const std::byte> InSnapshot, DeterministicRng* OutRng = nullptr,
                                   SnapshotRestoreExtras InExtras = {});

// Read-only acceptance check: header, schema and every record are validated, but
// the registry and the RNG are untouched. Callers that must destroy native state
// before a restore (the rollback session, PIE) call this first.
[[nodiscard]] bool ValidateSnapshot(std::span<const std::byte> InSnapshot, const SnapshotSchema& InSchema,
                                    std::string_view* InError = nullptr) noexcept;

// Restores `baseSnapshot` and then applies `delta`. The delta checksum must
[[nodiscard]] bool RestoreDelta(Registry& InRegistry, const SnapshotSchema& InSchema,
                                std::span<const std::byte> InBaseSnapshot, std::span<const std::byte> InDelta,
                                DeterministicRng* OutRng = nullptr, SnapshotRestoreExtras InExtras = {});

// Applies a delta to the current registry without restoring the baseline. Entity
// ids stored in the delta are expected to match the live registry, so handles
// survive incremental updates.
[[nodiscard]] bool ApplyDelta(Registry& InRegistry, const SnapshotSchema& InSchema,
                              std::span<const std::byte> InDelta) noexcept;

// What a delta-only client remembers about the snapshot it currently holds.
struct SnapshotBaseline
{
    std::uint64_t Tick = 0;
    std::uint64_t PayloadChecksum = 0;
    bool Valid = false;
};

// Records the tick and payload checksum of a full snapshot. Returns false for a
// delta or a malformed header, so a client cannot adopt a delta as its base.
[[nodiscard]] bool RememberBaseline(SnapshotBaseline& OutBaseline,
                                    std::span<const std::byte> InFullSnapshot) noexcept;

// ApplyDelta plus the base check: the delta must name exactly the remembered
// baseline tick and payload checksum before the registry is touched.
[[nodiscard]] bool ApplyDeltaChecked(Registry& InRegistry, const SnapshotSchema& InSchema,
                                     std::span<const std::byte> InDelta,
                                     const SnapshotBaseline& InBaseline) noexcept;

// Stable id of a registered component name. Exposed so a command log can write
[[nodiscard]] constexpr std::uint64_t SnapshotTypeId(std::string_view InName) noexcept
{
    return Reflect::Fnv1a(InName);
}
}
