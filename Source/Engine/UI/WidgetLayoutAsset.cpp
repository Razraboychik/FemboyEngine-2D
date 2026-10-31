// FemboyEngine2D - widget layout asset implementation.
#include "Engine/UI/WidgetLayoutAsset.hpp"

#include <toml.hpp>

#include <array>
#include <optional>
#include <cstdio>
#include <sstream>

namespace Fby::Ui
{
namespace
{
[[nodiscard]] const char* KindName(WidgetKind InKind) noexcept
{
    switch (InKind)
    {
        case WidgetKind::Panel:
            return "Panel";
        case WidgetKind::Button:
            return "Button";
        case WidgetKind::Label:
            return "Label";
        case WidgetKind::Image:
            return "Image";
        case WidgetKind::ProgressBar:
            return "ProgressBar";
        case WidgetKind::Hypertext:
            return "Hypertext";
        case WidgetKind::Spacer:
            return "Spacer";
        case WidgetKind::Border:
            return "Border";
        case WidgetKind::Slider:
            return "Slider";
        case WidgetKind::Checkbox:
            return "Checkbox";
        case WidgetKind::Scrollbar:
            return "Scrollbar";
        case WidgetKind::Switcher:
            return "Switcher";
        case WidgetKind::Overlay:
            return "Overlay";
        case WidgetKind::Custom:
            return "Custom";
        case WidgetKind::Tooltip:
            return "Tooltip";
        case WidgetKind::TextInput:
            return "TextInput";
    }
    return "Panel";
}

[[nodiscard]] bool ParseKind(std::string_view InName, WidgetKind* OutKind) noexcept
{
    constexpr std::array<std::pair<std::string_view, WidgetKind>, 16> kKinds{{
        {"Panel", WidgetKind::Panel},
        {"Button", WidgetKind::Button},
        {"Label", WidgetKind::Label},
        {"Image", WidgetKind::Image},
        {"ProgressBar", WidgetKind::ProgressBar},
        {"Hypertext", WidgetKind::Hypertext},
        {"Spacer", WidgetKind::Spacer},
        {"Border", WidgetKind::Border},
        {"Slider", WidgetKind::Slider},
        {"Checkbox", WidgetKind::Checkbox},
        {"Scrollbar", WidgetKind::Scrollbar},
        {"Switcher", WidgetKind::Switcher},
        {"Overlay", WidgetKind::Overlay},
        {"Custom", WidgetKind::Custom},
        {"Tooltip", WidgetKind::Tooltip},
        {"TextInput", WidgetKind::TextInput},
    }};
    for (const auto& [ScopedName, ScopedKind] : kKinds)
    {
        if (ScopedName == InName)
        {
            *OutKind = ScopedKind;
            return true;
        }
    }
    return false;
}

[[nodiscard]] Vector2D ReadVector2(const toml::node* InNode, Vector2D InFallback) noexcept
{
    if (InNode == nullptr || !InNode->is_array())
    {
        return InFallback;
    }
    const toml::array& ScopedArray = *InNode->as_array();
    if (ScopedArray.size() < 2)
    {
        return InFallback;
    }
    const double ScopedX = ScopedArray[0].value<double>().value_or(static_cast<double>(InFallback.X));
    const double ScopedY = ScopedArray[1].value<double>().value_or(static_cast<double>(InFallback.Y));
    return Vector2D{static_cast<float>(ScopedX), static_cast<float>(ScopedY)};
}

void WriteVector2(std::ostringstream& OutText, const char* InName, Vector2D InValue)
{
    OutText << InName << " = [" << InValue.X << ", " << InValue.Y << "]\n";
}
}

std::string WidgetLayoutAsset::Serialize(const WidgetCanvas& InCanvas)
{
    std::ostringstream ScopedOut;
    ScopedOut << "version = 1\n";
    for (std::uint32_t ScopedWidget = 0; ScopedWidget < InCanvas.widgetCount; ++ScopedWidget)
    {
        ScopedOut << "\n[[widget]]\n";
        ScopedOut << "kind = \"" << KindName(InCanvas.kinds[ScopedWidget]) << "\"\n";
        ScopedOut << "parent = " << InCanvas.parents[ScopedWidget] << "\n";
        WriteVector2(ScopedOut, "anchorMin", InCanvas.anchorMins[ScopedWidget]);
        WriteVector2(ScopedOut, "anchorMax", InCanvas.anchorMaxs[ScopedWidget]);
        WriteVector2(ScopedOut, "offsetMin", InCanvas.offsetMins[ScopedWidget]);
        WriteVector2(ScopedOut, "offsetMax", InCanvas.offsetMaxs[ScopedWidget]);
        ScopedOut << "flags = " << InCanvas.flags[ScopedWidget] << "\n";
        const std::string_view ScopedText = InCanvas.Text(static_cast<std::int32_t>(ScopedWidget));
        if (!ScopedText.empty())
        {
            ScopedOut << "text = \"";
            for (const char ScopedChar : ScopedText)
            {
                if (ScopedChar == '"' || ScopedChar == '\\')
                {
                    ScopedOut << '\\';
                }
                ScopedOut << ScopedChar;
            }
            ScopedOut << "\"\n";
        }
        const std::uint32_t ScopedKey = InCanvas.TextKey(static_cast<std::int32_t>(ScopedWidget));
        if (ScopedKey != 0xFFFFFFFFu)
        {
            ScopedOut << "textKey = " << ScopedKey << "\n";
        }
    }
    return ScopedOut.str();
}

bool WidgetLayoutAsset::Load(WidgetCanvas& InCanvas, std::string_view InToml, std::string* InError)
{
    toml::table ScopedRoot;
    try
    {
        ScopedRoot = toml::parse(InToml);
    }
    catch (const toml::parse_error& ScopedError)
    {
        if (InError != nullptr)
        {
            *InError = std::string("layout TOML parse error: ") + ScopedError.description().data();
        }
        return false;
    }
    const toml::node* ScopedVersion = ScopedRoot.get("version");
    if (ScopedVersion == nullptr || ScopedVersion->value_or(0) != 1)
    {
        if (InError != nullptr)
        {
            *InError = "layout version must be 1";
        }
        return false;
    }
    const toml::node* ScopedWidgets = ScopedRoot.get("widget");
    if (ScopedWidgets == nullptr || !ScopedWidgets->is_array())
    {
        if (InError != nullptr)
        {
            *InError = "layout has no [[widget]] entries";
        }
        return false;
    }

    // Loading a layout must not change the canvas size.
    const Vector2D ScopedSize = InCanvas.size;
    InCanvas.Clear();
    InCanvas.SetCanvasSize(ScopedSize);
    const toml::array& ScopedArray = *ScopedWidgets->as_array();
    for (const toml::node& ScopedNode : ScopedArray)
    {
        if (!ScopedNode.is_table())
        {
            continue;
        }
        const toml::table& ScopedTable = *ScopedNode.as_table();
        WidgetDesc ScopedDesc;
        WidgetKind ScopedKind = WidgetKind::Panel;
        const toml::node* ScopedKindNode = ScopedTable.get("kind");
        const std::optional<std::string_view> ScopedKindName =
            ScopedKindNode != nullptr ? ScopedKindNode->value<std::string_view>() : std::nullopt;
        if (!ScopedKindName.has_value() || !ParseKind(*ScopedKindName, &ScopedKind))
        {
            if (InError != nullptr)
            {
                *InError = "layout widget has an unknown kind";
            }
            InCanvas.Clear();
            return false;
        }
        ScopedDesc.kind = ScopedKind;
        if (const toml::node* ScopedParentNode = ScopedTable.get("parent");
            ScopedParentNode != nullptr && ScopedParentNode->is_integer())
        {
            ScopedDesc.parent = static_cast<std::int32_t>(ScopedParentNode->value<int64_t>().value_or(-1));
        }
        if (ScopedDesc.parent >= static_cast<std::int32_t>(InCanvas.widgetCount))
        {
            if (InError != nullptr)
            {
                *InError = "layout widget references a parent that does not exist yet";
            }
            InCanvas.Clear();
            return false;
        }
        ScopedDesc.anchorMin = ReadVector2(ScopedTable.get("anchorMin"), Vector2D{0.0f, 0.0f});
        ScopedDesc.anchorMax = ReadVector2(ScopedTable.get("anchorMax"), Vector2D{1.0f, 1.0f});
        ScopedDesc.offsetMin = ReadVector2(ScopedTable.get("offsetMin"), Vector2D{0.0f, 0.0f});
        ScopedDesc.offsetMax = ReadVector2(ScopedTable.get("offsetMax"), Vector2D{0.0f, 0.0f});
        if (const toml::node* ScopedFlagsNode = ScopedTable.get("flags");
            ScopedFlagsNode != nullptr && ScopedFlagsNode->is_integer())
        {
            ScopedDesc.flags = static_cast<std::uint32_t>(ScopedFlagsNode->value<int64_t>().value_or(0));
        }
        const std::int32_t ScopedWidget = InCanvas.AddWidget(ScopedDesc);
        const toml::node* ScopedTextNode = ScopedTable.get("text");
        if (const std::optional<std::string_view> ScopedText = ScopedTextNode != nullptr
                                                                   ? ScopedTextNode->value<std::string_view>()
                                                                   : std::nullopt;
            ScopedText.has_value())
        {
            (void)InCanvas.SetText(ScopedWidget, *ScopedText);
        }
        const toml::node* ScopedKeyNode = ScopedTable.get("textKey");
        if (ScopedKeyNode != nullptr && ScopedKeyNode->is_integer())
        {
            InCanvas.SetTextKey(ScopedWidget,
                                static_cast<std::uint32_t>(ScopedKeyNode->value<int64_t>().value_or(0)));
        }
    }
    return true;
}
}
