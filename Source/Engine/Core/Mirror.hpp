// FemboyEngine2D - compile-time component reflection ("Mirror").
#pragma once

#include "Engine/Core/Types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <type_traits>

namespace Fby::Reflect
{

enum class FieldKind : std::uint8_t
{
    Float = 0,
    Int32,
    UInt32,
    Bool,
    Vector2D,
    Color32,
    // A 32-bit entity/handle id. Serialized as an index into the scene's entity
    Entity,
    // A fixed-capacity, NUL-terminated UTF-8 string member (char[N] or
    String,
    // 64-bit unsigned integer, used for stable AssetId. TOML encodes it as a
    UInt64,
    // Fixed 128-bit signed integer, stored as two's complement.
    Int128,
    // Fixed-width signed decimal with coefficient and base-10 exponent.
    Decimal,
};

struct FieldDesc
{
    // Member name, exactly as written
    std::string_view name;
    FieldKind kind = FieldKind::Float;
    // Byte offset inside the component
    std::uint32_t offset = 0;
    // Sizeof(member)
    std::uint32_t size = 0;
};

struct ComponentDesc
{
    // "Transform2D"
    std::string_view name;
    std::span<const FieldDesc> fields;
    // Sizeof(component)
    std::uint32_t size = 0;
};

namespace Detail
{
template <typename T> struct Unregistered
{
    static constexpr bool kRegistered = false;
};
}

// Primary template: a component is unregistered until FBY_REFLECT_COMPONENT
template <typename T> struct Reflection : Detail::Unregistered<T>
{
};

// Compile-time description. Fails the build for an unregistered or non
template <typename T> [[nodiscard]] consteval ComponentDesc Describe()
{
    static_assert(std::is_standard_layout_v<T>, "Mirror: a component must be standard layout");
    static_assert(Reflection<T>::kRegistered, "Mirror: component is not registered - add FBY_REFLECT_COMPONENT");
    return ComponentDesc{Reflection<T>::kName, Reflection<T>::kFields, static_cast<std::uint32_t>(sizeof(T))};
}

template <typename T> [[nodiscard]] constexpr bool IsRegistered() noexcept
{
    return Reflection<T>::kRegistered;
}

template <typename T> [[nodiscard]] constexpr std::span<const FieldDesc> Fields() noexcept
{
    if constexpr (Reflection<T>::kRegistered)
    {
        return Reflection<T>::kFields;
    }
    else
    {
        return {};
    }
}

// Static reflection detection
#if defined(__cpp_lib_reflection) && __cpp_lib_reflection >= 202406L
#define FBY_HAS_STATIC_REFLECTION 1
#elif defined(__clang__) && defined(__has_feature)
#if __has_feature(reflection)
#define FBY_HAS_STATIC_REFLECTION 1
#endif
#endif
#if !defined(FBY_HAS_STATIC_REFLECTION)
#define FBY_HAS_STATIC_REFLECTION 0
#endif

// 64-bit FNV-1a. Used for the plugin ABI's layout hash: two builds of the same
[[nodiscard]] constexpr std::uint64_t Fnv1a(std::string_view InText,
                                            std::uint64_t InHash = 1469598103934665603ull) noexcept
{
    for (const char ScopedCharacter : InText)
    {
        InHash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(ScopedCharacter));
        InHash *= 1099511628211ull;
    }
    return InHash;
}

[[nodiscard]] constexpr std::uint64_t MixHash(std::uint64_t InHash, std::uint64_t InValue) noexcept
{
    InHash ^= InValue + 0x9E3779B97F4A7C15ull + (InHash << 6) + (InHash >> 2);
    return InHash;
}

// Stable identity of a component's memory layout: name, field names, kinds,
template <typename T> [[nodiscard]] constexpr std::uint64_t LayoutHash() noexcept
{
    static_assert(Reflection<T>::kRegistered, "Mirror: component is not registered");
    std::uint64_t ScopedHash = Fnv1a(Reflection<T>::kName);
    ScopedHash = MixHash(ScopedHash, sizeof(T));
    for (const FieldDesc& ScopedField : Reflection<T>::kFields)
    {
        ScopedHash = Fnv1a(ScopedField.name, ScopedHash);
        ScopedHash = MixHash(ScopedHash, static_cast<std::uint64_t>(ScopedField.kind));
        ScopedHash = MixHash(ScopedHash, ScopedField.offset);
        ScopedHash = MixHash(ScopedHash, ScopedField.size);
    }
    return ScopedHash;
}

// Field-wise comparison
namespace Detail
{
[[nodiscard]] inline std::uint32_t LoadFieldU32(const std::byte* InBytes) noexcept
{
    std::uint32_t ScopedValue = 0;
    std::memcpy(&ScopedValue, InBytes, sizeof(ScopedValue));
    return ScopedValue;
}

[[nodiscard]] inline std::uint64_t LoadFieldU64(const std::byte* InBytes) noexcept
{
    std::uint64_t ScopedValue = 0;
    std::memcpy(&ScopedValue, InBytes, sizeof(ScopedValue));
    return ScopedValue;
}

// Fixed-capacity NUL-terminated string field: compares the logical content up
[[nodiscard]] inline bool StringFieldEqual(const std::byte* InA, const std::byte* InB,
                                           std::uint32_t InSize) noexcept
{
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < InSize; ++ScopedIndex)
    {
        const std::byte ScopedA = InA[ScopedIndex];
        const std::byte ScopedB = InB[ScopedIndex];
        if (ScopedA == ScopedB)
        {
            if (ScopedA == std::byte{0})
            {
                return true;
            }
            continue;
        }
        if (ScopedA == std::byte{0} || ScopedB == std::byte{0})
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] inline bool ByteRangeEqual(const std::byte* InA, const std::byte* InB, std::uint32_t InSize) noexcept
{
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < InSize; ++ScopedIndex)
    {
        if (InA[ScopedIndex] != InB[ScopedIndex])
        {
            return false;
        }
    }
    return true;
}
}

