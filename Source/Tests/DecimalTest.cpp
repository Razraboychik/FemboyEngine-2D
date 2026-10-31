// FemboyEngine2D - fixed-width decimal acceptance test.
#include "Engine/Core/Decimal.hpp"
#include "Engine/Core/DecimalStore.hpp"
#include "Engine/ECS/ComponentRegistry.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/Scene.hpp"
#include "Engine/ECS/Snapshot.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

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

void CheckString(const std::string& InActual, std::string_view InExpected, const char* InMessage)
{
    if (InActual != InExpected)
    {
        std::fprintf(stderr, "FAIL: %s (actual '%s', expected '%.*s')\n", InMessage, InActual.c_str(),
                     static_cast<int>(InExpected.size()), InExpected.data());
        ++GFailures;
    }
}

void TestParseAndPrint()
{
    using Fby::BigNum::Decimal128;
    using Fby::BigNum::Status;

    Decimal128 ScopedValue;
    Check(Decimal128::FromString("123.45", &ScopedValue) == Status::Ok, "123.45 parses");
    CheckString(ScopedValue.ToString(), "123.45", "123.45 prints");

    Check(Decimal128::FromString("-0.001", &ScopedValue) == Status::Ok, "-0.001 parses");
    CheckString(ScopedValue.ToString(), "-0.001", "-0.001 prints");

    Check(Decimal128::FromString("1.2300", &ScopedValue) == Status::Ok, "trailing zeros parse");
    CheckString(ScopedValue.ToString(), "1.23", "trailing zeros normalize");

    Check(Decimal128::FromString("1.2e3", &ScopedValue) == Status::Ok, "scientific parses");
    CheckString(ScopedValue.ToString(), "1200", "scientific expands");

    Check(Decimal128::FromString("-0", &ScopedValue) == Status::Ok, "negative zero parses");
    CheckString(ScopedValue.ToString(), "0", "negative zero canonicalizes");

    Check(Decimal128::FromString("bad", &ScopedValue) == Status::InvalidArgument, "invalid decimal rejects");
    Check(Decimal128::FromString("1.2.3", &ScopedValue) == Status::InvalidArgument, "double point rejects");
    Check(Decimal128::FromString("1e", &ScopedValue) == Status::InvalidArgument, "empty exponent rejects");
}

void TestArithmetic()
{
    using Fby::BigNum::Decimal128;
    using Fby::BigNum::Status;

    Decimal128 ScopedLeft;
    Decimal128 ScopedRight;
    Decimal128 ScopedResult;
    Check(Decimal128::FromString("1.5", &ScopedLeft) == Status::Ok, "1.5 parses");
    Check(Decimal128::FromString("2.5", &ScopedRight) == Status::Ok, "2.5 parses");
    Check(ScopedLeft.Add(ScopedRight, &ScopedResult) == Status::Ok, "1.5 + 2.5 status");
    CheckString(ScopedResult.ToString(), "4", "1.5 + 2.5 value");

    Check(Decimal128::FromString("0.1", &ScopedLeft) == Status::Ok, "0.1 parses");
    Check(Decimal128::FromString("0.2", &ScopedRight) == Status::Ok, "0.2 parses");
    Check(ScopedLeft.Add(ScopedRight, &ScopedResult) == Status::Ok, "0.1 + 0.2 status");
    CheckString(ScopedResult.ToString(), "0.3", "0.1 + 0.2 value");

    Check(ScopedLeft.Mul(ScopedRight, &ScopedResult) == Status::Ok, "0.1 * 0.2 status");
    CheckString(ScopedResult.ToString(), "0.02", "0.1 * 0.2 value");

    Check(Decimal128::FromString("10", &ScopedLeft) == Status::Ok, "10 parses");
    Check(Decimal128::FromString("2", &ScopedRight) == Status::Ok, "2 parses");
    Check(ScopedLeft.Div(ScopedRight, &ScopedResult) == Status::Ok, "10 / 2 status");
    CheckString(ScopedResult.ToString(), "5", "10 / 2 value");

    Check(Decimal128::FromString("1", &ScopedLeft) == Status::Ok, "1 parses");
    Check(Decimal128::FromString("8", &ScopedRight) == Status::Ok, "8 parses");
    Check(ScopedLeft.Div(ScopedRight, &ScopedResult) == Status::Ok, "1 / 8 status");
    CheckString(ScopedResult.ToString(), "0.125", "1 / 8 value");

    Check(Decimal128::FromString("1", &ScopedLeft) == Status::Ok, "1 parses for third");
    Check(Decimal128::FromString("3", &ScopedRight) == Status::Ok, "3 parses");
    Check(ScopedLeft.Div(ScopedRight, &ScopedResult) == Status::Inexact, "1 / 3 is inexact");
    CheckString(ScopedResult.ToString(), "0.333333", "1 / 3 precision");

    Check(Decimal128::FromString("2", &ScopedLeft) == Status::Ok, "2 parses for rounding");
    Check(ScopedLeft.Div(ScopedRight, &ScopedResult) == Status::Inexact, "2 / 3 is inexact");
    CheckString(ScopedResult.ToString(), "0.666667", "2 / 3 half-even rounding");

    Check(Decimal128::FromString("-2", &ScopedLeft) == Status::Ok, "negative 2 parses for rounding");
    Check(ScopedLeft.Div(ScopedRight, &ScopedResult, 6, Fby::BigNum::RoundingMode::Ceil) == Status::Inexact,
          "negative 2 / 3 ceil is inexact");
    CheckString(ScopedResult.ToString(), "-0.666666", "negative ceil truncates toward zero");

    Check(Decimal128::FromString("1e40", &ScopedLeft) == Status::Ok, "1e40 parses");
    Check(Decimal128::FromString("1", &ScopedRight) == Status::Ok, "1 parses for 1e40");
    Check(ScopedLeft.Add(ScopedRight, &ScopedResult) == Status::Inexact, "1e40 + 1 is inexact");
    CheckString(ScopedResult.ToString(), "10000000000000000000000000000000000000000",
                "1e40 + 1 keeps the large term");

    Check(ScopedLeft.Div(Decimal128::FromInt64(0), &ScopedResult) == Status::DivideByZero,
          "decimal division by zero rejects");
}

