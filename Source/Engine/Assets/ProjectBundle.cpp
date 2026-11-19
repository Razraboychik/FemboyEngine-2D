// FemboyEngine2D - portable source-only project archive.
#include "Engine/Assets/ProjectBundle.hpp"

#if defined(_WIN32)
#include <windows.h>
#endif

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <map>
#include <span>
#include <system_error>
#include <vector>

namespace Fby::Assets
{
namespace
{
constexpr char kMagic[8] = {'F', 'B', 'Y', 'P', 'R', 'O', 'J', '1'};
constexpr std::uint32_t kVersion = 1;
constexpr std::uint64_t kHeaderBytes = 24;
constexpr std::uint64_t kEntryBytes = 32;

struct Entry
{
    std::string Path;
    std::vector<std::byte> Data;
    std::uint32_t PathOffset = 0;
    std::uint32_t DataOffset = 0;
    std::uint32_t Checksum = 0;
};

bool HasSymlinkComponent(const std::filesystem::path& InRoot, const std::filesystem::path& InRelative)
{
    std::filesystem::path ScopedCurrent = InRoot;
    for (const std::filesystem::path& ScopedPart : InRelative)
    {
        ScopedCurrent /= ScopedPart;
        std::error_code ScopedError;
        const std::filesystem::file_status ScopedStatus = std::filesystem::symlink_status(ScopedCurrent, ScopedError);
        if (ScopedError)
        {
            if (ScopedError == std::errc::no_such_file_or_directory)
            {
                return false;
            }
            return true;
        }
        if (ScopedStatus.type() == std::filesystem::file_type::not_found)
        {
            return false;
        }
        if (std::filesystem::is_symlink(ScopedStatus))
        {
            return true;
        }
    }
    return false;
}

void SetError(std::string* OutError, std::string InText)
{
    if (OutError != nullptr)
    {
        *OutError = std::move(InText);
    }
}

[[nodiscard]] std::uint32_t Checksum(std::span<const std::byte> InBytes) noexcept
{
    std::uint32_t ScopedHash = 2166136261u;
    for (const std::byte ScopedByte : InBytes)
    {
        ScopedHash ^= static_cast<std::uint8_t>(ScopedByte);
        ScopedHash *= 16777619u;
    }
    return ScopedHash;
}

void Put32(std::vector<std::byte>& OutBytes, std::uint32_t InValue)
{
    for (std::uint32_t ScopedI = 0; ScopedI < 4; ++ScopedI)
    {
        OutBytes.push_back(static_cast<std::byte>((InValue >> (ScopedI * 8)) & 0xFFu));
    }
}

void Put64(std::vector<std::byte>& OutBytes, std::uint64_t InValue)
{
    for (std::uint32_t ScopedI = 0; ScopedI < 8; ++ScopedI)
    {
        OutBytes.push_back(static_cast<std::byte>((InValue >> (ScopedI * 8)) & 0xFFu));
    }
}

[[nodiscard]] bool Get32(std::span<const std::byte> InBytes, std::size_t InOffset, std::uint32_t* OutValue) noexcept
{
    if (InOffset + 4 > InBytes.size())
    {
        return false;
    }
    std::uint32_t ScopedValue = 0;
    for (std::uint32_t ScopedI = 0; ScopedI < 4; ++ScopedI)
    {
        ScopedValue |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(InBytes[InOffset + ScopedI]))
                       << (ScopedI * 8);
    }
    *OutValue = ScopedValue;
    return true;
}

[[nodiscard]] bool Get64(std::span<const std::byte> InBytes, std::size_t InOffset, std::uint64_t* OutValue) noexcept
{
    if (InOffset + 8 > InBytes.size())
    {
        return false;
    }
    std::uint64_t ScopedValue = 0;
    for (std::uint32_t ScopedI = 0; ScopedI < 8; ++ScopedI)
    {
        ScopedValue |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(InBytes[InOffset + ScopedI]))
                       << (ScopedI * 8);
    }
    *OutValue = ScopedValue;
    return true;
}

