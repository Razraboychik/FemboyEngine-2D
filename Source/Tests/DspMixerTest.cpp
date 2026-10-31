// FemboyEngine2D - DSP mixer graph tests.
#include "Engine/Audio/Audio.hpp"
#include "Engine/Audio/DspGraph.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <thread>
#include <vector>

namespace
{
int GFailures = 0;

#define FBY_CHECK(condition)                                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                  \
            ++GFailures;                                                                                              \
        }                                                                                                              \
    } while (false)

// Allocation tracking. The override is process-wide, but only counts while the
std::atomic<bool> GTrackAllocations{false};
std::atomic<std::size_t> GAllocationCount{0};

[[nodiscard]] bool NearlyEqual(float InLeft, float InRight, float InEpsilon = 1.0e-5f) noexcept
{
    return std::fabs(InLeft - InRight) <= InEpsilon;
}

[[nodiscard]] std::vector<float> MakeStereoClip(float InLeft, float InRight, std::uint32_t InFrameCount)
{
    std::vector<float> ScopedSamples(static_cast<std::size_t>(InFrameCount) * 2);
    for (std::uint32_t ScopedFrame = 0; ScopedFrame < InFrameCount; ++ScopedFrame)
    {
        ScopedSamples[static_cast<std::size_t>(ScopedFrame) * 2 + 0] = InLeft;
        ScopedSamples[static_cast<std::size_t>(ScopedFrame) * 2 + 1] = InRight;
    }
    return ScopedSamples;
}

[[nodiscard]] Fby::DspClipView ClipView(const std::vector<float>& InSamples, std::uint32_t InChannels = 2) noexcept
{
    Fby::DspClipView ScopedView;
    ScopedView.samples = reinterpret_cast<const std::byte*>(InSamples.data());
    ScopedView.frameCount = static_cast<std::uint32_t>(InSamples.size() / InChannels);
    ScopedView.channels = InChannels;
    ScopedView.sampleRate = 48000;
    ScopedView.format = Fby::Assets::PcmFormat::PcmF32;
    return ScopedView;
}

[[nodiscard]] float Energy(const float* InInterleavedStereo, std::uint32_t InFrameCount) noexcept
{
    float ScopedTotal = 0.0f;
    for (std::uint32_t ScopedFrame = 0; ScopedFrame < InFrameCount; ++ScopedFrame)
    {
        const float ScopedLeft = InInterleavedStereo[static_cast<std::size_t>(ScopedFrame) * 2 + 0];
        const float ScopedRight = InInterleavedStereo[static_cast<std::size_t>(ScopedFrame) * 2 + 1];
        ScopedTotal += ScopedLeft * ScopedLeft + ScopedRight * ScopedRight;
    }
    return ScopedTotal;
}

