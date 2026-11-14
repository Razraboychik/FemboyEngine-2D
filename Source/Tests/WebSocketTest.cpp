// FemboyEngine2D - WebSocket handshake key test.
#include "Engine/Network/WebSocket.hpp"

#include <cstdio>
#include <string>

int main()
{
    std::string ScopedAccept;
    if (!Fby::Net::MakeWebSocketAccept("dGhlIHNhbXBsZSBub25jZQ==", &ScopedAccept) ||
        ScopedAccept != "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=")
    {
        std::fprintf(stderr, "websocket accept mismatch: %s\n", ScopedAccept.c_str());
        return 1;
    }
    Fby::Net::WebSocketClient ScopedClient;
    std::string ScopedError;
    if (ScopedClient.Connect("wss://example.com/socket", &ScopedError))
    {
        std::fprintf(stderr, "wss must be rejected until TLS exists\n");
        return 1;
    }
    if (ScopedError.find("TLS") == std::string::npos)
    {
        std::fprintf(stderr, "missing TLS error: %s\n", ScopedError.c_str());
        return 1;
    }
    std::printf("websocket: accept=%s\n", ScopedAccept.c_str());
    return 0;
}
