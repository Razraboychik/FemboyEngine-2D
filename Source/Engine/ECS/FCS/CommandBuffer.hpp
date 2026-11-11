// FemboyEngine2D - FCS deferred structural commands.
//
// Systems queue create/destroy/emplace/remove here and play the buffer at a
// phase boundary. The buffer owns a small payload arena, so a command never
// stores a pointer into a caller's stack frame.
#pragma once

#include "Engine/ECS/FCS/World.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>
#include <vector>

namespace Fby::Fcs
{

class CommandBuffer
{
public:
    explicit CommandBuffer(World& InWorld) noexcept : MWorld(&InWorld)
    {
    }

    void Reserve(std::size_t InCommands)
    {
        MCommands.reserve(InCommands);
        MPayload.reserve(InCommands * 32u);
    }

    template <typename Component, typename... Args> void Emplace(Entity InEntity, Args&&... InArgs)
    {
        static_assert(std::is_trivially_copyable_v<Component>, "FCS commands accept trivially copyable components");
        Component ScopedValue(std::forward<Args>(InArgs)...);
        Push(CommandKind::Emplace, InEntity, &ApplyEmplace<Component>, &ScopedValue, sizeof(Component),
             alignof(Component));
    }

    template <typename Component> void Remove(Entity InEntity)
    {
        Push(CommandKind::Remove, InEntity, &ApplyRemove<Component>, nullptr, 0, 1);
    }

    void Destroy(Entity InEntity)
    {
        Push(CommandKind::Destroy, InEntity, &ApplyDestroy, nullptr, 0, 1);
    }

    void Play()
    {
        for (const Command& ScopedCommand : MCommands)
        {
            const void* ScopedPayload =
                ScopedCommand.HasPayload ? MPayload.data() + ScopedCommand.PayloadOffset : nullptr;
            ScopedCommand.Apply(*MWorld, ScopedCommand.Target, ScopedPayload);
        }
        MCommands.clear();
        MPayload.clear();
    }

    void Clear() noexcept
    {
        MCommands.clear();
        MPayload.clear();
    }

    [[nodiscard]] std::size_t Count() const noexcept
    {
        return MCommands.size();
    }

private:
    enum class CommandKind : std::uint8_t
    {
        Emplace,
        Remove,
        Destroy,
    };

    using ApplyFunction = void (*)(World&, Entity, const void*);

    struct Command
    {
        CommandKind Kind = CommandKind::Destroy;
        Entity Target = kNullEntity;
        ApplyFunction Apply = nullptr;
        std::size_t PayloadOffset = 0;
        bool HasPayload = false;
    };

    void Push(CommandKind InKind, Entity InEntity, ApplyFunction InApply, const void* InValue, std::size_t InBytes,
              std::size_t InAlignment)
    {
        if (!InEntity.IsValid() || InApply == nullptr || MWorld == nullptr)
        {
            return;
        }
        const std::size_t ScopedAligned = (MPayload.size() + InAlignment - 1u) & ~(InAlignment - 1u);
        if (ScopedAligned + InBytes > MPayload.size())
        {
            MPayload.resize(ScopedAligned + InBytes);
        }
        void* ScopedDestination = MPayload.data() + ScopedAligned;
        if (InBytes > 0)
        {
            std::memcpy(ScopedDestination, InValue, InBytes);
        }
        Command ScopedCommand;
        ScopedCommand.Kind = InKind;
        ScopedCommand.Target = InEntity;
        ScopedCommand.Apply = InApply;
        ScopedCommand.PayloadOffset = ScopedAligned;
        ScopedCommand.HasPayload = InBytes > 0;
        MCommands.push_back(ScopedCommand);
    }

    template <typename Component> static void ApplyEmplace(World& InWorld, Entity InEntity, const void* InPayload)
    {
        if (InWorld.IsAlive(InEntity))
        {
            InWorld.Emplace<Component>(InEntity, *static_cast<const Component*>(InPayload));
        }
    }

    template <typename Component> static void ApplyRemove(World& InWorld, Entity InEntity, const void*)
    {
        (void)InWorld.Remove<Component>(InEntity);
    }

    static void ApplyDestroy(World& InWorld, Entity InEntity, const void*)
    {
        InWorld.Destroy(InEntity);
    }

    World* MWorld = nullptr;
    std::vector<Command> MCommands;
    std::vector<std::byte> MPayload;
};

}