// 1 + 2: routing sums and parent gain propagation
void TestRoutingAndGains()
{
    Fby::DspGraph ScopedGraph;
    FBY_CHECK(ScopedGraph.Initialize(48000));

    std::vector<float> ScopedSfx = MakeStereoClip(0.2f, -0.1f, 64);
    std::vector<float> ScopedMusic = MakeStereoClip(0.05f, 0.15f, 64);

    FBY_CHECK(ScopedGraph.SetBusGain(Fby::kSfxBus, 0.5f) == Fby::DspResult::Ok);
    FBY_CHECK(ScopedGraph.SetBusGain(Fby::kMusicBus, 0.25f) == Fby::DspResult::Ok);
    FBY_CHECK(ScopedGraph.SetBusGain(Fby::kMasterBus, 1.0f) == Fby::DspResult::Ok);

    std::uint32_t ScopedSfxVoice = Fby::kInvalidVoice;
    std::uint32_t ScopedMusicVoice = Fby::kInvalidVoice;
    FBY_CHECK(ScopedGraph.PlayOneShot(ClipView(ScopedSfx), Fby::kSfxBus, 1.0f, 1.0f, true, &ScopedSfxVoice) ==
              Fby::DspResult::Ok);
    FBY_CHECK(ScopedGraph.PlayOneShot(ClipView(ScopedMusic), Fby::kMusicBus, 1.0f, 1.0f, true, &ScopedMusicVoice) ==
              Fby::DspResult::Ok);

    float ScopedOutput[64 * 2] = {};
    ScopedGraph.Process(ScopedOutput, 64);
    for (std::uint32_t ScopedFrame = 0; ScopedFrame < 64; ++ScopedFrame)
    {
        const float ScopedExpectedLeft = 0.2f * 0.5f + 0.05f * 0.25f;
        const float ScopedExpectedRight = -0.1f * 0.5f + 0.15f * 0.25f;
        FBY_CHECK(NearlyEqual(ScopedOutput[static_cast<std::size_t>(ScopedFrame) * 2 + 0], ScopedExpectedLeft));
        FBY_CHECK(NearlyEqual(ScopedOutput[static_cast<std::size_t>(ScopedFrame) * 2 + 1], ScopedExpectedRight));
    }

    // A created bus feeds its parent: child gain * Sfx gain * Master gain.
    Fby::BusId ScopedChild = Fby::kInvalidBus;
    FBY_CHECK(ScopedGraph.CreateBus(Fby::kSfxBus, 0.5f, &ScopedChild) == Fby::DspResult::Ok);
    FBY_CHECK(ScopedChild >= Fby::kFirstDynamicBus);
    (void)ScopedGraph.StopVoice(ScopedMusicVoice);
    (void)ScopedGraph.StopVoice(ScopedSfxVoice);
    ScopedGraph.Process(ScopedOutput, 64);

    std::vector<float> ScopedChildClip = MakeStereoClip(0.4f, 0.4f, 64);
    std::uint32_t ScopedChildVoice = Fby::kInvalidVoice;
    FBY_CHECK(ScopedGraph.PlayOneShot(ClipView(ScopedChildClip), ScopedChild, 1.0f, 1.0f, true, &ScopedChildVoice) ==
              Fby::DspResult::Ok);
    ScopedGraph.Process(ScopedOutput, 64);
    const float ScopedExpectedChildGain = 0.4f * 0.5f * 0.5f;
    FBY_CHECK(NearlyEqual(ScopedOutput[0], ScopedExpectedChildGain));
    FBY_CHECK(NearlyEqual(ScopedOutput[1], ScopedExpectedChildGain));

    // Re-routing under a different parent propagates that parent's gain.
    FBY_CHECK(ScopedGraph.SetBusGain(Fby::kSfxBus, 0.25f) == Fby::DspResult::Ok);
    ScopedGraph.Process(ScopedOutput, 64);
    FBY_CHECK(NearlyEqual(ScopedOutput[0], 0.4f * 0.5f * 0.25f));
    FBY_CHECK(ScopedGraph.SetBusRouting(ScopedChild, Fby::kMusicBus) == Fby::DspResult::Ok);
    FBY_CHECK(ScopedGraph.SetBusGain(Fby::kMusicBus, 0.5f) == Fby::DspResult::Ok);
    ScopedGraph.Process(ScopedOutput, 64);
    FBY_CHECK(NearlyEqual(ScopedOutput[0], 0.4f * 0.5f * 0.5f));

    ScopedGraph.Shutdown();
}

