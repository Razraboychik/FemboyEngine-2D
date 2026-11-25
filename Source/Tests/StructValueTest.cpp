// FemboyEngine2D - bounded authoring struct payload tests.
#include "Engine/Data/StructValue.hpp"
#include "Engine/Data/StructValueToml.hpp"
#include "Engine/Gameplay/GameplayEffects.hpp"
#include "Engine/ECS/Snapshot.hpp"
#include "Engine/ECS/StructPayloadComponent.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{
std::uint32_t GFailures = 0;

void Check(bool InCondition, const char* InMessage)
{
    if (!InCondition)
    {
        std::fprintf(stderr, "FAIL: %s\n", InMessage);
        ++GFailures;
    }
}

struct Durability
{
    float Current = 100.0f;
    float Maximum = 100.0f;
};

struct RollRange
{
    std::uint32_t Minimum = 1;
    std::uint32_t Maximum = 5;
    bool Enabled = true;
};

[[nodiscard]] std::array<Fby::Data::StructField, 2> DurabilityFields()
{
    return {{
        {"current", Fby::Reflect::FieldKind::Float, static_cast<std::uint32_t>(offsetof(Durability, Current)),
         sizeof(float)},
        {"maximum", Fby::Reflect::FieldKind::Float, static_cast<std::uint32_t>(offsetof(Durability, Maximum)),
         sizeof(float)},
    }};
}

[[nodiscard]] std::array<Fby::Data::StructField, 3> RollFields()
{
    return {{
        {"minimum", Fby::Reflect::FieldKind::UInt32, static_cast<std::uint32_t>(offsetof(RollRange, Minimum)),
         sizeof(std::uint32_t)},
        {"maximum", Fby::Reflect::FieldKind::UInt32, static_cast<std::uint32_t>(offsetof(RollRange, Maximum)),
         sizeof(std::uint32_t)},
        {"enabled", Fby::Reflect::FieldKind::Bool, static_cast<std::uint32_t>(offsetof(RollRange, Enabled)),
         sizeof(bool)},
    }};
}

