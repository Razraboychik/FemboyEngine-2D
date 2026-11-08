// FemboyEngine2D - portable source-only project archive.
#include "Engine/Assets/ProjectBundle.hpp"

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
            ScopedName == "archives" || ScopedName == "stores")
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
    if (ScopedError || !std::filesystem::is_directory(ScopedRoot))
    {
        SetError(InError, "project bundle: project root is not a directory");
        return false;
    }
    if (!std::filesystem::is_regular_file(ScopedRoot / kProjectDescriptorName))
    {
        SetError(InError, "project bundle: project.femproj is missing");
        return false;
    }

    std::vector<Entry> ScopedEntries;
    for (std::filesystem::recursive_directory_iterator ScopedIt(ScopedRoot, ScopedError), ScopedEnd;
         !ScopedError && ScopedIt != ScopedEnd; ScopedIt.increment(ScopedError))
    {
        const std::filesystem::directory_entry& ScopedEntry = *ScopedIt;
        if (ScopedEntry.is_symlink(ScopedError) || !ScopedEntry.is_regular_file(ScopedError))
        {
            continue;
        }
        const std::filesystem::path ScopedRelative =
            std::filesystem::relative(ScopedEntry.path(), ScopedRoot, ScopedError);
        if (ScopedError || IsExcluded(ScopedRelative))
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
    std::ofstream ScopedStream(InArchive, std::ios::binary | std::ios::trunc);
    if (!ScopedStream)
    {
        SetError(InError, "project bundle: cannot write " + InArchive.string());
        return false;
    }
    ScopedStream.write(reinterpret_cast<const char*>(ScopedArchive.data()),
                       static_cast<std::streamsize>(ScopedArchive.size()));
    if (!ScopedStream.good())
    {
        SetError(InError, "project bundle: write failed");
        return false;
    }
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
        ScopedTable < kHeaderBytes || ScopedTable + kEntryBytes * ScopedCount > ScopedBytes.size())
    {
        SetError(InError, "project bundle: corrupt header");
        return false;
    }

    std::error_code ScopedError;
    if (std::filesystem::exists(InProjectRoot, ScopedError) && !InOverwrite &&
        !std::filesystem::is_empty(InProjectRoot, ScopedError))
    {
        SetError(InError, "project bundle: destination is not empty");
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
        if (ScopedRelative.is_absolute() || ScopedPath.find('\0') != std::string::npos)
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
        const std::filesystem::path ScopedDestination = InProjectRoot / ScopedRelative;
        std::filesystem::create_directories(ScopedDestination.parent_path(), ScopedError);
        std::ofstream ScopedFile(ScopedDestination, std::ios::binary | std::ios::trunc);
        if (!ScopedFile)
        {
            SetError(InError, "project bundle: cannot write " + ScopedPath);
            return false;
        }
        ScopedFile.write(reinterpret_cast<const char*>(ScopedData.data()),
                         static_cast<std::streamsize>(ScopedData.size()));
        if (!ScopedFile.good())
        {
            SetError(InError, "project bundle: write failed for " + ScopedPath);
            return false;
        }
        if (ScopedPath == kProjectDescriptorName)
        {
            ScopedHasDescriptor = true;
        }
    }
    if (!ScopedHasDescriptor)
    {
        SetError(InError, "project bundle: descriptor is missing");
        return false;
    }
    return true;
}

}
