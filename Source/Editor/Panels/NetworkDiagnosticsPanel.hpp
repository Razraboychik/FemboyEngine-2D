// FemboyEngine2D - live network session diagnostics panel.
#pragma once

#include "Engine/Network/Lobby.hpp"
#include "Engine/Network/Transport.hpp"
#include "Engine/Network/WebSocket.hpp"

#include <array>
#include <string>

namespace Fby::Net
{
class NetSession;
}

namespace Fby::Editor
{

// Read-only view of one NetSession. The host binds a session; with no session
// the panel stays in an explicit inactive state so the editor can ship without
// networking.
class NetworkDiagnosticsPanel
{
public:
    NetworkDiagnosticsPanel() = default;
    ~NetworkDiagnosticsPanel();

    void SetSession(const Fby::Net::NetSession* InSession) noexcept
    {
        MSession = InSession;
    }
    void Draw(bool* InOpen);

protected:
    const Fby::Net::NetSession* MSession = nullptr;
    Fby::Net::NetTransport MLobbyTransport;
    Fby::Net::NetLobbyClient MLobby;
    bool MLobbyStarted = false;
    std::array<char, 64> MLobbyHost{};
    std::array<char, 16> MLobbyPort{};
    std::array<char, 80> MLobbyKey{};
    std::array<char, 32> MLobbyName{};
    std::array<char, 32> MLobbyRoomName{};
    int MLobbyRoomId = 0;
    std::string MLobbyStatus;
    Fby::Net::WebSocketClient MWebSocket;
    std::array<char, 512> MWebSocketUrl{};
    std::array<char, 256> MWebSocketMessage{};
    std::string MWebSocketLog;
};
}