void TestRegistryAndValues()
{
    Fby::Data::StructTypeRegistry ScopedRegistry;
    const auto ScopedDurabilityFields = DurabilityFields();
    const auto ScopedRollFields = RollFields();
    Check(Fby::Data::RegisterStructType<Durability>(ScopedRegistry, "Item.Durability", ScopedDurabilityFields),
          "register durability type");
    Check(Fby::Data::RegisterStructType<RollRange>(ScopedRegistry, "Item.RollRange", ScopedRollFields),
          "register roll type");
    Check(ScopedRegistry.Count() == 2, "registry holds both types");
    Check(!Fby::Data::RegisterStructType<Durability>(ScopedRegistry, "Item.Durability", ScopedDurabilityFields),
          "duplicate type name is rejected");
    Check(!Fby::Data::RegisterStructType<Durability>(ScopedRegistry, "Item.NoFields"),
          "a type without fields is rejected");

    const Fby::Data::StructType* ScopedDurability = ScopedRegistry.Find("Item.Durability");
    const Fby::Data::StructType* ScopedRoll = ScopedRegistry.Find("Item.RollRange");
    Check(ScopedDurability != nullptr && ScopedRoll != nullptr, "types resolve by name");
    if (ScopedDurability == nullptr || ScopedRoll == nullptr)
    {
        return;
    }

    Fby::Data::StructType ScopedCollision;
    ScopedCollision.Id = ScopedDurability->Id;
    ScopedCollision.Name = "Other.Durability";
    ScopedCollision.Size = sizeof(Durability);
    ScopedCollision.Alignment = alignof(Durability);
    Check(!ScopedRegistry.Register(ScopedCollision), "duplicate type id is rejected");

    Fby::Data::StructPayload ScopedPayload;
    Durability ScopedDurabilityValue{42.0f, 80.0f};
    RollRange ScopedRollValue{2, 9, false};
    Check(ScopedPayload.Add(*ScopedDurability, ScopedDurabilityValue), "add durability payload");
    Check(ScopedPayload.Add(*ScopedRoll, ScopedRollValue), "add roll payload");
    Check(ScopedPayload.Count == 2, "payload count");
    const Durability* ScopedStoredDurability = ScopedPayload.Find<Durability>(ScopedDurability->Id);
    const RollRange* ScopedStoredRoll = ScopedPayload.Find<RollRange>(ScopedRoll->Id);
    Check(ScopedStoredDurability != nullptr && ScopedStoredDurability->Current == 42.0f,
          "durability value roundtrips in memory");
    Check(ScopedStoredRoll != nullptr && ScopedStoredRoll->Maximum == 9 && !ScopedStoredRoll->Enabled,
          "roll value roundtrips in memory");
    Check(ScopedPayload.Find<RollRange>(ScopedDurability->Id) == nullptr, "type lookup rejects a mismatched type");

    Fby::Data::StructPayload ScopedCanonicalA;
    Fby::Data::StructPayload ScopedCanonicalB;
    RollRange ScopedPaddedA{3, 6, true};
    RollRange ScopedPaddedB{3, 6, true};
    std::memset(&ScopedPaddedA, 0xAA, sizeof(ScopedPaddedA));
    std::memset(&ScopedPaddedB, 0x55, sizeof(ScopedPaddedB));
    ScopedPaddedA.Minimum = 3;
    ScopedPaddedA.Maximum = 6;
    ScopedPaddedA.Enabled = true;
    ScopedPaddedB.Minimum = 3;
    ScopedPaddedB.Maximum = 6;
    ScopedPaddedB.Enabled = true;
    Check(ScopedCanonicalA.Add(*ScopedRoll, ScopedPaddedA), "add padded payload A");
    Check(ScopedCanonicalB.Add(*ScopedRoll, ScopedPaddedB), "add padded payload B");
    Check(ScopedCanonicalA.Hash() == ScopedCanonicalB.Hash(), "padding does not affect the payload hash");

    const std::uint64_t ScopedHash = ScopedPayload.Hash();
    Check(ScopedHash != 0, "payload hash is non-zero");
    Check(ScopedPayload.RemoveAt(0), "remove first payload value");
    Check(ScopedPayload.Count == 1 && ScopedPayload.Values[1].Empty(), "removal leaves a cleared tail");
    Check(ScopedPayload.Add(*ScopedDurability, ScopedDurabilityValue), "add after removal");
    Check(ScopedPayload.Hash() != ScopedHash, "payload hash changes with content");

    Check(!ScopedPayload.Values[0].SetRaw(0, 0, {}), "empty type is rejected");
    std::array<std::byte, Fby::Data::kStructValueBytes + 1> ScopedOversize{};
    Check(!ScopedPayload.Values[0].SetRaw(ScopedDurability->Id, 0, ScopedOversize), "oversize raw value is rejected");
    Check(ScopedPayload.Add(*ScopedDurability, ScopedDurabilityValue), "fill payload");
    Check(ScopedPayload.Add(*ScopedDurability, ScopedDurabilityValue), "fill payload to capacity");
    Check(!ScopedPayload.Add(*ScopedDurability, ScopedDurabilityValue), "payload capacity is enforced");
}

