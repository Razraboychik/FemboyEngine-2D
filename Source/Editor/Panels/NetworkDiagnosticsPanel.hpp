// FemboyEngine2D - live network session diagnostics panel.
#pragma once

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

    void SetSession(const Fby::Net::NetSession* InSession) noexcept
    {
        MSession = InSession;
    }
    void Draw(bool* InOpen);

protected:
    const Fby::Net::NetSession* MSession = nullptr;
};
}
