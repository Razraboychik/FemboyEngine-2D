// FemboyEngine2D - asset license registry test.
#include "Engine/Assets/LicenseRegistry.hpp"
#include "Engine/Assets/VFS.hpp"
#include "Engine/Text/BaseFonts.hpp"

#include <cstdio>
#include <string>

int main()
{
    Fby::VirtualFileSystem ScopedVfs;
    std::string ScopedError;
    if (!ScopedVfs.MountDirectory("assets", 0, &ScopedError))
    {
        std::fprintf(stderr, "FAIL: mount assets: %s\n", ScopedError.c_str());
        return 1;
    }
    Fby::Assets::LicenseRegistry ScopedLicenses;
    if (!ScopedLicenses.LoadFromVfs(ScopedVfs, "Licenses", &ScopedError))
    {
        std::fprintf(stderr, "FAIL: load licenses: %s\n", ScopedError.c_str());
        return 1;
    }
    if (ScopedLicenses.Count() < 3)
    {
        std::fprintf(stderr, "FAIL: expected at least four bundled licenses, got %zu\n", ScopedLicenses.Count());
        return 1;
    }
    if (ScopedLicenses.Text("Noto-OFL").find("SIL OPEN FONT LICENSE") == std::string_view::npos)
    {
        std::fprintf(stderr, "FAIL: Noto license text is missing\n");
        return 1;
    }
    if (!Fby::Text::BindBaseFontLicenses(ScopedLicenses, ScopedVfs, &ScopedError))
    {
        std::fprintf(stderr, "FAIL: bind base font licenses: %s\n", ScopedError.c_str());
        return 1;
    }
    ScopedLicenses.BindAsset("images/hero.png", "Custom-Author-Notice");
    ScopedLicenses.Add("Custom-Author-Notice", "Author: Example\nUsed with permission.");
    if (ScopedLicenses.LicenseForAsset("images/hero.png") != "Custom-Author-Notice")
    {
        std::fprintf(stderr, "FAIL: asset license binding is missing\n");
        return 1;
    }
    if (ScopedLicenses.Text("Custom-Author-Notice") != "Author: Example\nUsed with permission.")
    {
        std::fprintf(stderr, "FAIL: custom notice text is missing\n");
        return 1;
    }
    Fby::Assets::AssetMeta ScopedMeta;
    std::snprintf(ScopedMeta.sourcePath, sizeof(ScopedMeta.sourcePath), "%s", "images/hero.png");
    std::snprintf(ScopedMeta.license, sizeof(ScopedMeta.license), "%s", "Noto-OFL");
    if (!ScopedLicenses.BindAssetMeta(ScopedMeta, &ScopedError))
    {
        std::fprintf(stderr, "FAIL: bind asset metadata: %s\n", ScopedError.c_str());
        return 1;
    }
    if (ScopedLicenses.LicenseForAsset("images/hero.png") != "Noto-OFL")
    {
        std::fprintf(stderr, "FAIL: asset metadata binding is missing\n");
        return 1;
    }
    if (Fby::Text::BaseFontCatalog().size() < 7)
    {
        std::fprintf(stderr, "FAIL: base font catalogue is too small\n");
        return 1;
    }
    Fby::Text::TextFontStack ScopedStack;
    const std::uint32_t ScopedMounted = Fby::Text::MountBaseFonts(ScopedStack, ScopedVfs, &ScopedError);
    if (ScopedMounted != Fby::Text::BaseFontCatalog().size())
    {
        std::fprintf(stderr, "FAIL: mounted %u/%zu base fonts: %s\n", ScopedMounted,
                     Fby::Text::BaseFontCatalog().size(), ScopedError.c_str());
        return 1;
    }
    std::printf("license_registry: OK (%zu licenses, %zu base fonts)\n", ScopedLicenses.Count(),
                Fby::Text::BaseFontCatalog().size());
    return 0;
}