void TestTomlRoundtrip()
{
    Fby::Data::StructTypeRegistry ScopedRegistry;
    const auto ScopedDurabilityFields = DurabilityFields();
    Check(Fby::Data::RegisterStructType<Durability>(ScopedRegistry, "Item.Durability", ScopedDurabilityFields),
          "register TOML type");
    const Fby::Data::StructType* ScopedType = ScopedRegistry.Find("Item.Durability");
    Check(ScopedType != nullptr, "TOML type resolves");

    Fby::Data::StructPayload ScopedPayload;
    Durability ScopedValue{12.5f, 20.0f};
    Check(ScopedPayload.Add(*ScopedType, ScopedValue), "add TOML payload");

    toml::array ScopedArray;
    std::string ScopedError;
    Check(Fby::Data::EncodeStructPayloadToml(ScopedPayload, ScopedRegistry, ScopedArray, &ScopedError),
          "encode payload to TOML");
    Fby::Data::StructPayload ScopedDecoded;
    Check(Fby::Data::DecodeStructPayloadToml(ScopedArray, ScopedRegistry, &ScopedDecoded, &ScopedError),
          "decode payload from TOML");
    const Durability* ScopedDecodedValue = ScopedDecoded.Find<Durability>(ScopedType->Id);
    Check(ScopedDecodedValue != nullptr && ScopedDecodedValue->Current == 12.5f && ScopedDecodedValue->Maximum == 20.0f,
          "TOML payload field values roundtrip");
    Check(ScopedDecoded.Hash() == ScopedPayload.Hash(), "TOML roundtrip preserves the payload hash");

    toml::array ScopedBadSchema;
    toml::table ScopedBadEntry;
    ScopedBadEntry.insert("type", std::string("Item.Durability"));
    ScopedBadEntry.insert("schema", std::string("1"));
    ScopedBadEntry.insert("fields", toml::table{});
    ScopedBadSchema.push_back(std::move(ScopedBadEntry));
    Check(!Fby::Data::DecodeStructPayloadToml(ScopedBadSchema, ScopedRegistry, &ScopedDecoded, &ScopedError),
          "schema mismatch is rejected");
    Check(!ScopedError.empty(), "schema mismatch reports an error");
}