void TestCompareAndFormat()
{
    using Fby::BigNum::Decimal128;
    using Fby::BigNum::FormatSpec;
    using Fby::BigNum::Notation;
    using Fby::BigNum::Status;

    Decimal128 ScopedLeft;
    Decimal128 ScopedRight;
    Check(Decimal128::FromString("1.5", &ScopedLeft) == Status::Ok, "compare left parses");
    Check(Decimal128::FromString("1.50", &ScopedRight) == Status::Ok, "compare right parses");
    Check(ScopedLeft.Compare(ScopedRight) == 0, "1.5 equals 1.50");

    Check(Decimal128::FromString("-2", &ScopedLeft) == Status::Ok, "negative compare parses");
    Check(ScopedLeft.Compare(ScopedRight) < 0, "negative decimal compares lower");

    Check(Decimal128::FromString("1e40", &ScopedLeft) == Status::Ok, "format 1e40 parses");
    FormatSpec ScopedSpec;
    ScopedSpec.Style = Notation::Scientific;
    ScopedSpec.SignificantDigits = 6;
    std::array<char, 64> ScopedBuffer{};
    std::size_t ScopedSize = 0;
    Check(ScopedLeft.Format(ScopedSpec, std::span<char>(ScopedBuffer), &ScopedSize) == Status::Ok,
          "decimal scientific format status");
    CheckString(std::string(ScopedBuffer.data(), ScopedSize), "1E40", "decimal scientific format value");

    Check(Decimal128::FromString("-12345", &ScopedLeft) == Status::Ok, "negative format parses");
    ScopedSpec.SignificantDigits = 3;
    Check(ScopedLeft.Format(ScopedSpec, std::span<char>(ScopedBuffer), &ScopedSize) == Status::Ok,
          "negative decimal scientific status");
    CheckString(std::string(ScopedBuffer.data(), ScopedSize), "-1.23E4", "negative decimal scientific value");
}

void TestMixedOperators()
{
    using Fby::BigNum::Decimal128;
    using Fby::BigNum::Int128;
    using Fby::BigNum::Status;

    Decimal128 ScopedValue;
    Check(Decimal128::FromString("1.5", &ScopedValue) == Status::Ok, "mixed decimal parses");
    CheckString((ScopedValue + 2).ToString(), "3.5", "decimal plus int");
    CheckString((2 + ScopedValue).ToString(), "3.5", "decimal reverse plus int");
    CheckString((ScopedValue * 2).ToString(), "3", "decimal mul int");
    CheckString((-ScopedValue).ToString(), "-1.5", "decimal unary minus");
    Decimal128 ScopedComparable;
    Check(Decimal128::FromString("1.50", &ScopedComparable) == Status::Ok, "decimal comparable parses");
    Check(ScopedValue == ScopedComparable, "decimal comparison ignores trailing zeros");

    Int128 ScopedInteger = Int128::FromInt64(2);
    CheckString((ScopedValue + ScopedInteger).ToString(), "3.5", "decimal plus bigint");
    CheckString((ScopedInteger + ScopedValue).ToString(), "3.5", "bigint plus decimal");
}

void TestInt64Conversion()
{
    using Fby::BigNum::Decimal128;
    using Fby::BigNum::Status;

    const Decimal128 ScopedMinimum = Decimal128::FromInt64(INT64_MIN);
    std::int64_t ScopedValue = 0;
    Check(ScopedMinimum.ToInt64(&ScopedValue) == Status::Ok && ScopedValue == INT64_MIN,
          "decimal int64 minimum round-trip");
    CheckString(ScopedMinimum.ToString(), "-9223372036854775808", "decimal int64 minimum prints");

    const Decimal128 ScopedMaximum = Decimal128::FromInt64(INT64_MAX);
    Check(ScopedMaximum.ToInt64(&ScopedValue) == Status::Ok && ScopedValue == INT64_MAX,
          "decimal int64 maximum round-trip");
}

