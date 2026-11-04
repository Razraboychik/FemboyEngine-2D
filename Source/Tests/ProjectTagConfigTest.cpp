// FemboyEngine2D - project gameplay tag database TOML roundtrip.
#include "Editor/Infrastructure/ProjectConfig.hpp"
#include "Engine/Gameplay/GameplayTags.hpp"

#include <cstdio>
#include <string>

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
    Fby::Editor::ProjectConfig ScopedConfig;
    Check(ScopedConfig.project.gameplayTags.Add("Character") >= 0, "add root tag");
    Check(ScopedConfig.project.gameplayTags.Add("Character.State.Stunned") >= 0, "add nested tag");
    Check(ScopedConfig.project.gameplayTags.Add("Character.State.Burning") >= 0, "add sibling tag");
    Check(ScopedConfig.project.gameplayTags.Count == 4, "ancestors are added once");

    const std::string ScopedToml = Fby::Editor::EncodeProjectToml(ScopedConfig);
    Check(ScopedToml.find("gameplay_tags") != std::string::npos, "project TOML contains gameplay_tags");
    Fby::Editor::ProjectConfig ScopedDecoded;
    std::string ScopedError;
    Check(Fby::Editor::DecodeProjectToml(ScopedToml, &ScopedDecoded, &ScopedError), "project TOML decodes");
    Check(ScopedDecoded.project.gameplayTags == ScopedConfig.project.gameplayTags,
          "tag database survives the TOML roundtrip");

    Fby::GameplayTagRegistry ScopedRegistry;
    ScopedDecoded.project.gameplayTags.ApplyToRegistry(ScopedRegistry);
    const Fby::GameplayTag ScopedStunned = ScopedRegistry.Find("Character.State.Stunned");
    const Fby::GameplayTag ScopedCharacter = ScopedRegistry.Find("Character");
    Check(ScopedStunned.IsValid() && ScopedCharacter.IsValid(), "registry resolves authored paths");
    Fby::GameplayTagContainer ScopedTags;
    ScopedTags.Add(ScopedStunned);
    Check(Fby::GameplayTagContainerHas(ScopedTags, ScopedRegistry, ScopedCharacter),
          "hierarchy survives the project roundtrip");

    if (GFailures == 0)
    {
        std::printf("project_tag_config_test: all checks passed\n");
        return 0;
    }
    return 1;
}
