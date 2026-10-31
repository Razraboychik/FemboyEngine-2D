// FemboyEngine2D - fixed-capacity storage for large numbers.
#pragma once

#include "Engine/Core/BigInt.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

namespace Fby::BigNum
{

inline constexpr std::uint32_t kInvalidBigNumber = 0xFFFFFFFFu;

// A stable slot reference. The generation rejects a handle after its slot has
// been released and reused by another value.
struct Handle
{
    std::uint32_t Index = kInvalidBigNumber;
    std::uint32_t Generation = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return Index != kInvalidBigNumber && Generation != 0;
    }

    [[nodiscard]] friend constexpr bool operator==(Handle InLeft, Handle InRight) noexcept
    {
        return InLeft.Index == InRight.Index && InLeft.Generation == InRight.Generation;
    }
};

static_assert(sizeof(Handle) == 8);
static_assert(std::is_trivially_copyable_v<Handle>);

// Values are stored in a preallocated array. Capacity is part of the type so a
// world can size the pool once and the frame path never allocates.
template <std::uint32_t MaxExpansion = 8, std::uint32_t Capacity = 256> class BigNumberStore
{
    static_assert(MaxExpansion >= 1 && MaxExpansion <= kMaxExpansion, "invalid store expansion");
    static_assert(Capacity > 0, "a big number store needs at least one slot");
    static_assert(Capacity <= 65536, "packed handles use a 16-bit slot index");

public:
    using Value = BigIntT<MaxExpansion>;

    static constexpr std::uint32_t kSlotCapacity = Capacity;

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
        return BinaryOperation(InLeft, InRight, OutResult, BinaryKind::Add);
    }

    [[nodiscard]] Status Sub(Handle InLeft, Handle InRight, Handle* OutResult) noexcept
    {
        return BinaryOperation(InLeft, InRight, OutResult, BinaryKind::Sub);
    }

    [[nodiscard]] Status Mul(Handle InLeft, Handle InRight, Handle* OutResult) noexcept
    {
        return BinaryOperation(InLeft, InRight, OutResult, BinaryKind::Mul);
    }

    [[nodiscard]] Status DivMod(Handle InLeft, Handle InRight, Handle* OutQuotient,
                                Handle* OutRemainder) noexcept
    {
        if (OutQuotient == nullptr || OutRemainder == nullptr)
        {
            return Status::InvalidArgument;
        }
        const Slot* ScopedLeft = Find(InLeft);
        const Slot* ScopedRight = Find(InRight);
        if (ScopedLeft == nullptr || ScopedRight == nullptr)
        {
            return Status::InvalidArgument;
        }
        Value ScopedQuotient;
        Value ScopedRemainder;
        const Status ScopedStatus = ScopedLeft->Data.DivMod(ScopedRight->Data, &ScopedQuotient, &ScopedRemainder);
        if (ScopedStatus != Status::Ok)
        {
            return ScopedStatus;
        }
        return PublishPair(ScopedQuotient, ScopedRemainder, OutQuotient, OutRemainder);
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

    [[nodiscard]] static constexpr std::uint32_t SlotCapacity() noexcept
    {
        return Capacity;
    }

    // Scripts have 32-bit registers, so the full handle is packed into one word.
    [[nodiscard]] static constexpr std::uint32_t Pack(Handle InHandle) noexcept
    {
        if (!InHandle.IsValid())
        {
            return 0;
        }
        return (InHandle.Generation << 16) | (InHandle.Index & 0xFFFFu);
    }

    [[nodiscard]] static constexpr Handle Unpack(std::uint32_t InPacked) noexcept
    {
        if (InPacked == 0)
        {
            return {};
        }
        return Handle{InPacked & 0xFFFFu, InPacked >> 16};
    }

    // Compact token for the script VM. The whole token fits in a float's 24-bit
    // mantissa, so a script register can carry it without losing bits.
    [[nodiscard]] static constexpr std::uint32_t PackScript(Handle InHandle) noexcept
    {
        static_assert(Capacity <= 256, "script tokens reserve eight bits for the slot");
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

    [[nodiscard]] Status PublishPair(const Value& InFirst, const Value& InSecond, Handle* OutFirst,
                                     Handle* OutSecond) noexcept
    {
        const Status ScopedFirst = Publish(InFirst, OutFirst);
        if (ScopedFirst != Status::Ok)
        {
            return ScopedFirst;
        }
        return Publish(InSecond, OutSecond);
    }

    [[nodiscard]] Status BinaryOperation(Handle InLeft, Handle InRight, Handle* OutResult,
                                         BinaryKind InKind) noexcept
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
        Value ScopedResult;
        Status ScopedStatus = Status::Ok;
        switch (InKind)
        {
            case BinaryKind::Add:
                ScopedStatus = ScopedLeft->Data.Add(ScopedRight->Data, &ScopedResult);
                break;
            case BinaryKind::Sub:
                ScopedStatus = ScopedLeft->Data.Sub(ScopedRight->Data, &ScopedResult);
                break;
            case BinaryKind::Mul:
                ScopedStatus = ScopedLeft->Data.Mul(ScopedRight->Data, &ScopedResult);
                break;
        }
        if (ScopedStatus != Status::Ok)
        {
            return ScopedStatus;
        }
        return Publish(ScopedResult, OutResult);
    }

    std::array<Slot, Capacity> MSlots{};
    std::uint32_t MUsed = 0;
};

using BigNumberPool = BigNumberStore<8, 256>;

static_assert(sizeof(Handle) == 8);
static_assert(std::is_trivially_copyable_v<BigNumberPool::Value>);
}
