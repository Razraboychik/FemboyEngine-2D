// FemboyEngine2D - view/physics profiles and scene metadata tests.
#include "Editor/Infrastructure/ProjectConfig.hpp"
#include "Engine/Assets/ProjectScaffold.hpp"
#include "Engine/ECS/Scene.hpp"
#include "Engine/Physics/PhysicsProfile.hpp"

#include <cstdio>
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
}

int main()
{
    Fby::ViewProfile ScopedProfile = Fby::ViewProfile::Default;
    Check(Fby::ParseViewProfile("side-view", &ScopedProfile) && ScopedProfile == Fby::ViewProfile::SideView,
          "side-view key parses");
    Check(Fby::ParseViewProfile("hex", &ScopedProfile) && ScopedProfile == Fby::ViewProfile::Hex,
          "hex key parses");
    Check(!Fby::ParseViewProfile("bogus", &ScopedProfile), "unknown profile key is rejected");

    Fby::PhysicsDesc ScopedDesc;
    ScopedDesc.gravity = {0.0f, 9.8f};
    const Fby::PhysicsDesc ScopedSide = Fby::ApplyPhysicsProfile(ScopedDesc, Fby::ViewProfile::SideView);
    Check(ScopedSide.backend == Fby::PhysicsBackendKind::Box2D && ScopedSide.gravity.Y == 9.8f,
          "side-view maps to Box2D gravity");
    const Fby::PhysicsDesc ScopedTop = Fby::ApplyPhysicsProfile(ScopedDesc, Fby::ViewProfile::TopDown);
    Check(ScopedTop.gravity.X == 0.0f && ScopedTop.gravity.Y == 0.0f, "top-down disables gravity");
    const Fby::PhysicsProfileDesc ScopedHex = Fby::PhysicsProfileFor(Fby::ViewProfile::Hex);
    Check(ScopedHex.GridMovement && ScopedHex.Backend == Fby::PhysicsBackendKind::Dod,
          "hex profile uses grid movement and the DOD backend");

    Fby::Scene::SceneData ScopedScene;
    ScopedScene.meta.profile = Fby::ViewProfile::Isometric;
    const std::string ScopedSceneToml = Fby::Scene::EncodeSceneToml(ScopedScene);
    Check(ScopedSceneToml.find("profile = \"isometric\"") != std::string::npos, "scene writes its profile");
    Fby::Scene::ComponentRegistry ScopedComponents;
    const auto ScopedDecoded = Fby::Scene::DecodeSceneToml(ScopedSceneToml, ScopedComponents);
    Check(ScopedDecoded.has_value() && ScopedDecoded->meta.profile == Fby::ViewProfile::Isometric,
          "scene profile roundtrips");
    const std::vector<std::byte> ScopedBinary = Fby::Scene::EncodeSceneBinary(ScopedScene);
    const auto ScopedBinaryDecoded = Fby::Scene::DecodeSceneBinary(ScopedBinary, ScopedComponents);
    Check(ScopedBinaryDecoded.has_value() &&
              ScopedBinaryDecoded->meta.profile == Fby::ViewProfile::Isometric,
          "binary scene profile roundtrips");

    Fby::Editor::ProjectConfig ScopedConfig;
    ScopedConfig.project.viewProfile = Fby::ViewProfile::Hex;
    const std::string ScopedProjectToml = Fby::Editor::EncodeProjectToml(ScopedConfig);
    Fby::Editor::ProjectConfig ScopedDecodedConfig;
    std::string ScopedError;
    Check(Fby::Editor::DecodeProjectToml(ScopedProjectToml, &ScopedDecodedConfig, &ScopedError),
          "project profile TOML decodes");
    Check(ScopedDecodedConfig.project.viewProfile == Fby::ViewProfile::Hex,
          "project profile roundtrips");
    const std::string ScopedProjectTomlText = Fby::Assets::Scaffold::ProjectToml("Test", "test", "bc7-srgb", "hex");
    Check(ScopedProjectTomlText.find("profile = \"hex\"") != std::string::npos,
          "generated project descriptor carries the profile");

    if (GFailures == 0)
    {
        std::printf("physics_profile_test: all checks passed\n");
        return 0;
    }
    return 1;
}
