// FemboyEngine2D - scene/prefab tool.
#include "Engine/Core/Log.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/ComponentRegistry.hpp"
#include "Engine/ECS/Scene.hpp"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

// A test-only component with an entity reference. It lives in the tool so the
struct FollowTarget
{
    Fby::Entity Target = entt::null;
    float Stiffness = 1.0f;
};

namespace
{
namespace fs = std::filesystem;
using Fby::Entity;
using Fby::Scene::ComponentRegistry;
using Fby::Scene::SceneData;
using Fby::Scene::SceneLoadResult;
using Fby::Scene::SceneMeta;

[[nodiscard]] bool ReadText(const fs::path& InPath, std::string* InText)
{
    std::ifstream ScopedStream(InPath, std::ios::binary);
    if (!ScopedStream)
    {
        return false;
    }
    *InText = std::string(std::istreambuf_iterator<char>(ScopedStream), std::istreambuf_iterator<char>());
    return true;
}

[[nodiscard]] bool WriteText(const fs::path& InPath, std::string_view InText)
{
    if (InPath.has_parent_path())
    {
        std::error_code ScopedCode;
        fs::create_directories(InPath.parent_path(), ScopedCode);
    }
    std::ofstream ScopedStream(InPath, std::ios::binary | std::ios::trunc);
    ScopedStream.write(InText.data(), static_cast<std::streamsize>(InText.size()));
    return static_cast<bool>(ScopedStream);
}

[[nodiscard]] bool WriteBinary(const fs::path& InPath, std::span<const std::byte> InBytes)
{
    if (InPath.has_parent_path())
    {
        std::error_code ScopedCode;
        fs::create_directories(InPath.parent_path(), ScopedCode);
    }
    std::ofstream ScopedStream(InPath, std::ios::binary | std::ios::trunc);
    ScopedStream.write(reinterpret_cast<const char*>(InBytes.data()), static_cast<std::streamsize>(InBytes.size()));
    return static_cast<bool>(ScopedStream);
}

[[nodiscard]] std::optional<std::vector<std::byte>> ReadBinary(const fs::path& InPath)
{
    std::string ScopedText;
    if (!ReadText(InPath, &ScopedText))
    {
        return std::nullopt;
    }
    std::vector<std::byte> ScopedBytes(ScopedText.size());
    std::memcpy(ScopedBytes.data(), ScopedText.data(), ScopedText.size());
    return ScopedBytes;
}

[[nodiscard]] int Usage()
{
    std::fprintf(stderr, "usage:\n"
                         "  fby_scene_tool toml2bin  <in.toml> <out.fbin>\n"
                         "  fby_scene_tool bin2toml  <in.fbin> <out.toml>\n"
                         "  fby_scene_tool roundtrip <in.toml>\n"
                         "  fby_scene_tool selftest  <output-directory>\n");
    return 2;
}

void BuildRegistry(ComponentRegistry& InRegistry)
{
    Fby::Scene::RegisterBuiltInComponents(InRegistry);
    InRegistry.Register<FollowTarget>();
}

[[nodiscard]] int TomlToBin(const fs::path& InInput, const fs::path& Output)
{
    std::string ScopedText;
    if (!ReadText(InInput, &ScopedText))
    {
        std::fprintf(stderr, "cannot read %s\n", InInput.string().c_str());
        return 1;
    }
    ComponentRegistry ScopedComponents;
    BuildRegistry(ScopedComponents);
    auto ScopedScene = Fby::Scene::DecodeSceneToml(ScopedText, ScopedComponents);
    if (!ScopedScene)
    {
        std::fprintf(stderr, "%s: %s\n", InInput.string().c_str(), ScopedScene.error().c_str());
        return 1;
    }
    const std::vector<std::byte> ScopedBinary = Fby::Scene::EncodeSceneBinary(*ScopedScene);
    if (ScopedBinary.empty() || !WriteBinary(Output, ScopedBinary))
    {
        std::fprintf(stderr, "cannot write %s\n", Output.string().c_str());
        return 1;
    }
    std::printf("cooked %s -> %s (%zu entities, %zu bytes)\n", InInput.string().c_str(), Output.string().c_str(),
                ScopedScene->entities.size(), ScopedBinary.size());
    return 0;
}

[[nodiscard]] int BinToToml(const fs::path& InInput, const fs::path& Output)
{
    const auto ScopedBinary = ReadBinary(InInput);
    if (!ScopedBinary)
    {
        std::fprintf(stderr, "cannot read %s\n", InInput.string().c_str());
        return 1;
    }
    ComponentRegistry ScopedComponents;
    BuildRegistry(ScopedComponents);
    auto ScopedScene = Fby::Scene::DecodeSceneBinary(*ScopedBinary, ScopedComponents);
    if (!ScopedScene)
    {
        std::fprintf(stderr, "%s: %s\n", InInput.string().c_str(), ScopedScene.error().c_str());
        return 1;
    }
    const std::string ScopedText = Fby::Scene::EncodeSceneToml(*ScopedScene);
    if (!WriteText(Output, ScopedText))
    {
        std::fprintf(stderr, "cannot write %s\n", Output.string().c_str());
        return 1;
    }
    std::printf("decompiled %s -> %s (%zu entities)\n", InInput.string().c_str(), Output.string().c_str(),
                ScopedScene->entities.size());
    return 0;
}

[[nodiscard]] int RoundTrip(const fs::path& InInput)
{
    std::string ScopedText;
    if (!ReadText(InInput, &ScopedText))
    {
        std::fprintf(stderr, "cannot read %s\n", InInput.string().c_str());
        return 1;
    }

    ComponentRegistry ScopedComponents;
    BuildRegistry(ScopedComponents);
    auto ScopedFirst = Fby::Scene::DecodeSceneToml(ScopedText, ScopedComponents);
    if (!ScopedFirst)
    {
        std::fprintf(stderr, "decode TOML: %s\n", ScopedFirst.error().c_str());
        return 1;
    }
    const std::string ScopedCanonicalToml = Fby::Scene::EncodeSceneToml(*ScopedFirst);

    const std::vector<std::byte> ScopedBinary = Fby::Scene::EncodeSceneBinary(*ScopedFirst);
    auto ScopedFromBinary = Fby::Scene::DecodeSceneBinary(ScopedBinary, ScopedComponents);
    if (!ScopedFromBinary)
    {
        std::fprintf(stderr, "decode binary: %s\n", ScopedFromBinary.error().c_str());
        return 1;
    }
    const std::string ScopedBinaryToml = Fby::Scene::EncodeSceneToml(*ScopedFromBinary);
    if (ScopedCanonicalToml != ScopedBinaryToml)
    {
        std::fprintf(stderr, "round-trip mismatch: TOML and FlatBuffers produce different scenes\n");
        return 1;
    }

    if (ScopedFirst->entities.size() != ScopedFromBinary->entities.size())
    {
        std::fprintf(stderr, "entity count changed across the binary round-trip\n");
        return 1;
    }

    // Apply twice into the same world and prove the ids are fresh. This is the
    entt::registry ScopedRegistry;
    const Entity ScopedSquatter = ScopedRegistry.create();
    Fby::Transform2D ScopedSquatterTransform;
    ScopedSquatterTransform.Position = {1234.0f, 5678.0f};
    ScopedRegistry.emplace<Fby::Transform2D>(ScopedSquatter, ScopedSquatterTransform);

    SceneLoadResult ScopedFirstLoad = Fby::Scene::LoadSceneToml(ScopedRegistry, ScopedComponents, ScopedText,
                                                                Fby::Scene::SceneLoadOptions{.clearExisting = false});
    if (!ScopedFirstLoad)
    {
        std::fprintf(stderr, "apply #1: %s\n", ScopedFirstLoad.error().c_str());
        return 1;
    }
    SceneLoadResult ScopedSecondLoad = Fby::Scene::LoadSceneToml(ScopedRegistry, ScopedComponents, ScopedText,
                                                                 Fby::Scene::SceneLoadOptions{.clearExisting = false});
    if (!ScopedSecondLoad)
    {
        std::fprintf(stderr, "apply #2: %s\n", ScopedSecondLoad.error().c_str());
        return 1;
    }
    if (ScopedFirstLoad->Size() != ScopedFirst->entities.size() ||
        ScopedSecondLoad->Size() != ScopedFirst->entities.size())
    {
        std::fprintf(stderr, "remap size mismatch\n");
        return 1;
    }
    for (std::uint32_t ScopedI = 0; ScopedI < ScopedFirstLoad->Size(); ++ScopedI)
    {
        const Entity ScopedA = ScopedFirstLoad->entities[ScopedI];
        if (ScopedA == ScopedSquatter || ScopedA == entt::null || !ScopedRegistry.valid(ScopedA))
        {
            std::fprintf(stderr, "load #1 reused an existing/invalid entity id\n");
            return 1;
        }
        for (std::uint32_t ScopedJ = 0; ScopedJ < ScopedSecondLoad->Size(); ++ScopedJ)
        {
            if (ScopedA == ScopedSecondLoad->entities[ScopedJ])
            {
                std::fprintf(stderr, "load #2 collided with load #1 (entity index %u)\n", ScopedI);
                return 1;
            }
        }
    }

    std::printf(
        "round-trip ok: %s: %zu entities, %zu components, %zu bytes binary\n", InInput.string().c_str(),
        ScopedFirst->entities.size(),
        [&ScopedFirst]
        {
            std::size_t ScopedCount = 0;
            for (const auto& ScopedEntity : ScopedFirst->entities)
            {
                ScopedCount += ScopedEntity.components.size();
            }
            return ScopedCount;
        }(),
        ScopedBinary.size());
    return 0;
}

// selftest writes a scene whose second entity follows the first, loads it as a
[[nodiscard]] int SelfTest(const fs::path& Output)
{
    ComponentRegistry ScopedComponents;
    BuildRegistry(ScopedComponents);

    Fby::Scene::SceneData ScopedScene;
    ScopedScene.meta.name = "selftest";
    ScopedScene.meta.author = "fby_scene_tool";
    {
        Fby::Scene::SceneEntity ScopedRoot;
        ScopedRoot.name = "root";
        Fby::Scene::SceneComponent ScopedTransform;
        ScopedTransform.type = ScopedComponents.Find("Transform2D");
        ScopedTransform.data.assign(ScopedTransform.type->size, std::byte{0});
        auto* ScopedValue = reinterpret_cast<Fby::Transform2D*>(ScopedTransform.data.data());
        ScopedValue->Position = {10.0f, 20.0f};
        ScopedRoot.components.push_back(std::move(ScopedTransform));
        ScopedScene.entities.push_back(std::move(ScopedRoot));
    }
    {
        Fby::Scene::SceneEntity ScopedFollower;
        ScopedFollower.name = "follower";
        Fby::Scene::SceneComponent ScopedTransform;
        ScopedTransform.type = ScopedComponents.Find("Transform2D");
        ScopedTransform.data.assign(ScopedTransform.type->size, std::byte{0});
        ScopedFollower.components.push_back(std::move(ScopedTransform));

        Fby::Scene::SceneComponent ScopedFollow;
        ScopedFollow.type = ScopedComponents.Find("FollowTarget");
        ScopedFollow.data.assign(ScopedFollow.type->size, std::byte{0});
        auto* ScopedValue = reinterpret_cast<FollowTarget*>(ScopedFollow.data.data());
        ScopedValue->Target = static_cast<Entity>(0);
        ScopedValue->Stiffness = 4.5f;
        ScopedFollower.components.push_back(std::move(ScopedFollow));
        ScopedScene.entities.push_back(std::move(ScopedFollower));
    }

    const std::string ScopedToml = Fby::Scene::EncodeSceneToml(ScopedScene);

    // Re-decode to prove the writer/reader pair agrees on entity indices.
    auto ScopedDecoded = Fby::Scene::DecodeSceneToml(ScopedToml, ScopedComponents);
    if (!ScopedDecoded)
    {
        std::fprintf(stderr, "selftest decode: %s\n", ScopedDecoded.error().c_str());
        return 1;
    }
    const std::vector<std::byte> ScopedBinary = Fby::Scene::EncodeSceneBinary(*ScopedDecoded);
    auto ScopedDecodedBinary = Fby::Scene::DecodeSceneBinary(ScopedBinary, ScopedComponents);
    if (!ScopedDecodedBinary)
    {
        std::fprintf(stderr, "selftest binary: %s\n", ScopedDecodedBinary.error().c_str());
        return 1;
    }

    entt::registry ScopedRegistry;
    const Entity ScopedExisting = ScopedRegistry.create();
    ScopedRegistry.emplace<Fby::Transform2D>(ScopedExisting, Fby::Transform2D{});

    auto ScopedLoaded = Fby::Scene::ApplyScene(ScopedRegistry, ScopedComponents, *ScopedDecodedBinary,
                                               Fby::Scene::SceneLoadOptions{.clearExisting = false});
    if (!ScopedLoaded)
    {
        std::fprintf(stderr, "selftest apply: %s\n", ScopedLoaded.error().c_str());
        return 1;
    }
    if (ScopedLoaded->Size() != 2)
    {
        std::fprintf(stderr, "selftest: expected 2 entities, got %zu\n", ScopedLoaded->Size());
        return 1;
    }
    const Entity ScopedRoot = ScopedLoaded->Resolve(0);
    const Entity ScopedFollower = ScopedLoaded->Resolve(1);
    const auto* ScopedFollow = ScopedRegistry.try_get<FollowTarget>(ScopedFollower);
    if (ScopedFollow == nullptr || ScopedFollow->Target != ScopedRoot || ScopedFollow->Target == ScopedExisting)
    {
        std::fprintf(stderr, "selftest: entity remap failed (target %u, expected %u)\n",
                     static_cast<unsigned>(ScopedFollow != nullptr ? ScopedFollow->Target : entt::null),
                     static_cast<unsigned>(ScopedRoot));
        return 1;
    }
    if (ScopedRegistry.try_get<Fby::Transform2D>(ScopedRoot) == nullptr)
    {
        std::fprintf(stderr, "selftest: root transform missing\n");
        return 1;
    }

    std::error_code ScopedCode;
    fs::create_directories(Output, ScopedCode);
    if (!WriteText(Output / "selftest.toml", ScopedToml) || !WriteBinary(Output / "selftest.fbin", ScopedBinary))
    {
        std::fprintf(stderr, "selftest: cannot write outputs to %s\n", Output.string().c_str());
        return 1;
    }
    std::printf("selftest ok: remap %u -> %u, %zu entities, %zu bytes binary; wrote %s\n",
                static_cast<unsigned>(ScopedExisting), static_cast<unsigned>(ScopedRoot), ScopedLoaded->Size(),
                ScopedBinary.size(), Output.string().c_str());
    return 0;
}
}

FBY_REFLECT_COMPONENT(FollowTarget, "FollowTarget", FBY_FIELD(FollowTarget, Target, Entity),
                      FBY_FIELD(FollowTarget, Stiffness, Float))

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        return Usage();
    }
    Fby::SetLogLevel(Fby::LogLevel::Warn);

    const std::string_view ScopedCommand = argv[1];
    if (ScopedCommand == "toml2bin" && argc == 4)
    {
        return TomlToBin(argv[2], argv[3]);
    }
    if (ScopedCommand == "bin2toml" && argc == 4)
    {
        return BinToToml(argv[2], argv[3]);
    }
    if (ScopedCommand == "roundtrip" && argc == 3)
    {
        return RoundTrip(argv[2]);
    }
    if (ScopedCommand == "selftest" && argc == 3)
    {
        return SelfTest(argv[2]);
    }
    return Usage();
}
