// FemboyEngine2D - minimal RFC 6455 WebSocket client.
#include "Engine/Network/WebSocket.hpp"

#include "Engine/Network/Transport.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <random>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace Fby::Net
{
namespace
{
constexpr std::string_view kWebSocketGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
constexpr std::uint32_t kSha1RotateLeft[4] = {5, 30, 30, 30};

[[nodiscard]] std::uint32_t RotateLeft(std::uint32_t InValue, std::uint32_t InBits) noexcept
{
    return (InValue << InBits) | (InValue >> (32u - InBits));
}

void Sha1Transform(std::array<std::uint32_t, 5>* InState, const std::uint8_t* InBlock) noexcept
{
    std::array<std::uint32_t, 80> ScopedWords{};
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < 16; ++ScopedIndex)
    {
        ScopedWords[ScopedIndex] = (static_cast<std::uint32_t>(InBlock[ScopedIndex * 4]) << 24) |
                                   (static_cast<std::uint32_t>(InBlock[ScopedIndex * 4 + 1]) << 16) |
                                   (static_cast<std::uint32_t>(InBlock[ScopedIndex * 4 + 2]) << 8) |
                                   static_cast<std::uint32_t>(InBlock[ScopedIndex * 4 + 3]);
    }
    for (std::uint32_t ScopedIndex = 16; ScopedIndex < 80; ++ScopedIndex)
    {
        ScopedWords[ScopedIndex] = RotateLeft(ScopedWords[ScopedIndex - 3] ^ ScopedWords[ScopedIndex - 8] ^
                                                  ScopedWords[ScopedIndex - 14] ^ ScopedWords[ScopedIndex - 16],
                                              1);
    }
    std::uint32_t ScopedA = (*InState)[0];
    std::uint32_t ScopedB = (*InState)[1];
    std::uint32_t ScopedC = (*InState)[2];
    std::uint32_t ScopedD = (*InState)[3];
    std::uint32_t ScopedE = (*InState)[4];
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < 80; ++ScopedIndex)
    {
        std::uint32_t ScopedF = 0;
        std::uint32_t ScopedK = 0;
        if (ScopedIndex < 20)
        {
            ScopedF = (ScopedB & ScopedC) | ((~ScopedB) & ScopedD);
            ScopedK = 0x5A827999u;
        }
        else if (ScopedIndex < 40)
        {
            ScopedF = ScopedB ^ ScopedC ^ ScopedD;
            ScopedK = 0x6ED9EBA1u;
        }
        else if (ScopedIndex < 60)
        {
            ScopedF = (ScopedB & ScopedC) | (ScopedB & ScopedD) | (ScopedC & ScopedD);
            ScopedK = 0x8F1BBCDCu;
        }
        else
        {
            ScopedF = ScopedB ^ ScopedC ^ ScopedD;
            ScopedK = 0xCA62C1D6u;
        }
        const std::uint32_t ScopedTemp =
            RotateLeft(ScopedA, kSha1RotateLeft[0]) + ScopedF + ScopedE + ScopedK + ScopedWords[ScopedIndex];
        ScopedE = ScopedD;
        ScopedD = ScopedC;
        ScopedC = RotateLeft(ScopedB, 30);
        ScopedB = ScopedA;
        ScopedA = ScopedTemp;
    }
    (*InState)[0] += ScopedA;
    (*InState)[1] += ScopedB;
    (*InState)[2] += ScopedC;
    (*InState)[3] += ScopedD;
    (*InState)[4] += ScopedE;
}

