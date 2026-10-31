// FemboyEngine2D - prefab import and transactional spawn.
#include "Engine/ECS/PrefabSystem.hpp"

#include "Engine/Physics/Physics.hpp"

#include <toml.hpp>
#include "prefab_generated.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

namespace Fby
{
namespace
{

struct PrefabBinaryHeader
{
    char magic[8] = {};
    std::uint32_t version = 0;
    std::uint32_t count = 0;
    char name[kPrefabAssetNameBytes] = {};
    std::uint32_t payloadBytes = 0;
    std::uint32_t reserved = 0;
};

static_assert(sizeof(PrefabBinaryHeader) == 88);

void CopyText(char* OutText, std::size_t InCapacity, std::string_view InValue) noexcept
{
    const std::size_t ScopedCount = std::min(InValue.size(), InCapacity - 1);
    std::memcpy(OutText, InValue.data(), ScopedCount);
    OutText[ScopedCount] = '\0';
}

[[nodiscard]] std::string StringField(const toml::table& InTable, std::string_view InKey,
                                     std::string_view InFallback = {})
{
    const toml::node_view ScopedNode = InTable[InKey];
    if (const auto ScopedValue = ScopedNode.value<std::string>(); ScopedValue.has_value())
    {
        return *ScopedValue;
    }
    return std::string(InFallback);
}

[[nodiscard]] double FloatField(const toml::table& InTable, std::string_view InKey, double InFallback) noexcept
{
    const toml::node_view ScopedNode = InTable[InKey];
    if (const auto ScopedValue = ScopedNode.value<double>(); ScopedValue.has_value())
    {
        return *ScopedValue;
    }
    if (const auto ScopedInteger = ScopedNode.value<std::int64_t>(); ScopedInteger.has_value())
    {
        return static_cast<double>(*ScopedInteger);
    }
    return InFallback;
}

[[nodiscard]] std::int64_t IntField(const toml::table& InTable, std::string_view InKey,
                                    std::int64_t InFallback) noexcept
{
    const toml::node_view ScopedNode = InTable[InKey];
    if (const auto ScopedValue = ScopedNode.value<std::int64_t>(); ScopedValue.has_value())
    {
        return *ScopedValue;
    }
    return InFallback;
}

[[nodiscard]] bool BoolField(const toml::table& InTable, std::string_view InKey, bool InFallback) noexcept
{
    const toml::node_view ScopedNode = InTable[InKey];
    if (const auto ScopedValue = ScopedNode.value<bool>(); ScopedValue.has_value())
    {
        return *ScopedValue;
    }
    return InFallback;
}

[[nodiscard]] Vector2D VectorField(const toml::table& InTable, std::string_view InKey, Vector2D InFallback) noexcept
{
    const toml::node_view ScopedNode = InTable[InKey];
    const toml::array* ScopedArray = ScopedNode.as_array();
    if (ScopedArray == nullptr || ScopedArray->size() < 2)
    {
        return InFallback;
    }
    return Vector2D{static_cast<float>((*ScopedArray)[0].value_or(InFallback.X)),
                    static_cast<float>((*ScopedArray)[1].value_or(InFallback.Y))};
}

void ErrorFile(std::string* OutError, std::string_view InText)
{
    if (OutError != nullptr)
    {
        *OutError = std::string(InText);
    }
}
}

bool PrefabSystem::LoadToml(std::string_view InText, PrefabAsset& OutAsset, std::string* OutError)
{
    OutAsset = PrefabAsset{};
    toml::table ScopedRoot;
    try
    {
        ScopedRoot = toml::parse(InText);
    }
    catch (const toml::parse_error& ScopedError)
    {
        ErrorFile(OutError, std::string("prefab TOML error: ") + std::string(ScopedError.description()));
        return false;
    }
    const toml::table* ScopedPrefab = ScopedRoot["prefab"].as_table();
    if (ScopedPrefab == nullptr)
    {
        ScopedPrefab = &ScopedRoot;
    }
    CopyText(OutAsset.name, sizeof(OutAsset.name), StringField(*ScopedPrefab, "name", "prefab"));
    const toml::array* ScopedEntities = (*ScopedPrefab)["entities"].as_array();
    if (ScopedEntities == nullptr || ScopedEntities->empty())
    {
        ErrorFile(OutError, "prefab has no [[entities]] array");
        return false;
    }
    if (ScopedEntities->size() > kPrefabMaxEntities)
    {
        ErrorFile(OutError, "prefab has more entities than kPrefabMaxEntities");
        return false;
    }
    for (const toml::node& ScopedNode : *ScopedEntities)
    {
        const toml::table* ScopedEntity = ScopedNode.as_table();
        if (ScopedEntity == nullptr)
        {
            ErrorFile(OutError, "prefab entity is not a table");
            return false;
        }
        PrefabEntityDesc& ScopedDesc = OutAsset.entities[OutAsset.count++];
        CopyText(ScopedDesc.name, sizeof(ScopedDesc.name), StringField(*ScopedEntity, "name", "entity"));
        ScopedDesc.transform.Position = VectorField(*ScopedEntity, "Position", Vector2D{});
        ScopedDesc.transform.Scale = VectorField(*ScopedEntity, "Scale", Vector2D{1.0f, 1.0f});
        ScopedDesc.transform.Rotation = static_cast<float>(FloatField(*ScopedEntity, "Rotation", 0.0));

        if (const toml::table* ScopedPhysics = (*ScopedEntity)["physics"].as_table(); ScopedPhysics != nullptr)
        {
            ScopedDesc.flags |= PrefabHasPhysics;
            ScopedDesc.physics.Type = static_cast<std::uint32_t>(IntField(*ScopedPhysics, "type", 2));
            ScopedDesc.physics.Shape = static_cast<std::uint32_t>(IntField(*ScopedPhysics, "shape", 0));
            ScopedDesc.physics.HalfExtents = VectorField(*ScopedPhysics, "half_extents", Vector2D{16.0f, 16.0f});
            ScopedDesc.physics.Radius = static_cast<float>(FloatField(*ScopedPhysics, "radius", 8.0));
            ScopedDesc.physics.Mass = static_cast<float>(FloatField(*ScopedPhysics, "mass", 1.0));
            ScopedDesc.physics.Density = static_cast<float>(FloatField(*ScopedPhysics, "density", 1.0));
            ScopedDesc.physics.Friction = static_cast<float>(FloatField(*ScopedPhysics, "friction", 0.3));
            ScopedDesc.physics.Restitution = static_cast<float>(FloatField(*ScopedPhysics, "restitution", 0.0));
            ScopedDesc.physics.LinearDamping = static_cast<float>(FloatField(*ScopedPhysics, "linear_damping", 0.0));
            ScopedDesc.physics.AngularDamping = static_cast<float>(FloatField(*ScopedPhysics, "angular_damping", 0.0));
            ScopedDesc.physics.GravityScale = static_cast<float>(FloatField(*ScopedPhysics, "gravity_scale", 1.0));
            ScopedDesc.physics.CategoryBits = static_cast<std::uint32_t>(IntField(*ScopedPhysics, "category", 1));
            ScopedDesc.physics.MaskBits =
                static_cast<std::uint32_t>(IntField(*ScopedPhysics, "mask", 0xFFFFFFFFll));
            ScopedDesc.physics.GroupIndex = static_cast<std::int32_t>(IntField(*ScopedPhysics, "group", 0));
            ScopedDesc.physics.Sensor = BoolField(*ScopedPhysics, "sensor", false);
            ScopedDesc.physics.ContactEvents = BoolField(*ScopedPhysics, "contact_events", true);
            ScopedDesc.physics.FixedRotation = BoolField(*ScopedPhysics, "fixed_rotation", false);
            ScopedDesc.physics.Bullet = BoolField(*ScopedPhysics, "bullet", false);
        }
        if (const toml::table* ScopedCharacter = (*ScopedEntity)["character"].as_table(); ScopedCharacter != nullptr)
        {
            ScopedDesc.flags |= PrefabHasCharacter;
            ScopedDesc.character.MaxHealth = static_cast<float>(FloatField(*ScopedCharacter, "health", 100.0));
            ScopedDesc.character.MaxStamina = static_cast<float>(FloatField(*ScopedCharacter, "stamina", 100.0));
            ScopedDesc.character.MoveSpeed = static_cast<float>(FloatField(*ScopedCharacter, "speed", 220.0));
            ScopedDesc.character.Acceleration = static_cast<float>(FloatField(*ScopedCharacter, "acceleration", 1800.0));
            ScopedDesc.character.JumpVelocity = static_cast<float>(FloatField(*ScopedCharacter, "jump", 520.0));
            ScopedDesc.character.Damage = static_cast<float>(FloatField(*ScopedCharacter, "damage", 10.0));
            ScopedDesc.character.FireInterval = static_cast<float>(FloatField(*ScopedCharacter, "fire_interval", 0.12));
            ScopedDesc.character.ProjectileSpeed =
                static_cast<float>(FloatField(*ScopedCharacter, "projectile_speed", 900.0));
            ScopedDesc.character.InvulnerabilitySeconds =
                static_cast<float>(FloatField(*ScopedCharacter, "invulnerability", 0.25));
            ScopedDesc.character.Armor = static_cast<float>(FloatField(*ScopedCharacter, "armor", 0.0));
            ScopedDesc.character.Team = static_cast<std::uint32_t>(IntField(*ScopedCharacter, "team", 0));
            ScopedDesc.character.WeaponArchetype =
                static_cast<std::uint32_t>(IntField(*ScopedCharacter, "weapon", 0));
        }
        if (const toml::table* ScopedSprite = (*ScopedEntity)["sprite"].as_table(); ScopedSprite != nullptr)
        {
            ScopedDesc.flags |= PrefabHasSprite;
            ScopedDesc.sprite.Size = VectorField(*ScopedSprite, "size", Vector2D{32.0f, 32.0f});
            ScopedDesc.sprite.UVMin = VectorField(*ScopedSprite, "uv_min", Vector2D{});
            ScopedDesc.sprite.UVMax = VectorField(*ScopedSprite, "uv_max", Vector2D{1.0f, 1.0f});
            ScopedDesc.sprite.Layer = static_cast<std::uint32_t>(IntField(*ScopedSprite, "layer", 0));
            ScopedDesc.sprite.Color = Color32{static_cast<std::uint32_t>(IntField(*ScopedSprite, "color", 0xFFFFFFFFu))};
            ScopedDesc.sprite.Flags = static_cast<std::uint32_t>(IntField(*ScopedSprite, "flags", 0));
        }
    }
    return true;
}

bool PrefabSystem::LoadTomlFile(std::string_view InPath, PrefabAsset& OutAsset, std::string* OutError)
{
    std::ifstream ScopedStream(std::string(InPath), std::ios::binary);
    if (!ScopedStream)
    {
        ErrorFile(OutError, "cannot open prefab TOML");
        return false;
    }
    const std::string ScopedText((std::istreambuf_iterator<char>(ScopedStream)), std::istreambuf_iterator<char>());
    return LoadToml(ScopedText, OutAsset, OutError);
}

std::size_t PrefabSystem::BinarySize(const PrefabAsset& InAsset) noexcept
{
    return sizeof(PrefabBinaryHeader) + static_cast<std::size_t>(InAsset.count) * sizeof(PrefabEntityDesc);
}

bool PrefabSystem::SaveBinary(const PrefabAsset& InAsset, std::span<std::byte> OutBytes,
                              std::size_t* OutBytesUsed) noexcept
{
    const std::size_t ScopedSize = BinarySize(InAsset);
    if (OutBytes.size() < ScopedSize || InAsset.count > kPrefabMaxEntities)
    {
        return false;
    }
    PrefabBinaryHeader ScopedHeader{};
    std::memcpy(ScopedHeader.magic, kBinaryMagic, sizeof(kBinaryMagic));
    ScopedHeader.version = kBinaryVersion;
    ScopedHeader.count = InAsset.count;
    std::memcpy(ScopedHeader.name, InAsset.name, sizeof(ScopedHeader.name));
    ScopedHeader.payloadBytes = static_cast<std::uint32_t>(InAsset.count * sizeof(PrefabEntityDesc));
    std::memcpy(OutBytes.data(), &ScopedHeader, sizeof(ScopedHeader));
    std::memcpy(OutBytes.data() + sizeof(ScopedHeader), InAsset.entities.data(), ScopedHeader.payloadBytes);
    if (OutBytesUsed != nullptr)
    {
        *OutBytesUsed = ScopedSize;
    }
    return true;
}

bool PrefabSystem::LoadBinary(std::span<const std::byte> InBytes, PrefabAsset& OutAsset,
                              std::string* OutError) noexcept
{
    OutAsset = PrefabAsset{};
    if (InBytes.size() < sizeof(PrefabBinaryHeader))
    {
        ErrorFile(OutError, "prefab binary is smaller than its header");
        return false;
    }
    PrefabBinaryHeader ScopedHeader{};
    std::memcpy(&ScopedHeader, InBytes.data(), sizeof(ScopedHeader));
    if (std::memcmp(ScopedHeader.magic, kBinaryMagic, sizeof(kBinaryMagic)) != 0 ||
        ScopedHeader.version != kBinaryVersion || ScopedHeader.count > kPrefabMaxEntities ||
        ScopedHeader.payloadBytes != ScopedHeader.count * sizeof(PrefabEntityDesc) ||
        InBytes.size() < sizeof(ScopedHeader) + ScopedHeader.payloadBytes)
    {
        ErrorFile(OutError, "invalid prefab binary header");
        return false;
    }
    OutAsset.count = ScopedHeader.count;
    std::memcpy(OutAsset.name, ScopedHeader.name, sizeof(OutAsset.name));
    std::memcpy(OutAsset.entities.data(), InBytes.data() + sizeof(ScopedHeader), ScopedHeader.payloadBytes);
    return true;
}

bool PrefabSystem::LoadFlatBuffers(std::span<const std::byte> InBytes, PrefabAsset& OutAsset, std::string* OutError)
{
    OutAsset = PrefabAsset{};
    if (InBytes.size() < 4)
    {
        ErrorFile(OutError, "prefab FlatBuffer is empty");
        return false;
    }
    flatbuffers::Verifier ScopedVerifier(reinterpret_cast<const std::uint8_t*>(InBytes.data()), InBytes.size());
    if (!Fby::Prefab::VerifyPrefabBuffer(ScopedVerifier))
    {
        ErrorFile(OutError, "prefab FlatBuffer verification failed");
        return false;
    }
    const Fby::Prefab::Prefab* ScopedPrefab = Fby::Prefab::GetPrefab(InBytes.data());
    if (ScopedPrefab == nullptr || ScopedPrefab->entities() == nullptr ||
        ScopedPrefab->entities()->size() > kPrefabMaxEntities)
    {
        ErrorFile(OutError, "prefab FlatBuffer has no entities or too many");
        return false;
    }
    CopyText(OutAsset.name, sizeof(OutAsset.name), ScopedPrefab->name() != nullptr ? ScopedPrefab->name()->string_view()
                                                                                   : std::string_view{});
    for (const Fby::Prefab::PrefabEntity* ScopedEntity : *ScopedPrefab->entities())
    {
        if (ScopedEntity == nullptr)
        {
            continue;
        }
        PrefabEntityDesc& ScopedDesc = OutAsset.entities[OutAsset.count++];
        if (ScopedEntity->name() != nullptr)
        {
            CopyText(ScopedDesc.name, sizeof(ScopedDesc.name), ScopedEntity->name()->string_view());
        }
        ScopedDesc.flags = ScopedEntity->flags();
        if (const Fby::Prefab::Transform2D* ScopedTransform = ScopedEntity->transform(); ScopedTransform != nullptr)
        {
            ScopedDesc.transform.Position = Vector2D{ScopedTransform->position().x(), ScopedTransform->position().y()};
            ScopedDesc.transform.Scale = Vector2D{ScopedTransform->scale().x(), ScopedTransform->scale().y()};
            ScopedDesc.transform.Rotation = ScopedTransform->rotation();
        }
        if (const Fby::Prefab::PhysicsBodyDesc* ScopedPhysics = ScopedEntity->physics(); ScopedPhysics != nullptr)
        {
            ScopedDesc.physics.Type = ScopedPhysics->type();
            ScopedDesc.physics.Shape = ScopedPhysics->shape();
            ScopedDesc.physics.HalfExtents =
                Vector2D{ScopedPhysics->halfExtents().x(), ScopedPhysics->halfExtents().y()};
            ScopedDesc.physics.Radius = ScopedPhysics->radius();
            ScopedDesc.physics.Mass = ScopedPhysics->mass();
            ScopedDesc.physics.Density = ScopedPhysics->density();
            ScopedDesc.physics.Friction = ScopedPhysics->friction();
            ScopedDesc.physics.Restitution = ScopedPhysics->restitution();
            ScopedDesc.physics.LinearDamping = ScopedPhysics->linearDamping();
            ScopedDesc.physics.AngularDamping = ScopedPhysics->angularDamping();
            ScopedDesc.physics.GravityScale = ScopedPhysics->gravityScale();
            ScopedDesc.physics.CategoryBits = ScopedPhysics->categoryBits();
            ScopedDesc.physics.MaskBits = ScopedPhysics->maskBits();
            ScopedDesc.physics.GroupIndex = ScopedPhysics->groupIndex();
            ScopedDesc.physics.Sensor = ScopedPhysics->sensor();
            ScopedDesc.physics.ContactEvents = ScopedPhysics->contactEvents();
            ScopedDesc.physics.FixedRotation = ScopedPhysics->fixedRotation();
            ScopedDesc.physics.Bullet = ScopedPhysics->bullet();
        }
        if (const Fby::Prefab::CharacterDesc* ScopedCharacter = ScopedEntity->character(); ScopedCharacter != nullptr)
        {
            ScopedDesc.character.MaxHealth = ScopedCharacter->maxHealth();
            ScopedDesc.character.MaxStamina = ScopedCharacter->maxStamina();
            ScopedDesc.character.MoveSpeed = ScopedCharacter->moveSpeed();
            ScopedDesc.character.Acceleration = ScopedCharacter->acceleration();
            ScopedDesc.character.JumpVelocity = ScopedCharacter->jumpVelocity();
            ScopedDesc.character.Damage = ScopedCharacter->damage();
            ScopedDesc.character.FireInterval = ScopedCharacter->fireInterval();
            ScopedDesc.character.ProjectileSpeed = ScopedCharacter->projectileSpeed();
            ScopedDesc.character.InvulnerabilitySeconds = ScopedCharacter->invulnerabilitySeconds();
            ScopedDesc.character.Armor = ScopedCharacter->armor();
            ScopedDesc.character.Team = ScopedCharacter->team();
            ScopedDesc.character.WeaponArchetype = ScopedCharacter->weaponArchetype();
        }
        if (const Fby::Prefab::Sprite2D* ScopedSprite = ScopedEntity->sprite(); ScopedSprite != nullptr)
        {
            ScopedDesc.sprite.Size = Vector2D{ScopedSprite->size().x(), ScopedSprite->size().y()};
            ScopedDesc.sprite.UVMin = Vector2D{ScopedSprite->uvMin().x(), ScopedSprite->uvMin().y()};
            ScopedDesc.sprite.UVMax = Vector2D{ScopedSprite->uvMax().x(), ScopedSprite->uvMax().y()};
            ScopedDesc.sprite.Layer = ScopedSprite->layer();
            ScopedDesc.sprite.Color = Color32{ScopedSprite->color()};
            ScopedDesc.sprite.Flags = ScopedSprite->flags();
        }
    }
    return OutAsset.count > 0;
}

bool PrefabSystem::Spawn(Ecs::Registry& InRegistry, Ecs::CommandBuffer& InCommands, const PrefabAsset& InAsset,
                         Vector2D InOrigin, std::span<Entity> OutEntities, PrefabOverrides InOverrides) noexcept
{
    if (InAsset.count == 0 || InAsset.count > kPrefabMaxEntities)
    {
        return false;
    }
    const std::uint32_t ScopedStart = InCommands.Count();
    std::array<Entity, kPrefabMaxEntities> ScopedEntities{};
    for (std::uint32_t ScopedI = 0; ScopedI < InAsset.count; ++ScopedI)
    {
        ScopedEntities[ScopedI] = InRegistry.create();
    }
    const auto ScopedRollback = [&]() noexcept
    {
        for (std::uint32_t ScopedI = 0; ScopedI < InAsset.count; ++ScopedI)
        {
            InRegistry.destroy(ScopedEntities[ScopedI]);
        }
        InCommands.Truncate(ScopedStart);
    };

    for (std::uint32_t ScopedI = 0; ScopedI < InAsset.count; ++ScopedI)
    {
        const PrefabEntityDesc& ScopedDesc = InAsset.entities[ScopedI];
        Transform2D ScopedTransform = ScopedDesc.transform;
        ScopedTransform.Position = InOrigin + ScopedDesc.transform.Position;
        const PrefabEntityOverride* ScopedOverride = nullptr;
        for (const PrefabEntityOverride& ScopedCandidate : InOverrides.entries)
        {
            if (ScopedCandidate.entityIndex == ScopedI)
            {
                ScopedOverride = &ScopedCandidate;
                break;
            }
        }
        if (ScopedOverride != nullptr)
        {
            if (ScopedOverride->overridePosition)
            {
                ScopedTransform.Position = InOrigin + ScopedOverride->position;
            }
            if (ScopedOverride->overrideScale)
            {
                ScopedTransform.Scale = ScopedOverride->scale;
            }
            if (ScopedOverride->overrideRotation)
            {
                ScopedTransform.Rotation = ScopedOverride->rotation;
            }
        }
        if (!InCommands.Emplace(ScopedEntities[ScopedI], ScopedTransform))
        {
            ScopedRollback();
            return false;
        }
        if ((ScopedDesc.flags & PrefabHasPhysics) != 0)
        {
            RigidBody2D ScopedBody{};
            ScopedBody.Type = static_cast<BodyType>(std::min(ScopedDesc.physics.Type, 2u));
            ScopedBody.GravityScale = ScopedDesc.physics.GravityScale;
            ScopedBody.LinearDamping = ScopedDesc.physics.LinearDamping;
            ScopedBody.AngularDamping = ScopedDesc.physics.AngularDamping;
            ScopedBody.Flags = RigidBodyAwake | RigidBodyEnabled;
            if (ScopedDesc.physics.FixedRotation)
            {
                ScopedBody.Flags |= RigidBodyFixedRotation;
            }
            if (ScopedDesc.physics.Bullet)
            {
                ScopedBody.Flags |= RigidBodyBullet;
            }
            Collider2D ScopedCollider{};
            ScopedCollider.HalfExtents = ScopedDesc.physics.Shape == static_cast<std::uint32_t>(PhysicsShapeKind::Circle)
                                             ? Vector2D{ScopedDesc.physics.Radius, ScopedDesc.physics.Radius}
                                             : ScopedDesc.physics.HalfExtents;
            ScopedCollider.Density = ScopedDesc.physics.Density;
            ScopedCollider.Friction = ScopedDesc.physics.Friction;
            ScopedCollider.Restitution = ScopedDesc.physics.Restitution;
            ScopedCollider.CategoryBits = ScopedDesc.physics.CategoryBits;
            ScopedCollider.MaskBits = ScopedDesc.physics.MaskBits;
            ScopedCollider.Sensor = ScopedDesc.physics.Sensor;
            ScopedCollider.ContactEvents = ScopedDesc.physics.ContactEvents;
            if (!InCommands.Emplace(ScopedEntities[ScopedI], ScopedDesc.physics) ||
                !InCommands.Emplace(ScopedEntities[ScopedI], ScopedBody) ||
                !InCommands.Emplace(ScopedEntities[ScopedI], ScopedCollider))
            {
                ScopedRollback();
                return false;
            }
        }
        if ((ScopedDesc.flags & PrefabHasCharacter) != 0 &&
            !InCommands.Emplace(ScopedEntities[ScopedI], ScopedDesc.character))
        {
            ScopedRollback();
            return false;
        }
        if ((ScopedDesc.flags & PrefabHasSprite) != 0)
        {
            Sprite2D ScopedSprite = ScopedDesc.sprite;
            if (ScopedOverride != nullptr && ScopedOverride->overrideColor)
            {
                ScopedSprite.Color = ScopedOverride->color;
            }
            if (!InCommands.Emplace(ScopedEntities[ScopedI], ScopedSprite))
            {
                ScopedRollback();
                return false;
            }
        }
    }
    if (!OutEntities.empty())
    {
        for (std::uint32_t ScopedI = 0; ScopedI < InAsset.count && ScopedI < OutEntities.size(); ++ScopedI)
        {
            OutEntities[ScopedI] = ScopedEntities[ScopedI];
        }
    }
    return true;
}

bool PrefabSystem::SpawnToml(Ecs::Registry& InRegistry, Ecs::CommandBuffer& InCommands, std::string_view InToml,
                             Vector2D InOrigin, std::span<Entity> OutEntities, std::string* OutError,
                             PrefabOverrides InOverrides)
{
    PrefabAsset ScopedAsset{};
    if (!LoadToml(InToml, ScopedAsset, OutError))
    {
        return false;
    }
    return Spawn(InRegistry, InCommands, ScopedAsset, InOrigin, OutEntities, InOverrides);
}
}
