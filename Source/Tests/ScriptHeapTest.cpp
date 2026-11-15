// FemboyEngine2D - fixed-capacity script heap tests.
#include "Engine/Script/ScriptHeap.hpp"

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
}

int main()
{
    using namespace Fby::Script;

    ScriptHeap ScopedHeap;

    const std::uint32_t ScopedHello = ScopedHeap.InternString("hello");
    const std::uint32_t ScopedWorld = ScopedHeap.InternString("world");
    Check(ScopedHello != ScriptHeap::kInvalidHandle, "a string is interned");
    std::string_view ScopedView;
    Check(ScopedHeap.StringView(ScopedHello, &ScopedView) && ScopedView == "hello", "the string reads back");

    std::uint32_t ScopedJoined = ScriptHeap::kInvalidHandle;
    Check(ScopedHeap.ConcatStrings(ScopedHello, ScopedWorld, &ScopedJoined), "strings concatenate");
    Check(ScopedHeap.StringView(ScopedJoined, &ScopedView) && ScopedView == "helloworld",
          "the concatenation is correct");
    Check(!ScopedHeap.StringView(ScriptHeap::kInvalidHandle, &ScopedView), "an invalid string handle is rejected");

    const std::uint32_t ScopedArray = ScopedHeap.NewArray();
    Check(ScopedArray != ScriptHeap::kInvalidHandle, "an array is created");
    for (std::uint32_t ScopedI = 0; ScopedI < 10; ++ScopedI)
    {
        Check(ScopedHeap.ArrayPush(ScopedArray, ScriptValue::FromInt(static_cast<std::int32_t>(ScopedI * 10))),
              "the array grows through its initial capacity");
    }
    std::uint32_t ScopedLength = 0;
    Check(ScopedHeap.ArrayLength(ScopedArray, &ScopedLength) && ScopedLength == 10, "the array length is ten");
    ScriptValue ScopedValue;
    Check(ScopedHeap.ArrayGet(ScopedArray, 7, &ScopedValue) && ScopedValue.Bits == 70 && ScopedValue.Tag == ValueTag::Int,
          "an array element reads back");
    Check(ScopedHeap.ArraySet(ScopedArray, 7, ScriptValue::FromInt(99)), "an array element is replaced");
    Check(ScopedHeap.ArrayGet(ScopedArray, 7, &ScopedValue) && ScopedValue.Bits == 99, "the replacement is visible");
    Check(!ScopedHeap.ArrayGet(ScopedArray, 10, &ScopedValue), "an out-of-range index is rejected");
    Check(!ScopedHeap.ArrayPush(ScopedHello, ScriptValue::FromInt(1)), "a string is not an array");

    const std::uint32_t ScopedMap = ScopedHeap.NewMap();
    Check(ScopedMap != ScriptHeap::kInvalidHandle, "a map is created");
    Check(ScopedHeap.MapSet(ScopedMap, ScriptValue::FromHandle(ScopedHello, ValueTag::String),
                            ScriptValue::FromInt(41)),
          "a string key is stored");
    Check(ScopedHeap.MapSet(ScopedMap, ScriptValue::FromInt(7), ScriptValue::FromFloat(1.5f)),
          "an integer key is stored");
    Check(ScopedHeap.MapSet(ScopedMap, ScriptValue::FromHandle(ScopedHello, ValueTag::String),
                            ScriptValue::FromInt(42)),
          "an existing key updates in place");
    Check(ScopedHeap.MapLength(ScopedMap, &ScopedLength) && ScopedLength == 2, "the map keeps two keys");
    Check(ScopedHeap.MapGet(ScopedMap, ScriptValue::FromHandle(ScopedHello, ValueTag::String), &ScopedValue) &&
              ScopedValue.Bits == 42 && ScopedValue.Tag == ValueTag::Int,
          "the updated string value reads back");
    Check(ScopedHeap.MapHas(ScopedMap, ScriptValue::FromInt(7)), "the integer key is present");
    Check(!ScopedHeap.MapHas(ScopedMap, ScriptValue::FromInt(8)), "a missing key is absent");
    Check(!ScopedHeap.MapGet(ScopedMap, ScriptValue::FromInt(8), &ScopedValue), "a missing key reads as false");

    Check(ScopedHeap.ObjectCount() == 5, "the object count matches the live objects");
    Check(ScopedHeap.StringBytes() == std::string_view("hello").size() + std::string_view("world").size() +
                                           std::string_view("helloworld").size(),
          "the string arena reports the used bytes");

    ScopedHeap.Reset();
    Check(ScopedHeap.ObjectCount() == 0 && ScopedHeap.StringBytes() == 0 && ScopedHeap.ElementCount() == 0,
          "reset clears every pool");
    Check(!ScopedHeap.StringView(ScopedHello, &ScopedView), "a handle from before the reset is invalid");

    if (GFailures == 0)
    {
        std::printf("script_heap: OK\n");
        return 0;
    }
    std::fprintf(stderr, "script_heap: %d failure(s)\n", GFailures);
    return 1;
}
