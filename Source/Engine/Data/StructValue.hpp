// FemboyEngine2D - bounded type-erased authoring structs.
//
// This is an FInstancedStruct-style payload layer, not a replacement for ECS
// components or for GameplayTags/GameplayEffects/GameplaySkills. A value is
// deliberately trivially copyable and inline-only so it can live in a
// component, a snapshot or a command log without owning allocations.
#pragma once

#include "Engine/Core/Mirror.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

namespace Fby::Data
{
inline constexpr std::uint32_t kStructValueBytes = 64;
inline constexpr std::uint32_t kStructPayloadMaxValues = 4;
inline constexpr std::uint32_t kStructRegistryCapacity = 256;
inline constexpr std::uint32_t kInvalidStructType = 0;

using StructTypeId = std::uint32_t;

// FNV-1a over the stable type name. Never use typeid(), a pointer or an address.
[[nodiscard]] constexpr std::uint32_t MakeStructTypeId(std::string_view InName) noexcept
{
    std::uint32_t ScopedHash = 2166136261u;
    for (const char ScopedChar : InName)
    {
        ScopedHash ^= static_cast<std::uint32_t>(static_cast<unsigned char>(ScopedChar));
        ScopedHash *= 16777619u;
    }
    return ScopedHash == 0 ? 1u : ScopedHash;
}

struct StructField
{
    std::string_view Name;
    Reflect::FieldKind Kind = Reflect::FieldKind::UInt32;
    std::uint32_t Offset = 0;
    std::uint32_t Size = 0;
};

[[nodiscard]] constexpr std::uint64_t MakeStructSchemaHash(std::span<const StructField> InFields) noexcept
{
    std::uint64_t ScopedHash = 1469598103934665603ull;
    for (const StructField& ScopedField : InFields)
    {
        for (const char ScopedChar : ScopedField.Name)
        {
            ScopedHash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(ScopedChar));
            ScopedHash *= 1099511628211ull;
        }
        ScopedHash ^= static_cast<std::uint64_t>(ScopedField.Kind);
        ScopedHash *= 1099511628211ull;
        ScopedHash ^= ScopedField.Offset;
        ScopedHash *= 1099511628211ull;
        ScopedHash ^= ScopedField.Size;
        ScopedHash *= 1099511628211ull;
    }
    return ScopedHash;
}

struct StructType
{
    StructTypeId Id = kInvalidStructType;
    std::string_view Name;
    std::uint32_t Size = 0;
    std::uint32_t Alignment = 1;
    std::uint64_t SchemaHash = 0;
    std::span<const StructField> Fields;
    bool BuiltIn = false;
};

class StructTypeRegistry
{
public:
    [[nodiscard]] bool Register(const StructType& InType) noexcept
    {
        if (InType.Id == kInvalidStructType || InType.Name.empty() || InType.Size == 0 ||
            InType.Size > kStructValueBytes || InType.Alignment == 0 || InType.Alignment > alignof(std::max_align_t) ||
            InType.Fields.empty() || MCount >= kStructRegistryCapacity)
        {
            return false;
        }
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < MCount; ++ScopedIndex)
        {
            if (MTypes[ScopedIndex].Id == InType.Id || MTypes[ScopedIndex].Name == InType.Name)
            {
                return false;
            }
        }
        for (const StructField& ScopedField : InType.Fields)
        {
            if (ScopedField.Name.empty() || ScopedField.Size == 0 || ScopedField.Offset > InType.Size ||
                ScopedField.Size > InType.Size - ScopedField.Offset)
            {
                return false;
            }
            for (const StructField& ScopedOther : InType.Fields)
            {
                if (&ScopedField == &ScopedOther)
                {
                    continue;
                }
                const std::uint64_t ScopedEnd = static_cast<std::uint64_t>(ScopedField.Offset) + ScopedField.Size;
                const std::uint64_t ScopedOtherEnd = static_cast<std::uint64_t>(ScopedOther.Offset) + ScopedOther.Size;
                if (ScopedField.Offset < ScopedOtherEnd && ScopedOther.Offset < ScopedEnd)
                {
                    return false;
                }
            }
        }
        MTypes[MCount] = InType;
        ++MCount;
        return true;
    }

    [[nodiscard]] const StructType* Find(StructTypeId InId) const noexcept
    {
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < MCount; ++ScopedIndex)
        {
            if (MTypes[ScopedIndex].Id == InId)
            {
                return &MTypes[ScopedIndex];
            }
        }
        return nullptr;
    }

    [[nodiscard]] const StructType* Find(std::string_view InName) const noexcept
    {
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < MCount; ++ScopedIndex)
        {
            if (MTypes[ScopedIndex].Name == InName)
            {
                return &MTypes[ScopedIndex];
            }
        }
        return nullptr;
    }

    [[nodiscard]] std::uint32_t Count() const noexcept
    {
        return MCount;
    }
    [[nodiscard]] const StructType& At(std::uint32_t InIndex) const noexcept
    {
        return MTypes[InIndex];
    }
    void Clear() noexcept
    {
        MCount = 0;
    }

