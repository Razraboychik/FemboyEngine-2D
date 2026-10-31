// FemboyEngine2D - sandbox PIE client: arguments, snapshot receive and host timeout.
#include "PieClient.hpp"
#include "SandboxOptions.hpp"

#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/Snapshot.hpp"
#include "Engine/Network/NetLink.hpp"
#include "Engine/Network/Replication.hpp"
#include "Engine/Network/Transport.hpp"

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <initializer_list>
#include <span>
#include <string>
#include <thread>
#include <vector>

#if defined(FBY_SANDBOX_AVAILABLE)
#if defined(_WIN32)
#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <chrono>
#include <thread>
extern char** environ;
#endif
#endif

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

[[nodiscard]] bool ParseArgs(std::initializer_list<const char*> InArgs, Fby::Sandbox::SandboxOptions* OutOptions,
                             std::string* OutError)
{
    std::vector<std::string> ScopedStorage;
    for (const char* ScopedArg : InArgs)
    {
        ScopedStorage.emplace_back(ScopedArg);
    }
    std::vector<char*> ScopedArgv;
    for (std::string& ScopedArg : ScopedStorage)
    {
        ScopedArgv.push_back(ScopedArg.data());
    }
    return Fby::Sandbox::ParseSandboxOptions(static_cast<int>(ScopedArgv.size()), ScopedArgv.data(), OutOptions,
                                             OutError);
}

[[nodiscard]] float FirstX(Fby::Ecs::Registry& InRegistry)
{
    for (const Fby::Entity ScopedEntity : InRegistry.view<Fby::Transform2D>())
    {
        return InRegistry.get<Fby::Transform2D>(ScopedEntity).Position.X;
    }
    return -1.0f;
}

void SpawnSprites(Fby::Ecs::Registry& InRegistry)
{
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < 2; ++ScopedIndex)
    {
        const Fby::Entity ScopedEntity = InRegistry.create();
        InRegistry.emplace<Fby::Transform2D>(
            ScopedEntity, Fby::Transform2D{Fby::Vector2D{10.0f, 20.0f}, Fby::Vector2D{1.0f, 1.0f}, 0.0f});
        InRegistry.emplace<Fby::Velocity>(ScopedEntity, Fby::Velocity{});
        InRegistry.emplace<Fby::Sprite2D>(ScopedEntity, Fby::Sprite2D{});
        InRegistry.emplace<Fby::DrawOrder>(ScopedEntity, Fby::DrawOrder{0, 0.0f});
    }
}

// Pumps both sides of the loopback UDP link for a bounded number of steps.
template <typename DoneFn>
void Pump(Fby::Sandbox::PieClient& InClient, Fby::Net::SnapshotSender& InSender, Fby::Net::NetTransport& InHost,
          std::uint64_t* InNowMs, int InSteps, DoneFn InDone)
{
    for (int ScopedStep = 0; ScopedStep < InSteps && !InDone(); ++ScopedStep)
    {
        *InNowMs += 10;
        InSender.Tick(*InNowMs);
        InHost.Tick(0.01f);
        InClient.Tick(*InNowMs, 0.01f);
    }
}

#if defined(FBY_SANDBOX_AVAILABLE)
// Applications live in the output root and the test binaries in its tests/
// directory, so the sandbox is looked up next to the test, then one and two
// levels up. FBY_SANDBOX_PATH overrides the search for a custom layout.
[[nodiscard]] std::string SandboxExecutablePath(const char* InArgv0)
{
    if (const char* ScopedEnvironment = std::getenv("FBY_SANDBOX_PATH");
        ScopedEnvironment != nullptr && ScopedEnvironment[0] != '\0')
    {
        return std::string(ScopedEnvironment);
    }
    const std::filesystem::path ScopedSelf =
        InArgv0 != nullptr ? std::filesystem::path(InArgv0) : std::filesystem::path();
    const std::filesystem::path ScopedDirectory =
        ScopedSelf.has_parent_path() ? ScopedSelf.parent_path() : std::filesystem::current_path();
#if defined(_WIN32)
    constexpr const char* kSuffix = ".exe";
#else
    constexpr const char* kSuffix = "";
#endif
    const std::string ScopedName = std::string("fby_sandbox") + kSuffix;
    for (const std::filesystem::path& ScopedCandidate :
         {ScopedDirectory, ScopedDirectory.parent_path(), ScopedDirectory.parent_path().parent_path()})
    {
        std::error_code ScopedError;
        if (std::filesystem::is_regular_file(ScopedCandidate / ScopedName, ScopedError))
        {
            return (ScopedCandidate / ScopedName).string();
        }
    }
    return (ScopedDirectory / ScopedName).string();
}

