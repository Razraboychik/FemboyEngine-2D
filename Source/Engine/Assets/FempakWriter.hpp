// FemboyEngine2D - .fempak archive writer (offline asset pipeline only).
#pragma once

#include "Engine/Assets/PakFormat.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Fby::Assets
{

struct FempakInput
{
    std::string path;
    std::vector<std::byte> bytes;
    std::uint32_t alignment = kPakAlignment;
};

class FempakWriter
{
public:
    // `path` is normalized with NormalizePakPath(). Adds or replaces the entry.
    void Add(std::string_view InPath, std::span<const std::byte> InBytes, std::uint32_t InAlignment = kPakAlignment);
    void Add(std::string_view InPath, std::vector<std::byte> InBytes, std::uint32_t InAlignment = kPakAlignment);

    // Adds every regular file under a directory, recursively. `virtualRoot` is
    bool AddDirectory(std::string_view InDirectory, std::string_view InVirtualRoot, std::string* InError = nullptr);

    [[nodiscard]] std::size_t Size() const noexcept
    {
        return MEntries.size();
    }
    [[nodiscard]] bool Empty() const noexcept
    {
        return MEntries.empty();
    }
    [[nodiscard]] const std::vector<FempakInput>& Entries() const noexcept
    {
        return MEntries;
    }

    // Serializes the archive. The result can be written straight to disk or
    [[nodiscard]] std::vector<std::byte> Build(std::string* InError = nullptr) const;

private:
    std::vector<FempakInput> MEntries;
};

// Writes `bytes` to `path` atomically enough for a build step: a temporary file
[[nodiscard]] bool WriteFileAtomically(std::string_view InPath, std::span<const std::byte> InBytes,
                                       std::string* InError = nullptr);
}
