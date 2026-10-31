#include "Engine/ECS/Snapshot.hpp"

#include "Engine/ECS/SkeletonAssetRef.hpp"
#include "Engine/ECS/LightSource.hpp"
#include "Engine/ECS/Presentation.hpp"
#include "Engine/ECS/RegistryOps.hpp"
#include "Engine/Physics/Particles.hpp"
#include "Engine/Physics/Cloth.hpp"
#include "Engine/Physics/Fluid.hpp"
#include "Engine/Physics/PhysicsJoints.hpp"
#include "Engine/UI/Hypertext.hpp"
#include "Engine/UI/PinkUI.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <string_view>
#include <vector>

namespace Fby::Ecs
{
class SnapshotSchema;

namespace
{
constexpr std::uint32_t kMagicSize = sizeof(kSnapshotMagic);

struct RecordHeader
{
    Entity entity = entt::null;
    std::uint16_t flags = 0;
    std::uint16_t componentCount = 0;
    std::uint32_t payloadBytes = 0;
};

struct ComponentHeader
{
    std::uint64_t typeId = 0;
    std::uint32_t size = 0;
    std::uint8_t op = SnapshotComponentSet;
    std::uint8_t padding[3] = {};
};

static_assert(sizeof(RecordHeader) == 12);
static_assert(sizeof(ComponentHeader) == 16);

[[nodiscard]] std::uint64_t HashBytes(std::span<const std::byte> InBytes,
                                      std::uint64_t InHash = 1469598103934665603ull) noexcept
{
    for (const std::byte ScopedByte : InBytes)
    {
        InHash ^= static_cast<std::uint64_t>(std::to_integer<unsigned char>(ScopedByte));
        InHash *= 1099511628211ull;
    }
    return InHash;
}

class Writer
{
public:
    Writer(std::byte* InData, std::size_t InCapacity) noexcept : MData(InData), MCapacity(InCapacity)
    {
    }

    template <typename T> [[nodiscard]] bool Write(const T& InValue) noexcept
    {
        static_assert(std::is_trivially_copyable_v<T>);
        if (MOffset + sizeof(T) > MCapacity)
        {
            return false;
        }
        std::memcpy(MData + MOffset, &InValue, sizeof(T));
        MOffset += sizeof(T);
        return true;
    }

    [[nodiscard]] bool WriteBytes(std::span<const std::byte> InBytes) noexcept
    {
        if (MOffset + InBytes.size() > MCapacity)
        {
            return false;
        }
        if (!InBytes.empty())
        {
            std::memcpy(MData + MOffset, InBytes.data(), InBytes.size());
        }
        MOffset += InBytes.size();
        return true;
    }

    [[nodiscard]] std::size_t Offset() const noexcept
    {
        return MOffset;
    }
    [[nodiscard]] std::size_t Remaining() const noexcept
    {
        return MCapacity - MOffset;
    }
    [[nodiscard]] std::byte* Data() const noexcept
    {
        return MData;
    }

private:
    std::byte* MData = nullptr;
    std::size_t MCapacity = 0;
    std::size_t MOffset = 0;
};

class Reader
{
public:
    Reader(const std::byte* InData, std::size_t InSize) noexcept : MData(InData), MSize(InSize)
    {
    }

    template <typename T> [[nodiscard]] bool Read(T& InValue) noexcept
    {
        static_assert(std::is_trivially_copyable_v<T>);
        if (MOffset + sizeof(T) > MSize)
        {
            return false;
        }
        std::memcpy(&InValue, MData + MOffset, sizeof(T));
        MOffset += sizeof(T);
        return true;
    }

    [[nodiscard]] bool ReadBytes(std::size_t InCount, std::span<const std::byte>& Out) noexcept
    {
        if (MOffset + InCount > MSize)
        {
            return false;
        }
        Out = {MData + MOffset, InCount};
        MOffset += InCount;
        return true;
    }

