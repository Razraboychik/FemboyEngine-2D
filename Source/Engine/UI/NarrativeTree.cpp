// FemboyEngine2D - Narrative Tree implementation.
#include "Engine/UI/NarrativeTree.hpp"

#include "Engine/ECS/Skeleton.hpp"
#include "Engine/ECS/SpriteAnimation.hpp"
#include "Engine/Physics/Cloth.hpp"
#include "Engine/Physics/Physics.hpp"
#include "Engine/Physics/PhysicsJoints.hpp"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace Fby::Ui
{
namespace
{
struct NarrativeTemplates
{
    const char* scene = nullptr;
    const char* entity = nullptr;
    const char* namedEntity = nullptr;
    const char* stateChange = nullptr;
    const char* namedStateChange = nullptr;
    const char* contact = nullptr;
    const char* hierarchy = nullptr;
    const char* namedHierarchy = nullptr;
    const char* gained = nullptr;
    const char* lost = nullptr;
    const char* speed = nullptr;
    const char* asleep = nullptr;
    const char* awake = nullptr;
    const char* sensor = nullptr;
    const char* hidden = nullptr;
    const char* const* groups = nullptr;
    const char* const* archetypes = nullptr;
    const char* const* states = nullptr;
    const char* const* contacts = nullptr;
};

const char* const kGroupsEn[] = {"scene", "actors", "physics", "visuals", "other"};
const char* const kArchetypesEn[] = {"object", "actor", "character", "physics body",
                                     "cloth", "rope link", "visual", "camera"};
const char* const kStatesEn[] = {"grounded", "airborne", "jumping", "crouching", "attacking", "blocking",
                                 "dashing",  "stunned",  "dead",    "invulnerable", "aiming", "reloading",
                                 "interacting", "hidden", "frozen", "carrying"};
const char* const kContactsEn[] = {"touch", "touch end", "hit"};

// Russian templates are written as universal character names so the style gate
const char* const kGroupsRu[] = {
    "\u0421\u0446\u0435\u043D\u0430",
    "\u0410\u043A\u0442\u0451\u0440\u044B",
    "\u0424\u0438\u0437\u0438\u043A\u0430",
    "\u0412\u0438\u0437\u0443\u0430\u043B",
    "\u041F\u0440\u043E\u0447\u0435\u0435",
};
const char* const kArchetypesRu[] = {
    "\u041E\u0431\u044A\u0435\u043A\u0442",
    "\u0410\u043A\u0442\u0451\u0440",
    "\u041F\u0435\u0440\u0441\u043E\u043D\u0430\u0436",
    "\u0424\u0438\u0437\u0438\u0447\u0435\u0441\u043A\u043E\u0435 \u0442\u0435\u043B\u043E",
    "\u0422\u043A\u0430\u043D\u044C",
    "\u0417\u0432\u0435\u043D\u043E \u0432\u0435\u0440\u0451\u0432\u043A\u0438",
    "\u0412\u0438\u0437\u0443\u0430\u043B\u044C\u043D\u044B\u0439 \u043E\u0431\u044A\u0435\u043A\u0442",
    "\u041A\u0430\u043C\u0435\u0440\u0430",
};
const char* const kStatesRu[] = {
    "\u043D\u0430 \u0437\u0435\u043C\u043B\u0435",
    "\u0432 \u0432\u043E\u0437\u0434\u0443\u0445\u0435",
    "\u043F\u0440\u044B\u0436\u043E\u043A",
    "\u043F\u0440\u0438\u0441\u0435\u0434",
    "\u0430\u0442\u0430\u043A\u0430",
    "\u0431\u043B\u043E\u043A",
    "\u0440\u044B\u0432\u043E\u043A",
    "\u043E\u0433\u043B\u0443\u0448\u0451\u043D",
    "\u043C\u0451\u0440\u0442\u0432",
    "\u043D\u0435\u0443\u044F\u0437\u0432\u0438\u043C",
    "\u043F\u0440\u0438\u0446\u0435\u043B",
    "\u043F\u0435\u0440\u0435\u0437\u0430\u0440\u044F\u0434\u043A\u0430",
    "\u0432\u0437\u0430\u0438\u043C\u043E\u0434\u0435\u0439\u0441\u0442\u0432\u0438\u0435",
    "\u0441\u043A\u0440\u044B\u0442",
    "\u0437\u0430\u043C\u043E\u0440\u043E\u0436\u0435\u043D",
    "\u043D\u0435\u0441\u0451\u0442",
};
const char* const kContactsRu[] = {
    "\u043A\u0430\u0441\u0430\u043D\u0438\u0435",
    "\u043A\u043E\u043D\u0435\u0446 \u043A\u0430\u0441\u0430\u043D\u0438\u044F",
    "\u0443\u0434\u0430\u0440",
};

const NarrativeTemplates& Templates(NarrativeLocale InLocale) noexcept
{
    static const NarrativeTemplates ScopedEnglish{
        "Scene: %u entities, %u actors, %u physical, %u visual",
        "%s #%u, position %.0f, %.0f",
        "%.*s, position %.0f, %.0f",
        "%s #%u: %s %s",
        "%.*s: %s %s",
        "Contact: %s and %s, speed %.0f",
        "Scene > %s > %s #%u",
        "Scene > %s > %.*s",
        "on",
        "off",
        ", speed %.0f",
        ", asleep",
        ", awake",
        ", sensor",
        ", hidden",
        kGroupsEn,
        kArchetypesEn,
        kStatesEn,
        kContactsEn,
    };
    static const NarrativeTemplates ScopedRussian{
        "\u0421\u0446\u0435\u043D\u0430: \u043E\u0431\u044A\u0435\u043A\u0442\u043E\u0432 %u, "
        "\u0430\u043A\u0442\u0451\u0440\u043E\u0432 %u, \u0444\u0438\u0437\u0438\u0447\u0435\u0441\u043A\u0438\u0445 %u, "
        "\u0432\u0438\u0437\u0443\u0430\u043B\u044C\u043D\u044B\u0445 %u",
        "%s #%u, \u043F\u043E\u0437\u0438\u0446\u0438\u044F %.0f, %.0f",
        "%.*s, \u043F\u043E\u0437\u0438\u0446\u0438\u044F %.0f, %.0f",
        "%s #%u: %s %s",
        "%.*s: %s %s",
        "\u041A\u043E\u043D\u0442\u0430\u043A\u0442: %s \u0438 %s, \u0441\u043A\u043E\u0440\u043E\u0441\u0442\u044C %.0f",
        "Scene > %s > %s #%u",
        "Scene > %s > %.*s",
        "\u0432\u043A\u043B\u044E\u0447\u0435\u043D\u043E",
        "\u0432\u044B\u043A\u043B\u044E\u0447\u0435\u043D\u043E",
        ", \u0441\u043A\u043E\u0440\u043E\u0441\u0442\u044C %.0f",
        ", \u0441\u043F\u0438\u0442",
        ", \u0430\u043A\u0442\u0438\u0432\u043D\u043E",
        ", \u0441\u0435\u043D\u0441\u043E\u0440",
        ", \u0441\u043A\u0440\u044B\u0442",
        kGroupsRu,
        kArchetypesRu,
        kStatesRu,
        kContactsRu,
    };
    return InLocale == NarrativeLocale::Russian ? ScopedRussian : ScopedEnglish;
}

[[nodiscard]] std::uint32_t WriteText(char* OutText, std::size_t InCapacity, const char* InFormat, ...) noexcept
{
    if (OutText == nullptr || InCapacity == 0 || InFormat == nullptr)
    {
        return 0;
    }
    va_list ScopedArguments;
    va_start(ScopedArguments, InFormat);
    const int ScopedWritten = std::vsnprintf(OutText, InCapacity, InFormat, ScopedArguments);
    va_end(ScopedArguments);
    if (ScopedWritten < 0)
    {
        OutText[0] = '\0';
        return 0;
    }
    return static_cast<std::uint32_t>(std::min<std::size_t>(static_cast<std::size_t>(ScopedWritten), InCapacity - 1));
}

void AppendText(char* OutText, std::size_t InCapacity, std::uint32_t* InLength, const char* InFormat, ...) noexcept
{
    if (OutText == nullptr || InCapacity == 0 || InLength == nullptr || *InLength >= InCapacity - 1 ||
        InFormat == nullptr)
    {
        return;
    }
    va_list ScopedArguments;
    va_start(ScopedArguments, InFormat);
    const int ScopedWritten =
        std::vsnprintf(OutText + *InLength, InCapacity - *InLength, InFormat, ScopedArguments);
    va_end(ScopedArguments);
    if (ScopedWritten <= 0)
    {
        return;
    }
    const std::size_t ScopedAvailable = InCapacity - 1 - *InLength;
    *InLength += static_cast<std::uint32_t>(std::min<std::size_t>(static_cast<std::size_t>(ScopedWritten),
                                                                     ScopedAvailable));
}

[[nodiscard]] std::uint32_t EntityId(Entity InEntity) noexcept
{
    return static_cast<std::uint32_t>(entt::to_integral(InEntity));
}

// Authored name, or an empty view. The component is fixed-capacity and may not
[[nodiscard]] std::string_view NarrativeLabel(Ecs::Registry& InRegistry, Entity InEntity) noexcept
{
    const auto* ScopedLabel = InRegistry.try_get<NarrativeLabelComponent>(InEntity);
    if (ScopedLabel == nullptr)
    {
        return {};
    }
    std::size_t ScopedLength = 0;
    while (ScopedLength < sizeof(ScopedLabel->Name) && ScopedLabel->Name[ScopedLength] != '\0')
    {
        ++ScopedLength;
    }
    return std::string_view(ScopedLabel->Name, ScopedLength);
}
}

void NarrativeTree::SetLocaleFromCode(std::string_view InCode) noexcept
{
    if (InCode.size() >= 2 && (InCode[0] == 'r' || InCode[0] == 'R') && (InCode[1] == 'u' || InCode[1] == 'U'))
    {
        MLocale = NarrativeLocale::Russian;
    }
    else
    {
        MLocale = NarrativeLocale::English;
    }
}

NarrativeGroup NarrativeTree::GroupOf(NarrativeArchetype InArchetype) noexcept
{
    switch (InArchetype)
    {
        case NarrativeArchetype::Actor:
        case NarrativeArchetype::Character:
            return NarrativeGroup::Actors;
        case NarrativeArchetype::PhysicsBody:
        case NarrativeArchetype::Cloth:
        case NarrativeArchetype::Rope:
            return NarrativeGroup::Physics;
        case NarrativeArchetype::Visual:
        case NarrativeArchetype::Camera:
            return NarrativeGroup::Visuals;
        case NarrativeArchetype::Unknown:
        case NarrativeArchetype::Count:
            break;
    }
    return NarrativeGroup::Other;
}

NarrativeArchetype NarrativeTree::Classify(Ecs::Registry& InRegistry, Entity InEntity) const noexcept
{
    // A camera with a sprite is still a camera: the most specific role wins.
    if (InRegistry.try_get<Camera2D>(InEntity) != nullptr)
    {
        return NarrativeArchetype::Camera;
    }
    if (InRegistry.try_get<JointLinkComponent>(InEntity) != nullptr)
    {
        return NarrativeArchetype::Rope;
    }
    if (InRegistry.try_get<ClothComponent>(InEntity) != nullptr)
    {
        return NarrativeArchetype::Cloth;
    }
    if (InRegistry.try_get<SkeletonComponent>(InEntity) != nullptr ||
        InRegistry.try_get<SkinComponent>(InEntity) != nullptr)
    {
        return NarrativeArchetype::Character;
    }
    if (InRegistry.try_get<RigidBody2D>(InEntity) != nullptr ||
        InRegistry.try_get<Collider2D>(InEntity) != nullptr)
    {
        return NarrativeArchetype::PhysicsBody;
    }
    if (InRegistry.try_get<Sprite2D>(InEntity) != nullptr || InRegistry.try_get<SpriteAnimationComponent>(InEntity) != nullptr)
    {
        return NarrativeArchetype::Visual;
    }
    if (InRegistry.try_get<GameplayState>(InEntity) != nullptr || InRegistry.try_get<Velocity>(InEntity) != nullptr)
    {
        return NarrativeArchetype::Actor;
    }
    return NarrativeArchetype::Unknown;
}

void NarrativeTree::Build(Ecs::Registry& InRegistry) noexcept
{
    MCount = 0;
    auto ScopedView = InRegistry.view<Transform2D>();
    for (const Entity ScopedEntity : ScopedView)
    {
        if (MCount >= kMaxNodes)
        {
            break;
        }
        const NarrativeArchetype ScopedArchetype = Classify(InRegistry, ScopedEntity);
        NarrativeNode& ScopedNode = MNodes[MCount++];
        ScopedNode.entity = ScopedEntity;
        ScopedNode.archetype = static_cast<std::uint8_t>(ScopedArchetype);
        ScopedNode.group = static_cast<std::uint8_t>(GroupOf(ScopedArchetype));
    }
}

const NarrativeNode* NarrativeTree::Find(Entity InEntity) const noexcept
{
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MCount; ++ScopedIndex)
    {
        if (MNodes[ScopedIndex].entity == InEntity)
        {
            return &MNodes[ScopedIndex];
        }
    }
    return nullptr;
}

