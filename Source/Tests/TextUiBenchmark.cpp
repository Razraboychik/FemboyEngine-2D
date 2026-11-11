// FemboyEngine2D - shaped-text hot-path benchmark for UI text.
#include "Engine/Assets/VFS.hpp"
#include "Engine/Text/TextShapeCache.hpp"
#include "Engine/Text/TextShaper.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

int main()
{
    using namespace Fby;

    VirtualFileSystem ScopedVfs;
    std::string ScopedError;
    if (!ScopedVfs.MountDirectory("assets", 0, &ScopedError))
    {
        std::fprintf(stderr, "text ui benchmark: cannot mount assets: %s\n", ScopedError.c_str());
        return 1;
    }
    const auto ScopedBytes = ScopedVfs.Read("fonts/Inter-Regular.ttf");
    if (!ScopedBytes)
    {
        std::fprintf(stderr, "text ui benchmark: the Latin face is missing\n");
        return 1;
    }
    Text::HarfBuzzShaper ScopedShaper;
    if (!ScopedShaper.SetFont(*ScopedBytes, 16.0f, 0))
    {
        std::fprintf(stderr, "text ui benchmark: cannot load the shaper\n");
        return 1;
    }

    Text::TextShapeCache ScopedCache;
    Text::TextLayoutParams ScopedLayout;
    ScopedLayout.MaxWidth = 240.0f;
    ScopedLayout.FontSize = 16.0f;
    const std::string_view ScopedText = "Hello, UI text hot path";
    Text::TextShapeKey ScopedKey;
    ScopedKey.TextHash = Text::TextShapeCache::HashText(ScopedText);
    ScopedKey.FontId = 0;
    ScopedKey.TextLength = static_cast<std::uint32_t>(ScopedText.size());
    ScopedKey.PixelSize = 16.0f;
    ScopedKey.Direction = Text::TextDirection::LeftToRight;
    ScopedKey.LocaleHash = 0;
    const Text::TextShapeHandle ScopedHandle = ScopedCache.Insert(ScopedShaper, ScopedText, ScopedKey, ScopedLayout);
    if (!ScopedHandle.IsValid())
    {
        std::fprintf(stderr, "text ui benchmark: initial shape failed\n");
        return 1;
    }

    constexpr std::uint32_t kIterations = 1'000'000;
    std::uint64_t ScopedChecksum = 0;
    for (std::uint32_t ScopedIteration = 0; ScopedIteration < 1000; ++ScopedIteration)
    {
        const Text::TextShapeHandle ScopedLookup = ScopedCache.Find(ScopedKey);
        ScopedChecksum += ScopedCache.View(ScopedLookup).Glyphs.size();
    }
    const auto ScopedStart = std::chrono::steady_clock::now();
    for (std::uint32_t ScopedIteration = 0; ScopedIteration < kIterations; ++ScopedIteration)
    {
        const Text::TextShapeHandle ScopedLookup = ScopedCache.Find(ScopedKey);
        const Text::TextShapeView ScopedView = ScopedCache.View(ScopedLookup);
        ScopedChecksum += ScopedView.Glyphs.size();
    }
    const auto ScopedEnd = std::chrono::steady_clock::now();
    const double ScopedNs =
        std::chrono::duration<double, std::nano>(ScopedEnd - ScopedStart).count() / static_cast<double>(kIterations);
    std::printf("text_ui_benchmark: %u cache hits, %.2f ns/hit, checksum=%llu, hits=%llu, misses=%llu\n", kIterations,
                ScopedNs, static_cast<unsigned long long>(ScopedChecksum),
                static_cast<unsigned long long>(ScopedCache.Hits()),
                static_cast<unsigned long long>(ScopedCache.Misses()));
    return 0;
}
