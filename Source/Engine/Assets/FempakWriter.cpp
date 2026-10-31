#include "Engine/Assets/FempakWriter.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace Fby::Assets
{
namespace
{
[[nodiscard]] std::string JoinVirtual(std::string_view InRoot, std::string_view InRelative)
{
    const std::string ScopedNormalizedRoot = NormalizePakPath(InRoot);
    const std::string ScopedNormalizedRelative = NormalizePakPath(InRelative);
    if (ScopedNormalizedRoot.empty())
    {
        return ScopedNormalizedRelative;
    }
    if (ScopedNormalizedRelative.empty())
    {
        return ScopedNormalizedRoot;
    }
    return ScopedNormalizedRoot + "/" + ScopedNormalizedRelative;
}

void SetError(std::string* InError, std::string InMessage)
{
    if (InError != nullptr)
    {
        *InError = std::move(InMessage);
    }
}

[[nodiscard]] bool ReadFileBytes(const std::filesystem::path& InPath, std::vector<std::byte>* InBytes)
{
    std::ifstream ScopedStream(InPath, std::ios::binary);
    if (!ScopedStream)
    {
        return false;
    }
    ScopedStream.seekg(0, std::ios::end);
    const std::streamoff ScopedSize = ScopedStream.tellg();
    if (ScopedSize < 0)
    {
        return false;
    }
    ScopedStream.seekg(0, std::ios::beg);
    InBytes->resize(static_cast<std::size_t>(ScopedSize));
    if (ScopedSize > 0)
    {
        ScopedStream.read(reinterpret_cast<char*>(InBytes->data()), static_cast<std::streamsize>(ScopedSize));
    }
    return static_cast<bool>(ScopedStream);
}
}

void FempakWriter::Add(std::string_view InPath, std::span<const std::byte> InBytes, std::uint32_t InAlignment)
{
    std::vector<std::byte> ScopedCopy(InBytes.begin(), InBytes.end());
    Add(InPath, std::move(ScopedCopy), InAlignment);
}

void FempakWriter::Add(std::string_view InPath, std::vector<std::byte> InBytes, std::uint32_t InAlignment)
{
    const std::string ScopedNormalized = NormalizePakPath(InPath);
    if (ScopedNormalized.empty())
    {
        return;
    }
    const auto ScopedExisting =
        std::find_if(MEntries.begin(), MEntries.end(),
                     [&ScopedNormalized](const FempakInput& InEntry) { return InEntry.path == ScopedNormalized; });
    FempakInput ScopedInput;
    ScopedInput.path = ScopedNormalized;
    ScopedInput.bytes = std::move(InBytes);
    ScopedInput.alignment = std::max(InAlignment, 1u);
    if (ScopedExisting != MEntries.end())
    {
        *ScopedExisting = std::move(ScopedInput);
        return;
    }
    MEntries.push_back(std::move(ScopedInput));
}

bool FempakWriter::AddDirectory(std::string_view InDirectory, std::string_view InVirtualRoot, std::string* InError)
{
    namespace fs = std::filesystem;
    std::error_code ScopedCode;
    const fs::path ScopedRoot{std::string(InDirectory)};
    if (!fs::is_directory(ScopedRoot, ScopedCode))
    {
        SetError(InError, "not a directory: " + std::string(InDirectory));
        return false;
    }

    for (const fs::directory_entry& ScopedItem :
         fs::recursive_directory_iterator(ScopedRoot, fs::directory_options::skip_permission_denied, ScopedCode))
    {
        if (!ScopedItem.is_regular_file(ScopedCode))
        {
            continue;
        }
        const fs::path ScopedRelative = fs::relative(ScopedItem.path(), ScopedRoot, ScopedCode);
        if (ScopedCode || ScopedRelative.empty())
        {
            continue;
        }
        std::vector<std::byte> ScopedBytes;
        if (!ReadFileBytes(ScopedItem.path(), &ScopedBytes))
        {
            SetError(InError, "cannot read " + ScopedItem.path().string());
            return false;
        }
        Add(JoinVirtual(InVirtualRoot, ScopedRelative.generic_string()), std::move(ScopedBytes));
    }
    return true;
}

std::vector<std::byte> FempakWriter::Build(std::string* InError) const
{
    std::vector<const FempakInput*> ScopedSorted;
    ScopedSorted.reserve(MEntries.size());
    for (const FempakInput& ScopedEntry : MEntries)
    {
        ScopedSorted.push_back(&ScopedEntry);
    }
    std::sort(ScopedSorted.begin(), ScopedSorted.end(),
              [](const FempakInput* InLeft, const FempakInput* InRight) { return InLeft->path < InRight->path; });

    if (ScopedSorted.size() > std::numeric_limits<std::uint32_t>::max())
    {
        SetError(InError, "too many entries for a .fempak");
        return {};
    }

    std::uint64_t ScopedStringBytes = 0;
    for (const FempakInput* ScopedEntry : ScopedSorted)
    {
        if (ScopedEntry->path.size() > std::numeric_limits<std::uint32_t>::max())
        {
            SetError(InError, "path is too long: " + ScopedEntry->path);
            return {};
        }
        ScopedStringBytes += ScopedEntry->path.size();
    }

    const std::uint64_t ScopedEntryTableOffset = sizeof(PakHeader);
    const std::uint64_t ScopedStringTableOffset = ScopedEntryTableOffset + ScopedSorted.size() * sizeof(PakEntry);
    const std::uint64_t ScopedStringTableEnd = ScopedStringTableOffset + ScopedStringBytes;
    const std::uint64_t ScopedDataOffset = AlignUp(ScopedStringTableEnd, kPakAlignment);

    std::vector<std::uint64_t> ScopedPayloadOffsets(ScopedSorted.size(), 0);
    std::uint64_t ScopedCursor = ScopedDataOffset;
    for (std::size_t ScopedI = 0; ScopedI < ScopedSorted.size(); ++ScopedI)
    {
        ScopedCursor = AlignUp(ScopedCursor, std::max<std::uint64_t>(ScopedSorted[ScopedI]->alignment, 1));
        ScopedPayloadOffsets[ScopedI] = ScopedCursor;
        ScopedCursor += ScopedSorted[ScopedI]->bytes.size();
        // Uint64 overflow
        if (ScopedCursor < ScopedPayloadOffsets[ScopedI])
        {
            SetError(InError, "archive is too large");
            return {};
        }
    }
    const std::uint64_t ScopedTotalSize = ScopedCursor;
    if (ScopedTotalSize > std::numeric_limits<std::size_t>::max())
    {
        SetError(InError, "archive is too large for this platform");
        return {};
    }

    std::vector<std::byte> ScopedOut(static_cast<std::size_t>(ScopedTotalSize), std::byte{0});

    PakHeader ScopedHeader{};
    std::memcpy(ScopedHeader.magic, kPakMagic, sizeof(kPakMagic));
    ScopedHeader.version = kPakVersion;
    ScopedHeader.entryCount = static_cast<std::uint32_t>(ScopedSorted.size());
    ScopedHeader.entryTableOffset = ScopedEntryTableOffset;
    ScopedHeader.stringTableOffset = ScopedStringTableOffset;
    ScopedHeader.dataOffset = ScopedDataOffset;
    std::memcpy(ScopedOut.data(), &ScopedHeader, sizeof(ScopedHeader));

    std::uint32_t ScopedPathCursor = 0;
    for (std::size_t ScopedI = 0; ScopedI < ScopedSorted.size(); ++ScopedI)
    {
        const FempakInput& ScopedInput = *ScopedSorted[ScopedI];
        PakEntry ScopedEntry{};
        ScopedEntry.pathOffset = ScopedPathCursor;
        ScopedEntry.pathSize = static_cast<std::uint32_t>(ScopedInput.path.size());
        ScopedEntry.offset = ScopedPayloadOffsets[ScopedI];
        ScopedEntry.size = ScopedInput.bytes.size();
        std::memcpy(ScopedOut.data() + ScopedEntryTableOffset + ScopedI * sizeof(PakEntry), &ScopedEntry,
                    sizeof(ScopedEntry));
        std::memcpy(ScopedOut.data() + ScopedStringTableOffset + ScopedPathCursor, ScopedInput.path.data(),
                    ScopedInput.path.size());
        ScopedPathCursor += ScopedEntry.pathSize;
        if (!ScopedInput.bytes.empty())
        {
            std::memcpy(ScopedOut.data() + ScopedEntry.offset, ScopedInput.bytes.data(), ScopedInput.bytes.size());
        }
    }

    return ScopedOut;
}

bool WriteFileAtomically(std::string_view InPath, std::span<const std::byte> InBytes, std::string* InError)
{
    namespace fs = std::filesystem;
    const fs::path ScopedTarget{std::string(InPath)};
    fs::path ScopedTemporary = ScopedTarget;
    ScopedTemporary += ".tmp";

    {
        std::error_code ScopedCode;
        if (ScopedTarget.has_parent_path())
        {
            fs::create_directories(ScopedTarget.parent_path(), ScopedCode);
        }
        std::ofstream ScopedStream(ScopedTemporary, std::ios::binary | std::ios::trunc);
        if (!ScopedStream)
        {
            SetError(InError, "cannot write " + ScopedTemporary.string());
            return false;
        }
        ScopedStream.write(reinterpret_cast<const char*>(InBytes.data()), static_cast<std::streamsize>(InBytes.size()));
        ScopedStream.flush();
        if (!ScopedStream)
        {
            SetError(InError, "write failed: " + ScopedTemporary.string());
            return false;
        }
    }

#if defined(_WIN32)
    if (!::MoveFileExW(ScopedTemporary.c_str(), ScopedTarget.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        SetError(InError, "cannot replace " + ScopedTarget.string());
        std::error_code ScopedIgnored;
        fs::remove(ScopedTemporary, ScopedIgnored);
        return false;
    }
#else
    std::error_code ScopedCode;
    fs::rename(ScopedTemporary, ScopedTarget, ScopedCode);
    if (ScopedCode)
    {
        SetError(InError, "cannot replace " + ScopedTarget.string() + ": " + ScopedCode.message());
        fs::remove(ScopedTemporary, ScopedCode);
        return false;
    }
#endif
    return true;
}
}