NarrativeArchetype NarrativeTree::ArchetypeOf(Entity InEntity) const noexcept
{
    const NarrativeNode* ScopedNode = Find(InEntity);
    return ScopedNode != nullptr ? static_cast<NarrativeArchetype>(ScopedNode->archetype) : NarrativeArchetype::Unknown;
}

std::string_view NarrativeTree::StateName(std::uint32_t InBit, NarrativeLocale InLocale) noexcept
{
    const NarrativeTemplates& ScopedTemplates = Templates(InLocale);
    return InBit < 16 ? std::string_view(ScopedTemplates.states[InBit]) : std::string_view("state");
}

std::string_view NarrativeTree::GroupName(NarrativeGroup InGroup, NarrativeLocale InLocale) noexcept
{
    const NarrativeTemplates& ScopedTemplates = Templates(InLocale);
    const auto ScopedIndex = static_cast<std::size_t>(InGroup);
    return ScopedIndex < static_cast<std::size_t>(NarrativeGroup::Count)
               ? std::string_view(ScopedTemplates.groups[ScopedIndex])
               : std::string_view("scene");
}

std::string_view NarrativeTree::ArchetypeName(NarrativeArchetype InArchetype, NarrativeLocale InLocale) noexcept
{
    const NarrativeTemplates& ScopedTemplates = Templates(InLocale);
    const auto ScopedIndex = static_cast<std::size_t>(InArchetype);
    return ScopedIndex < static_cast<std::size_t>(NarrativeArchetype::Count)
               ? std::string_view(ScopedTemplates.archetypes[ScopedIndex])
               : std::string_view("object");
}

