// FemboyEngine2D - fixed-pool hypertext parser.
#include "Engine/UI/Hypertext.hpp"

#include "Engine/Assets/Localization.hpp"

#include <algorithm>
#include <cstring>

namespace Fby::Ui
{
namespace
{

[[nodiscard]] bool StartsWithUri(std::string_view InTarget) noexcept
{
    return InTarget.starts_with("http://") || InTarget.starts_with("https://") || InTarget.starts_with("mailto:") ||
           InTarget.starts_with("ui://") || InTarget.starts_with("game://");
}
}

void HypertextPool::Clear() noexcept
{
    MPlainUsed = 0;
    MTargetUsed = 0;
    MLinkUsed = 0;
}

bool HypertextPool::AppendChar(char InValue) noexcept
{
    if (MPlainUsed >= MPlain.size())
    {
        return false;
    }
    MPlain[MPlainUsed++] = InValue;
    return true;
}

bool HypertextPool::AppendTarget(std::string_view InTarget, std::uint16_t* InOffset, std::uint16_t* InLength) noexcept
{
    if (InTarget.size() > static_cast<std::size_t>(kMaxTargetBytes - MTargetUsed))
    {
        return false;
    }
    const std::uint16_t ScopedTargetOffset = static_cast<std::uint16_t>(MTargetUsed);
    if (!InTarget.empty())
    {
        std::memcpy(MTargets.data() + MTargetUsed, InTarget.data(), InTarget.size());
    }
    MTargetUsed += static_cast<std::uint32_t>(InTarget.size());
    *InOffset = ScopedTargetOffset;
    *InLength = static_cast<std::uint16_t>(InTarget.size());
    return true;
}

HypertextComponent HypertextPool::Intern(std::string_view InMarkup, std::uint32_t InLocalizationId,
                                         std::uint16_t InFlags) noexcept
{
    HypertextComponent ScopedComponent{};
    ScopedComponent.LocalizationID = InLocalizationId;
    ScopedComponent.Flags = InFlags;
    ScopedComponent.PlainOffset = MPlainUsed;
    ScopedComponent.FirstLink = MLinkUsed;

    const std::uint32_t ScopedPlainStart = MPlainUsed;
    const std::uint32_t ScopedTargetStart = MTargetUsed;
    const std::uint32_t ScopedLinkStart = MLinkUsed;
    const auto ScopedRollback = [this, ScopedPlainStart, ScopedTargetStart, ScopedLinkStart]()
    {
        MPlainUsed = ScopedPlainStart;
        MTargetUsed = ScopedTargetStart;
        MLinkUsed = ScopedLinkStart;
    };

    std::size_t ScopedCursor = 0;
    while (ScopedCursor < InMarkup.size())
    {
        const char ScopedCharacter = InMarkup[ScopedCursor];
        if (ScopedCharacter == '\\' && ScopedCursor + 1 < InMarkup.size())
        {
            if (!AppendChar(InMarkup[ScopedCursor + 1]))
            {
                ScopedRollback();
                return {};
            }
            ScopedCursor += 2;
            continue;
        }

        if (ScopedCharacter != '[')
        {
            if (!AppendChar(ScopedCharacter))
            {
                ScopedRollback();
                return {};
            }
            ++ScopedCursor;
            continue;
        }

        const std::size_t ScopedLabelEnd = InMarkup.find(']', ScopedCursor + 1);
        if (ScopedLabelEnd == std::string_view::npos || ScopedLabelEnd + 1 >= InMarkup.size() ||
            InMarkup[ScopedLabelEnd + 1] != '(')
        {
            if (!AppendChar(ScopedCharacter))
            {
                ScopedRollback();
                return {};
            }
            ++ScopedCursor;
            continue;
        }
        const std::size_t ScopedTargetEnd = InMarkup.find(')', ScopedLabelEnd + 2);
        if (ScopedTargetEnd == std::string_view::npos)
        {
            if (!AppendChar(ScopedCharacter))
            {
                ScopedRollback();
                return {};
            }
            ++ScopedCursor;
            continue;
        }

        const std::string_view ScopedLabel = InMarkup.substr(ScopedCursor + 1, ScopedLabelEnd - ScopedCursor - 1);
        const std::string_view ScopedTarget = InMarkup.substr(ScopedLabelEnd + 2, ScopedTargetEnd - ScopedLabelEnd - 2);
        if (ScopedLabel.empty() || ScopedTarget.empty() || MLinkUsed >= MLinks.size())
        {
            // A malformed link is rendered as literal text rather than being
            for (std::size_t ScopedI = ScopedCursor; ScopedI <= ScopedTargetEnd; ++ScopedI)
            {
                if (!AppendChar(InMarkup[ScopedI]))
                {
                    ScopedRollback();
                    return {};
                }
            }
            ScopedCursor = ScopedTargetEnd + 1;
            continue;
        }

        const std::uint32_t ScopedBegin = MPlainUsed;
        for (const char ScopedGlyph : ScopedLabel)
        {
            if (!AppendChar(ScopedGlyph))
            {
                ScopedRollback();
                return {};
            }
        }
        const std::uint32_t ScopedEnd = MPlainUsed;

        HyperlinkRange ScopedLink{};
        ScopedLink.begin = static_cast<std::uint16_t>(ScopedBegin);
        ScopedLink.end = static_cast<std::uint16_t>(ScopedEnd);
        if (!AppendTarget(ScopedTarget, &ScopedLink.targetOffset, &ScopedLink.targetLength))
        {
            ScopedRollback();
            return {};
        }
        ScopedLink.flags = InFlags;
        if (StartsWithUri(ScopedTarget))
        {
            ScopedLink.flags = static_cast<std::uint16_t>(ScopedLink.flags | HypertextOpenExternal);
        }
        MLinks[MLinkUsed++] = ScopedLink;
        ScopedCursor = ScopedTargetEnd + 1;
    }

    ScopedComponent.PlainLength = MPlainUsed - ScopedComponent.PlainOffset;
    ScopedComponent.LinkCount = MLinkUsed - ScopedComponent.FirstLink;
    return ScopedComponent;
}

HypertextView HypertextPool::View(const HypertextComponent& InComponent) const noexcept
{
    HypertextView ScopedView{};
    if (InComponent.PlainOffset >= MPlainUsed || InComponent.PlainOffset + InComponent.PlainLength > MPlainUsed ||
        InComponent.FirstLink + InComponent.LinkCount > MLinkUsed)
    {
        return ScopedView;
    }
    ScopedView.text = std::string_view{MPlain.data() + InComponent.PlainOffset, InComponent.PlainLength};
    ScopedView.links = std::span<const HyperlinkRange>{MLinks.data() + InComponent.FirstLink, InComponent.LinkCount};
    return ScopedView;
}

HypertextView HypertextPool::Resolve(const LocalizationTable& InTable,
                                     const HypertextComponent& InComponent) const noexcept
{
    if ((InComponent.Flags & HypertextNoLocalize) != 0 || InComponent.LocalizationID == 0)
    {
        return View(InComponent);
    }
    const std::string_view ScopedLocalized = InTable.FindWithFallback(InComponent.LocalizationID);
    if (ScopedLocalized.empty())
    {
        return View(InComponent);
    }
    HypertextView ScopedView{};
    ScopedView.text = ScopedLocalized;
    // The link table is authored against the source string. A translation that
    if (ScopedLocalized.size() == InComponent.PlainLength)
    {
        const HypertextView ScopedAuthored = View(InComponent);
        ScopedView.links = ScopedAuthored.links;
    }
    return ScopedView;
}

std::string_view HypertextPool::Target(const HyperlinkRange& InLink) const noexcept
{
    if (InLink.targetOffset >= MTargetUsed || InLink.targetOffset + InLink.targetLength > MTargetUsed)
    {
        return {};
    }
    return {MTargets.data() + InLink.targetOffset, InLink.targetLength};
}

const HyperlinkRange* HypertextPool::LinkAt(const HypertextView& InView, std::uint32_t InByteOffset) const noexcept
{
    for (const HyperlinkRange& ScopedLink : InView.links)
    {
        if (InByteOffset >= ScopedLink.begin && InByteOffset < ScopedLink.end)
        {
            return &ScopedLink;
        }
    }
    return nullptr;
}

bool HyperlinkClicked(const HypertextView& InView, std::uint32_t InByteOffset, std::string_view* OutTarget,
                      std::uint16_t* OutFlags, const HypertextPool& InPool) noexcept
{
    const HyperlinkRange* ScopedLink = InPool.LinkAt(InView, InByteOffset);
    if (ScopedLink == nullptr)
    {
        return false;
    }
    if (OutTarget != nullptr)
    {
        *OutTarget = InPool.Target(*ScopedLink);
    }
    if (OutFlags != nullptr)
    {
        *OutFlags = ScopedLink->flags;
    }
    return true;
}
}
