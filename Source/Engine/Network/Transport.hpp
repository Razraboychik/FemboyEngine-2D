// FemboyEngine2D - UDP transport: sockets, reliable channel and fragmentation.
#pragma once

#include "Engine/Core/Types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace Fby::Net
{

// IPv4 endpoint. `Host` is in host byte order so two addresses compare with ==
// after a single ntohl on receive.
struct NetAddress
{
    std::uint32_t Host = 0;
    std::uint16_t Port = 0;

    [[nodiscard]] friend constexpr bool operator==(const NetAddress&, const NetAddress&) = default;
};

[[nodiscard]] bool ParseAddress(std::string_view InText, std::uint16_t InDefaultPort, NetAddress* OutAddress) noexcept;
[[nodiscard]] std::string AddressToString(const NetAddress& InAddress);

// A non-blocking IPv4 datagram socket. The handle is hidden behind an integer so
// the public header carries no platform socket types.
class DatagramSocket
{
public:
    DatagramSocket() = default;
    ~DatagramSocket();

    DatagramSocket(const DatagramSocket&) = delete;
    DatagramSocket& operator=(const DatagramSocket&) = delete;
    DatagramSocket(DatagramSocket&& InOther) noexcept;
    DatagramSocket& operator=(DatagramSocket&& InOther) noexcept;

    [[nodiscard]] bool Open(std::uint16_t InPort, std::string* InError = nullptr) noexcept;
    void Close() noexcept;

    [[nodiscard]] bool Valid() const noexcept
    {
        return MHandle != kInvalidHandle;
    }
    [[nodiscard]] std::uint16_t LocalPort() const noexcept;

    [[nodiscard]] bool SendTo(const NetAddress& InAddress, std::span<const std::byte> InData) noexcept;
    // Returns the datagram size, 0 when nothing is pending and -1 on error.
    [[nodiscard]] int ReceiveFrom(NetAddress* OutAddress, std::span<std::byte> InBuffer) noexcept;

    [[nodiscard]] static bool EnsureSocketsInitialized(std::string* InError = nullptr) noexcept;

private:
    static constexpr std::uintptr_t kInvalidHandle = static_cast<std::uintptr_t>(-1);
    std::uintptr_t MHandle = kInvalidHandle;
};

enum class TransportChannel : std::uint8_t
{
    Unreliable = 0,
    Reliable = 1,
};

struct TransportStats
{
    std::uint64_t PacketsSent = 0;
    std::uint64_t PacketsReceived = 0;
    std::uint64_t BytesSent = 0;
    std::uint64_t BytesReceived = 0;
    std::uint64_t DroppedBySimulation = 0;
    std::uint64_t Duplicates = 0;
    std::uint64_t Retransmits = 0;
    std::uint64_t FragmentPackets = 0;
    std::uint64_t MessagesDelivered = 0;
    std::uint64_t ReassemblyFailures = 0;
    std::uint64_t Rejected = 0;
};

// One peer's ack/sequence state and retransmit queue.
struct TransportPeer
{
    static constexpr std::uint32_t kMaxOutstanding = 48;
    static constexpr std::uint32_t kMaxReassemblyBytes = 16384;
    static constexpr std::uint32_t kMaxReliablePayload = 768;

    NetAddress Address{};
    bool Active = false;
    std::uint16_t TxSeq = 0;
    std::uint16_t TxMsgSeq = 0;
    std::uint16_t RxLatest = 0;
    std::uint32_t RxBits = 0;
    bool RxStarted = false;
    float Rtt = 0.1f;

    struct Outstanding
    {
        std::uint16_t Seq = 0;
        std::uint16_t Bytes = 0;
        float Age = 0.0f;
        float SentTime = 0.0f;
        std::uint8_t Attempts = 0;
        bool Used = false;
        std::array<std::byte, kMaxReliablePayload> Data{};
    };
    std::array<Outstanding, kMaxOutstanding> Outbox{};

    struct Reassembly
    {
        bool Active = false;
        std::uint16_t MsgId = 0;
        std::uint8_t FragCount = 0;
        std::uint8_t FragReceived = 0;
        std::uint16_t MsgBytes = 0;
        std::uint64_t Mask = 0;
        std::array<std::byte, kMaxReassemblyBytes> Data{};
    };
    Reassembly Fragment{};
};

// Server/client transport. A server accepts datagrams from up to `kMaxPeers`
// addresses; a client connects to one address and receives only from it.
class NetTransport
{
public:
    static constexpr std::uint32_t kMaxPeers = 8;
    static constexpr std::uint32_t kHeaderSize = 20;
    static constexpr std::uint32_t kMtu = 1200;
    static constexpr std::uint32_t kMaxDatagram = kMtu;
    static constexpr std::uint32_t kMaxPayload = kMtu - kHeaderSize;
    static constexpr std::uint32_t kMaxFragments = 64;

    using MessageFn = void (*)(void* InUser, const NetAddress& InFrom, std::span<const std::byte> InMessage);

    NetTransport() = default;

    NetTransport(const NetTransport&) = delete;
    NetTransport& operator=(const NetTransport&) = delete;

    [[nodiscard]] bool Open(std::uint16_t InPort, std::string* InError = nullptr) noexcept;
    void Close() noexcept;

    [[nodiscard]] bool Opened() const noexcept
    {
        return MSocket.Valid();
    }
    [[nodiscard]] std::uint16_t LocalPort() const noexcept
    {
        return MSocket.LocalPort();
    }
    [[nodiscard]] DatagramSocket& Socket() noexcept
    {
        return MSocket;
    }

    // Restricts the transport to one remote endpoint and pre-creates its peer.
    [[nodiscard]] bool Connect(const NetAddress& InRemote) noexcept;

    [[nodiscard]] bool Send(const NetAddress& InTo, std::span<const std::byte> InPayload,
                            TransportChannel InChannel) noexcept;

    // Drains pending datagrams and invokes `InFn` for every complete message.
    std::uint32_t Poll(MessageFn InFn, void* InUser) noexcept;

    // Retransmits due reliable messages and expires dead ones.
    void Tick(float InDeltaSeconds) noexcept;

    // Test hook: drops outgoing datagrams with this probability while keeping
    // reliable messages in the retransmit queue.
    void SetDropRate(float InRate) noexcept;
    [[nodiscard]] float DropRate() const noexcept
    {
        return MDropRate;
    }
    void SetRetransmitBase(float InSeconds) noexcept;
    void SetMaxAttempts(std::uint8_t InAttempts) noexcept;

    [[nodiscard]] const TransportStats& Stats() const noexcept
    {
        return MStats;
    }
    void ResetStats() noexcept
    {
        MStats = TransportStats{};
    }

    [[nodiscard]] std::uint32_t PeerCount() const noexcept;
    [[nodiscard]] const TransportPeer* Peer(std::uint32_t InIndex) const noexcept;

private:
    [[nodiscard]] TransportPeer* FindPeer(const NetAddress& InAddress) noexcept;
    [[nodiscard]] TransportPeer* FindOrCreatePeer(const NetAddress& InAddress) noexcept;

    void HandleDatagram(const NetAddress& InFrom, std::span<const std::byte> InDatagram, MessageFn InFn,
                        void* InUser) noexcept;
    void UpdateReceiveState(TransportPeer& InPeer, std::uint16_t InSeq, bool* OutNew) noexcept;
    void Acknowledge(TransportPeer& InPeer, std::uint16_t InAck, std::uint32_t InAckBits) noexcept;
    void Deliver(TransportPeer& InPeer, const NetAddress& InFrom, std::span<const std::byte> InPayload,
                 MessageFn InFn, void* InUser) noexcept;
    [[nodiscard]] bool SendDatagram(TransportPeer& InPeer, std::uint16_t InSeq, std::uint8_t InChannel,
                                    std::uint8_t InFragIndex, std::uint8_t InFragCount, std::uint16_t InMsgId,
                                    std::uint16_t InMsgBytes, std::span<const std::byte> InPayload) noexcept;
    [[nodiscard]] std::uint32_t RandomU32() noexcept;

    DatagramSocket MSocket{};
    // Peers own about 50 KiB each (retransmit queue + reassembly buffer), so the
    // table lives on the heap: a transport is a long-lived object, not a frame
    // allocation, and a 400 KiB stack object would blow a small thread stack.
    std::unique_ptr<TransportPeer[]> MPeers{};
    TransportPeer* MConnected = nullptr;
    TransportStats MStats{};
    float MDropRate = 0.0f;
    float MRetransmitBase = 0.05f;
    float MNow = 0.0f;
    std::uint8_t MMaxAttempts = 8;
    std::uint32_t MRandomState = 0x9E3779B9u;
};
}
