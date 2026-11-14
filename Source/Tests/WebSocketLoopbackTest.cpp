// FemboyEngine2D - loopback WebSocket handshake/frame test.
#include "Engine/Network/Transport.hpp"
#include "Engine/Network/WebSocket.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace
{
#if defined(_WIN32)
using Socket = SOCKET;
constexpr Socket kInvalid = INVALID_SOCKET;
void CloseSocket(Socket InSocket)
{
    ::closesocket(InSocket);
}
int ReceiveBytes(Socket InSocket, char* OutData, int InSize)
{
    return ::recv(InSocket, OutData, InSize, 0);
}
int SendBytes(Socket InSocket, const char* InData, int InSize)
{
    return ::send(InSocket, InData, InSize, 0);
}
#else
using Socket = int;
constexpr Socket kInvalid = -1;
void CloseSocket(Socket InSocket)
{
    ::close(InSocket);
}
int ReceiveBytes(Socket InSocket, char* OutData, int InSize)
{
    return static_cast<int>(::recv(InSocket, OutData, static_cast<std::size_t>(InSize), 0));
}
int SendBytes(Socket InSocket, const char* InData, int InSize)
{
    return static_cast<int>(::send(InSocket, InData, static_cast<std::size_t>(InSize), 0));
}
#endif

void TestServer(Socket InListener, std::atomic<bool>* OutOk)
{
    Socket ScopedClient = ::accept(InListener, nullptr, nullptr);
    if (ScopedClient == kInvalid)
    {
        return;
    }
    std::string ScopedRequest;
    std::array<char, 1024> ScopedChunk{};
    while (ScopedRequest.find("\r\n\r\n") == std::string::npos)
    {
        const int ScopedReceived = ReceiveBytes(ScopedClient, ScopedChunk.data(), static_cast<int>(ScopedChunk.size()));
        if (ScopedReceived <= 0)
        {
            CloseSocket(ScopedClient);
            return;
        }
        ScopedRequest.append(ScopedChunk.data(), static_cast<std::size_t>(ScopedReceived));
    }
    const std::size_t ScopedKeyStart = ScopedRequest.find("Sec-WebSocket-Key:");
    if (ScopedKeyStart == std::string::npos)
    {
        CloseSocket(ScopedClient);
        return;
    }
    const std::size_t ScopedValueStart = ScopedRequest.find(' ', ScopedKeyStart);
    const std::size_t ScopedValueEnd = ScopedRequest.find("\r\n", ScopedValueStart);
    const std::string ScopedKey = ScopedRequest.substr(ScopedValueStart + 1, ScopedValueEnd - ScopedValueStart - 1);
    std::string ScopedAccept;
    if (!Fby::Net::MakeWebSocketAccept(ScopedKey, &ScopedAccept))
    {
        CloseSocket(ScopedClient);
        return;
    }
    const std::string ScopedResponse = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
                                       "Connection: Upgrade\r\nSec-WebSocket-Accept: " +
                                       ScopedAccept + "\r\n\r\n";
    if (SendBytes(ScopedClient, ScopedResponse.data(), static_cast<int>(ScopedResponse.size())) <= 0)
    {
        CloseSocket(ScopedClient);
        return;
    }
    std::array<char, 128> ScopedFrame{};
    const int ScopedFrameBytes = ReceiveBytes(ScopedClient, ScopedFrame.data(), static_cast<int>(ScopedFrame.size()));
    if (ScopedFrameBytes < 6 || (static_cast<unsigned char>(ScopedFrame[0]) & 0x0Fu) != 1)
    {
        CloseSocket(ScopedClient);
        return;
    }
    const std::size_t ScopedPayloadLength = static_cast<unsigned char>(ScopedFrame[1]) & 0x7Fu;
    if (ScopedPayloadLength >= 126 || ScopedFrameBytes < static_cast<int>(6 + ScopedPayloadLength))
    {
        CloseSocket(ScopedClient);
        return;
    }
    constexpr std::string_view kReply = "hello";
    std::string ScopedReply;
    ScopedReply.push_back(static_cast<char>(0x81));
    ScopedReply.push_back(static_cast<char>(kReply.size()));
    ScopedReply += kReply;
    const bool ScopedSent = SendBytes(ScopedClient, ScopedReply.data(), static_cast<int>(ScopedReply.size())) > 0;
    ScopedClient = ::accept(InListener, nullptr, nullptr);
    if (ScopedClient != kInvalid)
    {
        CloseSocket(ScopedClient);
    }
    *OutOk = ScopedSent;
    CloseSocket(ScopedClient);
}
}

int main()
{
    using namespace Fby::Net;
    if (!DatagramSocket::EnsureSocketsInitialized())
    {
        return 1;
    }
    const Socket ScopedListener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ScopedListener == kInvalid)
    {
        return 1;
    }
    sockaddr_in ScopedAddress{};
    ScopedAddress.sin_family = AF_INET;
    ScopedAddress.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ScopedAddress.sin_port = 0;
    if (::bind(ScopedListener, reinterpret_cast<const sockaddr*>(&ScopedAddress), sizeof(ScopedAddress)) != 0 ||
        ::listen(ScopedListener, 1) != 0)
    {
        CloseSocket(ScopedListener);
        return 1;
    }
    socklen_t ScopedLength = sizeof(ScopedAddress);
#if defined(_WIN32)
    int ScopedLengthInt = static_cast<int>(sizeof(ScopedAddress));
    if (::getsockname(ScopedListener, reinterpret_cast<sockaddr*>(&ScopedAddress), &ScopedLengthInt) != 0)
    {
        CloseSocket(ScopedListener);
        return 1;
    }
#else
    if (::getsockname(ScopedListener, reinterpret_cast<sockaddr*>(&ScopedAddress), &ScopedLength) != 0)
    {
        CloseSocket(ScopedListener);
        return 1;
    }
#endif
    std::atomic<bool> ScopedServerOk{false};
    std::thread ScopedServer(TestServer, ScopedListener, &ScopedServerOk);
    WebSocketClient ScopedClient;
    std::string ScopedError;
    const std::string ScopedUrl = "ws://127.0.0.1:" + std::to_string(ntohs(ScopedAddress.sin_port)) + "/test";
    if (!ScopedClient.Connect(ScopedUrl, &ScopedError))
    {
        std::fprintf(stderr, "connect failed: %s\n", ScopedError.c_str());
        ScopedServer.join();
        CloseSocket(ScopedListener);
        return 1;
    }
    if (!ScopedClient.SendText("ping", &ScopedError))
    {
        std::fprintf(stderr, "send failed: %s\n", ScopedError.c_str());
        ScopedServer.join();
        CloseSocket(ScopedListener);
        return 1;
    }
    WebSocketMessage ScopedMessage;
    for (int ScopedAttempt = 0; ScopedAttempt < 200 && ScopedMessage.Data.empty(); ++ScopedAttempt)
    {
        (void)ScopedClient.Receive(&ScopedMessage, &ScopedError);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CloseSocket(ScopedListener);
    ScopedServer.join();
    if (!ScopedServerOk.load() || ScopedMessage.Data != "hello")
    {
        std::fprintf(stderr, "websocket loopback failed: '%s'\n", ScopedMessage.Data.c_str());
        return 1;
    }
    std::printf("websocket: loopback ok (%s)\n", ScopedMessage.Data.c_str());
    return 0;
}