[[nodiscard]] std::array<std::uint8_t, 20> Sha1(std::string_view InText) noexcept
{
    std::array<std::uint32_t, 5> ScopedState = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u,
                                                0xC3D2E1F0u};
    std::vector<std::uint8_t> ScopedMessage(InText.begin(), InText.end());
    const std::uint64_t ScopedBitLength = static_cast<std::uint64_t>(ScopedMessage.size()) * 8u;
    ScopedMessage.push_back(0x80u);
    while ((ScopedMessage.size() % 64u) != 56u)
    {
        ScopedMessage.push_back(0u);
    }
    for (std::uint32_t ScopedShift = 56; ScopedShift <= 56; ScopedShift -= 8)
    {
        ScopedMessage.push_back(static_cast<std::uint8_t>((ScopedBitLength >> ScopedShift) & 0xFFu));
        if (ScopedShift == 0)
        {
            break;
        }
    }
    for (std::size_t ScopedOffset = 0; ScopedOffset < ScopedMessage.size(); ScopedOffset += 64)
    {
        Sha1Transform(&ScopedState, ScopedMessage.data() + ScopedOffset);
    }
    std::array<std::uint8_t, 20> ScopedDigest{};
    for (std::size_t ScopedIndex = 0; ScopedIndex < 5; ++ScopedIndex)
    {
        ScopedDigest[ScopedIndex * 4] = static_cast<std::uint8_t>(ScopedState[ScopedIndex] >> 24);
        ScopedDigest[ScopedIndex * 4 + 1] = static_cast<std::uint8_t>(ScopedState[ScopedIndex] >> 16);
        ScopedDigest[ScopedIndex * 4 + 2] = static_cast<std::uint8_t>(ScopedState[ScopedIndex] >> 8);
        ScopedDigest[ScopedIndex * 4 + 3] = static_cast<std::uint8_t>(ScopedState[ScopedIndex]);
    }
    return ScopedDigest;
}

[[nodiscard]] std::string Base64Encode(std::span<const std::uint8_t> InData)
{
    constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string ScopedResult;
    ScopedResult.reserve(((InData.size() + 2) / 3) * 4);
    for (std::size_t ScopedIndex = 0; ScopedIndex < InData.size(); ScopedIndex += 3)
    {
        const std::uint32_t ScopedA = InData[ScopedIndex];
        const std::uint32_t ScopedB = ScopedIndex + 1 < InData.size() ? InData[ScopedIndex + 1] : 0;
        const std::uint32_t ScopedC = ScopedIndex + 2 < InData.size() ? InData[ScopedIndex + 2] : 0;
        const std::uint32_t ScopedTriple = (ScopedA << 16) | (ScopedB << 8) | ScopedC;
        ScopedResult.push_back(kAlphabet[(ScopedTriple >> 18) & 0x3Fu]);
        ScopedResult.push_back(kAlphabet[(ScopedTriple >> 12) & 0x3Fu]);
        ScopedResult.push_back(ScopedIndex + 1 < InData.size() ? kAlphabet[(ScopedTriple >> 6) & 0x3Fu] : '=');
        ScopedResult.push_back(ScopedIndex + 2 < InData.size() ? kAlphabet[ScopedTriple & 0x3Fu] : '=');
    }
    return ScopedResult;
}

[[nodiscard]] std::string ToLowerAscii(std::string_view InText)
{
    std::string ScopedResult(InText);
    for (char& ScopedCharacter : ScopedResult)
    {
        if (ScopedCharacter >= 'A' && ScopedCharacter <= 'Z')
        {
            ScopedCharacter = static_cast<char>(ScopedCharacter - 'A' + 'a');
        }
    }
    return ScopedResult;
}

[[nodiscard]] std::string_view TrimAscii(std::string_view InText) noexcept
{
    while (!InText.empty() && (InText.front() == ' ' || InText.front() == '\t'))
    {
        InText.remove_prefix(1);
    }
    while (!InText.empty() &&
           (InText.back() == ' ' || InText.back() == '\t' || InText.back() == '\r' || InText.back() == '\n'))
    {
        InText.remove_suffix(1);
    }
    return InText;
}

struct ParsedUrl
{
    std::string Host;
    std::uint16_t Port = 80;
    std::string Path = "/";
};