// 3: cycle rejection
void TestCycleRejection()
{
    Fby::DspGraph ScopedGraph;
    FBY_CHECK(ScopedGraph.Initialize(48000));

    Fby::BusId ScopedA = Fby::kInvalidBus;
    Fby::BusId ScopedB = Fby::kInvalidBus;
    FBY_CHECK(ScopedGraph.CreateBus(Fby::kMasterBus, 1.0f, &ScopedA) == Fby::DspResult::Ok);
    FBY_CHECK(ScopedGraph.CreateBus(ScopedA, 1.0f, &ScopedB) == Fby::DspResult::Ok);
    FBY_CHECK(ScopedGraph.ParentBus(ScopedB) == ScopedA);

    // a under b would close a -> b -> a.
    FBY_CHECK(ScopedGraph.SetBusRouting(ScopedA, ScopedB) == Fby::DspResult::Cycle);
    FBY_CHECK(ScopedGraph.ParentBus(ScopedA) == Fby::kMasterBus);
    FBY_CHECK(ScopedGraph.ParentBus(ScopedB) == ScopedA);

    FBY_CHECK(ScopedGraph.SetBusRouting(ScopedA, ScopedA) == Fby::DspResult::Cycle);
    FBY_CHECK(ScopedGraph.SetBusRouting(Fby::kMasterBus, ScopedA) == Fby::DspResult::InvalidArgument);
    FBY_CHECK(ScopedGraph.DestroyBus(Fby::kMasterBus) == Fby::DspResult::InvalidArgument);
    FBY_CHECK(ScopedGraph.CreateBus(static_cast<Fby::BusId>(Fby::kMaxBuses), 1.0f, nullptr) ==
              Fby::DspResult::InvalidArgument);

    // The graph is still usable after the rejected edges.
    std::vector<float> ScopedClip = MakeStereoClip(0.1f, 0.1f, 32);
    FBY_CHECK(ScopedGraph.PlayOneShot(ClipView(ScopedClip), ScopedB, 1.0f, 1.0f, true, nullptr) == Fby::DspResult::Ok);
    float ScopedOutput[32 * 2] = {};
    ScopedGraph.Process(ScopedOutput, 32);
    FBY_CHECK(NearlyEqual(ScopedOutput[0], 0.1f));

    ScopedGraph.Shutdown();
}

// 4: ducking
void TestDucking()
{
    Fby::DspGraph ScopedGraph;
    FBY_CHECK(ScopedGraph.Initialize(48000));

    std::vector<float> ScopedMusic = MakeStereoClip(0.5f, 0.5f, 256);
    std::vector<float> ScopedSfx = MakeStereoClip(0.8f, 0.8f, 256);
    std::uint32_t ScopedMusicVoice = Fby::kInvalidVoice;
    std::uint32_t ScopedSfxVoice = Fby::kInvalidVoice;
    FBY_CHECK(ScopedGraph.PlayOneShot(ClipView(ScopedMusic), Fby::kMusicBus, 1.0f, 1.0f, true, &ScopedMusicVoice) ==
              Fby::DspResult::Ok);
    FBY_CHECK(ScopedGraph.PlayOneShot(ClipView(ScopedSfx), Fby::kSfxBus, 1.0f, 1.0f, true, &ScopedSfxVoice) ==
              Fby::DspResult::Ok);

    float ScopedOutput[256 * 2] = {};
    ScopedGraph.Process(ScopedOutput, 256);
    FBY_CHECK(NearlyEqual(ScopedGraph.BusPeak(Fby::kMusicBus), 0.5f, 1.0e-4f));

    Fby::DuckingParams ScopedDucking;
    ScopedDucking.sourceBus = Fby::kSfxBus;
    ScopedDucking.threshold = 0.1f;
    ScopedDucking.ratio = 4.0f;
    ScopedDucking.depth = 0.9f;
    ScopedDucking.attackSeconds = 0.001f;
    ScopedDucking.releaseSeconds = 0.02f;
    FBY_CHECK(ScopedGraph.SetBusDucking(Fby::kMusicBus, ScopedDucking) == Fby::DspResult::Ok);

    // Two blocks: the first contains the attack ramp (the peak of a block is its
    ScopedGraph.Process(ScopedOutput, 256);
    ScopedGraph.Process(ScopedOutput, 256);
    const float ScopedDuckedPeak = ScopedGraph.BusPeak(Fby::kMusicBus);
    FBY_CHECK(ScopedDuckedPeak < 0.2f);
    FBY_CHECK(ScopedDuckedPeak > 0.02f);
    // The source bus itself is untouched by the sidechain.
    FBY_CHECK(NearlyEqual(ScopedGraph.BusPeak(Fby::kSfxBus), 0.8f, 1.0e-4f));

    // Release: stop the loud source; the detector falls and the music gain
    (void)ScopedGraph.StopVoice(ScopedSfxVoice);
    for (int ScopedBlock = 0; ScopedBlock < 200; ++ScopedBlock)
    {
        ScopedGraph.Process(ScopedOutput, 256);
    }
    FBY_CHECK(ScopedGraph.BusPeak(Fby::kMusicBus) > 0.45f);

    ScopedGraph.Shutdown();
}

