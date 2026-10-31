// FemboyEngine2D - no-allocation hot path for the shaped text cache.
#include "Engine/Assets/VFS.hpp"
#include "Engine/Text/TextShapeCache.hpp"
#include "Engine/Text/TextShaper.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include <string_view>

namespace
{
std::atomic<std::uint64_t> GAllocations{0};
std::atomic<std::uint64_t> GDeallocations{0};

int GFailures = 0;

void Check(bool InCondition, const char* InMessage)
{
    if (!InCondition)
    {
        std::fprintf(stderr, "FAIL: %s\n", InMessage);
        ++GFailures;
    }
}

// Wraps a real shaper so the test can count how often the cache reaches it.
class CountingShaper final : public Fby::Text::ITextShaper
{
public:
    explicit CountingShaper(Fby::Text::ITextShaper& InInner) : MInner(InInner)
    {
    }

    [[nodiscard]] bool Shape(std::string_view InText, const Fby::Text::TextShapeParams& InParams,
                             Fby::Text::TextShapeResult* OutResult) noexcept override
    {
        ++ShapeCalls;
        return MInner.Shape(InText, InParams, OutResult);
    }

    std::uint32_t ShapeCalls = 0;

private:
    Fby::Text::ITextShaper& MInner;
};
}

// Global allocator accounting: a cache hit must not reach the heap.
void* operator new(std::size_t InSize)
{
    GAllocations.fetch_add(1, std::memory_order_relaxed);
    void* ScopedMemory = std::malloc(InSize != 0 ? InSize : 1);
    if (ScopedMemory == nullptr)
    {
        throw std::bad_alloc();
    }
    return ScopedMemory;
}

void* operator new[](std::size_t InSize)
{
    GAllocations.fetch_add(1, std::memory_order_relaxed);
    void* ScopedMemory = std::malloc(InSize != 0 ? InSize : 1);
    if (ScopedMemory == nullptr)
    {
        throw std::bad_alloc();
    }
    return ScopedMemory;
}

void operator delete(void* InMemory) noexcept
{
    if (InMemory != nullptr)
    {
        GDeallocations.fetch_add(1, std::memory_order_relaxed);
        std::free(InMemory);
    }
}

void operator delete[](void* InMemory) noexcept
{
    if (InMemory != nullptr)
    {
        GDeallocations.fetch_add(1, std::memory_order_relaxed);
        std::free(InMemory);
    }
}

void operator delete(void* InMemory, std::size_t) noexcept
{
    operator delete(InMemory);
}

void operator delete[](void* InMemory, std::size_t) noexcept
{
    operator delete[](InMemory);
}

