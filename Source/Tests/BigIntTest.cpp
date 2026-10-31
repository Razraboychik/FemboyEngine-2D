// FemboyEngine2D - fixed-width big integer acceptance test.
#include "Engine/Core/BigInt.hpp"
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

void TestSmallArithmetic()
{
    using Fby::BigNum::Int128;
    using Fby::BigNum::Status;

    for (std::int64_t ScopedLeft = -100; ScopedLeft <= 100; ++ScopedLeft)
    {
        for (std::int64_t ScopedRight = -100; ScopedRight <= 100; ++ScopedRight)
        {
            const Int128 ScopedA = Int128::FromInt64(ScopedLeft);
            const Int128 ScopedB = Int128::FromInt64(ScopedRight);
            Int128 ScopedResult;
            std::int64_t ScopedValue = 0;

            Check(ScopedA.Add(ScopedB, &ScopedResult) == Status::Ok, "small add status");
            Check(ScopedResult.ToInt64(&ScopedValue) == Status::Ok && ScopedValue == ScopedLeft + ScopedRight,
                  "small add value");

            Check(ScopedA.Sub(ScopedB, &ScopedResult) == Status::Ok, "small sub status");
            Check(ScopedResult.ToInt64(&ScopedValue) == Status::Ok && ScopedValue == ScopedLeft - ScopedRight,
                  "small sub value");

            Check(ScopedA.Mul(ScopedB, &ScopedResult) == Status::Ok, "small mul status");
            Check(ScopedResult.ToInt64(&ScopedValue) == Status::Ok && ScopedValue == ScopedLeft * ScopedRight,
                  "small mul value");

            if (ScopedRight != 0)
            {
                Int128 ScopedQuotient;
                Int128 ScopedRemainder;
                Check(ScopedA.DivMod(ScopedB, &ScopedQuotient, &ScopedRemainder) == Status::Ok,
                      "small div status");
                Check(ScopedQuotient.ToInt64(&ScopedValue) == Status::Ok && ScopedValue == ScopedLeft / ScopedRight,
                      "small div quotient");
                Check(ScopedRemainder.ToInt64(&ScopedValue) == Status::Ok && ScopedValue == ScopedLeft % ScopedRight,
                      "small div remainder");
            }
        }
    }

    const Int128 ScopedMinusEight = Int128::FromInt64(-8);
    Int128 ScopedShifted;
    Check(ScopedMinusEight.ShiftRight(1, &ScopedShifted) == Status::Ok, "arithmetic shift right status");
    std::int64_t ScopedValue = 0;
    Check(ScopedShifted.ToInt64(&ScopedValue) == Status::Ok && ScopedValue == -4, "arithmetic shift right value");
}

void TestMixedOperators()
{
    using Fby::BigNum::Int128;

    const Int128 ScopedFive = Int128::FromInt64(5);
    CheckString((ScopedFive + 7).ToString(), "12", "bigint operator plus int");
    CheckString((7 + ScopedFive).ToString(), "12", "bigint reverse plus int");
    CheckString((ScopedFive - 2).ToString(), "3", "bigint operator minus int");
    CheckString((10 - ScopedFive).ToString(), "5", "bigint reverse minus int");
    CheckString((ScopedFive * 3).ToString(), "15", "bigint operator mul int");
    CheckString((-ScopedFive).ToString(), "-5", "bigint unary minus");
    Check(ScopedFive == 5, "bigint equals int");
    Check(ScopedFive != 4, "bigint not-equals int");
    Check(ScopedFive < 6, "bigint less than int");
    Check(ScopedFive > 4, "bigint greater than int");
    Check(ScopedFive + Int128::FromInt64(3) == 8, "bigint operator plus bigint");
}

