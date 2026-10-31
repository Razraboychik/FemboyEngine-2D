// FemboyEngine2D - scene and prefab serialization.
#pragma once

#include "Engine/ECS/ComponentRegistry.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Fby
{
class VirtualFileSystem;
namespace Assets
{
class AssetDatabase;
}
}

namespace Fby::Scene
{

inline constexpr std::uint32_t kSceneVersion = 1;
inline constexpr std::uint32_t kInvalidEntityIndex = 0xFFFFFFFFu;

struct SceneMeta
{
    std::string name = "untitled";
    std::string author;
    std::uint32_t version = kSceneVersion;
};

struct SceneComponent
{
    const ComponentOps* type = nullptr;
    // Raw component bytes. Entity-typed fields already hold serialized indices.
    std::vector<std::byte> data;
};

struct SceneEntity
{
    // Optional label, not an ECS component
    std::string name;
    std::vector<SceneComponent> components;
};

struct SceneData
{
    SceneMeta meta;
    std::vector<SceneEntity> entities;
};

// serialized index -> live entity. Built by ApplyScene and returned to the
struct EntityRemap
{
    std::vector<Entity> entities;
    std::unordered_map<Entity, std::uint32_t> reverse;

    [[nodiscard]] Entity Resolve(std::uint32_t InSerializedIndex) const noexcept;
    [[nodiscard]] std::uint32_t IndexOf(Entity InEntity) const noexcept;
    [[nodiscard]] bool Empty() const noexcept
    {
        return entities.empty();
    }
    [[nodiscard]] std::size_t Size() const noexcept
    {
        return entities.size();
    }
};

struct SceneLoadOptions
{
    // Scene load: replace the world. Prefab instantiation: false, append.
    bool clearExisting = true;
    // Rebuild EntityRemap::reverse even when the caller does not need IndexOf.
    bool buildReverse = true;
    // Optional asset database. When set, every SkeletonAssetRef is resolved
    VirtualFileSystem* assets = nullptr;
    Assets::AssetDatabase* assetDb = nullptr;
};

using SceneLoadResult = std::expected<EntityRemap, std::string>;

[[nodiscard]] SceneData CaptureScene(const Registry& InRegistry, const ComponentRegistry& InComponents,
                                     SceneMeta InMeta = {});
// Applies a scene in two passes: create every entity, then patch component
[[nodiscard]] SceneLoadResult ApplyScene(Registry& InRegistry, const ComponentRegistry& InComponents,
                                         const SceneData& InScene, SceneLoadOptions InOptions = {});

[[nodiscard]] std::string EncodeSceneToml(const SceneData& InScene);
[[nodiscard]] std::expected<SceneData, std::string> DecodeSceneToml(std::string_view InToml,
                                                                    const ComponentRegistry& InComponents);

[[nodiscard]] std::vector<std::byte> EncodeSceneBinary(const SceneData& InScene);
[[nodiscard]] std::expected<SceneData, std::string> DecodeSceneBinary(std::span<const std::byte> InBinary,
                                                                      const ComponentRegistry& InComponents);

[[nodiscard]] std::string SaveSceneToml(const Registry& InRegistry, const ComponentRegistry& InComponents,
                                        SceneMeta InMeta = {});
[[nodiscard]] SceneLoadResult LoadSceneToml(Registry& InRegistry, const ComponentRegistry& InComponents,
                                            std::string_view InToml, SceneLoadOptions InOptions = {});
[[nodiscard]] std::vector<std::byte> SaveSceneBinary(const Registry& InRegistry, const ComponentRegistry& InComponents,
                                                     SceneMeta InMeta = {});
[[nodiscard]] SceneLoadResult LoadSceneBinary(Registry& InRegistry, const ComponentRegistry& InComponents,
                                              std::span<const std::byte> InBinary, SceneLoadOptions InOptions = {});
// Appends without clearing: this is the prefab path.
[[nodiscard]] SceneLoadResult InstantiatePrefabToml(Registry& InRegistry, const ComponentRegistry& InComponents,
                                                    std::string_view InToml);
[[nodiscard]] SceneLoadResult InstantiatePrefabBinary(Registry& InRegistry, const ComponentRegistry& InComponents,
                                                      std::span<const std::byte> InBinary);

[[nodiscard]] bool SaveSceneFileToml(const std::filesystem::path& InPath, const Registry& InRegistry,
                                     const ComponentRegistry& InComponents, SceneMeta InMeta = {},
                                     std::string* InError = nullptr);
[[nodiscard]] bool SaveSceneFileBinary(const std::filesystem::path& InPath, const Registry& InRegistry,
                                       const ComponentRegistry& InComponents, SceneMeta InMeta = {},
                                       std::string* InError = nullptr);
[[nodiscard]] SceneLoadResult LoadSceneFileToml(Registry& InRegistry, const ComponentRegistry& InComponents,
                                                const std::filesystem::path& InPath, SceneLoadOptions InOptions = {});
[[nodiscard]] SceneLoadResult LoadSceneFileBinary(Registry& InRegistry, const ComponentRegistry& InComponents,
                                                  const std::filesystem::path& InPath, SceneLoadOptions InOptions = {});
}