[[nodiscard]] bool ParseUrl(std::string_view InUrl, ParsedUrl* OutUrl, std::string* InError)
{
    if (OutUrl == nullptr)
    {
        return false;
    }
    if (InUrl.starts_with("wss://"))
    {
        if (InError != nullptr)
        {
            *InError = "wss:// requires a TLS transport, which is not wired yet";
        }
        return false;
    }
    if (!InUrl.starts_with("ws://"))
    {
        if (InError != nullptr)
        {
            *InError = "WebSocket URL must start with ws://";
        }
        return false;
    }
    std::string_view ScopedRest = InUrl.substr(5);
    const std::size_t ScopedSlash = ScopedRest.find('/');
    std::string_view ScopedAuthority = ScopedRest.substr(0, ScopedSlash);
    if (ScopedSlash != std::string_view::npos)
    {
        OutUrl->Path = std::string(ScopedRest.substr(ScopedSlash));
    }
    const std::size_t ScopedColon = ScopedAuthority.rfind(':');
    std::string_view ScopedHost = ScopedAuthority;
    if (ScopedColon != std::string_view::npos)
    {
        ScopedHost = ScopedAuthority.substr(0, ScopedColon);
        const std::string_view ScopedPort = ScopedAuthority.substr(ScopedColon + 1);
        if (ScopedPort.empty() || ScopedPort.size() > 5)
        {
            if (InError != nullptr)
            {
                *InError = "invalid WebSocket port";
            }
            return false;
        }
        std::uint32_t ScopedValue = 0;
        for (const char ScopedCharacter : ScopedPort)
        {
            if (ScopedCharacter < '0' || ScopedCharacter > '9')
            {
                if (InError != nullptr)
                {
                    *InError = "invalid WebSocket port";
                }
                return false;
            }
            ScopedValue = ScopedValue * 10 + static_cast<std::uint32_t>(ScopedCharacter - '0');
        }
        if (ScopedValue == 0 || ScopedValue > 65535)
        {
            if (InError != nullptr)
            {
                *InError = "invalid WebSocket port";
            }
            return false;
        }
        OutUrl->Port = static_cast<std::uint16_t>(ScopedValue);
    }
    if (ScopedHost.empty())
    {
        if (InError != nullptr)
        {
            *InError = "WebSocket host is empty";
        }
        return false;
    }
    OutUrl->Host = std::string(ScopedHost);
    return true;
}

#if defined(_WIN32)
[[nodiscard]] bool WouldBlock() noexcept
{
    return WSAGetLastError() == WSAEWOULDBLOCK;
}
void CloseHandle(std::uintptr_t InHandle) noexcept
{
    ::closesocket(static_cast<SOCKET>(InHandle));
}
#else
[[nodiscard]] bool WouldBlock() noexcept
{
    return errno == EAGAIN || errno == EWOULDBLOCK;
}
void CloseHandle(std::uintptr_t InHandle) noexcept
{
    ::close(static_cast<int>(InHandle));
}
#endif

[[nodiscard]] std::uintptr_t InvalidHandle() noexcept
{
    return static_cast<std::uintptr_t>(-1);
}

void SetNonBlocking(std::uintptr_t InHandle, bool InNonBlocking) noexcept
{
#if defined(_WIN32)
    u_long ScopedMode = InNonBlocking ? 1 : 0;
    (void)::ioctlsocket(static_cast<SOCKET>(InHandle), FIONBIO, &ScopedMode);
#else
    const int ScopedFlags = ::fcntl(static_cast<int>(InHandle), F_GETFL, 0);
    if (ScopedFlags >= 0)
    {
        (void)::fcntl(static_cast<int>(InHandle), F_SETFL,
                      InNonBlocking ? (ScopedFlags | O_NONBLOCK) : (ScopedFlags & ~O_NONBLOCK));
    }
#endif
}

[[nodiscard]] int WaitSocket(std::uintptr_t InHandle, bool InWrite, std::uint32_t InTimeoutMs) noexcept
{
    fd_set ScopedSet;
    FD_ZERO(&ScopedSet);
#if defined(_WIN32)
    FD_SET(static_cast<SOCKET>(InHandle), &ScopedSet);
#else
    FD_SET(static_cast<int>(InHandle), &ScopedSet);
#endif
    timeval ScopedTimeout{static_cast<long>(InTimeoutMs / 1000), static_cast<long>((InTimeoutMs % 1000) * 1000)};
    const int ScopedResult =
        ::select(static_cast<int>(InHandle) + 1, InWrite ? nullptr : &ScopedSet, InWrite ? &ScopedSet : nullptr,
                 nullptr, &ScopedTimeout);
    return ScopedResult;
}