void TestInt64Boundaries()
{
    using Fby::BigNum::Int128;
    using Fby::BigNum::Status;

    std::int64_t ScopedValue = 0;
    Check(Int128::FromInt64(0).ToInt64(&ScopedValue) == Status::Ok && ScopedValue == 0, "int64 zero");
    Check(Int128::FromInt64(-1).ToInt64(&ScopedValue) == Status::Ok && ScopedValue == -1, "int64 minus one");
    Check(Int128::FromInt64(INT64_MIN).ToInt64(&ScopedValue) == Status::Ok && ScopedValue == INT64_MIN,
          "int64 minimum");
    Check(Int128::FromInt64(INT64_MAX).ToInt64(&ScopedValue) == Status::Ok && ScopedValue == INT64_MAX,
          "int64 maximum");

    Int128 ScopedTooLarge;
    Check(Int128::FromString("-170141183460469231731687303715884105727", &ScopedTooLarge) == Status::Ok,
          "negative beyond int64 parses");
    Check(ScopedTooLarge.ToInt64(&ScopedValue) == Status::Overflow, "negative beyond int64 rejects narrowing");
}

void TestInt128Boundaries()
{
    using Fby::BigNum::FormatSpec;
    using Fby::BigNum::Int128;
    using Fby::BigNum::Notation;
    using Fby::BigNum::Status;

    constexpr std::string_view kMaxText = "170141183460469231731687303715884105727";
    constexpr std::string_view kMinText = "-170141183460469231731687303715884105728";
    constexpr std::string_view kPositiveOverflow = "170141183460469231731687303715884105728";
    constexpr std::string_view kNegativeOverflow = "-170141183460469231731687303715884105729";

    Int128 ScopedMax;
    Int128 ScopedMin;
    Check(Int128::FromString(kMaxText, &ScopedMax) == Status::Ok, "int128 max parses");
    Check(Int128::FromString(kMinText, &ScopedMin) == Status::Ok, "int128 min parses");
    CheckString(ScopedMax.ToString(), kMaxText, "int128 max prints");
    CheckString(ScopedMin.ToString(), kMinText, "int128 min prints");

    Int128 ScopedOverflow;
    Check(Int128::FromString(kPositiveOverflow, &ScopedOverflow) == Status::Overflow, "positive overflow rejects");
    Check(Int128::FromString(kNegativeOverflow, &ScopedOverflow) == Status::Overflow, "negative overflow rejects");

    Int128 ScopedOne = Int128::FromInt64(1);
    Int128 ScopedResult;
    Check(ScopedMax.Add(ScopedOne, &ScopedResult) == Status::Overflow, "max plus one overflows");
    Check(ScopedMin.Sub(ScopedOne, &ScopedResult) == Status::Overflow, "min minus one overflows");
    Check(ScopedMin.DivMod(Int128::FromInt64(-1), &ScopedResult, &ScopedOverflow) == Status::Overflow,
          "minimum divided by minus one overflows");

    Int128 ScopedMinusTwo = Int128::FromInt64(-2);
    Int128 ScopedQuotient;
    Int128 ScopedRemainder;
    Check(ScopedMin.DivMod(ScopedMinusTwo, &ScopedQuotient, &ScopedRemainder) == Status::Ok,
          "minimum divided by minus two status");
    CheckString(ScopedQuotient.ToString(), "85070591730234615865843651857942052864",
                "minimum divided by minus two value");
    Check(ScopedRemainder.IsZero(), "minimum divided by minus two remainder");

    const Int128 ScopedHundred = Int128::FromInt64(100);
    const Int128 ScopedSeven = Int128::FromInt64(7);
    Check(ScopedHundred.DivMod(ScopedSeven, &ScopedQuotient, &ScopedRemainder) == Status::Ok, "positive divmod");
    CheckString(ScopedQuotient.ToString(), "14", "positive quotient");
    CheckString(ScopedRemainder.ToString(), "2", "positive remainder");

    const Int128 ScopedNegativeHundred = Int128::FromInt64(-100);
    Check(ScopedNegativeHundred.DivMod(ScopedSeven, &ScopedQuotient, &ScopedRemainder) == Status::Ok,
          "negative divmod");
    CheckString(ScopedQuotient.ToString(), "-14", "negative quotient");
    CheckString(ScopedRemainder.ToString(), "-2", "negative remainder");

    Check(ScopedHundred.DivMod(Int128::FromInt64(-7), &ScopedQuotient, &ScopedRemainder) == Status::Ok,
          "positive by negative divmod");
    CheckString(ScopedQuotient.ToString(), "-14", "positive by negative quotient");
    CheckString(ScopedRemainder.ToString(), "2", "positive by negative remainder");

    Check(ScopedNegativeHundred.DivMod(Int128::FromInt64(-7), &ScopedQuotient, &ScopedRemainder) == Status::Ok,
          "negative by negative divmod");
    CheckString(ScopedQuotient.ToString(), "14", "negative by negative quotient");
    CheckString(ScopedRemainder.ToString(), "-2", "negative by negative remainder");

    Check(ScopedHundred.DivMod(Int128::FromInt64(0), &ScopedQuotient, &ScopedRemainder) == Status::DivideByZero,
          "division by zero rejects");

    Int128 ScopedPowerOfTen;
    Check(Int128::FromString("1000000000000000000000000000000", &ScopedPowerOfTen) == Status::Ok,
          "10^30 parses");
    FormatSpec ScopedSpec;
    ScopedSpec.Style = Notation::Scientific;
    ScopedSpec.SignificantDigits = 6;
    std::array<char, 64> ScopedBuffer{};
    std::size_t ScopedSize = 0;
    Check(ScopedPowerOfTen.Format(ScopedSpec, std::span<char>(ScopedBuffer), &ScopedSize) == Status::Ok,
          "scientific format status");
    CheckString(std::string(ScopedBuffer.data(), ScopedSize), "1.00000E30", "scientific format value");

    const Int128 ScopedNegativeValue = Int128::FromInt64(-12345);
    ScopedSpec.SignificantDigits = 3;
    Check(ScopedNegativeValue.Format(ScopedSpec, std::span<char>(ScopedBuffer), &ScopedSize) == Status::Ok,
          "negative scientific format status");
    CheckString(std::string(ScopedBuffer.data(), ScopedSize), "-1.23E4", "negative scientific format value");

    ScopedSpec.Style = Notation::Plain;
    Check(ScopedNegativeValue.Format(ScopedSpec, std::span<char>(ScopedBuffer), &ScopedSize) == Status::Ok,
          "plain format status");
    CheckString(std::string(ScopedBuffer.data(), ScopedSize), "-12345", "plain format value");

    std::array<char, 4> ScopedTinyBuffer{};
    Check(ScopedNegativeValue.Format(ScopedSpec, std::span<char>(ScopedTinyBuffer), &ScopedSize) ==
              Status::BufferTooSmall,
          "small format buffer rejects");

    Int128 ScopedShifted;
    Check(Int128::FromInt64(1).ShiftLeft(100, &ScopedShifted) == Status::Ok, "shift left status");
    CheckString(ScopedShifted.ToString(), "1267650600228229401496703205376", "shift left value");
    Check(ScopedShifted.BitLength() == 101, "shift left bit length");
}

