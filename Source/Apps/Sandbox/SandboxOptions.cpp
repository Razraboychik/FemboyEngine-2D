// FemboyEngine2D - sandbox command-line options implementation.
#include "SandboxOptions.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>

namespace Fby::Sandbox
{
namespace
{
constexpr std::uint16_t kDefaultHostPort = 41010;

[[nodiscard]] bool ParsePort(const char* InText, std::uint16_t* OutPort) noexcept
{
    if (InText == nullptr || *InText == '\0')
    {
        return false;
    }
    char* ScopedEnd = nullptr;
    const unsigned long ScopedValue = std::strtoul(InText, &ScopedEnd, 10);
    if (ScopedEnd == nullptr || *ScopedEnd != '\0' || ScopedValue > 65535ul)
    {
        return false;
    }
    *OutPort = static_cast<std::uint16_t>(ScopedValue);
    return true;
}

[[nodiscard]] bool ParseCount(const char* InText, std::uint32_t* OutCount) noexcept
{
    if (InText == nullptr || *InText == '\0')
    {
        return false;
    }
    char* ScopedEnd = nullptr;
    const unsigned long ScopedValue = std::strtoul(InText, &ScopedEnd, 10);
    if (ScopedEnd == nullptr || *ScopedEnd != '\0' || ScopedValue > 0xFFFFFFFFul)
    {
        return false;
    }
    *OutCount = static_cast<std::uint32_t>(ScopedValue);
    return true;
}
}

void PrintSandboxUsage()
{
    std::printf("fby_sandbox - FemboyEngine2D sandbox\n"
                "  [<sprites>]                 demo entity count (default 100000)\n"
                "  --pie-client                render the PIE replicated client world\n"
                "  --connect <host:port>       PIE host address, acknowledgements go here\n"
                "  --listen <port>             snapshot receive port (default: host port + 1)\n"
                "  --headless                  no window; run the client until the host stops\n"
                "  --help                      this text\n");
}

bool ParseSandboxOptions(int InCount, char** InArgs, SandboxOptions* OutOptions, std::string* OutError)
{
    if (OutOptions == nullptr)
    {
        return false;
    }
    const auto Fail = [OutError](const char* InMessage)
    {
        if (OutError != nullptr)
        {
            *OutError = InMessage;
        }
        return false;
    };

    SandboxOptions ScopedOptions;
    bool ScopedHasConnect = false;
    bool ScopedHasListen = false;
    for (int ScopedIndex = 1; ScopedIndex < InCount; ++ScopedIndex)
    {
        const std::string_view ScopedArg(InArgs[ScopedIndex] != nullptr ? InArgs[ScopedIndex] : "");
        if (ScopedArg == "--help" || ScopedArg == "-h")
        {
            PrintSandboxUsage();
            return Fail("");
        }
        if (ScopedArg == "--pie-client")
        {
            ScopedOptions.PieClient = true;
            continue;
        }
        if (ScopedArg == "--headless")
        {
            ScopedOptions.Headless = true;
            continue;
        }
        if (ScopedArg == "--connect" || ScopedArg == "--listen")
        {
            if (ScopedIndex + 1 >= InCount)
            {
                return Fail("missing value after a network option");
            }
            const char* ScopedValue = InArgs[++ScopedIndex];
            if (ScopedArg == "--connect")
            {
                if (!Net::ParseAddress(ScopedValue, kDefaultHostPort, &ScopedOptions.Connect))
                {
                    return Fail("--connect expects host:port");
                }
                ScopedHasConnect = true;
            }
            else if (!ParsePort(ScopedValue, &ScopedOptions.ListenPort))
            {
                return Fail("--listen expects a port in 0..65535");
            }
            else
            {
                ScopedHasListen = true;
            }
            continue;
        }
        if (!ScopedArg.empty() && ScopedArg.front() != '-')
        {
            if (!ParseCount(InArgs[ScopedIndex], &ScopedOptions.SpriteCount))
            {
                return Fail("the sprite count must be a number");
            }
            continue;
        }
        return Fail("unknown option");
    }

    if (ScopedOptions.PieClient)
    {
        if (!ScopedHasConnect)
        {
            return Fail("--pie-client requires --connect host:port");
        }
        if (!ScopedHasListen)
        {
            if (ScopedOptions.Connect.Port == 65535)
            {
                return Fail("--connect port 65535 leaves no room for the default --listen port");
            }
            ScopedOptions.ListenPort = static_cast<std::uint16_t>(ScopedOptions.Connect.Port + 1);
        }
    }
    else if (ScopedOptions.Headless)
    {
        return Fail("--headless requires --pie-client");
    }
    *OutOptions = ScopedOptions;
    return true;
}
}
