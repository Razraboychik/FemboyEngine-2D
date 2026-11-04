// FemboyEngine2D - voice effects, conditions and spatial tests.
#include "Engine/Audio/VoiceChannel.hpp"
#include "Engine/Audio/VoiceEffects.hpp"
#include "Engine/Audio/VoiceSession.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <span>
#include <vector>

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

constexpr float kPi = 3.14159265358979323846f;
constexpr std::uint32_t kRate = 48000;

struct ChannelRouter
{
    Fby::VoiceChannel* Peers[8]{};
    std::uint8_t From = 0xFF;

    static bool SendThunk(void* InUser, std::uint8_t InToPlayer, std::span<const std::uint8_t> InPacket) noexcept
    {
        auto* ScopedRouter = static_cast<ChannelRouter*>(InUser);
        if (ScopedRouter == nullptr || InToPlayer >= 8 || ScopedRouter->Peers[InToPlayer] == nullptr)
        {
            return false;
        }
        return ScopedRouter->Peers[InToPlayer]->ReceivePacket(ScopedRouter->From, InPacket);
    }
};

Fby::VoiceEffectChain GDecodeChain;
bool GDecodeSinkUsed = false;

void DecodeMuteThunk(void*, std::uint8_t, float* InSamples, std::uint32_t InFrameCount) noexcept
{
    GDecodeSinkUsed = true;
    GDecodeChain.Process(InSamples, InFrameCount);
}

[[nodiscard]] std::vector<float> Sine(float InAmplitude, float InFrequency, std::uint32_t InFrames)
{
    std::vector<float> ScopedSamples(InFrames);
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < InFrames; ++ScopedIndex)
    {
        ScopedSamples[ScopedIndex] = InAmplitude * std::sin(2.0f * kPi * InFrequency * static_cast<float>(ScopedIndex) /
                                                            static_cast<float>(kRate));
    }
    return ScopedSamples;
}

[[nodiscard]] float RingRms(Fby::FloatRing& InRing)
{
    std::vector<float> ScopedSamples(InRing.AvailableRead());
    const std::uint32_t ScopedRead =
        InRing.Read(ScopedSamples.data(), static_cast<std::uint32_t>(ScopedSamples.size()));
    ScopedSamples.resize(ScopedRead);
    double ScopedSum = 0.0;
    for (const float ScopedSample : ScopedSamples)
    {
        ScopedSum += static_cast<double>(ScopedSample) * ScopedSample;
    }
    return ScopedSamples.empty() ? 0.0f : static_cast<float>(std::sqrt(ScopedSum / ScopedSamples.size()));
}

[[nodiscard]] float RmsFrom(const std::vector<float>& InSamples, std::uint32_t InFirstFrame)
{
    if (InFirstFrame >= InSamples.size())
    {
        return 0.0f;
    }
    double ScopedSum = 0.0;
    for (std::size_t ScopedIndex = InFirstFrame; ScopedIndex < InSamples.size(); ++ScopedIndex)
    {
        ScopedSum += static_cast<double>(InSamples[ScopedIndex]) * InSamples[ScopedIndex];
    }
    const auto ScopedCount = static_cast<double>(InSamples.size() - InFirstFrame);
    return static_cast<float>(std::sqrt(ScopedSum / ScopedCount));
}