// True when the field's declared range lies inside a component of InComponentSize.
[[nodiscard]] inline bool FieldFits(const FieldDesc& InField, std::uint32_t InComponentSize) noexcept
{
    return InField.offset <= InComponentSize && InField.size <= InComponentSize - InField.offset;
}

// Compares one reflected field of two component images. Never reads padding and
[[nodiscard]] inline bool FieldBytesEqual(const FieldDesc& InField, const void* InA, const void* InB) noexcept
{
    if (InA == nullptr || InB == nullptr || InField.size == 0)
    {
        return true;
    }
    const auto* ScopedA = static_cast<const std::byte*>(InA);
    const auto* ScopedB = static_cast<const std::byte*>(InB);
    switch (InField.kind)
    {
        case FieldKind::Float:
            if (InField.size == sizeof(float))
            {
                return Detail::LoadFieldU32(ScopedA) == Detail::LoadFieldU32(ScopedB);
            }
            break;
        case FieldKind::Int32:
        case FieldKind::UInt32:
        case FieldKind::Entity:
        case FieldKind::Color32:
            if (InField.size == sizeof(std::uint32_t))
            {
                return Detail::LoadFieldU32(ScopedA) == Detail::LoadFieldU32(ScopedB);
            }
            break;
        case FieldKind::Bool:
            if (InField.size == sizeof(bool))
            {
                return ScopedA[0] == ScopedB[0];
            }
            break;
        case FieldKind::Vector2D:
            if (InField.size == sizeof(Vector2D))
            {
                // Two floats, compared by their bit patterns so a NaN field is
                return Detail::LoadFieldU64(ScopedA) == Detail::LoadFieldU64(ScopedB);
            }
            break;
        case FieldKind::UInt64:
            if (InField.size == sizeof(std::uint64_t))
            {
                return Detail::LoadFieldU64(ScopedA) == Detail::LoadFieldU64(ScopedB);
            }
            break;
        case FieldKind::Int128:
        case FieldKind::Decimal:
            return Detail::ByteRangeEqual(ScopedA, ScopedB, InField.size);
        case FieldKind::String:
            return Detail::StringFieldEqual(ScopedA, ScopedB, InField.size);
    }
    // Unknown kind / unexpected size: stay inside the field range.
    return Detail::ByteRangeEqual(ScopedA, ScopedB, InField.size);
}

// Compares two full component images field by field. Padding bytes and any
[[nodiscard]] inline bool ComponentFieldsEqual(std::span<const FieldDesc> InFields, std::span<const std::byte> InA,
                                               std::span<const std::byte> InB) noexcept
{
    for (const FieldDesc& ScopedField : InFields)
    {
        if (!FieldFits(ScopedField, static_cast<std::uint32_t>(InA.size())) ||
            !FieldFits(ScopedField, static_cast<std::uint32_t>(InB.size())))
        {
            return false;
        }
        if (!FieldBytesEqual(ScopedField, InA.data() + ScopedField.offset, InB.data() + ScopedField.offset))
        {
            return false;
        }
    }
    return true;
}

// P2996 blueprint
}

// Registration macros
#define FBY_FIELD(owner, member, kind)                                                                                 \
    ::Fby::Reflect::FieldDesc                                                                                          \
    {                                                                                                                  \
        #member, ::Fby::Reflect::FieldKind::kind, static_cast<std::uint32_t>(offsetof(owner, member)),                 \
            static_cast<std::uint32_t>(sizeof(owner::member))                                                          \
    }

#define FBY_REFLECT_COMPONENT(type, displayName, ...)                                                                  \
    namespace Fby::Reflect                                                                                             \
    {                                                                                                                  \
    template <> struct Reflection<::type>                                                                              \
    {                                                                                                                  \
        static constexpr bool kRegistered = true;                                                                      \
        static constexpr std::string_view kName = displayName;                                                         \
        static constexpr auto kFields = std::array{__VA_ARGS__};                                                       \
    };                                                                                                                 \
    }

// A marker component (`struct Hidden {};`) has no fields. std::array{...} with
#define FBY_REFLECT_COMPONENT_EMPTY(type, displayName)                                                                 \
    namespace Fby::Reflect                                                                                             \
    {                                                                                                                  \
    template <> struct Reflection<::type>                                                                              \
    {                                                                                                                  \
        static constexpr bool kRegistered = true;                                                                      \
        static constexpr std::string_view kName = displayName;                                                         \
        static constexpr std::array<FieldDesc, 0> kFields{};                                                           \
    };                                                                                                                 \
    }
