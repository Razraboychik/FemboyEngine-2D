// FemboyEngine2D - .femarc distribution archiver.
#pragma once

#include "Engine/Assets/FemCrypto.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace Fby::Assets
{

inline constexpr char kFemArcMagic[8] = {'F', 'E', 'M', 'A', 'R', 'C', '0', '1'};
inline constexpr std::uint32_t kFemArcVersion = 1;
inline constexpr std::uint32_t kFemArcHeaderBytes = 128;
inline constexpr std::uint32_t kFemArcMaxEntries = 4096;
inline constexpr std::uint32_t kFemArcMaxPathBytes = 1024;
inline constexpr std::uint32_t kFemArcEntryBytes = 44;
inline constexpr std::uint32_t kFemArcAlignment = 16;

enum class ArchiveCodec : std::uint8_t
{
    Store = 0,
    Lzma = 1,
    FbyLz = 2,
};

enum class ArchiveCipher : std::uint8_t
{
    None = 0,
    ChaCha20Poly1305 = 1,
    Aes256Gcm = 2,
};

enum class ArchiveStatus : std::uint8_t
{
    Ok = 0,
    InvalidArgument,
    BufferTooSmall,
    TooManyEntries,
    Corrupt,
    Unsupported,
    AuthenticationFailed,
    CodecUnavailable,
};

[[nodiscard]] constexpr const char* ArchiveStatusName(ArchiveStatus InStatus) noexcept
{
    switch (InStatus)
    {
        case ArchiveStatus::Ok:
            return "ok";
        case ArchiveStatus::InvalidArgument:
            return "invalid argument";
        case ArchiveStatus::BufferTooSmall:
            return "buffer too small";
        case ArchiveStatus::TooManyEntries:
            return "too many entries";
        case ArchiveStatus::Corrupt:
            return "corrupt archive";
        case ArchiveStatus::Unsupported:
            return "unsupported archive feature";
        case ArchiveStatus::AuthenticationFailed:
            return "authentication failed";
        case ArchiveStatus::CodecUnavailable:
            return "codec unavailable";
    }
    return "unknown";
}

struct ArchiveInput
{
    std::string_view path;
    std::span<const std::byte> bytes;
};

enum class ArchiveProgressStage : std::uint8_t
{
    Compressing = 0,
    Encrypting = 1,
    Done = 2,
};

struct ArchiveProgress
{
    ArchiveProgressStage stage = ArchiveProgressStage::Compressing;
    std::uint32_t index = 0;
    std::uint32_t total = 0;
    // Current entry for Compressing; empty for Encrypting/Done.
    std::string_view path;
    std::uint64_t rawBytes = 0;
    std::uint64_t storedBytes = 0;
};

// Function pointer, not std::function: the cooker may call this from its worker
using ArchiveProgressFn = void (*)(void* InUser, const ArchiveProgress& InProgress) noexcept;

struct ArchiveBuildOptions
{
    ArchiveCodec codec = ArchiveCodec::FbyLz;
    ArchiveCipher cipher = ArchiveCipher::None;
    // Required when cipher != None. The key is copied into the local scope and
    const Crypto::AeadKey* key = nullptr;
    Crypto::AeadNonce nonce{};
    // 0 = fastest, 9 = best. Used by the LZMA SDK path only.
    std::uint32_t level = 5;
    // Compression scratch. FbyLz needs at least 256 KiB; the LZMA SDK path needs
    std::span<std::byte> scratch{};
    // Optional progress sink. Build() calls it once per compressed entry, then
    ArchiveProgressFn progress = nullptr;
    void* progressUser = nullptr;
};

struct ArchiveEntryView
{
    std::string_view path;
    std::span<const std::byte> stored;
    std::uint64_t rawSize = 0;
    std::uint64_t crc64 = 0;
    ArchiveCodec codec = ArchiveCodec::Store;
    std::uint8_t properties[5] = {};
    std::uint8_t propertyBytes = 0;
};

class ArchiveReader;

class FemArchiver
{
public:
    // Worst-case byte count for `InEntries`. The caller may pass a larger
    [[nodiscard]] static std::size_t MaxArchiveBytes(std::span<const ArchiveInput> InEntries,
                                                     ArchiveCodec InCodec) noexcept;

    // Builds a complete .femarc. `OutArchive` receives the header followed by
    [[nodiscard]] static ArchiveStatus Build(std::span<const ArchiveInput> InEntries,
                                             std::span<std::byte> OutArchive, const ArchiveBuildOptions& InOptions,
                                             std::size_t* OutBytes) noexcept;

    // Opens an archive. Encrypted archives are decrypted into `InDecryptBuffer`, which
    [[nodiscard]] static ArchiveStatus Open(std::span<const std::byte> InArchive, const Crypto::AeadKey* InKey,
                                            std::span<std::byte> InDecryptBuffer, ArchiveReader& OutReader) noexcept;
};

class ArchiveReader
{
public:
    ArchiveReader() = default;

    [[nodiscard]] std::uint32_t EntryCount() const noexcept
    {
        return MEntryCount;
    }
    [[nodiscard]] ArchiveCodec Codec() const noexcept
    {
        return MCodec;
    }
    [[nodiscard]] ArchiveCipher Cipher() const noexcept
    {
        return MCipher;
    }
    [[nodiscard]] std::uint64_t RawBytes() const noexcept
    {
        return MRawBytes;
    }
    [[nodiscard]] bool Encrypted() const noexcept
    {
        return MCipher != ArchiveCipher::None;
    }

    [[nodiscard]] ArchiveStatus Entry(std::uint32_t InIndex, ArchiveEntryView& OutEntry) const noexcept;
    // Decompresses one entry into `OutPlain`. The output size must equal
    [[nodiscard]] ArchiveStatus Extract(std::uint32_t InIndex, std::span<std::byte> OutPlain,
                                        std::span<std::byte> InScratch) const noexcept;
    // Path lookup without allocating. Returns the entry index or kFemArcMaxEntries.
    [[nodiscard]] std::uint32_t Find(std::string_view InPath) const noexcept;

private:
    friend class FemArchiver;

    std::span<const std::byte> MArchive{};
    std::span<const std::byte> MBody{};
    std::uint32_t MEntryCount = 0;
    std::uint32_t MTableOffset = 0;
    std::uint32_t MStringOffset = 0;
    std::uint32_t MDataOffset = 0;
    std::uint64_t MRawBytes = 0;
    ArchiveCodec MCodec = ArchiveCodec::Store;
    ArchiveCipher MCipher = ArchiveCipher::None;
};

// In-tree fallback codec. Public so the cooker test can verify it against a
[[nodiscard]] bool FbyLzCompress(std::span<const std::byte> InPlain, std::span<std::byte> OutCompressed,
                                 std::span<std::byte> InScratch, std::size_t* OutBytes) noexcept;
[[nodiscard]] bool FbyLzDecompress(std::span<const std::byte> InCompressed, std::span<std::byte> OutPlain) noexcept;
[[nodiscard]] bool LzmaSdkAvailable() noexcept;
}
