// FemboyEngine2D - shipped third-party attribution records.
#pragma once

#include <span>
#include <string_view>

namespace Fby::Notices
{

// One shipped component. Strings point at static storage generated from
// `third_party/notices.toml`; they stay valid for the whole process lifetime.
struct NoticeEntry
{
    std::string_view Name;
    std::string_view Version;
    std::string_view Spdx;
    std::string_view Purpose;
    std::string_view Copyright;
    std::string_view LicenseText;
};

// Every component that ships inside a binary, in manifest order.
[[nodiscard]] std::span<const NoticeEntry> Entries() noexcept;

// The complete notices document, identical to the shipped THIRD_PARTY_NOTICES.md.
// Games expose this from an in-game "Licenses" screen.
[[nodiscard]] std::string_view AllText() noexcept;
}
