// FemboyEngine2D - Noise XX session channel implementation.
#include "Engine/Network/SecureChannel.hpp"

#include "Engine/Assets/FemCrypto.hpp"
#include "Engine/Core/CryptoKdf.hpp"
#include "Engine/Core/SecureRng.hpp"
#include "Engine/Network/ByteOrder.hpp"

#include <cstring>

namespace Fby::Net
{
namespace
{
// The v1 transcript is FBY-shaped and not interoperable. The v2 transcript uses
// the standard Noise XX name and an empty default prologue; the record layer
// follows the Noise transport rules, so the record path below has two branches.
constexpr char kFbyProtocolName[] = "Fby_XX_25519_ChaChaPoly_SHA256";
constexpr char kNoiseProtocolName[] = "Noise_XX_25519_ChaChaPoly_SHA256";
constexpr char kPrologueName[] = "FemboyEngine2D-secure-channel-v1";
// Frozen v1 record rekey label; v2 uses the Noise REKEY() chain instead.
constexpr char kRekeyLabel[] = "FBY-REKEY";

constexpr std::uint8_t kTypeMessage1 = 1;
constexpr std::uint8_t kTypeMessage2 = 2;
constexpr std::uint8_t kTypeMessage3 = 3;
constexpr std::uint8_t kTypeCookieChallenge = 4;
constexpr std::size_t kCookieChallengeBytes =
    kSecureHandshakeHeaderBytes + kSecureCookieNonceBytes + kSecureCookieBytes;
constexpr std::uint8_t kFlagCookie = 1u << 0;
constexpr std::size_t kMessage1BaseBytes = kSecureHandshakeHeaderBytes + Crypto::kX25519KeyBytes;
constexpr std::size_t kMessage1CookieBytes = kMessage1BaseBytes + kSecureCookieNonceBytes + kSecureCookieBytes;
constexpr std::size_t kMessage2Bytes = kSecureHandshakeHeaderBytes + Crypto::kX25519KeyBytes +
                                       (Crypto::kX25519KeyBytes + kSecureTagBytes) + kSecureTagBytes;
constexpr std::size_t kMessage3Bytes = kSecureHandshakeHeaderBytes + (Crypto::kX25519KeyBytes + kSecureTagBytes) +
                                       kSecureTagBytes;

[[nodiscard]] std::uint8_t* BytesOf(std::span<std::byte> InBytes) noexcept
{
    return reinterpret_cast<std::uint8_t*>(InBytes.data());
}

[[nodiscard]] const std::uint8_t* BytesOf(std::span<const std::byte> InBytes) noexcept
{
    return reinterpret_cast<const std::uint8_t*>(InBytes.data());
}

[[nodiscard]] std::array<std::byte, 12> NonceFromCounter(std::uint64_t InCounter) noexcept
{
    std::array<std::byte, 12> ScopedNonce{};
    Detail::StoreU64(BytesOf(std::span<std::byte>(ScopedNonce)), InCounter);
    return ScopedNonce;
}

// Standard Noise ChaChaPoly nonce: 32 zero bits followed by the 64-bit counter
// in little endian. v1 froze the counter in the first eight bytes instead, so
// only the v1 path keeps NonceFromCounter above.
[[nodiscard]] std::array<std::byte, 12> NoiseNonceFromCounter(std::uint64_t InCounter) noexcept
{
    std::array<std::byte, 12> ScopedNonce{};
    Detail::StoreU64(BytesOf(std::span<std::byte>(ScopedNonce)) + 4, InCounter);
    return ScopedNonce;
}

// h = HASH(h || data), the Noise handshake-hash update.
void MixHashInto(std::array<std::byte, Crypto::kSha256Bytes>& InHash,
                 std::span<const std::byte> InData) noexcept
{
    Crypto::Sha256Context ScopedContext;
    Crypto::Sha256Init(&ScopedContext);
    Crypto::Sha256Update(&ScopedContext, InHash);
    Crypto::Sha256Update(&ScopedContext, InData);
    Crypto::Sha256Final(&ScopedContext, InHash);
}

[[nodiscard]] std::array<std::byte, Crypto::kSha256Bytes> HashOf(std::span<const std::byte> InData) noexcept
{
    std::array<std::byte, Crypto::kSha256Bytes> ScopedDigest{};
    Crypto::Sha256(InData, ScopedDigest);
    return ScopedDigest;
}

// AEAD helpers. Handshake and record frames keep the tag next to the ciphertext.
void SealWithTag(std::span<const std::byte> InKey, std::span<const std::byte> InNonce,
                 std::span<const std::byte> InAad, std::span<const std::byte> InPlaintext,
                 std::span<std::byte> OutCipherAndTag) noexcept
{
    Crypto::AeadKey ScopedKey;
    std::memcpy(ScopedKey.bytes.data(), InKey.data(), ScopedKey.bytes.size());
    Crypto::AeadNonce ScopedNonce;
    std::memcpy(ScopedNonce.bytes.data(), InNonce.data(), ScopedNonce.bytes.size());

    Crypto::AeadTag ScopedTag{};
    (void)Crypto::ChaCha20Poly1305Encrypt(ScopedKey, ScopedNonce, InAad, InPlaintext,
                                    OutCipherAndTag.first(InPlaintext.size()), ScopedTag);
    std::memcpy(OutCipherAndTag.data() + InPlaintext.size(), ScopedTag.bytes.data(), ScopedTag.bytes.size());
}

[[nodiscard]] bool OpenWithTag(std::span<const std::byte> InKey, std::span<const std::byte> InNonce,
                               std::span<const std::byte> InAad, std::span<const std::byte> InCipherAndTag,
                               std::span<std::byte> OutPlaintext) noexcept
{
    if (InCipherAndTag.size() < kSecureTagBytes)
    {
        return false;
    }
    const std::size_t ScopedCipherBytes = InCipherAndTag.size() - kSecureTagBytes;
    if (OutPlaintext.size() != ScopedCipherBytes)
    {
        return false;
    }

    Crypto::AeadKey ScopedKey;
    std::memcpy(ScopedKey.bytes.data(), InKey.data(), ScopedKey.bytes.size());
    Crypto::AeadNonce ScopedNonce;
    std::memcpy(ScopedNonce.bytes.data(), InNonce.data(), ScopedNonce.bytes.size());
    Crypto::AeadTag ScopedTag{};
    std::memcpy(ScopedTag.bytes.data(), InCipherAndTag.data() + ScopedCipherBytes, ScopedTag.bytes.size());

    return Crypto::ChaCha20Poly1305Decrypt(ScopedKey, ScopedNonce, InAad, InCipherAndTag.first(ScopedCipherBytes),
                                           OutPlaintext, ScopedTag);
}

// Standard Noise DH() is the raw RFC 7748 output; the v1 transcript keeps the
// BLAKE2b post-hash it was frozen with.
[[nodiscard]] bool DhShared(bool InV2, std::span<const std::byte> InSecret,
                            std::span<const std::byte> InPeerPublic, std::span<std::byte> OutShared) noexcept
{
    return InV2 ? Crypto::X25519RawSharedSecret(InSecret, InPeerPublic, OutShared)
                : Crypto::X25519SharedSecret(InSecret, InPeerPublic, OutShared);
}
}

std::array<std::byte, Crypto::kSha256Bytes> NoiseRekey(std::span<const std::byte> InKey) noexcept
{
    std::array<std::byte, Crypto::kSha256Bytes> ScopedResult{};
    if (InKey.size() != Crypto::kSha256Bytes)
    {
        return ScopedResult;
    }

    Crypto::AeadKey ScopedKey;
    std::memcpy(ScopedKey.bytes.data(), InKey.data(), ScopedKey.bytes.size());
    Crypto::AeadNonce ScopedNonce;
    ScopedNonce.SetCounter(kSecureMaxTransportSequence);

    // The Noise REKEY() rule encrypts 32 zero bytes under the maximum nonce
    // with empty associated data and keeps the first 32 bytes of ciphertext.
    std::array<std::byte, Crypto::kSha256Bytes> ScopedZeros{};
    Crypto::AeadTag ScopedTag{};
    (void)Crypto::ChaCha20Poly1305Encrypt(ScopedKey, ScopedNonce, {}, ScopedZeros, ScopedResult, ScopedTag);
    return ScopedResult;
}

void SecureCookieIssuer::Reset() noexcept
{
    MSecret.fill(std::byte{0});
    MReady = SecureRng::Fill(MSecret);
}

void SecureCookieIssuer::Issue(std::uint64_t InAddressTag, std::span<const std::byte> InNonce,
                               std::span<std::byte> OutCookie) const noexcept
{
    if (!MReady || InNonce.size() != kSecureCookieNonceBytes || OutCookie.size() != kSecureCookieBytes)
    {
        if (!OutCookie.empty())
        {
            std::memset(OutCookie.data(), 0, OutCookie.size());
        }
        return;
    }

    std::array<std::byte, 8 + kSecureCookieNonceBytes> ScopedInput{};
    Detail::StoreU64(BytesOf(std::span<std::byte>(ScopedInput)), InAddressTag);
    std::memcpy(ScopedInput.data() + 8, InNonce.data(), InNonce.size());

    std::array<std::byte, Crypto::kSha256Bytes> ScopedMac{};
    Crypto::HmacSha256(MSecret, ScopedInput, ScopedMac);
    std::memcpy(OutCookie.data(), ScopedMac.data(), OutCookie.size());
}

bool SecureCookieIssuer::Validate(std::uint64_t InAddressTag, std::span<const std::byte> InNonce,
                                  std::span<const std::byte> InCookie) const noexcept
{
    if (!MReady || InCookie.size() != kSecureCookieBytes || InNonce.size() != kSecureCookieNonceBytes)
    {
        return false;
    }
    std::array<std::byte, kSecureCookieBytes> ScopedExpected{};
    Issue(InAddressTag, InNonce, ScopedExpected);
    return Crypto::ConstantTimeEquals(ScopedExpected, InCookie);
}

SecureStatus SecureChannel::Start(const SecureChannelConfig& InConfig) noexcept
{
    if (MPhase == Phase::Established)
    {
        return SecureStatus::AlreadyEstablished;
    }
    Reset();

    if (InConfig.RecordsPerKey == 0 || InConfig.PrologueBytes > kSecureMaxPrologueBytes)
    {
        return SecureStatus::InvalidConfig;
    }

    // The caller's static key must be self-consistent before it authenticates
    // anything.
    Crypto::X25519KeyPair ScopedDerived{};
    if (!Crypto::X25519KeyPairFromSeed(std::span<const std::byte>(InConfig.LocalStatic.Secret), &ScopedDerived) ||
        ScopedDerived.Public != InConfig.LocalStatic.Public)
    {
        return SecureStatus::InvalidConfig;
    }
    if (InConfig.RequireExpectedRemote)
    {
        std::uint8_t ScopedAccumulator = 0;
        for (const std::byte ScopedByte : InConfig.ExpectedRemoteStatic)
        {
            ScopedAccumulator |= static_cast<std::uint8_t>(ScopedByte);
        }
        if (ScopedAccumulator == 0)
        {
            return SecureStatus::InvalidConfig;
        }
    }

    std::array<std::byte, Crypto::kX25519KeyBytes> ScopedEphemeralSeed{};
    if (InConfig.HasFixedEphemeral)
    {
        ScopedEphemeralSeed = InConfig.FixedEphemeralSeed;
    }
    else if (!SecureRng::Fill(ScopedEphemeralSeed))
    {
        return SecureStatus::NoRandomness;
    }
    const bool ScopedEphemeralOk = Crypto::X25519KeyPairFromSeed(ScopedEphemeralSeed, &MLocalEphemeral);
    Crypto::WipeBytes(ScopedEphemeralSeed);
    if (!ScopedEphemeralOk)
    {
        return SecureStatus::NoRandomness;
    }

    MConfig = InConfig;
    MRecordsPerKey = InConfig.RecordsPerKey;
    MProtocolV2 = InConfig.ProtocolV2;
    MVersion = MProtocolV2 ? kSecureVersionV2 : kSecureVersionV1;

    // v2: h = HASH(standard Noise name), then only the optional ticket binding.
    // v1: the frozen FBY name, fixed prologue string and session id.
    const char* ScopedProtocolName = MProtocolV2 ? kNoiseProtocolName : kFbyProtocolName;
    const std::span<const std::byte> ScopedProtocol(
        reinterpret_cast<const std::byte*>(ScopedProtocolName), std::strlen(ScopedProtocolName));
    if (MProtocolV2 && ScopedProtocol.size() <= Crypto::kSha256Bytes)
    {
        // Standard Noise: a protocol name no longer than the hash is the initial
        // hash itself, zero padded. The XX name is exactly 32 bytes, so hashing
        // it would break interoperability. v1 keeps the frozen hashed form.
        MHandshakeHash.fill(std::byte{0});
        std::memcpy(MHandshakeHash.data(), ScopedProtocol.data(), ScopedProtocol.size());
    }
    else
    {
        MHandshakeHash = HashOf(ScopedProtocol);
    }

    // Noise keeps the chaining key at the initial hash while the prologue only
    // updates h, so ck must not absorb the prologue. The frozen v1 transcript
    // mixed the prologue into ck, and its KATs depend on that.
    const std::array<std::byte, Crypto::kSha256Bytes> ScopedInitialChainKey = MHandshakeHash;

    if (!MProtocolV2)
    {
        std::array<std::byte, sizeof(kPrologueName) - 1 + 8> ScopedPrologue{};
        std::memcpy(ScopedPrologue.data(), kPrologueName, sizeof(kPrologueName) - 1);
        const std::array<std::byte, 8> ScopedSession = SessionIdBytes();
        std::memcpy(ScopedPrologue.data() + sizeof(kPrologueName) - 1, ScopedSession.data(), ScopedSession.size());
        MixHash(ScopedPrologue);
    }
    // The secure session transport binds the signed ticket hash here; a client
    // that presents one ticket in AUTH and another in the handshake fails.
    // Conformance vectors pass their prologue bytes the same way.
    if (InConfig.PrologueBytes > 0)
    {
        MixHash(std::span<const std::byte>(InConfig.Prologue.data(), InConfig.PrologueBytes));
    }

    MInitialHandshakeHash = MHandshakeHash;
    MInitialChainKey = MProtocolV2 ? ScopedInitialChainKey : MHandshakeHash;
    MChainKey = MInitialChainKey;
    MPhase = InConfig.Initiator ? Phase::InitSend1 : Phase::RespRecv1;
    return SecureStatus::Ok;
}

void SecureChannel::Reset() noexcept
{
    MConfig = SecureChannelConfig{};
    MPhase = Phase::Idle;
    MVersion = kSecureVersionV1;
    MProtocolV2 = false;
    MChainKey.fill(std::byte{0});
    MHandshakeHash.fill(std::byte{0});
    MInitialHandshakeHash.fill(std::byte{0});
    MInitialChainKey.fill(std::byte{0});
    MCipherKey.fill(std::byte{0});
    MHandshakeNonce = 0;
    MLocalEphemeral = Crypto::X25519KeyPair{};
    MRemoteEphemeral.fill(std::byte{0});
    MRemoteStatic.fill(std::byte{0});
    MSendKey.fill(std::byte{0});
    MRecvKey.fill(std::byte{0});
    MPrevRecvKey.fill(std::byte{0});
    MSendSequence = 0;
    MSendEpoch = 0;
    MRecvEpoch = 0;
    MRecordsPerKey = kSecureDefaultRecordsPerKey;
    MRecvStarted = false;
    MRecvHighest = 0;
    MRecvBits = 0;
}

void SecureChannel::MixHash(std::span<const std::byte> InData) noexcept
{
    MixHashInto(MHandshakeHash, InData);
}

void SecureChannel::MixKey(std::span<const std::byte> InKeyMaterial) noexcept
{
    std::array<std::byte, Crypto::kSha256Bytes * 2> ScopedOutput{};
    Crypto::HkdfSha256(InKeyMaterial, MChainKey, {}, ScopedOutput);
    std::memcpy(MChainKey.data(), ScopedOutput.data(), Crypto::kSha256Bytes);
    std::memcpy(MCipherKey.data(), ScopedOutput.data() + Crypto::kSha256Bytes, Crypto::kSha256Bytes);
    MHandshakeNonce = 0;
}

void SecureChannel::EncryptAndHash(std::span<const std::byte> InPlaintext, std::span<std::byte> OutCiphertext,
                                   std::size_t* OutSize) noexcept
{
    const std::array<std::byte, 12> ScopedNonce =
        MProtocolV2 ? NoiseNonceFromCounter(MHandshakeNonce) : NonceFromCounter(MHandshakeNonce);
    SealWithTag(MCipherKey, ScopedNonce, MHandshakeHash, InPlaintext,
                OutCiphertext.first(InPlaintext.size() + kSecureTagBytes));
    ++MHandshakeNonce;
    MixHash(OutCiphertext.first(InPlaintext.size() + kSecureTagBytes));
    if (OutSize != nullptr)
    {
        *OutSize = InPlaintext.size() + kSecureTagBytes;
    }
}

SecureStatus SecureChannel::DecryptAndHash(std::span<const std::byte> InCiphertext,
                                           std::span<std::byte> OutPlaintext, std::size_t* OutSize) noexcept
{
    if (InCiphertext.size() < kSecureTagBytes)
    {
        return SecureStatus::BadSize;
    }
    const std::size_t ScopedPlainBytes = InCiphertext.size() - kSecureTagBytes;
    if (OutPlaintext.size() < ScopedPlainBytes)
    {
        return SecureStatus::BadSize;
    }

    const std::array<std::byte, 12> ScopedNonce =
        MProtocolV2 ? NoiseNonceFromCounter(MHandshakeNonce) : NonceFromCounter(MHandshakeNonce);
    if (!OpenWithTag(MCipherKey, ScopedNonce, MHandshakeHash, InCiphertext, OutPlaintext.first(ScopedPlainBytes)))
    {
        return SecureStatus::BadTag;
    }
    ++MHandshakeNonce;
    MixHash(InCiphertext);
    if (OutSize != nullptr)
    {
        *OutSize = ScopedPlainBytes;
    }
    return SecureStatus::Ok;
}

void SecureChannel::Split() noexcept
{
    std::array<std::byte, Crypto::kSha256Bytes * 2> ScopedOutput{};
    Crypto::HkdfSha256({}, MChainKey, {}, ScopedOutput);
    const std::span<const std::byte> ScopedFirst = std::span<const std::byte>(ScopedOutput).first(Crypto::kSha256Bytes);
    const std::span<const std::byte> ScopedSecond =
        std::span<const std::byte>(ScopedOutput).subspan(Crypto::kSha256Bytes, Crypto::kSha256Bytes);
    const std::span<const std::byte> ScopedSend = MConfig.Initiator ? ScopedFirst : ScopedSecond;
    const std::span<const std::byte> ScopedRecv = MConfig.Initiator ? ScopedSecond : ScopedFirst;
    std::memcpy(MSendKey.data(), ScopedSend.data(), Crypto::kSha256Bytes);
    std::memcpy(MRecvKey.data(), ScopedRecv.data(), Crypto::kSha256Bytes);
    MPrevRecvKey.fill(std::byte{0});
}

#if defined(FBY_ENGINE_TEST_HOOKS)
void SecureChannel::ExportTransportKeys(std::span<std::byte> OutSendKey,
                                        std::span<std::byte> OutReceiveKey) const noexcept
{
    if (OutSendKey.size() >= MSendKey.size())
    {
        std::memcpy(OutSendKey.data(), MSendKey.data(), MSendKey.size());
    }
    if (OutReceiveKey.size() >= MRecvKey.size())
    {
        std::memcpy(OutReceiveKey.data(), MRecvKey.data(), MRecvKey.size());
    }
}
#endif

SecureStatus SecureChannel::WriteHandshake(std::span<std::byte> OutMessage, std::size_t* OutSize,
                                           std::span<const std::byte> InPayload) noexcept
{
    if (OutSize != nullptr)
    {
        *OutSize = 0;
    }
    if (InPayload.size() > kSecureMaxHandshakePayloadBytes)
    {
        return SecureStatus::BadSize;
    }

    switch (MPhase)
    {
        case Phase::InitSend1:
        {
            const bool ScopedCookie = MConfig.HasCookie;
            const std::size_t ScopedBytes =
                (ScopedCookie ? kMessage1CookieBytes : kMessage1BaseBytes) + InPayload.size();
            if (OutMessage.size() < ScopedBytes)
            {
                return SecureStatus::BadSize;
            }

            std::memset(OutMessage.data(), 0, ScopedBytes);
            std::uint8_t* ScopedOut = BytesOf(OutMessage);
            Detail::StoreU32(ScopedOut, kSecureMagic);
            Detail::StoreU16(ScopedOut + 4, MVersion);
            ScopedOut[6] = kTypeMessage1;
            ScopedOut[7] = ScopedCookie ? kFlagCookie : 0;
            std::size_t ScopedOffset = kSecureHandshakeHeaderBytes;
            if (ScopedCookie)
            {
                std::memcpy(ScopedOut + ScopedOffset, MConfig.CookieNonce.data(), kSecureCookieNonceBytes);
                ScopedOffset += kSecureCookieNonceBytes;
                std::memcpy(ScopedOut + ScopedOffset, MConfig.Cookie.data(), kSecureCookieBytes);
                ScopedOffset += kSecureCookieBytes;
            }
            std::memcpy(ScopedOut + ScopedOffset, MLocalEphemeral.Public.data(), Crypto::kX25519KeyBytes);
            ScopedOffset += Crypto::kX25519KeyBytes;
            MixHash(std::span<const std::byte>(MLocalEphemeral.Public));

            // Message 1 has no cipher key yet, so its payload travels in the
            // clear and is absorbed into the transcript after the ephemeral key.
            // Mixing an empty payload would hash the transcript a second time,
            // so the empty case is skipped.
            if (!InPayload.empty())
            {
                std::memcpy(ScopedOut + ScopedOffset, InPayload.data(), InPayload.size());
                MixHash(InPayload);
            }

            MPhase = Phase::InitRecv2;
            if (OutSize != nullptr)
            {
                *OutSize = ScopedBytes;
            }
            return SecureStatus::Ok;
        }
        case Phase::InitSend3:
        {
            const std::size_t ScopedBytes = kMessage3Bytes + InPayload.size();
            if (OutMessage.size() < ScopedBytes)
            {
                return SecureStatus::BadSize;
            }

            std::memset(OutMessage.data(), 0, ScopedBytes);
            std::uint8_t* ScopedOut = BytesOf(OutMessage);
            Detail::StoreU32(ScopedOut, kSecureMagic);
            Detail::StoreU16(ScopedOut + 4, MVersion);
            ScopedOut[6] = kTypeMessage3;
            ScopedOut[7] = 0;

            std::size_t ScopedOffset = kSecureHandshakeHeaderBytes;
            std::size_t ScopedWritten = 0;
            EncryptAndHash(std::span<const std::byte>(MConfig.LocalStatic.Public),
                           std::span<std::byte>(OutMessage.data() + ScopedOffset, ScopedBytes - ScopedOffset),
                           &ScopedWritten);
            ScopedOffset += ScopedWritten;

            std::array<std::byte, Crypto::kX25519KeyBytes> ScopedShared{};
            if (!DhShared(MProtocolV2, std::span<const std::byte>(MConfig.LocalStatic.Secret), MRemoteEphemeral,
                                            ScopedShared))
            {
                return SecureStatus::BadKey;
            }
            MixKey(ScopedShared);

            EncryptAndHash(InPayload, std::span<std::byte>(OutMessage.data() + ScopedOffset, ScopedBytes - ScopedOffset),
                           &ScopedWritten);
            ScopedOffset += ScopedWritten;

            Split();
            MPhase = Phase::Established;
            if (OutSize != nullptr)
            {
                *OutSize = ScopedOffset;
            }
            return SecureStatus::Ok;
        }
        case Phase::RespSend2:
        {
            const std::size_t ScopedBytes = kMessage2Bytes + InPayload.size();
            if (OutMessage.size() < ScopedBytes)
            {
                return SecureStatus::BadSize;
            }

            std::memset(OutMessage.data(), 0, ScopedBytes);
            std::uint8_t* ScopedOut = BytesOf(OutMessage);
            Detail::StoreU32(ScopedOut, kSecureMagic);
            Detail::StoreU16(ScopedOut + 4, MVersion);
            ScopedOut[6] = kTypeMessage2;
            ScopedOut[7] = 0;

            std::size_t ScopedOffset = kSecureHandshakeHeaderBytes;
            std::memcpy(ScopedOut + ScopedOffset, MLocalEphemeral.Public.data(), Crypto::kX25519KeyBytes);
            ScopedOffset += Crypto::kX25519KeyBytes;
            MixHash(std::span<const std::byte>(MLocalEphemeral.Public));

            std::array<std::byte, Crypto::kX25519KeyBytes> ScopedShared{};
            if (!DhShared(MProtocolV2, std::span<const std::byte>(MLocalEphemeral.Secret), MRemoteEphemeral,
                                            ScopedShared))
            {
                return SecureStatus::BadKey;
            }
            MixKey(ScopedShared);

            std::size_t ScopedWritten = 0;
            EncryptAndHash(std::span<const std::byte>(MConfig.LocalStatic.Public),
                           std::span<std::byte>(OutMessage.data() + ScopedOffset, ScopedBytes - ScopedOffset),
                           &ScopedWritten);
            ScopedOffset += ScopedWritten;

            // es: the responder static key with the initiator ephemeral.
            if (!DhShared(MProtocolV2, std::span<const std::byte>(MConfig.LocalStatic.Secret), MRemoteEphemeral,
                                            ScopedShared))
            {
                return SecureStatus::BadKey;
            }
            MixKey(ScopedShared);

            EncryptAndHash(InPayload, std::span<std::byte>(OutMessage.data() + ScopedOffset, ScopedBytes - ScopedOffset),
                           &ScopedWritten);
            ScopedOffset += ScopedWritten;

            MPhase = Phase::RespRecv3;
            if (OutSize != nullptr)
            {
                *OutSize = ScopedOffset;
            }
            return SecureStatus::Ok;
        }
        case Phase::Established:
            return SecureStatus::AlreadyEstablished;
        default:
            return SecureStatus::WrongTurn;
    }
}

SecureStatus SecureChannel::WriteCookieChallenge(std::span<std::byte> OutMessage,
                                                  std::size_t* OutSize) noexcept
{
    if (MConfig.CookieIssuer == nullptr || !MConfig.CookieIssuer->Ready())
    {
        return SecureStatus::InvalidConfig;
    }
    if (OutMessage.size() < kCookieChallengeBytes)
    {
        return SecureStatus::BadSize;
    }
    std::uint8_t* ScopedOut = BytesOf(OutMessage);
    Detail::StoreU32(ScopedOut, kSecureMagic);
    Detail::StoreU16(ScopedOut + 4, MVersion);
    ScopedOut[6] = kTypeCookieChallenge;
    ScopedOut[7] = 0;
    std::array<std::byte, kSecureCookieNonceBytes> ScopedNonce{};
    if (!SecureRng::Fill(ScopedNonce))
    {
        return SecureStatus::NoRandomness;
    }
    std::array<std::byte, kSecureCookieBytes> ScopedCookie{};
    MConfig.CookieIssuer->Issue(MConfig.AddressTag, ScopedNonce, ScopedCookie);
    std::memcpy(ScopedOut + kSecureHandshakeHeaderBytes, ScopedNonce.data(), kSecureCookieNonceBytes);
    std::memcpy(ScopedOut + kSecureHandshakeHeaderBytes + kSecureCookieNonceBytes, ScopedCookie.data(),
                kSecureCookieBytes);
    if (OutSize != nullptr)
    {
        *OutSize = kCookieChallengeBytes;
    }
    return SecureStatus::Ok;
}

SecureStatus SecureChannel::ReadHandshake(std::span<const std::byte> InMessage, std::span<std::byte> OutPayload,
                                          std::size_t* OutPayloadSize) noexcept
{
    if (OutPayloadSize != nullptr)
    {
        *OutPayloadSize = 0;
    }
    if (MPhase == Phase::Established)
    {
        return SecureStatus::AlreadyEstablished;
    }
    if (MPhase == Phase::Idle || MPhase == Phase::Failed)
    {
        return SecureStatus::NotStarted;
    }
    if (InMessage.size() < kSecureHandshakeHeaderBytes)
    {
        return SecureStatus::BadSize;
    }

    const std::uint8_t* ScopedIn = BytesOf(InMessage);
    if (Detail::LoadU32(ScopedIn) != kSecureMagic)
    {
        return SecureStatus::BadMagic;
    }
    if (Detail::LoadU16(ScopedIn + 4) != MVersion)
    {
        return SecureStatus::BadVersion;
    }
    const std::uint8_t ScopedType = ScopedIn[6];
    const std::uint8_t ScopedFlags = ScopedIn[7];

    if (ScopedType == kTypeMessage1 && MPhase == Phase::RespRecv1)
    {
        std::size_t ScopedOffset = kSecureHandshakeHeaderBytes;
        const bool ScopedCookie = (ScopedFlags & kFlagCookie) != 0;
        if (ScopedCookie)
        {
            if (InMessage.size() < kMessage1CookieBytes || MConfig.CookieIssuer == nullptr ||
                !MConfig.CookieIssuer->Ready())
            {
                return SecureStatus::BadCookie;
            }
            const std::span<const std::byte> ScopedNonce = InMessage.subspan(ScopedOffset, kSecureCookieNonceBytes);
            ScopedOffset += kSecureCookieNonceBytes;
            const std::span<const std::byte> ScopedCookieBytes = InMessage.subspan(ScopedOffset, kSecureCookieBytes);
            ScopedOffset += kSecureCookieBytes;
            if (!MConfig.CookieIssuer->Validate(MConfig.AddressTag, ScopedNonce, ScopedCookieBytes))
            {
                return SecureStatus::BadCookie;
            }
        }
        else if (MConfig.CookieIssuer != nullptr)
        {
            // The endpoint answers this status with a cookie challenge.
            return SecureStatus::CookieRequired;
        }

        const std::size_t ScopedBaseBytes = ScopedCookie ? kMessage1CookieBytes : kMessage1BaseBytes;
        if (InMessage.size() < ScopedBaseBytes)
        {
            return SecureStatus::BadSize;
        }
        const std::size_t ScopedPayloadBytes = InMessage.size() - ScopedBaseBytes;
        if (ScopedPayloadBytes > kSecureMaxHandshakePayloadBytes || OutPayload.size() < ScopedPayloadBytes)
        {
            return SecureStatus::BadSize;
        }

        std::memcpy(MRemoteEphemeral.data(), ScopedIn + ScopedOffset, Crypto::kX25519KeyBytes);
        MixHash(MRemoteEphemeral);

        // Message 1 has no cipher key, so the payload is cleartext and joins the
        // transcript after the ephemeral key. Hashed only when non-empty.
        if (ScopedPayloadBytes > 0)
        {
            std::memcpy(OutPayload.data(), ScopedIn + ScopedOffset + Crypto::kX25519KeyBytes, ScopedPayloadBytes);
            MixHash(std::span<const std::byte>(OutPayload.data(), ScopedPayloadBytes));
        }
        if (OutPayloadSize != nullptr)
        {
            *OutPayloadSize = ScopedPayloadBytes;
        }
        MPhase = Phase::RespSend2;
        return SecureStatus::Ok;
    }

    if (ScopedType == kTypeCookieChallenge && MPhase == Phase::InitRecv2)
    {
        if (InMessage.size() != kCookieChallengeBytes)
        {
            return SecureStatus::BadSize;
        }
        const std::uint8_t* ScopedChallenge = ScopedIn + kSecureHandshakeHeaderBytes;
        std::memcpy(MConfig.CookieNonce.data(), ScopedChallenge, kSecureCookieNonceBytes);
        std::memcpy(MConfig.Cookie.data(), ScopedChallenge + kSecureCookieNonceBytes, kSecureCookieBytes);
        MConfig.HasCookie = true;
        // The first message 1 never reached the responder's hash state (the
        // cookie check runs first), so rewind the hash and resend it.
        MHandshakeHash = MInitialHandshakeHash;
        MChainKey = MInitialChainKey;
        MPhase = Phase::InitSend1;
        return SecureStatus::Ok;
    }

    if (ScopedType == kTypeMessage2 && MPhase == Phase::InitRecv2)
    {
        const std::size_t ScopedFixedBytes = kSecureHandshakeHeaderBytes + Crypto::kX25519KeyBytes +
                                             (Crypto::kX25519KeyBytes + kSecureTagBytes);
        if (InMessage.size() < ScopedFixedBytes + kSecureTagBytes)
        {
            return SecureStatus::BadSize;
        }
        const std::size_t ScopedPayloadBytes = InMessage.size() - ScopedFixedBytes - kSecureTagBytes;
        if (ScopedPayloadBytes > kSecureMaxHandshakePayloadBytes || OutPayload.size() < ScopedPayloadBytes)
        {
            return SecureStatus::BadSize;
        }

        std::size_t ScopedOffset = kSecureHandshakeHeaderBytes;
        std::memcpy(MRemoteEphemeral.data(), ScopedIn + ScopedOffset, Crypto::kX25519KeyBytes);
        ScopedOffset += Crypto::kX25519KeyBytes;
        MixHash(MRemoteEphemeral);

        std::array<std::byte, Crypto::kX25519KeyBytes> ScopedShared{};
        if (!DhShared(MProtocolV2, std::span<const std::byte>(MLocalEphemeral.Secret), MRemoteEphemeral,
                                        ScopedShared))
        {
            return SecureStatus::BadKey;
        }
        MixKey(ScopedShared);

        std::array<std::byte, Crypto::kX25519KeyBytes> ScopedRemoteStatic{};
        std::size_t ScopedWritten = 0;
        SecureStatus ScopedStatus = DecryptAndHash(
            InMessage.subspan(ScopedOffset, Crypto::kX25519KeyBytes + kSecureTagBytes), ScopedRemoteStatic,
            &ScopedWritten);
        if (ScopedStatus != SecureStatus::Ok || ScopedWritten != Crypto::kX25519KeyBytes)
        {
            return SecureStatus::BadTag;
        }
        ScopedOffset += Crypto::kX25519KeyBytes + kSecureTagBytes;
        if (MConfig.RequireExpectedRemote && MConfig.ExpectedRemoteStatic != ScopedRemoteStatic)
        {
            return SecureStatus::BadKey;
        }
        MRemoteStatic = ScopedRemoteStatic;

        // es: initiator ephemeral with responder static.
        if (!DhShared(MProtocolV2, std::span<const std::byte>(MLocalEphemeral.Secret), MRemoteStatic,
                                        ScopedShared))
        {
            return SecureStatus::BadKey;
        }
        MixKey(ScopedShared);

        ScopedStatus = DecryptAndHash(InMessage.subspan(ScopedOffset, ScopedPayloadBytes + kSecureTagBytes),
                                      std::span<std::byte>(OutPayload.data(), ScopedPayloadBytes), &ScopedWritten);
        if (ScopedStatus != SecureStatus::Ok || ScopedWritten != ScopedPayloadBytes)
        {
            return SecureStatus::BadTag;
        }
        if (OutPayloadSize != nullptr)
        {
            *OutPayloadSize = ScopedPayloadBytes;
        }

        MPhase = Phase::InitSend3;
        return SecureStatus::Ok;
    }

    if (ScopedType == kTypeMessage3 && MPhase == Phase::RespRecv3)
    {
        const std::size_t ScopedFixedBytes =
            kSecureHandshakeHeaderBytes + (Crypto::kX25519KeyBytes + kSecureTagBytes);
        if (InMessage.size() < ScopedFixedBytes + kSecureTagBytes)
        {
            return SecureStatus::BadSize;
        }
        const std::size_t ScopedPayloadBytes = InMessage.size() - ScopedFixedBytes - kSecureTagBytes;
        if (ScopedPayloadBytes > kSecureMaxHandshakePayloadBytes || OutPayload.size() < ScopedPayloadBytes)
        {
            return SecureStatus::BadSize;
        }

        std::size_t ScopedOffset = kSecureHandshakeHeaderBytes;
        std::array<std::byte, Crypto::kX25519KeyBytes> ScopedRemoteStatic{};
        std::size_t ScopedWritten = 0;
        SecureStatus ScopedStatus = DecryptAndHash(
            InMessage.subspan(ScopedOffset, Crypto::kX25519KeyBytes + kSecureTagBytes), ScopedRemoteStatic,
            &ScopedWritten);
        if (ScopedStatus != SecureStatus::Ok || ScopedWritten != Crypto::kX25519KeyBytes)
        {
            return SecureStatus::BadTag;
        }
        ScopedOffset += Crypto::kX25519KeyBytes + kSecureTagBytes;
        if (MConfig.RequireExpectedRemote && MConfig.ExpectedRemoteStatic != ScopedRemoteStatic)
        {
            return SecureStatus::BadKey;
        }
        MRemoteStatic = ScopedRemoteStatic;

        // se: initiator static with responder ephemeral.
        std::array<std::byte, Crypto::kX25519KeyBytes> ScopedShared{};
        if (!DhShared(MProtocolV2, std::span<const std::byte>(MLocalEphemeral.Secret), MRemoteStatic,
                                        ScopedShared))
        {
            return SecureStatus::BadKey;
        }
        MixKey(ScopedShared);

        ScopedStatus = DecryptAndHash(InMessage.subspan(ScopedOffset, ScopedPayloadBytes + kSecureTagBytes),
                                      std::span<std::byte>(OutPayload.data(), ScopedPayloadBytes), &ScopedWritten);
        if (ScopedStatus != SecureStatus::Ok || ScopedWritten != ScopedPayloadBytes)
        {
            return SecureStatus::BadTag;
        }
        if (OutPayloadSize != nullptr)
        {
            *OutPayloadSize = ScopedPayloadBytes;
        }

        Split();
        MPhase = Phase::Established;
        return SecureStatus::Ok;
    }

    return SecureStatus::BadType;
}

std::array<std::byte, 8> SecureChannel::SessionIdBytes() const noexcept
{
    std::array<std::byte, 8> ScopedBytes{};
    Detail::StoreU64(BytesOf(std::span<std::byte>(ScopedBytes)), MConfig.SessionId);
    return ScopedBytes;
}

std::array<std::byte, 12> SecureChannel::RecordNonce(std::uint64_t InSequence) const noexcept
{
    return MProtocolV2 ? NoiseNonceFromCounter(InSequence) : NonceFromCounter(InSequence);
}

// v1 only: the frozen FBY-REKEY label chain, keyed by the sequence number.
SecureStatus SecureChannel::DeriveSendKey(std::uint64_t InSequence) noexcept
{
    const std::uint64_t ScopedTarget = InSequence / MRecordsPerKey;
    while (MSendEpoch < ScopedTarget)
    {
        const std::uint64_t ScopedNextEpoch = MSendEpoch + 1;
        std::array<std::byte, sizeof(kRekeyLabel) - 1 + 8> ScopedInput{};
        std::memcpy(ScopedInput.data(), kRekeyLabel, sizeof(kRekeyLabel) - 1);
        Detail::StoreU64(BytesOf(std::span<std::byte>(ScopedInput)) + sizeof(kRekeyLabel) - 1, ScopedNextEpoch);

        std::array<std::byte, Crypto::kSha256Bytes> ScopedNext{};
        Crypto::HkdfSha256(ScopedInput, MSendKey, {}, ScopedNext);
        MSendKey = ScopedNext;
        MSendEpoch = ScopedNextEpoch;
    }
    return SecureStatus::Ok;
}

SecureStatus SecureChannel::DeriveReceiveKey(std::uint64_t InSequence) noexcept
{
    const std::uint64_t ScopedTarget = InSequence / MRecordsPerKey;
    // A forged sequence must not force an unbounded key-derivation chain; the
    // v2 epoch path uses the same bound.
    if (ScopedTarget > MRecvEpoch && ScopedTarget - MRecvEpoch > kSecureMaxEpochJump)
    {
        return SecureStatus::Replayed;
    }
    while (MRecvEpoch < ScopedTarget)
    {
        const std::uint64_t ScopedNextEpoch = MRecvEpoch + 1;
        std::array<std::byte, sizeof(kRekeyLabel) - 1 + 8> ScopedInput{};
        std::memcpy(ScopedInput.data(), kRekeyLabel, sizeof(kRekeyLabel) - 1);
        Detail::StoreU64(BytesOf(std::span<std::byte>(ScopedInput)) + sizeof(kRekeyLabel) - 1, ScopedNextEpoch);

        std::array<std::byte, Crypto::kSha256Bytes> ScopedNext{};
        Crypto::HkdfSha256(ScopedInput, MRecvKey, {}, ScopedNext);
        MPrevRecvKey = MRecvKey;
        MRecvKey = ScopedNext;
        MRecvEpoch = ScopedNextEpoch;
    }
    return SecureStatus::Ok;
}

void SecureChannel::AdvanceSendEpoch(std::uint64_t InTargetEpoch) noexcept
{
    while (MSendEpoch < InTargetEpoch)
    {
        MSendKey = NoiseRekey(MSendKey);
        ++MSendEpoch;
    }
}

SecureStatus SecureChannel::SelectReceiveKey(std::uint64_t InEpoch, std::array<std::byte, Crypto::kSha256Bytes>& OutKey,
                                             std::array<std::byte, Crypto::kSha256Bytes>& OutPreviousKey,
                                             bool& OutAdvancesEpoch) const noexcept
{
    OutAdvancesEpoch = false;
    if (InEpoch > MRecvEpoch)
    {
        // A peer may rekey later than this side, but a forged header must not
        // force an unbounded chain of derivations.
        if (InEpoch - MRecvEpoch > kSecureMaxEpochJump)
        {
            return SecureStatus::Replayed;
        }
        std::array<std::byte, Crypto::kSha256Bytes> ScopedKey = MRecvKey;
        std::array<std::byte, Crypto::kSha256Bytes> ScopedPrevious = MRecvKey;
        for (std::uint64_t ScopedEpoch = MRecvEpoch; ScopedEpoch < InEpoch; ++ScopedEpoch)
        {
            ScopedPrevious = ScopedKey;
            ScopedKey = NoiseRekey(ScopedKey);
        }
        OutKey = ScopedKey;
        OutPreviousKey = ScopedPrevious;
        OutAdvancesEpoch = true;
        return SecureStatus::Ok;
    }
    if (InEpoch == MRecvEpoch)
    {
        OutKey = MRecvKey;
        return SecureStatus::Ok;
    }
    // One record from the previous epoch may still be in flight; the replay
    // window is wider than an epoch only when RecordsPerKey is small.
    if (MRecvEpoch > 0 && InEpoch + 1 == MRecvEpoch)
    {
        OutKey = MPrevRecvKey;
        return SecureStatus::Ok;
    }
    return SecureStatus::Replayed;
}

bool SecureChannel::WouldAccept(std::uint64_t InSequence) const noexcept
{
    if (!MRecvStarted)
    {
        return true;
    }
    if (InSequence > MRecvHighest)
    {
        return true;
    }
    const std::uint64_t ScopedDelta = MRecvHighest - InSequence;
    if (ScopedDelta >= kSecureReplayWindow)
    {
        return false;
    }
    return ((MRecvBits >> ScopedDelta) & 1u) == 0;
}

void SecureChannel::CommitReceive(std::uint64_t InSequence) noexcept
{
    if (!MRecvStarted)
    {
        MRecvStarted = true;
        MRecvHighest = InSequence;
        MRecvBits = 1;
        return;
    }
    if (InSequence > MRecvHighest)
    {
        const std::uint64_t ScopedShift = InSequence - MRecvHighest;
        MRecvBits = ScopedShift >= kSecureReplayWindow ? 0 : (MRecvBits << ScopedShift);
        MRecvBits |= 1;
        MRecvHighest = InSequence;
        return;
    }
    const std::uint64_t ScopedDelta = MRecvHighest - InSequence;
    MRecvBits |= (1ull << ScopedDelta);
}

SecureStatus SecureChannel::Seal(std::span<const std::byte> InPlaintext, std::span<std::byte> OutRecord,
                                 std::size_t* OutSize) noexcept
{
    if (OutSize != nullptr)
    {
        *OutSize = 0;
    }
    if (MPhase != Phase::Established)
    {
        return SecureStatus::NotEstablished;
    }
    if (InPlaintext.size() > kSecureMaxPlainBytes)
    {
        return SecureStatus::BadSize;
    }
    const std::size_t ScopedHeaderBytes = MProtocolV2 ? kSecureV2RecordHeaderBytes : kSecureRecordHeaderBytes;
    const std::size_t ScopedTotal = ScopedHeaderBytes + InPlaintext.size() + kSecureTagBytes;
    if (OutRecord.size() < ScopedTotal)
    {
        return SecureStatus::BadSize;
    }

    const std::uint64_t ScopedSequence = MSendSequence;
    if (MProtocolV2 && ScopedSequence >= kSecureMaxTransportSequence)
    {
        return SecureStatus::BadKey;
    }

    if (MProtocolV2)
    {
        // The epoch is the sender's rekey policy; the receiver reads it from the
        // header and never has to know RecordsPerKey.
        const std::uint64_t ScopedEpoch = ScopedSequence / MRecordsPerKey;
        AdvanceSendEpoch(ScopedEpoch);
        Detail::StoreU64(BytesOf(OutRecord), ScopedEpoch);
        Detail::StoreU64(BytesOf(OutRecord) + 8, ScopedSequence);
    }
    else
    {
        (void)DeriveSendKey(ScopedSequence);
        Detail::StoreU64(BytesOf(OutRecord), ScopedSequence);
    }

    // v2: the header is the associated data, so the epoch and the sequence are
    // authenticated and the session id is no longer part of the record.
    const std::array<std::byte, 8> ScopedSession = SessionIdBytes();
    const std::span<const std::byte> ScopedAad =
        MProtocolV2 ? std::span<const std::byte>(OutRecord.data(), kSecureV2RecordHeaderBytes)
                    : std::span<const std::byte>(ScopedSession);
    SealWithTag(MSendKey, RecordNonce(ScopedSequence), ScopedAad, InPlaintext,
                OutRecord.subspan(ScopedHeaderBytes, InPlaintext.size() + kSecureTagBytes));
    ++MSendSequence;
    if (OutSize != nullptr)
    {
        *OutSize = ScopedTotal;
    }
    return SecureStatus::Ok;
}

SecureStatus SecureChannel::Open(std::span<const std::byte> InRecord, std::span<std::byte> OutPlaintext,
                                 std::size_t* OutSize) noexcept
{
    if (OutSize != nullptr)
    {
        *OutSize = 0;
    }
    if (MPhase != Phase::Established)
    {
        return SecureStatus::NotEstablished;
    }
    const std::size_t ScopedHeaderBytes = MProtocolV2 ? kSecureV2RecordHeaderBytes : kSecureRecordHeaderBytes;
    if (InRecord.size() < ScopedHeaderBytes + kSecureTagBytes)
    {
        return SecureStatus::BadSize;
    }

    std::uint64_t ScopedEpoch = 0;
    std::uint64_t ScopedSequence = 0;
    if (MProtocolV2)
    {
        ScopedEpoch = Detail::LoadU64(BytesOf(InRecord));
        ScopedSequence = Detail::LoadU64(BytesOf(InRecord) + 8);
        if (ScopedSequence >= kSecureMaxTransportSequence)
        {
            return SecureStatus::BadKey;
        }
    }
    else
    {
        ScopedSequence = Detail::LoadU64(BytesOf(InRecord));
    }

    if (!WouldAccept(ScopedSequence) ||
        (MRecvStarted && ScopedSequence > MRecvHighest + kSecureMaxSequenceJump))
    {
        return SecureStatus::Replayed;
    }

    const std::size_t ScopedPlainBytes = InRecord.size() - ScopedHeaderBytes - kSecureTagBytes;
    if (ScopedPlainBytes > kSecureMaxPlainBytes || OutPlaintext.size() < ScopedPlainBytes)
    {
        return SecureStatus::BadSize;
    }

    const std::array<std::byte, 8> ScopedSession = SessionIdBytes();
    const std::span<const std::byte> ScopedAad =
        MProtocolV2 ? std::span<const std::byte>(InRecord.data(), kSecureV2RecordHeaderBytes)
                    : std::span<const std::byte>(ScopedSession);

    if (MProtocolV2)
    {
        // The key is selected from the authenticated epoch, but the receive
        // state is committed only after the tag succeeds, so a forged header
        // cannot push the peer ahead of the real stream.
        std::array<std::byte, Crypto::kSha256Bytes> ScopedKey{};
        std::array<std::byte, Crypto::kSha256Bytes> ScopedPreviousKey{};
        bool ScopedAdvancesEpoch = false;
        const SecureStatus ScopedKeyStatus =
            SelectReceiveKey(ScopedEpoch, ScopedKey, ScopedPreviousKey, ScopedAdvancesEpoch);
        if (ScopedKeyStatus != SecureStatus::Ok)
        {
            return ScopedKeyStatus;
        }
        if (!OpenWithTag(ScopedKey, RecordNonce(ScopedSequence), ScopedAad, InRecord.subspan(ScopedHeaderBytes),
                         OutPlaintext.first(ScopedPlainBytes)))
        {
            return SecureStatus::BadTag;
        }
        if (ScopedAdvancesEpoch)
        {
            MPrevRecvKey = ScopedPreviousKey;
            MRecvKey = ScopedKey;
            MRecvEpoch = ScopedEpoch;
        }
        CommitReceive(ScopedSequence);
        if (OutSize != nullptr)
        {
            *OutSize = ScopedPlainBytes;
        }
        return SecureStatus::Ok;
    }

    const SecureStatus ScopedDerived = DeriveReceiveKey(ScopedSequence);
    if (ScopedDerived != SecureStatus::Ok)
    {
        return ScopedDerived;
    }
    const std::uint64_t ScopedSequenceEpoch = ScopedSequence / MRecordsPerKey;
    if (ScopedSequenceEpoch + 1 < MRecvEpoch)
    {
        return SecureStatus::Replayed;
    }

    const std::span<const std::byte> ScopedKey =
        ScopedSequenceEpoch < MRecvEpoch ? std::span<const std::byte>(MPrevRecvKey) : std::span<const std::byte>(MRecvKey);

    if (!OpenWithTag(ScopedKey, RecordNonce(ScopedSequence), ScopedAad,
                     InRecord.subspan(ScopedHeaderBytes), OutPlaintext.first(ScopedPlainBytes)))
    {
        return SecureStatus::BadTag;
    }

    CommitReceive(ScopedSequence);
    if (OutSize != nullptr)
    {
        *OutSize = ScopedPlainBytes;
    }
    return SecureStatus::Ok;
}
}
