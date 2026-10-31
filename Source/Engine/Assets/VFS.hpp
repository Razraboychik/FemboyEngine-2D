// FemboyEngine2D - virtual file system.
#pragma once

#include "Engine/Core/FlatMap.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Fby
{

// MappedFile - one read-only mapping, move-only.
class MappedFile
{
public:
    MappedFile() = default;
    ~MappedFile();

    MappedFile(MappedFile&& InOther) noexcept;
    MappedFile& operator=(MappedFile&& InOther) noexcept;
    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;

    [[nodiscard]] static std::unique_ptr<MappedFile> Open(std::string_view InPath, std::string* InError = nullptr);

    [[nodiscard]] std::span<const std::byte> Bytes() const noexcept
    {
        return {static_cast<const std::byte*>(MData), MSize};
    }
    [[nodiscard]] bool IsOpen() const noexcept
    {
        return MData != nullptr;
    }

private:
    void Close() noexcept;

    void* MData = nullptr;
    std::size_t MSize = 0;
#if defined(_WIN32)
    void* MFile = nullptr;
    void* MMapping = nullptr;
#else
    int MFileDescriptor = -1;
#endif
};

// VirtualFileSystem
struct VfsEntry
{
    std::uint32_t mount = 0;
    std::int32_t priority = 0;
    std::span<const std::byte> data;
};

// A read handle that owns the mapping it points into. Read() is the cheap
struct PinnedFile
{
    std::shared_ptr<const MappedFile> mapping{};
    std::span<const std::byte> bytes{};
};

class VirtualFileSystem
{
public:
    VirtualFileSystem() = default;
    ~VirtualFileSystem();

    VirtualFileSystem(const VirtualFileSystem&) = delete;
    VirtualFileSystem& operator=(const VirtualFileSystem&) = delete;

    // A directory mount maps files lazily on first access and keeps the mapping
    bool MountDirectory(std::string_view InRoot, int InPriority, std::string* InError = nullptr);

    // Re-walks every directory mount and refreshes its index. The editor calls
    // this after creating, renaming, duplicating or deleting files on disk, which
    // happen outside the VFS write path.
    void RefreshDirectoryMounts();

    // A .fempak is mapped whole at mount time; every entry becomes a span into it.
    bool MountPak(std::string_view InPakPath, int InPriority, std::string* InError = nullptr);

    void Unmount(std::uint32_t InMount);

    // Zero-copy read. The span stays valid until the owning mount is unmounted
    [[nodiscard]] std::optional<std::span<const std::byte>> Read(std::string_view InPath) const;

    // Owning read. The returned PinnedFile keeps the exact mapping alive even
    [[nodiscard]] std::optional<PinnedFile> Pin(std::string_view InPath) const;

    [[nodiscard]] bool Exists(std::string_view InPath) const;
    [[nodiscard]] std::optional<VfsEntry> Find(std::string_view InPath) const;

    // Drops the path's mapping and index entry before a background re-cook
    [[nodiscard]] bool InvalidateFile(std::string_view InPath);

    // Writable overlay (editor only, never used by the shipped game loop).
    [[nodiscard]] std::optional<std::string> ResolveWritablePath(std::string_view InPath) const;
    // Writes through the overlay, remaps the file and updates the index, so a
    [[nodiscard]] bool WriteFile(std::string_view InPath, std::span<const std::byte> InBytes,
                                 std::string* InError = nullptr);

    // Re-reads one loose file after an external writer (the OS watcher's hot
    [[nodiscard]] bool ReloadFile(std::string_view InPath, std::string* InError = nullptr);

    // Entries directly under `directory`, deduplicated with the same priority
    [[nodiscard]] std::vector<std::string_view> List(std::string_view InDirectory) const;

    [[nodiscard]] std::size_t EntryCount() const
    {
        return MEntries.size();
    }
    [[nodiscard]] std::size_t MountCount() const
    {
        return MMounts.size();
    }

    // Canonical form used for lookup: forward slashes, no leading slash, no
    [[nodiscard]] static std::string Normalize(std::string_view InPath);

private:
    struct MountRecord
    {
        std::uint32_t id = 0;
        std::int32_t priority = 0;
        std::string root;
        bool isPak = false;
        // Shared, because Pin() hands the mapping to consumers that may outlive
        std::shared_ptr<MappedFile> pak;
    };

    bool AddEntry(std::string InPath, VfsEntry InEntry, std::string* InError);
    [[nodiscard]] const MountRecord* FindWritableMount() const noexcept;
    [[nodiscard]] std::optional<VfsEntry> Materialize(std::string_view InPath) const;

    std::vector<std::unique_ptr<MountRecord>> MMounts;
    mutable Fby::FlatMap<std::string, VfsEntry> MEntries;
    // Directory mounts map on demand. The mapping is shared so a Pin() can keep
    mutable Fby::FlatMap<std::string, std::shared_ptr<MappedFile>> MLazyMappings;
    std::uint32_t MNextMountId = 1;
};
}
