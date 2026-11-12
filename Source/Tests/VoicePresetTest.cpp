// FemboyEngine2D - voice preset registry, data pack and policy tests.
#include "Engine/Audio/VoicePreset.hpp"
#include "Engine/Core/PluginAPI.h"
#include "Engine/Audio/VoiceSession.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

namespace
{
std::uint32_t GFailures = 0;

#define FBY_CHECK(condition)                                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                  \
            ++GFailures;                                                                                               \
        }                                                                                                              \
    } while (false)

[[nodiscard]] Fby::VoicePresetDesc MakeTestPreset(const char* InId, float InMix)
{
    Fby::VoicePresetDesc ScopedPreset{};
    std::snprintf(ScopedPreset.Id, sizeof(ScopedPreset.Id), "%s", InId);
    std::snprintf(ScopedPreset.NameKey, sizeof(ScopedPreset.NameKey), "voice.test.%s", InId);
    ScopedPreset.Category = Fby::VoicePresetCategory::Cosmetic;
    ScopedPreset.Flags = Fby::VoicePresetFlagCosmetic | Fby::VoicePresetFlagAllowDownlink;
    Fby::VoiceEffectSlot ScopedSlot{};
    ScopedSlot.Kind = Fby::VoiceEffectKind::Muffle;
    ScopedSlot.Mix = InMix;
    (void)ScopedPreset.Profile.BaseChain.Add(ScopedSlot);
    return ScopedPreset;
}

void TestRegistry()
{
    Fby::VoicePresetRegistry ScopedRegistry;
    std::string ScopedError;
    FBY_CHECK(ScopedRegistry.Empty());
    FBY_CHECK(ScopedRegistry.Add(MakeTestPreset("Test.One", 1.0f), nullptr, &ScopedError));
    FBY_CHECK(ScopedRegistry.Add(MakeTestPreset("Test.Two", 0.5f), nullptr, &ScopedError));
    FBY_CHECK(ScopedRegistry.Count() == 2);
    FBY_CHECK(ScopedRegistry.Find("Test.One") != nullptr);
    FBY_CHECK(ScopedRegistry.Find("Test.Two")->Profile.BaseChain.Count == 1);
    FBY_CHECK(ScopedRegistry.Find("Test.Missing") == nullptr);

    FBY_CHECK(!ScopedRegistry.Add(MakeTestPreset("Test.One", 1.0f), nullptr, &ScopedError));
    FBY_CHECK(!ScopedError.empty());

    Fby::VoicePresetDesc ScopedBadId = MakeTestPreset("bad id", 1.0f);
    FBY_CHECK(!ScopedRegistry.Add(ScopedBadId, nullptr, &ScopedError));

    Fby::VoicePresetDesc ScopedBadCategory = MakeTestPreset("Test.BadCategory", 1.0f);
    ScopedBadCategory.Category = Fby::VoicePresetCategory::None;
    FBY_CHECK(!ScopedRegistry.Add(ScopedBadCategory, nullptr, &ScopedError));

    const void* ScopedOwner = reinterpret_cast<const void*>(0x1234);
    FBY_CHECK(ScopedRegistry.Add(MakeTestPreset("Test.Owned", 1.0f), ScopedOwner, &ScopedError));
    ScopedRegistry.RemoveOwner(ScopedOwner);
    FBY_CHECK(ScopedRegistry.Find("Test.Owned") == nullptr);
    FBY_CHECK(ScopedRegistry.Count() == 2);
}

void TestBuiltIns()
{
    Fby::VoicePresetRegistry ScopedRegistry;
    ScopedRegistry.RegisterBuiltIns();
    FBY_CHECK(ScopedRegistry.Count() == 24);
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedRegistry.Count(); ++ScopedIndex)
    {
        const Fby::VoicePresetDesc* ScopedPreset = ScopedRegistry.At(ScopedIndex);
        FBY_CHECK(ScopedPreset != nullptr);
        FBY_CHECK(ScopedPreset->Id[0] != '\0');
        FBY_CHECK(ScopedPreset->Profile.BaseChain.Count > 0);
        FBY_CHECK(ScopedPreset->Category != Fby::VoicePresetCategory::None);
        FBY_CHECK(ScopedPreset->MaxIntensity >= ScopedPreset->MinIntensity);
    }
    FBY_CHECK(ScopedRegistry.Find("Fby.Voice.Underwater") != nullptr);
    FBY_CHECK(ScopedRegistry.Find("Fby.Voice.ModerationMumble") != nullptr);
    FBY_CHECK(ScopedRegistry.Find("Fby.Voice.Chipmunk") != nullptr);
    FBY_CHECK(ScopedRegistry.Find("Fby.Voice.Giant") != nullptr);
    const Fby::VoicePresetDesc* ScopedMumble = ScopedRegistry.Find("Fby.Voice.ImitationMumble");
    FBY_CHECK(ScopedMumble != nullptr);
    FBY_CHECK((ScopedMumble->Flags & Fby::VoicePresetFlagAllowUplink) != 0);
}

