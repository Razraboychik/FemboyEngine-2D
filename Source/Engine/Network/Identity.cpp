// FemboyEngine2D - network identity record implementation.
#include "Engine/Core/Time.hpp"
#include "Engine/Network/Identity.hpp"

#include "Engine/Assets/FemCrypto.hpp"
#include "Engine/Core/CryptoKdf.hpp"
#include "Engine/Core/SecureRng.hpp"
#include "Engine/Network/ByteOrder.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wincrypt.h>
#endif

namespace Fby::Net
{
namespace
{
constexpr std::size_t kVersionOffset = 4;
constexpr std::size_t kReservedOffset = 6;
constexpr std::size_t kNameOffset = 8;
constexpr std::size_t kExchangeSecretOffset = kNameOffset + kIdentityNameBytes;
constexpr std::size_t kExchangePublicOffset = kExchangeSecretOffset + Crypto::kX25519KeyBytes;
constexpr std::size_t kSigningSecretOffset = kExchangePublicOffset + Crypto::kX25519KeyBytes;
constexpr std::size_t kSigningPublicOffset = kSigningSecretOffset + Crypto::kEddsaSecretBytes;
constexpr std::size_t kCreatedOffset = kSigningPublicOffset + Crypto::kEddsaPublicBytes;
constexpr std::size_t kDigestOffset = kCreatedOffset + 8;

static_assert(kDigestOffset + Crypto::kSha256Bytes == kIdentityFileBytes,
              "identity record layout must fill exactly kIdentityFileBytes");

[[nodiscard]] std::uint8_t* BytesOf(std::span<std::byte> InBytes) noexcept
{
    return reinterpret_cast<std::uint8_t*>(InBytes.data());
}

[[nodiscard]] const std::uint8_t* BytesOf(std::span<const std::byte> InBytes) noexcept
{
    return reinterpret_cast<const std::uint8_t*>(InBytes.data());
}

void SetError(std::string* InError, const char* InMessage)
{
    if (InError != nullptr)
    {
        *InError = InMessage;
    }
}

// Copies at most kIdentityNameBytes - 1 characters and zero-fills the rest.
void CopyName(std::string_view InName, char* OutName) noexcept
{
    std::memset(OutName, 0, kIdentityNameBytes);
    const std::size_t ScopedCount = InName.size() < kIdentityNameBytes - 1 ? InName.size() : kIdentityNameBytes - 1;
    if (ScopedCount > 0)
    {
        std::memcpy(OutName, InName.data(), ScopedCount);
    }
}

#if defined(_WIN32)
constexpr wchar_t kDpapiDescription[] = L"FemboyEngine2D network identity";

[[nodiscard]] bool ProtectIdentity(std::span<const std::byte> InRecord, std::vector<std::byte>* OutBlob,
                                   std::string* InError)
{
    DATA_BLOB ScopedInput{};
    ScopedInput.pbData = const_cast<BYTE*>(reinterpret_cast<const BYTE*>(InRecord.data()));
    ScopedInput.cbData = static_cast<DWORD>(InRecord.size());
    DATA_BLOB ScopedOutput{};
    if (!CryptProtectData(&ScopedInput, kDpapiDescription, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN,
                          &ScopedOutput))
    {
        SetError(InError, "identity: DPAPI refused to protect the record");
        return false;
    }
    OutBlob->assign(reinterpret_cast<const std::byte*>(ScopedOutput.pbData),
                    reinterpret_cast<const std::byte*>(ScopedOutput.pbData) + ScopedOutput.cbData);
    LocalFree(ScopedOutput.pbData);
    return true;
}

[[nodiscard]] bool UnprotectIdentity(std::span<const std::byte> InBlob, std::vector<std::byte>* OutRecord,
                                     std::string* InError)
{
    DATA_BLOB ScopedInput{};
    ScopedInput.pbData = const_cast<BYTE*>(reinterpret_cast<const BYTE*>(InBlob.data()));
    ScopedInput.cbData = static_cast<DWORD>(InBlob.size());
    DATA_BLOB ScopedOutput{};
    if (!CryptUnprotectData(&ScopedInput, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN,
                            &ScopedOutput))
    {
        SetError(InError, "identity: DPAPI could not decrypt the record for this user");
        return false;
    }
    OutRecord->assign(reinterpret_cast<const std::byte*>(ScopedOutput.pbData),
                      reinterpret_cast<const std::byte*>(ScopedOutput.pbData) + ScopedOutput.cbData);
    LocalFree(ScopedOutput.pbData);
    return true;
}
#endif
}

std::uint64_t UnixTimeNow() noexcept
{
    return Fby::Core::Clock::UnixSeconds();
}

bool NetIdentity::Valid() const noexcept
{
    Crypto::X25519KeyPair ScopedDerived{};
    if (!Crypto::X25519KeyPairFromSeed(std::span<const std::byte>(Exchange.Secret), &ScopedDerived))
    {
        return false;
    }
    if (ScopedDerived.Public != Exchange.Public)
    {
        return false;
    }

    // The EdDSA secret carries the public half back to back; a mismatch means
    // the record was assembled from parts of two different identities.
    if (std::memcmp(Signing.Secret.data() + Crypto::kEddsaPublicBytes, Signing.Public.data(),
                    Crypto::kEddsaPublicBytes) != 0)
    {
        return false;
    }

    std::size_t ScopedNameLength = 0;
    while (ScopedNameLength < kIdentityNameBytes && Name[ScopedNameLength] != '\0')
    {
        ++ScopedNameLength;
    }
    if (ScopedNameLength == 0 || ScopedNameLength == kIdentityNameBytes)
    {
        return false;
    }

    return CreatedUnixSeconds != 0;
}

bool GenerateIdentity(std::string_view InName, NetIdentity* OutIdentity) noexcept
{
    if (OutIdentity == nullptr)
    {
        return false;
    }
    *OutIdentity = NetIdentity{};

    std::array<std::byte, Crypto::kX25519KeyBytes> ScopedExchangeSeed{};
    std::array<std::byte, Crypto::kEddsaSeedBytes> ScopedSigningSeed{};
    if (!SecureRng::Fill(ScopedExchangeSeed) || !SecureRng::Fill(ScopedSigningSeed))
    {
        Crypto::WipeBytes(ScopedExchangeSeed);
        Crypto::WipeBytes(ScopedSigningSeed);
        return false;
    }

    const bool ScopedExchangeOk = Crypto::X25519KeyPairFromSeed(ScopedExchangeSeed, &OutIdentity->Exchange);
    const bool ScopedSigningOk = Crypto::EddsaKeyPairFromSeed(ScopedSigningSeed, &OutIdentity->Signing);
    Crypto::WipeBytes(ScopedExchangeSeed);
    Crypto::WipeBytes(ScopedSigningSeed);
    if (!ScopedExchangeOk || !ScopedSigningOk)
    {
        return false;
    }

    OutIdentity->CreatedUnixSeconds = UnixTimeNow();
    CopyName(InName, OutIdentity->Name);
    return OutIdentity->Valid();
}

bool EncodeIdentity(const NetIdentity& InIdentity, std::span<std::byte> OutBytes) noexcept
{
    if (OutBytes.size() != kIdentityFileBytes)
    {
        return false;
    }
    std::memset(OutBytes.data(), 0, OutBytes.size());

    std::uint8_t* ScopedBytes = BytesOf(OutBytes);
    Detail::StoreU32(ScopedBytes, kIdentityMagic);
    Detail::StoreU16(ScopedBytes + kVersionOffset, kIdentityVersion);
    Detail::StoreU16(ScopedBytes + kReservedOffset, 0);
    std::memcpy(ScopedBytes + kNameOffset, InIdentity.Name, kIdentityNameBytes);
    std::memcpy(ScopedBytes + kExchangeSecretOffset, InIdentity.Exchange.Secret.data(), Crypto::kX25519KeyBytes);
    std::memcpy(ScopedBytes + kExchangePublicOffset, InIdentity.Exchange.Public.data(), Crypto::kX25519KeyBytes);
    std::memcpy(ScopedBytes + kSigningSecretOffset, InIdentity.Signing.Secret.data(), Crypto::kEddsaSecretBytes);
    std::memcpy(ScopedBytes + kSigningPublicOffset, InIdentity.Signing.Public.data(), Crypto::kEddsaPublicBytes);
    Detail::StoreU64(ScopedBytes + kCreatedOffset, InIdentity.CreatedUnixSeconds);

    // The digest covers the whole record up to the digest itself.
    Crypto::Blake2b(OutBytes.first(kDigestOffset),
                    OutBytes.subspan(kDigestOffset, Crypto::kSha256Bytes));
    return true;
}

bool DecodeIdentity(std::span<const std::byte> InBytes, NetIdentity* OutIdentity) noexcept
{
    if (OutIdentity == nullptr || InBytes.size() != kIdentityFileBytes)
    {
        return false;
    }

    const std::uint8_t* ScopedBytes = BytesOf(InBytes);
    if (Detail::LoadU32(ScopedBytes) != kIdentityMagic)
    {
        return false;
    }
    if (Detail::LoadU16(ScopedBytes + kVersionOffset) != kIdentityVersion)
    {
        return false;
    }

    std::array<std::byte, Crypto::kSha256Bytes> ScopedDigest{};
    Crypto::Blake2b(InBytes.first(kDigestOffset), ScopedDigest);
    if (!Crypto::ConstantTimeEquals(ScopedDigest, InBytes.subspan(kDigestOffset, Crypto::kSha256Bytes)))
    {
        return false;
    }

    NetIdentity ScopedIdentity{};
    std::memcpy(ScopedIdentity.Name, ScopedBytes + kNameOffset, kIdentityNameBytes);
    std::memcpy(ScopedIdentity.Exchange.Secret.data(), ScopedBytes + kExchangeSecretOffset, Crypto::kX25519KeyBytes);
    std::memcpy(ScopedIdentity.Exchange.Public.data(), ScopedBytes + kExchangePublicOffset, Crypto::kX25519KeyBytes);
    std::memcpy(ScopedIdentity.Signing.Secret.data(), ScopedBytes + kSigningSecretOffset, Crypto::kEddsaSecretBytes);
    std::memcpy(ScopedIdentity.Signing.Public.data(), ScopedBytes + kSigningPublicOffset, Crypto::kEddsaPublicBytes);
    ScopedIdentity.CreatedUnixSeconds = Detail::LoadU64(ScopedBytes + kCreatedOffset);
    if (!ScopedIdentity.Valid())
    {
        return false;
    }
    *OutIdentity = ScopedIdentity;
    return true;
}

bool SaveIdentity(const NetIdentity& InIdentity, const std::filesystem::path& InPath, std::string* InError)
{
    std::array<std::byte, kIdentityFileBytes> ScopedRecord{};
    if (!EncodeIdentity(InIdentity, ScopedRecord))
    {
        SetError(InError, "identity: cannot encode the record");
        return false;
    }

    std::vector<std::byte> ScopedPayload;
    std::uint16_t ScopedFlags = 0;
#if defined(_WIN32)
    if (!ProtectIdentity(ScopedRecord, &ScopedPayload, InError))
    {
        return false;
    }
    ScopedFlags = kIdentityStorageFlagDpapi;
#else
    ScopedPayload.assign(ScopedRecord.begin(), ScopedRecord.end());
#endif
    if (ScopedPayload.empty() || ScopedPayload.size() > kIdentityStorageMaxPayloadBytes)
    {
        SetError(InError, "identity: protected record has an unexpected size");
        return false;
    }

    std::vector<std::byte> ScopedFile(kIdentityStorageHeaderBytes + ScopedPayload.size());
    std::uint8_t* ScopedHeader = BytesOf(std::span<std::byte>(ScopedFile));
    Detail::StoreU32(ScopedHeader, kIdentityStorageMagic);
    Detail::StoreU16(ScopedHeader + 4, kIdentityStorageVersion);
    Detail::StoreU16(ScopedHeader + 6, ScopedFlags);
    Detail::StoreU32(ScopedHeader + 8, static_cast<std::uint32_t>(ScopedPayload.size()));
    std::memcpy(ScopedFile.data() + kIdentityStorageHeaderBytes, ScopedPayload.data(), ScopedPayload.size());

    std::FILE* ScopedOutput = std::fopen(InPath.string().c_str(), "wb");
    if (ScopedOutput == nullptr)
    {
        SetError(InError, "identity: cannot open the target file for writing");
        return false;
    }
    const std::size_t ScopedWritten = std::fwrite(ScopedFile.data(), 1, ScopedFile.size(), ScopedOutput);
    const bool ScopedClosed = std::fclose(ScopedOutput) == 0;
    if (ScopedWritten != ScopedFile.size() || !ScopedClosed)
    {
        SetError(InError, "identity: the record was not written completely");
        return false;
    }

    // Owner-only access. On Windows the record is DPAPI-protected and the file
    // inherits the profile ACL; the permission call is best effort there.
    std::error_code ScopedError;
    std::filesystem::permissions(InPath,
                                 std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::replace, ScopedError);
    return true;
}

bool LoadIdentity(const std::filesystem::path& InPath, NetIdentity* OutIdentity, std::string* InError)
{
    if (OutIdentity == nullptr)
    {
        SetError(InError, "identity: no destination");
        return false;
    }

    std::error_code ScopedSizeError;
    const std::uintmax_t ScopedSize = std::filesystem::file_size(InPath, ScopedSizeError);
    if (ScopedSizeError || ScopedSize < kIdentityStorageHeaderBytes ||
        ScopedSize > kIdentityStorageHeaderBytes + kIdentityStorageMaxPayloadBytes)
    {
        SetError(InError, "identity: the record has an unexpected size");
        return false;
    }

    std::FILE* ScopedInput = std::fopen(InPath.string().c_str(), "rb");
    if (ScopedInput == nullptr)
    {
        SetError(InError, "identity: cannot open the record for reading");
        return false;
    }
    std::vector<std::byte> ScopedFile(static_cast<std::size_t>(ScopedSize));
    const std::size_t ScopedRead = std::fread(ScopedFile.data(), 1, ScopedFile.size(), ScopedInput);
    std::fclose(ScopedInput);
    if (ScopedRead != ScopedFile.size())
    {
        SetError(InError, "identity: the record was not read completely");
        return false;
    }

    const std::uint8_t* ScopedBytes = BytesOf(std::span<const std::byte>(ScopedFile));
    // Version 1: the bare 240-byte record. It is still accepted so an existing
    // install can migrate on the next save.
    if (ScopedFile.size() == kIdentityFileBytes && Detail::LoadU32(ScopedBytes) == kIdentityMagic)
    {
        if (!DecodeIdentity(ScopedFile, OutIdentity))
        {
            SetError(InError, "identity: the version-1 record is corrupted or inconsistent");
            return false;
        }
        return true;
    }

    if (Detail::LoadU32(ScopedBytes) != kIdentityStorageMagic)
    {
        SetError(InError, "identity: unknown record magic");
        return false;
    }
    if (Detail::LoadU16(ScopedBytes + 4) != kIdentityStorageVersion)
    {
        SetError(InError, "identity: unsupported record version");
        return false;
    }
    const std::uint16_t ScopedFlags = Detail::LoadU16(ScopedBytes + 6);
    const std::uint32_t ScopedPayloadBytes = Detail::LoadU32(ScopedBytes + 8);
    if (ScopedPayloadBytes == 0 ||
        ScopedFile.size() != kIdentityStorageHeaderBytes + static_cast<std::size_t>(ScopedPayloadBytes))
    {
        SetError(InError, "identity: record payload length does not match the file");
        return false;
    }
    const std::span<const std::byte> ScopedPayload(ScopedFile.data() + kIdentityStorageHeaderBytes,
                                                   ScopedPayloadBytes);

    std::vector<std::byte> ScopedRecord;
    if ((ScopedFlags & kIdentityStorageFlagDpapi) != 0)
    {
#if defined(_WIN32)
        if (!UnprotectIdentity(ScopedPayload, &ScopedRecord, InError))
        {
            return false;
        }
#else
        SetError(InError, "identity: DPAPI record cannot be read on this platform");
        return false;
#endif
    }
    else
    {
        ScopedRecord.assign(ScopedPayload.begin(), ScopedPayload.end());
    }

    if (ScopedRecord.size() != kIdentityFileBytes || !DecodeIdentity(ScopedRecord, OutIdentity))
    {
        SetError(InError, "identity: the record is corrupted or inconsistent");
        return false;
    }
    return true;
}
}
