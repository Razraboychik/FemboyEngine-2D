#include "Engine/ECS/Scene.hpp"

#include "Engine/Assets/AssetDatabase.hpp"
#include "Engine/Assets/VFS.hpp"
#include "Engine/Core/Log.hpp"
#include "Engine/ECS/SkeletonAssetRef.hpp"
#include "Engine/ECS/SkeletonAsset.hpp"
#include "Engine/ECS/SceneField.hpp"
#include "Engine/ECS/RegistryOps.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>

namespace Fby::Scene
{
namespace
{
[[nodiscard]] bool ValidEntityFieldSize(const Reflect::FieldDesc& InField) noexcept
{
    switch (InField.kind)
    {
        case Reflect::FieldKind::Float:
            return InField.size == sizeof(float);
        case Reflect::FieldKind::Int32:
        case Reflect::FieldKind::UInt32:
            return InField.size == 1 || InField.size == 2 || InField.size == 4 || InField.size == 8;
        case Reflect::FieldKind::Bool:
            return InField.size == sizeof(bool);
        case Reflect::FieldKind::Vector2D:
            return InField.size == sizeof(Vector2D);
        case Reflect::FieldKind::Color32:
            return InField.size == sizeof(std::uint32_t);
        case Reflect::FieldKind::Entity:
            return InField.size == sizeof(std::uint32_t);
        case Reflect::FieldKind::String:
            return InField.size > 0;
        case Reflect::FieldKind::UInt64:
            return InField.size == sizeof(std::uint64_t);
        case Reflect::FieldKind::Int128:
            return InField.size == sizeof(BigNum::Int128);
        case Reflect::FieldKind::Decimal:
            return InField.size == sizeof(BigNum::Decimal128);
    }
    return false;
}

[[nodiscard]] bool ReadTextFile(const std::filesystem::path& InPath, std::string* InText, std::string* InError)
{
    std::ifstream ScopedStream(InPath, std::ios::binary);
    if (!ScopedStream)
    {
        if (InError != nullptr)
        {
            *InError = "cannot open " + InPath.string();
        }
        return false;
    }
    *InText = std::string(std::istreambuf_iterator<char>(ScopedStream), std::istreambuf_iterator<char>());
    if (ScopedStream.bad())
    {
        if (InError != nullptr)
        {
            *InError = "read failed: " + InPath.string();
        }
        return false;
    }
    return true;
}

[[nodiscard]] bool ValidateSkeletonAssets(const SceneData& InScene, VirtualFileSystem* InVfs,
                                          Assets::AssetDatabase* InAssetDb, std::string* InError)
{
    if (InVfs == nullptr)
    {
        return true;
    }
    for (std::size_t ScopedEntityIndex = 0; ScopedEntityIndex < InScene.entities.size(); ++ScopedEntityIndex)
    {
        for (const SceneComponent& ScopedComponent : InScene.entities[ScopedEntityIndex].components)
        {
            if (ScopedComponent.type == nullptr || ScopedComponent.type->name != "SkeletonAssetRef")
            {
                continue;
            }
            if (ScopedComponent.data.size() != sizeof(SkeletonAssetRef))
            {
                *InError = "SkeletonAssetRef on entity " + std::to_string(ScopedEntityIndex) + " has the wrong size";
                return false;
            }
            SkeletonAssetRef ScopedRef{};
            std::memcpy(&ScopedRef, ScopedComponent.data.data(), sizeof(ScopedRef));
            if (ScopedRef.Empty())
            {
                continue;
            }
            if (InAssetDb == nullptr)
            {
                *InError = "entity " + std::to_string(ScopedEntityIndex) +
                           " has a SkeletonAssetRef but SceneLoadOptions::assetDb is null";
                return false;
            }
            const std::string_view ScopedPath = InAssetDb->SourcePath(ScopedRef.Asset);
            if (ScopedPath.empty())
            {
                *InError =
                    "entity " + std::to_string(ScopedEntityIndex) + " references an unregistered skeleton AssetId";
                return false;
            }
            const auto ScopedBytes = InVfs->Read(ScopedPath);
            if (!ScopedBytes)
            {
                *InError = "entity " + std::to_string(ScopedEntityIndex) + " references missing skeleton asset '" +
                           std::string(ScopedPath) + "'";
                return false;
            }
            const auto ScopedView = Skeleton::DecodeSkeletonAsset(*ScopedBytes);
            if (!ScopedView)
            {
                *InError = "entity " + std::to_string(ScopedEntityIndex) + " has an invalid skeleton asset '" +
                           std::string(ScopedPath) + "': " + ScopedView.error();
                return false;
            }
            if (ScopedRef.ContentHash != 0 &&
                ScopedRef.ContentHash != static_cast<std::uint32_t>(ScopedView->header->contentHash))
            {
                LogWarn("scene",
                        "skeleton asset '{}' is newer than the scene reference (hash {:08x} vs {:08x}); "
                        "loading the asset anyway",
                        ScopedPath, ScopedRef.ContentHash, static_cast<std::uint32_t>(ScopedView->header->contentHash));
            }
        }
    }
    return true;
}

[[nodiscard]] bool InstantiateSkeletonRefs(Registry& InRegistry, const EntityRemap& InRemap, VirtualFileSystem* InVfs,
                                           Assets::AssetDatabase* InAssetDb, std::string* InError)
{
    if (InVfs == nullptr || InAssetDb == nullptr)
    {
        return true;
    }
    for (const Entity ScopedEntity : InRemap.entities)
    {
        const auto* ScopedRef = InRegistry.try_get<SkeletonAssetRef>(ScopedEntity);
        if (ScopedRef == nullptr || ScopedRef->Empty() || InRegistry.all_of<SkeletonComponent>(ScopedEntity))
        {
            continue;
        }
        const std::string_view ScopedPath = InAssetDb->SourcePath(ScopedRef->Asset);
        const auto ScopedBytes = InVfs->Read(ScopedPath);
        if (!ScopedBytes)
        {
            *InError = "skeleton asset disappeared while applying the scene: " + std::string(ScopedPath);
            return false;
        }
        const auto ScopedView = Skeleton::DecodeSkeletonAsset(*ScopedBytes);
        if (!ScopedView)
        {
            *InError = "invalid skeleton asset '" + std::string(ScopedPath) + "': " + ScopedView.error();
            return false;
        }

        auto& ScopedSkeleton = InRegistry.emplace<SkeletonComponent>(ScopedEntity);
        // A bone-only asset (an IK target, a procedural chain) still needs its
        const bool ScopedWantSkin = ScopedView->HasSkin() && (ScopedRef->Flags & kSkeletonAssetRefSpawnSkin) != 0;
        SkinComponent ScopedScratchSkin;
        SkinComponent& ScopedSkin =
            ScopedWantSkin ? InRegistry.get_or_emplace<SkinComponent>(ScopedEntity) : ScopedScratchSkin;
        Skeleton::SkeletonNameTable ScopedNames;
        Skeleton::FesklSprite ScopedSprite;
        Skeleton::InstantiateSkeletonAsset(*ScopedView, ScopedSkeleton, ScopedSkin, ScopedNames, ScopedSprite);
    }
    return true;
}

[[nodiscard]] bool WriteFile(const std::filesystem::path& InPath, std::span<const std::byte> InBytes,
                             std::string* InError)
{
    std::error_code ScopedCode;
    if (InPath.has_parent_path())
    {
        std::filesystem::create_directories(InPath.parent_path(), ScopedCode);
    }
    std::ofstream ScopedStream(InPath, std::ios::binary | std::ios::trunc);
    if (!ScopedStream)
    {
        if (InError != nullptr)
        {
            *InError = "cannot write " + InPath.string();
        }
        return false;
    }
    ScopedStream.write(reinterpret_cast<const char*>(InBytes.data()), static_cast<std::streamsize>(InBytes.size()));
    if (!ScopedStream)
    {
        if (InError != nullptr)
        {
            *InError = "write failed: " + InPath.string();
        }
        return false;
    }
    return true;
}
}

Entity EntityRemap::Resolve(std::uint32_t InSerializedIndex) const noexcept
{
    if (InSerializedIndex == kInvalidEntityIndex || InSerializedIndex >= entities.size())
    {
        return entt::null;
    }
    return entities[InSerializedIndex];
}

std::uint32_t EntityRemap::IndexOf(Entity InEntity) const noexcept
{
    const auto ScopedIt = reverse.find(InEntity);
    return ScopedIt != reverse.end() ? ScopedIt->second : kInvalidEntityIndex;
}

SceneData CaptureScene(const Registry& InRegistry, const ComponentRegistry& InComponents, SceneMeta InMeta)
{
    std::vector<Entity> ScopedEntities;
    const auto* ScopedStorage = InRegistry.storage<Entity>();
    if (ScopedStorage != nullptr)
    {
        for (const Entity ScopedEntity : *ScopedStorage)
        {
            ScopedEntities.push_back(ScopedEntity);
        }
    }
    std::sort(ScopedEntities.begin(), ScopedEntities.end(),
              [](Entity InLeft, Entity InRight) { return entt::to_integral(InLeft) < entt::to_integral(InRight); });

    std::unordered_map<Entity, std::uint32_t> ScopedIndices;
    ScopedIndices.reserve(ScopedEntities.size());
    for (std::uint32_t ScopedI = 0; ScopedI < ScopedEntities.size(); ++ScopedI)
    {
        ScopedIndices.emplace(ScopedEntities[ScopedI], ScopedI);
    }

    SceneData ScopedScene;
    ScopedScene.meta = std::move(InMeta);
    ScopedScene.entities.reserve(ScopedEntities.size());

    for (const Entity ScopedEntity : ScopedEntities)
    {
        SceneEntity ScopedRecord;
        for (const ComponentOps& ScopedOps : InComponents.All())
        {
            if (!ScopedOps.serializable || !ScopedOps.has(InRegistry, ScopedEntity))
            {
                continue;
            }
            const void* ScopedComponent = ScopedOps.get(InRegistry, ScopedEntity);
            if (!ScopedOps.empty && ScopedComponent == nullptr)
            {
                continue;
            }

            SceneComponent ScopedStored;
            ScopedStored.type = &ScopedOps;
            ScopedStored.data.assign(ScopedOps.size, std::byte{0});
            if (!ScopedOps.empty)
            {
                std::memcpy(ScopedStored.data.data(), ScopedComponent, ScopedOps.size);
            }

            for (const Reflect::FieldDesc& ScopedField : ScopedOps.fields)
            {
                if (ScopedField.kind != Reflect::FieldKind::Entity)
                {
                    continue;
                }
                const Entity ScopedTarget = Detail::ReadEntity(ScopedStored.data.data(), ScopedField);
                const auto ScopedIt = ScopedIndices.find(ScopedTarget);
                const std::uint32_t ScopedSerialized =
                    ScopedIt != ScopedIndices.end() ? ScopedIt->second : kInvalidEntityIndex;
                (void)Detail::WriteEntityIndex(ScopedStored.data.data(), ScopedField, ScopedSerialized);
            }
            ScopedRecord.components.push_back(std::move(ScopedStored));
        }
        ScopedScene.entities.push_back(std::move(ScopedRecord));
    }
    return ScopedScene;
}

SceneLoadResult ApplyScene(Registry& InRegistry, const ComponentRegistry& InComponents, const SceneData& InScene,
                           SceneLoadOptions InOptions)
{
    // Validate before mutating anything. A prefab that fails halfway through
    for (std::size_t ScopedEntityIndex = 0; ScopedEntityIndex < InScene.entities.size(); ++ScopedEntityIndex)
    {
        for (const SceneComponent& ScopedComponent : InScene.entities[ScopedEntityIndex].components)
        {
            if (ScopedComponent.type == nullptr)
            {
                return std::unexpected("entity " + std::to_string(ScopedEntityIndex) + " has an untyped component");
            }
            const ComponentOps* ScopedOps = InComponents.Find(ScopedComponent.type->name);
            if (ScopedOps == nullptr)
            {
                return std::unexpected("unknown component '" + std::string(ScopedComponent.type->name) +
                                       "' on entity " + std::to_string(ScopedEntityIndex));
            }
            if (ScopedComponent.data.size() != ScopedOps->size)
            {
                return std::unexpected("component '" + std::string(ScopedOps->name) + "' on entity " +
                                       std::to_string(ScopedEntityIndex) + " is " +
                                       std::to_string(ScopedComponent.data.size()) + " bytes, engine layout is " +
                                       std::to_string(ScopedOps->size));
            }
            for (const Reflect::FieldDesc& ScopedField : ScopedOps->fields)
            {
                if (!ValidEntityFieldSize(ScopedField))
                {
                    return std::unexpected("component '" + std::string(ScopedOps->name) + "' field '" +
                                           std::string(ScopedField.name) + "' has an unsupported size " +
                                           std::to_string(ScopedField.size));
                }
            }
        }
    }

    // Skeleton assets are validated before the world is touched for the same
    std::string ScopedSkeletonError;
    if (!ValidateSkeletonAssets(InScene, InOptions.assets, InOptions.assetDb, &ScopedSkeletonError))
    {
        return std::unexpected(std::move(ScopedSkeletonError));
    }

    if (InOptions.clearExisting)
    {
        DestroyAllEntities(InRegistry);
    }

    EntityRemap ScopedRemap;
    ScopedRemap.entities.reserve(InScene.entities.size());
    for (std::size_t ScopedI = 0; ScopedI < InScene.entities.size(); ++ScopedI)
    {
        ScopedRemap.entities.push_back(InRegistry.create());
    }
    if (InOptions.buildReverse)
    {
        ScopedRemap.reverse.reserve(ScopedRemap.entities.size());
        for (std::uint32_t ScopedI = 0; ScopedI < ScopedRemap.entities.size(); ++ScopedI)
        {
            ScopedRemap.reverse.emplace(ScopedRemap.entities[ScopedI], ScopedI);
        }
    }

    for (std::size_t ScopedEntityIndex = 0; ScopedEntityIndex < InScene.entities.size(); ++ScopedEntityIndex)
    {
        const Entity ScopedEntity = ScopedRemap.entities[ScopedEntityIndex];
        for (const SceneComponent& ScopedComponent : InScene.entities[ScopedEntityIndex].components)
        {
            const ComponentOps* ScopedOps = InComponents.Find(ScopedComponent.type->name);
            if (ScopedOps == nullptr)
            {
                // Pre-validated, unreachable
                continue;
            }
            ScopedOps->emplaceBytes(InRegistry, ScopedEntity, ScopedComponent.data.data());

            if (ScopedOps->empty)
            {
                continue;
            }
            void* ScopedTarget = ScopedOps->getMutable(InRegistry, ScopedEntity);
            if (ScopedTarget == nullptr)
            {
                return std::unexpected("failed to create component '" + std::string(ScopedOps->name) + "'");
            }
            for (const Reflect::FieldDesc& ScopedField : ScopedOps->fields)
            {
                if (ScopedField.kind != Reflect::FieldKind::Entity)
                {
                    continue;
                }
                const std::uint32_t ScopedSerialized = Detail::ReadEntityIndex(ScopedTarget, ScopedField);
                const Entity ScopedResolved = ScopedRemap.Resolve(ScopedSerialized);
                if (!Detail::WriteEntity(ScopedTarget, ScopedField, ScopedResolved))
                {
                    return std::unexpected("failed to remap entity field '" + std::string(ScopedField.name) + "'");
                }
            }
        }
    }

    std::string ScopedSkeletonInstanceError;
    if (!InstantiateSkeletonRefs(InRegistry, ScopedRemap, InOptions.assets, InOptions.assetDb,
                                 &ScopedSkeletonInstanceError))
    {
        return std::unexpected(std::move(ScopedSkeletonInstanceError));
    }

    LogInfo("scene", "applied '{}': {} entities{}", InScene.meta.name, ScopedRemap.entities.size(),
            InOptions.clearExisting ? " (world replaced)" : " (appended)");
    return ScopedRemap;
}

std::string SaveSceneToml(const Registry& InRegistry, const ComponentRegistry& InComponents, SceneMeta InMeta)
{
    return EncodeSceneToml(CaptureScene(InRegistry, InComponents, std::move(InMeta)));
}

SceneLoadResult LoadSceneToml(Registry& InRegistry, const ComponentRegistry& InComponents, std::string_view InToml,
                              SceneLoadOptions InOptions)
{
    std::expected<SceneData, std::string> ScopedDecoded = DecodeSceneToml(InToml, InComponents);
    if (!ScopedDecoded)
    {
        return std::unexpected(std::move(ScopedDecoded.error()));
    }
    return ApplyScene(InRegistry, InComponents, *ScopedDecoded, InOptions);
}

std::vector<std::byte> SaveSceneBinary(const Registry& InRegistry, const ComponentRegistry& InComponents,
                                       SceneMeta InMeta)
{
    return EncodeSceneBinary(CaptureScene(InRegistry, InComponents, std::move(InMeta)));
}

SceneLoadResult LoadSceneBinary(Registry& InRegistry, const ComponentRegistry& InComponents,
                                std::span<const std::byte> InBinary, SceneLoadOptions InOptions)
{
    std::expected<SceneData, std::string> ScopedDecoded = DecodeSceneBinary(InBinary, InComponents);
    if (!ScopedDecoded)
    {
        return std::unexpected(std::move(ScopedDecoded.error()));
    }
    return ApplyScene(InRegistry, InComponents, *ScopedDecoded, InOptions);
}

SceneLoadResult InstantiatePrefabToml(Registry& InRegistry, const ComponentRegistry& InComponents,
                                      std::string_view InToml)
{
    SceneLoadOptions ScopedOptions;
    ScopedOptions.clearExisting = false;
    return LoadSceneToml(InRegistry, InComponents, InToml, ScopedOptions);
}

SceneLoadResult InstantiatePrefabBinary(Registry& InRegistry, const ComponentRegistry& InComponents,
                                        std::span<const std::byte> InBinary)
{
    SceneLoadOptions ScopedOptions;
    ScopedOptions.clearExisting = false;
    return LoadSceneBinary(InRegistry, InComponents, InBinary, ScopedOptions);
}

bool SaveSceneFileToml(const std::filesystem::path& InPath, const Registry& InRegistry,
                       const ComponentRegistry& InComponents, SceneMeta InMeta, std::string* InError)
{
    const std::string ScopedText = SaveSceneToml(InRegistry, InComponents, std::move(InMeta));
    return WriteFile(InPath, std::as_bytes(std::span<const char>(ScopedText.data(), ScopedText.size())), InError);
}

bool SaveSceneFileBinary(const std::filesystem::path& InPath, const Registry& InRegistry,
                         const ComponentRegistry& InComponents, SceneMeta InMeta, std::string* InError)
{
    const std::vector<std::byte> ScopedBinary = SaveSceneBinary(InRegistry, InComponents, std::move(InMeta));
    if (ScopedBinary.empty())
    {
        if (InError != nullptr)
        {
            *InError = "scene encoder produced an empty buffer";
        }
        return false;
    }
    return WriteFile(InPath, ScopedBinary, InError);
}

SceneLoadResult LoadSceneFileToml(Registry& InRegistry, const ComponentRegistry& InComponents,
                                  const std::filesystem::path& InPath, SceneLoadOptions InOptions)
{
    std::string ScopedText;
    std::string ScopedError;
    if (!ReadTextFile(InPath, &ScopedText, &ScopedError))
    {
        return std::unexpected(std::move(ScopedError));
    }
    return LoadSceneToml(InRegistry, InComponents, ScopedText, InOptions);
}

SceneLoadResult LoadSceneFileBinary(Registry& InRegistry, const ComponentRegistry& InComponents,
                                    const std::filesystem::path& InPath, SceneLoadOptions InOptions)
{
    std::string ScopedBytes;
    std::string ScopedError;
    if (!ReadTextFile(InPath, &ScopedBytes, &ScopedError))
    {
        return std::unexpected(std::move(ScopedError));
    }
    const auto* ScopedData = reinterpret_cast<const std::byte*>(ScopedBytes.data());
    return LoadSceneBinary(InRegistry, InComponents, std::span<const std::byte>(ScopedData, ScopedBytes.size()),
                           InOptions);
}
}
