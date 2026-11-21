// FemboyEngine2D - view and simulation profile shared by projects and scenes.
#pragma once

#include <cstdint>
#include <string_view>

namespace Fby
{
enum class ViewProfile : std::uint8_t
{
    Default = 0,
    SideView = 1,
    TopDown = 2,
    Isometric = 3,
    Hex = 4,
    Count
};

[[nodiscard]] constexpr const char* ViewProfileName(ViewProfile InProfile) noexcept
{
    switch (InProfile)
    {
        case ViewProfile::SideView:
            return "Side View";
        case ViewProfile::TopDown:
            return "Top-Down";
        case ViewProfile::Isometric:
            return "Isometric";
        case ViewProfile::Hex:
            return "Hex Isometric";
        default:
            return "Default";
    }
}

[[nodiscard]] constexpr std::string_view ViewProfileKey(ViewProfile InProfile) noexcept
{
    switch (InProfile)
    {
        case ViewProfile::SideView:
            return "side-view";
        case ViewProfile::TopDown:
            return "top-down";
        case ViewProfile::Isometric:
            return "isometric";
        case ViewProfile::Hex:
            return "hex";
        default:
            return "default";
    }
}

[[nodiscard]] constexpr bool ParseViewProfile(std::string_view InKey, ViewProfile* OutProfile) noexcept
{
    for (std::uint8_t ScopedIndex = 0; ScopedIndex < static_cast<std::uint8_t>(ViewProfile::Count); ++ScopedIndex)
    {
        const ViewProfile ScopedProfile = static_cast<ViewProfile>(ScopedIndex);
        if (InKey == ViewProfileKey(ScopedProfile))
        {
            if (OutProfile != nullptr)
            {
                *OutProfile = ScopedProfile;
            }
            return true;
        }
    }
    return false;
}
}
