// FemboyEngine2D - global definitions.
#pragma once

#include "Engine/Core/Mirror.hpp"
#include "Engine/Core/Types.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>
#include <utility>

// Inlining
#if defined(_MSC_VER)
#define FORCEINLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define FORCEINLINE inline __attribute__((always_inline))
#else
#define FORCEINLINE inline
#endif

// The non-aliasing promise the vectoriser needs on the bone SoA blend. It
#if defined(__clang__) || defined(__GNUC__) || defined(_MSC_VER)
#define FBY_RESTRICT __restrict
#else
#define FBY_RESTRICT
#endif

namespace Fby
{

// Stable type identity
using TypeId = std::uint64_t;

inline constexpr TypeId kInvalidTypeId = 0;

// Explicit cross-compiler name for a non-reflected type. A reflected component
template <typename T> struct TypeNameOverride
{
    static constexpr std::string_view Value{};
};

namespace TypeDetail
{

[[nodiscard]] constexpr TypeId Fnv1a(std::string_view InText) noexcept
{
    TypeId ScopedHash = 1469598103934665603ull;
    for (const char ScopedCharacter : InText)
    {
        ScopedHash ^= static_cast<TypeId>(static_cast<unsigned char>(ScopedCharacter));
        ScopedHash *= 1099511628211ull;
    }
    return ScopedHash;
}

template <typename T> [[nodiscard]] constexpr std::string_view CompilerTypeName() noexcept
{
#if defined(_MSC_VER)
    return __FUNCSIG__;
#else
    return __PRETTY_FUNCTION__;
#endif
}

template <typename T> [[nodiscard]] consteval std::string_view StableName() noexcept
{
    if constexpr (!TypeNameOverride<T>::Value.empty())
    {
        return TypeNameOverride<T>::Value;
    }
    else if constexpr (Reflect::Reflection<T>::kRegistered)
    {
        return Reflect::Reflection<T>::kName;
    }
    else
    {
        return CompilerTypeName<T>();
    }
}

// Flat parent table. Registration happens during static initialisation, before
struct TypeRecord
{
    TypeId Id = kInvalidTypeId;
    TypeId Base = kInvalidTypeId;
};

inline constexpr std::size_t kMaxTypeRecords = 512;

struct TypeTable
{
    std::array<TypeRecord, kMaxTypeRecords> Records{};
    std::size_t Count = 0;
};

[[nodiscard]] inline TypeTable& Types() noexcept
{
    static TypeTable ScopedTable;
    return ScopedTable;
}

[[nodiscard]] inline bool RegisterType(TypeId InId, TypeId InBase) noexcept
{
    if (InId == kInvalidTypeId || InBase == kInvalidTypeId)
    {
        return false;
    }

    TypeTable& ScopedTable = Types();
    for (std::size_t ScopedIndex = 0; ScopedIndex < ScopedTable.Count; ++ScopedIndex)
    {
        if (ScopedTable.Records[ScopedIndex].Id == InId)
        {
            return ScopedTable.Records[ScopedIndex].Base == InBase;
        }
    }
    if (ScopedTable.Count >= kMaxTypeRecords)
    {
        return false;
    }

    ScopedTable.Records[ScopedTable.Count++] = TypeRecord{InId, InBase};
    return true;
}

[[nodiscard]] inline bool IsDerivedFrom(TypeId InRuntime, TypeId InTarget) noexcept
{
    if (InRuntime == InTarget)
    {
        return true;
    }

    const TypeTable& ScopedTable = Types();
    for (std::size_t ScopedDepth = 0; ScopedDepth < 32; ++ScopedDepth)
    {
        TypeId ScopedParent = kInvalidTypeId;
        bool ScopedFound = false;
        for (std::size_t ScopedIndex = 0; ScopedIndex < ScopedTable.Count; ++ScopedIndex)
        {
            if (ScopedTable.Records[ScopedIndex].Id == InRuntime)
            {
                ScopedParent = ScopedTable.Records[ScopedIndex].Base;
                ScopedFound = true;
                break;
            }
        }
        if (!ScopedFound || ScopedParent == kInvalidTypeId)
        {
            return false;
        }
        if (ScopedParent == InTarget)
        {
            return true;
        }
        InRuntime = ScopedParent;
    }
    return false;
}
}

// A reflected component's id is the FNV-1a hash of its Mirror name. Every other
template <typename T> [[nodiscard]] consteval TypeId StableTypeId() noexcept
{
    return TypeDetail::Fnv1a(TypeDetail::StableName<T>());
}

template <typename T> struct TypeTraits
{
    static constexpr TypeId Id = StableTypeId<T>();
    static constexpr TypeId Base = kInvalidTypeId;
};

// Casts

// Static cast with a single, deliberate spelling. Use this when the concrete
template <typename To, typename From> [[nodiscard]] FORCEINLINE To FemCast(From&& InValue) noexcept
{
    return static_cast<To>(std::forward<From>(InValue));
}

// Base for every polymorphic engine object that participates in Cast<>.
class TypeObject
{
public:
    virtual ~TypeObject() = default;

