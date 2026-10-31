// FemboyEngine2D - semantic scene description for BlindMode (Narrative Tree).
#pragma once

#include "Engine/Core/Types.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/Schedule.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Fby::Ui
{

enum class NarrativeLocale : std::uint8_t
{
    English = 0,
    Russian = 1,
};

enum class NarrativeArchetype : std::uint8_t
{
    Unknown = 0,
    Actor,
    Character,
    PhysicsBody,
    Cloth,
    Rope,
    Visual,
    Camera,
    Count,
};

// Semantic branches. A node points at one group; the group is the branch of the
enum class NarrativeGroup : std::uint8_t
{
    Scene = 0,
    Actors,
    Physics,
    Visuals,
    Other,
    Count,
};

struct NarrativeNode
{
    Entity entity = entt::null;
    std::uint8_t archetype = static_cast<std::uint8_t>(NarrativeArchetype::Unknown);
    std::uint8_t group = static_cast<std::uint8_t>(NarrativeGroup::Other);
    std::uint16_t reserved = 0;
};

class NarrativeTree
{
public:
    static constexpr std::uint32_t kMaxNodes = 64;

    NarrativeTree() = default;

    void SetLocale(NarrativeLocale InLocale) noexcept
    {
        MLocale = InLocale;
    }
    // BCP-47-ish: any code starting with "ru" selects Russian, everything else
    void SetLocaleFromCode(std::string_view InCode) noexcept;
    [[nodiscard]] NarrativeLocale Locale() const noexcept
    {
        return MLocale;
    }

    // One registry walk. Entities beyond kMaxNodes are ignored deterministically
    void Build(Ecs::Registry& InRegistry) noexcept;

    [[nodiscard]] std::uint32_t NodeCount() const noexcept
    {
        return MCount;
    }
    [[nodiscard]] const NarrativeNode* Find(Entity InEntity) const noexcept;
    [[nodiscard]] const NarrativeNode* FindByIndex(std::uint32_t InIndex) const noexcept
    {
        return InIndex < MCount ? &MNodes[InIndex] : nullptr;
    }
    [[nodiscard]] NarrativeArchetype ArchetypeOf(Entity InEntity) const noexcept;

    // Bounded, localized descriptions. Return the number of characters written
    std::uint32_t DescribeEntity(Ecs::Registry& InRegistry, Entity InEntity, char* OutText,
                                 std::size_t InCapacity) const noexcept;
    std::uint32_t DescribeStateChange(Ecs::Registry& InRegistry, Entity InEntity, std::uint32_t InBit, bool InGained,
                                      char* OutText, std::size_t InCapacity) const noexcept;
    std::uint32_t DescribeContact(Ecs::Registry& InRegistry, Entity InA, Entity InB, std::uint32_t InContactType,
                                  float InSpeed, char* OutText, std::size_t InCapacity) const noexcept;
    std::uint32_t DescribeScene(Ecs::Registry& InRegistry, char* OutText, std::size_t InCapacity) const noexcept;
    std::uint32_t DescribeHierarchy(Ecs::Registry& InRegistry, Entity InEntity, char* OutText,
                                    std::size_t InCapacity) const noexcept;

    [[nodiscard]] static std::string_view StateName(std::uint32_t InBit, NarrativeLocale InLocale) noexcept;
    [[nodiscard]] static std::string_view GroupName(NarrativeGroup InGroup, NarrativeLocale InLocale) noexcept;
    [[nodiscard]] static std::string_view ArchetypeName(NarrativeArchetype InArchetype,
                                                        NarrativeLocale InLocale) noexcept;

private:
    [[nodiscard]] NarrativeArchetype Classify(Ecs::Registry& InRegistry, Entity InEntity) const noexcept;
    [[nodiscard]] static NarrativeGroup GroupOf(NarrativeArchetype InArchetype) noexcept;

    std::array<NarrativeNode, kMaxNodes> MNodes{};
    std::uint32_t MCount = 0;
    NarrativeLocale MLocale = NarrativeLocale::English;
};
}
