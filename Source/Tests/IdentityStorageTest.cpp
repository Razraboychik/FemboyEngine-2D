// FemboyEngine2D - identity storage: DPAPI/0600, versioning and migration.
#include "Engine/Network/Identity.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <vector>

namespace
{
std::uint32_t GFailures = 0;

#define FBY_CHECK(condition)                                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                  \
            ++GFailures;                                                                                               \
        }                                                                                                              \
    } while (false)

[[nodiscard]] std::filesystem::path TempPath(const char* InName)
{
    std::error_code ScopedError;
    return std::filesystem::current_path(ScopedError) / InName;
}

[[nodiscard]] bool ReadFile(const std::filesystem::path& InPath, std::vector<std::byte>* OutBytes)
{
    std::ifstream ScopedInput(InPath, std::ios::binary);
    if (!ScopedInput)
    {
        return false;
    }
    ScopedInput.seekg(0, std::ios::end);
    const std::streamoff ScopedEnd = ScopedInput.tellg();
    ScopedInput.seekg(0, std::ios::beg);
    if (ScopedEnd <= 0)
    {
        return false;
    }
    OutBytes->resize(static_cast<std::size_t>(ScopedEnd));
    ScopedInput.read(reinterpret_cast<char*>(OutBytes->data()), ScopedEnd);
    return ScopedInput.good() || ScopedInput.eof();
}

[[nodiscard]] std::uint32_t LoadU32(const std::vector<std::byte>& InBytes, std::size_t InOffset)
{
    if (InOffset + 4 > InBytes.size())
    {
        return 0;
    }
    return static_cast<std::uint32_t>(InBytes[InOffset]) |
           (static_cast<std::uint32_t>(InBytes[InOffset + 1]) << 8) |
           (static_cast<std::uint32_t>(InBytes[InOffset + 2]) << 16) |
           (static_cast<std::uint32_t>(InBytes[InOffset + 3]) << 24);
}

[[nodiscard]] std::uint16_t LoadU16(const std::vector<std::byte>& InBytes, std::size_t InOffset)
{
    if (InOffset + 2 > InBytes.size())
    {
        return 0;
    }
    return static_cast<std::uint16_t>(static_cast<std::uint16_t>(InBytes[InOffset]) |
                                      (static_cast<std::uint16_t>(InBytes[InOffset + 1]) << 8));
}

[[nodiscard]] bool ContainsSequence(const std::vector<std::byte>& InBytes, std::span<const std::byte> InNeedle)
{
    if (InNeedle.empty() || InBytes.size() < InNeedle.size())
    {
        return false;
    }
    for (std::size_t ScopedIndex = 0; ScopedIndex + InNeedle.size() <= InBytes.size(); ++ScopedIndex)
    {
        if (std::memcmp(InBytes.data() + ScopedIndex, InNeedle.data(), InNeedle.size()) == 0)
        {
            return true;
        }
    }
    return false;
}
}

