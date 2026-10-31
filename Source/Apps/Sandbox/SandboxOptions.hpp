// FemboyEngine2D - sandbox command-line options.
#pragma once

#include "Engine/Network/Transport.hpp"

#include <cstdint>
#include <string>

namespace Fby::Sandbox
{

struct SandboxOptions
{
    // Demo entity count when the sandbox renders its own generated world.
    std::uint32_t SpriteCount = 100'000;
    // PIE client mode receives the editor's replicated world instead of
    // spawning one, then renders it in the sandbox window.
    bool PieClient = false;
    // Headless client mode is for CI and for a second copy without a window.
    bool Headless = false;
    // PIE host address; the client sends acknowledgements here.
    Net::NetAddress Connect{};
    // Local UDP port that receives snapshots. The parser fills in
    // `Connect.Port + 1` when `--listen` is absent.
    std::uint16_t ListenPort = 0;
};

void PrintSandboxUsage();

// Parses the sandbox command line. Returns false with `OutError` set on a
// malformed argument; `--help` prints the usage and also returns false.
[[nodiscard]] bool ParseSandboxOptions(int InCount, char** InArgs, SandboxOptions* OutOptions, std::string* OutError);
}