    [[nodiscard]] virtual TypeId GetTypeId() const noexcept = 0;
};

// Checked pointer cast.
template <typename To, typename From> [[nodiscard]] FORCEINLINE To* Cast(From* InObject) noexcept
{
    if (InObject == nullptr)
    {
        return nullptr;
    }

    if constexpr (std::is_base_of_v<To, From>)
    {
        return static_cast<To*>(InObject);
    }
    else if constexpr (std::is_base_of_v<From, To>)
    {
        static_assert(
            requires(const From& InValue) { InValue.GetTypeId(); },
            "Cast<> downcast requires GetTypeId() on the source type");
        const TypeId ScopedRuntime = InObject->GetTypeId();
        if (ScopedRuntime == StableTypeId<To>() || TypeDetail::IsDerivedFrom(ScopedRuntime, StableTypeId<To>()))
        {
            return static_cast<To*>(InObject);
        }
        return nullptr;
    }
    else
    {
        static_assert(std::is_base_of_v<From, To> || std::is_base_of_v<To, From>,
                      "Cast<> requires related source and target types");
        return nullptr;
    }
}

template <typename To, typename From> [[nodiscard]] FORCEINLINE const To* Cast(const From* InObject) noexcept
{
    return Cast<To>(const_cast<From*>(InObject));
}

// Checked reference cast for a call site that has already proven the type.
template <typename To, typename From> [[nodiscard]] FORCEINLINE To& Cast(From& InObject) noexcept
{
    To* ScopedResult = Cast<To>(&InObject);
    assert(ScopedResult != nullptr);
    return *ScopedResult;
}

template <typename To, typename From> [[nodiscard]] FORCEINLINE const To& Cast(const From& InObject) noexcept
{
    const To* ScopedResult = Cast<To>(&InObject);
    assert(ScopedResult != nullptr);
    return *ScopedResult;
}

// Type-safe component lookup. This is the ECS spelling of a cast: EnTT proves
template <typename To, typename Registry, typename Entity>
[[nodiscard]] FORCEINLINE To* CastComponent(Registry& InRegistry, Entity InEntity) noexcept
{
    return InRegistry.template try_get<To>(InEntity);
}

template <typename To, typename Registry, typename Entity>
[[nodiscard]] FORCEINLINE const To* CastComponent(const Registry& InRegistry, Entity InEntity) noexcept
{
    return InRegistry.template try_get<To>(InEntity);
}
}

// Registration macros
#define FBY_DETAIL_CONCAT_INNER(InLeft, InRight) InLeft##InRight
#define FBY_DETAIL_CONCAT(InLeft, InRight) FBY_DETAIL_CONCAT_INNER(InLeft, InRight)

// Binds a concrete class to its parent for Cast<>. The class must be named
#define FBY_REGISTER_TYPE(Type, InBase)                                                                                \
    namespace Fby                                                                                                      \
    {                                                                                                                  \
    template <> struct TypeTraits<::Type>                                                                              \
    {                                                                                                                  \
        static constexpr TypeId Id = StableTypeId<::Type>();                                                           \
        static constexpr TypeId Base = StableTypeId<::InBase>();                                                       \
    };                                                                                                                 \
    namespace TypeDetail                                                                                               \
    {                                                                                                                  \
    inline const bool FBY_DETAIL_CONCAT(MKTypeRegistered, __COUNTER__) = RegisterType(TypeTraits<::Type>::Id,          \
                                                                                      TypeTraits<::Type>::Base);       \
    }                                                                                                                  \
    }

// Pins a cross-compiler stable name for a class that Mirror does not reflect.
#define FBY_TYPE_NAME(Type, Name)                                                                                      \
    namespace Fby                                                                                                      \
    {                                                                                                                  \
    template <> struct TypeNameOverride<::Type>                                                                        \
    {                                                                                                                  \
        static constexpr std::string_view Value = Name;                                                                \
    };                                                                                                                 \
    }
