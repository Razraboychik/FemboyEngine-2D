// FemboyEngine2D - fixed-capacity big number store acceptance test.
#include "Engine/Core/BigNumberStore.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
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

void CheckString(const std::string& InActual, std::string_view InExpected, const char* InMessage)
{
    if (InActual != InExpected)
    {
        std::fprintf(stderr, "FAIL: %s (actual '%s', expected '%.*s')\n", InMessage, InActual.c_str(),
                     static_cast<int>(InExpected.size()), InExpected.data());
        ++GFailures;
    }
}

void TestStoreArithmetic()
{
    using Fby::BigNum::BigNumberStore;
    using Fby::BigNum::FormatSpec;
    using Fby::BigNum::Notation;
    using Fby::BigNum::Status;

    using Store = BigNumberStore<1, 8>;
    using Value = Store::Value;
    Store ScopedStore;
    const auto ScopedFive = ScopedStore.Create(Value::FromInt64(5));
    const auto ScopedSeven = ScopedStore.Create(Value::FromInt64(7));
    Check(ScopedStore.IsValid(ScopedFive) && ScopedStore.IsValid(ScopedSeven), "created handles are valid");
    Check(ScopedStore.Used() == 2, "store used count");

    Fby::BigNum::Handle ScopedSum;
    Check(ScopedStore.Add(ScopedFive, ScopedSeven, &ScopedSum) == Status::Ok, "store add status");
    Check(ScopedStore.IsValid(ScopedSum), "store add result handle");

    Value ScopedValue;
    Check(ScopedStore.Get(ScopedSum, &ScopedValue), "store add result read");
    CheckString(ScopedValue.ToString(), "12", "store add result value");

    Fby::BigNum::Handle ScopedProduct;
    Check(ScopedStore.Mul(ScopedFive, ScopedSeven, &ScopedProduct) == Status::Ok, "store mul status");
    Check(ScopedStore.Get(ScopedProduct, &ScopedValue), "store mul result read");
    CheckString(ScopedValue.ToString(), "35", "store mul result value");

    int ScopedComparison = 0;
    Check(ScopedStore.Compare(ScopedFive, ScopedSeven, &ScopedComparison) == Status::Ok && ScopedComparison < 0,
          "store compare");

    Fby::BigNum::Handle ScopedQuotient;
    Fby::BigNum::Handle ScopedRemainder;
    Check(ScopedStore.DivMod(ScopedProduct, ScopedSeven, &ScopedQuotient, &ScopedRemainder) == Status::Ok,
          "store divmod status");
    Check(ScopedStore.Get(ScopedQuotient, &ScopedValue), "store quotient read");
    CheckString(ScopedValue.ToString(), "5", "store quotient value");
    Check(ScopedStore.Get(ScopedRemainder, &ScopedValue), "store remainder read");
    CheckString(ScopedValue.ToString(), "0", "store remainder value");

    const auto ScopedLarge = ScopedStore.CreateFromString("1000000000000000000000000000000");
    Check(ScopedStore.IsValid(ScopedLarge), "store parses large value");
    FormatSpec ScopedSpec;
    ScopedSpec.Style = Notation::Scientific;
    ScopedSpec.SignificantDigits = 6;
    std::array<char, 64> ScopedBuffer{};
    std::size_t ScopedSize = 0;
    Check(ScopedStore.Format(ScopedLarge, ScopedSpec, std::span<char>(ScopedBuffer), &ScopedSize) == Status::Ok,
          "store format status");
    CheckString(std::string(ScopedBuffer.data(), ScopedSize), "1.00000E30", "store scientific format");
}

void TestGenerationAndCapacity()
{
    using Fby::BigNum::BigNumberStore;
    using Fby::BigNum::Status;

    using Store = BigNumberStore<1, 2>;
    using Value = Store::Value;
    Store ScopedStore;
    const auto ScopedFirst = ScopedStore.Create(Value::FromInt64(1));
    const auto ScopedSecond = ScopedStore.Create(Value::FromInt64(2));
    Check(ScopedStore.IsValid(ScopedFirst) && ScopedStore.IsValid(ScopedSecond), "two handles fit");

    Check(!ScopedStore.Create(Value::FromInt64(3)).IsValid(), "full store rejects a new value");

    const auto ScopedFirstGeneration = ScopedFirst.Generation;
    Check(ScopedStore.Release(ScopedFirst), "release first");
    Check(!ScopedStore.IsValid(ScopedFirst), "released handle is invalid");

    const auto ScopedThird = ScopedStore.Create(Value::FromInt64(3));
    Check(ScopedThird.IsValid(), "released slot is reusable");
    Check(ScopedThird.Generation != ScopedFirstGeneration, "reused slot changes generation");
    Check(!ScopedStore.IsValid(ScopedFirst), "old generation stays invalid");

    Value ScopedValue;
    Check(!ScopedStore.Get(ScopedFirst, &ScopedValue), "stale handle cannot be read");
    Check(ScopedStore.Get(ScopedThird, &ScopedValue), "new handle can be read");
    CheckString(ScopedValue.ToString(), "3", "new slot value");

    ScopedStore.Clear();
    Check(!ScopedStore.IsValid(ScopedSecond), "clear invalidates the remaining handle");
    Check(ScopedStore.Used() == 0, "clear resets the used count");
}

void TestOverflowPropagation()
{
    using Fby::BigNum::BigNumberStore;
    using Fby::BigNum::Status;

    using Store = BigNumberStore<1, 4>;
    using Value = Store::Value;
    Store ScopedStore;
    const auto ScopedMax = ScopedStore.CreateFromString("170141183460469231731687303715884105727");
    const auto ScopedOne = ScopedStore.Create(Value::FromInt64(1));
    Fby::BigNum::Handle ScopedResult;
    Check(ScopedStore.Add(ScopedMax, ScopedOne, &ScopedResult) == Status::Overflow, "store propagates overflow");
    Check(!ScopedStore.IsValid(ScopedResult), "failed operation does not create a result");
}
}

int main()
{
    TestStoreArithmetic();
    TestGenerationAndCapacity();
    TestOverflowPropagation();

    if (GFailures != 0)
    {
        std::fprintf(stderr, "%d big number store test(s) failed\n", GFailures);
        return 1;
    }
    std::printf("big number store tests: ok\n");
    return 0;
}