[[nodiscard]] bool SendAllRaw(std::uintptr_t InHandle, std::span<const std::uint8_t> InData,
                           std::string* InError) noexcept
{
    std::size_t ScopedSent = 0;
    while (ScopedSent < InData.size())
    {
        int ScopedResult = 0;
#if defined(_WIN32)
        ScopedResult = ::send(static_cast<SOCKET>(InHandle), reinterpret_cast<const char*>(InData.data() + ScopedSent),
                              static_cast<int>(InData.size() - ScopedSent), 0);
        if (ScopedResult == SOCKET_ERROR)
        {
            if (!WouldBlock())
            {
                break;
            }
            ScopedResult = 0;
        }
#else
        ScopedResult = static_cast<int>(
            ::send(static_cast<int>(InHandle), InData.data() + ScopedSent, InData.size() - ScopedSent, 0));
        if (ScopedResult < 0)
        {
            if (!WouldBlock())
            {
                break;
            }
            ScopedResult = 0;
        }
#endif
        ScopedSent += static_cast<std::size_t>(ScopedResult);
        if (ScopedResult == 0 && ScopedSent < InData.size())
        {
            if (WaitSocket(InHandle, true, 5000) <= 0)
            {
                break;
            }
        }
    }
    if (ScopedSent != InData.size())
    {
        if (InError != nullptr)
        {
            *InError = "WebSocket send failed";
        }
        return false;
    }
    return true;
}
}

bool MakeWebSocketAccept(std::string_view InKey, std::string* OutAccept)
{
    if (OutAccept == nullptr)
    {
        return false;
    }
    std::string ScopedSource(InKey);
    ScopedSource += kWebSocketGuid;
    const std::array<std::uint8_t, 20> ScopedDigest = Sha1(ScopedSource);
    *OutAccept = Base64Encode(ScopedDigest);
    return true;
}

WebSocketClient::~WebSocketClient()
{
    Close();
}

WebSocketClient::WebSocketClient(WebSocketClient&& InOther) noexcept
{
    MHandle = InOther.MHandle;
    MState = InOther.MState;
    MLastError = std::move(InOther.MLastError);
    MBuffer = std::move(InOther.MBuffer);
    MQueue = std::move(InOther.MQueue);
    MFragmentOpcode = InOther.MFragmentOpcode;
    MFragmentData = std::move(InOther.MFragmentData);
    MFragmentActive = InOther.MFragmentActive;
    MStats = InOther.MStats;
    InOther.Reset();
}

WebSocketClient& WebSocketClient::operator=(WebSocketClient&& InOther) noexcept
{
    if (this != &InOther)
    {
        Close();
        MHandle = InOther.MHandle;
        MState = InOther.MState;
        MLastError = std::move(InOther.MLastError);
        MBuffer = std::move(InOther.MBuffer);
        MQueue = std::move(InOther.MQueue);
        MFragmentOpcode = InOther.MFragmentOpcode;
        MFragmentData = std::move(InOther.MFragmentData);
        MFragmentActive = InOther.MFragmentActive;
        MStats = InOther.MStats;
        InOther.Reset();
    }
    return *this;
}

bool WebSocketClient::Fail(std::string InMessage, std::string* InError) noexcept
{
    MLastError = std::move(InMessage);
    if (InError != nullptr)
    {
        *InError = MLastError;
    }
    MState = WebSocketState::Error;
    if (MHandle != kInvalidHandle)
    {
        CloseHandle(MHandle);
        MHandle = kInvalidHandle;
    }
    return false;
}

void WebSocketClient::Reset() noexcept
{
    MHandle = kInvalidHandle;
    MState = WebSocketState::Closed;
    MLastError.clear();
    MBuffer.clear();
    MQueue.clear();
    MFragmentData.clear();
    MFragmentActive = false;
    MStats = {};
}