void TestGameplayEffectExtensionPoint()
{
    Fby::Data::StructTypeRegistry ScopedRegistry;
    const auto ScopedFields = DurabilityFields();
    Check(Fby::Data::RegisterStructType<Durability>(ScopedRegistry, "Item.Durability", ScopedFields),
          "register effect payload type");
    const Fby::Data::StructType* ScopedType = ScopedRegistry.Find("Item.Durability");
    Check(ScopedType != nullptr, "effect payload type resolves");
    if (ScopedType == nullptr)
    {
        return;
    }
    Fby::GameplayEffectDef ScopedDef;
    ScopedDef.Id = 7;
    Durability ScopedValue{3.0f, 4.0f};
    Check(ScopedDef.Params.Add(*ScopedType, ScopedValue), "effect definition carries a payload");
    Fby::GameplayEffectSystem ScopedEffects;
    Check(ScopedEffects.AddDef(ScopedDef), "effect definition with payload registers");
    const Fby::GameplayEffectDef* ScopedStored = ScopedEffects.FindDef(7);
    Check(ScopedStored != nullptr, "effect definition resolves");
    if (ScopedStored != nullptr)
    {
        const Durability* ScopedStoredValue = ScopedStored->Params.Find<Durability>(ScopedType->Id);
        Check(ScopedStoredValue != nullptr && ScopedStoredValue->Current == 3.0f,
              "effect payload is not duplicated by the effect system");
    }
}
void TestSnapshotRoundtrip()
{
    Fby::Data::StructTypeRegistry ScopedRegistry;
    const auto ScopedFields = DurabilityFields();
    Check(Fby::Data::RegisterStructType<Durability>(ScopedRegistry, "Item.Durability", ScopedFields),
          "register snapshot payload type");
    const Fby::Data::StructType* ScopedType = ScopedRegistry.Find("Item.Durability");
    Check(ScopedType != nullptr, "snapshot payload type resolves");
    if (ScopedType == nullptr)
    {
        return;
    }

    Fby::Ecs::Registry ScopedSource;
    const Fby::Entity ScopedEntity = ScopedSource.create();
    auto& ScopedComponent = ScopedSource.emplace<Fby::StructPayloadComponent>(ScopedEntity);
    Durability ScopedValue{7.0f, 9.0f};
    Check(ScopedComponent.Payload.Add(*ScopedType, ScopedValue), "add snapshot payload");

    const Fby::Ecs::SnapshotSchema ScopedSchema = Fby::Ecs::SnapshotSchema::BuiltIns();
    const std::size_t ScopedBytes = Fby::Ecs::SnapshotSize(ScopedSource, ScopedSchema);
    std::vector<std::byte> ScopedSnapshot(ScopedBytes);
    Fby::DeterministicRng ScopedRng = Fby::DeterministicRng::FromSeed(1);
    Check(Fby::Ecs::CaptureSnapshot(ScopedSource, ScopedSchema, ScopedSnapshot, 1, ScopedRng),
          "payload snapshot captures");

    Fby::Ecs::Registry ScopedTarget;
    Check(Fby::Ecs::RestoreSnapshot(ScopedTarget, ScopedSchema, ScopedSnapshot), "payload snapshot restores");
    const auto* ScopedRestored = ScopedTarget.try_get<Fby::StructPayloadComponent>(ScopedEntity);
    Check(ScopedRestored != nullptr, "payload component survives snapshot");
    if (ScopedRestored != nullptr)
    {
        const Durability* ScopedRestoredValue = ScopedRestored->Payload.Find<Durability>(ScopedType->Id);
        Check(ScopedRestoredValue != nullptr && ScopedRestoredValue->Current == 7.0f &&
                  ScopedRestoredValue->Maximum == 9.0f,
              "payload fields survive snapshot");
        Check(ScopedRestored->Payload.Hash() == ScopedComponent.Payload.Hash(), "payload hash survives snapshot");
    }
}
void TestRegistryStress()
{
    Fby::Data::StructTypeRegistry ScopedRegistry;
    const auto ScopedFields = RollFields();
    std::array<std::array<char, 32>, Fby::Data::kStructRegistryCapacity> ScopedNames{};
    std::uint32_t ScopedRegistered = 0;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < Fby::Data::kStructRegistryCapacity; ++ScopedIndex)
    {
        std::snprintf(ScopedNames[ScopedIndex].data(), ScopedNames[ScopedIndex].size(), "Stress.Type.%u", ScopedIndex);
        if (Fby::Data::RegisterStructType<RollRange>(ScopedRegistry, ScopedNames[ScopedIndex].data(), ScopedFields))
        {
            ++ScopedRegistered;
        }
    }
    Check(ScopedRegistered == Fby::Data::kStructRegistryCapacity, "registry accepts its full capacity");

    Fby::Data::StructPayload ScopedPayload;
    const Fby::Data::StructType* ScopedType = ScopedRegistry.Find(ScopedNames[0].data());
    Check(ScopedType != nullptr, "stress type resolves");
    if (ScopedType == nullptr)
    {
        return;
    }
    std::uint64_t ScopedHash = 0;
    for (std::uint32_t ScopedIteration = 0; ScopedIteration < 200000; ++ScopedIteration)
    {
        RollRange ScopedValue{ScopedIteration % 7u, ScopedIteration % 11u, (ScopedIteration % 2u) != 0};
        Check(ScopedPayload.Add(*ScopedType, ScopedValue), "stress add");
        ScopedHash ^= ScopedPayload.Hash();
        Check(ScopedPayload.RemoveAt(0), "stress remove");
    }
    Check(ScopedPayload.Count == 0, "stress leaves an empty payload");
    Check(ScopedHash != 0, "stress produced hashes");
}
}

int main()
{
    TestRegistryAndValues();
    TestTomlRoundtrip();
    TestGameplayEffectExtensionPoint();
    TestSnapshotRoundtrip();
    TestRegistryStress();
    if (GFailures == 0)
    {
        std::printf("struct_value_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "struct_value_test: %u failure(s)\n", GFailures);
    return 1;
}
