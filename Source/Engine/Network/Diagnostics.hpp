// FemboyEngine2D - network state snapshot for debug panels.
#pragma once

#include "Engine/Network/Session.hpp"

#include <array>
#include <cstdint>

namespace Fby::Net
{

struct NetDiagnosticsSnapshot
{
    NetRole Role = NetRole::Offline;
    NetSessionStatus Status = NetSessionStatus::NotStarted;
    std::uint8_t LocalPlayer = kSessionNoPlayer;
    std::uint32_t PlayerCount = 0;
    std::uint64_t LocalTick = 0;
    std::uint64_t ConfirmedTick = 0;
    std::uint32_t RttMs = 0;
    std::int64_t ClockOffsetMs = 0;
    NetAddress Remote{};
    std::array<NetPlayerSlot, kSessionMaxPlayers> Slots{};
};

[[nodiscard]] inline NetDiagnosticsSnapshot CaptureNetDiagnostics(const NetSession& InSession,
                                                                  const NetAddress& InRemote = {}) noexcept
{
    NetDiagnosticsSnapshot ScopedSnapshot;
    ScopedSnapshot.Role = InSession.Role();
    ScopedSnapshot.Status = InSession.Status();
    ScopedSnapshot.LocalPlayer = InSession.LocalPlayer();
    ScopedSnapshot.PlayerCount = InSession.PlayerCount();
    ScopedSnapshot.LocalTick = InSession.LocalTick();
    ScopedSnapshot.ConfirmedTick = InSession.ConfirmedTick();
    ScopedSnapshot.RttMs = InSession.RttMs();
    ScopedSnapshot.ClockOffsetMs = InSession.ClockOffsetMs();
    ScopedSnapshot.Remote = InRemote;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < kSessionMaxPlayers; ++ScopedIndex)
    {
        ScopedSnapshot.Slots[ScopedIndex] = InSession.Slot(ScopedIndex);
    }
    return ScopedSnapshot;
}
}