// 5: reverb tail
void TestReverb()
{
    constexpr std::uint32_t ScopedKTotalFrames = 24000;
    const auto ScopedRunImpulse = [](bool InWithReverb) -> std::pair<float, float>
    {
        Fby::DspGraph ScopedGraph;
        if (!ScopedGraph.Initialize(48000))
        {
            return {0.0f, 0.0f};
        }
        // One stereo frame
        std::vector<float> ScopedImpulse = {1.0f, 1.0f};
        if (InWithReverb)
        {
            Fby::ReverbParams ScopedReverb;
            ScopedReverb.enabled = true;
            ScopedReverb.roomSize = 0.5f;
            ScopedReverb.damping = 0.3f;
            ScopedReverb.wet = 0.6f;
            (void)ScopedGraph.SetBusReverb(Fby::kSfxBus, ScopedReverb);
        }
        (void)ScopedGraph.PlayOneShot(ClipView(ScopedImpulse), Fby::kSfxBus, 1.0f, 1.0f, false, nullptr);

        float ScopedEarly = 0.0f;
        float ScopedLate = 0.0f;
        std::uint32_t ScopedPosition = 0;
        float ScopedBlock[256 * 2] = {};
        while (ScopedPosition < ScopedKTotalFrames)
        {
            const std::uint32_t ScopedFrames = std::min<std::uint32_t>(256, ScopedKTotalFrames - ScopedPosition);
            ScopedGraph.Process(ScopedBlock, ScopedFrames);
            for (std::uint32_t ScopedFrame = 0; ScopedFrame < ScopedFrames; ++ScopedFrame)
            {
                const std::uint32_t ScopedAbsolute = ScopedPosition + ScopedFrame;
                const float ScopedEnergy = Energy(ScopedBlock + static_cast<std::size_t>(ScopedFrame) * 2, 1);
                if (ScopedAbsolute >= 1500 && ScopedAbsolute < 4000)
                {
                    ScopedEarly += ScopedEnergy;
                }
                else if (ScopedAbsolute >= 20000)
                {
                    ScopedLate += ScopedEnergy;
                }
            }
            ScopedPosition += ScopedFrames;
        }
        ScopedGraph.Shutdown();
        return {ScopedEarly, ScopedLate};
    };

    const auto ScopedDry = ScopedRunImpulse(false);
    FBY_CHECK(ScopedDry.first == 0.0f);
    FBY_CHECK(ScopedDry.second == 0.0f);

    const auto ScopedWet = ScopedRunImpulse(true);
    // A tail exists after the first comb delay
    FBY_CHECK(ScopedWet.first > 0.01f);
    // It is still audible much later
    FBY_CHECK(ScopedWet.second > 0.0f);
    // And it decays
    FBY_CHECK(ScopedWet.second < ScopedWet.first);
}

