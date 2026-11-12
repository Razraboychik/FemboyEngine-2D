// FemboyEngine2D - fixed-capacity heap for FemboyScript values.
#pragma once

#include <array>
#include <bit>
#include <cstdint>
#include <span>
#include <string_view>

namespace Fby::Script
{

// Tag of one 32-bit script register payload. Numbers keep their raw bits in the
// payload; strings, arrays and maps keep a heap handle.
enum class ValueTag : std::uint8_t
{
    Float = 0,
    Int = 1,
    Bool = 2,
    Entity = 3,
    String = 4,
    Array = 5,
    Map = 6,
};

struct ScriptValue
{
    std::uint32_t Bits = 0;
    ValueTag Tag = ValueTag::Float;

    [[nodiscard]] static constexpr ScriptValue FromFloat(float InValue) noexcept
    {
        ScriptValue ScopedValue;
        ScopedValue.Bits = std::bit_cast<std::uint32_t>(InValue);
        ScopedValue.Tag = ValueTag::Float;
        return ScopedValue;
    }

    [[nodiscard]] static constexpr ScriptValue FromInt(std::int32_t InValue) noexcept
    {
        ScriptValue ScopedValue;
        ScopedValue.Bits = static_cast<std::uint32_t>(InValue);
        ScopedValue.Tag = ValueTag::Int;
        return ScopedValue;
    }

    [[nodiscard]] static constexpr ScriptValue FromHandle(std::uint32_t InHandle, ValueTag InTag) noexcept
    {
        ScriptValue ScopedValue;
        ScopedValue.Bits = InHandle;
        ScopedValue.Tag = InTag;
        return ScopedValue;
    }
};

// A bounded, deterministic value heap shared by the VM and the AOT host. It is
// reset per script run: handles from a previous run are never reused, and no
// allocation happens after construction. A string, array or map that outgrows
// its budget makes the operation fail with a script fault instead of aborting.
class ScriptHeap
{
public:
    static constexpr std::uint32_t kMaxObjects = 4096;
    static constexpr std::uint32_t kMaxStringBytes = 256u * 1024u;
    static constexpr std::uint32_t kMaxElements = 65536;
    static constexpr std::uint32_t kMaxMapEntries = 16384;
    static constexpr std::uint32_t kInvalidHandle = 0xFFFFFFFFu;
    static constexpr std::uint32_t kArrayGrowthLimit = 256;
    static constexpr std::uint32_t kMapGrowthLimit = 64;

    void Reset() noexcept;

    [[nodiscard]] std::uint32_t InternString(std::string_view InText) noexcept;
    [[nodiscard]] bool StringView(std::uint32_t InHandle, std::string_view* OutText) const noexcept;
    [[nodiscard]] bool ConcatStrings(std::uint32_t InLeft, std::uint32_t InRight,
                                     std::uint32_t* OutHandle) noexcept;

    [[nodiscard]] std::uint32_t NewArray() noexcept;
    [[nodiscard]] bool ArrayPush(std::uint32_t InArray, ScriptValue InValue) noexcept;
    [[nodiscard]] bool ArrayGet(std::uint32_t InArray, std::uint32_t InIndex, ScriptValue* OutValue) const noexcept;
    [[nodiscard]] bool ArraySet(std::uint32_t InArray, std::uint32_t InIndex, ScriptValue InValue) noexcept;
    [[nodiscard]] bool ArrayLength(std::uint32_t InArray, std::uint32_t* OutLength) const noexcept;

    [[nodiscard]] std::uint32_t NewMap() noexcept;
    [[nodiscard]] bool MapSet(std::uint32_t InMap, ScriptValue InKey, ScriptValue InValue) noexcept;
    [[nodiscard]] bool MapGet(std::uint32_t InMap, ScriptValue InKey, ScriptValue* OutValue) const noexcept;
    [[nodiscard]] bool MapHas(std::uint32_t InMap, ScriptValue InKey) const noexcept;
    [[nodiscard]] bool MapLength(std::uint32_t InMap, std::uint32_t* OutLength) const noexcept;

    [[nodiscard]] std::uint32_t ObjectCount() const noexcept
    {
        return MObjectCount;
    }
    [[nodiscard]] std::uint32_t StringBytes() const noexcept
    {
        return MStringUsed;
    }
    [[nodiscard]] std::uint32_t ElementCount() const noexcept
    {
        return MElementUsed;
    }
    [[nodiscard]] std::uint32_t MapEntryCount() const noexcept
    {
        return MMapEntryUsed;
    }

private:
    enum class ObjectKind : std::uint8_t
    {
        String = 0,
        Array = 1,
        Map = 2,
    };

    struct Object
    {
        ObjectKind Kind = ObjectKind::String;
        // String: byte offset and byte length. Array and map: first pool slot
        // and capacity.
        std::uint32_t First = 0;
        std::uint32_t Length = 0;
        std::uint32_t Capacity = 0;
    };

    [[nodiscard]] const Object* Find(std::uint32_t InHandle, ObjectKind InKind) const noexcept;
    [[nodiscard]] Object* FindMutable(std::uint32_t InHandle, ObjectKind InKind) noexcept;
    [[nodiscard]] bool Equal(const ScriptValue& InLeft, const ScriptValue& InRight) const noexcept;
    [[nodiscard]] bool GrowArray(Object& InObject) noexcept;
    [[nodiscard]] bool GrowMap(Object& InObject) noexcept;

    std::array<Object, kMaxObjects> MObjects{};
    std::uint32_t MObjectCount = 0;
    std::array<char, kMaxStringBytes> MStringBytes{};
    std::uint32_t MStringUsed = 0;
    std::array<ScriptValue, kMaxElements> MElements{};
    std::uint32_t MElementUsed = 0;
    struct MapEntry
    {
        ScriptValue Key{};
        ScriptValue Value{};
    };
    std::array<MapEntry, kMaxMapEntries> MMapEntries{};
    std::uint32_t MMapEntryUsed = 0;
};
}