[[nodiscard]] bool AllFinite(const std::vector<float>& InSamples)
{
    for (const float ScopedSample : InSamples)
    {
        if (!std::isfinite(ScopedSample))
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::uint32_t ZeroCrossings(const std::vector<float>& InSamples, std::uint32_t InFirstFrame)
{
    std::uint32_t ScopedCount = 0;
    float ScopedPrevious = InSamples[InFirstFrame];
    for (std::size_t ScopedIndex = InFirstFrame + 1; ScopedIndex < InSamples.size(); ++ScopedIndex)
    {
        const float ScopedSample = InSamples[ScopedIndex];
        if ((ScopedPrevious <= 0.0f && ScopedSample > 0.0f) || (ScopedPrevious >= 0.0f && ScopedSample < 0.0f))
        {
            ++ScopedCount;
        }
        ScopedPrevious = ScopedSample;
    }
    return ScopedCount;
}

void TestSettingsAndPresets()
{
    using namespace Fby;

    VoiceMuffleSettings ScopedBadMuffle{};
    ScopedBadMuffle.HighCutHz = 40000.0f;
    ScopedBadMuffle.Resonance = 40.0f;
    FBY_CHECK(ScopedBadMuffle.Issue() != nullptr);
    ScopedBadMuffle.Clamp();
    FBY_CHECK(ScopedBadMuffle.Issue() == nullptr);
    FBY_CHECK(ScopedBadMuffle.HighCutHz <= 12000.0f);
    FBY_CHECK(ScopedBadMuffle.LowCutHz < ScopedBadMuffle.HighCutHz);

    VoiceConditionSettings ScopedConditions{};
    ScopedConditions.UnderwaterFadeSeconds = -4.0f;
    ScopedConditions.InterferenceMuteThreshold = 9.0f;
    FBY_CHECK(ScopedConditions.Issue() != nullptr);
    ScopedConditions.Clamp();
    FBY_CHECK(ScopedConditions.Issue() == nullptr);

    VoiceEffectChainSettings ScopedChain{};
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < kVoiceEffectMaxSlots; ++ScopedIndex)
    {
        VoiceEffectSlot ScopedSlot{};
        ScopedSlot.Kind = VoiceEffectKind::Muffle;
        FBY_CHECK(ScopedChain.Add(ScopedSlot));
    }
    VoiceEffectSlot ScopedExtra{};
    FBY_CHECK(!ScopedChain.Add(ScopedExtra));

    for (std::uint8_t ScopedPreset = 0; ScopedPreset < static_cast<std::uint8_t>(VoiceEffectPresetKind::Count);
         ++ScopedPreset)
    {
        const VoiceEffectChainSettings ScopedPresetChain =
            VoiceEffectPresetChain(static_cast<VoiceEffectPresetKind>(ScopedPreset));
        FBY_CHECK(ScopedPresetChain.Issue() == nullptr);
        if (ScopedPreset != static_cast<std::uint8_t>(VoiceEffectPresetKind::None))
        {
            FBY_CHECK(ScopedPresetChain.Count > 0);
        }
    }
    const VoiceEffectChainSettings ScopedChipmunk = VoiceEffectPresetChain(VoiceEffectPresetKind::Chipmunk);
    FBY_CHECK(ScopedChipmunk.Count == 2);
    FBY_CHECK(ScopedChipmunk.Slots[0].Pitch.Semitones > 0.0f);
}

void TestMute()
{
    using namespace Fby;

    VoiceEffectChainSettings ScopedChain{};
    VoiceEffectSlot ScopedSlot{};
    ScopedSlot.Kind = VoiceEffectKind::Mute;
    ScopedSlot.Mix = 1.0f;
    ScopedSlot.Mute.FadeSeconds = 0.02f;
    FBY_CHECK(ScopedChain.Add(ScopedSlot));

    VoiceEffectChain ScopedEffect;
    ScopedEffect.Configure(ScopedChain, kRate);
    std::vector<float> ScopedSamples = Sine(0.5f, 440.0f, kRate / 2);
    ScopedEffect.Process(ScopedSamples.data(), static_cast<std::uint32_t>(ScopedSamples.size()));
    FBY_CHECK(AllFinite(ScopedSamples));
    FBY_CHECK(RmsFrom(ScopedSamples, kRate / 4) < 0.001f);
}

void TestMuffle()
{
    using namespace Fby;

    VoiceEffectChainSettings ScopedChain{};
    VoiceEffectSlot ScopedSlot{};
    ScopedSlot.Kind = VoiceEffectKind::Muffle;
    ScopedSlot.Mix = 1.0f;
    ScopedSlot.Muffle.HighCutHz = 620.0f;
    FBY_CHECK(ScopedChain.Add(ScopedSlot));

    VoiceEffectChain ScopedEffect;
    ScopedEffect.Configure(ScopedChain, kRate);
    std::vector<float> ScopedLow = Sine(0.5f, 220.0f, kRate / 4);
    std::vector<float> ScopedHigh = Sine(0.5f, 4000.0f, kRate / 4);
    ScopedEffect.Process(ScopedLow.data(), static_cast<std::uint32_t>(ScopedLow.size()));
    ScopedEffect.Reset();
    ScopedEffect.Process(ScopedHigh.data(), static_cast<std::uint32_t>(ScopedHigh.size()));
    const float ScopedLowRms = RmsFrom(ScopedLow, kRate / 8);
    const float ScopedHighRms = RmsFrom(ScopedHigh, kRate / 8);
    FBY_CHECK(AllFinite(ScopedLow));
    FBY_CHECK(AllFinite(ScopedHigh));
    FBY_CHECK(ScopedLowRms > 0.02f);
    FBY_CHECK(ScopedHighRms < ScopedLowRms * 0.5f);
}

void TestPitch()
{
    using namespace Fby;

    VoiceEffectChainSettings ScopedUpChain{};
    VoiceEffectSlot ScopedUp{};
    ScopedUp.Kind = VoiceEffectKind::Pitch;
    ScopedUp.Mix = 1.0f;
    ScopedUp.Pitch.Semitones = 12.0f;
    ScopedUp.Pitch.WindowMs = 20.0f;
    FBY_CHECK(ScopedUpChain.Add(ScopedUp));

    VoiceEffectChain ScopedUpEffect;
    ScopedUpEffect.Configure(ScopedUpChain, kRate);
    std::vector<float> ScopedUpSamples = Sine(0.4f, 440.0f, kRate);
    ScopedUpEffect.Process(ScopedUpSamples.data(), static_cast<std::uint32_t>(ScopedUpSamples.size()));
    const std::uint32_t ScopedUpCrossings = ZeroCrossings(ScopedUpSamples, kRate / 2);
    FBY_CHECK(AllFinite(ScopedUpSamples));
    FBY_CHECK(ScopedUpCrossings > 700);

    VoiceEffectChainSettings ScopedDownChain{};
    VoiceEffectSlot ScopedDown{};
    ScopedDown.Kind = VoiceEffectKind::Pitch;
    ScopedDown.Mix = 1.0f;
    ScopedDown.Pitch.Semitones = -12.0f;
    FBY_CHECK(ScopedDownChain.Add(ScopedDown));
    VoiceEffectChain ScopedDownEffect;
    ScopedDownEffect.Configure(ScopedDownChain, kRate);
    std::vector<float> ScopedDownSamples = Sine(0.4f, 440.0f, kRate);
    ScopedDownEffect.Process(ScopedDownSamples.data(), static_cast<std::uint32_t>(ScopedDownSamples.size()));
    const std::uint32_t ScopedDownCrossings = ZeroCrossings(ScopedDownSamples, kRate / 2);
    FBY_CHECK(AllFinite(ScopedDownSamples));
    FBY_CHECK(ScopedDownCrossings < 350);
}

void TestUnderwaterCondition()
{
    using namespace Fby;

    VoiceEffectProfile ScopedProfile{};
    ScopedProfile.EnableConditions = true;
    ScopedProfile.Conditions.UnderwaterFadeSeconds = 0.05f;

    VoiceEffectRack ScopedRack;
    ScopedRack.Configure(ScopedProfile, kRate);
    VoiceConditionState ScopedState{};
    ScopedState.Submerged = true;
    ScopedState.Oxygen01 = 0.1f;
    ScopedRack.SetConditionState(ScopedState);
    for (std::uint32_t ScopedStep = 0; ScopedStep < 8; ++ScopedStep)
    {
        ScopedRack.Advance(0.25f);
    }
    FBY_CHECK(ScopedRack.Condition().Submerged);
    FBY_CHECK(ScopedRack.Condition().UnderwaterBlend > 0.9f);
    FBY_CHECK(std::fabs(ScopedRack.Condition().Oxygen01 - 0.1f) < 1e-5f);

    std::vector<float> ScopedSamples = Sine(0.4f, 4000.0f, kRate / 2);
    ScopedRack.Process(ScopedSamples.data(), static_cast<std::uint32_t>(ScopedSamples.size()));
    FBY_CHECK(AllFinite(ScopedSamples));
    FBY_CHECK(RmsFrom(ScopedSamples, kRate / 4) < 0.1f);
}

void TestConditionalRadioAndHardMute()
{
    using namespace Fby;

    VoiceEffectProfile ScopedProfile{};
    ScopedProfile.Conditions.InterferenceHardMute = true;
    ScopedProfile.Conditions.InterferenceMuteThreshold = 0.8f;
    ScopedProfile.Conditions.InterferenceFadeSeconds = 0.02f;
    VoiceEffectRack ScopedRack;
    ScopedRack.Configure(ScopedProfile, kRate);
    VoiceConditionState ScopedState{};
    ScopedState.Interference01 = 1.0f;
    ScopedRack.SetConditionState(ScopedState);
    for (std::uint32_t ScopedStep = 0; ScopedStep < 8; ++ScopedStep)
    {
        ScopedRack.Advance(0.25f);
    }
    FBY_CHECK(ScopedRack.Condition().Interference > 0.9f);
    FBY_CHECK(ScopedRack.Condition().OutputGain < 0.01f);

    std::vector<float> ScopedSamples = Sine(0.5f, 440.0f, kRate);
    ScopedRack.Process(ScopedSamples.data(), static_cast<std::uint32_t>(ScopedSamples.size()));
    FBY_CHECK(AllFinite(ScopedSamples));
    FBY_CHECK(RmsFrom(ScopedSamples, kRate / 2) < 0.001f);
}

void TestSpatialAndOverride()
{
    using namespace Fby;

    VoiceSession ScopedSession;
    FBY_CHECK(ScopedSession.AddParticipant(1));
    VoiceSpatialSettings ScopedSpatial{};
    ScopedSpatial.Enabled = true;
    ScopedSpatial.MinDistance = 1.0f;
    ScopedSpatial.MaxDistance = 20.0f;
    ScopedSpatial.Rolloff = 1.0f;
    ScopedSpatial.PanWidth = 1.0f;
    ScopedSession.SetSpatialSettings(ScopedSpatial);
    ScopedSession.SetListenerPosition(Vector2D{0.0f, 0.0f});
    ScopedSession.SetParticipantPosition(1, Vector2D{10.0f, 0.0f});
    ScopedSession.Update(0.016f);
    FBY_CHECK(std::fabs(ScopedSession.ParticipantDistance(1) - 10.0f) < 1e-4f);
    FBY_CHECK(ScopedSession.ParticipantGain(1) < 0.2f);
    FBY_CHECK(ScopedSession.ParticipantPan(1) > 0.9f);

    ScopedSession.SetParticipantPositionOverride(1, Vector2D{0.0f, 10.0f});
    ScopedSession.Update(0.016f);
    FBY_CHECK(std::fabs(ScopedSession.ParticipantPan(1)) < 1e-4f);
    FBY_CHECK(ScopedSession.ParticipantGain(1) < 0.2f);

    ScopedSession.ClearParticipantPositionOverride(1);
    ScopedSession.Update(0.016f);
    FBY_CHECK(ScopedSession.ParticipantPan(1) > 0.9f);

    ScopedSpatial.MaxDistance = 5.0f;
    ScopedSpatial.MuteBeyondMaxDistance = true;
    ScopedSession.SetSpatialSettings(ScopedSpatial);
    ScopedSession.Update(0.016f);
    FBY_CHECK(ScopedSession.ParticipantGain(1) < 1e-5f);

    const VoiceSpatialPosition ScopedPosition =
        EvaluateVoiceSpatial(ScopedSpatial, Vector2D{0.0f, 0.0f}, Vector2D{10.0f, 0.0f});
    FBY_CHECK(ScopedPosition.Gain < 1e-5f);
    FBY_CHECK(ScopedPosition.Pan > 0.9f);

    FBY_CHECK(ScopedSession.AddParticipant(2));
    ScopedSession.SetParticipantPositionOverride(2, Vector2D{10.0f, 0.0f});
    ScopedSession.Update(0.016f);
    FBY_CHECK(ScopedSession.ParticipantGain(2) < 1e-5f);
    FBY_CHECK(ScopedSession.ParticipantPan(2) > 0.9f);
}

void TestDecodeSink()
{
    using namespace Fby;

    auto ScopedA = std::make_unique<VoiceChannel>();
    auto ScopedB = std::make_unique<VoiceChannel>();
    ChannelRouter ScopedRouterA{};
    ChannelRouter ScopedRouterB{};
    ScopedRouterA.Peers[1] = ScopedB.get();
    ScopedRouterA.From = 0;
    ScopedRouterB.Peers[0] = ScopedA.get();
    ScopedRouterB.From = 1;

    VoiceChannelConfig ScopedConfigA{};
    ScopedConfigA.Routing = VoiceRouting::Mesh;
    ScopedConfigA.LocalPlayer = 0;
    VoiceChannelConfig ScopedConfigB = ScopedConfigA;
    ScopedConfigB.LocalPlayer = 1;
    if (!ScopedA->Start(ScopedConfigA, &ChannelRouter::SendThunk, &ScopedRouterA) ||
        !ScopedB->Start(ScopedConfigB, &ChannelRouter::SendThunk, &ScopedRouterB))
    {
        std::printf("voice_effects_test: Opus unavailable, decode sink skipped\n");
        return;
    }
    FBY_CHECK(ScopedA->AddParticipant(1));
    FBY_CHECK(ScopedB->AddParticipant(0));

    VoiceEffectChainSettings ScopedMuteChain{};
    VoiceEffectSlot ScopedMute{};
    ScopedMute.Kind = VoiceEffectKind::Mute;
    ScopedMute.Mix = 1.0f;
    ScopedMute.Mute.FadeSeconds = 0.0f;
    FBY_CHECK(ScopedMuteChain.Add(ScopedMute));
    GDecodeChain.Configure(ScopedMuteChain, kRate);
    GDecodeSinkUsed = false;
    ScopedB->SetDecodeSink(&DecodeMuteThunk, nullptr);

    VoiceSession ScopedSessionA;
    VoiceSession ScopedSessionB;
    (void)ScopedSessionA.AddParticipant(1);
    (void)ScopedSessionB.AddParticipant(0);

    std::vector<float> ScopedStorage(kVoiceFrameSamples * 4);
    FloatRing ScopedCapture;
    ScopedCapture.Configure(ScopedStorage.data(), static_cast<std::uint32_t>(ScopedStorage.size()), 1);
    const std::vector<float> ScopedSine = Sine(0.3f, 440.0f, kVoiceFrameSamples);
    FBY_CHECK(ScopedCapture.Write(ScopedSine.data(), kVoiceFrameSamples) == kVoiceFrameSamples);
    ScopedA->SetCaptureRing(&ScopedCapture);

    ScopedA->Tick(0.02f, &ScopedSessionA);
    ScopedB->Tick(0.02f, &ScopedSessionB);
    FBY_CHECK(GDecodeSinkUsed);
    FloatRing* ScopedRing = ScopedB->ParticipantRing(0);
    FBY_CHECK(ScopedRing != nullptr);
    if (ScopedRing != nullptr)
    {
        FBY_CHECK(RingRms(*ScopedRing) < 0.01f);
    }
}

void TestCustomChain()
{
    using namespace Fby;

    VoiceEffectProfile ScopedProfile{};
    ScopedProfile.EnableConditions = false;
    VoiceEffectSlot ScopedMuffle{};
    ScopedMuffle.Kind = VoiceEffectKind::Muffle;
    ScopedMuffle.Mix = 0.8f;
    FBY_CHECK(ScopedProfile.BaseChain.Add(ScopedMuffle));
    VoiceEffectSlot ScopedPitch{};
    ScopedPitch.Kind = VoiceEffectKind::Pitch;
    ScopedPitch.Mix = 0.5f;
    ScopedPitch.Pitch.Semitones = 4.0f;
    FBY_CHECK(ScopedProfile.BaseChain.Add(ScopedPitch));

    VoiceEffectRack ScopedRack;
    ScopedRack.Configure(ScopedProfile, kRate);
    FBY_CHECK(ScopedRack.Active());
    FBY_CHECK(ScopedRack.EffectiveChain().Count == 2);
    std::vector<float> ScopedSamples = Sine(0.3f, 440.0f, kRate);
    ScopedRack.Process(ScopedSamples.data(), static_cast<std::uint32_t>(ScopedSamples.size()));
    FBY_CHECK(AllFinite(ScopedSamples));
}
}

int main()
{
    TestSettingsAndPresets();
    TestMute();
    TestMuffle();
    TestPitch();
    TestUnderwaterCondition();
    TestConditionalRadioAndHardMute();
    TestSpatialAndOverride();
    TestDecodeSink();
    TestCustomChain();

    if (GFailures == 0)
    {
        std::printf("voice_effects_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "voice_effects_test: %u failure(s)\n", GFailures);
    return 1;
}