bool WebSocketClient::Connect(std::string_view InUrl, std::string* InError, std::uint32_t InTimeoutMs) noexcept
{
    Close();
    std::string ScopedError;
    if (!DatagramSocket::EnsureSocketsInitialized(&ScopedError))
    {
        return Fail(std::move(ScopedError), InError);
    }
    ParsedUrl ScopedUrl;
    if (!ParseUrl(InUrl, &ScopedUrl, &ScopedError))
    {
        return Fail(std::move(ScopedError), InError);
    }
    addrinfo ScopedHints{};
    ScopedHints.ai_family = AF_UNSPEC;
    ScopedHints.ai_socktype = SOCK_STREAM;
    ScopedHints.ai_protocol = IPPROTO_TCP;
    addrinfo* ScopedAddresses = nullptr;
    const std::string ScopedPort = std::to_string(ScopedUrl.Port);
    if (::getaddrinfo(ScopedUrl.Host.c_str(), ScopedPort.c_str(), &ScopedHints, &ScopedAddresses) != 0 ||
        ScopedAddresses == nullptr)
    {
        return Fail("WebSocket DNS lookup failed", InError);
    }
    std::uintptr_t ScopedHandle = InvalidHandle();
    for (addrinfo* ScopedAddress = ScopedAddresses; ScopedAddress != nullptr;
         ScopedAddress = ScopedAddress->ai_next)
    {
#if defined(_WIN32)
        const SOCKET ScopedSocket = ::socket(ScopedAddress->ai_family, ScopedAddress->ai_socktype,
                                             ScopedAddress->ai_protocol);
        if (ScopedSocket == INVALID_SOCKET)
        {
            continue;
        }
        SetNonBlocking(static_cast<std::uintptr_t>(ScopedSocket), true);
        const int ScopedResult = ::connect(ScopedSocket, ScopedAddress->ai_addr,
                                           static_cast<int>(ScopedAddress->ai_addrlen));
        if (ScopedResult == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK)
        {
            ::closesocket(ScopedSocket);
            continue;
        }
        if (ScopedResult == SOCKET_ERROR && WaitSocket(static_cast<std::uintptr_t>(ScopedSocket), true,
                                                       InTimeoutMs) <= 0)
        {
            ::closesocket(ScopedSocket);
            continue;
        }
        ScopedHandle = static_cast<std::uintptr_t>(ScopedSocket);
#else
        const int ScopedSocket = ::socket(ScopedAddress->ai_family, ScopedAddress->ai_socktype,
                                          ScopedAddress->ai_protocol);
        if (ScopedSocket < 0)
        {
            continue;
        }
        SetNonBlocking(static_cast<std::uintptr_t>(ScopedSocket), true);
        const int ScopedResult = ::connect(ScopedSocket, ScopedAddress->ai_addr, ScopedAddress->ai_addrlen);
        if (ScopedResult < 0 && errno != EINPROGRESS)
        {
            ::close(ScopedSocket);
            continue;
        }
        if (ScopedResult < 0 && WaitSocket(static_cast<std::uintptr_t>(ScopedSocket), true, InTimeoutMs) <= 0)
        {
            ::close(ScopedSocket);
            continue;
        }
        ScopedHandle = static_cast<std::uintptr_t>(ScopedSocket);
#endif
        break;
    }
    ::freeaddrinfo(ScopedAddresses);
    if (ScopedHandle == InvalidHandle())
    {
        return Fail("WebSocket TCP connect failed", InError);
    }
    MHandle = ScopedHandle;
    SetNonBlocking(MHandle, false);

    std::array<std::uint8_t, 16> ScopedKeyBytes{};
    std::random_device ScopedRandom;
    for (std::uint8_t& ScopedByte : ScopedKeyBytes)
    {
        ScopedByte = static_cast<std::uint8_t>(ScopedRandom() & 0xFFu);
    }
    const std::string ScopedKey = Base64Encode(ScopedKeyBytes);
    std::string ScopedRequest = "GET " + ScopedUrl.Path + " HTTP/1.1\r\nHost: " + ScopedUrl.Host + ":" +
                                ScopedPort + "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                                "Sec-WebSocket-Key: " +
                                ScopedKey + "\r\nSec-WebSocket-Version: 13\r\n\r\n";
    if (!SendAllRaw(MHandle, std::span<const std::uint8_t>(
                              reinterpret_cast<const std::uint8_t*>(ScopedRequest.data()), ScopedRequest.size()),
                 &ScopedError))
    {
        return Fail(std::move(ScopedError), InError);
    }
    std::string ScopedResponse;
    std::array<char, 2048> ScopedChunk{};
    while (ScopedResponse.find("\r\n\r\n") == std::string::npos && ScopedResponse.size() < 16384)
    {
        if (WaitSocket(MHandle, false, InTimeoutMs) <= 0)
        {
            return Fail("WebSocket handshake timed out", InError);
        }
        const int ScopedReceived = ::recv(
#if defined(_WIN32)
            static_cast<SOCKET>(MHandle),
#else
            static_cast<int>(MHandle),
#endif
            ScopedChunk.data(), static_cast<int>(ScopedChunk.size()), 0);
        if (ScopedReceived <= 0)
        {
            return Fail("WebSocket handshake read failed", InError);
        }
        ScopedResponse.append(ScopedChunk.data(), static_cast<std::size_t>(ScopedReceived));
    }
    const std::size_t ScopedHeaderEnd = ScopedResponse.find("\r\n\r\n");
    if (ScopedHeaderEnd == std::string::npos || ScopedResponse.find(" 101 ") == std::string::npos)
    {
        return Fail("WebSocket server rejected the upgrade", InError);
    }
    std::string ScopedExpectedAccept;
    MakeWebSocketAccept(ScopedKey, &ScopedExpectedAccept);
    const std::string ScopedLowerResponse = ToLowerAscii(ScopedResponse.substr(0, ScopedHeaderEnd));
    const std::size_t ScopedAccept = ScopedLowerResponse.find("sec-websocket-accept:");
    if (ScopedAccept == std::string::npos)
    {
        return Fail("WebSocket handshake has no Sec-WebSocket-Accept", InError);
    }
    const std::size_t ScopedLineEnd = ScopedLowerResponse.find("\r\n", ScopedAccept);
    const std::string_view ScopedAcceptValue = TrimAscii(
        std::string_view(ScopedResponse).substr(ScopedAccept + 21, ScopedLineEnd - ScopedAccept - 21));
    if (ScopedAcceptValue != ScopedExpectedAccept)
    {
        return Fail("WebSocket handshake accept mismatch", InError);
    }
    SetNonBlocking(MHandle, true);
    MState = WebSocketState::Open;
    MBuffer = ScopedResponse.substr(ScopedHeaderEnd + 4);
    return Poll(InError);
}

