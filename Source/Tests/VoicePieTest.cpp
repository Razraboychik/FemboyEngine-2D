// FemboyEngine2D - PIE voice: client packets reach the host through the PIE link.
#include "Editor/Infrastructure/PlayInEditor.hpp"

#include "Engine/Core/JobSystem.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/SystemGraph.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
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

[[nodiscard]] std::vector<float> Sine(std::uint32_t InFrames)
{
    std::vector<float> ScopedSamples(InFrames);
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < InFrames; ++ScopedIndex)
    {
        ScopedSamples[ScopedIndex] = 0.2f * std::sin(2.0f * 3.14159265f * 440.0f *
                                                     static_cast<float>(ScopedIndex) / 48000.0f);
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
}

int main()
{
    using namespace Fby;

    JobSystem ScopedJobs(1);
    Ecs::SnapshotSchema ScopedSchema = Ecs::SnapshotSchema::BuiltIns();
    std::vector<std::byte> ScopedArena(1u << 20);

    Editor::PlayInEditor ScopedPie;
    FBY_CHECK(ScopedPie.Initialize(ScopedArena, ScopedSchema, ScopedArena.size()));

    Ecs::Registry ScopedEditorWorld;
    const Entity ScopedEntity = ScopedEditorWorld.create();
    ScopedEditorWorld.emplace<Transform2D>(ScopedEntity,
                                           Transform2D{Vector2D{1.0f, 0.0f}, Vector2D{1.0f, 1.0f}, 0.0f});
    ScopedEditorWorld.emplace<Velocity>(ScopedEntity, Velocity{});

    Ecs::SystemContext ScopedServices{ScopedEditorWorld, ScopedJobs};
    FBY_CHECK(ScopedPie.StartMultiplayer(ScopedEditorWorld, ScopedSchema, ScopedServices, 1, 1, nullptr) ==
              Editor::PlayInEditor::Result::Ok);
    FBY_CHECK(ScopedPie.VoiceActive());
    FBY_CHECK(ScopedPie.VoiceHostSession().CanTransmit());
    FBY_CHECK(!ScopedPie.VoiceClientSession().LocalMuted());

    Ecs::SystemGraph ScopedGraph;
    const std::vector<float> ScopedSpeech = Sine(kVoiceFrameSamples * 4);

    // 1. The simulated client speaks; the host receives and decodes the frame.
    ScopedPie.VoiceClient()->PushCaptured(ScopedSpeech);
    for (std::uint32_t ScopedStep = 0;
         ScopedStep < 120 && ScopedPie.VoiceHost()->Channel().Stats().PacketsReceived == 0; ++ScopedStep)
    {
        (void)ScopedPie.StepMultiplayer(ScopedGraph, 1.0f / 60.0f);
    }
    FBY_CHECK(ScopedPie.VoiceClient()->Channel().Stats().PacketsSent >= 1);
    FBY_CHECK(ScopedPie.VoiceHost()->Channel().Stats().PacketsReceived >= 1);

    // Jitter playout runs on the 20 ms voice grid, so let the host decode.
    for (std::uint32_t ScopedStep = 0; ScopedStep < 10; ++ScopedStep)
    {
        (void)ScopedPie.StepMultiplayer(ScopedGraph, 1.0f / 60.0f);
    }
    FloatRing* ScopedHostRing = ScopedPie.VoiceHost()->Channel().ParticipantRing(1);
    FBY_CHECK(ScopedHostRing != nullptr);
    if (ScopedHostRing != nullptr)
    {
        FBY_CHECK(RingRms(*ScopedHostRing) > 0.001f);
    }

    // 2. Muting the client stops transmission even while frames are captured.
    const std::uint64_t ScopedSentBeforeMute = ScopedPie.VoiceClient()->Channel().Stats().PacketsSent;
    ScopedPie.VoiceClientSession().SetLocalMuted(true);
    FBY_CHECK(!ScopedPie.VoiceClientSession().CanTransmit());
    ScopedPie.VoiceClient()->PushCaptured(ScopedSpeech);
    for (std::uint32_t ScopedStep = 0; ScopedStep < 40; ++ScopedStep)
    {
        (void)ScopedPie.StepMultiplayer(ScopedGraph, 1.0f / 60.0f);
    }
    FBY_CHECK(ScopedPie.VoiceClient()->Channel().Stats().PacketsSent == ScopedSentBeforeMute);

    // 3. Push-to-talk gates the same path until the key is held.
    ScopedPie.VoiceClientSession().SetLocalMuted(false);
    ScopedPie.VoiceClientSession().SetPushToTalkEnabled(true);
    ScopedPie.VoiceClientSession().SetPushToTalkHeld(false);
    FBY_CHECK(!ScopedPie.VoiceClientSession().CanTransmit());
    ScopedPie.VoiceClient()->PushCaptured(ScopedSpeech);
    for (std::uint32_t ScopedStep = 0; ScopedStep < 40; ++ScopedStep)
    {
        (void)ScopedPie.StepMultiplayer(ScopedGraph, 1.0f / 60.0f);
    }
    FBY_CHECK(ScopedPie.VoiceClient()->Channel().Stats().PacketsSent == ScopedSentBeforeMute);

    ScopedPie.VoiceClientSession().SetPushToTalkHeld(true);
    FBY_CHECK(ScopedPie.VoiceClientSession().CanTransmit());
    ScopedPie.VoiceClient()->PushCaptured(ScopedSpeech);
    for (std::uint32_t ScopedStep = 0;
         ScopedStep < 60 && ScopedPie.VoiceClient()->Channel().Stats().PacketsSent == ScopedSentBeforeMute;
         ++ScopedStep)
    {
        (void)ScopedPie.StepMultiplayer(ScopedGraph, 1.0f / 60.0f);
    }
    FBY_CHECK(ScopedPie.VoiceClient()->Channel().Stats().PacketsSent > ScopedSentBeforeMute);

    // 4. Stop tears the voice link down together with the PIE worlds.
    ScopedPie.Stop();
    FBY_CHECK(!ScopedPie.Active());
    FBY_CHECK(!ScopedPie.Multiplayer());
    FBY_CHECK(!ScopedPie.VoiceActive());

    if (GFailures == 0)
    {
        std::printf("voice_pie_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "voice_pie_test: %u failure(s)\n", GFailures);
    return 1;
}
