// FemboyEngine2D - reactive voice SFX and procedural cue tests.
#include "Engine/Audio/VoiceReactiveSfx.hpp"
#include "Engine/Audio/VoiceSfxSynth.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>

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

void TestSynthesis()
{
    std::array<float, Fby::kVoiceSfxMaxSamples> ScopedFirst{};
    std::array<float, Fby::kVoiceSfxMaxSamples> ScopedSecond{};
    for (std::uint8_t ScopedKind = 1; ScopedKind < static_cast<std::uint8_t>(Fby::VoiceSfxKind::Count); ++ScopedKind)
    {
        const auto ScopedValue = static_cast<Fby::VoiceSfxKind>(ScopedKind);
        std::fill(ScopedFirst.begin(), ScopedFirst.end(), 0.0f);
        std::fill(ScopedSecond.begin(), ScopedSecond.end(), 0.0f);
        const std::uint32_t ScopedCount = Fby::SynthesizeVoiceSfx(ScopedValue, 48000, std::span<float>(ScopedFirst));
        FBY_CHECK(ScopedCount > 0);
        FBY_CHECK(Fby::SynthesizeVoiceSfx(ScopedValue, 48000, std::span<float>(ScopedSecond)) == ScopedCount);
        bool ScopedFinite = true;
        float ScopedPeak = 0.0f;
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedCount; ++ScopedIndex)
        {
            ScopedFinite = ScopedFinite && std::isfinite(ScopedFirst[ScopedIndex]);
            ScopedPeak = std::max(ScopedPeak, std::fabs(ScopedFirst[ScopedIndex]));
        }
        FBY_CHECK(ScopedFinite);
        FBY_CHECK(ScopedPeak > 0.0f);
        FBY_CHECK(ScopedPeak <= 0.8001f);
        FBY_CHECK(std::memcmp(ScopedFirst.data(), ScopedSecond.data(), ScopedCount * sizeof(float)) == 0);
        FBY_CHECK(Fby::SynthesizeVoiceSfx(Fby::VoiceSfxKind::None, 48000, std::span<float>(ScopedFirst)) == 0);
        FBY_CHECK(Fby::SynthesizeVoiceSfx(ScopedValue, 48000, std::span<float>()) == 0);
    }
}

void TestReactions()
{
    Fby::VoiceReactiveSfx ScopedSfx;
    Fby::VoiceReactiveSfxSettings ScopedSettings{};
    ScopedSettings.MasterGain = 1.0f;
    ScopedSettings.GlobalCooldownSeconds = 0.0f;
    ScopedSfx.Configure(ScopedSettings);

    Fby::VoiceReactiveSfxInput ScopedInput{};
    ScopedInput.Players[0].Present = true;
    ScopedInput.Players[0].Position = Fby::Vector2D{12.0f, 8.0f};
    std::array<Fby::VoiceSfxEvent, 8> ScopedEvents{};

    FBY_CHECK(ScopedSfx.Update(0.1f, ScopedInput, ScopedEvents) == 0);

    ScopedInput.Players[0].Submerged = true;
    FBY_CHECK(ScopedSfx.Update(0.1f, ScopedInput, ScopedEvents) == 1);
    FBY_CHECK(ScopedEvents[0].Kind == Fby::VoiceSfxKind::Splash);
    FBY_CHECK(ScopedEvents[0].Player == 0);
    FBY_CHECK(ScopedEvents[0].Gain > 0.0f && ScopedEvents[0].Gain <= 1.0f);

    FBY_CHECK(ScopedSfx.Update(0.1f, ScopedInput, ScopedEvents) == 0);
    ScopedInput.Players[0].Talking = true;
    FBY_CHECK(ScopedSfx.Update(0.1f, ScopedInput, ScopedEvents) == 1);
    FBY_CHECK(ScopedEvents[0].Kind == Fby::VoiceSfxKind::BubbleCluster);
    FBY_CHECK(ScopedSfx.Update(0.1f, ScopedInput, ScopedEvents) == 0);

    ScopedInput.Players[0].LocallyMuted = true;
    FBY_CHECK(ScopedSfx.Update(0.1f, ScopedInput, ScopedEvents) == 1);
    FBY_CHECK(ScopedEvents[0].Kind == Fby::VoiceSfxKind::ExplosionMuffled);

    ScopedInput.Players[0].LocallyMuted = false;
    FBY_CHECK(ScopedSfx.Update(0.1f, ScopedInput, ScopedEvents) == 1);
    FBY_CHECK(ScopedEvents[0].Kind == Fby::VoiceSfxKind::RadioClick);

    ScopedSettings.Enabled = false;
    ScopedSfx.Configure(ScopedSettings);
    ScopedInput.Players[0].LocallyMuted = true;
    FBY_CHECK(ScopedSfx.Update(0.1f, ScopedInput, ScopedEvents) == 0);

    ScopedSettings.Enabled = true;
    ScopedSettings.MasterGain = 1.0f;
    ScopedSettings.SplashGain = 2.0f;
    ScopedSfx.Configure(ScopedSettings);
    ScopedInput.Players[0].Submerged = true;
    ScopedInput.Players[0].Pan = 5.0f;
    FBY_CHECK(ScopedSfx.Update(0.1f, ScopedInput, ScopedEvents) >= 1);
    FBY_CHECK(ScopedEvents[0].Kind != Fby::VoiceSfxKind::None);
    FBY_CHECK(ScopedEvents[0].Gain <= 1.0f);
    FBY_CHECK(ScopedEvents[0].Pan <= 1.0f);

    ScopedSfx.Reset();
    FBY_CHECK(ScopedSfx.Update(0.1f, ScopedInput, std::span<Fby::VoiceSfxEvent>()) == 0);
}

void TestCooldowns()
{
    Fby::VoiceReactiveSfx ScopedSfx;
    Fby::VoiceReactiveSfxSettings ScopedSettings{};
    ScopedSettings.GlobalCooldownSeconds = 10.0f;
    ScopedSfx.Configure(ScopedSettings);
    Fby::VoiceReactiveSfxInput ScopedInput{};
    ScopedInput.Players[1].Present = true;
    std::array<Fby::VoiceSfxEvent, 4> ScopedEvents{};

    ScopedInput.Players[1].Submerged = true;
    FBY_CHECK(ScopedSfx.Update(0.1f, ScopedInput, ScopedEvents) == 1);
    ScopedInput.Players[1].LocallyMuted = true;
    FBY_CHECK(ScopedSfx.Update(0.1f, ScopedInput, ScopedEvents) == 0);

    ScopedInput.Players[1].Present = false;
    FBY_CHECK(ScopedSfx.Update(0.1f, ScopedInput, ScopedEvents) == 0);
    ScopedInput.Players[1].Present = true;
    ScopedInput.Players[1].Submerged = false;
    ScopedInput.Players[1].LocallyMuted = false;
    ScopedSettings.GlobalCooldownSeconds = 0.0f;
    ScopedSfx.Configure(ScopedSettings);
    FBY_CHECK(ScopedSfx.Update(0.1f, ScopedInput, ScopedEvents) == 0);
}
}

int main()
{
    TestSynthesis();
    TestReactions();
    TestCooldowns();
    if (GFailures != 0)
    {
        std::fprintf(stderr, "%u voice reactive sfx failure(s)\n", GFailures);
        return 1;
    }
    std::printf("voice reactive sfx: all checks passed\n");
    return 0;
}