void TestInt256()
{
    using Fby::BigNum::Int256;
    using Fby::BigNum::Status;

    constexpr std::string_view kTwoTo200 = "1606938044258990275541962092341162602522202993782792835301376";
    Int256 ScopedValue;
    Check(Int256::FromString(kTwoTo200, &ScopedValue) == Status::Ok, "2^200 parses");
    CheckString(ScopedValue.ToString(), kTwoTo200, "2^200 prints");
    Check(ScopedValue.BitLength() == 201, "2^200 bit length");
}

void TestSceneRoundTrip()
{
    using Fby::BigNum::Int128;
    using Fby::BigNum::Status;

    Fby::Scene::ComponentRegistry ScopedComponents;
    Fby::Scene::RegisterBuiltInComponents(ScopedComponents);

    entt::registry ScopedRegistry;
    const Fby::Entity ScopedEntity = ScopedRegistry.create();
    Int128 ScopedValue;
    Check(Int128::FromString("-170141183460469231731687303715884105728", &ScopedValue) == Status::Ok,
          "scene value parses");
    ScopedRegistry.emplace<Fby::BigIntComponent>(ScopedEntity, Fby::BigIntComponent{ScopedValue});

    const std::string ScopedToml = Fby::Scene::SaveSceneToml(ScopedRegistry, ScopedComponents);
    Check(ScopedToml.find("-170141183460469231731687303715884105728") != std::string::npos,
          "scene TOML carries the full 128-bit value");
    Check(ScopedToml.find("BigIntComponent") != std::string::npos, "scene TOML names BigIntComponent");

    entt::registry ScopedLoaded;
    const auto ScopedResult = Fby::Scene::LoadSceneToml(ScopedLoaded, ScopedComponents, ScopedToml);
    Check(ScopedResult.has_value(), "scene TOML loads");
    if (!ScopedResult.has_value())
    {
        std::fprintf(stderr, "scene load error: %s\n", ScopedResult.error().c_str());
        return;
    }
    auto ScopedView = ScopedLoaded.view<Fby::BigIntComponent>();
    Check(!ScopedView.empty(), "loaded scene has BigIntComponent");
    for (const Fby::Entity ScopedLoadedEntity : ScopedView)
    {
        CheckString(ScopedView.get<Fby::BigIntComponent>(ScopedLoadedEntity).Value.ToString(),
                    "-170141183460469231731687303715884105728", "scene round-trip value");
    }
}

