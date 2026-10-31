// FemboyEngine2D - fixed-capacity storage for decimal values.
#pragma once

#include "Engine/Core/BigNumberStore.hpp"
#include "Engine/Core/Decimal.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

namespace Fby::BigNum
{

template <std::uint32_t Capacity = 64> class DecimalStore
{
    static_assert(Capacity > 0 && Capacity <= 256, "decimal script tokens reserve eight slot bits");

public:
    using Value = Decimal128;

    [[nodiscard]] Handle Create(Value InValue = {}) noexcept
    {
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < Capacity; ++ScopedIndex)
        {
            Slot& ScopedSlot = MSlots[ScopedIndex];
            if (ScopedSlot.Used)
            {
                continue;
            }
            ScopedSlot.Generation = NextGeneration(ScopedSlot.Generation);
            ScopedSlot.Used = true;
            ScopedSlot.Data = InValue;
            ++MUsed;
            return Handle{ScopedIndex, ScopedSlot.Generation};
        }
        return {};
    }

    [[nodiscard]] Handle CreateFromString(std::string_view InText) noexcept
    {
        Value ScopedValue;
        if (Value::FromString(InText, &ScopedValue) != Status::Ok)
        {
            return {};
        }
        return Create(ScopedValue);
    }

    [[nodiscard]] bool Release(Handle InHandle) noexcept
    {
        Slot* ScopedSlot = Find(InHandle);
        if (ScopedSlot == nullptr)
        {
            return false;
        }
        ScopedSlot->Used = false;
        ScopedSlot->Data = {};
        if (MUsed > 0)
        {
            --MUsed;
        }
        return true;
    }

    [[nodiscard]] bool IsValid(Handle InHandle) const noexcept
    {
        return Find(InHandle) != nullptr;
    }

    [[nodiscard]] bool Get(Handle InHandle, Value* OutValue) const noexcept
    {
        if (OutValue == nullptr)
        {
            return false;
        }
        const Slot* ScopedSlot = Find(InHandle);
        if (ScopedSlot == nullptr)
        {
            return false;
        }
        *OutValue = ScopedSlot->Data;
        return true;
    }

    [[nodiscard]] bool Set(Handle InHandle, const Value& InValue) noexcept
    {
        Slot* ScopedSlot = Find(InHandle);
        if (ScopedSlot == nullptr)
        {
            return false;
        }
        ScopedSlot->Data = InValue;
        return true;
    }

    [[nodiscard]] Status Add(Handle InLeft, Handle InRight, Handle* OutResult) noexcept
    {
        return Binary(InLeft, InRight, OutResult, BinaryKind::Add);
    }

    [[nodiscard]] Status Sub(Handle InLeft, Handle InRight, Handle* OutResult) noexcept
    {
        return Binary(InLeft, InRight, OutResult, BinaryKind::Sub);
    }

    [[nodiscard]] Status Mul(Handle InLeft, Handle InRight, Handle* OutResult) noexcept
    {
        return Binary(InLeft, InRight, OutResult, BinaryKind::Mul);
    }

    [[nodiscard]] Status Div(Handle InLeft, Handle InRight, Handle* OutResult,
                             std::uint32_t InPrecision = kDefaultDecimalPrecision,
                             RoundingMode InRounding = RoundingMode::HalfEven) noexcept
    {
        if (OutResult == nullptr)
        {
            return Status::InvalidArgument;
        }
        const Slot* ScopedLeft = Find(InLeft);
        const Slot* ScopedRight = Find(InRight);
        if (ScopedLeft == nullptr || ScopedRight == nullptr)
        {
            return Status::InvalidArgument;
        }
        Value ScopedValue;
        const Status ScopedStatus = ScopedLeft->Data.Div(ScopedRight->Data, &ScopedValue, InPrecision, InRounding);
        if (ScopedStatus == Status::DivideByZero || ScopedStatus == Status::Overflow ||
            ScopedStatus == Status::InvalidArgument)
        {
            return ScopedStatus;
        }
        const Status ScopedPublish = Publish(ScopedValue, OutResult);
        return ScopedPublish == Status::Ok ? ScopedStatus : ScopedPublish;
    }

    [[nodiscard]] Status Compare(Handle InLeft, Handle InRight, int* OutComparison) const noexcept
    {
        if (OutComparison == nullptr)
        {
            return Status::InvalidArgument;
        }
        const Slot* ScopedLeft = Find(InLeft);
        const Slot* ScopedRight = Find(InRight);
        if (ScopedLeft == nullptr || ScopedRight == nullptr)
        {
            return Status::InvalidArgument;
        }
        *OutComparison = ScopedLeft->Data.Compare(ScopedRight->Data);
        return Status::Ok;
    }