private:
    std::array<StructType, kStructRegistryCapacity> MTypes{};
    std::uint32_t MCount = 0;
};

// Process-wide registry for engine and plugin authored types. Registration is
// explicit and happens during startup; simulation threads only read it.
[[nodiscard]] StructTypeRegistry& GlobalStructTypes() noexcept;

template <typename T>
[[nodiscard]] bool RegisterStructType(StructTypeRegistry& InRegistry, std::string_view InName,
                                      std::span<const StructField> InFields = {}) noexcept
{
    static_assert(std::is_trivially_copyable_v<T>, "StructValue payloads must be trivially copyable");
    static_assert(std::is_standard_layout_v<T>, "StructValue payloads must be standard layout");
    static_assert(sizeof(T) <= kStructValueBytes, "StructValue payload exceeds the inline capacity");
    static_assert(alignof(T) <= alignof(std::max_align_t), "StructValue payload is over-aligned");
    StructType ScopedType;
    ScopedType.Id = MakeStructTypeId(InName);
    ScopedType.Name = InName;
    ScopedType.Size = static_cast<std::uint32_t>(sizeof(T));
    ScopedType.Alignment = static_cast<std::uint32_t>(alignof(T));
    ScopedType.SchemaHash = MakeStructSchemaHash(InFields);
    ScopedType.Fields = InFields;
    return InRegistry.Register(ScopedType);
}

class StructValue
{
public:
    [[nodiscard]] bool Empty() const noexcept
    {
        return MType == kInvalidStructType;
    }
    [[nodiscard]] StructTypeId Type() const noexcept
    {
        return MType;
    }
    [[nodiscard]] std::uint64_t SchemaHash() const noexcept
    {
        return MSchemaHash;
    }
    [[nodiscard]] std::uint32_t Size() const noexcept
    {
        return MSize;
    }
    [[nodiscard]] std::span<const std::byte> Bytes() const noexcept
    {
        return {MBytes.data(), MSize};
    }
    [[nodiscard]] std::span<std::byte> Bytes() noexcept
    {
        return {MBytes.data(), MSize};
    }

    void Clear() noexcept
    {
        MType = kInvalidStructType;
        MSchemaHash = 0;
        MSize = 0;
        MBytes.fill(std::byte{0});
    }

    template <typename T> [[nodiscard]] bool Set(const StructType& InType, const T& InValue) noexcept
    {
        if (InType.Id == kInvalidStructType || InType.Size != sizeof(T))
        {
            return false;
        }
        Clear();
        MType = InType.Id;
        MSchemaHash = InType.SchemaHash;
        MSize = InType.Size;
        if (InType.Fields.empty())
        {
            std::memcpy(MBytes.data(), &InValue, sizeof(T));
            return true;
        }
        const auto* ScopedSource = reinterpret_cast<const std::byte*>(&InValue);
        for (const StructField& ScopedField : InType.Fields)
        {
            if (ScopedField.Offset > MSize || ScopedField.Size > MSize - ScopedField.Offset)
            {
                Clear();
                return false;
            }
            std::memcpy(MBytes.data() + ScopedField.Offset, ScopedSource + ScopedField.Offset, ScopedField.Size);
        }
        return true;
    }

    template <typename T> [[nodiscard]] bool Set(StructTypeId InType, const T& InValue) noexcept
    {
        static_assert(std::is_trivially_copyable_v<T>, "StructValue payloads must be trivially copyable");
        static_assert(sizeof(T) <= kStructValueBytes, "StructValue payload exceeds the inline capacity");
        if (InType == kInvalidStructType || sizeof(T) > kStructValueBytes)
        {
            return false;
        }
        Clear();
        MType = InType;
        MSize = static_cast<std::uint32_t>(sizeof(T));
        std::memcpy(MBytes.data(), &InValue, sizeof(T));
        return true;
    }

    template <typename T> [[nodiscard]] const T* Get(StructTypeId InType) const noexcept
    {
        static_assert(std::is_trivially_copyable_v<T>, "StructValue payloads must be trivially copyable");
        if (MType != InType || MSize != sizeof(T))
        {
            return nullptr;
        }
        return reinterpret_cast<const T*>(MBytes.data());
    }