// 6: SPSC ring under two threads
void TestCommandRingConcurrency()
{
    constexpr std::uint32_t ScopedKCommandCount = 20000;
    Fby::CommandQueue ScopedQueue;
    std::atomic<bool> ScopedStart{false};
    std::atomic<int> ScopedErrors{0};
    std::atomic<std::uint32_t> ScopedReceived{0};

    std::thread ScopedProducer(
        [&ScopedQueue, &ScopedStart, &ScopedErrors]
        {
            while (!ScopedStart.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }
            for (std::uint32_t ScopedSequence = 0; ScopedSequence < ScopedKCommandCount; ++ScopedSequence)
            {
                Fby::AudioCommand ScopedCommand{};
                ScopedCommand.type = Fby::AudioCommandType::SetGain;
                ScopedCommand.generation = ScopedSequence;
                std::uint32_t ScopedSpins = 0;
                while (!ScopedQueue.Push(ScopedCommand))
                {
                    std::this_thread::yield();
                    if (++ScopedSpins > 1000000u)
                    {
                        ScopedErrors.fetch_add(1, std::memory_order_relaxed);
                        return;
                    }
                }
            }
        });

    std::thread ScopedConsumer(
        [&ScopedQueue, &ScopedStart, &ScopedErrors, &ScopedReceived]
        {
            while (!ScopedStart.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }
            std::uint32_t ScopedExpected = 0;
            std::uint64_t ScopedIdleSpins = 0;
            while (ScopedExpected < ScopedKCommandCount)
            {
                Fby::AudioCommand ScopedCommand{};
                if (!ScopedQueue.Pop(&ScopedCommand))
                {
                    std::this_thread::yield();
                    if (++ScopedIdleSpins > 200000000ull)
                    {
                        ScopedErrors.fetch_add(1, std::memory_order_relaxed);
                        return;
                    }
                    continue;
                }
                if (ScopedCommand.generation != ScopedExpected || ScopedCommand.type != Fby::AudioCommandType::SetGain)
                {
                    ScopedErrors.fetch_add(1, std::memory_order_relaxed);
                    return;
                }
                ++ScopedExpected;
                ScopedReceived.store(ScopedExpected, std::memory_order_release);
            }
        });

    ScopedStart.store(true, std::memory_order_release);
    ScopedProducer.join();
    ScopedConsumer.join();

    FBY_CHECK(ScopedErrors.load() == 0);
    FBY_CHECK(ScopedReceived.load() == ScopedKCommandCount);
    FBY_CHECK(ScopedQueue.Size() == 0u);
}

// 7: full-ring rejection
void TestFullRingRejection()
{
    Fby::CommandQueue ScopedQueue;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < Fby::kMaxCommands; ++ScopedIndex)
    {
        Fby::AudioCommand ScopedCommand{};
        ScopedCommand.generation = ScopedIndex;
        FBY_CHECK(ScopedQueue.Push(ScopedCommand));
    }
    Fby::AudioCommand ScopedOverflow{};
    // Dropped, never blocking
    FBY_CHECK(!ScopedQueue.Push(ScopedOverflow));
    FBY_CHECK(ScopedQueue.DroppedCount() == 1u);
    FBY_CHECK(ScopedQueue.Size() == Fby::kMaxCommands);

    // One pop frees a slot; the ring stays ordered.
    Fby::AudioCommand ScopedFirst{};
    FBY_CHECK(ScopedQueue.Pop(&ScopedFirst));
    FBY_CHECK(ScopedFirst.generation == 0u);
    Fby::AudioCommand ScopedReplacement{};
    ScopedReplacement.generation = Fby::kMaxCommands;
    FBY_CHECK(ScopedQueue.Push(ScopedReplacement));

    for (std::uint32_t ScopedExpected = 1; ScopedExpected <= Fby::kMaxCommands; ++ScopedExpected)
    {
        Fby::AudioCommand ScopedCommand{};
        FBY_CHECK(ScopedQueue.Pop(&ScopedCommand));
        FBY_CHECK(ScopedCommand.generation == ScopedExpected);
    }
    FBY_CHECK(ScopedQueue.Size() == 0u);
    FBY_CHECK(!ScopedQueue.Pop(&ScopedOverflow));

    // The graph's public command path behaves the same way.
    Fby::DspGraph ScopedGraph;
    FBY_CHECK(ScopedGraph.Initialize(48000));
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < Fby::kMaxCommands; ++ScopedIndex)
    {
        Fby::AudioCommand ScopedCommand{};
        ScopedCommand.type = Fby::AudioCommandType::None;
        FBY_CHECK(ScopedGraph.PushCommand(ScopedCommand));
    }
    Fby::AudioCommand ScopedCommand{};
    FBY_CHECK(!ScopedGraph.PushCommand(ScopedCommand));
    FBY_CHECK(ScopedGraph.DroppedCommandCount() == 1u);
    FBY_CHECK(ScopedGraph.QueuedCommandCount() == Fby::kMaxCommands);
    float ScopedOutput[4] = {};
    ScopedGraph.Process(ScopedOutput, 2);
    FBY_CHECK(ScopedGraph.QueuedCommandCount() == 0u);
    FBY_CHECK(ScopedGraph.PushCommand(ScopedCommand));
    ScopedGraph.Shutdown();
}