[[nodiscard]] bool IsExcluded(const std::filesystem::path& InRelative)
{
    for (const std::filesystem::path& ScopedPart : InRelative)
    {
        const std::string ScopedName = ScopedPart.string();
        if (ScopedName == ".git" || ScopedName == "build" || ScopedName == "Saved" || ScopedName == ".fbyproject" ||
            ScopedName == "archives" || ScopedName == "stores" || ScopedName == ".cache" ||
            ScopedName == "editor.ini" || ScopedName == ".fbyrelease.seed")
        {
            return true;
        }
    }
    const std::string ScopedExtension = InRelative.extension().string();
    return ScopedExtension == ".fempak" || ScopedExtension == ".ftex" || ScopedExtension == ".fpcm" ||
           ScopedExtension == ".fbin" || ScopedExtension == ".obj" || ScopedExtension == ".pdb";
}

[[nodiscard]] bool ReadAll(const std::filesystem::path& InPath, std::vector<std::byte>* OutBytes)
{
    std::ifstream ScopedStream(InPath, std::ios::binary | std::ios::ate);
    if (!ScopedStream)
    {
        return false;
    }
    const std::streamsize ScopedSize = ScopedStream.tellg();
    if (ScopedSize < 0)
    {
        return false;
    }
    ScopedStream.seekg(0, std::ios::beg);
    OutBytes->resize(static_cast<std::size_t>(ScopedSize));
    return ScopedSize == 0 || ScopedStream.read(reinterpret_cast<char*>(OutBytes->data()), ScopedSize).good();
}

[[nodiscard]] std::string ManifestText(std::size_t InFiles)
{
    std::string ScopedText = "# FemboyEngine2D portable project\nformat = 1\n";
    ScopedText += "descriptor = \"";
    ScopedText += kProjectDescriptorName;
    ScopedText += "\"\nfiles = ";
    ScopedText += std::to_string(InFiles);
    ScopedText += "\n";
    return ScopedText;
}
}

