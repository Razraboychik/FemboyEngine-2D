// FemboyEngine2D - datagram link backends implementation.
#include "Engine/Network/NetLink.hpp"

#include <cstring>

namespace Fby::Net
{
namespace
{
bool TransportSend(void* InSelf, const NetAddress& InTo, std::span<const std::byte> InMessage) noexcept
{
    auto* ScopedTransport = static_cast<NetTransport*>(InSelf);
    return ScopedTransport != nullptr &&
           ScopedTransport->Send(InTo, InMessage, TransportChannel::Unreliable);
}

std::uint32_t TransportPoll(void* InSelf, LinkMessageFn InFn, void* InUser) noexcept
{
    auto* ScopedTransport = static_cast<NetTransport*>(InSelf);
    return ScopedTransport != nullptr ? ScopedTransport->Poll(InFn, InUser) : 0;
}
}

NetLink MakeTransportLink(NetTransport* InTransport) noexcept
{
    NetLink ScopedLink;
    ScopedLink.Self = InTransport;
    ScopedLink.SendCall = &TransportSend;
    ScopedLink.PollCall = &TransportPoll;
    return ScopedLink;
}

NetLink MakeMemoryLink(MemoryEndpoint* InEndpoint) noexcept
{
    return InEndpoint != nullptr ? InEndpoint->Link() : NetLink{};
}

NetAddress MemoryNetwork::Open(std::uint16_t InPort) noexcept
{
    std::uint16_t ScopedPort = InPort;
    if (ScopedPort == 0)
    {
        for (std::uint16_t ScopedCandidate = 20000; ScopedCandidate < 20000 + kMaxEndpoints; ++ScopedCandidate)
        {
            bool ScopedTaken = false;
            for (const Endpoint& ScopedEndpoint : MEndpoints)
            {
                ScopedTaken = ScopedTaken || (ScopedEndpoint.Used && ScopedEndpoint.Address.Port == ScopedCandidate);
            }
            if (!ScopedTaken)
            {
                ScopedPort = ScopedCandidate;
                break;
            }
        }
    }
    if (ScopedPort == 0)
    {
        return NetAddress{};
    }
    for (const Endpoint& ScopedEndpoint : MEndpoints)
    {
        if (ScopedEndpoint.Used && ScopedEndpoint.Address.Port == ScopedPort)
        {
            return NetAddress{};
        }
    }
    for (Endpoint& ScopedEndpoint : MEndpoints)
    {
        if (!ScopedEndpoint.Used)
        {
            ScopedEndpoint = Endpoint{};
            ScopedEndpoint.Used = true;
            ScopedEndpoint.Address.Host = 0x7F000001u;
            ScopedEndpoint.Address.Port = ScopedPort;
            return ScopedEndpoint.Address;
        }
    }
    return NetAddress{};
}

void MemoryNetwork::Close(const NetAddress& InAddress) noexcept
{
    Endpoint* ScopedEndpoint = Find(InAddress);
    if (ScopedEndpoint != nullptr)
    {
        *ScopedEndpoint = Endpoint{};
    }
}

MemoryNetwork::Endpoint* MemoryNetwork::Find(const NetAddress& InAddress) noexcept
{
    for (Endpoint& ScopedEndpoint : MEndpoints)
    {
        if (ScopedEndpoint.Used && ScopedEndpoint.Address == InAddress)
        {
            return &ScopedEndpoint;
        }
    }
    return nullptr;
}

bool MemoryNetwork::Route(const NetAddress& InFrom, const NetAddress& InTo,
                          std::span<const std::byte> InMessage) noexcept
{
    if (InMessage.size() > kMaxMessageBytes)
    {
        return false;
    }
    if (MDropEvery != 0)
    {
        ++MDropCounter;
        if (MDropCounter % MDropEvery == 0)
        {
            return true;
        }
    }

    Endpoint* ScopedTarget = Find(InTo);
    if (ScopedTarget == nullptr)
    {
        return false;
    }
    if (ScopedTarget->Count >= kQueueDepth)
    {
        return false;
    }

    Frame& ScopedFrame = ScopedTarget->Queue[(ScopedTarget->Head + ScopedTarget->Count) % kQueueDepth];
    ScopedFrame.From = InFrom;
    ScopedFrame.Bytes = static_cast<std::uint16_t>(InMessage.size());
    ScopedFrame.ReleaseTick = MTick + MLatencyTicks;
    std::memcpy(ScopedFrame.Data.data(), InMessage.data(), InMessage.size());
    ++ScopedTarget->Count;
    return true;
}

bool MemoryNetwork::Pop(const NetAddress& InAddress, Frame* OutFrame) noexcept
{
    if (OutFrame == nullptr)
    {
        return false;
    }
    Endpoint* ScopedEndpoint = Find(InAddress);
    if (ScopedEndpoint == nullptr || ScopedEndpoint->Count == 0)
    {
        return false;
    }
    const Frame& ScopedHead = ScopedEndpoint->Queue[ScopedEndpoint->Head];
    if (ScopedHead.ReleaseTick > MTick)
    {
        return false;
    }
    *OutFrame = ScopedHead;
    ScopedEndpoint->Head = (ScopedEndpoint->Head + 1) % kQueueDepth;
    --ScopedEndpoint->Count;
    return true;
}

bool MemoryEndpoint::Open(MemoryNetwork* InNetwork, std::uint16_t InPort) noexcept
{
    if (InNetwork == nullptr)
    {
        return false;
    }
    const NetAddress ScopedAddress = InNetwork->Open(InPort);
    if (ScopedAddress.Port == 0)
    {
        return false;
    }
    MNetwork = InNetwork;
    MAddress = ScopedAddress;
    return true;
}

void MemoryEndpoint::Close() noexcept
{
    if (MNetwork != nullptr)
    {
        MNetwork->Close(MAddress);
    }
    MNetwork = nullptr;
    MAddress = NetAddress{};
}

NetLink MemoryEndpoint::Link() noexcept
{
    NetLink ScopedLink;
    ScopedLink.Self = this;
    ScopedLink.SendCall = &MemoryEndpoint::SendThunk;
    ScopedLink.PollCall = &MemoryEndpoint::PollThunk;
    return ScopedLink;
}

bool MemoryEndpoint::SendThunk(void* InSelf, const NetAddress& InTo,
                               std::span<const std::byte> InMessage) noexcept
{
    auto* ScopedEndpoint = static_cast<MemoryEndpoint*>(InSelf);
    return ScopedEndpoint != nullptr && ScopedEndpoint->MNetwork != nullptr &&
           ScopedEndpoint->MNetwork->Route(ScopedEndpoint->MAddress, InTo, InMessage);
}

std::uint32_t MemoryEndpoint::PollThunk(void* InSelf, LinkMessageFn InFn, void* InUser) noexcept
{
    auto* ScopedEndpoint = static_cast<MemoryEndpoint*>(InSelf);
    if (ScopedEndpoint == nullptr || ScopedEndpoint->MNetwork == nullptr || InFn == nullptr)
    {
        return 0;
    }

    std::uint32_t ScopedDelivered = 0;
    MemoryNetwork::Frame ScopedFrame;
    while (ScopedEndpoint->MNetwork->Pop(ScopedEndpoint->MAddress, &ScopedFrame))
    {
        InFn(InUser, ScopedFrame.From,
             std::span<const std::byte>(ScopedFrame.Data.data(), ScopedFrame.Bytes));
        ++ScopedDelivered;
    }
    return ScopedDelivered;
}
}
