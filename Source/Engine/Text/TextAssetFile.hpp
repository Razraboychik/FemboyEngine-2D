// FemboyEngine2D - mmap-backed cooked text asset.
#pragma once

#include "Engine/Assets/VFS.hpp"
#include "Engine/Text/TextAssetFormat.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace Fby::Text
{

// Owns the mapping while exposing a zero-copy TextAssetView. The view stays
// valid until Close() or destruction; no text bytes are copied.
class MappedTextAsset
{
public:
    [[nodiscard]] bool Open(const VirtualFileSystem& InVfs, std::string_view InPath,
                            std::string* InError = nullptr) noexcept
    {
        std::optional<PinnedFile> ScopedPinned = InVfs.Pin(InPath);
        if (!ScopedPinned.has_value())
        {
            if (InError != nullptr)
            {
                *InError = "text asset is not present in the VFS";
            }
            return false;
        }
        TextAssetView ScopedView;
        if (OpenTextAsset(ScopedPinned->bytes, &ScopedView, InError) != TextAssetStatus::Ok)
        {
            return false;
        }
        MFile = std::move(*ScopedPinned);
        MView = ScopedView;
        return true;
    }

    void Close() noexcept
    {
        MFile = {};
        MView = {};
    }

    [[nodiscard]] bool IsOpen() const noexcept
    {
        return MFile.mapping != nullptr;
    }

    [[nodiscard]] const TextAssetView& View() const noexcept
    {
        return MView;
    }

    [[nodiscard]] std::string_view Block(std::uint32_t InIndex) const noexcept
    {
        return MView.Block(InIndex);
    }

private:
    PinnedFile MFile{};
    TextAssetView MView{};
};
}