void TestSnapshotRoundTrip()
{
    using Fby::BigNum::Int128;
    using Fby::BigNum::Status;

    Int128 ScopedValue;
    Check(Int128::FromString("170141183460469231731687303715884105727", &ScopedValue) == Status::Ok,
          "snapshot value parses");

    entt::registry ScopedSource;
    const Fby::Entity ScopedEntity = ScopedSource.create();
    ScopedSource.emplace<Fby::BigIntComponent>(ScopedEntity, Fby::BigIntComponent{ScopedValue});

    const Fby::Ecs::SnapshotSchema ScopedSchema = Fby::Ecs::SnapshotSchema::BuiltIns();
    const std::size_t ScopedBytes = Fby::Ecs::SnapshotSize(ScopedSource, ScopedSchema);
    std::vector<std::byte> ScopedSnapshot(ScopedBytes);
    Fby::DeterministicRng ScopedRng = Fby::DeterministicRng::FromSeed(1);
    Check(Fby::Ecs::CaptureSnapshot(ScopedSource, ScopedSchema, ScopedSnapshot, 1, ScopedRng),
          "BigIntComponent snapshot captures");

    entt::registry ScopedTarget;
    Check(Fby::Ecs::RestoreSnapshot(ScopedTarget, ScopedSchema, ScopedSnapshot), "BigIntComponent snapshot restores");
    auto ScopedView = ScopedTarget.view<Fby::BigIntComponent>();
    Check(!ScopedView.empty(), "restored registry has BigIntComponent");
    for (const Fby::Entity ScopedTargetEntity : ScopedView)
    {
        CheckString(ScopedView.get<Fby::BigIntComponent>(ScopedTargetEntity).Value.ToString(),
                    "170141183460469231731687303715884105727", "snapshot round-trip value");
    }
}
}

int main()
{
    TestSmallArithmetic();
    TestMixedOperators();
    TestInt64Boundaries();
    TestInt128Boundaries();
    TestInt256();
    TestSceneRoundTrip();
    TestSnapshotRoundTrip();

    if (GFailures != 0)
    {
        std::fprintf(stderr, "%d big integer test(s) failed\n", GFailures);
        return 1;
    }
    std::printf("big integer tests: ok\n");
    return 0;
}
