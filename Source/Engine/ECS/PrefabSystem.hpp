// FemboyEngine2D - data-driven prefab spawner.
#pragma once

#include "Engine/ECS/CommandBuffer.hpp"
#include "Engine/ECS/Descriptors.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace Fby
{

// Per-instance overrides applied after the prefab record is copied. Entity
// indices refer to PrefabAsset::entities; unset fields keep the prefab value.
struct PrefabEntityOverride
{
    std::uint32_t entityIndex = 0;
    bool overridePosition = false;
    Vector2D position{};
    bool overrideScale = false;
    Vector2D scale{1.0f, 1.0f};
    bool overrideRotation = false;
    float rotation = 0.0f;
    bool overrideColor = false;
    Color32 color = Color32::White();
};

struct PrefabOverrides
{
    std::span<const PrefabEntityOverride> entries{};
};

class PrefabSystem
{
public:
    static constexpr std::uint32_t kBinaryVersion = 1;
    static constexpr char kBinaryMagic[8] = {'F', 'B', 'Y', 'P', 'R', 'F', '0', '1'};

    // Asset import: strict subset of TOML used by prefabs. Returns false with a
    [[nodiscard]] static bool LoadToml(std::string_view InText, PrefabAsset& OutAsset, std::string* OutError = nullptr);
    [[nodiscard]] static bool LoadTomlFile(std::string_view InPath, PrefabAsset& OutAsset,
                                           std::string* OutError = nullptr);
    [[nodiscard]] static bool LoadBinary(std::span<const std::byte> InBytes, PrefabAsset& OutAsset,
                                         std::string* OutError = nullptr) noexcept;
    // Reads the generated FlatBuffers Prefab table. This is the shipping asset
    [[nodiscard]] static bool LoadFlatBuffers(std::span<const std::byte> InBytes, PrefabAsset& OutAsset,
                                              std::string* OutError = nullptr);
    [[nodiscard]] static std::size_t BinarySize(const PrefabAsset& InAsset) noexcept;
    [[nodiscard]] static bool SaveBinary(const PrefabAsset& InAsset, std::span<std::byte> OutBytes,
                                         std::size_t* OutBytesUsed = nullptr) noexcept;

    // Instantiates a prefab in the caller's registry. Returns false and leaves the
    [[nodiscard]] static bool Spawn(Ecs::Registry& InRegistry, Ecs::CommandBuffer& InCommands,
                                    const PrefabAsset& InAsset, Vector2D InOrigin,
                                    std::span<Entity> OutEntities = {}, PrefabOverrides InOverrides = {}) noexcept;

    // Convenience for a tool/editor path: parse then spawn.
    [[nodiscard]] static bool SpawnToml(Ecs::Registry& InRegistry, Ecs::CommandBuffer& InCommands,
                                        std::string_view InToml, Vector2D InOrigin,
                                        std::span<Entity> OutEntities = {}, std::string* OutError = nullptr,
                                        PrefabOverrides InOverrides = {});
};
}
