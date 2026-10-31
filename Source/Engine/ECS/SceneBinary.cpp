#include "Engine/ECS/Scene.hpp"

#include "scene_generated.h"

#include <flatbuffers/flatbuffers.h>
#include <flatbuffers/verifier.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace Fby::Scene
{
namespace
{
[[nodiscard]] std::string Hex(std::uint64_t InValue)
{
    char ScopedBuffer[17]{};
    std::snprintf(ScopedBuffer, sizeof(ScopedBuffer), "%016llx", static_cast<unsigned long long>(InValue));
    return ScopedBuffer;
}
}

std::vector<std::byte> EncodeSceneBinary(const SceneData& InScene)
{
    flatbuffers::FlatBufferBuilder ScopedBuilder(4096);

    std::vector<flatbuffers::Offset<Fb::Entity>> ScopedEntityOffsets;
    ScopedEntityOffsets.reserve(InScene.entities.size());

    for (const SceneEntity& ScopedEntity : InScene.entities)
    {
        std::vector<flatbuffers::Offset<Fb::Component>> ScopedComponentOffsets;
        ScopedComponentOffsets.reserve(ScopedEntity.components.size());
        for (const SceneComponent& ScopedComponent : ScopedEntity.components)
        {
            if (ScopedComponent.type == nullptr)
            {
                continue;
            }
            const auto ScopedType =
                ScopedBuilder.CreateString(ScopedComponent.type->name.data(), ScopedComponent.type->name.size());
            const auto ScopedData = ScopedBuilder.CreateVector(
                reinterpret_cast<const std::uint8_t*>(ScopedComponent.data.data()), ScopedComponent.data.size());
            ScopedComponentOffsets.push_back(
                Fb::CreateComponent(ScopedBuilder, ScopedType, ScopedComponent.type->layoutHash, ScopedData));
        }

        const auto ScopedName = ScopedEntity.name.empty() ? flatbuffers::Offset<flatbuffers::String>{}
                                                          : ScopedBuilder.CreateString(ScopedEntity.name);
        ScopedEntityOffsets.push_back(
            Fb::CreateEntity(ScopedBuilder, ScopedName, ScopedBuilder.CreateVector(ScopedComponentOffsets)));
    }

    const auto ScopedName = InScene.meta.name.empty() ? flatbuffers::Offset<flatbuffers::String>{}
                                                      : ScopedBuilder.CreateString(InScene.meta.name);
    const auto ScopedAuthor = InScene.meta.author.empty() ? flatbuffers::Offset<flatbuffers::String>{}
                                                          : ScopedBuilder.CreateString(InScene.meta.author);
    const auto ScopedRoot = Fb::CreateScene(ScopedBuilder, InScene.meta.version, ScopedName, ScopedAuthor,
                                            ScopedBuilder.CreateVector(ScopedEntityOffsets));
    ScopedBuilder.Finish(ScopedRoot, "FBSC");

    std::vector<std::byte> ScopedOut(ScopedBuilder.GetSize());
    std::memcpy(ScopedOut.data(), ScopedBuilder.GetBufferPointer(), ScopedBuilder.GetSize());
    return ScopedOut;
}

std::expected<SceneData, std::string> DecodeSceneBinary(std::span<const std::byte> InBinary,
                                                        const ComponentRegistry& InComponents)
{
    if (InBinary.size() < 8)
    {
        return std::unexpected("binary scene is too small");
    }

    flatbuffers::Verifier ScopedVerifier(reinterpret_cast<const std::uint8_t*>(InBinary.data()), InBinary.size());
    if (!Fb::VerifySceneBuffer(ScopedVerifier))
    {
        return std::unexpected("invalid FlatBuffers scene (verifier rejected the buffer)");
    }

    const Fb::Scene* ScopedRoot = Fb::GetScene(InBinary.data());
    if (ScopedRoot == nullptr)
    {
        return std::unexpected("binary scene has no root table");
    }
    if (ScopedRoot->version() == 0 || ScopedRoot->version() > kSceneVersion)
    {
        return std::unexpected("unsupported binary scene version " + std::to_string(ScopedRoot->version()));
    }

    SceneData ScopedScene;
    ScopedScene.meta.version = ScopedRoot->version();
    if (const flatbuffers::String* ScopedName = ScopedRoot->name())
    {
        ScopedScene.meta.name.assign(ScopedName->c_str(), ScopedName->size());
    }
    if (const flatbuffers::String* ScopedAuthor = ScopedRoot->author())
    {
        ScopedScene.meta.author.assign(ScopedAuthor->c_str(), ScopedAuthor->size());
    }

    if (const flatbuffers::Vector<flatbuffers::Offset<Fb::Entity>>* ScopedEntities = ScopedRoot->entities())
    {
        ScopedScene.entities.reserve(ScopedEntities->size());
        for (const Fb::Entity* ScopedEntity : *ScopedEntities)
        {
            if (ScopedEntity == nullptr)
            {
                continue;
            }
            SceneEntity ScopedRecord;
            if (const flatbuffers::String* ScopedName = ScopedEntity->name())
            {
                ScopedRecord.name.assign(ScopedName->c_str(), ScopedName->size());
            }
            if (const auto* ScopedStoredComponents = ScopedEntity->components())
            {
                ScopedRecord.components.reserve(ScopedStoredComponents->size());
                for (const Fb::Component* ScopedStored : *ScopedStoredComponents)
                {
                    if (ScopedStored == nullptr || ScopedStored->type() == nullptr)
                    {
                        return std::unexpected("binary scene contains a component without a type");
                    }
                    const std::string_view ScopedType(ScopedStored->type()->c_str(), ScopedStored->type()->size());
                    const ComponentOps* ScopedOps = InComponents.Find(ScopedType);
                    if (ScopedOps == nullptr)
                    {
                        return std::unexpected("binary scene uses unknown component '" + std::string(ScopedType) + "'");
                    }
                    if (ScopedStored->layout_hash() != ScopedOps->layoutHash)
                    {
                        return std::unexpected("layout mismatch for component '" + std::string(ScopedType) +
                                               "': file " + Hex(ScopedStored->layout_hash()) + ", engine " +
                                               Hex(ScopedOps->layoutHash) + " - recook the scene");
                    }
                    const auto* ScopedBytes = ScopedStored->data();
                    const std::size_t ScopedSize = ScopedBytes != nullptr ? ScopedBytes->size() : 0;
                    if (ScopedSize != ScopedOps->size)
                    {
                        return std::unexpected("component '" + std::string(ScopedType) + "' payload is " +
                                               std::to_string(ScopedSize) + " bytes, engine layout is " +
                                               std::to_string(ScopedOps->size));
                    }
                    SceneComponent ScopedComponent;
                    ScopedComponent.type = ScopedOps;
                    ScopedComponent.data.assign(ScopedOps->size, std::byte{0});
                    if (ScopedBytes != nullptr && ScopedSize > 0)
                    {
                        std::memcpy(ScopedComponent.data.data(), ScopedBytes->data(), ScopedSize);
                    }
                    ScopedRecord.components.push_back(std::move(ScopedComponent));
                }
            }
            ScopedScene.entities.push_back(std::move(ScopedRecord));
        }
    }
    return ScopedScene;
}
}
