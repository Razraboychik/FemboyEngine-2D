// FemboyEngine2D - text placeholder formatting test.
#include "Engine/Core/AssetId.hpp"
#include "Engine/Core/BigInt.hpp"
#include "Engine/Core/Decimal.hpp"
#include "Engine/Text/TextFormat.hpp"
#include "Engine/Text/TextService.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>

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

void CheckString(std::string_view InActual, std::string_view InExpected, const char* InMessage)
{
    if (InActual != InExpected)
    {
        std::fprintf(stderr, "FAIL: %s (actual '%.*s', expected '%.*s')\n", InMessage,
                     static_cast<int>(InActual.size()), InActual.data(), static_cast<int>(InExpected.size()),
                     InExpected.data());
        ++GFailures;
    }
}

void TestNumbers()
{
    using namespace Fby;

    BigNum::Int128 ScopedBig;
    Check(BigNum::Int128::FromString("1000000000000000000000000000000", &ScopedBig) == BigNum::Status::Ok,
          "placeholder bigint parses");
    BigNum::Decimal128 ScopedDecimal;
    Check(BigNum::Decimal128::FromString("-123.45", &ScopedDecimal) == BigNum::Status::Ok,
          "placeholder decimal parses");

    const std::array<Text::TextFormatArg, 2> ScopedArgs{
        Text::MakeBigIntArg(&ScopedBig),
        Text::MakeDecimalArg(&ScopedDecimal),
    };
    std::array<char, 128> ScopedBuffer{};
    std::size_t ScopedSize = 0;
    const std::string_view ScopedTemplate = "gold={0}; price={1}; literal={{x}}";
    Check(Text::FormatTextTemplate(ScopedTemplate, std::span<const Text::TextFormatArg>(ScopedArgs),
                                   std::span<char>(ScopedBuffer), &ScopedSize) == Text::TextFormatStatus::Ok,
          "number placeholders format");
    CheckString(std::string_view(ScopedBuffer.data(), ScopedSize),
                "gold=1000000000000000000000000000000; price=-123.45; literal={x}", "number placeholder value");
}

void TestPackedSpecs()
{
    using namespace Fby;

    BigNum::FormatSpec ScopedSpec;
    ScopedSpec.Style = BigNum::Notation::Scientific;
    ScopedSpec.SignificantDigits = 4;
    ScopedSpec.ExponentMarker = 'e';
    const std::uint32_t ScopedPacked = Text::PackFormatSpec(ScopedSpec);
    const BigNum::FormatSpec ScopedDecoded = Text::UnpackFormatSpec(ScopedPacked);
    Check(ScopedDecoded.Style == BigNum::Notation::Scientific, "packed format style");
    Check(ScopedDecoded.SignificantDigits == 4, "packed significant digits");
    Check(ScopedDecoded.ExponentMarker == 'e', "packed exponent marker");
}

void TestStatuses()
{
    using namespace Fby;

    BigNum::Int128 ScopedBig = BigNum::Int128::FromInt64(42);
    const std::array<Text::TextFormatArg, 1> ScopedArgs{Text::MakeBigIntArg(&ScopedBig)};
    std::array<char, 4> ScopedSmall{};
    Check(Text::FormatTextTemplate("value={0}", std::span<const Text::TextFormatArg>(ScopedArgs),
                                   std::span<char>(ScopedSmall), nullptr) == Text::TextFormatStatus::BufferTooSmall,
          "placeholder small buffer rejects");

    std::array<char, 32> ScopedBuffer{};
    Check(Text::FormatTextTemplate("value={1}", std::span<const Text::TextFormatArg>(ScopedArgs),
                                   std::span<char>(ScopedBuffer), nullptr) == Text::TextFormatStatus::MissingArgument,
          "placeholder missing argument rejects");
    Check(Text::FormatTextTemplate("value={", std::span<const Text::TextFormatArg>(ScopedArgs),
                                   std::span<char>(ScopedBuffer), nullptr) == Text::TextFormatStatus::InvalidTemplate,
          "placeholder invalid template rejects");
    Check(Text::FormatTextTemplate("plain", {}, std::span<char>(ScopedBuffer), nullptr) == Text::TextFormatStatus::Ok,
          "plain template formats");
    CheckString(std::string_view(ScopedBuffer.data(), 5), "plain", "plain template value");
}

void TestServiceFormatting()
{
    using namespace Fby;

    auto ScopedService = std::make_unique<Text::TextService>();
    const AssetId ScopedAsset = AssetId::FromPath("text/gold.txt", AssetKind::Text);
    Check(ScopedService->AddAsset(ScopedAsset, "Gold: {0}"), "format asset stores");
    Check(ScopedService->HasAsset(ScopedAsset), "text service reports a stored asset");
    const Text::TextAssetRef ScopedRef = ScopedService->MakeRef(ScopedAsset);
    BigNum::Int128 ScopedBig = BigNum::Int128::FromInt64(42);
    const std::array<Text::TextFormatArg, 1> ScopedArgs{Text::MakeBigIntArg(&ScopedBig)};
    std::array<char, 64> ScopedBuffer{};
    std::size_t ScopedSize = 0;
    Check(ScopedService->FormatBlock(ScopedRef, std::span<const Text::TextFormatArg>(ScopedArgs),
                                     std::span<char>(ScopedBuffer), &ScopedSize) == Text::TextFormatStatus::Ok,
          "service formats a block");
    CheckString(std::string_view(ScopedBuffer.data(), ScopedSize), "Gold: 42", "service formatted value");

    std::string_view ScopedCached;
    Check(ScopedService->FormatCached(ScopedRef, 0xC0FFEEull,
                                      std::span<const Text::TextFormatArg>(ScopedArgs), &ScopedCached),
          "service formats into the cache");
    CheckString(ScopedCached, "Gold: 42", "cached formatted value");
    Check(ScopedService->FormatCacheMisses() == 1, "formatted cache miss");
    Check(ScopedService->FormatCached(ScopedRef, 0xC0FFEEull,
                                      std::span<const Text::TextFormatArg>(ScopedArgs), &ScopedCached),
          "service reuses the formatted cache");
    Check(ScopedService->FormatCacheHits() == 1, "formatted cache hit");

    Text::TextLayoutParams ScopedLayoutParams;
    ScopedLayoutParams.MaxWidth = 128.0f;
    ScopedLayoutParams.Advance = 8.0f;
    const Text::TextLayoutResult* ScopedLayout =
        ScopedService->CachedFormattedLayout("cached text", ScopedLayoutParams);
    Check(ScopedLayout != nullptr && ScopedLayout->Count == 1, "formatted layout cache miss");
    Check(ScopedService->FormattedLayoutMisses() == 1, "formatted layout miss count");
    const Text::TextLayoutResult* ScopedSecond = ScopedService->CachedFormattedLayout("cached text", ScopedLayoutParams);
    Check(ScopedSecond == ScopedLayout, "formatted layout cache hit pointer");
    Check(ScopedService->FormattedLayoutHits() == 1, "formatted layout hit count");
}
}

int main()
{
    TestNumbers();
    TestPackedSpecs();
    TestStatuses();
    TestServiceFormatting();

    if (GFailures != 0)
    {
        std::fprintf(stderr, "%d text format test(s) failed\n", GFailures);
        return 1;
    }
    std::printf("text format tests: ok\n");
    return 0;
}