int main()
{
    using namespace Fby::Net;

    const std::filesystem::path ScopedPath = TempPath("fby_identity_storage_test.bin");
    std::error_code ScopedError;
    std::filesystem::remove(ScopedPath, ScopedError);

    NetIdentity ScopedIdentity{};
    FBY_CHECK(GenerateIdentity("Storage Host", &ScopedIdentity));
    FBY_CHECK(ScopedIdentity.Valid());

    // 1. Version-2 round trip.
    std::string ScopedMessage;
    FBY_CHECK(SaveIdentity(ScopedIdentity, ScopedPath, &ScopedMessage));
    std::vector<std::byte> ScopedFile;
    FBY_CHECK(ReadFile(ScopedPath, &ScopedFile));
    FBY_CHECK(ScopedFile.size() > kIdentityStorageHeaderBytes);
    FBY_CHECK(LoadU32(ScopedFile, 0) == kIdentityStorageMagic);
    FBY_CHECK(LoadU16(ScopedFile, 4) == kIdentityStorageVersion);
    FBY_CHECK(LoadU32(ScopedFile, 8) == ScopedFile.size() - kIdentityStorageHeaderBytes);

    NetIdentity ScopedLoaded{};
    FBY_CHECK(LoadIdentity(ScopedPath, &ScopedLoaded, &ScopedMessage));
    FBY_CHECK(ScopedLoaded.Exchange.Secret == ScopedIdentity.Exchange.Secret);
    FBY_CHECK(ScopedLoaded.Signing.Secret == ScopedIdentity.Signing.Secret);

#if defined(_WIN32)
    // DPAPI: the record is encrypted for this user and the raw signing secret is
    // not present in the file.
    const std::uint16_t ScopedFlags = LoadU16(ScopedFile, 6);
    FBY_CHECK((ScopedFlags & kIdentityStorageFlagDpapi) != 0);
    FBY_CHECK(!ContainsSequence(ScopedFile, ScopedIdentity.Signing.Secret));
    FBY_CHECK(!ContainsSequence(ScopedFile, ScopedIdentity.Exchange.Secret));
#else
    // POSIX: the record is plain but owner-only.
    const std::uint16_t ScopedFlags = LoadU16(ScopedFile, 6);
    FBY_CHECK((ScopedFlags & kIdentityStorageFlagDpapi) == 0);
    const std::filesystem::perms ScopedPerms = std::filesystem::status(ScopedPath, ScopedError).permissions();
    FBY_CHECK((ScopedPerms & std::filesystem::perms::group_read) == std::filesystem::perms::none);
    FBY_CHECK((ScopedPerms & std::filesystem::perms::others_read) == std::filesystem::perms::none);
#endif

    // 2. A version-1 file (bare 240-byte record) still loads and the next save
    // upgrades it to the container.
    const std::filesystem::path ScopedLegacyPath = TempPath("fby_identity_storage_legacy.bin");
    std::filesystem::remove(ScopedLegacyPath, ScopedError);
    {
        std::array<std::byte, kIdentityFileBytes> ScopedRecord{};
        FBY_CHECK(EncodeIdentity(ScopedIdentity, ScopedRecord));
        std::ofstream ScopedOutput(ScopedLegacyPath, std::ios::binary | std::ios::trunc);
        ScopedOutput.write(reinterpret_cast<const char*>(ScopedRecord.data()),
                           static_cast<std::streamsize>(ScopedRecord.size()));
    }
    NetIdentity ScopedLegacy{};
    FBY_CHECK(LoadIdentity(ScopedLegacyPath, &ScopedLegacy, &ScopedMessage));
    FBY_CHECK(ScopedLegacy.Signing.Public == ScopedIdentity.Signing.Public);
    FBY_CHECK(SaveIdentity(ScopedLegacy, ScopedLegacyPath, &ScopedMessage));
    std::vector<std::byte> ScopedMigrated;
    FBY_CHECK(ReadFile(ScopedLegacyPath, &ScopedMigrated));
    FBY_CHECK(LoadU32(ScopedMigrated, 0) == kIdentityStorageMagic);
    NetIdentity ScopedReloaded{};
    FBY_CHECK(LoadIdentity(ScopedLegacyPath, &ScopedReloaded, &ScopedMessage));
    FBY_CHECK(ScopedReloaded.Signing.Public == ScopedIdentity.Signing.Public);

    // 3. Truncated and header-tampered files are rejected.
    {
        std::vector<std::byte> ScopedTruncated(ScopedFile.begin(), ScopedFile.begin() + 8);
        const std::filesystem::path ScopedBadPath = TempPath("fby_identity_storage_bad.bin");
        std::filesystem::remove(ScopedBadPath, ScopedError);
        {
            std::ofstream ScopedOutput(ScopedBadPath, std::ios::binary | std::ios::trunc);
            ScopedOutput.write(reinterpret_cast<const char*>(ScopedTruncated.data()),
                               static_cast<std::streamsize>(ScopedTruncated.size()));
        }
        NetIdentity ScopedBad{};
        FBY_CHECK(!LoadIdentity(ScopedBadPath, &ScopedBad, &ScopedMessage));

        std::vector<std::byte> ScopedWrongVersion = ScopedFile;
        ScopedWrongVersion[4] = std::byte{9};
        {
            std::ofstream ScopedOutput(ScopedBadPath, std::ios::binary | std::ios::trunc);
            ScopedOutput.write(reinterpret_cast<const char*>(ScopedWrongVersion.data()),
                               static_cast<std::streamsize>(ScopedWrongVersion.size()));
        }
        FBY_CHECK(!LoadIdentity(ScopedBadPath, &ScopedBad, &ScopedMessage));

        std::vector<std::byte> ScopedWrongLength = ScopedFile;
        ScopedWrongLength[8] =
            static_cast<std::byte>(static_cast<std::uint8_t>(ScopedWrongLength[8]) + std::uint8_t{1});
        {
            std::ofstream ScopedOutput(ScopedBadPath, std::ios::binary | std::ios::trunc);
            ScopedOutput.write(reinterpret_cast<const char*>(ScopedWrongLength.data()),
                               static_cast<std::streamsize>(ScopedWrongLength.size()));
        }
        FBY_CHECK(!LoadIdentity(ScopedBadPath, &ScopedBad, &ScopedMessage));
        std::filesystem::remove(ScopedBadPath, ScopedError);
    }

    std::filesystem::remove(ScopedPath, ScopedError);
    std::filesystem::remove(ScopedLegacyPath, ScopedError);

    if (GFailures == 0)
    {
        std::printf("identity_storage_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "identity_storage_test: %u failure(s)\n", GFailures);
    return 1;
}
