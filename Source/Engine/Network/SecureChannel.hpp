// FemboyEngine2D - Noise XX session channel with AEAD record framing.
#pragma once

#include "Engine/Core/CryptoAsymmetric.hpp"
#include "Engine/Core/Sha256.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Fby::Net
{

inline constexpr std::uint32_t kSecureMagic = 0x31435346u;
// v1 is the FBY transcript; v2 is the standard-Xx transcript name with the
// configured prologue (empty unless the caller binds one), the raw X25519
// shared secret and the Noise record layer. The v2 wire format passes the
// noise-c vector; it stays opt-in until the default flips and v1 is deprecated.
inline constexpr std::uint16_t kSecureVersionV1 = 1;
inline constexpr std::uint16_t kSecureVersionV2 = 2;
inline constexpr std::uint16_t kSecureVersion = kSecureVersionV1;
inline constexpr std::size_t kSecureHandshakeHeaderBytes = 8;
inline constexpr std::size_t kSecureTagBytes = 16;
// Maximum application prologue. The ticket binding uses 32 bytes of this.
inline constexpr std::size_t kSecureMaxPrologueBytes = 256;
// Handshake payloads are optional in Noise; the engine sends empty ones by
// default and the conformance vectors use short text payloads.
inline constexpr std::size_t kSecureMaxHandshakePayloadBytes = 512;
// Worst-case handshake datagram: the longest message is message 2, which carries
// the ephemeral key, the encrypted static key, a payload and two tags.
inline constexpr std::size_t kSecureHandshakeBytes = kSecureHandshakeHeaderBytes + Crypto::kX25519KeyBytes +
                                                     (Crypto::kX25519KeyBytes + kSecureTagBytes) +
                                                     kSecureMaxHandshakePayloadBytes + kSecureTagBytes;
// v1 keeps the frozen sequence-only header.
inline constexpr std::size_t kSecureRecordHeaderBytes = 8;
// v2 carries the explicit key epoch before the message counter. Both fields are
// the AEAD associated data, so a rewritten epoch or sequence fails the tag.
inline constexpr std::size_t kSecureV2RecordHeaderBytes = 16;
inline constexpr std::size_t kSecureMaxPlainBytes = 1024;
inline constexpr std::size_t kSecureMaxRecordBytes =
    kSecureV2RecordHeaderBytes + kSecureMaxPlainBytes + kSecureTagBytes;
inline constexpr std::size_t kSecureCookieBytes = 16;
inline constexpr std::size_t kSecureCookieNonceBytes = 16;
inline constexpr std::uint64_t kSecureReplayWindow = 64;
// Records sealed under one epoch key before the next Noise REKEY() boundary.
inline constexpr std::uint32_t kSecureDefaultRecordsPerKey = 1u << 20;
// A record may not jump more than this many sequence numbers ahead, so a forged
// header cannot force unbounded key derivation.
inline constexpr std::uint64_t kSecureMaxSequenceJump = 4096;
// The Noise nonce is a 64-bit counter and 2^64-1 is reserved, so the transport
// refuses to seal or open at that value.
inline constexpr std::uint64_t kSecureMaxTransportSequence = 0xFFFFFFFFFFFFFFFFull;
// A forged v2 epoch may not advance the receive chain further than this before
// authentication, which bounds the derivation work a single datagram can cause.
inline constexpr std::uint64_t kSecureMaxEpochJump = 64;

// The Noise REKEY() primitive: the first 32 bytes of the ChaChaPoly encryption
// of 32 zero bytes under the maximum nonce with empty associated data. The v2
// epoch chain starts at the split key and applies this once per epoch, which is
// the standard Noise rekey rather than the frozen v1 FBY-REKEY label. A key
// whose length is not 32 bytes returns zeros and must not be used.
[[nodiscard]] std::array<std::byte, Crypto::kSha256Bytes> NoiseRekey(std::span<const std::byte> InKey) noexcept;

enum class SecureStatus : std::uint8_t
{
    Ok = 0,
    NotStarted,
    AlreadyEstablished,
    WrongTurn,
    NotEstablished,
    BadSize,
    BadMagic,
    BadVersion,
    BadType,
    BadCookie,
    BadTag,
    CookieRequired,
    BadKey,
    Replayed,
    InvalidConfig,
    NoRandomness,
    SendFailed,
};

[[nodiscard]] constexpr const char* SecureStatusName(SecureStatus InStatus) noexcept
{
    switch (InStatus)
    {
        case SecureStatus::Ok:
            return "Ok";
        case SecureStatus::NotStarted:
            return "NotStarted";
        case SecureStatus::AlreadyEstablished:
            return "AlreadyEstablished";
        case SecureStatus::WrongTurn:
            return "WrongTurn";
        case SecureStatus::NotEstablished:
            return "NotEstablished";
        case SecureStatus::BadSize:
            return "BadSize";
        case SecureStatus::BadMagic:
            return "BadMagic";
        case SecureStatus::BadVersion:
            return "BadVersion";
        case SecureStatus::BadType:
            return "BadType";
        case SecureStatus::BadCookie:
            return "BadCookie";
        case SecureStatus::CookieRequired:
            return "CookieRequired";
        case SecureStatus::BadTag:
            return "BadTag";
        case SecureStatus::BadKey:
            return "BadKey";
        case SecureStatus::Replayed:
            return "Replayed";
        case SecureStatus::InvalidConfig:
            return "InvalidConfig";
        case SecureStatus::NoRandomness:
            return "NoRandomness";
        case SecureStatus::SendFailed:
            return "SendFailed";
    }
    return "?";
}

// Stateless anti-spoofing cookie. One issuer is kept by a server and shared
// across connections: the cookie binds an address tag and a client nonce to a
// secret, so a forged source address never causes a Diffie-Hellman computation.
class SecureCookieIssuer
{
public:
    // Fills the secret from the OS entropy source.
    void Reset() noexcept;
    [[nodiscard]] bool Ready() const noexcept
    {
        return MReady;
    }

    void Issue(std::uint64_t InAddressTag, std::span<const std::byte> InNonce,
               std::span<std::byte> OutCookie) const noexcept;
    [[nodiscard]] bool Validate(std::uint64_t InAddressTag, std::span<const std::byte> InNonce,
                                std::span<const std::byte> InCookie) const noexcept;

protected:
    std::array<std::byte, 32> MSecret{};
    bool MReady = false;
};

struct SecureChannelConfig
{
    Crypto::X25519KeyPair LocalStatic{};
    std::array<std::byte, Crypto::kX25519KeyBytes> ExpectedRemoteStatic{};
    bool RequireExpectedRemote = false;
    bool Initiator = false;
    std::uint64_t SessionId = 0;
    // Records sealed under one epoch key. v1 derives the epoch from the sequence
    // number, so both sides must agree; v2 sends the epoch in the header, so a
    // receiver follows the sender's policy and never needs this value to open.
    std::uint32_t RecordsPerKey = kSecureDefaultRecordsPerKey;
    // Protocol v2 is the default: standard Noise XX name, the configured
    // prologue (the ticket hash by default in the session transport) and raw
    // X25519. Set false only to talk to a peer that still needs the frozen v1
    // FBY transcript; there is no silent downgrade.
    bool ProtocolV2 = true;

    // Responder side: verifies the cookie when the field below is set.
    const SecureCookieIssuer* CookieIssuer = nullptr;
    // Binds the cookie to the observed peer address.
    std::uint64_t AddressTag = 0;

    // Initiator side: the cookie obtained from the responder's challenge.
    bool HasCookie = false;
    std::array<std::byte, kSecureCookieNonceBytes> CookieNonce{};
    std::array<std::byte, kSecureCookieBytes> Cookie{};

    // Optional prologue mixed after the protocol name (v2) or after the fixed
    // v1 prologue. The secure session transport stores the 32-byte ticket hash
    // here, so both sides must agree on the exact ticket bytes. Conformance
    // vectors pass their prologue verbatim.
    std::size_t PrologueBytes = 0;
    std::array<std::byte, kSecureMaxPrologueBytes> Prologue{};

    // Deterministic test/KAT hook: derive the ephemeral key from this seed
    // instead of the OS entropy source. Production code never sets it.
    bool HasFixedEphemeral = false;
    std::array<std::byte, Crypto::kX25519KeyBytes> FixedEphemeralSeed{};
};

// Noise XX handshake plus the AEAD record layer. The handshake messages are
// exchanged by the caller (usually through NetTransport's reliable channel);
// records are sent as datagrams. v2 seals records with the Noise transport
// layout: the nonce is the transport message number, the header
// [epoch u64][sequence u64] is the associated data and the key chain is the
// Noise REKEY() chain from the split key, so no session id or custom label is
// mixed into the record. v1 keeps the frozen FBY framing.
class SecureChannel
{
public:
    SecureChannel() = default;

    SecureStatus Start(const SecureChannelConfig& InConfig) noexcept;
    void Reset() noexcept;

    [[nodiscard]] bool Established() const noexcept
    {
        return MPhase == Phase::Established;
    }
    [[nodiscard]] std::uint64_t SessionId() const noexcept
    {
        return MConfig.SessionId;
    }
    [[nodiscard]] const std::array<std::byte, Crypto::kX25519KeyBytes>& RemoteStatic() const noexcept
    {
        return MRemoteStatic;
    }
    // Current transcript hash. Exposed for the KAT and diagnostics only; it is
    // not a secret but it must never be sent on the wire.
    [[nodiscard]] const std::array<std::byte, Crypto::kX25519KeyBytes>& HandshakeHash() const noexcept
    {
        return MHandshakeHash;
    }

#if defined(FBY_ENGINE_TEST_HOOKS)
    // Conformance hook, only compiled into test builds: copies the split
    // transport keys so a vector test can check them against a reference
    // implementation's ciphertexts. Never call it on a live session; the values
    // are secret key material.
    void ExportTransportKeys(std::span<std::byte> OutSendKey, std::span<std::byte> OutReceiveKey) const noexcept;
#endif

    // Writes the next handshake message when it is this side's turn, otherwise
    // returns WrongTurn. `OutSize` receives the message length. `InPayload` is
    // the optional Noise handshake payload; the engine normally sends none.
    SecureStatus WriteHandshake(std::span<std::byte> OutMessage, std::size_t* OutSize,
                                std::span<const std::byte> InPayload = {}) noexcept;
    // Processes one inbound handshake message. After Ok the caller should call
    // WriteHandshake on its next turn (for the initiator's third message). The
    // optional payload is copied to OutPayload; OutPayloadSize receives its
    // length. A message whose payload does not fit is rejected with BadSize
    // before the transcript state advances.
    SecureStatus ReadHandshake(std::span<const std::byte> InMessage, std::span<std::byte> OutPayload = {},
                               std::size_t* OutPayloadSize = nullptr) noexcept;

    // Responder only: writes a stateless cookie challenge. The initiator stores
    // the nonce/cookie from it and resends its first handshake message.
    SecureStatus WriteCookieChallenge(std::span<std::byte> OutMessage, std::size_t* OutSize) noexcept;

    SecureStatus Seal(std::span<const std::byte> InPlaintext, std::span<std::byte> OutRecord,
                      std::size_t* OutSize) noexcept;
    SecureStatus Open(std::span<const std::byte> InRecord, std::span<std::byte> OutPlaintext,
                      std::size_t* OutSize) noexcept;

protected:
    enum class Phase : std::uint8_t
    {
        Idle = 0,
        InitSend1,
        InitRecv2,
        InitSend3,
        RespRecv1,
        RespSend2,
        RespRecv3,
        Established,
        Failed,
    };

    void MixHash(std::span<const std::byte> InData) noexcept;
    void MixKey(std::span<const std::byte> InKeyMaterial) noexcept;
    void EncryptAndHash(std::span<const std::byte> InPlaintext, std::span<std::byte> OutCiphertext,
                        std::size_t* OutSize) noexcept;
    SecureStatus DecryptAndHash(std::span<const std::byte> InCiphertext, std::span<std::byte> OutPlaintext,
                                std::size_t* OutSize) noexcept;
    void Split() noexcept;

    [[nodiscard]] SecureStatus DeriveSendKey(std::uint64_t InSequence) noexcept;
    [[nodiscard]] SecureStatus DeriveReceiveKey(std::uint64_t InSequence) noexcept;
    void AdvanceSendEpoch(std::uint64_t InTargetEpoch) noexcept;
    // Picks the key for an explicit v2 epoch without changing the receive state.
    // OutAdvancesEpoch is set when the caller must commit OutKey and
    // OutPreviousKey after a successful tag check.
    [[nodiscard]] SecureStatus SelectReceiveKey(std::uint64_t InEpoch,
                                                std::array<std::byte, Crypto::kSha256Bytes>& OutKey,
                                                std::array<std::byte, Crypto::kSha256Bytes>& OutPreviousKey,
                                                bool& OutAdvancesEpoch) const noexcept;
    void CommitReceive(std::uint64_t InSequence) noexcept;
    [[nodiscard]] bool WouldAccept(std::uint64_t InSequence) const noexcept;
    [[nodiscard]] std::array<std::byte, 12> RecordNonce(std::uint64_t InSequence) const noexcept;
    [[nodiscard]] std::array<std::byte, 8> SessionIdBytes() const noexcept;

    SecureChannelConfig MConfig{};
    Phase MPhase = Phase::Idle;
    std::uint16_t MVersion = kSecureVersionV1;
    bool MProtocolV2 = false;

    std::array<std::byte, Crypto::kX25519KeyBytes> MChainKey{};
    std::array<std::byte, Crypto::kX25519KeyBytes> MHandshakeHash{};
    std::array<std::byte, Crypto::kX25519KeyBytes> MInitialHandshakeHash{};
    std::array<std::byte, Crypto::kX25519KeyBytes> MInitialChainKey{};
    std::array<std::byte, Crypto::kX25519KeyBytes> MCipherKey{};
    std::uint64_t MHandshakeNonce = 0;
    Crypto::X25519KeyPair MLocalEphemeral{};
    std::array<std::byte, Crypto::kX25519KeyBytes> MRemoteEphemeral{};
    std::array<std::byte, Crypto::kX25519KeyBytes> MRemoteStatic{};

    std::array<std::byte, Crypto::kX25519KeyBytes> MSendKey{};
    std::array<std::byte, Crypto::kX25519KeyBytes> MRecvKey{};
    std::array<std::byte, Crypto::kX25519KeyBytes> MPrevRecvKey{};
    std::uint64_t MSendSequence = 0;
    std::uint64_t MSendEpoch = 0;
    std::uint64_t MRecvEpoch = 0;
    std::uint32_t MRecordsPerKey = kSecureDefaultRecordsPerKey;

    bool MRecvStarted = false;
    std::uint64_t MRecvHighest = 0;
    std::uint64_t MRecvBits = 0;
};
}
