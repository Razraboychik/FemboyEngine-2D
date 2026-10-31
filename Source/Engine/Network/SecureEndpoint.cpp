// FemboyEngine2D - secure session endpoint implementation.
#include "Engine/Network/SecureEndpoint.hpp"

#include <array>
#include <cstring>

namespace Fby::Net
{

SecureStatus SecureEndpoint::Start(NetTransport* InTransport, const NetAddress& InRemote,
                                   const SecureChannelConfig& InConfig) noexcept
{
    if (InTransport == nullptr || !InTransport->Opened())
    {
        return SecureStatus::NotStarted;
    }

    MTransport = InTransport;
    MRemote = InRemote;
    MStarted = true;
    MFirstError = SecureStatus::Ok;
    MLastStatus = MChannel.Start(InConfig);
    if (MLastStatus != SecureStatus::Ok)
    {
        MStarted = false;
        return MLastStatus;
    }

    if (InConfig.Initiator)
    {
        const SecureStatus ScopedSent = SendHandshake();
        if (ScopedSent != SecureStatus::Ok)
        {
            MStarted = false;
            return ScopedSent;
        }
    }
    return SecureStatus::Ok;
}

void SecureEndpoint::Stop() noexcept
{
    MStarted = false;
    MTransport = nullptr;
    MChannel.Reset();
    MLastStatus = SecureStatus::NotStarted;
    MFirstError = SecureStatus::Ok;
}

SecureStatus SecureEndpoint::SendHandshake() noexcept
{
    std::array<std::byte, kSecureHandshakeBytes> ScopedMessage{};
    std::size_t ScopedSize = 0;
    const SecureStatus ScopedWritten = MChannel.WriteHandshake(ScopedMessage, &ScopedSize);
    if (ScopedWritten == SecureStatus::WrongTurn || ScopedWritten == SecureStatus::AlreadyEstablished)
    {
        return SecureStatus::Ok;
    }
    if (ScopedWritten != SecureStatus::Ok)
    {
        return ScopedWritten;
    }
    if (!MTransport->Send(MRemote, std::span<const std::byte>(ScopedMessage.data(), ScopedSize),
                          TransportChannel::Reliable))
    {
        return SecureStatus::SendFailed;
    }
    return SecureStatus::Ok;
}

SecureStatus SecureEndpoint::Send(std::span<const std::byte> InPayload) noexcept
{
    if (!MStarted)
    {
        return SecureStatus::NotStarted;
    }
    std::array<std::byte, kSecureMaxRecordBytes> ScopedRecord{};
    std::size_t ScopedSize = 0;
    const SecureStatus ScopedSealed = MChannel.Seal(InPayload, ScopedRecord, &ScopedSize);
    if (ScopedSealed != SecureStatus::Ok)
    {
        return ScopedSealed;
    }
    if (!MTransport->Send(MRemote, std::span<const std::byte>(ScopedRecord.data(), ScopedSize),
                          TransportChannel::Unreliable))
    {
        return SecureStatus::SendFailed;
    }
    return SecureStatus::Ok;
}

SecureStatus SecureEndpoint::Pump(PayloadFn InFn, void* InUser) noexcept
{
    if (!MStarted)
    {
        return SecureStatus::NotStarted;
    }
    MPumpFn = InFn;
    MPumpUser = InUser;
    MFirstError = SecureStatus::Ok;

    MTransport->Poll(&SecureEndpoint::OnMessage, this);

    MPumpFn = nullptr;
    MPumpUser = nullptr;
    return MFirstError;
}

void SecureEndpoint::OnMessage(void* InUser, const NetAddress& InFrom,
                               std::span<const std::byte> InMessage) noexcept
{
    auto* ScopedEndpoint = static_cast<SecureEndpoint*>(InUser);
    if (ScopedEndpoint == nullptr || !ScopedEndpoint->MStarted)
    {
        return;
    }
    // A server starts with an empty address and binds the peer on the first
    // inbound datagram; a client always knows its remote.
    if (ScopedEndpoint->MRemote.Host == 0 && ScopedEndpoint->MRemote.Port == 0)
    {
        ScopedEndpoint->MRemote = InFrom;
    }
    else if (!(InFrom == ScopedEndpoint->MRemote))
    {
        return;
    }
    const SecureStatus ScopedStatus = ScopedEndpoint->HandleMessage(InMessage);
    if (ScopedStatus != SecureStatus::Ok && ScopedEndpoint->MFirstError == SecureStatus::Ok)
    {
        ScopedEndpoint->MFirstError = ScopedStatus;
    }
}

SecureStatus SecureEndpoint::HandleMessage(std::span<const std::byte> InMessage) noexcept
{
    if (!MChannel.Established())
    {
        const SecureStatus ScopedRead = MChannel.ReadHandshake(InMessage);
        if (ScopedRead == SecureStatus::CookieRequired)
        {
            std::array<std::byte, 256> ScopedChallenge{};
            std::size_t ScopedChallengeSize = 0;
            const SecureStatus ScopedChallengeStatus =
                MChannel.WriteCookieChallenge(ScopedChallenge, &ScopedChallengeSize);
            if (ScopedChallengeStatus != SecureStatus::Ok)
            {
                MLastStatus = ScopedChallengeStatus;
                return ScopedChallengeStatus;
            }
            if (!MTransport->Send(MRemote,
                                  std::span<const std::byte>(ScopedChallenge.data(), ScopedChallengeSize),
                                  TransportChannel::Reliable))
            {
                MLastStatus = SecureStatus::SendFailed;
                return SecureStatus::SendFailed;
            }
            MLastStatus = SecureStatus::Ok;
            return SecureStatus::Ok;
        }
        if (ScopedRead != SecureStatus::Ok)
        {
            MLastStatus = ScopedRead;
            return ScopedRead;
        }
        // Every accepted handshake message except the final one has a reply.
        const SecureStatus ScopedSent = SendHandshake();
        if (ScopedSent != SecureStatus::Ok)
        {
            MLastStatus = ScopedSent;
            return ScopedSent;
        }
        MLastStatus = SecureStatus::Ok;
        return SecureStatus::Ok;
    }

    std::array<std::byte, kSecureMaxPlainBytes> ScopedPayload{};
    std::size_t ScopedSize = 0;
    const SecureStatus ScopedOpened = MChannel.Open(InMessage, ScopedPayload, &ScopedSize);
    if (ScopedOpened != SecureStatus::Ok)
    {
        MLastStatus = ScopedOpened;
        return ScopedOpened;
    }
    MLastStatus = SecureStatus::Ok;
    if (MPumpFn != nullptr)
    {
        MPumpFn(MPumpUser, std::span<const std::byte>(ScopedPayload.data(), ScopedSize));
    }
    else if (MSinkFn != nullptr)
    {
        MSinkFn(MSinkUser, std::span<const std::byte>(ScopedPayload.data(), ScopedSize));
    }
    return SecureStatus::Ok;
}

SecureStatus SecureEndpoint::HandleRawMessage(const NetAddress& InFrom,
                                              std::span<const std::byte> InMessage) noexcept
{
    if (!MStarted)
    {
        return SecureStatus::NotStarted;
    }
    if (MRemote.Host == 0 && MRemote.Port == 0)
    {
        MRemote = InFrom;
    }
    else if (!(InFrom == MRemote))
    {
        return SecureStatus::BadKey;
    }
    return HandleMessage(InMessage);
}
}