std::uint32_t NarrativeTree::DescribeEntity(Ecs::Registry& InRegistry, Entity InEntity, char* OutText,
                                            std::size_t InCapacity) const noexcept
{
    const NarrativeTemplates& ScopedTemplates = Templates(MLocale);
    const NarrativeArchetype ScopedArchetype =
        Find(InEntity) != nullptr ? ArchetypeOf(InEntity) : Classify(InRegistry, InEntity);
    const auto* ScopedTransform = InRegistry.try_get<Transform2D>(InEntity);
    const Vector2D ScopedPosition = ScopedTransform != nullptr ? ScopedTransform->Position : Vector2D{};
    const std::string_view ScopedLabel = NarrativeLabel(InRegistry, InEntity);
    std::uint32_t ScopedLength = 0;
    if (!ScopedLabel.empty())
    {
        // The human name replaces the archetype and the raw entity id entirely.
        ScopedLength = WriteText(OutText, InCapacity, ScopedTemplates.namedEntity,
                                 static_cast<int>(ScopedLabel.size()), ScopedLabel.data(),
                                 static_cast<double>(ScopedPosition.X), static_cast<double>(ScopedPosition.Y));
    }
    else
    {
        ScopedLength = WriteText(OutText, InCapacity, ScopedTemplates.entity,
                                 ScopedTemplates.archetypes[static_cast<std::size_t>(ScopedArchetype)],
                                 EntityId(InEntity), static_cast<double>(ScopedPosition.X),
                                 static_cast<double>(ScopedPosition.Y));
    }

    if (const auto* ScopedVelocity = InRegistry.try_get<Velocity>(InEntity); ScopedVelocity != nullptr)
    {
        const float ScopedSpeed = std::sqrt(ScopedVelocity->Value.X * ScopedVelocity->Value.X +
                                            ScopedVelocity->Value.Y * ScopedVelocity->Value.Y);
        AppendText(OutText, InCapacity, &ScopedLength, ScopedTemplates.speed, static_cast<double>(ScopedSpeed));
    }
    if (const auto* ScopedState = InRegistry.try_get<GameplayState>(InEntity); ScopedState != nullptr)
    {
        for (std::uint32_t ScopedBit = 0; ScopedBit < 16; ++ScopedBit)
        {
            if ((ScopedState->Bits & (1ull << ScopedBit)) != 0)
            {
                AppendText(OutText, InCapacity, &ScopedLength, ", %s", ScopedTemplates.states[ScopedBit]);
                break;
            }
        }
    }
    if (const auto* ScopedBody = InRegistry.try_get<RigidBody2D>(InEntity); ScopedBody != nullptr)
    {
        AppendText(OutText, InCapacity, &ScopedLength, "%s",
                   (ScopedBody->Flags & RigidBodyAwake) != 0 ? ScopedTemplates.awake : ScopedTemplates.asleep);
    }
    if (const auto* ScopedCollider = InRegistry.try_get<Collider2D>(InEntity);
        ScopedCollider != nullptr && ScopedCollider->Sensor)
    {
        AppendText(OutText, InCapacity, &ScopedLength, "%s", ScopedTemplates.sensor);
    }
    if (InRegistry.all_of<Hidden>(InEntity))
    {
        AppendText(OutText, InCapacity, &ScopedLength, "%s", ScopedTemplates.hidden);
    }
    return ScopedLength;
}

