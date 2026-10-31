// FemboyEngine2D - long-lived network identity and its restricted file record.
#pragma once

#include "Engine/Core/CryptoAsymmetric.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace Fby::Net
{

inline constexpr std::size_t kIdentityNameBytes = 32;
inline constexpr std::uint32_t kIdentityMagic = 0x31444946u;
inline constexpr std::uint16_t kIdentityVersion = 1;
// Layout: magic, version, reserved, name, X25519 secret/public, EdDSA
// secret/public, creation time and a BLAKE2b-256 integrity digest.
inline constexpr std::size_t kIdentityFileBytes = 240;

// On-disk container (version 2): magic, version, flags, payload length and the
// payload. On Windows the payload is a DPAPI blob of the 240-byte record; on
// other platforms it is the record itself and the file is owner-only (0600).
inline constexpr std::uint32_t kIdentityStorageMagic = 0x32444946u;
inline constexpr std::uint16_t kIdentityStorageVersion = 2;
inline constexpr std::size_t kIdentityStorageHeaderBytes = 12;
inline constexpr std::uint16_t kIdentityStorageFlagDpapi = 1u << 0;
inline constexpr std::size_t kIdentityStorageMaxPayloadBytes = 4096;

struct NetIdentity
{
    Crypto::X25519KeyPair Exchange{};
    Crypto::EddsaKeyPair Signing{};
    std::uint64_t CreatedUnixSeconds = 0;
    char Name[kIdentityNameBytes]{};

    // Recomputes both public keys from their secrets and checks the name. A
    // corrupted record fails here before it can be used to sign a ticket.
    [[nodiscard]] bool Valid() const noexcept;
};

[[nodiscard]] std::uint64_t UnixTimeNow() noexcept;

// Fills both key pairs from the OS entropy source. The name is truncated to
// kIdentityNameBytes - 1 characters and zero-padded.
[[nodiscard]] bool GenerateIdentity(std::string_view InName, NetIdentity* OutIdentity) noexcept;

// Fixed binary record. The digest detects corruption and accidental edits; it
// is not a trust anchor, so a tampered record can only be re-created by hand
// and it still fails Valid() unless the keys are consistent.
[[nodiscard]] bool EncodeIdentity(const NetIdentity& InIdentity, std::span<std::byte> OutBytes) noexcept;
[[nodiscard]] bool DecodeIdentity(std::span<const std::byte> InBytes, NetIdentity* OutIdentity) noexcept;

// Writes the record in the version-2 container. On Windows the record is
// encrypted with DPAPI and the file only contains the protected blob; on POSIX
// the file is restricted to mode 0600. Version-1 files (exactly
// kIdentityFileBytes bytes) still load and are upgraded by the next save.
[[nodiscard]] bool SaveIdentity(const NetIdentity& InIdentity, const std::filesystem::path& InPath,
                                std::string* InError = nullptr);
[[nodiscard]] bool LoadIdentity(const std::filesystem::path& InPath, NetIdentity* OutIdentity,
                                std::string* InError = nullptr);
}