struct ChildProcess
{
#if defined(_WIN32)
    void* Handle = nullptr;
#else
    int Pid = -1;
#endif

    [[nodiscard]] bool Valid() const noexcept
    {
#if defined(_WIN32)
        return Handle != nullptr;
#else
        return Pid > 0;
#endif
    }
};

[[nodiscard]] ChildProcess StartSandbox(const std::string& InExecutable, const std::string& InArguments)
{
    ChildProcess ScopedChild;
#if defined(_WIN32)
    std::string ScopedLine = std::string("\"") + InExecutable + "\" " + InArguments;
    std::vector<char> ScopedCommand(ScopedLine.begin(), ScopedLine.end());
    ScopedCommand.push_back('\0');
    STARTUPINFOA ScopedStartup{};
    ScopedStartup.cb = sizeof(ScopedStartup);
    PROCESS_INFORMATION ScopedInfo{};
    if (CreateProcessA(InExecutable.c_str(), ScopedCommand.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                       &ScopedStartup, &ScopedInfo))
    {
        (void)CloseHandle(ScopedInfo.hThread);
        ScopedChild.Handle = ScopedInfo.hProcess;
    }
#else
    std::vector<std::string> ScopedParts;
    ScopedParts.emplace_back(InExecutable);
    std::size_t ScopedBegin = 0;
    while (ScopedBegin < InArguments.size())
    {
        const std::size_t ScopedSpace = InArguments.find(' ', ScopedBegin);
        const std::size_t ScopedEnd = ScopedSpace == std::string::npos ? InArguments.size() : ScopedSpace;
        if (ScopedEnd > ScopedBegin)
        {
            ScopedParts.push_back(InArguments.substr(ScopedBegin, ScopedEnd - ScopedBegin));
        }
        ScopedBegin = ScopedEnd + 1;
    }
    std::vector<char*> ScopedArgv;
    for (std::string& ScopedPart : ScopedParts)
    {
        ScopedArgv.push_back(ScopedPart.data());
    }
    ScopedArgv.push_back(nullptr);
    pid_t ScopedPid = 0;
    if (posix_spawn(&ScopedPid, InExecutable.c_str(), nullptr, nullptr, ScopedArgv.data(), environ) == 0)
    {
        ScopedChild.Pid = static_cast<int>(ScopedPid);
    }
#endif
    return ScopedChild;
}

void KillSandbox(ChildProcess& InChild)
{
#if defined(_WIN32)
    if (InChild.Handle == nullptr)
    {
        return;
    }
    (void)TerminateProcess(static_cast<HANDLE>(InChild.Handle), 0);
    (void)WaitForSingleObject(static_cast<HANDLE>(InChild.Handle), 2000);
    (void)CloseHandle(static_cast<HANDLE>(InChild.Handle));
    InChild.Handle = nullptr;
#else
    if (InChild.Pid <= 0)
    {
        return;
    }
    (void)kill(InChild.Pid, SIGKILL);
    (void)waitpid(InChild.Pid, nullptr, 0);
    InChild.Pid = -1;
#endif
}

