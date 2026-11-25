// FemboyEngine2D - bundled base fonts and their license bindings.
#include "Engine/Text/BaseFonts.hpp"

#include <array>

namespace Fby::Text
{
namespace
{
constexpr std::array<BaseFontInfo, 7> kBaseFonts{{
    {"inter.latin", "fonts/Inter-Regular.ttf", "Inter-OFL", BaseFontRole::Sans, TextScript::Latin, 32.0f, 2048,
     false, false},
    {"inter.cyrillic", "fonts/Inter-Regular.ttf", "Inter-OFL", BaseFontRole::Sans, TextScript::Cyrillic, 32.0f, 2048,
     false, false},
    {"noto.hebrew", "fonts/tests/NotoSansHebrew-Regular.ttf", "Noto-OFL", BaseFontRole::Sans, TextScript::Hebrew,
     32.0f, 2048, false, true},
    {"noto.arabic", "fonts/tests/NotoSansArabic-Regular.ttf", "Noto-OFL", BaseFontRole::Sans, TextScript::Arabic,
     32.0f, 2048, false, true},
    {"noto.devanagari", "fonts/tests/NotoSansDevanagari-Regular.ttf", "Noto-OFL", BaseFontRole::Sans,
     TextScript::Devanagari, 32.0f, 2048, false, true},
    {"noto.thai", "fonts/tests/NotoSansThai-Regular.ttf", "Noto-OFL", BaseFontRole::Sans, TextScript::Thai, 32.0f,
     2048, false, true},
    {"noto.han", "fonts/tests/NotoSansSC-Regular.ttf", "Noto-OFL", BaseFontRole::Sans, TextScript::Han, 32.0f, 1024,
     true, true},
}};
}

std::span<const BaseFontInfo> BaseFontCatalog() noexcept
{
    return kBaseFonts;
}

std::uint32_t MountBaseFonts(TextFontStack& InStack, VirtualFileSystem& InVfs, std::string* OutError)
{
    std::uint32_t ScopedMounted = 0;
    for (const BaseFontInfo& ScopedFont : kBaseFonts)
    {
        const std::uint32_t ScopedId = 100u + ScopedMounted;
        const bool ScopedOk =
            ScopedFont.Paged
                ? InStack.AddPagedFont(InVfs, ScopedFont.Path, ScopedId, ScopedFont.Script, ScopedFont.PixelSize,
                                       ScopedFont.AtlasSize, OutError)
                : InStack.AddFont(InVfs, ScopedFont.Path, ScopedId, ScopedFont.Script, ScopedFont.PixelSize,
                                  ScopedFont.AtlasSize, ScopedFont.AllGlyphs, OutError);
        if (!ScopedOk)
        {
            if (OutError != nullptr && OutError->empty())
            {
                *OutError = "cannot mount base font: " + std::string(ScopedFont.Path);
            }
            return ScopedMounted;
        }
        ++ScopedMounted;
    }
    return ScopedMounted;
}

bool BindBaseFontLicenses(Assets::LicenseRegistry& InLicenses, VirtualFileSystem& InVfs, std::string* OutError)
{
    if (!InLicenses.LoadFromVfs(InVfs, "Licenses", OutError))
    {
        return false;
    }
    for (const BaseFontInfo& ScopedFont : kBaseFonts)
    {
        if (InLicenses.Find(ScopedFont.LicenseId) == nullptr)
        {
            if (OutError != nullptr)
            {
                *OutError = "missing bundled font license: " + std::string(ScopedFont.LicenseId);
            }
            return false;
        }
        InLicenses.BindAsset(ScopedFont.Path, ScopedFont.LicenseId);
    }
    return true;
}

}