// FemboyEngine2D - UDP transport implementation.
#include "Engine/Network/Transport.hpp"

#include <algorithm>
#include <cstring>

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
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace Fby::Net
{
namespace
{
constexpr std::uint16_t kMagic = 0xB7F1u;

[[nodiscard]] std::byte ByteOf(std::uint32_t InValue) noexcept
{
    return static_cast<std::byte>(InValue & 0xFFu);
}

void WriteU16(std::byte* OutData, std::uint16_t InValue) noexcept
{
    OutData[0] = ByteOf(InValue);
    OutData[1] = ByteOf(static_cast<std::uint32_t>(InValue) >> 8);
}

void WriteU32(std::byte* OutData, std::uint32_t InValue) noexcept
{
    OutData[0] = ByteOf(InValue);
    OutData[1] = ByteOf(InValue >> 8);
    OutData[2] = ByteOf(InValue >> 16);
    OutData[3] = ByteOf(InValue >> 24);
}

[[nodiscard]] std::uint16_t ReadU16(const std::byte* InData) noexcept
{
    return static_cast<std::uint16_t>(static_cast<unsigned>(InData[0]) |
                                      (static_cast<unsigned>(InData[1]) << 8));
}

[[nodiscard]] std::uint32_t ReadU32(const std::byte* InData) noexcept
{
    return static_cast<std::uint32_t>(static_cast<unsigned>(InData[0])) |
           (static_cast<std::uint32_t>(static_cast<unsigned>(InData[1])) << 8) |
           (static_cast<std::uint32_t>(static_cast<unsigned>(InData[2])) << 16) |
           (static_cast<std::uint32_t>(static_cast<unsigned>(InData[3])) << 24);
}

#if defined(_WIN32)
[[nodiscard]] bool SocketWouldBlock() noexcept
{
    return WSAGetLastError() == WSAEWOULDBLOCK;
}
void CloseSocketHandle(std::uintptr_t InHandle) noexcept
{
    ::closesocket(static_cast<SOCKET>(InHandle));
}
#else
[[nodiscard]] bool SocketWouldBlock() noexcept
{
    return errno == EAGAIN || errno == EWOULDBLOCK;
}
void CloseSocketHandle(std::uintptr_t InHandle) noexcept
{
    ::close(static_cast<int>(InHandle));
}
#endif

[[nodiscard]] std::uintptr_t InvalidHandle() noexcept
{
    return static_cast<std::uintptr_t>(-1);
}

[[nodiscard]] int SocketError() noexcept
{
#if defined(_WIN32)
    return WSAGetLastError();
#else
    return errno;
#endif
}
}

bool DatagramSocket::EnsureSocketsInitialized(std::string* InError) noexcept
{
#if defined(_WIN32)
    static const int GStartupResult = []()
    {
        WSADATA ScopedData{};
        return WSAStartup(MAKEWORD(2, 2), &ScopedData);
    }();
    if (GStartupResult != 0)
    {
        if (InError != nullptr)
        {
            *InError = "WSAStartup failed: " + std::to_string(GStartupResult);
        }
        return false;
    }
#else
    (void)InError;
#endif
    return true;
}

DatagramSocket::~DatagramSocket()
{
    Close();
}

DatagramSocket::DatagramSocket(DatagramSocket&& InOther) noexcept
{
    MHandle = InOther.MHandle;
    InOther.MHandle = kInvalidHandle;
}

DatagramSocket& DatagramSocket::operator=(DatagramSocket&& InOther) noexcept
{
    if (this != &InOther)
    {
        Close();
        MHandle = InOther.MHandle;
        InOther.MHandle = kInvalidHandle;
    }
    return *this;
}

bool DatagramSocket::Open(std::uint16_t InPort, std::string* InError) noexcept
{
    if (!EnsureSocketsInitialized(InError))
    {
        return false;
    }
    Close();
#if defined(_WIN32)
    const SOCKET ScopedSocket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (ScopedSocket == INVALID_SOCKET)
    {
        if (InError != nullptr)
        {
            *InError = "socket() failed: " + std::to_string(SocketError());
        }
        return false;
    }
    sockaddr_in ScopedAddress{};
    ScopedAddress.sin_family = AF_INET;
    ScopedAddress.sin_addr.s_addr = htonl(INADDR_ANY);
    ScopedAddress.sin_port = htons(InPort);
    if (::bind(ScopedSocket, reinterpret_cast<const sockaddr*>(&ScopedAddress), sizeof(ScopedAddress)) ==
        SOCKET_ERROR)
    {
        if (InError != nullptr)
        {
            *InError = "bind() failed: " + std::to_string(SocketError());
        }
        ::closesocket(ScopedSocket);
        return false;
    }
    u_long ScopedMode = 1;
    if (::ioctlsocket(ScopedSocket, FIONBIO, &ScopedMode) == SOCKET_ERROR)
    {
        if (InError != nullptr)
        {
            *InError = "ioctlsocket(FIONBIO) failed: " + std::to_string(SocketError());
        }
        ::closesocket(ScopedSocket);
        return false;
    }
    MHandle = static_cast<std::uintptr_t>(ScopedSocket);
#else
    const int ScopedSocket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (ScopedSocket < 0)
    {
        if (InError != nullptr)
        {
            *InError = "socket() failed: " + std::to_string(SocketError());
        }
        return false;
    }
    sockaddr_in ScopedAddress{};
    ScopedAddress.sin_family = AF_INET;
    ScopedAddress.sin_addr.s_addr = htonl(INADDR_ANY);
    ScopedAddress.sin_port = htons(InPort);
    if (::bind(ScopedSocket, reinterpret_cast<const sockaddr*>(&ScopedAddress), sizeof(ScopedAddress)) != 0)
    {
        if (InError != nullptr)
        {
            *InError = "bind() failed: " + std::to_string(SocketError());
        }
        ::close(ScopedSocket);
        return false;
    }
    const int ScopedFlags = ::fcntl(ScopedSocket, F_GETFL, 0);
    if (ScopedFlags >= 0)
    {
        (void)::fcntl(ScopedSocket, F_SETFL, ScopedFlags | O_NONBLOCK);
    }
    MHandle = static_cast<std::uintptr_t>(ScopedSocket);
#endif
    return true;
}

void DatagramSocket::Close() noexcept
{
    if (MHandle != kInvalidHandle)
    {
        CloseSocketHandle(MHandle);
        MHandle = kInvalidHandle;
    }
}

std::uint16_t DatagramSocket::LocalPort() const noexcept
{
    if (!Valid())
    {
        return 0;
    }
    sockaddr_in ScopedAddress{};
#if defined(_WIN32)
    int ScopedLength = static_cast<int>(sizeof(ScopedAddress));
    if (::getsockname(static_cast<SOCKET>(MHandle), reinterpret_cast<sockaddr*>(&ScopedAddress), &ScopedLength) != 0)
    {
        return 0;
    }
#else
    socklen_t ScopedLength = sizeof(ScopedAddress);
    if (::getsockname(static_cast<int>(MHandle), reinterpret_cast<sockaddr*>(&ScopedAddress), &ScopedLength) != 0)
    {
        return 0;
    }
#endif
    return ntohs(ScopedAddress.sin_port);
}

bool DatagramSocket::SendTo(const NetAddress& InAddress, std::span<const std::byte> InData) noexcept
{
    if (!Valid() || InData.empty())
    {
        return false;
    }
    sockaddr_in ScopedAddress{};
    ScopedAddress.sin_family = AF_INET;
    ScopedAddress.sin_addr.s_addr = htonl(InAddress.Host);
    ScopedAddress.sin_port = htons(InAddress.Port);
#if defined(_WIN32)
    const int ScopedSent =
        ::sendto(static_cast<SOCKET>(MHandle), reinterpret_cast<const char*>(InData.data()),
                 static_cast<int>(InData.size()), 0, reinterpret_cast<const sockaddr*>(&ScopedAddress),
                 sizeof(ScopedAddress));
    return ScopedSent == static_cast<int>(InData.size());
#else
    const ssize_t ScopedSent =
        ::sendto(static_cast<int>(MHandle), InData.data(), InData.size(), 0,
                 reinterpret_cast<const sockaddr*>(&ScopedAddress), sizeof(ScopedAddress));
    return ScopedSent == static_cast<ssize_t>(InData.size());
#endif
}

int DatagramSocket::ReceiveFrom(NetAddress* OutAddress, std::span<std::byte> InBuffer) noexcept
{
    if (!Valid() || InBuffer.empty())
    {
        return -1;
    }
    sockaddr_in ScopedAddress{};
#if defined(_WIN32)
    int ScopedLength = static_cast<int>(sizeof(ScopedAddress));
    const int ScopedReceived =
        ::recvfrom(static_cast<SOCKET>(MHandle), reinterpret_cast<char*>(InBuffer.data()),
                   static_cast<int>(InBuffer.size()), 0, reinterpret_cast<sockaddr*>(&ScopedAddress), &ScopedLength);
    if (ScopedReceived == SOCKET_ERROR)
    {
        return SocketWouldBlock() ? 0 : -1;
    }
#else
    socklen_t ScopedLength = sizeof(ScopedAddress);
    const ssize_t ScopedReceived =
        ::recvfrom(static_cast<int>(MHandle), InBuffer.data(), InBuffer.size(), 0,
                   reinterpret_cast<sockaddr*>(&ScopedAddress), &ScopedLength);
    if (ScopedReceived < 0)
    {
        return SocketWouldBlock() ? 0 : -1;
    }
#endif
    if (OutAddress != nullptr)
    {
        OutAddress->Host = ntohl(ScopedAddress.sin_addr.s_addr);
        OutAddress->Port = ntohs(ScopedAddress.sin_port);
    }
    return static_cast<int>(ScopedReceived);
}

bool ParseAddress(std::string_view InText, std::uint16_t InDefaultPort, NetAddress* OutAddress) noexcept
{
    if (OutAddress == nullptr || InText.empty())
    {
        return false;
    }
    std::string ScopedHost(InText);
    std::uint16_t ScopedPort = InDefaultPort;
    const std::size_t ScopedColon = ScopedHost.rfind(':');
    if (ScopedColon != std::string::npos && ScopedHost.find(':') == ScopedColon)
    {
        const std::string ScopedPortText = ScopedHost.substr(ScopedColon + 1);
        ScopedHost = ScopedHost.substr(0, ScopedColon);
        unsigned long ScopedParsed = 0;
        for (const char ScopedCharacter : ScopedPortText)
        {
            if (ScopedCharacter < '0' || ScopedCharacter > '9')
            {
                return false;
            }
            ScopedParsed = ScopedParsed * 10u + static_cast<unsigned long>(ScopedCharacter - '0');
            if (ScopedParsed > 65535u)
            {
                return false;
            }
        }
        ScopedPort = static_cast<std::uint16_t>(ScopedParsed);
    }
    if (ScopedHost.empty() || ScopedHost.size() > 255)
    {
        return false;
    }
    in_addr ScopedAddress{};
    if (::inet_pton(AF_INET, ScopedHost.c_str(), &ScopedAddress) != 1)
    {
        if (ScopedHost == "localhost")
        {
            ScopedAddress.s_addr = htonl(INADDR_LOOPBACK);
        }
        else
        {
            return false;
        }
    }
    OutAddress->Host = ntohl(ScopedAddress.s_addr);
    OutAddress->Port = ScopedPort;
    return true;
}

std::string AddressToString(const NetAddress& InAddress)
{
    in_addr ScopedAddress{};
    ScopedAddress.s_addr = htonl(InAddress.Host);
    char ScopedBuffer[64] = {};
    if (::inet_ntop(AF_INET, &ScopedAddress, ScopedBuffer, sizeof(ScopedBuffer)) == nullptr)
    {
        return {};
    }
    return std::string(ScopedBuffer) + ":" + std::to_string(InAddress.Port);
}

bool NetTransport::Open(std::uint16_t InPort, std::string* InError) noexcept
{
    Close();
    if (!MSocket.Open(InPort, InError))
    {
        return false;
    }
    MPeers = std::make_unique<TransportPeer[]>(kMaxPeers);
    if (!MPeers)
    {
        MSocket.Close();
        return false;
    }
    MNow = 0.0f;
    return true;
}

void NetTransport::Close() noexcept
{
    MSocket.Close();
    MPeers.reset();
    MConnected = nullptr;
}

bool NetTransport::Connect(const NetAddress& InRemote) noexcept
{
    if (!Opened())
    {
        return false;
    }
    TransportPeer* ScopedPeer = FindOrCreatePeer(InRemote);
    if (ScopedPeer == nullptr)
    {
        return false;
    }
    MConnected = ScopedPeer;
    return true;
}

TransportPeer* NetTransport::FindPeer(const NetAddress& InAddress) noexcept
{
    if (!MPeers)
    {
        return nullptr;
    }
    for (std::uint32_t ScopedI = 0; ScopedI < kMaxPeers; ++ScopedI)
    {
        TransportPeer& ScopedPeer = MPeers[ScopedI];
        if (ScopedPeer.Active && ScopedPeer.Address == InAddress)
        {
            return &ScopedPeer;
        }
    }
    return nullptr;
}

TransportPeer* NetTransport::FindOrCreatePeer(const NetAddress& InAddress) noexcept
{
    TransportPeer* ScopedExisting = FindPeer(InAddress);
    if (ScopedExisting != nullptr)
    {
        return ScopedExisting;
    }
    if (!MPeers)
    {
        return nullptr;
    }
    for (std::uint32_t ScopedI = 0; ScopedI < kMaxPeers; ++ScopedI)
    {
        TransportPeer& ScopedPeer = MPeers[ScopedI];
        if (!ScopedPeer.Active)
        {
            ScopedPeer = TransportPeer{};
            ScopedPeer.Active = true;
            ScopedPeer.Address = InAddress;
            return &ScopedPeer;
        }
    }
    return nullptr;
}

std::uint32_t NetTransport::RandomU32() noexcept
{
    MRandomState ^= MRandomState << 13;
    MRandomState ^= MRandomState >> 17;
    MRandomState ^= MRandomState << 5;
    return MRandomState;
}

void NetTransport::SetDropRate(float InRate) noexcept
{
    MDropRate = std::clamp(InRate, 0.0f, 1.0f);
}

void NetTransport::SetRetransmitBase(float InSeconds) noexcept
{
    MRetransmitBase = std::max(InSeconds, 0.001f);
}

void NetTransport::SetMaxAttempts(std::uint8_t InAttempts) noexcept
{
    MMaxAttempts = std::max<std::uint8_t>(InAttempts, 1);
}

std::uint32_t NetTransport::PeerCount() const noexcept
{
    if (!MPeers)
    {
        return 0;
    }
    std::uint32_t ScopedCount = 0;
    for (std::uint32_t ScopedI = 0; ScopedI < kMaxPeers; ++ScopedI)
    {
        if (MPeers[ScopedI].Active)
        {
            ++ScopedCount;
        }
    }
    return ScopedCount;
}

const TransportPeer* NetTransport::Peer(std::uint32_t InIndex) const noexcept
{
    if (!MPeers)
    {
        return nullptr;
    }
    std::uint32_t ScopedCursor = 0;
    for (std::uint32_t ScopedI = 0; ScopedI < kMaxPeers; ++ScopedI)
    {
        const TransportPeer& ScopedPeer = MPeers[ScopedI];
        if (!ScopedPeer.Active)
        {
            continue;
        }
        if (ScopedCursor == InIndex)
        {
            return &ScopedPeer;
        }
        ++ScopedCursor;
    }
    return nullptr;
}

bool NetTransport::SendDatagram(TransportPeer& InPeer, std::uint16_t InSeq, std::uint8_t InChannel,
                                std::uint8_t InFragIndex, std::uint8_t InFragCount, std::uint16_t InMsgId,
                                std::uint16_t InMsgBytes, std::span<const std::byte> InPayload) noexcept
{
    if (!Opened() || InPayload.size() + kHeaderSize > kMaxDatagram)
    {
        return false;
    }
    std::array<std::byte, kMaxDatagram> ScopedBuffer{};
    WriteU16(ScopedBuffer.data(), kMagic);
    WriteU16(ScopedBuffer.data() + 2, InSeq);
    WriteU16(ScopedBuffer.data() + 4, InPeer.RxLatest);
    WriteU32(ScopedBuffer.data() + 6, InPeer.RxBits);
    ScopedBuffer[10] = static_cast<std::byte>(InChannel);
    ScopedBuffer[11] = static_cast<std::byte>(InFragCount > 1 ? 1 : 0);
    ScopedBuffer[12] = static_cast<std::byte>(InFragIndex);
    ScopedBuffer[13] = static_cast<std::byte>(InFragCount);
    WriteU16(ScopedBuffer.data() + 14, InMsgId);
    WriteU16(ScopedBuffer.data() + 16, InMsgBytes);
    WriteU16(ScopedBuffer.data() + 18, static_cast<std::uint16_t>(InPayload.size()));
    std::memcpy(ScopedBuffer.data() + kHeaderSize, InPayload.data(), InPayload.size());
    const std::span<const std::byte> ScopedDatagram(ScopedBuffer.data(), kHeaderSize + InPayload.size());

    if (MDropRate > 0.0f)
    {
        const float ScopedRoll = static_cast<float>(RandomU32() & 0xFFFFFFu) / 16777215.0f;
        if (ScopedRoll < MDropRate)
        {
            ++MStats.DroppedBySimulation;
            return true;
        }
    }
    if (!MSocket.SendTo(InPeer.Address, ScopedDatagram))
    {
        ++MStats.Rejected;
        return false;
    }
    ++MStats.PacketsSent;
    MStats.BytesSent += ScopedDatagram.size();
    return true;
}

bool NetTransport::Send(const NetAddress& InTo, std::span<const std::byte> InPayload,
                        TransportChannel InChannel) noexcept
{
    if (!Opened() || InPayload.empty())
    {
        return false;
    }
    if (MConnected != nullptr && !(MConnected->Address == InTo))
    {
        return false;
    }
    if (InPayload.size() > TransportPeer::kMaxReassemblyBytes || InPayload.size() > 0xFFFFu)
    {
        return false;
    }
    TransportPeer* ScopedPeer = FindOrCreatePeer(InTo);
    if (ScopedPeer == nullptr)
    {
        return false;
    }
    if (InChannel == TransportChannel::Reliable)
    {
        if (InPayload.size() > TransportPeer::kMaxReliablePayload)
        {
            return false;
        }
        TransportPeer::Outstanding* ScopedSlot = nullptr;
        for (TransportPeer::Outstanding& ScopedCandidate : ScopedPeer->Outbox)
        {
            if (!ScopedCandidate.Used)
            {
                ScopedSlot = &ScopedCandidate;
                break;
            }
        }
        if (ScopedSlot == nullptr)
        {
            ++MStats.Rejected;
            return false;
        }
        const std::uint16_t ScopedSeq = ScopedPeer->TxSeq++;
        ScopedSlot->Used = true;
        ScopedSlot->Seq = ScopedSeq;
        ScopedSlot->Bytes = static_cast<std::uint16_t>(InPayload.size());
        ScopedSlot->Age = 0.0f;
        ScopedSlot->SentTime = MNow;
        ScopedSlot->Attempts = 1;
        std::memcpy(ScopedSlot->Data.data(), InPayload.data(), InPayload.size());
        if (!SendDatagram(*ScopedPeer, ScopedSeq, 1, 0, 1, ScopedSeq, static_cast<std::uint16_t>(InPayload.size()),
                          InPayload))
        {
            ScopedSlot->Used = false;
            return false;
        }
        return true;
    }

    const std::uint32_t ScopedFragmentCount =
        (static_cast<std::uint32_t>(InPayload.size()) + kMaxPayload - 1) / kMaxPayload;
    if (ScopedFragmentCount > kMaxFragments)
    {
        return false;
    }
    const std::uint16_t ScopedMsgId = ScopedPeer->TxMsgSeq++;
    for (std::uint32_t ScopedFragment = 0; ScopedFragment < ScopedFragmentCount; ++ScopedFragment)
    {
        const std::size_t ScopedOffset = static_cast<std::size_t>(ScopedFragment) * kMaxPayload;
        const std::size_t ScopedBytes = std::min<std::size_t>(kMaxPayload, InPayload.size() - ScopedOffset);
        const std::uint16_t ScopedSeq = ScopedPeer->TxSeq++;
        if (!SendDatagram(*ScopedPeer, ScopedSeq, 0, static_cast<std::uint8_t>(ScopedFragment),
                          static_cast<std::uint8_t>(ScopedFragmentCount), ScopedMsgId,
                          static_cast<std::uint16_t>(InPayload.size()), InPayload.subspan(ScopedOffset, ScopedBytes)))
        {
            return false;
        }
        if (ScopedFragmentCount > 1)
        {
            ++MStats.FragmentPackets;
        }
    }
    return true;
}

void NetTransport::UpdateReceiveState(TransportPeer& InPeer, std::uint16_t InSeq, bool* OutNew) noexcept
{
    if (OutNew != nullptr)
    {
        *OutNew = false;
    }
    if (!InPeer.RxStarted)
    {
        InPeer.RxStarted = true;
        InPeer.RxLatest = InSeq;
        InPeer.RxBits = 0;
        if (OutNew != nullptr)
        {
            *OutNew = true;
        }
        return;
    }
    if (InSeq == InPeer.RxLatest)
    {
        return;
    }
    if (InSeq > InPeer.RxLatest)
    {
        const std::uint32_t ScopedShift = static_cast<std::uint32_t>(InSeq) - InPeer.RxLatest;
        if (ScopedShift >= 32)
        {
            InPeer.RxBits = 0;
        }
        else
        {
            InPeer.RxBits = (InPeer.RxBits << ScopedShift) | (1u << (ScopedShift - 1u));
        }
        InPeer.RxLatest = InSeq;
        if (OutNew != nullptr)
        {
            *OutNew = true;
        }
        return;
    }
    const std::uint32_t ScopedDiff = static_cast<std::uint32_t>(InPeer.RxLatest) - InSeq;
    if (ScopedDiff > 32)
    {
        return;
    }
    const std::uint32_t ScopedMask = 1u << (ScopedDiff - 1u);
    if ((InPeer.RxBits & ScopedMask) != 0)
    {
        return;
    }
    InPeer.RxBits |= ScopedMask;
    if (OutNew != nullptr)
    {
        *OutNew = true;
    }
}

void NetTransport::Acknowledge(TransportPeer& InPeer, std::uint16_t InAck, std::uint32_t InAckBits) noexcept
{
    for (TransportPeer::Outstanding& ScopedSlot : InPeer.Outbox)
    {
        if (!ScopedSlot.Used)
        {
            continue;
        }
        const std::uint32_t ScopedDiff = static_cast<std::uint32_t>(InAck - ScopedSlot.Seq);
        bool ScopedAcked = false;
        if (ScopedDiff == 0)
        {
            ScopedAcked = true;
        }
        else if (ScopedDiff <= 32 && (InAckBits & (1u << (ScopedDiff - 1u))) != 0)
        {
            ScopedAcked = true;
        }
        if (!ScopedAcked)
        {
            continue;
        }
        const float ScopedSample = std::max(MNow - ScopedSlot.SentTime, 0.0f);
        if (ScopedSlot.Attempts <= 1)
        {
            InPeer.Rtt = InPeer.Rtt * 0.8f + ScopedSample * 0.2f;
        }
        ScopedSlot.Used = false;
    }
}

void NetTransport::Deliver(TransportPeer& InPeer, const NetAddress& InFrom, std::span<const std::byte> InPayload,
                           MessageFn InFn, void* InUser) noexcept
{
    (void)InPeer;
    ++MStats.MessagesDelivered;
    if (InFn != nullptr)
    {
        InFn(InUser, InFrom, InPayload);
    }
}

void NetTransport::HandleDatagram(const NetAddress& InFrom, std::span<const std::byte> InDatagram, MessageFn InFn,
                                  void* InUser) noexcept
{
    if (InDatagram.size() < kHeaderSize)
    {
        ++MStats.Rejected;
        return;
    }
    if (ReadU16(InDatagram.data()) != kMagic)
    {
        ++MStats.Rejected;
        return;
    }
    if (MConnected != nullptr && !(MConnected->Address == InFrom))
    {
        ++MStats.Rejected;
        return;
    }
    TransportPeer* ScopedPeer = FindOrCreatePeer(InFrom);
    if (ScopedPeer == nullptr)
    {
        ++MStats.Rejected;
        return;
    }
    const std::uint16_t ScopedSeq = ReadU16(InDatagram.data() + 2);
    const std::uint16_t ScopedAck = ReadU16(InDatagram.data() + 4);
    const std::uint32_t ScopedAckBits = ReadU32(InDatagram.data() + 6);
    const std::uint8_t ScopedChannel = static_cast<std::uint8_t>(InDatagram[10]);
    const std::uint8_t ScopedFragIndex = static_cast<std::uint8_t>(InDatagram[12]);
    const std::uint8_t ScopedFragCount = static_cast<std::uint8_t>(InDatagram[13]);
    const std::uint16_t ScopedMsgId = ReadU16(InDatagram.data() + 14);
    const std::uint16_t ScopedMsgBytes = ReadU16(InDatagram.data() + 16);
    const std::uint16_t ScopedPayloadBytes = ReadU16(InDatagram.data() + 18);

    if (ScopedChannel > 1 || ScopedFragCount == 0 || ScopedFragIndex >= ScopedFragCount)
    {
        ++MStats.Rejected;
        return;
    }
    if (ScopedPayloadBytes != InDatagram.size() - kHeaderSize)
    {
        ++MStats.Rejected;
        return;
    }

    Acknowledge(*ScopedPeer, ScopedAck, ScopedAckBits);

    bool ScopedNew = false;
    UpdateReceiveState(*ScopedPeer, ScopedSeq, &ScopedNew);
    if (!ScopedNew)
    {
        ++MStats.Duplicates;
        return;
    }
    const std::span<const std::byte> ScopedPayload = InDatagram.subspan(kHeaderSize, ScopedPayloadBytes);
    if (ScopedFragCount <= 1)
    {
        Deliver(*ScopedPeer, InFrom, ScopedPayload, InFn, InUser);
        return;
    }

    TransportPeer::Reassembly& ScopedFragment = ScopedPeer->Fragment;
    if (!ScopedFragment.Active || ScopedFragment.MsgId != ScopedMsgId)
    {
        ScopedFragment = TransportPeer::Reassembly{};
        ScopedFragment.Active = true;
        ScopedFragment.MsgId = ScopedMsgId;
        ScopedFragment.FragCount = ScopedFragCount;
        ScopedFragment.MsgBytes = ScopedMsgBytes;
    }
    if (ScopedFragment.FragCount != ScopedFragCount || ScopedFragment.MsgBytes != ScopedMsgBytes)
    {
        ScopedFragment = TransportPeer::Reassembly{};
        ++MStats.ReassemblyFailures;
        return;
    }
    const std::size_t ScopedOffset = static_cast<std::size_t>(ScopedFragIndex) * kMaxPayload;
    if (ScopedOffset + ScopedPayloadBytes > TransportPeer::kMaxReassemblyBytes)
    {
        ScopedFragment = TransportPeer::Reassembly{};
        ++MStats.ReassemblyFailures;
        return;
    }
    const std::uint64_t ScopedMask = 1ull << (ScopedFragIndex & 63u);
    if ((ScopedFragment.Mask & ScopedMask) == 0)
    {
        std::memcpy(ScopedFragment.Data.data() + ScopedOffset, ScopedPayload.data(), ScopedPayloadBytes);
        ScopedFragment.Mask |= ScopedMask;
        ++ScopedFragment.FragReceived;
    }
    if (ScopedFragment.FragReceived >= ScopedFragment.FragCount)
    {
        const std::uint32_t ScopedTotal = std::min<std::uint32_t>(ScopedFragment.MsgBytes,
                                                                  TransportPeer::kMaxReassemblyBytes);
        // Deliver straight out of the reassembly buffer: the callback runs
        // synchronously before the buffer is reset for the next message.
        const std::span<const std::byte> ScopedMessage(ScopedFragment.Data.data(), ScopedTotal);
        const bool ScopedActive = ScopedFragment.Active;
        ScopedFragment.Active = false;
        if (ScopedActive)
        {
            Deliver(*ScopedPeer, InFrom, ScopedMessage, InFn, InUser);
        }
        ScopedFragment = TransportPeer::Reassembly{};
    }
}

std::uint32_t NetTransport::Poll(MessageFn InFn, void* InUser) noexcept
{
    if (!Opened())
    {
        return 0;
    }
    std::uint32_t ScopedDelivered = 0;
    std::array<std::byte, 4096> ScopedBuffer{};
    for (std::uint32_t ScopedIteration = 0; ScopedIteration < 64; ++ScopedIteration)
    {
        NetAddress ScopedFrom{};
        const int ScopedBytes = MSocket.ReceiveFrom(&ScopedFrom, std::span<std::byte>(ScopedBuffer));
        if (ScopedBytes <= 0)
        {
            break;
        }
        ++MStats.PacketsReceived;
        MStats.BytesReceived += static_cast<std::uint64_t>(ScopedBytes);
        const std::uint64_t ScopedBefore = MStats.MessagesDelivered;
        HandleDatagram(ScopedFrom, std::span<const std::byte>(ScopedBuffer.data(), static_cast<std::size_t>(ScopedBytes)),
                       InFn, InUser);
        if (MStats.MessagesDelivered > ScopedBefore)
        {
            ++ScopedDelivered;
        }
    }
    return ScopedDelivered;
}

void NetTransport::Tick(float InDeltaSeconds) noexcept
{
    MNow += std::max(InDeltaSeconds, 0.0f);
    if (!MPeers)
    {
        return;
    }
    for (std::uint32_t ScopedPeerIndex = 0; ScopedPeerIndex < kMaxPeers; ++ScopedPeerIndex)
    {
        TransportPeer& ScopedPeer = MPeers[ScopedPeerIndex];
        if (!ScopedPeer.Active)
        {
            continue;
        }
        for (TransportPeer::Outstanding& ScopedSlot : ScopedPeer.Outbox)
        {
            if (!ScopedSlot.Used)
            {
                continue;
            }
            ScopedSlot.Age += std::max(InDeltaSeconds, 0.0f);
            const float ScopedTimeout = std::max(ScopedPeer.Rtt * 1.5f, MRetransmitBase);
            if (ScopedSlot.Age < ScopedTimeout)
            {
                continue;
            }
            if (ScopedSlot.Attempts >= MMaxAttempts)
            {
                ScopedSlot.Used = false;
                ++MStats.Rejected;
                continue;
            }
            ScopedSlot.Attempts++;
            ScopedSlot.Age = 0.0f;
            const std::span<const std::byte> ScopedPayload(ScopedSlot.Data.data(), ScopedSlot.Bytes);
            if (SendDatagram(ScopedPeer, ScopedSlot.Seq, 1, 0, 1, ScopedSlot.Seq, ScopedSlot.Bytes, ScopedPayload))
            {
                ++MStats.Retransmits;
            }
        }
    }
}
}