    [[nodiscard]] Status Format(Handle InHandle, const FormatSpec& InSpec, std::span<char> OutBuffer,
                                std::size_t* OutSize = nullptr) const noexcept
    {
        const Slot* ScopedSlot = Find(InHandle);
        if (ScopedSlot == nullptr)
        {
            return Status::InvalidArgument;
        }
        return ScopedSlot->Data.Format(InSpec, OutBuffer, OutSize);
    }

    [[nodiscard]] std::uint32_t Used() const noexcept
    {
        return MUsed;
    }

    [[nodiscard]] static constexpr std::uint32_t PackScript(Handle InHandle) noexcept
    {
        if (!InHandle.IsValid())
        {
            return 0;
        }
        return ((InHandle.Generation & 0xFFFFu) << 8) | (InHandle.Index & 0xFFu);
    }

    [[nodiscard]] static constexpr Handle UnpackScript(std::uint32_t InPacked) noexcept
    {
        if (InPacked == 0)
        {
            return {};
        }
        return Handle{InPacked & 0xFFu, InPacked >> 8};
    }

    void Clear() noexcept
    {
        for (Slot& ScopedSlot : MSlots)
        {
            ScopedSlot = {};
        }
        MUsed = 0;
    }

private:
    struct Slot
    {
        Value Data{};
        std::uint32_t Generation = 0;
        bool Used = false;
    };

    enum class BinaryKind : std::uint8_t
    {
        Add = 0,
        Sub,
        Mul,
    };

    [[nodiscard]] static std::uint32_t NextGeneration(std::uint32_t InGeneration) noexcept
    {
        const std::uint32_t ScopedNext = InGeneration + 1;
        return ScopedNext == 0 ? 1u : ScopedNext;
    }

    [[nodiscard]] Slot* Find(Handle InHandle) noexcept
    {
        if (!InHandle.IsValid() || InHandle.Index >= Capacity)
        {
            return nullptr;
        }
        Slot& ScopedSlot = MSlots[InHandle.Index];
        if (!ScopedSlot.Used || ScopedSlot.Generation != InHandle.Generation)
        {
            return nullptr;
        }
        return &ScopedSlot;
    }

    [[nodiscard]] const Slot* Find(Handle InHandle) const noexcept
    {
        if (!InHandle.IsValid() || InHandle.Index >= Capacity)
        {
            return nullptr;
        }
        const Slot& ScopedSlot = MSlots[InHandle.Index];
        if (!ScopedSlot.Used || ScopedSlot.Generation != InHandle.Generation)
        {
            return nullptr;
        }
        return &ScopedSlot;
    }

    [[nodiscard]] Status Publish(const Value& InValue, Handle* OutHandle) noexcept
    {
        if (OutHandle == nullptr)
        {
            return Status::InvalidArgument;
        }
        if (!IsValid(*OutHandle))
        {
            *OutHandle = Create(InValue);
            return IsValid(*OutHandle) ? Status::Ok : Status::TooLarge;
        }
        return Set(*OutHandle, InValue) ? Status::Ok : Status::InvalidArgument;
    }

    [[nodiscard]] Status Binary(Handle InLeft, Handle InRight, Handle* OutResult, BinaryKind InKind) noexcept
    {
        if (OutResult == nullptr)
        {
            return Status::InvalidArgument;
        }
        const Slot* ScopedLeft = Find(InLeft);
        const Slot* ScopedRight = Find(InRight);
        if (ScopedLeft == nullptr || ScopedRight == nullptr)
        {
            return Status::InvalidArgument;
        }
        Value ScopedValue;
        Status ScopedStatus = Status::Ok;
        switch (InKind)
        {
            case BinaryKind::Add:
                ScopedStatus = ScopedLeft->Data.Add(ScopedRight->Data, &ScopedValue);
                break;
            case BinaryKind::Sub:
                ScopedStatus = ScopedLeft->Data.Sub(ScopedRight->Data, &ScopedValue);
                break;
            case BinaryKind::Mul:
                ScopedStatus = ScopedLeft->Data.Mul(ScopedRight->Data, &ScopedValue);
                break;
        }
        if (ScopedStatus != Status::Ok && ScopedStatus != Status::Inexact)
        {
            return ScopedStatus;
        }
        const Status ScopedPublish = Publish(ScopedValue, OutResult);
        return ScopedPublish == Status::Ok ? ScopedStatus : ScopedPublish;
    }

    std::array<Slot, Capacity> MSlots{};
    std::uint32_t MUsed = 0;
};

using DecimalPool = DecimalStore<64>;

static_assert(sizeof(Handle) == 8);
static_assert(std::is_trivially_copyable_v<DecimalPool::Value>);
}