void WebSocketClient::Close() noexcept
{
    if (MHandle == kInvalidHandle)
    {
        MState = WebSocketState::Closed;
        return;
    }
    if (MState == WebSocketState::Open)
    {
        std::array<std::byte, 2> ScopedClosePayload = {static_cast<std::byte>(0x03), static_cast<std::byte>(0xE8)};
        (void)SendFrame(WebSocketOpcode::Close, ScopedClosePayload, nullptr);
    }
    CloseHandle(MHandle);
    MHandle = kInvalidHandle;
    MState = WebSocketState::Closed;
    MBuffer.clear();
    MQueue.clear();
    MFragmentData.clear();
    MFragmentActive = false;
}

bool WebSocketClient::SendAll(std::span<const std::byte> InData, std::string* InError) noexcept
{
    return SendAllRaw(MHandle, std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(InData.data()),
                                                          InData.size()),
                   InError);
}

bool WebSocketClient::SendFrame(WebSocketOpcode InOpcode, std::span<const std::byte> InPayload,
                                std::string* InError) noexcept
{
    if (MHandle == kInvalidHandle)
    {
        if (InError != nullptr)
        {
            *InError = "WebSocket is closed";
        }
        return false;
    }
    std::vector<std::uint8_t> ScopedFrame;
    ScopedFrame.push_back(static_cast<std::uint8_t>(0x80u | static_cast<std::uint8_t>(InOpcode)));
    const std::size_t ScopedSize = InPayload.size();
    const bool ScopedMasked = true;
    if (ScopedSize < 126)
    {
        ScopedFrame.push_back(static_cast<std::uint8_t>((ScopedMasked ? 0x80u : 0u) | ScopedSize));
    }
    else if (ScopedSize <= 0xFFFFu)
    {
        ScopedFrame.push_back(static_cast<std::uint8_t>((ScopedMasked ? 0x80u : 0u) | 126u));
        ScopedFrame.push_back(static_cast<std::uint8_t>(ScopedSize >> 8));
        ScopedFrame.push_back(static_cast<std::uint8_t>(ScopedSize));
    }
    else
    {
        ScopedFrame.push_back(static_cast<std::uint8_t>((ScopedMasked ? 0x80u : 0u) | 127u));
        for (int ScopedShift = 56; ScopedShift >= 0; ScopedShift -= 8)
        {
            ScopedFrame.push_back(static_cast<std::uint8_t>(
                (static_cast<std::uint64_t>(ScopedSize) >> ScopedShift) & 0xFFu));
        }
    }
    std::array<std::uint8_t, 4> ScopedMask{};
    std::random_device ScopedRandom;
    for (std::uint8_t& ScopedByte : ScopedMask)
    {
        ScopedByte = static_cast<std::uint8_t>(ScopedRandom() & 0xFFu);
    }
    ScopedFrame.insert(ScopedFrame.end(), ScopedMask.begin(), ScopedMask.end());
    const std::size_t ScopedMaskOffset = ScopedFrame.size() - 4;
    for (std::size_t ScopedIndex = 0; ScopedIndex < ScopedSize; ++ScopedIndex)
    {
        ScopedFrame.push_back(static_cast<std::uint8_t>(InPayload[ScopedIndex]) ^
                              ScopedMask[(ScopedMaskOffset + ScopedIndex) & 3u]);
    }
    if (!SendAllRaw(MHandle, ScopedFrame, InError))
    {
        return false;
    }
    MStats.bytesSent += ScopedFrame.size();
    ++MStats.messagesSent;
    return true;
}