// 8: no allocation on the audio thread
void TestProcessDoesNotAllocate()
{
    Fby::DspGraph ScopedGraph;
    FBY_CHECK(ScopedGraph.Initialize(48000));

    std::vector<float> ScopedMusic = MakeStereoClip(0.25f, 0.25f, 512);
    std::vector<float> ScopedSfx = MakeStereoClip(0.5f, 0.5f, 512);
    (void)ScopedGraph.PlayOneShot(ClipView(ScopedMusic), Fby::kMusicBus, 1.0f, 1.0f, true, nullptr);
    (void)ScopedGraph.PlayOneShot(ClipView(ScopedSfx), Fby::kSfxBus, 1.0f, 1.0f, true, nullptr);

    Fby::ReverbParams ScopedReverb;
    ScopedReverb.enabled = true;
    ScopedReverb.roomSize = 0.6f;
    ScopedReverb.damping = 0.2f;
    ScopedReverb.wet = 0.3f;
    (void)ScopedGraph.SetBusReverb(Fby::kSfxBus, ScopedReverb);

    Fby::DuckingParams ScopedDucking;
    ScopedDucking.sourceBus = Fby::kSfxBus;
    ScopedDucking.threshold = 0.05f;
    ScopedDucking.ratio = 3.0f;
    ScopedDucking.depth = 0.7f;
    (void)ScopedGraph.SetBusDucking(Fby::kMusicBus, ScopedDucking);

    float ScopedOutput[256 * 2] = {};
    // Warm up: drain the parameter commands first
    ScopedGraph.Process(ScopedOutput, 256);

    GAllocationCount.store(0, std::memory_order_relaxed);
    GTrackAllocations.store(true, std::memory_order_release);
    for (int ScopedIteration = 0; ScopedIteration < 64; ++ScopedIteration)
    {
        ScopedGraph.Process(ScopedOutput, 256);
    }
    GTrackAllocations.store(false, std::memory_order_release);
    FBY_CHECK(GAllocationCount.load(std::memory_order_relaxed) == 0u);

    // The rendered output really did exercise the graph.
    FBY_CHECK(Energy(ScopedOutput, 256) > 0.0f);
    ScopedGraph.Shutdown();
}

// 9: AudioSystem integration (no device)
void TestAudioSystemIntegration()
{
    Fby::AudioSystem ScopedSystem;
    FBY_CHECK(ScopedSystem.Mixer().Initialize(48000));

    std::vector<float> ScopedSamples = MakeStereoClip(0.5f, 0.5f, 64);
    const Fby::AssetId ScopedClipId = Fby::AssetId::FromPath("audio/test.wav", Fby::AssetKind::AudioClip);
    FBY_CHECK(ScopedSystem.RegisterClip(ScopedClipId, ScopedSamples, 2, 48000));

    const std::uint32_t ScopedVoice = ScopedSystem.Play(ScopedClipId, Fby::AudioBus::Sfx, 1.0f, 1.0f, true);
    FBY_CHECK(ScopedVoice != Fby::kInvalidAudioVoice);
    FBY_CHECK(ScopedSystem.BusVolume(Fby::AudioBus::Sfx) == 1.0f);

    float ScopedOutput[64 * 2] = {};
    ScopedSystem.Mix(ScopedOutput, 64);
    FBY_CHECK(NearlyEqual(ScopedOutput[0], 0.5f, 1.0e-4f));

    ScopedSystem.SetBusVolume(Fby::AudioBus::Master, 0.0f);
    FBY_CHECK(ScopedSystem.BusVolume(Fby::AudioBus::Master) == 0.0f);
    ScopedSystem.Mix(ScopedOutput, 64);
    FBY_CHECK(ScopedOutput[0] == 0.0f);
    ScopedSystem.SetBusVolume(Fby::AudioBus::Master, 1.0f);

    ScopedSystem.Stop(ScopedVoice);
    ScopedSystem.Mix(ScopedOutput, 64);
    FBY_CHECK(Energy(ScopedOutput, 64) == 0.0f);

    // ECS path: an AudioSourceComponent becomes a voice through Update().
    Fby::Ecs::Registry ScopedRegistry;
    const auto ScopedEntity = ScopedRegistry.create();
    Fby::AudioSourceComponent ScopedSource;
    ScopedSource.Clip = ScopedClipId;
    ScopedSource.Volume = 0.5f;
    ScopedSource.BusID = Fby::kSfxBus;
    ScopedSource.Playing = true;
    ScopedRegistry.emplace<Fby::AudioSourceComponent>(ScopedEntity, ScopedSource);
    ScopedSystem.Update(ScopedRegistry);
    ScopedSystem.Mix(ScopedOutput, 64);
    FBY_CHECK(Energy(ScopedOutput, 64) > 0.0f);

    ScopedSource.Playing = false;
    ScopedRegistry.replace<Fby::AudioSourceComponent>(ScopedEntity, ScopedSource);
    ScopedSystem.Update(ScopedRegistry);
    ScopedSystem.Mix(ScopedOutput, 64);
    FBY_CHECK(Energy(ScopedOutput, 64) == 0.0f);

    // Bus component: an unassigned bus is created once, then edited in place.
    const auto ScopedBusEntity = ScopedRegistry.create();
    Fby::AudioBusComponent ScopedBus;
    ScopedBus.BusID = Fby::kInvalidBus;
    ScopedBus.ParentBus = Fby::kMasterBus;
    ScopedBus.Volume = 0.5f;
    ScopedRegistry.emplace<Fby::AudioBusComponent>(ScopedBusEntity, ScopedBus);
    ScopedSystem.Update(ScopedRegistry);
    const auto& ScopedCreated = ScopedRegistry.get<Fby::AudioBusComponent>(ScopedBusEntity);
    FBY_CHECK(ScopedCreated.BusID != Fby::kInvalidBus);
    FBY_CHECK(ScopedSystem.Mixer().IsBusActive(ScopedCreated.BusID));

    ScopedSystem.Shutdown();
}
}