void TestLoader()
{
    constexpr const char* kValid = R"(
version = 1
id = "Test.Voice.Custom"
name_key = "voice.test.custom"
category = "environment"
flags = ["cosmetic", "downlink"]
min_intensity = 0.2
max_intensity = 1.0

[[slot]]
kind = "underwater"
mix = 1.0
low_cut_hz = 140.0
high_cut_hz = 700.0

[[slot]]
kind = "pitch"
mix = 0.5
semitones = 4.0
)";
    Fby::VoicePresetRegistry ScopedRegistry;
    std::string ScopedError;
    FBY_CHECK(Fby::LoadVoicePresetToml(kValid, ScopedRegistry, nullptr, &ScopedError));
    FBY_CHECK(ScopedError.empty());
    const Fby::VoicePresetDesc* ScopedPreset = ScopedRegistry.Find("Test.Voice.Custom");
    FBY_CHECK(ScopedPreset != nullptr);
    if (ScopedPreset != nullptr)
    {
        FBY_CHECK(ScopedPreset->Category == Fby::VoicePresetCategory::Environment);
        FBY_CHECK(ScopedPreset->Profile.BaseChain.Count == 2);
        FBY_CHECK(std::fabs(ScopedPreset->Profile.BaseChain.Slots[0].Underwater.LowCutHz - 140.0f) < 0.01f);
        FBY_CHECK(std::fabs(ScopedPreset->Profile.BaseChain.Slots[1].Pitch.Semitones - 4.0f) < 0.01f);
    }

    FBY_CHECK(!Fby::LoadVoicePresetToml("version = 2\nid = \"X.Y\"\n", ScopedRegistry, nullptr, &ScopedError));
    FBY_CHECK(!ScopedError.empty());
    FBY_CHECK(!Fby::LoadVoicePresetToml("version = 1\nid = \"X.Y\"\nname_key = \"k\"\ncategory = \"nope\"\n",
                                        ScopedRegistry, nullptr, &ScopedError));
    FBY_CHECK(!Fby::LoadVoicePresetToml(
        "version = 1\nid = \"X.Y\"\nname_key = \"k\"\ncategory = \"cosmetic\"\nflags = [\"nope\"]\n", ScopedRegistry,
        nullptr, &ScopedError));
    FBY_CHECK(!Fby::LoadVoicePresetToml(
        "version = 1\nid = \"X.Y\"\nname_key = \"k\"\ncategory = \"cosmetic\"\n[[slot]]\nkind = \"echo\"\n",
        ScopedRegistry, nullptr, &ScopedError));
}

