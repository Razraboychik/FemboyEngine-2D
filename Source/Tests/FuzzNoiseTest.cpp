// FemboyEngine2D - deterministic mutation test for the Noise handshake and records.
#include "Engine/Core/CryptoAsymmetric.hpp"
#include "Engine/Network/SecureChannel.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <vector>

using namespace Fby::Net;

namespace
{
int GFailures = 0;

void Check(bool InCondition, const char* InMessage)
{
    if (!InCondition)
    {
        std::fprintf(stderr, "FAIL: %s\n", InMessage);
        ++GFailures;
    }
}

std::uint64_t GState = 0xC2B2AE3D27D4EB4Full;

std::uint32_t NextRandom()
{
    GState ^= GState << 13;
    GState ^= GState >> 7;
    GState ^= GState << 17;
    return static_cast<std::uint32_t>(GState >> 32);
}

SecureChannelConfig MakeConfig(bool InInitiator, std::uint8_t InSeed, std::uint64_t InSessionId)
{
    std::array<std::byte, Fby::Crypto::kX25519KeyBytes> ScopedSeed{};
    ScopedSeed.fill(static_cast<std::byte>(InSeed));
    SecureChannelConfig ScopedConfig;
    ScopedConfig.ProtocolV2 = false;
    (void)Fby::Crypto::X25519KeyPairFromSeed(ScopedSeed, &ScopedConfig.LocalStatic);
    ScopedConfig.Initiator = InInitiator;
    ScopedConfig.SessionId = InSessionId;
    return ScopedConfig;
}

void Mutate(std::vector<std::byte>& InBytes)
{
    if (InBytes.empty())
    {
        return;
    }
    const std::uint32_t ScopedFlips = 1u + (NextRandom() % 4u);
    for (std::uint32_t ScopedFlip = 0; ScopedFlip < ScopedFlips; ++ScopedFlip)
    {
        const std::size_t ScopedIndex = NextRandom() % InBytes.size();
        InBytes[ScopedIndex] = static_cast<std::byte>(NextRandom() & 0xFFu);
    }
}

bool Establish(SecureChannel& InInitiator, SecureChannel& InResponder)
{
    std::array<std::byte, 4096> ScopedBuffer{};
    std::size_t ScopedBytes = 0;
    if (InInitiator.WriteHandshake(ScopedBuffer, &ScopedBytes) != SecureStatus::Ok)
    {
        return false;
    }
    if (InResponder.ReadHandshake(std::span<const std::byte>(ScopedBuffer.data(), ScopedBytes)) != SecureStatus::Ok)
    {
        return false;
    }
    if (InResponder.WriteHandshake(ScopedBuffer, &ScopedBytes) != SecureStatus::Ok)
    {
        return false;
    }
    if (InInitiator.ReadHandshake(std::span<const std::byte>(ScopedBuffer.data(), ScopedBytes)) != SecureStatus::Ok)
    {
        return false;
    }
    if (InInitiator.WriteHandshake(ScopedBuffer, &ScopedBytes) != SecureStatus::Ok)
    {
        return false;
    }
    if (InResponder.ReadHandshake(std::span<const std::byte>(ScopedBuffer.data(), ScopedBytes)) != SecureStatus::Ok)
    {
        return false;
    }
    return InInitiator.Established() && InResponder.Established();
}

void TestHandshakeMutations(const SecureChannelConfig& InInitiatorConfig,
                            const SecureChannelConfig& InResponderConfig)
{
    std::array<std::byte, 4096> ScopedMessage{};
    std::size_t ScopedBytes = 0;
    SecureChannel ScopedInitiator;
    Check(ScopedInitiator.Start(InInitiatorConfig) == SecureStatus::Ok, "initiator starts");
    Check(ScopedInitiator.WriteHandshake(ScopedMessage, &ScopedBytes) == SecureStatus::Ok, "message 1 writes");

    for (std::uint32_t ScopedIteration = 0; ScopedIteration < 300; ++ScopedIteration)
    {
        std::vector<std::byte> ScopedMutated(ScopedMessage.data(), ScopedMessage.data() + ScopedBytes);
        Mutate(ScopedMutated);
        SecureChannel ScopedResponder;
        (void)ScopedResponder.Start(InResponderConfig);
        (void)ScopedResponder.ReadHandshake(ScopedMutated);
    }
}

void TestRecordMutations(const SecureChannelConfig& InInitiatorConfig, const SecureChannelConfig& InResponderConfig)
{
    SecureChannel ScopedInitiator;
    SecureChannel ScopedResponder;
    Check(ScopedInitiator.Start(InInitiatorConfig) == SecureStatus::Ok, "record initiator starts");
    Check(ScopedResponder.Start(InResponderConfig) == SecureStatus::Ok, "record responder starts");
    Check(Establish(ScopedInitiator, ScopedResponder), "handshake establishes");

    const std::array<std::byte, 16> ScopedPlaintext{std::byte{0x41}, std::byte{0x42}, std::byte{0x43}};
    std::array<std::byte, 256> ScopedRecord{};
    std::size_t ScopedRecordBytes = 0;
    Check(ScopedInitiator.Seal(ScopedPlaintext, ScopedRecord, &ScopedRecordBytes) == SecureStatus::Ok, "record seals");
    const std::vector<std::byte> ScopedValid(ScopedRecord.data(), ScopedRecord.data() + ScopedRecordBytes);

    std::array<std::byte, 256> ScopedOutput{};
    for (std::uint32_t ScopedIteration = 0; ScopedIteration < 2000; ++ScopedIteration)
    {
        std::vector<std::byte> ScopedMutated = ScopedValid;
        if ((ScopedIteration % 5u) == 4u)
        {
            ScopedMutated.resize(NextRandom() % 300u);
            for (std::byte& ScopedByte : ScopedMutated)
            {
                ScopedByte = static_cast<std::byte>(NextRandom() & 0xFFu);
            }
        }
        else
        {
            Mutate(ScopedMutated);
        }
        std::size_t ScopedOutputBytes = 0;
        (void)ScopedResponder.Open(ScopedMutated, ScopedOutput, &ScopedOutputBytes);
    }
}
}

int main()
{
    constexpr std::uint64_t ScopedSessionId = 0x1234567890ABCDEFull;
    const SecureChannelConfig ScopedInitiatorConfig = MakeConfig(true, 0x31, ScopedSessionId);
    const SecureChannelConfig ScopedResponderConfig = MakeConfig(false, 0x32, ScopedSessionId);

    TestHandshakeMutations(ScopedInitiatorConfig, ScopedResponderConfig);
    TestRecordMutations(ScopedInitiatorConfig, ScopedResponderConfig);

    // The same sweeps on the opt-in v2 transcript and envelope.
    SecureChannelConfig ScopedInitiatorV2 = ScopedInitiatorConfig;
    SecureChannelConfig ScopedResponderV2 = ScopedResponderConfig;
    ScopedInitiatorV2.ProtocolV2 = true;
    ScopedResponderV2.ProtocolV2 = true;
    TestHandshakeMutations(ScopedInitiatorV2, ScopedResponderV2);
    TestRecordMutations(ScopedInitiatorV2, ScopedResponderV2);

    if (GFailures == 0)
    {
        std::printf("fuzz_noise: OK (v1/v2 handshake and record mutation sweeps)\n");
        return 0;
    }
    std::fprintf(stderr, "fuzz_noise: %d failure(s)\n", GFailures);
    return 1;
}