bool WebSocketClient::SendText(std::string_view InText, std::string* InError) noexcept
{
    return SendFrame(WebSocketOpcode::Text,
                     std::span<const std::byte>(reinterpret_cast<const std::byte*>(InText.data()), InText.size()),
                     InError);
}

bool WebSocketClient::SendBinary(std::span<const std::byte> InData, std::string* InError) noexcept
{
    return SendFrame(WebSocketOpcode::Binary, InData, InError);
}

bool WebSocketClient::Poll(std::string* InError) noexcept
{
    if (MHandle == kInvalidHandle)
    {
        return false;
    }
    std::array<std::byte, 4096> ScopedBuffer{};
    for (;;)
    {
#if defined(_WIN32)
        const int ScopedReceived = ::recv(static_cast<SOCKET>(MHandle),
                                          reinterpret_cast<char*>(ScopedBuffer.data()),
                                          static_cast<int>(ScopedBuffer.size()), 0);
#else
        const int ScopedReceived = static_cast<int>(
            ::recv(static_cast<int>(MHandle), ScopedBuffer.data(), ScopedBuffer.size(), 0));
#endif
        if (ScopedReceived > 0)
        {
            MBuffer.append(reinterpret_cast<const char*>(ScopedBuffer.data()),
                           static_cast<std::size_t>(ScopedReceived));
            MStats.bytesReceived += static_cast<std::uint64_t>(ScopedReceived);
            continue;
        }
        if (ScopedReceived == 0)
        {
            MState = WebSocketState::Closed;
            break;
        }
        if (WouldBlock())
        {
            break;
        }
        return Fail("WebSocket receive failed", InError);
    }
    if (!ParseFrames(InError))
    {
        return false;
    }
    if (MState == WebSocketState::Closed)
    {
        CloseHandle(MHandle);
        MHandle = kInvalidHandle;
    }
    return true;
}