void TestPolicy()
{
    Fby::VoicePresetRegistry ScopedRegistry;
    ScopedRegistry.RegisterBuiltIns();
    Fby::VoiceSession ScopedSession;
    FBY_CHECK(ScopedSession.AddParticipant(1));
    Fby::VoiceEffectPolicy ScopedPolicy{};
    Fby::VoicePresetChoice ScopedChoice;
    ScopedChoice.Set("Fby.Voice.Chipmunk", 0.5f);

    const Fby::VoicePresetResolution ScopedCosmetic =
        Fby::ResolveVoicePreset(ScopedRegistry, ScopedPolicy, ScopedSession, 1, ScopedChoice);
    FBY_CHECK(ScopedCosmetic.Preset != nullptr);
    FBY_CHECK(std::string(ScopedCosmetic.Preset->Id) == "Fby.Voice.Chipmunk");
    FBY_CHECK(!ScopedCosmetic.Moderation);
    FBY_CHECK(std::fabs(ScopedCosmetic.Intensity - 0.5f) < 0.001f);

    ScopedSession.SetParticipantMuted(1, true);
    const Fby::VoicePresetResolution ScopedForced =
        Fby::ResolveVoicePreset(ScopedRegistry, ScopedPolicy, ScopedSession, 1, ScopedChoice);
    FBY_CHECK(ScopedForced.Preset != nullptr);
    FBY_CHECK(std::string(ScopedForced.Preset->Id) == "Fby.Voice.ModerationMumble");
    FBY_CHECK(ScopedForced.Moderation);

    ScopedPolicy.MumbleWhenModerated = false;
    const Fby::VoicePresetResolution ScopedHardMuted =
        Fby::ResolveVoicePreset(ScopedRegistry, ScopedPolicy, ScopedSession, 1, ScopedChoice);
    FBY_CHECK(ScopedHardMuted.Preset != nullptr);
    FBY_CHECK(std::string(ScopedHardMuted.Preset->Id) == "Fby.Voice.Muted");
    FBY_CHECK(ScopedHardMuted.Moderation);

    ScopedChoice.Clear();
    const Fby::VoicePresetResolution ScopedMutedWithoutChoice =
        Fby::ResolveVoicePreset(ScopedRegistry, ScopedPolicy, ScopedSession, 1, ScopedChoice);
    FBY_CHECK(ScopedMutedWithoutChoice.Preset != nullptr);
    FBY_CHECK(std::string(ScopedMutedWithoutChoice.Preset->Id) == "Fby.Voice.Muted");

    ScopedSession.SetParticipantMuted(1, false);
    const Fby::VoicePresetResolution ScopedEmpty =
        Fby::ResolveVoicePreset(ScopedRegistry, ScopedPolicy, ScopedSession, 1, ScopedChoice);
    FBY_CHECK(ScopedEmpty.Preset == nullptr);
}

void TestHostCapability()
{
    FbyHostApi ScopedOldHost{};
    ScopedOldHost.struct_size = static_cast<std::uint32_t>(offsetof(FbyHostApi, voice));
    FBY_CHECK(!FbyHostHasVoice(&ScopedOldHost));

    FbyVoiceApi ScopedVoiceApi{};
    ScopedVoiceApi.struct_size = static_cast<std::uint32_t>(sizeof(FbyVoiceApi));
    ScopedVoiceApi.abi_version = FBY_VOICE_API_VERSION;
    FbyHostApi ScopedNewHost{};
    ScopedNewHost.struct_size = static_cast<std::uint32_t>(sizeof(FbyHostApi));
    ScopedNewHost.voice = &ScopedVoiceApi;
    FBY_CHECK(FbyHostHasVoice(&ScopedNewHost));
    ScopedNewHost.voice = nullptr;
    FBY_CHECK(!FbyHostHasVoice(&ScopedNewHost));
    FBY_CHECK(FbyHostHasVoice(nullptr) == 0);
}

void TestProfileScaling()
{
    Fby::VoicePresetRegistry ScopedRegistry;
    ScopedRegistry.RegisterBuiltIns();
    const Fby::VoicePresetDesc* ScopedPreset = ScopedRegistry.Find("Fby.Voice.Chipmunk");
    FBY_CHECK(ScopedPreset != nullptr);
    if (ScopedPreset == nullptr)
    {
        return;
    }
    Fby::VoicePresetResolution ScopedResolution{};
    ScopedResolution.Preset = ScopedPreset;
    ScopedResolution.Intensity = 0.5f;
    const Fby::VoiceEffectProfile ScopedProfile = Fby::BuildVoicePresetProfile(ScopedResolution);
    FBY_CHECK(ScopedProfile.BaseChain.Count == ScopedPreset->Profile.BaseChain.Count);
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedProfile.BaseChain.Count; ++ScopedIndex)
    {
        FBY_CHECK(std::fabs(ScopedProfile.BaseChain.Slots[ScopedIndex].Mix -
                            ScopedPreset->Profile.BaseChain.Slots[ScopedIndex].Mix * 0.5f) < 0.001f);
    }

    Fby::VoicePresetResolution ScopedNone{};
    FBY_CHECK(Fby::BuildVoicePresetProfile(ScopedNone).BaseChain.Count == 0);
}
}

int main()
{
    TestRegistry();
    TestBuiltIns();
    TestLoader();
    TestPolicy();
    TestProfileScaling();
    TestHostCapability();
    if (GFailures != 0)
    {
        std::fprintf(stderr, "%u voice preset failure(s)\n", GFailures);
        return 1;
    }
    std::printf("voice presets: all checks passed\n");
    return 0;
}
