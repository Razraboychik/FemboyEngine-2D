// FemboyEngine2D - VFS-backed document slots and the TXT paginator.
#include "Engine/Documents/Document.hpp"

#include "Engine/Assets/VFS.hpp"
#include "Engine/Core/ThreadAffinity.hpp"

#include <algorithm>
#include <cstring>

namespace Fby::Documents
{
namespace
{
void SetError(DocumentError* OutError, DocumentError InError) noexcept
{
    if (OutError != nullptr)
    {
        *OutError = InError;
    }
}

[[nodiscard]] bool HasTextExtension(std::string_view InPath) noexcept
{
    const std::size_t ScopedDot = InPath.find_last_of('.');
    const std::size_t ScopedSlash = InPath.find_last_of("/\\");
    if (ScopedDot == std::string_view::npos || (ScopedSlash != std::string_view::npos && ScopedSlash > ScopedDot))
    {
        return false;
    }
    std::string_view ScopedExtension = InPath.substr(ScopedDot);
    char ScopedLower[8]{};
    if (ScopedExtension.size() >= sizeof(ScopedLower))
    {
        return false;
    }
    for (std::size_t ScopedI = 0; ScopedI < ScopedExtension.size(); ++ScopedI)
    {
        const char ScopedChar = ScopedExtension[ScopedI];
        ScopedLower[ScopedI] =
            static_cast<char>(ScopedChar >= 'A' && ScopedChar <= 'Z' ? ScopedChar - 'A' + 'a' : ScopedChar);
    }
    const std::string_view ScopedNormalized(ScopedLower, ScopedExtension.size());
    return ScopedNormalized == ".txt" || ScopedNormalized == ".text";
}
}

DocumentSystem::Slot* DocumentSystem::Resolve(DocumentHandle InHandle) noexcept
{
    if (!InHandle.IsValid() || InHandle.Index >= MSlots.size())
    {
        return nullptr;
    }
    Slot& ScopedSlot = MSlots[InHandle.Index];
    if (!ScopedSlot.Used || ScopedSlot.Generation != InHandle.Generation)
    {
        return nullptr;
    }
    return &ScopedSlot;
}

const DocumentSystem::Slot* DocumentSystem::Resolve(DocumentHandle InHandle) const noexcept
{
    return const_cast<DocumentSystem*>(this)->Resolve(InHandle);
}

std::uint32_t DocumentSystem::PageCountOf(const Slot& InSlot) const noexcept
{
    if (InSlot.Lines.empty())
    {
        return 0;
    }
    const std::uint32_t ScopedLines = static_cast<std::uint32_t>(InSlot.Lines.size());
    return (ScopedLines + MLinesPerPage - 1u) / MLinesPerPage;
}

DocumentHandle DocumentSystem::Open(std::string_view InPath, DocumentError* OutError)
{
    SetError(OutError, DocumentError::None);
    if (!RequireMainThread("DocumentSystem::Open"))
    {
        SetError(OutError, DocumentError::ReadFailed);
        return {};
    }
    if (MVfs == nullptr)
    {
        SetError(OutError, DocumentError::ReadFailed);
        return {};
    }
    if (!HasTextExtension(InPath))
    {
        SetError(OutError, DocumentError::Unsupported);
        return {};
    }
    const auto ScopedBytes = MVfs->Read(InPath);
    if (!ScopedBytes.has_value())
    {
        SetError(OutError, DocumentError::NotFound);
        return {};
    }
    if (ScopedBytes->size() > kMaxDocumentBytes)
    {
        SetError(OutError, DocumentError::TooLarge);
        return {};
    }

    std::uint32_t ScopedIndex = kInvalidDocumentIndex;
    for (std::uint32_t ScopedI = 0; ScopedI < MSlots.size(); ++ScopedI)
    {
        if (!MSlots[ScopedI].Used)
        {
            ScopedIndex = ScopedI;
            break;
        }
    }
    if (ScopedIndex == kInvalidDocumentIndex)
    {
        SetError(OutError, DocumentError::TooManyOpen);
        return {};
    }

    Slot& ScopedSlot = MSlots[ScopedIndex];
    std::size_t ScopedStart = 0;
    if (ScopedBytes->size() >= 3 && std::to_integer<unsigned char>((*ScopedBytes)[0]) == 0xEF &&
        std::to_integer<unsigned char>((*ScopedBytes)[1]) == 0xBB &&
        std::to_integer<unsigned char>((*ScopedBytes)[2]) == 0xBF)
    {
        ScopedStart = 3;
    }
    ScopedSlot.Text.clear();
    ScopedSlot.Text.reserve(ScopedBytes->size() - ScopedStart);
    for (std::size_t ScopedI = ScopedStart; ScopedI < ScopedBytes->size(); ++ScopedI)
    {
        const char ScopedChar = static_cast<char>((*ScopedBytes)[ScopedI]);
        if (ScopedChar == '\r')
        {
            ScopedSlot.Text.push_back('\n');
            if (ScopedI + 1 < ScopedBytes->size() && (*ScopedBytes)[ScopedI + 1] == std::byte{'\n'})
            {
                ++ScopedI;
            }
            continue;
        }
        ScopedSlot.Text.push_back(ScopedChar);
    }

    ScopedSlot.Lines.clear();
    if (!ScopedSlot.Text.empty())
    {
        ScopedSlot.Lines.push_back(0);
        for (std::size_t ScopedI = 0; ScopedI < ScopedSlot.Text.size(); ++ScopedI)
        {
            if (ScopedSlot.Text[ScopedI] == '\n' && ScopedI + 1 < ScopedSlot.Text.size())
            {
                ScopedSlot.Lines.push_back(static_cast<std::uint32_t>(ScopedI + 1));
            }
        }
    }

    // Title: the first non-empty line, trimmed and cut to the fixed buffer.
    ScopedSlot.Title.fill('\0');
    for (const std::uint32_t ScopedOffset : ScopedSlot.Lines)
    {
        std::size_t ScopedEnd = ScopedOffset;
        while (ScopedEnd < ScopedSlot.Text.size() && ScopedSlot.Text[ScopedEnd] != '\n')
        {
            ++ScopedEnd;
        }
        std::size_t ScopedBegin = ScopedOffset;
        while (ScopedBegin < ScopedEnd && (ScopedSlot.Text[ScopedBegin] == ' ' || ScopedSlot.Text[ScopedBegin] == '\t'))
        {
            ++ScopedBegin;
        }
        std::size_t ScopedTrimEnd = ScopedEnd;
        while (ScopedTrimEnd > ScopedBegin &&
               (ScopedSlot.Text[ScopedTrimEnd - 1] == ' ' || ScopedSlot.Text[ScopedTrimEnd - 1] == '\t'))
        {
            --ScopedTrimEnd;
        }
        if (ScopedTrimEnd > ScopedBegin)
        {
            const std::size_t ScopedLength = std::min(ScopedTrimEnd - ScopedBegin, kDocumentTitleBytes - 1);
            std::memcpy(ScopedSlot.Title.data(), ScopedSlot.Text.data() + ScopedBegin, ScopedLength);
            break;
        }
    }

    ScopedSlot.Used = true;
    ++ScopedSlot.Generation;
    if (ScopedSlot.Generation == 0)
    {
        ScopedSlot.Generation = 1;
    }
    return DocumentHandle{ScopedIndex, ScopedSlot.Generation};
}

bool DocumentSystem::Close(DocumentHandle InHandle) noexcept
{
    if (!RequireMainThread("DocumentSystem::Close"))
    {
        return false;
    }
    Slot* ScopedSlot = Resolve(InHandle);
    if (ScopedSlot == nullptr)
    {
        return false;
    }
    ScopedSlot->Text = {};
    ScopedSlot->Lines = {};
    ScopedSlot->Title.fill('\0');
    ScopedSlot->Used = false;
    return true;
}

bool DocumentSystem::GetInfo(DocumentHandle InHandle, DocumentInfo* OutInfo) const noexcept
{
    const Slot* ScopedSlot = Resolve(InHandle);
    if (ScopedSlot == nullptr || OutInfo == nullptr)
    {
        return false;
    }
    OutInfo->PageCount = PageCountOf(*ScopedSlot);
    OutInfo->LineCount = static_cast<std::uint32_t>(ScopedSlot->Lines.size());
    OutInfo->ByteSize = ScopedSlot->Text.size();
    OutInfo->Title = ScopedSlot->Title;
    return true;
}

std::uint32_t DocumentSystem::PageCount(DocumentHandle InHandle) const noexcept
{
    const Slot* ScopedSlot = Resolve(InHandle);
    return ScopedSlot == nullptr ? 0 : PageCountOf(*ScopedSlot);
}

std::uint32_t DocumentSystem::LineCount(DocumentHandle InHandle) const noexcept
{
    const Slot* ScopedSlot = Resolve(InHandle);
    return ScopedSlot == nullptr ? 0 : static_cast<std::uint32_t>(ScopedSlot->Lines.size());
}

bool DocumentSystem::PageText(DocumentHandle InHandle, std::uint32_t InPage, std::span<char> OutText,
                              std::uint32_t* OutBytes) const noexcept
{
    const Slot* ScopedSlot = Resolve(InHandle);
    if (ScopedSlot == nullptr || ScopedSlot->Lines.empty())
    {
        return false;
    }
    const std::uint32_t ScopedFirstLine = InPage * MLinesPerPage;
    if (ScopedFirstLine >= ScopedSlot->Lines.size())
    {
        return false;
    }
    const std::uint32_t ScopedStart = ScopedSlot->Lines[ScopedFirstLine];
    const std::uint32_t ScopedNextLine = (InPage + 1u) * MLinesPerPage;
    const std::uint32_t ScopedEnd = ScopedNextLine < ScopedSlot->Lines.size()
                                        ? ScopedSlot->Lines[ScopedNextLine]
                                        : static_cast<std::uint32_t>(ScopedSlot->Text.size());
    const std::uint32_t ScopedBytes = ScopedEnd - ScopedStart;
    if (OutBytes != nullptr)
    {
        *OutBytes = ScopedBytes;
    }
    const std::size_t ScopedCopy = std::min<std::size_t>(ScopedBytes, OutText.size());
    if (ScopedCopy > 0)
    {
        std::memcpy(OutText.data(), ScopedSlot->Text.data() + ScopedStart, ScopedCopy);
    }
    return true;
}

std::uint32_t DocumentSystem::FindPage(DocumentHandle InHandle, std::string_view InNeedle) const noexcept
{
    const Slot* ScopedSlot = Resolve(InHandle);
    if (ScopedSlot == nullptr || InNeedle.empty())
    {
        return kInvalidDocumentIndex;
    }
    const std::string_view ScopedText(ScopedSlot->Text.data(), ScopedSlot->Text.size());
    const std::size_t ScopedPosition = ScopedText.find(InNeedle);
    if (ScopedPosition == std::string_view::npos)
    {
        return kInvalidDocumentIndex;
    }
    std::uint32_t ScopedLine = 0;
    for (const std::uint32_t ScopedOffset : ScopedSlot->Lines)
    {
        if (ScopedOffset > ScopedPosition)
        {
            break;
        }
        ++ScopedLine;
    }
    return ScopedLine == 0 ? 0 : (ScopedLine - 1u) / MLinesPerPage;
}

}