std::uint32_t NarrativeTree::DescribeStateChange(Ecs::Registry& InRegistry, Entity InEntity, std::uint32_t InBit,
                                                 bool InGained, char* OutText, std::size_t InCapacity) const noexcept
{
    const NarrativeTemplates& ScopedTemplates = Templates(MLocale);
    const NarrativeArchetype ScopedArchetype =
        Find(InEntity) != nullptr ? ArchetypeOf(InEntity) : Classify(InRegistry, InEntity);
    const std::string_view ScopedLabel = NarrativeLabel(InRegistry, InEntity);
    if (!ScopedLabel.empty())
    {
        return WriteText(OutText, InCapacity, ScopedTemplates.namedStateChange,
                         static_cast<int>(ScopedLabel.size()), ScopedLabel.data(),
                         InGained ? ScopedTemplates.gained : ScopedTemplates.lost,
                         InBit < 16 ? ScopedTemplates.states[InBit] : "state");
    }
    return WriteText(OutText, InCapacity, ScopedTemplates.stateChange,
                     ScopedTemplates.archetypes[static_cast<std::size_t>(ScopedArchetype)], EntityId(InEntity),
                     InGained ? ScopedTemplates.gained : ScopedTemplates.lost,
                     InBit < 16 ? ScopedTemplates.states[InBit] : "state");
}

