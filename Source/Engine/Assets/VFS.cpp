#include "Engine/Assets/VFS.hpp"

#include "Engine/Assets/PakFormat.hpp"
#include "Engine/Core/Log.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace Fby
{
namespace
{
using Assets::IsRangeInside;
using Assets::kPakMagic;
using Assets::kPakVersion;
using Assets::PakEntry;
using Assets::PakHeader;

// One definition of the format, shared with the cooker. VFS only ever reads it.

// A writable overlay must not escape its mount root. Normalize() intentionally
[[nodiscard]] bool HasParentTraversal(std::string_view InPath) noexcept
{
    std::size_t ScopedStart = 0;
    while (ScopedStart <= InPath.size())
    {
        const std::size_t ScopedSlash = InPath.find('/', ScopedStart);
        const std::string_view ScopedSegment = InPath.substr(
            ScopedStart, ScopedSlash == std::string_view::npos ? std::string_view::npos : ScopedSlash - ScopedStart);
        if (ScopedSegment == "..")
        {
            return true;
        }
        if (ScopedSlash == std::string_view::npos)
        {
            break;
        }
        ScopedStart = ScopedSlash + 1;
    }
    return false;
}
}

// MappedFile
MappedFile::~MappedFile()
{
    Close();
}

MappedFile::MappedFile(MappedFile&& InOther) noexcept
{
    *this = std::move(InOther);
}

MappedFile& MappedFile::operator=(MappedFile&& InOther) noexcept
{
    if (this == &InOther)
    {
        return *this;
    }
    Close();
    MData = std::exchange(InOther.MData, nullptr);
    MSize = std::exchange(InOther.MSize, 0);
#if defined(_WIN32)
    MFile = std::exchange(InOther.MFile, nullptr);
    MMapping = std::exchange(InOther.MMapping, nullptr);
#else
    MFileDescriptor = std::exchange(InOther.MFileDescriptor, -1);
#endif
    return *this;
}

void MappedFile::Close() noexcept
{
#if defined(_WIN32)
    if (MData != nullptr)
    {
        ::UnmapViewOfFile(MData);
    }
    if (MMapping != nullptr)
    {
        ::CloseHandle(static_cast<HANDLE>(MMapping));
    }
    if (MFile != nullptr)
    {
        ::CloseHandle(static_cast<HANDLE>(MFile));
    }
    MData = nullptr;
    MMapping = nullptr;
    MFile = nullptr;
    MSize = 0;
#else
    if (MData != nullptr)
    {
        ::munmap(MData, MSize);
    }
    if (MFileDescriptor >= 0)
    {
        ::close(MFileDescriptor);
    }
    MData = nullptr;
    MFileDescriptor = -1;
    MSize = 0;
#endif
}

std::unique_ptr<MappedFile> MappedFile::Open(std::string_view InPath, std::string* InError)
{
    const auto ScopedFail = [InError](std::string InMessage)
    {
        if (InError != nullptr)
        {
            *InError = std::move(InMessage);
        }
        return nullptr;
    };

    auto ScopedFile = std::make_unique<MappedFile>();
    const std::filesystem::path ScopedFilePath{std::string(InPath)};

#if defined(_WIN32)
    // Loose assets are hot-reload inputs: an editor, a compiler or a cook must
    const HANDLE ScopedHandle =
        ::CreateFileW(ScopedFilePath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                      nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (ScopedHandle == INVALID_HANDLE_VALUE)
    {
        return ScopedFail("cannot open " + std::string(InPath));
    }
    LARGE_INTEGER ScopedSize{};
    if (!::GetFileSizeEx(ScopedHandle, &ScopedSize) || ScopedSize.QuadPart <= 0)
    {
        ::CloseHandle(ScopedHandle);
        return ScopedFail("empty or unreadable file: " + std::string(InPath));
    }
    const HANDLE ScopedMapping = ::CreateFileMappingW(ScopedHandle, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (ScopedMapping == nullptr)
    {
        ::CloseHandle(ScopedHandle);
        return ScopedFail("CreateFileMapping failed: " + std::string(InPath));
    }
    void* ScopedView = ::MapViewOfFile(ScopedMapping, FILE_MAP_READ, 0, 0, 0);
    if (ScopedView == nullptr)
    {
        ::CloseHandle(ScopedMapping);
        ::CloseHandle(ScopedHandle);
        return ScopedFail("MapViewOfFile failed: " + std::string(InPath));
    }
    ScopedFile->MData = ScopedView;
    ScopedFile->MSize = static_cast<std::size_t>(ScopedSize.QuadPart);
    ScopedFile->MFile = ScopedHandle;
    ScopedFile->MMapping = ScopedMapping;
#else
    const int descriptor = ::open(std::string(InPath).c_str(), O_RDONLY);
    if (descriptor < 0)
    {
        return ScopedFail("cannot open " + std::string(InPath));
    }
    struct stat status{};
    if (::fstat(descriptor, &status) != 0 || status.st_size <= 0)
    {
        ::close(descriptor);
        return ScopedFail("empty or unreadable file: " + std::string(InPath));
    }
    void* view = ::mmap(nullptr, static_cast<std::size_t>(status.st_size), PROT_READ, MAP_PRIVATE, descriptor, 0);
    if (view == MAP_FAILED)
    {
        ::close(descriptor);
        return ScopedFail("mmap failed: " + std::string(InPath));
    }
    ScopedFile->MData = view;
    ScopedFile->MSize = static_cast<std::size_t>(status.st_size);
    // The mapping keeps the file alive; the descriptor is not needed.
    ::close(descriptor);
#endif

    return ScopedFile;
}

// VirtualFileSystem
VirtualFileSystem::~VirtualFileSystem() = default;

std::string VirtualFileSystem::Normalize(std::string_view InPath)
{
    return Assets::NormalizePakPath(InPath);
}

bool VirtualFileSystem::AddEntry(std::string InPath, VfsEntry InEntry, std::string* InError)
{
    const auto ScopedExisting = MEntries.find(InPath);
    if (ScopedExisting != MEntries.end())
    {
        // Higher priority wins; on a tie the later mount wins (mods load after
        if (ScopedExisting->second.priority > InEntry.priority)
        {
            return true;
        }
        ScopedExisting->second = InEntry;
        return true;
    }
    MEntries.insert_or_assign(std::move(InPath), InEntry);
    (void)InError;
    return true;
}

bool VirtualFileSystem::MountDirectory(std::string_view InRoot, int InPriority, std::string* InError)
{
    namespace fs = std::filesystem;
    std::error_code ScopedCode;
    const fs::path ScopedRootPath{std::string(InRoot)};
    if (!fs::is_directory(ScopedRootPath, ScopedCode))
    {
        if (InError != nullptr)
        {
            *InError = "not a directory: " + std::string(InRoot);
        }
        return false;
    }

    auto ScopedMount = std::make_unique<MountRecord>();
    ScopedMount->id = MNextMountId++;
    ScopedMount->priority = InPriority;
    ScopedMount->root = ScopedRootPath.string();

    const std::uint32_t ScopedMountId = ScopedMount->id;
    std::size_t ScopedAdded = 0;

    for (const fs::directory_entry& ScopedItem :
         fs::recursive_directory_iterator(ScopedRootPath, fs::directory_options::skip_permission_denied, ScopedCode))
    {
        if (!ScopedItem.is_regular_file(ScopedCode))
        {
            continue;
        }
        const fs::path ScopedRelative = fs::relative(ScopedItem.path(), ScopedRootPath, ScopedCode);
        if (ScopedCode)
        {
            continue;
        }
        const std::string ScopedKey = Normalize(ScopedRelative.generic_string());
        // Directory mounts are lazy: index the path now, map on first Read().
        AddEntry(ScopedKey, VfsEntry{ScopedMountId, InPriority, {}}, nullptr);
        ++ScopedAdded;
    }

    MMounts.push_back(std::move(ScopedMount));
    LogInfo("vfs", "mounted directory '{}' (priority {}) - {} files", InRoot, InPriority, ScopedAdded);
    return true;
}

void VirtualFileSystem::RefreshDirectoryMounts()
{
    namespace fs = std::filesystem;
    for (const std::unique_ptr<MountRecord>& ScopedMount : MMounts)
    {
        if (ScopedMount->isPak)
        {
            continue;
        }
        std::vector<std::string> ScopedDoomed;
        for (const auto& [ScopedPath, ScopedEntry] : MEntries)
        {
            if (ScopedEntry.mount == ScopedMount->id)
            {
                ScopedDoomed.push_back(ScopedPath);
            }
        }
        for (const std::string& ScopedPath : ScopedDoomed)
        {
            MLazyMappings.erase(ScopedPath);
            MEntries.erase(ScopedPath);
        }
        std::error_code ScopedCode;
        for (const fs::directory_entry& ScopedItem : fs::recursive_directory_iterator(
                 ScopedMount->root, fs::directory_options::skip_permission_denied, ScopedCode))
        {
            if (!ScopedItem.is_regular_file(ScopedCode))
            {
                continue;
            }
            const fs::path ScopedRelative = fs::relative(ScopedItem.path(), ScopedMount->root, ScopedCode);
            if (ScopedCode)
            {
                continue;
            }
            AddEntry(Normalize(ScopedRelative.generic_string()),
                     VfsEntry{ScopedMount->id, ScopedMount->priority, {}}, nullptr);
        }
    }
}

bool VirtualFileSystem::MountPak(std::string_view InPakPath, int InPriority, std::string* InError)
{
    const auto ScopedFail = [InError](std::string InMessage)
    {
        if (InError != nullptr)
        {
            *InError = std::move(InMessage);
        }
        return false;
    };

    std::shared_ptr<MappedFile> ScopedMapping = MappedFile::Open(InPakPath, InError);
    if (!ScopedMapping)
    {
        return false;
    }

    const std::span<const std::byte> ScopedBytes = ScopedMapping->Bytes();
    if (ScopedBytes.size() < sizeof(PakHeader))
    {
        return ScopedFail("pak too small: " + std::string(InPakPath));
    }

    PakHeader ScopedHeader{};
    std::memcpy(&ScopedHeader, ScopedBytes.data(), sizeof(ScopedHeader));
    if (std::memcmp(ScopedHeader.magic, kPakMagic, sizeof(kPakMagic)) != 0)
    {
        return ScopedFail("bad magic: " + std::string(InPakPath));
    }
    if (ScopedHeader.version != kPakVersion)
    {
        return ScopedFail("unsupported pak version " + std::to_string(ScopedHeader.version));
    }
    if (!IsRangeInside(ScopedHeader.entryTableOffset,
                       static_cast<std::uint64_t>(ScopedHeader.entryCount) * sizeof(PakEntry), ScopedBytes.size()) ||
        !IsRangeInside(ScopedHeader.stringTableOffset, 0, ScopedBytes.size()) ||
        !IsRangeInside(ScopedHeader.dataOffset, 0, ScopedBytes.size()))
    {
        return ScopedFail("corrupt pak index: " + std::string(InPakPath));
    }

    auto ScopedMount = std::make_unique<MountRecord>();
    ScopedMount->id = MNextMountId++;
    ScopedMount->priority = InPriority;
    ScopedMount->isPak = true;
    ScopedMount->pak = std::move(ScopedMapping);

    const std::uint32_t ScopedMountId = ScopedMount->id;
    std::size_t ScopedAdded = 0;

    for (std::uint32_t ScopedI = 0; ScopedI < ScopedHeader.entryCount; ++ScopedI)
    {
        PakEntry ScopedEntry{};
        std::memcpy(&ScopedEntry,
                    ScopedBytes.data() + ScopedHeader.entryTableOffset +
                        static_cast<std::size_t>(ScopedI) * sizeof(PakEntry),
                    sizeof(ScopedEntry));

        const std::uint64_t ScopedPathPosition = ScopedHeader.stringTableOffset + ScopedEntry.pathOffset;
        if (!IsRangeInside(ScopedPathPosition, ScopedEntry.pathSize, ScopedBytes.size()))
        {
            continue;
        }
        if (!IsRangeInside(ScopedEntry.offset, ScopedEntry.size, ScopedBytes.size()))
        {
            continue;
        }
        const std::string ScopedPath(reinterpret_cast<const char*>(ScopedBytes.data() + ScopedPathPosition),
                                     ScopedEntry.pathSize);
        const std::span<const std::byte> ScopedData(ScopedBytes.data() + ScopedEntry.offset,
                                                    static_cast<std::size_t>(ScopedEntry.size));
        AddEntry(Normalize(ScopedPath), VfsEntry{ScopedMountId, InPriority, ScopedData}, nullptr);
        ++ScopedAdded;
    }

    LogInfo("vfs", "mounted pak '{}' (priority {}) - {} entries", InPakPath, InPriority, ScopedAdded);
    MMounts.push_back(std::move(ScopedMount));
    return true;
}

const VirtualFileSystem::MountRecord* VirtualFileSystem::FindWritableMount() const noexcept
{
    // Highest priority wins; on a tie the later mount wins, matching AddEntry's
    const MountRecord* ScopedBest = nullptr;
    for (const std::unique_ptr<MountRecord>& ScopedMount : MMounts)
    {
        if (ScopedMount->isPak)
        {
            continue;
        }
        if (ScopedBest == nullptr || ScopedMount->priority >= ScopedBest->priority)
        {
            ScopedBest = ScopedMount.get();
        }
    }
    return ScopedBest;
}

std::optional<std::string> VirtualFileSystem::ResolveWritablePath(std::string_view InPath) const
{
    const MountRecord* ScopedMount = FindWritableMount();
    if (ScopedMount == nullptr)
    {
        return std::nullopt;
    }
    const std::string ScopedKey = Normalize(InPath);
    const std::filesystem::path ScopedRelative(ScopedKey);
    if (ScopedKey.empty() || HasParentTraversal(ScopedKey) || ScopedRelative.is_absolute() ||
        ScopedRelative.has_root_name())
    {
        return std::nullopt;
    }
    const std::filesystem::path ScopedPhysical = std::filesystem::path(ScopedMount->root) / ScopedRelative;
    return ScopedPhysical.string();
}

bool VirtualFileSystem::WriteFile(std::string_view InPath, std::span<const std::byte> InBytes, std::string* InError)
{
    const auto ScopedFail = [InError](std::string InMessage)
    {
        if (InError != nullptr)
        {
            *InError = std::move(InMessage);
        }
        return false;
    };

    const MountRecord* ScopedMount = FindWritableMount();
    if (ScopedMount == nullptr)
    {
        return ScopedFail("VFS is read-only: mount a directory (for example 'assets') to enable asset saving");
    }
    const std::string ScopedKey = Normalize(InPath);
    if (ScopedKey.empty())
    {
        return ScopedFail("cannot write an empty VFS path");
    }
    if (HasParentTraversal(ScopedKey))
    {
        return ScopedFail("refusing to write a VFS path with a '..' segment: " + ScopedKey);
    }

    namespace fs = std::filesystem;
    const fs::path ScopedRelative(ScopedKey);
    if (ScopedRelative.is_absolute() || ScopedRelative.has_root_name())
    {
        return ScopedFail("refusing to write an absolute VFS path: " + ScopedKey);
    }
    std::error_code ScopedCode;
    const fs::path ScopedPhysical = fs::path(ScopedMount->root) / ScopedRelative;
    if (ScopedPhysical.has_parent_path())
    {
        fs::create_directories(ScopedPhysical.parent_path(), ScopedCode);
        if (ScopedCode)
        {
            return ScopedFail("cannot create directory for " + ScopedPhysical.string() + ": " + ScopedCode.message());
        }
    }

    // Write beside the target and replace it in one operation. On Windows the
    const fs::path ScopedTemporary = ScopedPhysical.string() + ".fbtmp";
    {
        std::ofstream ScopedStream(ScopedTemporary, std::ios::binary | std::ios::trunc);
        if (!ScopedStream)
        {
            return ScopedFail("cannot open temporary file " + ScopedTemporary.string());
        }
        ScopedStream.write(reinterpret_cast<const char*>(InBytes.data()), static_cast<std::streamsize>(InBytes.size()));
        ScopedStream.flush();
        if (!ScopedStream)
        {
            ScopedStream.close();
            fs::remove(ScopedTemporary, ScopedCode);
            return ScopedFail("write failed for " + ScopedPhysical.string());
        }
    }

    // The old mapping must be closed before the target is replaced. Erasing the
    if (MLazyMappings.contains(ScopedKey))
    {
        MLazyMappings.erase(ScopedKey);
    }
    MEntries.erase(ScopedKey);

    const auto ScopedRestoreOldFile = [&]()
    {
        std::shared_ptr<MappedFile> ScopedOldMapping = MappedFile::Open(ScopedPhysical.string(), nullptr);
        if (!ScopedOldMapping)
        {
            return;
        }
        const std::span<const std::byte> ScopedOldBytes = ScopedOldMapping->Bytes();
        MLazyMappings.insert_or_assign(ScopedKey, std::move(ScopedOldMapping));
        AddEntry(ScopedKey, VfsEntry{ScopedMount->id, ScopedMount->priority, ScopedOldBytes}, nullptr);
    };

#if defined(_WIN32)
    if (::MoveFileExW(ScopedTemporary.wstring().c_str(), ScopedPhysical.wstring().c_str(),
                      MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0)
    {
        const DWORD ScopedCodeWin32 = ::GetLastError();
        fs::remove(ScopedTemporary, ScopedCode);
        ScopedRestoreOldFile();
        return ScopedFail("cannot replace " + ScopedPhysical.string() + " (Win32 error " +
                          std::to_string(ScopedCodeWin32) + ")");
    }
#else
    if (::rename(temporary.c_str(), physical.c_str()) != 0)
    {
        const std::string reason = std::strerror(errno);
        fs::remove(temporary, code);
        restoreOldFile();
        return fail("cannot replace " + physical.string() + ": " + reason);
    }
#endif

    std::shared_ptr<MappedFile> ScopedFreshMapping = MappedFile::Open(ScopedPhysical.string(), nullptr);
    if (!ScopedFreshMapping)
    {
        ScopedRestoreOldFile();
        return ScopedFail("file was written but cannot be remapped: " + ScopedPhysical.string());
    }
    const std::span<const std::byte> ScopedFreshBytes = ScopedFreshMapping->Bytes();
    MLazyMappings.insert_or_assign(ScopedKey, std::move(ScopedFreshMapping));
    AddEntry(ScopedKey, VfsEntry{ScopedMount->id, ScopedMount->priority, ScopedFreshBytes}, nullptr);
    LogInfo("vfs", "wrote '{}' ({} bytes) to the writable overlay '{}'", ScopedKey, InBytes.size(), ScopedMount->root);
    return true;
}

bool VirtualFileSystem::ReloadFile(std::string_view InPath, std::string* InError)
{
    const auto ScopedFail = [InError](std::string InMessage)
    {
        if (InError != nullptr)
        {
            *InError = std::move(InMessage);
        }
        return false;
    };

    const MountRecord* ScopedMount = FindWritableMount();
    if (ScopedMount == nullptr)
    {
        return ScopedFail("VFS is read-only: mount a directory to hot reload loose assets");
    }
    const std::string ScopedKey = Normalize(InPath);
    if (ScopedKey.empty())
    {
        return ScopedFail("cannot reload an empty VFS path");
    }
    if (HasParentTraversal(ScopedKey))
    {
        return ScopedFail("refusing to reload a VFS path with a '..' segment: " + ScopedKey);
    }
    const std::filesystem::path ScopedRelative(ScopedKey);
    if (ScopedRelative.is_absolute() || ScopedRelative.has_root_name())
    {
        return ScopedFail("refusing to reload an absolute VFS path: " + ScopedKey);
    }

    const std::filesystem::path ScopedPhysical = std::filesystem::path(ScopedMount->root) / ScopedRelative;
    std::error_code ScopedCode;
    if (!std::filesystem::is_regular_file(ScopedPhysical, ScopedCode))
    {
        return ScopedFail("file does not exist: " + ScopedPhysical.string());
    }

    // Open the replacement before touching the old mapping. If the new file is
    std::shared_ptr<MappedFile> ScopedFreshMapping = MappedFile::Open(ScopedPhysical.string(), InError);
    if (!ScopedFreshMapping)
    {
        return false;
    }
    const std::span<const std::byte> ScopedFreshBytes = ScopedFreshMapping->Bytes();

    // Any existing mapping (whether materialised at mount time or by an earlier
    MLazyMappings.insert_or_assign(ScopedKey, std::move(ScopedFreshMapping));
    AddEntry(ScopedKey, VfsEntry{ScopedMount->id, ScopedMount->priority, ScopedFreshBytes}, nullptr);
    LogInfo("vfs", "hot reloaded '{}' ({} bytes) from '{}'", ScopedKey, ScopedFreshBytes.size(), ScopedMount->root);
    return true;
}

void VirtualFileSystem::Unmount(std::uint32_t InMount)
{
    // Collect first: erasing while iterating a sorted vector invalidates the
    std::vector<std::string> ScopedDoomed;
    for (const auto& [ScopedPath, ScopedEntry] : MEntries)
    {
        if (ScopedEntry.mount == InMount)
        {
            ScopedDoomed.push_back(ScopedPath);
        }
    }
    for (const std::string& ScopedPath : ScopedDoomed)
    {
        if (MLazyMappings.contains(ScopedPath))
        {
            MLazyMappings.erase(ScopedPath);
        }
        MEntries.erase(ScopedPath);
    }

    std::erase_if(MMounts, [InMount](const std::unique_ptr<MountRecord>& InRecord) { return InRecord->id == InMount; });
}

std::optional<VfsEntry> VirtualFileSystem::Materialize(std::string_view InPath) const
{
    const std::string ScopedKey = Normalize(InPath);
    auto ScopedIt = MEntries.find(ScopedKey);
    if (ScopedIt == MEntries.end())
    {
        return std::nullopt;
    }
    if (!ScopedIt->second.data.empty())
    {
        return ScopedIt->second;
    }

    const MountRecord* ScopedOwner = nullptr;
    for (const std::unique_ptr<MountRecord>& ScopedMount : MMounts)
    {
        if (ScopedMount->id == ScopedIt->second.mount)
        {
            ScopedOwner = ScopedMount.get();
            break;
        }
    }
    if (ScopedOwner == nullptr || ScopedOwner->isPak)
    {
        return ScopedIt->second;
    }
    const std::filesystem::path ScopedPhysical =
        std::filesystem::path(ScopedOwner->root) / std::filesystem::path(ScopedKey);
    std::shared_ptr<MappedFile> ScopedMapping = MappedFile::Open(ScopedPhysical.string(), nullptr);
    if (!ScopedMapping)
    {
        return std::nullopt;
    }
    const std::span<const std::byte> ScopedBytes = ScopedMapping->Bytes();
    MLazyMappings.insert_or_assign(ScopedKey, std::move(ScopedMapping));
    ScopedIt->second.data = ScopedBytes;
    return ScopedIt->second;
}

std::optional<VfsEntry> VirtualFileSystem::Find(std::string_view InPath) const
{
    return Materialize(InPath);
}

bool VirtualFileSystem::InvalidateFile(std::string_view InPath)
{
    const std::string ScopedKey = Normalize(InPath);
    bool ScopedExisted = false;
    if (MLazyMappings.contains(ScopedKey))
    {
        MLazyMappings.erase(ScopedKey);
        ScopedExisted = true;
    }
    const auto ScopedIt = MEntries.find(ScopedKey);
    if (ScopedIt != MEntries.end())
    {
        const MountRecord* ScopedOwner = nullptr;
        for (const std::unique_ptr<MountRecord>& ScopedMount : MMounts)
        {
            if (ScopedMount->id == ScopedIt->second.mount)
            {
                ScopedOwner = ScopedMount.get();
                break;
            }
        }
        if (ScopedOwner != nullptr && ScopedOwner->isPak)
        {
            // A pak entry is a span into the read-only mapping: erasing the
            // index row would hide the asset and leave the mount inconsistent.
            LogWarn("vfs", "refusing to invalidate pak-backed '{}'", ScopedKey);
            return false;
        }
        MEntries.erase(ScopedKey);
        ScopedExisted = true;
    }
    return ScopedExisted;
}

bool VirtualFileSystem::Exists(std::string_view InPath) const
{
    return MEntries.contains(Normalize(InPath));
}

std::optional<std::span<const std::byte>> VirtualFileSystem::Read(std::string_view InPath) const
{
    const auto ScopedEntry = Find(InPath);
    if (!ScopedEntry.has_value())
    {
        return std::nullopt;
    }
    return ScopedEntry->data;
}

std::optional<PinnedFile> VirtualFileSystem::Pin(std::string_view InPath) const
{
    // Materialize() indexes the path and, for a directory mount, opens the lazy
    const auto ScopedEntry = Find(InPath);
    if (!ScopedEntry.has_value() || ScopedEntry->data.empty())
    {
        return std::nullopt;
    }

    for (const std::unique_ptr<MountRecord>& ScopedMount : MMounts)
    {
        if (ScopedMount->id != ScopedEntry->mount)
        {
            continue;
        }
        if (ScopedMount->isPak)
        {
            if (!ScopedMount->pak)
            {
                return std::nullopt;
            }
            return PinnedFile{ScopedMount->pak, ScopedEntry->data};
        }
        // A directory mount owns the mapping in MLazyMappings. Materialize()
        const std::string ScopedKey = Normalize(InPath);
        const auto ScopedIt = MLazyMappings.find(ScopedKey);
        if (ScopedIt == MLazyMappings.end() || !ScopedIt->second)
        {
            return std::nullopt;
        }
        return PinnedFile{ScopedIt->second, ScopedEntry->data};
    }
    return std::nullopt;
}

std::vector<std::string_view> VirtualFileSystem::List(std::string_view InDirectory) const
{
    std::vector<std::string_view> ScopedResult;
    std::string ScopedPrefix = Normalize(InDirectory);
    if (!ScopedPrefix.empty() && ScopedPrefix.back() != '/')
    {
        ScopedPrefix.push_back('/');
    }

    for (const auto& [ScopedPath, ScopedEntry] : MEntries)
    {
        (void)ScopedEntry;
        if (ScopedPath.size() > ScopedPrefix.size() && ScopedPath.compare(0, ScopedPrefix.size(), ScopedPrefix) == 0)
        {
            ScopedResult.push_back(ScopedPath);
        }
    }
    return ScopedResult;
}
}
