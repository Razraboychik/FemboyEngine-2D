// FemboyEngine2D - VFS-backed long-form text documents.
#pragma once

#include "Engine/Core/Types.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace Fby
{
class VirtualFileSystem;

namespace Documents
{

inline constexpr std::uint32_t kMaxDocuments = 8;
inline constexpr std::uint32_t kInvalidDocumentIndex = 0xFFFFFFFFu;
inline constexpr std::uint32_t kDefaultLinesPerPage = 32;
inline constexpr std::uint64_t kMaxDocumentBytes = 8ull * 1024ull * 1024ull;
inline constexpr std::size_t kDocumentTitleBytes = 96;

enum class DocumentError : std::uint8_t
{
    None = 0,
    NotFound,
    Unsupported,
    TooLarge,
    InvalidHandle,
    ReadFailed,
    TooManyOpen,
};

// Process-local document identity. A slot is reused after Close().
struct DocumentHandle
{
    std::uint32_t Index = kInvalidDocumentIndex;
    std::uint32_t Generation = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return Index != kInvalidDocumentIndex;
    }
};

struct DocumentInfo
{
    std::uint32_t PageCount = 0;
    std::uint32_t LineCount = 0;
    std::uint64_t ByteSize = 0;
    std::array<char, kDocumentTitleBytes> Title{};
};

// Fixed-slot TXT owner. Open/Close read the VFS and copy the UTF-8 text once;
// PageText only slices the immutable stored text. Rich documents and PDFs are
// separate systems: this type deliberately owns plain text only.
class DocumentSystem
{
public:
    DocumentSystem() = default;
    ~DocumentSystem() = default;

    DocumentSystem(const DocumentSystem&) = delete;
    DocumentSystem& operator=(const DocumentSystem&) = delete;

    void SetVfs(VirtualFileSystem* InVfs) noexcept
    {
        MVfs = InVfs;
    }
    [[nodiscard]] VirtualFileSystem* Vfs() const noexcept
    {
        return MVfs;
    }

    // TXT/TEXT only. UTF-8 is required, a BOM and CRLF are normalised, and the
    // copy is paginated by complete lines. Other extensions are Unsupported.
    [[nodiscard]] DocumentHandle Open(std::string_view InPath, DocumentError* OutError = nullptr);
    bool Close(DocumentHandle InHandle) noexcept;

    [[nodiscard]] bool GetInfo(DocumentHandle InHandle, DocumentInfo* OutInfo) const noexcept;
    [[nodiscard]] std::uint32_t PageCount(DocumentHandle InHandle) const noexcept;
    [[nodiscard]] std::uint32_t LineCount(DocumentHandle InHandle) const noexcept;

    // Copies the page's UTF-8 bytes (including line endings) into OutText. The
    // result is truncated to the destination capacity; OutBytes reports the
    // stored page size so the caller can grow the buffer.
    bool PageText(DocumentHandle InHandle, std::uint32_t InPage, std::span<char> OutText,
                  std::uint32_t* OutBytes = nullptr) const noexcept;

    // Returns the first page containing InNeedle, or kInvalidDocumentIndex.
    [[nodiscard]] std::uint32_t FindPage(DocumentHandle InHandle, std::string_view InNeedle) const noexcept;

    void SetLinesPerPage(std::uint32_t InLines) noexcept
    {
        MLinesPerPage = InLines == 0 ? kDefaultLinesPerPage : InLines;
    }
    [[nodiscard]] std::uint32_t LinesPerPage() const noexcept
    {
        return MLinesPerPage;
    }

private:
    struct Slot
    {
        std::vector<char> Text;
        // Byte offset of every line start; the last line ends at Text.size().
        std::vector<std::uint32_t> Lines;
        std::array<char, kDocumentTitleBytes> Title{};
        std::uint32_t Generation = 0;
        bool Used = false;
    };

    [[nodiscard]] Slot* Resolve(DocumentHandle InHandle) noexcept;
    [[nodiscard]] const Slot* Resolve(DocumentHandle InHandle) const noexcept;
    [[nodiscard]] std::uint32_t PageCountOf(const Slot& InSlot) const noexcept;

    VirtualFileSystem* MVfs = nullptr;
    std::array<Slot, kMaxDocuments> MSlots{};
    std::uint32_t MLinesPerPage = kDefaultLinesPerPage;
};

}
}