void TestSceneAndSnapshot()
{
    using Fby::BigNum::Decimal128;
    using Fby::BigNum::Status;

    Decimal128 ScopedValue;
    Check(Decimal128::FromString("-123.45", &ScopedValue) == Status::Ok, "scene decimal parses");

    Fby::Scene::ComponentRegistry ScopedComponents;
    Fby::Scene::RegisterBuiltInComponents(ScopedComponents);
    entt::registry ScopedRegistry;
    const Fby::Entity ScopedEntity = ScopedRegistry.create();
    ScopedRegistry.emplace<Fby::DecimalComponent>(ScopedEntity, Fby::DecimalComponent{ScopedValue});

    const std::string ScopedToml = Fby::Scene::SaveSceneToml(ScopedRegistry, ScopedComponents);
    Check(ScopedToml.find("-123.45") != std::string::npos, "scene TOML carries the decimal");

    entt::registry ScopedLoaded;
    const auto ScopedLoadedResult = Fby::Scene::LoadSceneToml(ScopedLoaded, ScopedComponents, ScopedToml);
    Check(ScopedLoadedResult.has_value(), "scene decimal loads");
    auto ScopedView = ScopedLoaded.view<Fby::DecimalComponent>();
    Check(!ScopedView.empty(), "loaded scene has DecimalComponent");
    for (const Fby::Entity ScopedLoadedEntity : ScopedView)
    {
        CheckString(ScopedView.get<Fby::DecimalComponent>(ScopedLoadedEntity).Value.ToString(), "-123.45",
                    "scene decimal round-trip");
    }

    const Fby::Ecs::SnapshotSchema ScopedSchema = Fby::Ecs::SnapshotSchema::BuiltIns();
    const std::size_t ScopedBytes = Fby::Ecs::SnapshotSize(ScopedRegistry, ScopedSchema);
    std::vector<std::byte> ScopedSnapshot(ScopedBytes);
    Fby::DeterministicRng ScopedRng = Fby::DeterministicRng::FromSeed(2);
    Check(Fby::Ecs::CaptureSnapshot(ScopedRegistry, ScopedSchema, ScopedSnapshot, 1, ScopedRng),
          "decimal snapshot captures");
    entt::registry ScopedRestored;
    Check(Fby::Ecs::RestoreSnapshot(ScopedRestored, ScopedSchema, ScopedSnapshot), "decimal snapshot restores");
    auto ScopedRestoredView = ScopedRestored.view<Fby::DecimalComponent>();
    Check(!ScopedRestoredView.empty(), "restored registry has DecimalComponent");
    for (const Fby::Entity ScopedRestoredEntity : ScopedRestoredView)
    {
        CheckString(ScopedRestoredView.get<Fby::DecimalComponent>(ScopedRestoredEntity).Value.ToString(), "-123.45",
                    "snapshot decimal round-trip");
    }
}

void TestDecimalStore()
{
    using Fby::BigNum::DecimalPool;
    using Fby::BigNum::Status;

    DecimalPool ScopedPool;
    const auto ScopedLeft = ScopedPool.CreateFromString("1.5");
    const auto ScopedRight = ScopedPool.CreateFromString("2.5");
    Check(ScopedPool.IsValid(ScopedLeft) && ScopedPool.IsValid(ScopedRight), "decimal store creates values");

    Fby::BigNum::Handle ScopedSum;
    Check(ScopedPool.Add(ScopedLeft, ScopedRight, &ScopedSum) == Status::Ok, "decimal store add");
    DecimalPool::Value ScopedValue;
    Check(ScopedPool.Get(ScopedSum, &ScopedValue), "decimal store add read");
    CheckString(ScopedValue.ToString(), "4", "decimal store add value");

    Fby::BigNum::Handle ScopedQuotient;
    Check(ScopedPool.Div(ScopedSum, ScopedLeft, &ScopedQuotient) == Status::Inexact,
          "decimal store reports inexact division");
    Check(ScopedPool.Get(ScopedQuotient, &ScopedValue), "decimal store div read");
    CheckString(ScopedValue.ToString(), "2.666667", "decimal store div value");

    const auto ScopedGeneration = ScopedLeft.Generation;
    Check(ScopedPool.Release(ScopedLeft), "decimal store release");
    const auto ScopedReused = ScopedPool.CreateFromString("9");
    Check(ScopedReused.IsValid() && ScopedReused.Generation != ScopedGeneration, "decimal store generation");
    Check(!ScopedPool.IsValid(ScopedLeft), "decimal store stale handle");
}
}

int main()
{
    TestParseAndPrint();
    TestArithmetic();
    TestCompareAndFormat();
    TestMixedOperators();
    TestInt64Conversion();
    TestSceneAndSnapshot();
    TestDecimalStore();

    if (GFailures != 0)
    {
        std::fprintf(stderr, "%d decimal test(s) failed\n", GFailures);
        return 1;
    }
    std::printf("decimal tests: ok\n");
    return 0;
}