int main()
{
    using namespace Fby;

    VirtualFileSystem ScopedVfs;
    std::string ScopedError;
    Check(ScopedVfs.MountDirectory("assets", 0, &ScopedError), "hot path test mounts the assets");
    const auto ScopedBytes = ScopedVfs.Read("fonts/Inter-Regular.ttf");
    if (!ScopedBytes)
    {
        std::fprintf(stderr, "hot path test: the Latin face is missing\n");
        return 1;
    }
    Text::HarfBuzzShaper ScopedShaper;
    Check(ScopedShaper.SetFont(*ScopedBytes, 16.0f, 0), "hot path test loads the Latin shaper");
    CountingShaper ScopedCounting(ScopedShaper);

    Text::TextShapeCache ScopedCache;
    Text::TextLayoutParams ScopedLayout;
    ScopedLayout.MaxWidth = 240.0f;
    ScopedLayout.FontSize = 16.0f;

    const std::string_view ScopedText = "Hello, hot path";
    Text::TextShapeKey ScopedKey;
    ScopedKey.TextHash = Text::TextShapeCache::HashText(ScopedText);
    ScopedKey.FontId = 0;
    ScopedKey.TextLength = static_cast<std::uint32_t>(ScopedText.size());
    ScopedKey.PixelSize = 16.0f;
    ScopedKey.Direction = Text::TextDirection::LeftToRight;
    ScopedKey.LocaleHash = 0;

    const std::uint64_t ScopedMissAllocations = GAllocations.load();
    const Text::TextShapeHandle ScopedFirst =
        ScopedCache.Insert(ScopedCounting, ScopedText, ScopedKey, ScopedLayout);
    Check(ScopedFirst.IsValid(), "the first insert shapes and caches the run");
    Check(ScopedCache.Misses() == 1 && ScopedCache.Lookups() == 1,
          "the first insert counts one lookup and one miss");
    Check(ScopedCounting.ShapeCalls != 0, "a cache miss reaches the shaper");
    Check(!ScopedCache.View(ScopedFirst).Glyphs.empty(), "the miss stores the shaped glyphs");
    (void)ScopedMissAllocations;

    // Cache hit: same key, no shaper call, no allocation.
    const std::uint32_t ScopedCallsAfterMiss = ScopedCounting.ShapeCalls;
    const std::uint64_t ScopedHitAllocations = GAllocations.load();
    const std::uint64_t ScopedHitDeallocations = GDeallocations.load();
    for (std::uint32_t ScopedFrame = 0; ScopedFrame < 120; ++ScopedFrame)
    {
        const Text::TextShapeHandle ScopedHandle = ScopedCache.Find(ScopedKey);
        const Text::TextShapeView ScopedView = ScopedCache.View(ScopedHandle);
        Check(ScopedHandle == ScopedFirst, "every hit returns the cached handle");
        if (ScopedView.Glyphs.empty())
        {
            Check(false, "a cache hit exposes the cached glyphs");
            break;
        }
    }
    Check(ScopedCache.Hits() == 120, "every lookup after the miss is a hit");
    Check(GAllocations.load() == ScopedHitAllocations && GDeallocations.load() == ScopedHitDeallocations,
          "the cache hit path does not allocate");
    Check(ScopedCounting.ShapeCalls == ScopedCallsAfterMiss, "repeated frames never shape again");

    // Repeated insert of the same key is still a hit.
    const std::uint64_t ScopedInsertHitAllocations = GAllocations.load();
    Check(ScopedCache.Insert(ScopedCounting, ScopedText, ScopedKey, ScopedLayout) == ScopedFirst,
          "re-inserting a cached key returns the same handle");
    Check(ScopedCounting.ShapeCalls == ScopedCallsAfterMiss, "the re-insert does not shape");
    Check(GAllocations.load() == ScopedInsertHitAllocations, "the re-insert does not allocate");

    // Misses are bounded by the fixed capacity and the LRU eviction.
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < Text::kMaxShapeEntries + 8u; ++ScopedIndex)
    {
        char ScopedBuffer[64] = {};
        std::snprintf(ScopedBuffer, sizeof(ScopedBuffer), "glyph run %u", ScopedIndex);
        const std::string_view ScopedVariant(ScopedBuffer);
        Text::TextShapeKey ScopedVariantKey;
        ScopedVariantKey.TextHash = Text::TextShapeCache::HashText(ScopedVariant);
        ScopedVariantKey.FontId = 0;
        ScopedVariantKey.TextLength = static_cast<std::uint32_t>(ScopedVariant.size());
        ScopedVariantKey.PixelSize = 16.0f;
        ScopedVariantKey.Direction = Text::TextDirection::LeftToRight;
        (void)ScopedCache.Insert(ScopedCounting, ScopedVariant, ScopedVariantKey, ScopedLayout);
    }
    Check(ScopedCache.Count() <= Text::kMaxShapeEntries, "the cache never exceeds its slot count");
    Check(ScopedCache.Evictions() > 0, "the oldest entry is evicted instead of growing the cache");
    Check(ScopedCache.Misses() == 1 + Text::kMaxShapeEntries + 8u, "every distinct key counts as a miss");
    Check(ScopedCache.Hits() + ScopedCache.Misses() == ScopedCache.Lookups(),
          "hits and misses account for every lookup");

    if (GFailures != 0)
    {
        std::fprintf(stderr, "%d text hot path test(s) failed\n", GFailures);
        return 1;
    }
    std::printf("text hot path tests: ok\n");
    return 0;
}
