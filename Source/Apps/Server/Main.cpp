// FemboyEngine2D - dedicated server application.
#include "Engine/Core/Log.hpp"
#include "Engine/Core/CrashHandler.hpp"
#include "Engine/Core/Time.hpp"
#include "Engine/Network/NetLink.hpp"
#include "Engine/Network/Session.hpp"
#include "Engine/Network/Transport.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

namespace
{
struct Options
{
    std::uint16_t Port = 27015;
    std::uint32_t TickRate = 120;
    std::uint32_t Frames = 0;
    char Name[Fby::Net::kSessionNameBytes] = "Dedicated";
};

void PrintUsage()
{
    std::printf("fby_server - FemboyEngine2D dedicated server\n"
                "  --port <n>        UDP port (default 27015)\n"
                "  --tick-rate <n>   fixed ticks per second (default 120)\n"
                "  --name <text>     session name\n"
                "  --frames <n>      exit after n frames (0 = run forever)\n");
}

[[nodiscard]] bool ParseOptions(int InCount, char** InArgs, Options* OutOptions)
{
    for (int ScopedIndex = 1; ScopedIndex < InCount; ++ScopedIndex)
    {
        const char* ScopedArg = InArgs[ScopedIndex];
        if (std::strcmp(ScopedArg, "--help") == 0)
        {
            PrintUsage();
            return false;
        }
        if (ScopedIndex + 1 >= InCount)
        {
            return false;
        }
        const char* ScopedValue = InArgs[++ScopedIndex];
        if (std::strcmp(ScopedArg, "--port") == 0)
        {
            OutOptions->Port = static_cast<std::uint16_t>(std::atoi(ScopedValue));
        }
        else if (std::strcmp(ScopedArg, "--tick-rate") == 0)
        {
            OutOptions->TickRate = static_cast<std::uint32_t>(std::atoi(ScopedValue));
        }
        else if (std::strcmp(ScopedArg, "--frames") == 0)
        {
            OutOptions->Frames = static_cast<std::uint32_t>(std::atoi(ScopedValue));
        }
        else if (std::strcmp(ScopedArg, "--name") == 0)
        {
            std::snprintf(OutOptions->Name, sizeof(OutOptions->Name), "%s", ScopedValue);
        }
        else
        {
            return false;
        }
    }
    return true;
}

void OnSlotChanged(void* InUser, std::uint8_t InPlayerId, bool InPresent)
{
    (void)InUser;
    std::printf("[server] player %u %s\n", static_cast<unsigned>(InPlayerId), InPresent ? "joined" : "left");
}

void OnEvent(void* InUser, std::uint8_t InFromPlayer, std::uint32_t InEventId,
             std::span<const std::byte> InPayload)
{
    (void)InUser;
    std::printf("[server] event %u from player %u (%zu bytes)\n", InEventId,
                static_cast<unsigned>(InFromPlayer), InPayload.size());
}

void OnTimeSync(void* InUser, std::uint64_t InServerTick, std::uint32_t InRttMs)
{
    (void)InUser;
    (void)InServerTick;
    (void)InRttMs;
}
}

int main(int InCount, char** InArgs)
{
    Fby::Core::InstallCrashHandler("fby_server");
    Fby::SetLogCategory("server");

    using namespace Fby::Net;

    Options ScopedOptions;
    if (!ParseOptions(InCount, InArgs, &ScopedOptions))
    {
        return 1;
    }
    if (ScopedOptions.TickRate == 0)
    {
        ScopedOptions.TickRate = 120;
    }

    std::string ScopedError;
    if (!DatagramSocket::EnsureSocketsInitialized(&ScopedError))
    {
        std::fprintf(stderr, "[server] sockets unavailable: %s\n", ScopedError.c_str());
        return 1;
    }
    NetTransport ScopedTransport;
    if (!ScopedTransport.Open(ScopedOptions.Port, &ScopedError))
    {
        std::fprintf(stderr, "[server] bind failed on port %u: %s\n", static_cast<unsigned>(ScopedOptions.Port),
                     ScopedError.c_str());
        return 1;
    }

    NetSessionConfig ScopedConfig{};
    ScopedConfig.Mode = NetSessionMode::Dedicated;
    ScopedConfig.Headless = true;
    ScopedConfig.FixedDtSeconds = 1.0f / static_cast<float>(ScopedOptions.TickRate);
    std::snprintf(ScopedConfig.LocalName, sizeof(ScopedConfig.LocalName), "%s", ScopedOptions.Name);

    NetSessionCallbacks ScopedCallbacks{};
    ScopedCallbacks.OnSlotChanged = &OnSlotChanged;
    ScopedCallbacks.OnEvent = &OnEvent;
    ScopedCallbacks.OnTimeSync = &OnTimeSync;

    NetSession ScopedSession;
    ScopedSession.SetCallbacks(ScopedCallbacks);
    const NetLink ScopedLink = MakeTransportLink(&ScopedTransport);
    if (!ScopedSession.Start(ScopedLink, ScopedConfig))
    {
        std::fprintf(stderr, "[server] session start failed: %s\n",
                     NetSessionStatusName(ScopedSession.Status()));
        return 1;
    }

    std::printf("[server] listening on port %u, %u ticks/s, role %s\n",
                static_cast<unsigned>(ScopedTransport.LocalPort()), ScopedOptions.TickRate,
                NetRoleName(ScopedSession.Role()));

    Fby::Core::Clock ScopedClock;
    ScopedClock.SetMaxDelta(1.0);
    std::uint32_t ScopedFrames = 0;
    for (;;)
    {
        ScopedClock.Tick();
        const float ScopedDt = static_cast<float>(ScopedClock.Delta());

        ScopedSession.Pump();
        ScopedSession.Advance(ScopedDt);
        ScopedTransport.Tick(ScopedDt);
        Fby::Core::SleepMilliseconds(1.0);

        ++ScopedFrames;
        if (ScopedOptions.Frames != 0 && ScopedFrames >= ScopedOptions.Frames)
        {
            break;
        }
    }

    ScopedSession.Stop();
    ScopedTransport.Close();
    std::printf("[server] stopped after %u frames, tick %llu\n", ScopedFrames,
                static_cast<unsigned long long>(ScopedSession.LocalTick()));
    return 0;
}