bool WebSocketClient::ParseFrames(std::string* InError) noexcept
{
    std::size_t ScopedOffset = 0;
    while (MBuffer.size() - ScopedOffset >= 2)
    {
        const std::uint8_t ScopedFirst = static_cast<std::uint8_t>(MBuffer[ScopedOffset]);
        const std::uint8_t ScopedSecond = static_cast<std::uint8_t>(MBuffer[ScopedOffset + 1]);
        const bool ScopedFin = (ScopedFirst & 0x80u) != 0;
        const WebSocketOpcode ScopedOpcode = static_cast<WebSocketOpcode>(ScopedFirst & 0x0Fu);
        const bool ScopedMasked = (ScopedSecond & 0x80u) != 0;
        std::uint64_t ScopedPayloadLength = ScopedSecond & 0x7Fu;
        std::size_t ScopedHeaderLength = 2;
        if (ScopedPayloadLength == 126)
        {
            if (MBuffer.size() - ScopedOffset < 4)
            {
                break;
            }
            ScopedPayloadLength = (static_cast<std::uint64_t>(static_cast<std::uint8_t>(MBuffer[ScopedOffset + 2])) << 8) |
                                  static_cast<std::uint8_t>(MBuffer[ScopedOffset + 3]);
            ScopedHeaderLength = 4;
        }
        else if (ScopedPayloadLength == 127)
        {
            if (MBuffer.size() - ScopedOffset < 10)
            {
                break;
            }
            ScopedPayloadLength = 0;
            for (std::size_t ScopedIndex = 0; ScopedIndex < 8; ++ScopedIndex)
            {
                ScopedPayloadLength = (ScopedPayloadLength << 8) |
                                      static_cast<std::uint8_t>(MBuffer[ScopedOffset + 2 + ScopedIndex]);
            }
            ScopedHeaderLength = 10;
        }
        if (ScopedPayloadLength > kMaxMessageBytes)
        {
            return Fail("WebSocket frame exceeds the message cap", InError);
        }
        if (ScopedMasked)
        {
            ScopedHeaderLength += 4;
        }
        if (MBuffer.size() - ScopedOffset < ScopedHeaderLength + ScopedPayloadLength)
        {
            break;
        }
        std::string ScopedPayload = MBuffer.substr(ScopedOffset + ScopedHeaderLength,
                                                  static_cast<std::size_t>(ScopedPayloadLength));
        if (ScopedMasked)
        {
            const std::size_t ScopedMaskOffset = ScopedOffset + ScopedHeaderLength - 4;
            for (std::size_t ScopedIndex = 0; ScopedIndex < ScopedPayload.size(); ++ScopedIndex)
            {
                ScopedPayload[ScopedIndex] = static_cast<char>(
                    static_cast<std::uint8_t>(ScopedPayload[ScopedIndex]) ^
                    static_cast<std::uint8_t>(MBuffer[ScopedMaskOffset + (ScopedIndex & 3u)]));
            }
        }
        ScopedOffset += ScopedHeaderLength + static_cast<std::size_t>(ScopedPayloadLength);
        if (ScopedOpcode == WebSocketOpcode::Ping)
        {
            ++MStats.pings;
            (void)SendFrame(WebSocketOpcode::Pong,
                            std::span<const std::byte>(reinterpret_cast<const std::byte*>(ScopedPayload.data()),
                                                       ScopedPayload.size()),
                            nullptr);
            continue;
        }
        if (ScopedOpcode == WebSocketOpcode::Pong)
        {
            continue;
        }
        if (ScopedOpcode == WebSocketOpcode::Close)
        {
            MState = WebSocketState::Closed;
            continue;
        }
        if (ScopedOpcode == WebSocketOpcode::Continuation)
        {
            if (!MFragmentActive)
            {
                return Fail("WebSocket continuation without a fragment", InError);
            }
            MFragmentData += ScopedPayload;
        }
        else
        {
            if (MFragmentActive)
            {
                return Fail("WebSocket new message while fragmented", InError);
            }
            MFragmentOpcode = ScopedOpcode;
            MFragmentData = std::move(ScopedPayload);
        }
        if (MFragmentData.size() > kMaxMessageBytes)
        {
            return Fail("WebSocket message exceeds the cap", InError);
        }
        MFragmentActive = !ScopedFin;
        if (ScopedFin)
        {
            MQueue.push_back(WebSocketMessage{MFragmentOpcode, std::move(MFragmentData)});
            MFragmentData.clear();
            ++MStats.messagesReceived;
        }
    }
    if (ScopedOffset > 0)
    {
        MBuffer.erase(0, ScopedOffset);
    }
    return true;
}

bool WebSocketClient::Receive(WebSocketMessage* OutMessage, std::string* InError) noexcept
{
    if (OutMessage == nullptr)
    {
        return false;
    }
    if (MQueue.empty() && MState != WebSocketState::Closed && MState != WebSocketState::Error)
    {
        (void)Poll(InError);
    }
    if (MQueue.empty())
    {
        return false;
    }
    *OutMessage = std::move(MQueue.front());
    MQueue.erase(MQueue.begin());
    return true;
}

}
