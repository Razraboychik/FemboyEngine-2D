// FemboyEngine2D - four-player voice call over UDP relay and live device/tier switch.
#include "Engine/Audio/AudioDevices.hpp"
#include "Engine/Audio/AudioStream.hpp"
#include "Engine/Audio/VoiceAec.hpp"
#include "Engine/Audio/VoiceChannel.hpp"
#include "Engine/Audio/VoiceDsp.hpp"
#include "Engine/Network/Transport.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <thread>
#include <vector>

namespace
{
using Fby::Net::DatagramSocket;
using Fby::Net::NetAddress;
using Fby::Net::NetTransport;
using Fby::Net::TransportChannel;

std::uint32_t GFailures = 0;

#define FBY_CHECK(condition)                                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                  \
            ++GFailures;                                                                                              \
        }                                                                                                              \
    } while (false)

constexpr std::uint8_t kServerPlayer = 7;

[[nodiscard]] std::vector<float> Sine(std::uint32_t InFrames, float InAmplitude = 0.3f)
{
    std::vector<float> ScopedSamples(InFrames);
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < InFrames; ++ScopedIndex)
    {
        ScopedSamples[ScopedIndex] = InAmplitude * std::sin(2.0f * 3.14159265f * 440.0f *
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

struct Endpoint
{
    NetTransport Transport;
    Fby::VoiceChannel Channel;
    NetAddress Peers[8]{};
    std::vector<float> CaptureStorage;
    Fby::FloatRing CaptureRing;
};

bool SendThunk(void* InUser, std::uint8_t InToPlayer,
               std::span<const std::uint8_t> InPacket) noexcept
{
    auto* ScopedEndpoint = static_cast<Endpoint*>(InUser);
    if (ScopedEndpoint == nullptr || InToPlayer >= 8 || ScopedEndpoint->Peers[InToPlayer].Port == 0)
    {
        return false;
    }
    return ScopedEndpoint->Transport.Send(
        ScopedEndpoint->Peers[InToPlayer],
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(InPacket.data()), InPacket.size()),
        TransportChannel::Unreliable);
}

void PumpThunk(void* InUser, const NetAddress& InFrom,
               std::span<const std::byte> InPacket) noexcept
{
    auto* ScopedEndpoint = static_cast<Endpoint*>(InUser);
    if (ScopedEndpoint == nullptr || InPacket.size() < 12)
    {
        return;
    }
    const auto* ScopedBytes = reinterpret_cast<const std::uint8_t*>(InPacket.data());
    const std::uint8_t ScopedSpeaker = ScopedBytes[6];
    if (ScopedSpeaker >= 8)
    {
        return;
    }
    ScopedEndpoint->Peers[ScopedSpeaker] = InFrom;
    (void)ScopedEndpoint->Channel.ReceivePacket(
        ScopedSpeaker, std::span<const std::uint8_t>(ScopedBytes, InPacket.size()));
}

void TestUdpRelayCall()
{
    using namespace Fby;

    std::string ScopedError;
    if (!DatagramSocket::EnsureSocketsInitialized(&ScopedError))
    {
        std::printf("voice_call_test: sockets unavailable, UDP skipped\n");
        return;
    }

    Endpoint ScopedServer;
    Endpoint ScopedClients[3];
    if (!ScopedServer.Transport.Open(0, &ScopedError))
    {
        std::printf("voice_call_test: server bind failed, UDP skipped\n");
        return;
    }
    NetAddress ScopedServerAddress{};
    ScopedServerAddress.Host = 0x7F000001u;
    ScopedServerAddress.Port = ScopedServer.Transport.LocalPort();
    for (Endpoint& ScopedClient : ScopedClients)
    {
        if (!ScopedClient.Transport.Open(0, &ScopedError) || !ScopedClient.Transport.Connect(ScopedServerAddress))
        {
            std::printf("voice_call_test: client bind failed, UDP skipped\n");
            return;
        }
        ScopedClient.Peers[kServerPlayer] = ScopedServerAddress;
    }

    VoiceChannelConfig ScopedServerConfig{};
    ScopedServerConfig.Routing = VoiceRouting::Relay;
    ScopedServerConfig.LocalPlayer = kServerPlayer;
    ScopedServerConfig.IsRelayServer = true;
    if (!ScopedServer.Channel.Start(ScopedServerConfig, &SendThunk, &ScopedServer))
    {
        std::printf("voice_call_test: Opus unavailable, UDP skipped\n");
        return;
    }
    FBY_CHECK(ScopedServer.Channel.AddParticipant(0));
    FBY_CHECK(ScopedServer.Channel.AddParticipant(1));
    FBY_CHECK(ScopedServer.Channel.AddParticipant(2));

    for (std::uint8_t ScopedIndex = 0; ScopedIndex < 3; ++ScopedIndex)
    {
        VoiceChannelConfig ScopedConfig{};
        ScopedConfig.Routing = VoiceRouting::Relay;
        ScopedConfig.LocalPlayer = ScopedIndex;
        ScopedConfig.RelayPlayer = kServerPlayer;
        if (!ScopedClients[ScopedIndex].Channel.Start(ScopedConfig, &SendThunk, &ScopedClients[ScopedIndex]))
        {
            std::printf("voice_call_test: Opus unavailable, UDP skipped\n");
            return;
        }
        FBY_CHECK(ScopedClients[ScopedIndex].Channel.AddParticipant(kServerPlayer));
        for (std::uint8_t ScopedOther = 0; ScopedOther < 3; ++ScopedOther)
        {
            if (ScopedOther != ScopedIndex)
            {
                FBY_CHECK(ScopedClients[ScopedIndex].Channel.AddParticipant(ScopedOther));
            }
        }
    }

    // Every client announces itself once so the relay learns all addresses.
    for (Endpoint& ScopedClient : ScopedClients)
    {
        ScopedClient.CaptureStorage.resize(kVoiceFrameSamples * 4);
        ScopedClient.CaptureRing.Configure(ScopedClient.CaptureStorage.data(),
                                           static_cast<std::uint32_t>(ScopedClient.CaptureStorage.size()), 1);
        const std::vector<float> ScopedFrames = Sine(kVoiceFrameSamples);
        (void)ScopedClient.CaptureRing.Write(ScopedFrames.data(), kVoiceFrameSamples);
        ScopedClient.Channel.SetCaptureRing(&ScopedClient.CaptureRing);
        ScopedClient.Channel.Tick(0.02f, nullptr);
    }

    for (std::uint32_t ScopedStep = 0; ScopedStep < 12; ++ScopedStep)
    {
        (void)ScopedServer.Transport.Poll(&PumpThunk, &ScopedServer);
        for (Endpoint& ScopedClient : ScopedClients)
        {
            (void)ScopedClient.Transport.Poll(&PumpThunk, &ScopedClient);
        }
        ScopedServer.Transport.Tick(0.01f);
        for (Endpoint& ScopedClient : ScopedClients)
        {
            ScopedClient.Transport.Tick(0.01f);
        }
        for (Endpoint& ScopedClient : ScopedClients)
        {
            ScopedClient.Channel.Tick(0.02f, nullptr);
        }
    }
    FBY_CHECK(ScopedServer.Channel.Stats().PacketsForwarded >= 1);

    // Client 0 speaks; the relay forwards to 1 and 2, which decode it.
    const std::vector<float> ScopedSpeech = Sine(kVoiceFrameSamples);
    (void)ScopedClients[0].CaptureRing.Write(ScopedSpeech.data(), kVoiceFrameSamples);
    ScopedClients[0].Channel.Tick(0.02f, nullptr);
    for (std::uint32_t ScopedStep = 0; ScopedStep < 10; ++ScopedStep)
    {
        (void)ScopedServer.Transport.Poll(&PumpThunk, &ScopedServer);
        for (Endpoint& ScopedClient : ScopedClients)
        {
            (void)ScopedClient.Transport.Poll(&PumpThunk, &ScopedClient);
        }
        for (std::uint8_t ScopedClient = 1; ScopedClient < 3; ++ScopedClient)
        {
            ScopedClients[ScopedClient].Channel.Tick(0.02f, nullptr);
        }
        ScopedServer.Transport.Tick(0.005f);
        for (Endpoint& ScopedClient : ScopedClients)
        {
            ScopedClient.Transport.Tick(0.005f);
        }
    }

    for (std::uint8_t ScopedClient = 1; ScopedClient < 3; ++ScopedClient)
    {
        FloatRing* ScopedRing = ScopedClients[ScopedClient].Channel.ParticipantRing(0);
        FBY_CHECK(ScopedRing != nullptr);
        if (ScopedRing != nullptr)
        {
            FBY_CHECK(ScopedRing->AvailableRead() > 0);
            FBY_CHECK(RingRms(*ScopedRing) > 0.005f);
        }
    }

    ScopedServer.Channel.Stop();
    for (Endpoint& ScopedClient : ScopedClients)
    {
        ScopedClient.Channel.Stop();
        ScopedClient.Transport.Close();
    }
    ScopedServer.Transport.Close();
}

void TestDeviceSwitchAndTiers()
{
    using namespace Fby;

    AudioDeviceEnumerator ScopedDevices;
    std::string ScopedError;
    if (ScopedDevices.Initialize(&ScopedError))
    {
        const AudioDeviceList& ScopedInputs = ScopedDevices.Devices(AudioDeviceKind::Input);
        if (ScopedInputs.Count > 0)
        {
            std::vector<float> ScopedStorage(kVoiceFrameSamples * 8);
            FloatRing ScopedRing;
            ScopedRing.Configure(ScopedStorage.data(), static_cast<std::uint32_t>(ScopedStorage.size()), 1);
            for (std::uint32_t ScopedDevice = 0; ScopedDevice < ScopedInputs.Count && ScopedDevice < 2;
                 ++ScopedDevice)
            {
                VoiceCapture ScopedCapture;
                if (ScopedCapture.Start(ScopedDevices, ScopedInputs.Devices[ScopedDevice].Id, &ScopedRing, 48000,
                                        &ScopedError))
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(30));
                    ScopedCapture.Stop();
                    FBY_CHECK(!ScopedCapture.Active());
                }
            }
        }
    }

    // Every denoise tier must accept a live block without producing non-finite
    // samples, which is what a device/tier switch does at runtime.
    VoiceDspChain ScopedChain;
    const VoiceDenoiseTier ScopedTiers[] = {VoiceDenoiseTier::None, VoiceDenoiseTier::Gate,
                                            VoiceDenoiseTier::Spectral, VoiceDenoiseTier::Rnnoise};
    for (const VoiceDenoiseTier ScopedTier : ScopedTiers)
    {
        VoiceDspSettings ScopedSettings{};
        ScopedSettings.Denoise = ScopedTier;
        ScopedChain.Configure(ScopedSettings, 48000);
        std::vector<float> ScopedBlock = Sine(2048);
        ScopedChain.Process(ScopedBlock.data(), static_cast<std::uint32_t>(ScopedBlock.size()));
        for (const float ScopedSample : ScopedBlock)
        {
            FBY_CHECK(std::isfinite(ScopedSample));
        }
    }

    VoiceAecSettings ScopedAecSettings{};
    VoiceAec ScopedAec;
    ScopedAec.Configure(ScopedAecSettings, 48000);
    std::vector<float> ScopedMic(480, 0.0f);
    std::vector<float> ScopedReference(480, 0.0f);
    ScopedAec.Process(ScopedMic.data(), ScopedReference.data(), 480);
    FBY_CHECK(std::isfinite(ScopedAec.ErleDb()));
}
}

int main()
{
    TestUdpRelayCall();
    TestDeviceSwitchAndTiers();

    if (GFailures == 0)
    {
        std::printf("voice_call_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "voice_call_test: %u failure(s)\n", GFailures);
    return 1;
}
