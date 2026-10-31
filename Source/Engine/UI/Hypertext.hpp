// FemboyEngine2D - hypertext inside FlatBuffers strings.
#pragma once

#include "Engine/Core/Types.hpp"
#include "Engine/Core/Mirror.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

namespace Fby
{
class LocalizationTable;

namespace Ui
{

enum HypertextFlags : std::uint32_t
{
    HypertextNone = 0,
    HypertextNoLocalize = 1u << 0,
    // Uri:// or https:// target
    HypertextOpenExternal = 1u << 1,
    HypertextUnderline = 1u << 2,
    // Target is a command id, not a URL
    HypertextAction = 1u << 3,
};

struct HypertextComponent
{
    // 0 = raw text
    std::uint32_t LocalizationID = 0;
    // Into HypertextPool::plain_
    std::uint32_t PlainOffset = 0;
    std::uint32_t PlainLength = 0;
    // Into HypertextPool::links_
    std::uint32_t FirstLink = 0;
    std::uint32_t LinkCount = 0;
    // HypertextFlags
    std::uint32_t Flags = 0;
    std::uint32_t Reserved = 0;
};

static_assert(std::is_trivially_copyable_v<HypertextComponent>);
static_assert(std::is_standard_layout_v<HypertextComponent>);
static_assert(sizeof(HypertextComponent) == 28);

struct HyperlinkRange
{
    // Byte range in the plain text
    std::uint16_t begin = 0;
    std::uint16_t end = 0;
    // Into HypertextPool::targets_
    std::uint16_t targetOffset = 0;
    std::uint16_t targetLength = 0;
    // HypertextFlags link-specific bits
    std::uint16_t flags = 0;
    std::uint16_t reserved = 0;
};

static_assert(std::is_trivially_copyable_v<HyperlinkRange>);
static_assert(sizeof(HyperlinkRange) == 12);

struct HypertextView
{
    std::string_view text{};
    std::span<const HyperlinkRange> links{};
};

// A bounded parser/pool. One instance is typically owned by the UI system; the
class HypertextPool
{
public:
    static constexpr std::uint32_t kMaxPlainBytes = 8192;
    static constexpr std::uint32_t kMaxTargetBytes = 2048;
    static constexpr std::uint32_t kMaxLinks = 256;

    void Clear() noexcept;

    // Parses inline links and copies the visible text into the pool. Returns a
    [[nodiscard]] HypertextComponent Intern(std::string_view InMarkup, std::uint32_t InLocalizationId = 0,
                                            std::uint16_t InFlags = 0) noexcept;
    // Same parser, named for the call site that has a flatbuffers::String view.
    [[nodiscard]] HypertextComponent InternFlatBufferString(std::string_view InUtf8, std::uint32_t InLocalizationId = 0,
                                                            std::uint16_t InFlags = 0) noexcept
    {
        return Intern(InUtf8, InLocalizationId, InFlags);
    }

    [[nodiscard]] HypertextView View(const HypertextComponent& InComponent) const noexcept;
    // Resolves the component against a localization pack. NoLocalize bypasses
    [[nodiscard]] HypertextView Resolve(const LocalizationTable& InTable,
                                        const HypertextComponent& InComponent) const noexcept;

    [[nodiscard]] std::string_view Target(const HyperlinkRange& InLink) const noexcept;
    // Returns nullptr when the byte is not inside a link. `byteOffset` is
    [[nodiscard]] const HyperlinkRange* LinkAt(const HypertextView& InView, std::uint32_t InByteOffset) const noexcept;
    [[nodiscard]] std::uint32_t LinkCount(const HypertextComponent& InComponent) const noexcept
    {
        return InComponent.LinkCount;
    }

private:
    [[nodiscard]] bool AppendChar(char InValue) noexcept;
    [[nodiscard]] bool AppendTarget(std::string_view InTarget, std::uint16_t* InOffset,
                                    std::uint16_t* InLength) noexcept;

    std::array<char, kMaxPlainBytes> MPlain{};
    std::array<char, kMaxTargetBytes> MTargets{};
    std::array<HyperlinkRange, kMaxLinks> MLinks{};
    std::uint32_t MPlainUsed = 0;
    std::uint32_t MTargetUsed = 0;
    std::uint32_t MLinkUsed = 0;
};

// Optional bridge into Pink UI: a label displays the plain text, while a click
[[nodiscard]] bool HyperlinkClicked(const HypertextView& InView, std::uint32_t InByteOffset,
                                    std::string_view* OutTarget, std::uint16_t* OutFlags,
                                    const HypertextPool& InPool) noexcept;
}
}

FBY_REFLECT_COMPONENT(Fby::Ui::HypertextComponent, "HypertextComponent",
                      FBY_FIELD(Fby::Ui::HypertextComponent, LocalizationID, UInt32),
                      FBY_FIELD(Fby::Ui::HypertextComponent, PlainOffset, UInt32),
                      FBY_FIELD(Fby::Ui::HypertextComponent, PlainLength, UInt32),
                      FBY_FIELD(Fby::Ui::HypertextComponent, FirstLink, UInt32),
                      FBY_FIELD(Fby::Ui::HypertextComponent, LinkCount, UInt32),
                      FBY_FIELD(Fby::Ui::HypertextComponent, Flags, UInt32))
