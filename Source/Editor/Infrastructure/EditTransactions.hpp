// FemboyEngine2D - one interactive edit gesture -> one CommandStack command.
#pragma once

#include "Editor/Infrastructure/CommandStack.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>

namespace Fby::Editor
{

class InteractionTransaction
{
public:
    enum class Source : std::uint8_t
    {
        None = 0,
        InspectorField,
        Gizmo,
        Timeline,
        AssetDrop,
        Settings,
    };

    using Result = CommandStack::Result;

    InteractionTransaction() = default;

    [[nodiscard]] Result Begin(CommandStack& InStack, Entity InEntity, std::uint16_t InCodec, std::string_view InLabel,
                               Source InSource) noexcept
    {
        if (Active())
        {
            return IsFor(InEntity, InCodec, InSource) ? Result::Ok : Result::TransactionOpen;
        }
        const std::uint32_t ScopedSize = InStack.CodecSize(InCodec);
        if (ScopedSize == 0 || ScopedSize > kMaxTrackedComponentBytes)
        {
            return Result::UnknownCodec;
        }
        std::array<std::byte, kMaxTrackedComponentBytes> ScopedOriginal{};
        const Result ScopedCaptured = InStack.CaptureComponent(InEntity, InCodec, ScopedOriginal);
        if (ScopedCaptured != Result::Ok)
        {
            return ScopedCaptured;
        }
        return BeginWithSnapshot(InStack, InEntity, InCodec, InLabel, InSource,
                                 std::span<const std::byte>(ScopedOriginal.data(), ScopedSize));
    }

    // `before` is the component image captured immediately before this frame's
    [[nodiscard]] Result BeginWithSnapshot(CommandStack& InStack, Entity InEntity, std::uint16_t InCodec,
                                           std::string_view InLabel, Source InSource,
                                           std::span<const std::byte> InBefore) noexcept
    {
        if (Active())
        {
            return IsFor(InEntity, InCodec, InSource) ? Result::Ok : Result::TransactionOpen;
        }
        const std::uint32_t ScopedSize = InStack.CodecSize(InCodec);
        if (ScopedSize == 0 || ScopedSize > kMaxTrackedComponentBytes || InBefore.size() != ScopedSize)
        {
            return Result::InvalidArgument;
        }
        const Result ScopedBegun = InStack.BeginTransaction(InLabel);
        if (ScopedBegun != Result::Ok)
        {
            return ScopedBegun;
        }
        std::memcpy(MOriginal.data(), InBefore.data(), ScopedSize);
        MSize = ScopedSize;
        MEntity = InEntity;
        MCodec = InCodec;
        MSource = InSource;
        MStack = &InStack;
        MDirty = false;
        MActive = true;
        return Result::Ok;
    }

    // Records the component's current bytes as the new side of the delta. Safe
    [[nodiscard]] Result Record() noexcept
    {
        if (!MActive || MStack == nullptr)
        {
            return Result::NoOpenTransaction;
        }
        const Result ScopedRecorded =
            MStack->RecordAfterMutation(MEntity, MCodec, std::span<const std::byte>(MOriginal.data(), MSize));
        if (ScopedRecorded == Result::Ok)
        {
            MDirty = true;
        }
        return ScopedRecorded;
    }

    // Ends the gesture. With no byte change the CommandStack cancels itself, so
    [[nodiscard]] Result Commit() noexcept
    {
        if (!MActive)
        {
            return Result::Ok;
        }
        CommandStack* ScopedStack = MStack;
        const Result ScopedResult = ScopedStack->CommitTransaction();
        Reset();
        return ScopedResult;
    }

    // Abandons the transaction without creating an undo step. The component
    void Cancel() noexcept
    {
        if (MActive && MStack != nullptr)
        {
            MStack->CancelTransaction();
        }
        Reset();
    }

    // Restores the pre-gesture bytes and cancels. This is the Escape key / lost
    [[nodiscard]] Result Abort() noexcept
    {
        if (!MActive || MStack == nullptr)
        {
            return Result::Ok;
        }
        const Result ScopedApplied =
            MStack->ApplyComponentBytes(MEntity, MCodec, std::span<const std::byte>(MOriginal.data(), MSize));
        MStack->CancelTransaction();
        Reset();
        return ScopedApplied;
    }

    [[nodiscard]] bool Active() const noexcept
    {
        return MActive;
    }
    [[nodiscard]] bool Dirty() const noexcept
    {
        return MDirty;
    }
    [[nodiscard]] Entity Target() const noexcept
    {
        return MEntity;
    }
    [[nodiscard]] std::uint16_t Codec() const noexcept
    {
        return MCodec;
    }
    [[nodiscard]] Source GetSource() const noexcept
    {
        return MSource;
    }
    [[nodiscard]] bool IsFor(Entity InEntity, std::uint16_t InCodec, Source InSource) const noexcept
    {
        return MActive && MEntity == InEntity && MCodec == InCodec && MSource == InSource;
    }
    [[nodiscard]] std::span<const std::byte> OriginalBytes() const noexcept
    {
        return std::span<const std::byte>(MOriginal.data(), MSize);
    }

private:
    void Reset() noexcept
    {
        MActive = false;
        MDirty = false;
        MStack = nullptr;
        MEntity = entt::null;
        MCodec = 0xFFFFu;
        MSource = Source::None;
        MSize = 0;
    }

    CommandStack* MStack = nullptr;
    Entity MEntity = entt::null;
    std::uint16_t MCodec = 0xFFFFu;
    Source MSource = Source::None;
    std::array<std::byte, kMaxTrackedComponentBytes> MOriginal{};
    std::uint32_t MSize = 0;
    bool MActive = false;
    bool MDirty = false;
};

// Structural counterpart of InteractionTransaction. A Content Browser drop
class SpawnTransaction
{
public:
    using Result = CommandStack::Result;

    SpawnTransaction() = default;

    [[nodiscard]] Result Begin(CommandStack& InStack, std::string_view InLabel) noexcept
    {
        if (InStack.InTransaction())
        {
            return Result::TransactionOpen;
        }
        const Result ScopedBegun = InStack.BeginTransaction(InLabel);
        if (ScopedBegun == Result::Ok)
        {
            MStack = &InStack;
            MActive = true;
            MRecorded = false;
        }
        return ScopedBegun;
    }

    [[nodiscard]] Result Record(Entity InEntity) noexcept
    {
        if (!MActive || MStack == nullptr)
        {
            return Result::NoOpenTransaction;
        }
        const Result ScopedResult = MStack->RecordSpawn(InEntity);
        if (ScopedResult == Result::Ok)
        {
            MRecorded = true;
        }
        return ScopedResult;
    }

    [[nodiscard]] Result Commit() noexcept
    {
        if (!MActive)
        {
            return Result::Ok;
        }
        CommandStack* ScopedStack = MStack;
        const Result ScopedResult = ScopedStack->CommitTransaction();
        Reset();
        return ScopedResult;
    }

    void Cancel() noexcept
    {
        if (MActive && MStack != nullptr)
        {
            MStack->CancelTransaction();
        }
        Reset();
    }

    [[nodiscard]] bool Active() const noexcept
    {
        return MActive;
    }
    [[nodiscard]] bool Recorded() const noexcept
    {
        return MRecorded;
    }

private:
    void Reset() noexcept
    {
        MActive = false;
        MRecorded = false;
        MStack = nullptr;
    }

    CommandStack* MStack = nullptr;
    bool MActive = false;
    bool MRecorded = false;
};
}
