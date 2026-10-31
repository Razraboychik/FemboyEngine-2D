// FemboyEngine2D - fixed-capacity shape cache tests.
#include "Engine/Text/TextShapeCache.hpp"

#include <cstdio>
#include <memory>

namespace
{
int GFailures = 0;

void Check(bool InCondition, const char* InMessage)
{
    if (!InCondition)
    {
        std::fprintf(stderr, "FAIL: %s\n", InMessage);
        ++GFailures;
    }
}
}

int main()
{
    using namespace Fby;
    Text::CodepointShaper ScopedShaper;
    Text::TextShapeCache ScopedCache;
    Text::TextShapeKey ScopedKey;
    ScopedKey.TextHash = Text::TextShapeCache::HashText("AA");
    ScopedKey.FontId = 0;
    ScopedKey.TextLength = 2;
    ScopedKey.PixelSize = 16.0f;
    Text::TextLayoutParams ScopedLayoutParams;
    ScopedLayoutParams.MaxWidth = 64.0f;
    ScopedLayoutParams.FontSize = 16.0f;

    const Text::TextShapeHandle ScopedFirst =
        ScopedCache.Insert(ScopedShaper, "AA", ScopedKey, ScopedLayoutParams);
    Check(ScopedFirst.IsValid(), "shape cache inserts a shaped entry");
    Check(ScopedCache.Find(ScopedKey) == ScopedFirst, "shape cache finds the same key");
    const Text::TextShapeView ScopedView = ScopedCache.View(ScopedFirst);
    Check(ScopedView.Glyphs.size() == 2, "shape cache view exposes glyphs");
    Check(ScopedView.Lines.size() == 1, "shape cache view exposes lines");
    Check(ScopedView.Width > 0.0f, "shape cache records line width");

    const Text::TextShapeHandle ScopedSecond =
        ScopedCache.Insert(ScopedShaper, "AA", ScopedKey, ScopedLayoutParams);
    Check(ScopedSecond == ScopedFirst, "shape cache reuses a hot key");
    Check(ScopedCache.Count() == 1, "shape cache does not duplicate a hot key");
    Check(ScopedShaper.ShapeCalls == 1, "shape cache does not re-shape a hot key");

    ScopedCache.Clear();
    Check(!ScopedCache.View(ScopedFirst).Glyphs.size(), "cleared cache invalidates handles");
    Check(ScopedCache.Count() == 0, "cleared cache is empty");

    auto ScopedBounded = std::make_unique<Text::TextShapeCache>();
    Text::TextShapeHandle ScopedLast;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < Text::kMaxShapeGlyphs + 2u; ++ScopedIndex)
    {
        Text::TextShapeKey ScopedEntryKey;
        ScopedEntryKey.TextHash = Text::TextShapeCache::HashText("A");
        ScopedEntryKey.FontId = ScopedIndex;
        ScopedEntryKey.TextLength = 1;
        ScopedEntryKey.PixelSize = 16.0f;
        ScopedLast = ScopedBounded->Insert(ScopedShaper, "A", ScopedEntryKey, ScopedLayoutParams);
    }
    Check(ScopedBounded->Evictions() > 0, "shape cache evicts when the glyph pool is full");
    Check(ScopedLast.IsValid() && ScopedBounded->View(ScopedLast).Glyphs.size() == 1,
          "shape cache remains usable after eviction");

    if (GFailures != 0)
    {
        std::fprintf(stderr, "%d shape cache test(s) failed\n", GFailures);
        return 1;
    }
    std::printf("shape cache tests: ok\n");
    return 0;
}