    template <typename T> [[nodiscard]] T* Get(StructTypeId InType) noexcept
    {
        return const_cast<T*>(std::as_const(*this).Get<T>(InType));
    }

    [[nodiscard]] bool SetRaw(StructTypeId InType, std::uint64_t InSchemaHash,
                              std::span<const std::byte> InBytes) noexcept
    {
        if (InType == kInvalidStructType || InBytes.empty() || InBytes.size() > kStructValueBytes)
        {
            return false;
        }
        Clear();
        MType = InType;
        MSchemaHash = InSchemaHash;
        MSize = static_cast<std::uint32_t>(InBytes.size());
        std::memcpy(MBytes.data(), InBytes.data(), InBytes.size());
        return true;
    }

    [[nodiscard]] std::uint64_t Hash() const noexcept
    {
        std::uint64_t ScopedHash = 1469598103934665603ull;
        const auto ScopedMix = [&ScopedHash](std::uint64_t InValue)
        {
            for (std::uint32_t ScopedByte = 0; ScopedByte < 8; ++ScopedByte)
            {
                ScopedHash ^= (InValue >> (ScopedByte * 8u)) & 0xFFu;
                ScopedHash *= 1099511628211ull;
            }
        };
        ScopedMix(MType);
        ScopedMix(MSchemaHash);
        ScopedMix(MSize);
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < MSize; ++ScopedIndex)
        {
            ScopedHash ^= static_cast<std::uint8_t>(MBytes[ScopedIndex]);
            ScopedHash *= 1099511628211ull;
        }
        return ScopedHash;
    }

private:
    StructTypeId MType = kInvalidStructType;
    std::uint64_t MSchemaHash = 0;
    std::uint32_t MSize = 0;
    alignas(std::max_align_t) std::array<std::byte, kStructValueBytes> MBytes{};
};

struct StructPayload
{
    std::array<StructValue, kStructPayloadMaxValues> Values{};
    std::uint32_t Count = 0;

    void Clear() noexcept
    {
        for (StructValue& ScopedValue : Values)
        {
            ScopedValue.Clear();
        }
        Count = 0;
    }

    template <typename T> [[nodiscard]] bool Add(const StructType& InType, const T& InValue) noexcept
    {
        if (Count >= kStructPayloadMaxValues)
        {
            return false;
        }
        if (!Values[Count].Set(InType, InValue))
        {
            return false;
        }
        ++Count;
        return true;
    }

    template <typename T> [[nodiscard]] bool Add(StructTypeId InType, const T& InValue) noexcept
    {
        if (Count >= kStructPayloadMaxValues)
        {
            return false;
        }
        if (!Values[Count].Set(InType, InValue))
        {
            return false;
        }
        ++Count;
        return true;
    }

    [[nodiscard]] bool AddRaw(StructTypeId InType, std::uint64_t InSchemaHash,
                              std::span<const std::byte> InBytes) noexcept
    {
        if (Count >= kStructPayloadMaxValues || !Values[Count].SetRaw(InType, InSchemaHash, InBytes))
        {
            return false;
        }
        ++Count;
        return true;
    }

    [[nodiscard]] bool RemoveAt(std::uint32_t InIndex) noexcept
    {
        if (InIndex >= Count)
        {
            return false;
        }
        for (std::uint32_t ScopedIndex = InIndex + 1; ScopedIndex < Count; ++ScopedIndex)
        {
            Values[ScopedIndex - 1] = Values[ScopedIndex];
        }
        Values[Count - 1].Clear();
        --Count;
        return true;
    }

    template <typename T> [[nodiscard]] const T* Find(StructTypeId InType) const noexcept
    {
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < Count; ++ScopedIndex)
        {
            if (const T* ScopedValue = Values[ScopedIndex].Get<T>(InType))
            {
                return ScopedValue;
            }
        }
        return nullptr;
    }

    template <typename T> [[nodiscard]] T* Find(StructTypeId InType) noexcept
    {
        return const_cast<T*>(std::as_const(*this).Find<T>(InType));
    }

    [[nodiscard]] std::uint64_t Hash() const noexcept
    {
        std::uint64_t ScopedHash = Count;
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < Count; ++ScopedIndex)
        {
            ScopedHash ^= Values[ScopedIndex].Hash();
            ScopedHash *= 1099511628211ull;
        }
        return ScopedHash;
    }
};

static_assert(std::is_trivially_copyable_v<StructValue>);
static_assert(std::is_trivially_copyable_v<StructPayload>);
}