// Waits for the child and returns its exit code; a timeout kills it.
[[nodiscard]] bool WaitSandbox(ChildProcess& InChild, std::uint32_t InTimeoutMs, int* OutExitCode)
{
    if (OutExitCode == nullptr)
    {
        return false;
    }
    *OutExitCode = 1;
#if defined(_WIN32)
    if (InChild.Handle == nullptr)
    {
        return false;
    }
    const DWORD ScopedWait = WaitForSingleObject(static_cast<HANDLE>(InChild.Handle), InTimeoutMs);
    if (ScopedWait != WAIT_OBJECT_0)
    {
        KillSandbox(InChild);
        return false;
    }
    DWORD ScopedCode = 1;
    const bool ScopedOk = GetExitCodeProcess(static_cast<HANDLE>(InChild.Handle), &ScopedCode) != 0;
    (void)CloseHandle(static_cast<HANDLE>(InChild.Handle));
    InChild.Handle = nullptr;
    *OutExitCode = static_cast<int>(ScopedCode);
    return ScopedOk;
#else
    if (InChild.Pid <= 0)
    {
        return false;
    }
    const auto ScopedDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(InTimeoutMs);
    for (;;)
    {
        int ScopedStatus = 0;
        const pid_t ScopedResult = waitpid(InChild.Pid, &ScopedStatus, WNOHANG);
        if (ScopedResult == InChild.Pid)
        {
            InChild.Pid = -1;
            if (WIFEXITED(ScopedStatus))
            {
                *OutExitCode = WEXITSTATUS(ScopedStatus);
                return true;
            }
            return false;
        }
        if (std::chrono::steady_clock::now() >= ScopedDeadline)
        {
            KillSandbox(InChild);
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
#endif
}

[[nodiscard]] bool PortFree(std::uint16_t InPort)
{
    Fby::Net::DatagramSocket ScopedProbe;
    if (!ScopedProbe.Open(InPort))
    {
        return false;
    }
    ScopedProbe.Close();
    return true;
}
#endif
}

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    using namespace Fby;

    // 1. Argument parsing: client mode, default listen port and rejections.
    {
        Sandbox::SandboxOptions ScopedOptions;
        std::string ScopedError;
        FBY_CHECK(ParseArgs({"fby_sandbox", "--pie-client", "--connect", "127.0.0.1:41010"}, &ScopedOptions,
                            &ScopedError));
        FBY_CHECK(ScopedOptions.PieClient);
        FBY_CHECK(!ScopedOptions.Headless);
        FBY_CHECK(ScopedOptions.Connect.Host == 0x7F000001u);
        FBY_CHECK(ScopedOptions.Connect.Port == 41010);
        FBY_CHECK(ScopedOptions.ListenPort == 41011);
        FBY_CHECK(ScopedOptions.SpriteCount == 100'000);

        FBY_CHECK(ParseArgs({"fby_sandbox", "--pie-client", "--connect", "127.0.0.1:42000", "--listen", "43000",
                             "--headless"},
                            &ScopedOptions, &ScopedError));
        FBY_CHECK(ScopedOptions.ListenPort == 43000);
        FBY_CHECK(ScopedOptions.Headless);

        FBY_CHECK(!ParseArgs({"fby_sandbox", "--pie-client"}, &ScopedOptions, &ScopedError));
        FBY_CHECK(!ScopedError.empty());
        FBY_CHECK(!ParseArgs({"fby_sandbox", "--pie-client", "--connect", "not-an-address"}, &ScopedOptions,
                             &ScopedError));
        FBY_CHECK(!ParseArgs({"fby_sandbox", "--bogus"}, &ScopedOptions, &ScopedError));
        FBY_CHECK(!ParseArgs({"fby_sandbox", "--headless"}, &ScopedOptions, &ScopedError));

        FBY_CHECK(ParseArgs({"fby_sandbox", "2500"}, &ScopedOptions, &ScopedError));
        FBY_CHECK(!ScopedOptions.PieClient);
        FBY_CHECK(ScopedOptions.SpriteCount == 2500);
    }

    // 2. The headless client receives a baseline and a delta over loopback UDP.
    std::string ScopedSocketError;
    FBY_CHECK(Net::DatagramSocket::EnsureSocketsInitialized(&ScopedSocketError));

    Net::NetTransport ScopedHostTransport;
    FBY_CHECK(ScopedHostTransport.Open(0, &ScopedSocketError));
    FBY_CHECK(ScopedHostTransport.LocalPort() != 0);

    const Ecs::SnapshotSchema ScopedSchema = Ecs::SnapshotSchema::BuiltIns();
    Ecs::Registry ScopedWorld;
    SpawnSprites(ScopedWorld);

    Sandbox::PieClientConfig ScopedConfig;
    ScopedConfig.Host = Net::NetAddress{0x7F000001u, ScopedHostTransport.LocalPort()};
    ScopedConfig.ListenPort = 0;
    ScopedConfig.FirstStateTimeoutMs = 1000;
    ScopedConfig.StateTimeoutMs = 400;

    Sandbox::PieClient ScopedClient;
    FBY_CHECK(ScopedClient.Start(ScopedConfig, ScopedSchema, &ScopedSocketError));
    FBY_CHECK(ScopedClient.Running());
    FBY_CHECK(ScopedClient.ListenPort() != 0);
    FBY_CHECK(!ScopedClient.HasState());
    FBY_CHECK(!ScopedClient.ShouldQuit());

    Net::SnapshotSenderConfig ScopedSenderConfig{};
    Net::SnapshotSender ScopedSender;
    FBY_CHECK(ScopedSender.Start(Net::MakeTransportLink(&ScopedHostTransport),
                                 Net::NetAddress{0x7F000001u, ScopedClient.ListenPort()}, ScopedSenderConfig));

    std::vector<std::byte> ScopedBaseline(1u << 20);
    std::size_t ScopedBaselineBytes = 0;
    DeterministicRng ScopedRng = DeterministicRng::FromSeed(7);
    FBY_CHECK(Ecs::CaptureSnapshot(ScopedWorld, ScopedSchema, ScopedBaseline, 1, ScopedRng, &ScopedBaselineBytes));
    FBY_CHECK(ScopedBaselineBytes > 0);
    FBY_CHECK(ScopedSender.PublishBaseline(1, std::span<const std::byte>(ScopedBaseline.data(), ScopedBaselineBytes),
                                           0));

    std::uint64_t ScopedNow = 0;
    Pump(ScopedClient, ScopedSender, ScopedHostTransport, &ScopedNow, 200,
         [&ScopedClient] { return ScopedClient.HasState(); });
    FBY_CHECK(ScopedClient.HasState());
    FBY_CHECK(ScopedClient.AppliedTick() == 1);
    FBY_CHECK(ScopedClient.World().storage<Entity>().size() == 2);
    FBY_CHECK(std::fabs(FirstX(ScopedClient.World()) - 10.0f) < 0.01f);
    FBY_CHECK(!ScopedClient.ShouldQuit());

    // 3. A delta keeps the entity handles and moves the replicated sprites.
    for (const Entity ScopedEntity : ScopedWorld.view<Transform2D>())
    {
        ScopedWorld.get<Transform2D>(ScopedEntity).Position.X += 100.0f;
    }
    std::vector<std::byte> ScopedDelta(1u << 20);
    std::size_t ScopedDeltaBytes = 0;
    FBY_CHECK(Ecs::CaptureDelta(ScopedWorld, ScopedSchema,
                                std::span<const std::byte>(ScopedBaseline.data(), ScopedBaselineBytes), ScopedDelta, 2,
                                ScopedRng, &ScopedDeltaBytes));
    FBY_CHECK(ScopedDeltaBytes > 0);
    FBY_CHECK(ScopedSender.PublishDelta(2, 1, std::span<const std::byte>(ScopedDelta.data(), ScopedDeltaBytes),
                                        ScopedNow));
    Pump(ScopedClient, ScopedSender, ScopedHostTransport, &ScopedNow, 200,
         [&ScopedClient] { return ScopedClient.AppliedTick() >= 2; });
    FBY_CHECK(ScopedClient.AppliedTick() == 2);
    FBY_CHECK(std::fabs(FirstX(ScopedClient.World()) - 110.0f) < 0.01f);

    // 4. A paused host republishes the last state under the same tick. The
    // receiver ignores it, but the datagram keeps the client from timing out.
    for (int ScopedStep = 0; ScopedStep < 100; ++ScopedStep)
    {
        ScopedNow += 50;
        (void)ScopedSender.PublishDelta(2, 1, std::span<const std::byte>(ScopedDelta.data(), ScopedDeltaBytes),
                                        ScopedNow);
        ScopedSender.Tick(ScopedNow);
        ScopedHostTransport.Tick(0.05f);
        ScopedClient.Tick(ScopedNow, 0.05f);
    }
    FBY_CHECK(!ScopedClient.ShouldQuit());

    // 5. The host stops publishing: the client times out and can be closed.
    for (int ScopedStep = 0; ScopedStep < 200 && !ScopedClient.ShouldQuit(); ++ScopedStep)
    {
        ScopedNow += 10;
        ScopedClient.Tick(ScopedNow, 0.01f);
    }
    FBY_CHECK(ScopedClient.ShouldQuit());
    FBY_CHECK(ScopedClient.TimedOut());
    ScopedClient.Stop();
    FBY_CHECK(!ScopedClient.Running());
    FBY_CHECK(!ScopedClient.HasState());

    // 6. An explicit stop request wins over the timeout.
    Sandbox::PieClient ScopedStoppingClient;
    FBY_CHECK(ScopedStoppingClient.Start(ScopedConfig, ScopedSchema, &ScopedSocketError));
    ScopedStoppingClient.RequestStop();
    FBY_CHECK(ScopedStoppingClient.ShouldQuit());
    FBY_CHECK(!ScopedStoppingClient.TimedOut());
    ScopedStoppingClient.Stop();

    ScopedSender.Stop();
    ScopedHostTransport.Close();

#if defined(FBY_SANDBOX_AVAILABLE)
    // 7. The real sandbox binary consumes the stream in a second process.
    {
        Net::NetTransport ScopedProcessHost;
        FBY_CHECK(ScopedProcessHost.Open(0, &ScopedSocketError));
        std::uint16_t ScopedChildPort = 0;
        for (std::uint32_t ScopedAttempt = 0; ScopedAttempt < 16 && ScopedChildPort == 0; ++ScopedAttempt)
        {
            const std::uint32_t ScopedCandidate =
                static_cast<std::uint32_t>(ScopedProcessHost.LocalPort()) + 1 + ScopedAttempt * 2;
            if (ScopedCandidate <= 65535 && PortFree(static_cast<std::uint16_t>(ScopedCandidate)))
            {
                ScopedChildPort = static_cast<std::uint16_t>(ScopedCandidate);
            }
        }
        FBY_CHECK(ScopedChildPort != 0);
        const std::string ScopedArguments =
            "--pie-client --headless --connect 127.0.0.1:" + std::to_string(ScopedProcessHost.LocalPort()) +
            " --listen " + std::to_string(ScopedChildPort);
        ChildProcess ScopedChild = StartSandbox(SandboxExecutablePath(argv[0]), ScopedArguments);
        FBY_CHECK(ScopedChild.Valid());

        Net::SnapshotSender ScopedProcessSender;
        FBY_CHECK(ScopedProcessSender.Start(Net::MakeTransportLink(&ScopedProcessHost),
                                            Net::NetAddress{0x7F000001u, ScopedChildPort}, ScopedSenderConfig));
        std::vector<std::byte> ScopedProcessState(1u << 20);
        std::size_t ScopedProcessBytes = 0;
        DeterministicRng ScopedProcessRng = DeterministicRng::FromSeed(11);
        FBY_CHECK(Ecs::CaptureSnapshot(ScopedWorld, ScopedSchema, ScopedProcessState, 3, ScopedProcessRng,
                                       &ScopedProcessBytes));
        FBY_CHECK(ScopedProcessSender.PublishBaseline(
            3, std::span<const std::byte>(ScopedProcessState.data(), ScopedProcessBytes), 0));
        std::uint64_t ScopedProcessNow = 0;
        for (int ScopedStep = 0; ScopedStep < 400 && ScopedProcessSender.Stats().Acks == 0; ++ScopedStep)
        {
            ScopedProcessNow += 10;
            ScopedProcessSender.Tick(ScopedProcessNow);
            ScopedProcessHost.Tick(0.01f);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        // An acknowledgement proves the child bound its socket and received the
        // baseline through the same command line the editor uses.
        FBY_CHECK(ScopedProcessSender.Stats().Acks > 0);

        // The child has the state; host silence must end it cleanly.
        int ScopedExitCode = 1;
        FBY_CHECK(WaitSandbox(ScopedChild, 8000, &ScopedExitCode));
        FBY_CHECK(ScopedExitCode == 0);
        ScopedProcessSender.Stop();
        ScopedProcessHost.Close();
    }
#endif

    if (GFailures == 0)
    {
        std::printf("sandbox_client_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "sandbox_client_test: %u failure(s)\n", GFailures);
    return 1;
}