bool ExportProjectBundle(const std::filesystem::path& InProjectRoot, const std::filesystem::path& InArchive,
                         std::string* InError)
{
    std::error_code ScopedError;
    const std::filesystem::path ScopedRoot = std::filesystem::weakly_canonical(InProjectRoot, ScopedError);
    if (ScopedError || !std::filesystem::is_directory(ScopedRoot, ScopedError))
    {
        SetError(InError, "project bundle: project root is not a directory");
        return false;
    }
    const std::filesystem::path ScopedDescriptor = ScopedRoot / kProjectDescriptorName;
    const std::filesystem::file_status ScopedDescriptorStatus = std::filesystem::symlink_status(ScopedDescriptor, ScopedError);
    if (ScopedError || !std::filesystem::is_regular_file(ScopedDescriptorStatus))
    {
        SetError(InError, "project bundle: project.femproj is missing or not a regular file");
        return false;
    }

    std::vector<Entry> ScopedEntries;
    std::error_code ScopedScanError;
    auto ScopedIt = std::filesystem::recursive_directory_iterator(
        ScopedRoot, std::filesystem::directory_options::skip_permission_denied, ScopedScanError);
    const std::filesystem::recursive_directory_iterator ScopedEnd;
    for (; ScopedIt != ScopedEnd; ScopedIt.increment(ScopedScanError))
    {
        if (ScopedScanError)
        {
            SetError(InError, "project bundle: cannot enumerate " + ScopedRoot.string() + ": " + ScopedScanError.message());
            return false;
        }
        const std::filesystem::directory_entry& ScopedEntry = *ScopedIt;
        std::error_code ScopedEntryError;
        if (ScopedEntry.is_symlink(ScopedEntryError) || !ScopedEntry.is_regular_file(ScopedEntryError) || ScopedEntryError)
        {
            continue;
        }
        const std::filesystem::path ScopedRelative =
            std::filesystem::relative(ScopedEntry.path(), ScopedRoot, ScopedEntryError);
        if (ScopedEntryError)
        {
            SetError(InError, "project bundle: cannot resolve " + ScopedEntry.path().string() + ": " +
                                  ScopedEntryError.message());
            return false;
        }
        if (IsExcluded(ScopedRelative))
        {
            continue;
        }
        Entry ScopedFile;
        ScopedFile.Path = ScopedRelative.generic_string();
        if (!ReadAll(ScopedEntry.path(), &ScopedFile.Data))
        {
            SetError(InError, "project bundle: cannot read " + ScopedFile.Path);
            return false;
        }
        ScopedFile.Checksum = Checksum(ScopedFile.Data);
        ScopedEntries.push_back(std::move(ScopedFile));
    }
    if (ScopedError)
    {
        SetError(InError, "project bundle: cannot walk project: " + ScopedError.message());
        return false;
    }

    // Archive order must not depend on directory enumeration. Case-folded
    // duplicates are rejected because a Windows or macOS checkout would merge
    // two distinct Linux paths.
    std::sort(ScopedEntries.begin(), ScopedEntries.end(),
              [](const Entry& InLeft, const Entry& InRight) { return InLeft.Path < InRight.Path; });
    std::map<std::string, std::string> ScopedFoldedPaths;
    for (const Entry& ScopedEntry : ScopedEntries)
    {
        std::string ScopedFolded = ScopedEntry.Path;
        std::transform(ScopedFolded.begin(), ScopedFolded.end(), ScopedFolded.begin(),
                       [](unsigned char InCharacter) { return static_cast<char>(std::tolower(InCharacter)); });
        const auto ScopedInserted = ScopedFoldedPaths.emplace(ScopedFolded, ScopedEntry.Path);
        if (!ScopedInserted.second)
        {
            SetError(InError, "project bundle: case-colliding paths: " + ScopedInserted.first->second + " and " +
                                  ScopedEntry.Path);
            return false;
        }
    }

    Entry ScopedManifest;
    ScopedManifest.Path = "manifest.toml";
    const std::string ScopedManifestText = ManifestText(ScopedEntries.size());
    ScopedManifest.Data.assign(reinterpret_cast<const std::byte*>(ScopedManifestText.data()),
                               reinterpret_cast<const std::byte*>(ScopedManifestText.data()) + ScopedManifestText.size());
    ScopedManifest.Checksum = Checksum(ScopedManifest.Data);
    ScopedEntries.insert(ScopedEntries.begin(), std::move(ScopedManifest));

    std::vector<std::byte> ScopedPaths;
    for (Entry& ScopedEntry : ScopedEntries)
    {
        ScopedEntry.PathOffset = static_cast<std::uint32_t>(ScopedPaths.size());
        for (const char ScopedCharacter : ScopedEntry.Path)
        {
            ScopedPaths.push_back(static_cast<std::byte>(ScopedCharacter));
        }
    }

    const std::uint64_t ScopedPathTable = kHeaderBytes + kEntryBytes * ScopedEntries.size();
    std::uint64_t ScopedDataOffset = ScopedPathTable + ScopedPaths.size();
    std::vector<std::byte> ScopedIndex;
    for (Entry& ScopedEntry : ScopedEntries)
    {
        ScopedEntry.DataOffset = static_cast<std::uint32_t>(ScopedDataOffset);
        Put32(ScopedIndex, ScopedEntry.PathOffset);
        Put32(ScopedIndex, static_cast<std::uint32_t>(ScopedEntry.Path.size()));
        Put64(ScopedIndex, ScopedDataOffset);
        Put64(ScopedIndex, static_cast<std::uint64_t>(ScopedEntry.Data.size()));
        Put32(ScopedIndex, ScopedEntry.Checksum);
        Put32(ScopedIndex, 0);
        ScopedDataOffset += ScopedEntry.Data.size();
    }

    std::vector<std::byte> ScopedArchive;
    for (const char ScopedCharacter : kMagic)
    {
        ScopedArchive.push_back(static_cast<std::byte>(ScopedCharacter));
    }
    Put32(ScopedArchive, kVersion);
    Put32(ScopedArchive, static_cast<std::uint32_t>(ScopedEntries.size()));
    Put64(ScopedArchive, kHeaderBytes);
    ScopedArchive.insert(ScopedArchive.end(), ScopedIndex.begin(), ScopedIndex.end());
    ScopedArchive.insert(ScopedArchive.end(), ScopedPaths.begin(), ScopedPaths.end());
    for (const Entry& ScopedEntry : ScopedEntries)
    {
        ScopedArchive.insert(ScopedArchive.end(), ScopedEntry.Data.begin(), ScopedEntry.Data.end());
    }

    std::error_code ScopedParentError;
    if (!InArchive.parent_path().empty())
    {
        std::filesystem::create_directories(InArchive.parent_path(), ScopedParentError);
    }
    std::filesystem::path ScopedTemp = InArchive;
    ScopedTemp += ".tmp";
    std::ofstream ScopedStream(ScopedTemp, std::ios::binary | std::ios::trunc);
    if (!ScopedStream)
    {
        SetError(InError, "project bundle: cannot write " + ScopedTemp.string());
        return false;
    }
    ScopedStream.write(reinterpret_cast<const char*>(ScopedArchive.data()),
                       static_cast<std::streamsize>(ScopedArchive.size()));
    ScopedStream.flush();
    if (!ScopedStream.good())
    {
        SetError(InError, "project bundle: write failed");
        return false;
    }
    ScopedStream.close();
    if (ScopedStream.fail())
    {
        std::filesystem::remove(ScopedTemp, ScopedError);
        SetError(InError, "project bundle: close failed");
        return false;
    }
#if defined(_WIN32)
    if (!MoveFileExW(ScopedTemp.c_str(), InArchive.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        std::filesystem::remove(ScopedTemp, ScopedError);
        SetError(InError, "project bundle: cannot replace the target archive");
        return false;
    }
#else
    std::filesystem::rename(ScopedTemp, InArchive, ScopedError);
    if (ScopedError)
    {
        std::filesystem::remove(ScopedTemp, ScopedError);
        SetError(InError, "project bundle: cannot replace the target archive");
        return false;
    }
#endif
    return true;
}

bool ImportProjectBundle(const std::filesystem::path& InArchive, const std::filesystem::path& InProjectRoot,
                         bool InOverwrite, std::string* InError)
{
    std::vector<std::byte> ScopedBytes;
    if (!ReadAll(InArchive, &ScopedBytes))
    {
        SetError(InError, "project bundle: cannot read archive");
        return false;
    }
    if (ScopedBytes.size() < kHeaderBytes || std::memcmp(ScopedBytes.data(), kMagic, sizeof(kMagic)) != 0)
    {
        SetError(InError, "project bundle: bad magic");
        return false;
    }
    std::uint32_t ScopedVersion = 0;
    std::uint32_t ScopedCount = 0;
    std::uint64_t ScopedTable = 0;
    if (!Get32(ScopedBytes, 8, &ScopedVersion) || ScopedVersion != kVersion ||
        !Get32(ScopedBytes, 12, &ScopedCount) || !Get64(ScopedBytes, 16, &ScopedTable) ||
        ScopedTable < kHeaderBytes || ScopedTable > ScopedBytes.size() ||
        ScopedCount > (ScopedBytes.size() - ScopedTable) / kEntryBytes)
    {
        SetError(InError, "project bundle: corrupt header");
        return false;
    }

    std::error_code ScopedError;
    const bool ScopedRootExists = std::filesystem::exists(InProjectRoot, ScopedError);
    if (ScopedError)
    {
        SetError(InError, "project bundle: cannot inspect destination");
        return false;
    }
    if (ScopedRootExists && !InOverwrite)
    {
        const bool ScopedEmpty = std::filesystem::is_empty(InProjectRoot, ScopedError);
        if (ScopedError)
        {
            SetError(InError, "project bundle: cannot inspect destination");
            return false;
        }
        if (!ScopedEmpty)
        {
            SetError(InError, "project bundle: destination is not empty");
            return false;
        }
    }
    std::error_code ScopedRootStatusError;
    const std::filesystem::file_status ScopedRootStatus =
        std::filesystem::symlink_status(InProjectRoot, ScopedRootStatusError);
    if (!ScopedRootStatusError && std::filesystem::is_symlink(ScopedRootStatus))
    {
        SetError(InError, "project bundle: destination is a symlink");
        return false;
    }
    std::filesystem::create_directories(InProjectRoot, ScopedError);
    if (ScopedError)
    {
        SetError(InError, "project bundle: cannot create destination");
        return false;
    }

    const std::uint64_t ScopedPathTable = ScopedTable + kEntryBytes * ScopedCount;
    bool ScopedHasDescriptor = false;
    struct ValidatedEntry
    {
        std::string Path;
        std::span<const std::byte> Data;
    };
    std::vector<ValidatedEntry> ScopedValidated;
    ScopedValidated.reserve(ScopedCount);
    for (std::uint32_t ScopedI = 0; ScopedI < ScopedCount; ++ScopedI)
    {
        const std::size_t ScopedEntryOffset = static_cast<std::size_t>(ScopedTable + kEntryBytes * ScopedI);
        std::uint32_t ScopedPathOffset = 0;
        std::uint32_t ScopedPathSize = 0;
        std::uint64_t ScopedDataOffset = 0;
        std::uint64_t ScopedDataSize = 0;
        std::uint32_t ScopedChecksum = 0;
        std::uint32_t ScopedReserved = 0;
        if (!Get32(ScopedBytes, ScopedEntryOffset, &ScopedPathOffset) ||
            !Get32(ScopedBytes, ScopedEntryOffset + 4, &ScopedPathSize) ||
            !Get64(ScopedBytes, ScopedEntryOffset + 8, &ScopedDataOffset) ||
            !Get64(ScopedBytes, ScopedEntryOffset + 16, &ScopedDataSize) ||
            !Get32(ScopedBytes, ScopedEntryOffset + 24, &ScopedChecksum) ||
            !Get32(ScopedBytes, ScopedEntryOffset + 28, &ScopedReserved) ||
            ScopedPathTable + ScopedPathOffset > ScopedBytes.size() ||
            ScopedPathSize > ScopedBytes.size() - (ScopedPathTable + ScopedPathOffset) ||
            ScopedDataOffset > ScopedBytes.size() || ScopedDataSize > ScopedBytes.size() - ScopedDataOffset)
        {
            SetError(InError, "project bundle: corrupt entry");
            return false;
        }
        const std::string ScopedPath(
            reinterpret_cast<const char*>(ScopedBytes.data() + ScopedPathTable + ScopedPathOffset), ScopedPathSize);
        const std::span<const std::byte> ScopedData(ScopedBytes.data() + ScopedDataOffset,
                                                    static_cast<std::size_t>(ScopedDataSize));
        if (Checksum(ScopedData) != ScopedChecksum)
        {
            SetError(InError, "project bundle: checksum mismatch for " + ScopedPath);
            return false;
        }
        const std::filesystem::path ScopedRelative(ScopedPath);
        if (ScopedRelative.is_absolute() || ScopedRelative.has_root_name() ||
            ScopedRelative.has_root_directory() || ScopedPath.find('\0') != std::string::npos)
        {
            SetError(InError, "project bundle: absolute path rejected");
            return false;
        }
        for (const std::filesystem::path& ScopedPart : ScopedRelative)
        {
            if (ScopedPart == ".." || ScopedPart == ".")
            {
                SetError(InError, "project bundle: traversal rejected");
                return false;
            }
        }
        if (ScopedPath == kProjectDescriptorName)
        {
            ScopedHasDescriptor = true;
        }
        ScopedValidated.push_back(ValidatedEntry{ScopedPath, ScopedData});
    }
    if (!ScopedHasDescriptor)
    {
        SetError(InError, "project bundle: descriptor is missing");
        return false;
    }
    for (const ValidatedEntry& ScopedEntry : ScopedValidated)
    {
        const std::filesystem::path ScopedRelative = std::filesystem::path(ScopedEntry.Path);
        if (HasSymlinkComponent(InProjectRoot, ScopedRelative))
        {
            SetError(InError, "project bundle: symlink component rejected for " + ScopedEntry.Path);
            return false;
        }
        const std::filesystem::path ScopedDestination = InProjectRoot / ScopedRelative;
        std::filesystem::create_directories(ScopedDestination.parent_path(), ScopedError);
        if (ScopedError)
        {
            SetError(InError, "project bundle: cannot create directory for " + ScopedEntry.Path);
            return false;
        }
        std::ofstream ScopedFile(ScopedDestination, std::ios::binary | std::ios::trunc);
        if (!ScopedFile)
        {
            SetError(InError, "project bundle: cannot write " + ScopedEntry.Path);
            return false;
        }
        ScopedFile.write(reinterpret_cast<const char*>(ScopedEntry.Data.data()),
                         static_cast<std::streamsize>(ScopedEntry.Data.size()));
        ScopedFile.flush();
        if (!ScopedFile.good())
        {
            SetError(InError, "project bundle: write failed for " + ScopedEntry.Path);
            return false;
        }
        ScopedFile.close();
        if (ScopedFile.fail())
        {
            SetError(InError, "project bundle: close failed for " + ScopedEntry.Path);
            return false;
        }
    }
    return true;
}

}