std::uint32_t NarrativeTree::DescribeContact(Ecs::Registry& InRegistry, Entity InA, Entity InB,
                                             std::uint32_t InContactType, float InSpeed, char* OutText,
                                             std::size_t InCapacity) const noexcept
{
    const NarrativeTemplates& ScopedTemplates = Templates(MLocale);
    const auto ScopedSubject = [this, &InRegistry](Entity InEntity, char* OutBuffer, std::size_t InBufferCapacity)
    {
        const std::string_view ScopedLabel = NarrativeLabel(InRegistry, InEntity);
        if (!ScopedLabel.empty())
        {
            (void)WriteText(OutBuffer, InBufferCapacity, "%.*s", static_cast<int>(ScopedLabel.size()),
                            ScopedLabel.data());
            return;
        }
        const NarrativeArchetype ScopedArchetype =
            Find(InEntity) != nullptr ? ArchetypeOf(InEntity) : Classify(InRegistry, InEntity);
        (void)WriteText(OutBuffer, InBufferCapacity, "%s #%u",
                        Templates(MLocale).archetypes[static_cast<std::size_t>(ScopedArchetype)],
                        EntityId(InEntity));
    };
    char ScopedA[48]{};
    char ScopedB[48]{};
    ScopedSubject(InA, ScopedA, sizeof(ScopedA));
    ScopedSubject(InB, ScopedB, sizeof(ScopedB));
    const char* ScopedContact = InContactType < 3 ? ScopedTemplates.contacts[InContactType] : ScopedTemplates.contacts[0];
    (void)ScopedContact;
    return WriteText(OutText, InCapacity, ScopedTemplates.contact, ScopedA, ScopedB,
                     static_cast<double>(InSpeed));
}