    [[nodiscard]] std::size_t Remaining() const noexcept
    {
        return MSize - MOffset;
    }
    [[nodiscard]] std::size_t Offset() const noexcept
    {
        return MOffset;
    }

private:
    const std::byte* MData = nullptr;
    std::size_t MSize = 0;
    std::size_t MOffset = 0;
};

struct BaseEntity
{
    Entity entity = entt::null;
    std::uint32_t firstComponent = 0;
    std::uint16_t componentCount = 0;
};

struct BaseComponent
{
    std::uint64_t typeId = 0;
    std::uint32_t size = 0;
    const std::byte* data = nullptr;
};

struct BaseView
{
    SnapshotHeader header{};
    std::vector<BaseEntity> entities;
    std::vector<BaseComponent> components;
};

[[nodiscard]] bool ParseBase(std::span<const std::byte> InBytes, bool InRequireFull, BaseView& Out,
                             std::string_view* InError) noexcept
{
    const SnapshotView ScopedView = SnapshotView::Open(InBytes, InError);
    if (ScopedView.header.version == 0)
    {
        return false;
    }
    if (InRequireFull && (ScopedView.header.flags & SnapshotFull) == 0)
    {
        if (InError != nullptr)
        {
            *InError = "base snapshot is not a full snapshot";
        }
        return false;
    }

    Out.header = ScopedView.header;
    Out.entities.clear();
    Out.components.clear();
    Out.entities.reserve(ScopedView.header.entityCount);
    // Open() already bounds the count by the payload; reserve from that bound so
    // a corrupt header can never throw inside this noexcept parser.
    Out.components.reserve(std::min<std::size_t>(ScopedView.header.componentCount,
                                                 ScopedView.payload.size() / sizeof(ComponentHeader)));

    Reader ScopedReader(ScopedView.payload.data(), ScopedView.payload.size());
    for (std::uint32_t ScopedRecordIndex = 0; ScopedRecordIndex < ScopedView.header.entityCount; ++ScopedRecordIndex)
    {
        RecordHeader ScopedRecord;
        if (!ScopedReader.Read(ScopedRecord))
        {
            if (InError != nullptr)
            {
                *InError = "truncated entity record";
            }
            return false;
        }
        BaseEntity ScopedEntity;
        ScopedEntity.entity = ScopedRecord.entity;
        ScopedEntity.firstComponent = static_cast<std::uint32_t>(Out.components.size());
        ScopedEntity.componentCount = ScopedRecord.componentCount;
        for (std::uint16_t ScopedComponentIndex = 0; ScopedComponentIndex < ScopedRecord.componentCount;
             ++ScopedComponentIndex)
        {
            ComponentHeader ScopedComponent;
            if (!ScopedReader.Read(ScopedComponent))
            {
                if (InError != nullptr)
                {
                    *InError = "truncated component header";
                }
                return false;
            }
            if (ScopedComponent.op != SnapshotComponentSet)
            {
                if (InError != nullptr)
                {
                    *InError = "a full snapshot cannot contain component removals";
                }
                return false;
            }
            std::span<const std::byte> ScopedData;
            if (!ScopedReader.ReadBytes(ScopedComponent.size, ScopedData))
            {
                if (InError != nullptr)
                {
                    *InError = "truncated component data";
                }
                return false;
            }
            Out.components.push_back(BaseComponent{ScopedComponent.typeId, ScopedComponent.size, ScopedData.data()});
        }
        Out.entities.push_back(ScopedEntity);
    }

    if (ScopedReader.Remaining() != 0)
    {
        if (InError != nullptr)
        {
            *InError = "trailing bytes in snapshot payload";
        }
        return false;
    }
    return true;
}

// Read-only structural pass over an entity payload. Restore uses it before it
// destroys anything, so a semantically invalid (but checksum-valid) snapshot
// cannot leave a half-built registry.
[[nodiscard]] bool ValidatePayload(std::span<const std::byte> InPayload, const SnapshotSchema& InSchema,
                                   std::uint32_t InEntityCount, bool InAllowRemoved,
                                   std::string_view* InError) noexcept
{
    Reader ScopedReader(InPayload.data(), InPayload.size());
    for (std::uint32_t ScopedRecordIndex = 0; ScopedRecordIndex < InEntityCount; ++ScopedRecordIndex)
    {
        RecordHeader ScopedRecord;
        if (!ScopedReader.Read(ScopedRecord) || ScopedRecord.entity == entt::null)
        {
            if (InError != nullptr)
            {
                *InError = "invalid entity record";
            }
            return false;
        }
        if ((ScopedRecord.flags & SnapshotEntityRemoved) != 0)
        {
            if (!InAllowRemoved || ScopedRecord.componentCount != 0 || ScopedRecord.payloadBytes != 0)
            {
                if (InError != nullptr)
                {
                    *InError = "removed entity record carries components";
                }
                return false;
            }
            continue;
        }
        for (std::uint16_t ScopedComponentIndex = 0; ScopedComponentIndex < ScopedRecord.componentCount;
             ++ScopedComponentIndex)
        {
            ComponentHeader ScopedComponent;
            if (!ScopedReader.Read(ScopedComponent))
            {
                if (InError != nullptr)
                {
                    *InError = "truncated component header";
                }
                return false;
            }
            const SnapshotComponent* ScopedCodec = InSchema.Find(ScopedComponent.typeId);
            if (ScopedCodec == nullptr)
            {
                if (InError != nullptr)
                {
                    *InError = "snapshot names an unknown component";
                }
                return false;
            }
            std::span<const std::byte> ScopedData;
            if (ScopedComponent.op == SnapshotComponentRemove)
            {
                if (ScopedComponent.size != 0 || !ScopedReader.ReadBytes(0, ScopedData))
                {
                    if (InError != nullptr)
                    {
                        *InError = "invalid component removal";
                    }
                    return false;
                }
            }
            else if (ScopedComponent.op == SnapshotComponentSet)
            {
                if (ScopedComponent.size != ScopedCodec->size ||
                    !ScopedReader.ReadBytes(ScopedComponent.size, ScopedData))
                {
                    if (InError != nullptr)
                    {
                        *InError = "component size does not match the schema";
                    }
                    return false;
                }
            }
            else
            {
                if (InError != nullptr)
                {
                    *InError = "unknown component operation";
                }
                return false;
            }
        }
    }
    if (ScopedReader.Remaining() != 0)
    {
        if (InError != nullptr)
        {
            *InError = "trailing bytes in snapshot payload";
        }
        return false;
    }
    return true;
}

[[nodiscard]] std::uint16_t CountPresent(const Registry& InRegistry, const SnapshotSchema& InSchema,
                                         Entity InEntity) noexcept
{
    std::uint16_t ScopedCount = 0;
    for (const SnapshotComponent& ScopedCodec : InSchema.All())
    {
        if (ScopedCodec.has(InRegistry, InEntity))
        {
            ++ScopedCount;
        }
    }
    return ScopedCount;
}

void CollectSortedComponents(const SnapshotSchema& InSchema, std::span<const SnapshotComponent*> InDestination,
                             std::uint16_t* OutCount) noexcept
{
    std::uint16_t ScopedCount = 0;
    for (const SnapshotComponent& ScopedCodec : InSchema.All())
    {
        InDestination[ScopedCount++] = &ScopedCodec;
    }
    std::sort(InDestination.begin(), InDestination.begin() + ScopedCount,
              [](const SnapshotComponent* InLeft, const SnapshotComponent* InRight)
              { return InLeft->typeId < InRight->typeId; });
    *OutCount = ScopedCount;
}

[[nodiscard]] std::uint32_t CollectEntities(const Registry& InRegistry, std::span<Entity> InDestination,
                                            bool* OutOverflow = nullptr) noexcept
{
    std::uint32_t ScopedCount = 0;
    bool ScopedOverflow = false;
    const auto* ScopedStorage = InRegistry.storage<Entity>();
    if (ScopedStorage == nullptr)
    {
        if (OutOverflow != nullptr)
        {
            *OutOverflow = false;
        }
        return 0;
    }
    for (const Entity ScopedEntity : *ScopedStorage)
    {
        if (ScopedCount >= InDestination.size())
        {
            ScopedOverflow = true;
            break;
        }
        InDestination[ScopedCount++] = ScopedEntity;
    }
    std::sort(InDestination.begin(), InDestination.begin() + ScopedCount,
              [](Entity InLeft, Entity InRight) { return entt::to_integral(InLeft) < entt::to_integral(InRight); });
    if (OutOverflow != nullptr)
    {
        *OutOverflow = ScopedOverflow;
    }
    return ScopedCount;
}

[[nodiscard]] const BaseComponent* FindBaseComponent(const BaseView& InBase, const BaseEntity& InEntity,
                                                     std::uint64_t InTypeId) noexcept
{
    const BaseComponent* ScopedBegin = InBase.components.data() + InEntity.firstComponent;
    for (std::uint16_t ScopedI = 0; ScopedI < InEntity.componentCount; ++ScopedI)
    {
        if (ScopedBegin[ScopedI].typeId == InTypeId)
        {
            return &ScopedBegin[ScopedI];
        }
    }
    return nullptr;
}

struct DeltaChange
{
    const SnapshotComponent* codec = nullptr;
    std::uint8_t op = SnapshotComponentSet;
};

struct ChangeScratch
{
    std::array<DeltaChange, kMaxSnapshotComponents> changes{};
    std::uint16_t count = 0;
    std::uint32_t payloadBytes = 0;
};

[[nodiscard]] bool ComputeEntityChanges(const Registry& InRegistry, Entity InEntity, std::uint16_t InComponentCount,
                                        std::span<const SnapshotComponent*> InComponents, const BaseView& InBase,
                                        const BaseEntity* InBaseEntity, ChangeScratch& InScratch)
{
    InScratch.count = 0;
    InScratch.payloadBytes = 0;
    static thread_local std::array<std::byte, kMaxSnapshotComponentBytes> ScopedCanonical{};

    for (std::uint16_t ScopedI = 0; ScopedI < InComponentCount; ++ScopedI)
    {
        const SnapshotComponent& ScopedCodec = *InComponents[ScopedI];
        const bool ScopedHasNow = ScopedCodec.has(InRegistry, InEntity);
        const BaseComponent* ScopedPrevious =
            InBaseEntity != nullptr ? FindBaseComponent(InBase, *InBaseEntity, ScopedCodec.typeId) : nullptr;

        std::uint8_t ScopedOp = SnapshotComponentSet;
        bool ScopedChanged = false;
        if (ScopedHasNow && ScopedPrevious == nullptr)
        {
            ScopedChanged = true;
        }
        else if (!ScopedHasNow && ScopedPrevious != nullptr)
        {
            ScopedChanged = true;
            ScopedOp = SnapshotComponentRemove;
        }
        else if (ScopedHasNow && ScopedPrevious != nullptr)
        {
            if (ScopedPrevious->size != ScopedCodec.size)
            {
                ScopedChanged = true;
            }
            else if (ScopedCodec.size > 0)
            {
                if (!ScopedCodec.capture(InRegistry, InEntity,
                                         std::span<std::byte>(ScopedCanonical.data(), ScopedCodec.size)))
                {
                    return false;
                }
                // Bit-exact comparison on purpose: -0.0 differs from 0.0 and
                // identical NaN payloads compare equal. Peers must therefore
                // keep the same arithmetic order; the checksum locks the same
                // policy.
                ScopedChanged = std::memcmp(ScopedCanonical.data(), ScopedPrevious->data, ScopedCodec.size) != 0;
            }
        }

        if (!ScopedChanged)
        {
            continue;
        }
        if (InScratch.count >= InScratch.changes.size())
        {
            return false;
        }
        InScratch.changes[InScratch.count++] = DeltaChange{&ScopedCodec, ScopedOp};
        InScratch.payloadBytes += sizeof(ComponentHeader) + (ScopedOp == SnapshotComponentSet ? ScopedCodec.size : 0u);
    }
    return true;
}

void WriteHeader(std::span<std::byte> InDestination, SnapshotHeader& InHeader,
                 std::span<const std::byte> InExtension) noexcept
{
    InHeader.extensionBytes = static_cast<std::uint32_t>(InExtension.size());
    InHeader.extensionOffset = InHeader.payloadBytes;
    InHeader.checksum = HashBytes(InDestination.subspan(sizeof(SnapshotHeader), InHeader.payloadBytes));
    InHeader.extensionChecksum = HashBytes(InExtension);
    std::memcpy(InDestination.data(), &InHeader, sizeof(InHeader));
}
}

// Physics codecs
namespace Detail
{
bool HasRigidBody(const Registry& InRegistry, Entity InEntity)
{
    return InRegistry.all_of<RigidBody2D>(InEntity);
}

bool CaptureRigidBody(const Registry& InRegistry, Entity InEntity, std::span<std::byte> InDestination)
{
    if (InDestination.size() < sizeof(RigidBodySnapshot))
    {
        return false;
    }
    const auto* ScopedBody = InRegistry.try_get<RigidBody2D>(InEntity);
    if (ScopedBody == nullptr)
    {
        return false;
    }
    RigidBodySnapshot ScopedSnapshot{};
    ScopedSnapshot.position = ScopedBody->Position;
    ScopedSnapshot.linearVelocity = ScopedBody->LinearVelocity;
    ScopedSnapshot.rotation = ScopedBody->Rotation;
    ScopedSnapshot.angularVelocity = ScopedBody->AngularVelocity;
    ScopedSnapshot.gravityScale = ScopedBody->GravityScale;
    ScopedSnapshot.linearDamping = ScopedBody->LinearDamping;
    ScopedSnapshot.angularDamping = ScopedBody->AngularDamping;
    ScopedSnapshot.flags = ScopedBody->Flags;
    ScopedSnapshot.type = ScopedBody->Type;
    std::memcpy(InDestination.data(), &ScopedSnapshot, sizeof(ScopedSnapshot));
    return true;
}

bool RestoreRigidBody(Registry& InRegistry, Entity InEntity, std::span<const std::byte> InSource)
{
    if (InSource.size() != sizeof(RigidBodySnapshot))
    {
        return false;
    }
    RigidBodySnapshot ScopedSnapshot{};
    std::memcpy(&ScopedSnapshot, InSource.data(), sizeof(ScopedSnapshot));
    auto* ScopedBody = InRegistry.try_get<RigidBody2D>(InEntity);
    if (ScopedBody == nullptr)
    {
        ScopedBody = &InRegistry.emplace<RigidBody2D>(InEntity);
    }
    ScopedBody->Position = ScopedSnapshot.position;
    ScopedBody->PreviousPosition = ScopedSnapshot.position;
    ScopedBody->LinearVelocity = ScopedSnapshot.linearVelocity;
    ScopedBody->Rotation = ScopedSnapshot.rotation;
    ScopedBody->PreviousRotation = ScopedSnapshot.rotation;
    ScopedBody->AngularVelocity = ScopedSnapshot.angularVelocity;
    ScopedBody->GravityScale = ScopedSnapshot.gravityScale;
    ScopedBody->LinearDamping = ScopedSnapshot.linearDamping;
    ScopedBody->AngularDamping = ScopedSnapshot.angularDamping;
    ScopedBody->Flags = ScopedSnapshot.flags | RigidBodyTeleport;
    ScopedBody->Type = ScopedSnapshot.type;
    return true;
}

void RemoveRigidBody(Registry& InRegistry, Entity InEntity)
{
    InRegistry.remove<RigidBody2D>(InEntity);
}

bool HasCollider(const Registry& InRegistry, Entity InEntity)
{
    return InRegistry.all_of<Collider2D>(InEntity);
}

bool CaptureCollider(const Registry& InRegistry, Entity InEntity, std::span<std::byte> InDestination)
{
    if (InDestination.size() < sizeof(ColliderSnapshot))
    {
        return false;
    }
    const auto* ScopedCollider = InRegistry.try_get<Collider2D>(InEntity);
    if (ScopedCollider == nullptr)
    {
        return false;
    }
    ColliderSnapshot ScopedSnapshot{};
    ScopedSnapshot.halfExtents = ScopedCollider->HalfExtents;
    ScopedSnapshot.density = ScopedCollider->Density;
    ScopedSnapshot.friction = ScopedCollider->Friction;
    ScopedSnapshot.restitution = ScopedCollider->Restitution;
    ScopedSnapshot.categoryBits = ScopedCollider->CategoryBits;
    ScopedSnapshot.maskBits = ScopedCollider->MaskBits;
    ScopedSnapshot.sensor = ScopedCollider->Sensor;
    ScopedSnapshot.contactEvents = ScopedCollider->ContactEvents;
    std::memcpy(InDestination.data(), &ScopedSnapshot, sizeof(ScopedSnapshot));
    return true;
}

bool RestoreCollider(Registry& InRegistry, Entity InEntity, std::span<const std::byte> InSource)
{
    if (InSource.size() != sizeof(ColliderSnapshot))
    {
        return false;
    }
    ColliderSnapshot ScopedSnapshot{};
    std::memcpy(&ScopedSnapshot, InSource.data(), sizeof(ScopedSnapshot));
    auto* ScopedCollider = InRegistry.try_get<Collider2D>(InEntity);
    if (ScopedCollider == nullptr)
    {
        ScopedCollider = &InRegistry.emplace<Collider2D>(InEntity);
    }
    ScopedCollider->HalfExtents = ScopedSnapshot.halfExtents;
    ScopedCollider->Density = ScopedSnapshot.density;
    ScopedCollider->Friction = ScopedSnapshot.friction;
    ScopedCollider->Restitution = ScopedSnapshot.restitution;
    ScopedCollider->CategoryBits = ScopedSnapshot.categoryBits;
    ScopedCollider->MaskBits = ScopedSnapshot.maskBits;
    ScopedCollider->Sensor = ScopedSnapshot.sensor;
    ScopedCollider->ContactEvents = ScopedSnapshot.contactEvents;
    return true;
}

void RemoveCollider(Registry& InRegistry, Entity InEntity)
{
    InRegistry.remove<Collider2D>(InEntity);
}

void AddPhysicsCodecs(SnapshotSchema& InSchema)
{
    const auto ScopedAddRaw = [&InSchema](std::string_view InName, std::uint32_t InSize,
                                          bool (*InHas)(const Registry&, Entity),
                                          bool (*InCapture)(const Registry&, Entity, std::span<std::byte>),
                                          bool (*InRestore)(Registry&, Entity, std::span<const std::byte>),
                                          void (*InRemove)(Registry&, Entity), std::uint64_t InLayoutTag)
    {
        SnapshotComponent ScopedCodec;
        ScopedCodec.name = InName;
        ScopedCodec.typeId = SnapshotTypeId(InName);
        ScopedCodec.layoutHash = InLayoutTag;
        ScopedCodec.size = InSize;
        ScopedCodec.has = InHas;
        ScopedCodec.capture = InCapture;
        ScopedCodec.restore = InRestore;
        ScopedCodec.remove = InRemove;
        (void)InSchema.AddCodec(ScopedCodec);
    };

    SnapshotComponent ScopedBody;
    ScopedBody.name = "RigidBody2D";
    ScopedBody.typeId = SnapshotTypeId(ScopedBody.name);
    ScopedBody.layoutHash = SnapshotTypeId("RigidBody2D/v3");
    ScopedBody.size = sizeof(RigidBodySnapshot);
    ScopedBody.has = &HasRigidBody;
    ScopedBody.capture = &CaptureRigidBody;
    ScopedBody.restore = &RestoreRigidBody;
    ScopedBody.remove = &RemoveRigidBody;
    (void)InSchema.AddCodec(ScopedBody);

    SnapshotComponent ScopedCollider;
    ScopedCollider.name = "Collider2D";
    ScopedCollider.typeId = SnapshotTypeId(ScopedCollider.name);
    ScopedCollider.layoutHash = SnapshotTypeId("Collider2D/v2");
    ScopedCollider.size = sizeof(ColliderSnapshot);
    ScopedCollider.has = &HasCollider;
    ScopedCollider.capture = &CaptureCollider;
    ScopedCollider.restore = &RestoreCollider;
    ScopedCollider.remove = &RemoveCollider;
    (void)InSchema.AddCodec(ScopedCollider);

    // The authoring chain record owns its path array and therefore cannot use
    ScopedAddRaw(
        "JointChainComponent", static_cast<std::uint32_t>(sizeof(JointChainComponent)),
        +[](const Registry& InRegistry, Entity InEntity) { return InRegistry.all_of<JointChainComponent>(InEntity); },
        +[](const Registry& InRegistry, Entity InEntity, std::span<std::byte> InDestination)
        {
            const auto* ScopedComponent = InRegistry.try_get<JointChainComponent>(InEntity);
            if (ScopedComponent == nullptr || InDestination.size() < sizeof(JointChainComponent))
            {
                return false;
            }
            JointChainComponent ScopedCopy{};
            ScopedCopy.ChainID = ScopedComponent->ChainID;
            ScopedCopy.Kind = ScopedComponent->Kind;
            ScopedCopy.Flags = ScopedComponent->Flags;
            ScopedCopy.AnchorA = ScopedComponent->AnchorA;
            ScopedCopy.AnchorB = ScopedComponent->AnchorB;
            ScopedCopy.Start = ScopedComponent->Start;
            ScopedCopy.End = ScopedComponent->End;
            ScopedCopy.SegmentCount = ScopedComponent->SegmentCount;
            ScopedCopy.PathCount = ScopedComponent->PathCount;
            ScopedCopy.Path = ScopedComponent->Path;
            ScopedCopy.Radius = ScopedComponent->Radius;
            ScopedCopy.Density = ScopedComponent->Density;
            ScopedCopy.Friction = ScopedComponent->Friction;
            ScopedCopy.Restitution = ScopedComponent->Restitution;
            ScopedCopy.Hertz = ScopedComponent->Hertz;
            ScopedCopy.DampingRatio = ScopedComponent->DampingRatio;
            ScopedCopy.LowerAngle = ScopedComponent->LowerAngle;
            ScopedCopy.UpperAngle = ScopedComponent->UpperAngle;
            ScopedCopy.MotorSpeed = ScopedComponent->MotorSpeed;
            ScopedCopy.MaxMotorTorque = ScopedComponent->MaxMotorTorque;
            ScopedCopy.CategoryBits = ScopedComponent->CategoryBits;
            ScopedCopy.MaskBits = ScopedComponent->MaskBits;
            ScopedCopy.LinkLayer = ScopedComponent->LinkLayer;
            ScopedCopy.EnableLimit = ScopedComponent->EnableLimit;
            ScopedCopy.EnableMotor = ScopedComponent->EnableMotor;
            // A value-initialized copy keeps padding zero instead of leaking
            // indeterminate bytes into the payload and the delta checksum.
            std::memcpy(InDestination.data(), &ScopedCopy, sizeof(ScopedCopy));
            return true;
        },
        +[](Registry& InRegistry, Entity InEntity, std::span<const std::byte> InSource)
        {
            if (InSource.size() != sizeof(JointChainComponent))
            {
                return false;
            }
            JointChainComponent ScopedComponent{};
            std::memcpy(&ScopedComponent, InSource.data(), sizeof(ScopedComponent));
            InRegistry.emplace_or_replace<JointChainComponent>(InEntity, ScopedComponent);
            return true;
        },
        +[](Registry& InRegistry, Entity InEntity) { InRegistry.remove<JointChainComponent>(InEntity); },
        SnapshotTypeId("JointChainComponent/v3"));
}
}

// Schema
bool SnapshotSchema::AddCodec(const SnapshotComponent& InCodec)
{
    if (InCodec.name.empty() || InCodec.has == nullptr || InCodec.capture == nullptr || InCodec.restore == nullptr ||
        InCodec.remove == nullptr || InCodec.size > kMaxSnapshotComponentBytes || MCount >= MComponents.size())
    {
        return false;
    }
    if (Find(InCodec.typeId) != nullptr)
    {
        return false;
    }
    MComponents[MCount++] = InCodec;
    return true;
}

const SnapshotComponent* SnapshotSchema::Find(std::uint64_t InTypeId) const noexcept
{
    for (std::size_t ScopedI = 0; ScopedI < MCount; ++ScopedI)
    {
        if (MComponents[ScopedI].typeId == InTypeId)
        {
            return &MComponents[ScopedI];
        }
    }
    return nullptr;
}

const SnapshotComponent* SnapshotSchema::Find(std::string_view InName) const noexcept
{
    for (std::size_t ScopedI = 0; ScopedI < MCount; ++ScopedI)
    {
        if (MComponents[ScopedI].name == InName)
        {
            return &MComponents[ScopedI];
        }
    }
    return nullptr;
}

std::uint64_t SnapshotSchema::Hash() const noexcept
{
    std::uint64_t ScopedHash = 1469598103934665603ull;
    for (std::size_t ScopedI = 0; ScopedI < MCount; ++ScopedI)
    {
        const SnapshotComponent& ScopedCodec = MComponents[ScopedI];
        ScopedHash = HashBytes(std::span<const std::byte>(reinterpret_cast<const std::byte*>(&ScopedCodec.typeId),
                                                          sizeof(ScopedCodec.typeId)),
                               ScopedHash);
        ScopedHash = HashBytes(std::span<const std::byte>(reinterpret_cast<const std::byte*>(&ScopedCodec.layoutHash),
                                                          sizeof(ScopedCodec.layoutHash)),
                               ScopedHash);
        ScopedHash = HashBytes(
            std::span<const std::byte>(reinterpret_cast<const std::byte*>(&ScopedCodec.size), sizeof(ScopedCodec.size)),
            ScopedHash);
    }
    return ScopedHash;
}

SnapshotSchema SnapshotSchema::BuiltIns()
{
    SnapshotSchema ScopedSchema;
    (void)ScopedSchema.Add<Transform2D>();
    (void)ScopedSchema.Add<Velocity>();
    (void)ScopedSchema.Add<Sprite2D>();
    (void)ScopedSchema.Add<DrawOrder>();
    (void)ScopedSchema.Add<Camera2D>();
    (void)ScopedSchema.Add<Lifetime>();
    (void)ScopedSchema.Add<GameplayState>();
    (void)ScopedSchema.Add<BigIntComponent>();
    (void)ScopedSchema.Add<DecimalComponent>();
    (void)ScopedSchema.Add<TextComponent>();
    (void)ScopedSchema.Add<TextEffectComponent>();
    (void)ScopedSchema.Add<TextFormatComponent>();
    (void)ScopedSchema.Add<TextConditionComponent>();
    (void)ScopedSchema.Add<Hidden>();
    (void)ScopedSchema.Add<SpriteAnimationComponent>();
    (void)ScopedSchema.Add<SpriteSheetComponent>();
    (void)ScopedSchema.Add<SkeletonAssetRef>();
    // Authoring components. Their runtime handles are stable ids or are
    (void)ScopedSchema.Add<ClothComponent>();
    (void)ScopedSchema.Add<FluidInteractorComponent>();
    (void)ScopedSchema.Add<ParticleEmitterComponent>();
    (void)ScopedSchema.Add<Ui::WidgetComponent>();
    (void)ScopedSchema.Add<Ui::HypertextComponent>();
    // Presentation policy is simulation state: a rollback restore must keep a
    // frozen body frozen and a sleeping-static light asleep.
    (void)ScopedSchema.Add<CollisionPolicyComponent>();
    (void)ScopedSchema.Add<RenderMaskComponent>();
    (void)ScopedSchema.Add<TimeScaleComponent>();
    (void)ScopedSchema.Add<SleepingStaticComponent>();
    (void)ScopedSchema.Add<LightSourceComponent>();
    Detail::AddPhysicsCodecs(ScopedSchema);
    return ScopedSchema;
}

// Open / size
SnapshotView SnapshotView::Open(std::span<const std::byte> InBytes, std::string_view* InError)
{
    const auto ScopedFail = [InError](std::string_view InMessage)
    {
        if (InError != nullptr)
        {
            *InError = InMessage;
        }
        return SnapshotView{};
    };

    if (InBytes.size() < sizeof(SnapshotHeader))
    {
        return ScopedFail("snapshot is smaller than its header");
    }
    SnapshotHeader ScopedHeader{};
    std::memcpy(&ScopedHeader, InBytes.data(), sizeof(ScopedHeader));
    if (std::memcmp(ScopedHeader.magic, kSnapshotMagic, kMagicSize) != 0)
    {
        return ScopedFail("bad snapshot magic");
    }
    if (ScopedHeader.version != kSnapshotVersion)
    {
        return ScopedFail("unsupported snapshot version");
    }
    const std::uint32_t ScopedKind = ScopedHeader.flags & (SnapshotFull | SnapshotDelta);
    if (ScopedKind == 0)
    {
        return ScopedFail("snapshot has no kind flag");
    }
    if (ScopedKind == (SnapshotFull | SnapshotDelta))
    {
        return ScopedFail("snapshot cannot be both full and delta");
    }
    if (ScopedHeader.entityCount > kMaxSnapshotEntities)
    {
        return ScopedFail("snapshot entity count exceeds the rollback limit");
    }
    // Every component record carries a ComponentHeader, so a count larger than
    // the payload divided by that header is impossible and must not reach a
    // reserve() inside a noexcept parser.
    if (ScopedHeader.componentCount > ScopedHeader.payloadBytes / sizeof(ComponentHeader))
    {
        return ScopedFail("snapshot component count exceeds its payload");
    }
    if (ScopedHeader.extensionOffset != ScopedHeader.payloadBytes)
    {
        return ScopedFail("snapshot world-extension offset is invalid");
    }
    const std::size_t ScopedExpected = sizeof(SnapshotHeader) + static_cast<std::size_t>(ScopedHeader.payloadBytes) +
                                       static_cast<std::size_t>(ScopedHeader.extensionBytes);
    if (InBytes.size() != ScopedExpected)
    {
        return ScopedFail("snapshot size does not match its header");
    }
    const std::span<const std::byte> ScopedPayload = InBytes.subspan(sizeof(SnapshotHeader), ScopedHeader.payloadBytes);
    if (HashBytes(ScopedPayload) != ScopedHeader.checksum)
    {
        return ScopedFail("snapshot checksum mismatch");
    }
    const std::span<const std::byte> ScopedExtension =
        InBytes.subspan(sizeof(SnapshotHeader) + ScopedHeader.payloadBytes, ScopedHeader.extensionBytes);
    if (HashBytes(ScopedExtension) != ScopedHeader.extensionChecksum)
    {
        return ScopedFail("snapshot world-extension checksum mismatch");
    }

    SnapshotView ScopedView;
    ScopedView.header = ScopedHeader;
    ScopedView.payload = ScopedPayload;
    ScopedView.extension = ScopedExtension;
    return ScopedView;
}

std::size_t SnapshotSize(const Registry& InRegistry, const SnapshotSchema& InSchema,
                         std::size_t InExtensionBytes) noexcept
{
    std::size_t ScopedBytes = sizeof(SnapshotHeader) + InExtensionBytes;
    const auto* ScopedStorage = InRegistry.storage<Entity>();
    if (ScopedStorage == nullptr)
    {
        return ScopedBytes;
    }
    for (const Entity ScopedEntity : *ScopedStorage)
    {
        ScopedBytes += sizeof(RecordHeader);
        for (const SnapshotComponent& ScopedCodec : InSchema.All())
        {
            if (ScopedCodec.has(InRegistry, ScopedEntity))
            {
                ScopedBytes += sizeof(ComponentHeader) + ScopedCodec.size;
            }
        }
    }
    return ScopedBytes;
}

// Full capture
bool CaptureSnapshot(const Registry& InRegistry, const SnapshotSchema& InSchema, std::span<std::byte> InDestination,
                     std::uint64_t InTick, const DeterministicRng& InRng, std::size_t* OutBytes,
                     SnapshotExtras InExtras)
{
    if (InDestination.size() < sizeof(SnapshotHeader))
    {
        return false;
    }

    std::array<Entity, kMaxSnapshotEntities> ScopedEntities{};
    bool ScopedOverflow = false;
    const std::uint32_t ScopedEntityCount = CollectEntities(InRegistry, ScopedEntities, &ScopedOverflow);
    if (ScopedOverflow)
    {
        // More entities than the snapshot table can hold: fail instead of
        // silently capturing a subset.
        return false;
    }

    std::array<const SnapshotComponent*, kMaxSnapshotComponents> ScopedComponents{};
    std::uint16_t ScopedComponentCount = 0;
    CollectSortedComponents(InSchema, ScopedComponents, &ScopedComponentCount);

    Writer ScopedWriter(InDestination.data() + sizeof(SnapshotHeader), InDestination.size() - sizeof(SnapshotHeader));
    std::array<std::byte, kMaxSnapshotComponentBytes> ScopedCanonical{};
    std::uint32_t ScopedTotalComponents = 0;

    for (std::uint32_t ScopedI = 0; ScopedI < ScopedEntityCount; ++ScopedI)
    {
        const Entity ScopedEntity = ScopedEntities[ScopedI];
        const std::uint16_t ScopedPresent = CountPresent(InRegistry, InSchema, ScopedEntity);
        RecordHeader ScopedRecord;
        ScopedRecord.entity = ScopedEntity;
        ScopedRecord.componentCount = ScopedPresent;
        ScopedRecord.payloadBytes = 0;

        std::uint32_t ScopedRecordBytes = 0;
        for (std::uint16_t ScopedC = 0; ScopedC < ScopedComponentCount; ++ScopedC)
        {
            const SnapshotComponent& ScopedCodec = *ScopedComponents[ScopedC];
            if (ScopedCodec.has(InRegistry, ScopedEntity))
            {
                ScopedRecordBytes += sizeof(ComponentHeader) + ScopedCodec.size;
            }
        }
        ScopedRecord.payloadBytes = ScopedRecordBytes;

        if (!ScopedWriter.Write(ScopedRecord))
        {
            return false;
        }
        for (std::uint16_t ScopedC = 0; ScopedC < ScopedComponentCount; ++ScopedC)
        {
            const SnapshotComponent& ScopedCodec = *ScopedComponents[ScopedC];
            if (!ScopedCodec.has(InRegistry, ScopedEntity))
            {
                continue;
            }
            ComponentHeader ScopedComponent;
            ScopedComponent.typeId = ScopedCodec.typeId;
            ScopedComponent.size = ScopedCodec.size;
            ScopedComponent.op = SnapshotComponentSet;
            if (!ScopedWriter.Write(ScopedComponent))
            {
                return false;
            }
            if (ScopedCodec.size > 0)
            {
                if (!ScopedCodec.capture(InRegistry, ScopedEntity,
                                         std::span<std::byte>(ScopedCanonical.data(), ScopedCodec.size)))
                {
                    return false;
                }
                if (!ScopedWriter.WriteBytes(std::span<const std::byte>(ScopedCanonical.data(), ScopedCodec.size)))
                {
                    return false;
                }
            }
        }
        ScopedTotalComponents += ScopedPresent;
    }

    SnapshotHeader ScopedHeader;
    std::memcpy(ScopedHeader.magic, kSnapshotMagic, kMagicSize);
    ScopedHeader.version = kSnapshotVersion;
    ScopedHeader.flags = SnapshotFull;
    ScopedHeader.entityCount = ScopedEntityCount;
    ScopedHeader.componentCount = ScopedTotalComponents;
    ScopedHeader.tick = InTick;
    ScopedHeader.baseTick = InTick;
    ScopedHeader.payloadBytes = ScopedWriter.Offset();
    if (ScopedWriter.Remaining() < InExtras.extension.size() || !ScopedWriter.WriteBytes(InExtras.extension))
    {
        return false;
    }
    ScopedHeader.baseChecksum = 0;
    ScopedHeader.schemaHash = InSchema.Hash();
    ScopedHeader.rng = InRng.Save();
    WriteHeader(InDestination, ScopedHeader, InExtras.extension);

    if (OutBytes != nullptr)
    {
        *OutBytes = sizeof(SnapshotHeader) + ScopedWriter.Offset();
    }
    return true;
}

// Delta capture
bool CaptureDelta(const Registry& InRegistry, const SnapshotSchema& InSchema, std::span<const std::byte> InBaseSnapshot,
                  std::span<std::byte> InDestination, std::uint64_t InTick, const DeterministicRng& InRng,
                  std::size_t* OutBytes, SnapshotExtras InExtras)
{
    if (InDestination.size() < sizeof(SnapshotHeader))
    {
        return false;
    }
    BaseView ScopedBase;
    std::string_view ScopedError;
    if (!ParseBase(InBaseSnapshot, true, ScopedBase, &ScopedError))
    {
        return false;
    }
    if (ScopedBase.header.schemaHash != InSchema.Hash())
    {
        // The base was written by a different component schema
        return false;
    }

    std::array<Entity, kMaxSnapshotEntities> ScopedEntities{};
    bool ScopedOverflow = false;
    const std::uint32_t ScopedEntityCount = CollectEntities(InRegistry, ScopedEntities, &ScopedOverflow);
    if (ScopedOverflow)
    {
        return false;
    }
    std::array<const SnapshotComponent*, kMaxSnapshotComponents> ScopedComponents{};
    std::uint16_t ScopedComponentCount = 0;
    CollectSortedComponents(InSchema, ScopedComponents, &ScopedComponentCount);

    Writer ScopedWriter(InDestination.data() + sizeof(SnapshotHeader), InDestination.size() - sizeof(SnapshotHeader));
    std::array<std::byte, kMaxSnapshotComponentBytes> ScopedCanonical{};
    std::uint32_t ScopedRecordCount = 0;
    std::uint32_t ScopedTotalComponents = 0;

    std::size_t ScopedBaseIndex = 0;
    std::uint32_t ScopedCurrentIndex = 0;
    while (ScopedBaseIndex < ScopedBase.entities.size() || ScopedCurrentIndex < ScopedEntityCount)
    {
        const BaseEntity* ScopedPrevious =
            ScopedBaseIndex < ScopedBase.entities.size() ? &ScopedBase.entities[ScopedBaseIndex] : nullptr;
        const Entity ScopedCurrent =
            ScopedCurrentIndex < ScopedEntityCount ? ScopedEntities[ScopedCurrentIndex] : entt::null;

        if (ScopedPrevious != nullptr && (ScopedCurrentIndex >= ScopedEntityCount ||
                                          entt::to_integral(ScopedPrevious->entity) < entt::to_integral(ScopedCurrent)))
        {
            // Entity existed in the base and is gone now.
            RecordHeader ScopedRecord;
            ScopedRecord.entity = ScopedPrevious->entity;
            ScopedRecord.flags = SnapshotEntityRemoved;
            ScopedRecord.componentCount = 0;
            ScopedRecord.payloadBytes = 0;
            if (!ScopedWriter.Write(ScopedRecord))
            {
                return false;
            }
            ++ScopedRecordCount;
            ++ScopedBaseIndex;
            continue;
        }

        if (ScopedPrevious == nullptr || entt::to_integral(ScopedCurrent) < entt::to_integral(ScopedPrevious->entity))
        {
            // New entity: write every component.
            std::uint16_t ScopedPresent = 0;
            std::uint32_t ScopedRecordBytes = 0;
            for (std::uint16_t ScopedC = 0; ScopedC < ScopedComponentCount; ++ScopedC)
            {
                if (ScopedComponents[ScopedC]->has(InRegistry, ScopedCurrent))
                {
                    ++ScopedPresent;
                    ScopedRecordBytes += sizeof(ComponentHeader) + ScopedComponents[ScopedC]->size;
                }
            }
            RecordHeader ScopedRecord;
            ScopedRecord.entity = ScopedCurrent;
            ScopedRecord.flags = SnapshotEntityAdded;
            ScopedRecord.componentCount = ScopedPresent;
            ScopedRecord.payloadBytes = ScopedRecordBytes;
            if (!ScopedWriter.Write(ScopedRecord))
            {
                return false;
            }
            for (std::uint16_t ScopedC = 0; ScopedC < ScopedComponentCount; ++ScopedC)
            {
                const SnapshotComponent& ScopedCodec = *ScopedComponents[ScopedC];
                if (!ScopedCodec.has(InRegistry, ScopedCurrent))
                {
                    continue;
                }
                ComponentHeader ScopedComponent;
                ScopedComponent.typeId = ScopedCodec.typeId;
                ScopedComponent.size = ScopedCodec.size;
                ScopedComponent.op = SnapshotComponentSet;
                if (!ScopedWriter.Write(ScopedComponent))
                {
                    return false;
                }
                if (ScopedCodec.size > 0)
                {
                    if (!ScopedCodec.capture(InRegistry, ScopedCurrent,
                                             std::span<std::byte>(ScopedCanonical.data(), ScopedCodec.size)) ||
                        !ScopedWriter.WriteBytes(std::span<const std::byte>(ScopedCanonical.data(), ScopedCodec.size)))
                    {
                        return false;
                    }
                }
            }
            ScopedTotalComponents += ScopedPresent;
            ++ScopedRecordCount;
            ++ScopedCurrentIndex;
            continue;
        }

        // Same entity in both: emit only the component-level differences.
        ChangeScratch ScopedScratch;
        if (!ComputeEntityChanges(InRegistry, ScopedCurrent, ScopedComponentCount, ScopedComponents, ScopedBase,
                                  ScopedPrevious, ScopedScratch))
        {
            return false;
        }
        if (ScopedScratch.count > 0)
        {
            RecordHeader ScopedRecord;
            ScopedRecord.entity = ScopedCurrent;
            ScopedRecord.flags = 0;
            ScopedRecord.componentCount = ScopedScratch.count;
            ScopedRecord.payloadBytes = ScopedScratch.payloadBytes;
            if (!ScopedWriter.Write(ScopedRecord))
            {
                return false;
            }
            for (std::uint16_t ScopedC = 0; ScopedC < ScopedScratch.count; ++ScopedC)
            {
                const DeltaChange& ScopedChange = ScopedScratch.changes[ScopedC];
                ComponentHeader ScopedComponent;
                ScopedComponent.typeId = ScopedChange.codec->typeId;
                ScopedComponent.size = ScopedChange.op == SnapshotComponentSet ? ScopedChange.codec->size : 0u;
                ScopedComponent.op = ScopedChange.op;
                if (!ScopedWriter.Write(ScopedComponent))
                {
                    return false;
                }
                if (ScopedComponent.size > 0)
                {
                    if (!ScopedChange.codec->capture(
                            InRegistry, ScopedCurrent,
                            std::span<std::byte>(ScopedCanonical.data(), ScopedComponent.size)) ||
                        !ScopedWriter.WriteBytes(
                            std::span<const std::byte>(ScopedCanonical.data(), ScopedComponent.size)))
                    {
                        return false;
                    }
                }
            }
            ScopedTotalComponents += ScopedScratch.count;
            ++ScopedRecordCount;
        }
        ++ScopedBaseIndex;
        ++ScopedCurrentIndex;
    }

    if (ScopedRecordCount > kMaxSnapshotEntities)
    {
        // A base plus current churn can exceed what a delta reader accepts;
        // refuse instead of writing an unreadable delta.
        return false;
    }

    SnapshotHeader ScopedHeader;
    std::memcpy(ScopedHeader.magic, kSnapshotMagic, kMagicSize);
    ScopedHeader.version = kSnapshotVersion;
    ScopedHeader.flags = SnapshotDelta;
    ScopedHeader.entityCount = ScopedRecordCount;
    ScopedHeader.componentCount = ScopedTotalComponents;
    ScopedHeader.tick = InTick;
    ScopedHeader.baseTick = ScopedBase.header.tick;
    ScopedHeader.payloadBytes = ScopedWriter.Offset();
    if (ScopedWriter.Remaining() < InExtras.extension.size() || !ScopedWriter.WriteBytes(InExtras.extension))
    {
        return false;
    }
    ScopedHeader.baseChecksum = ScopedBase.header.checksum;
    ScopedHeader.schemaHash = InSchema.Hash();
    ScopedHeader.rng = InRng.Save();
    WriteHeader(InDestination, ScopedHeader, InExtras.extension);

    if (OutBytes != nullptr)
    {
        *OutBytes = sizeof(SnapshotHeader) + ScopedWriter.Offset();
    }
    return true;
}

// Restore
bool RestoreSnapshot(Registry& InRegistry, const SnapshotSchema& InSchema, std::span<const std::byte> InSnapshot,
                     DeterministicRng* OutRng, SnapshotRestoreExtras InExtras)
{
    std::string_view ScopedError;
    const SnapshotView ScopedView = SnapshotView::Open(InSnapshot, &ScopedError);
    if (ScopedView.header.version == 0)
    {
        return false;
    }
    if ((ScopedView.header.flags & SnapshotFull) == 0)
    {
        // Use RestoreDelta for a delta snapshot
        return false;
    }
    if (ScopedView.header.schemaHash != InSchema.Hash())
    {
        return false;
    }
    if (!InExtras.extension.empty() && InExtras.extension.size() < ScopedView.extension.size())
    {
        return false;
    }
    // Validate every record before the registry is touched. The checksum only
    // proves the bytes were not corrupted; it says nothing about entity ids,
    // component ids or sizes.
    std::string_view ScopedPayloadError;
    if (!ValidatePayload(ScopedView.payload, InSchema, ScopedView.header.entityCount, false, &ScopedPayloadError))
    {
        return false;
    }

    Scene::DestroyAllEntities(InRegistry);
    Reader ScopedReader(ScopedView.payload.data(), ScopedView.payload.size());
    for (std::uint32_t ScopedRecordIndex = 0; ScopedRecordIndex < ScopedView.header.entityCount; ++ScopedRecordIndex)
    {
        RecordHeader ScopedRecord;
        if (!ScopedReader.Read(ScopedRecord) || ScopedRecord.entity == entt::null)
        {
            return false;
        }
        if (!InRegistry.valid(ScopedRecord.entity))
        {
            (void)InRegistry.create(ScopedRecord.entity);
        }
        for (std::uint16_t ScopedComponentIndex = 0; ScopedComponentIndex < ScopedRecord.componentCount;
             ++ScopedComponentIndex)
        {
            ComponentHeader ScopedComponent;
            if (!ScopedReader.Read(ScopedComponent))
            {
                return false;
            }
            const SnapshotComponent* ScopedCodec = InSchema.Find(ScopedComponent.typeId);
            if (ScopedCodec == nullptr || ScopedComponent.op != SnapshotComponentSet ||
                ScopedComponent.size != ScopedCodec->size)
            {
                return false;
            }
            std::span<const std::byte> ScopedData;
            if (!ScopedReader.ReadBytes(ScopedComponent.size, ScopedData) ||
                !ScopedCodec->restore(InRegistry, ScopedRecord.entity, ScopedData))
            {
                return false;
            }
        }
    }
    if (ScopedReader.Remaining() != 0)
    {
        return false;
    }
    // Report the extension only after the entity payload has been accepted, so
    // a rejected restore cannot mutate the caller's world bytes.
    if (!InExtras.extension.empty() && !ScopedView.extension.empty())
    {
        std::memcpy(InExtras.extension.data(), ScopedView.extension.data(), ScopedView.extension.size());
    }
    if (OutRng != nullptr)
    {
        OutRng->Restore(ScopedView.header.rng);
    }
    return true;
}

bool ValidateSnapshot(std::span<const std::byte> InSnapshot, const SnapshotSchema& InSchema,
                      std::string_view* InError) noexcept
{
    const SnapshotView ScopedView = SnapshotView::Open(InSnapshot, InError);
    if (ScopedView.header.version == 0)
    {
        return false;
    }
    if ((ScopedView.header.flags & SnapshotFull) == 0)
    {
        if (InError != nullptr)
        {
            *InError = "snapshot is not a full snapshot";
        }
        return false;
    }
    if (ScopedView.header.schemaHash != InSchema.Hash())
    {
        if (InError != nullptr)
        {
            *InError = "snapshot schema does not match";
        }
        return false;
    }
    return ValidatePayload(ScopedView.payload, InSchema, ScopedView.header.entityCount, false, InError);
}
bool RestoreDelta(Registry& InRegistry, const SnapshotSchema& InSchema, std::span<const std::byte> InBaseSnapshot,
                  std::span<const std::byte> InDelta, DeterministicRng* OutRng, SnapshotRestoreExtras InExtras)
{
    BaseView ScopedBase;
    std::string_view ScopedError;
    if (!ParseBase(InBaseSnapshot, true, ScopedBase, &ScopedError))
    {
        return false;
    }
    if (ScopedBase.header.schemaHash != InSchema.Hash())
    {
        // The base was written by a different component schema
        return false;
    }

    std::string_view ScopedDeltaError;
    const SnapshotView ScopedView = SnapshotView::Open(InDelta, &ScopedDeltaError);
    if (ScopedView.header.version == 0 || (ScopedView.header.flags & SnapshotDelta) == 0)
    {
        return false;
    }
    if (ScopedView.header.baseChecksum != ScopedBase.header.checksum ||
        ScopedView.header.baseTick != ScopedBase.header.tick ||
        ScopedView.header.schemaHash != InSchema.Hash() || ScopedBase.header.schemaHash != InSchema.Hash())
    {
        // Delta belongs to a different base frame or schema
        return false;
    }
    // Validate the delta before the base restore touches the registry.
    std::string_view ScopedDeltaPayloadError;
    if (!ValidatePayload(ScopedView.payload, InSchema, ScopedView.header.entityCount, true,
                         &ScopedDeltaPayloadError))
    {
        return false;
    }

    // Restore the base world extension too: a delta may carry an empty one, and
    // dropping the base bytes would silently change the world state.
    if (!RestoreSnapshot(InRegistry, InSchema, InBaseSnapshot, nullptr, InExtras))
    {
        return false;
    }

    Reader ScopedReader(ScopedView.payload.data(), ScopedView.payload.size());
    for (std::uint32_t ScopedRecordIndex = 0; ScopedRecordIndex < ScopedView.header.entityCount; ++ScopedRecordIndex)
    {
        RecordHeader ScopedRecord;
        if (!ScopedReader.Read(ScopedRecord) || ScopedRecord.entity == entt::null)
        {
            return false;
        }
        if ((ScopedRecord.flags & SnapshotEntityRemoved) != 0)
        {
            if (InRegistry.valid(ScopedRecord.entity))
            {
                InRegistry.destroy(ScopedRecord.entity);
            }
            continue;
        }
        if (!InRegistry.valid(ScopedRecord.entity))
        {
            (void)InRegistry.create(ScopedRecord.entity);
        }
        for (std::uint16_t ScopedComponentIndex = 0; ScopedComponentIndex < ScopedRecord.componentCount;
             ++ScopedComponentIndex)
        {
            ComponentHeader ScopedComponent;
            if (!ScopedReader.Read(ScopedComponent))
            {
                return false;
            }
            const SnapshotComponent* ScopedCodec = InSchema.Find(ScopedComponent.typeId);
            if (ScopedCodec == nullptr)
            {
                return false;
            }
            std::span<const std::byte> ScopedData;
            if (ScopedComponent.op == SnapshotComponentRemove)
            {
                if (ScopedComponent.size != 0 || !ScopedReader.ReadBytes(0, ScopedData))
                {
                    return false;
                }
                ScopedCodec->remove(InRegistry, ScopedRecord.entity);
            }
            else if (ScopedComponent.op == SnapshotComponentSet)
            {
                if (ScopedComponent.size != ScopedCodec->size ||
                    !ScopedReader.ReadBytes(ScopedComponent.size, ScopedData) ||
                    !ScopedCodec->restore(InRegistry, ScopedRecord.entity, ScopedData))
                {
                    return false;
                }
            }
            else
            {
                return false;
            }
        }
    }
    if (ScopedReader.Remaining() != 0)
    {
        return false;
    }
    if (!InExtras.extension.empty() && InExtras.extension.size() < ScopedView.extension.size())
    {
        return false;
    }
    if (!InExtras.extension.empty() && !ScopedView.extension.empty())
    {
        std::memcpy(InExtras.extension.data(), ScopedView.extension.data(), ScopedView.extension.size());
    }
    if (OutRng != nullptr)
    {
        OutRng->Restore(ScopedView.header.rng);
    }
    return true;
}

bool ApplyDelta(Registry& InRegistry, const SnapshotSchema& InSchema, std::span<const std::byte> InDelta) noexcept
{
    std::string_view ScopedError;
    const SnapshotView ScopedView = SnapshotView::Open(InDelta, &ScopedError);
    if (ScopedView.header.version == 0 || (ScopedView.header.flags & SnapshotDelta) == 0 ||
        ScopedView.header.schemaHash != InSchema.Hash())
    {
        return false;
    }

    Reader ScopedReader(ScopedView.payload.data(), ScopedView.payload.size());
    for (std::uint32_t ScopedRecordIndex = 0; ScopedRecordIndex < ScopedView.header.entityCount; ++ScopedRecordIndex)
    {
        RecordHeader ScopedRecord;
        if (!ScopedReader.Read(ScopedRecord) || ScopedRecord.entity == entt::null)
        {
            return false;
        }
        if ((ScopedRecord.flags & SnapshotEntityRemoved) != 0)
        {
            if (InRegistry.valid(ScopedRecord.entity))
            {
                InRegistry.destroy(ScopedRecord.entity);
            }
            continue;
        }
        if (!InRegistry.valid(ScopedRecord.entity))
        {
            (void)InRegistry.create(ScopedRecord.entity);
        }
        for (std::uint16_t ScopedComponentIndex = 0; ScopedComponentIndex < ScopedRecord.componentCount;
             ++ScopedComponentIndex)
        {
            ComponentHeader ScopedComponent;
            if (!ScopedReader.Read(ScopedComponent))
            {
                return false;
            }
            const SnapshotComponent* ScopedCodec = InSchema.Find(ScopedComponent.typeId);
            if (ScopedCodec == nullptr)
            {
                return false;
            }
            std::span<const std::byte> ScopedData;
            if (ScopedComponent.op == SnapshotComponentRemove)
            {
                if (ScopedComponent.size != 0 || !ScopedReader.ReadBytes(0, ScopedData))
                {
                    return false;
                }
                ScopedCodec->remove(InRegistry, ScopedRecord.entity);
            }
            else if (ScopedComponent.op == SnapshotComponentSet)
            {
                if (ScopedComponent.size != ScopedCodec->size ||
                    !ScopedReader.ReadBytes(ScopedComponent.size, ScopedData) ||
                    !ScopedCodec->restore(InRegistry, ScopedRecord.entity, ScopedData))
                {
                    return false;
                }
            }
            else
            {
                return false;
            }
        }
    }
    return true;
}

bool RememberBaseline(SnapshotBaseline& OutBaseline, std::span<const std::byte> InFullSnapshot) noexcept
{
    std::string_view ScopedError;
    const SnapshotView ScopedView = SnapshotView::Open(InFullSnapshot, &ScopedError);
    if (ScopedView.header.version == 0 || (ScopedView.header.flags & SnapshotDelta) != 0)
    {
        return false;
    }
    OutBaseline.Tick = ScopedView.header.tick;
    OutBaseline.PayloadChecksum = ScopedView.header.checksum;
    OutBaseline.Valid = true;
    return true;
}

bool ApplyDeltaChecked(Registry& InRegistry, const SnapshotSchema& InSchema,
                       std::span<const std::byte> InDelta, const SnapshotBaseline& InBaseline) noexcept
{
    if (!InBaseline.Valid)
    {
        return false;
    }
    std::string_view ScopedError;
    const SnapshotView ScopedView = SnapshotView::Open(InDelta, &ScopedError);
    if (ScopedView.header.version == 0 || (ScopedView.header.flags & SnapshotDelta) == 0 ||
        ScopedView.header.baseTick != InBaseline.Tick ||
        ScopedView.header.baseChecksum != InBaseline.PayloadChecksum)
    {
        return false;
    }
    return ApplyDelta(InRegistry, InSchema, InDelta);
}
}