// Allocation counting hooks (global). Only new/delete matter for the audio
void* operator new(std::size_t InSize)
{
    if (GTrackAllocations.load(std::memory_order_relaxed))
    {
        GAllocationCount.fetch_add(1, std::memory_order_relaxed);
    }
    void* ScopedPointer = std::malloc(InSize == 0 ? 1 : InSize);
    if (ScopedPointer == nullptr)
    {
        throw std::bad_alloc();
    }
    return ScopedPointer;
}

void* operator new[](std::size_t InSize)
{
    if (GTrackAllocations.load(std::memory_order_relaxed))
    {
        GAllocationCount.fetch_add(1, std::memory_order_relaxed);
    }
    void* ScopedPointer = std::malloc(InSize == 0 ? 1 : InSize);
    if (ScopedPointer == nullptr)
    {
        throw std::bad_alloc();
    }
    return ScopedPointer;
}

void* operator new(std::size_t InSize, const std::nothrow_t&) noexcept
{
    return std::malloc(InSize == 0 ? 1 : InSize);
}

void* operator new[](std::size_t InSize, const std::nothrow_t&) noexcept
{
    return std::malloc(InSize == 0 ? 1 : InSize);
}

void operator delete(void* InPointer) noexcept
{
    std::free(InPointer);
}

void operator delete[](void* InPointer) noexcept
{
    std::free(InPointer);
}

void operator delete(void* InPointer, std::size_t) noexcept
{
    std::free(InPointer);
}

void operator delete[](void* InPointer, std::size_t) noexcept
{
    std::free(InPointer);
}

void operator delete(void* InPointer, const std::nothrow_t&) noexcept
{
    std::free(InPointer);
}

void operator delete[](void* InPointer, const std::nothrow_t&) noexcept
{
    std::free(InPointer);
}

int main()
{
    TestRoutingAndGains();
    TestCycleRejection();
    TestDucking();
    TestReverb();
    TestCommandRingConcurrency();
    TestFullRingRejection();
    TestProcessDoesNotAllocate();
    TestAudioSystemIntegration();

    if (GFailures == 0)
    {
        std::printf("dsp_mixer_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "dsp_mixer_test: %d check(s) failed\n", GFailures);
    return 1;
}