std::uint32_t NarrativeTree::DescribeScene(Ecs::Registry& InRegistry, char* OutText,
                                           std::size_t InCapacity) const noexcept
{
    (void)InRegistry;
    const NarrativeTemplates& ScopedTemplates = Templates(MLocale);
    std::uint32_t ScopedActors = 0;
    std::uint32_t ScopedPhysics = 0;
    std::uint32_t ScopedVisuals = 0;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MCount; ++ScopedIndex)
    {
        switch (static_cast<NarrativeGroup>(MNodes[ScopedIndex].group))
        {
            case NarrativeGroup::Actors:
                ++ScopedActors;
                break;
            case NarrativeGroup::Physics:
                ++ScopedPhysics;
                break;
            case NarrativeGroup::Visuals:
                ++ScopedVisuals;
                break;
            case NarrativeGroup::Scene:
            case NarrativeGroup::Other:
            case NarrativeGroup::Count:
                break;
        }
    }
    return WriteText(OutText, InCapacity, ScopedTemplates.scene, MCount, ScopedActors, ScopedPhysics, ScopedVisuals);
}

std::uint32_t NarrativeTree::DescribeHierarchy(Ecs::Registry& InRegistry, Entity InEntity, char* OutText,
                                               std::size_t InCapacity) const noexcept
{
    const NarrativeTemplates& ScopedTemplates = Templates(MLocale);
    const NarrativeArchetype ScopedArchetype =
        Find(InEntity) != nullptr ? ArchetypeOf(InEntity) : Classify(InRegistry, InEntity);
    const std::string_view ScopedLabel = NarrativeLabel(InRegistry, InEntity);
    if (!ScopedLabel.empty())
    {
        return WriteText(OutText, InCapacity, ScopedTemplates.namedHierarchy,
                         ScopedTemplates.groups[static_cast<std::size_t>(GroupOf(ScopedArchetype))],
                         static_cast<int>(ScopedLabel.size()), ScopedLabel.data());
    }
    return WriteText(OutText, InCapacity, ScopedTemplates.hierarchy,
                     ScopedTemplates.groups[static_cast<std::size_t>(GroupOf(ScopedArchetype))],
                     ScopedTemplates.archetypes[static_cast<std::size_t>(ScopedArchetype)], EntityId(InEntity));
}
}
