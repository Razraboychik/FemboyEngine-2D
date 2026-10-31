// FemboyEngine2D - deferred structural ECS changes.
#pragma once

#include "Engine/Core/FrameArena.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/Schedule.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace Fby::Ecs
{

inline constexpr std::uint32_t kCommandBufferCapacity = 4096;

enum class CommandKind : std::uint8_t
{
    Destroy,
    Emplace,
    Replace,
    Remove,
};

using CommandApply = void (*)(Registry&, Entity, const void*);

struct Command
{
    CommandKind kind = CommandKind::Destroy;
    Entity entity = entt::null;
    CommandApply apply = nullptr;
    const void* payload = nullptr;
};

class CommandBuffer
{
public:
    CommandBuffer() = default;

    void Initialize(FrameArena* InArena) noexcept
    {
        MArena = InArena;
        MCount = 0;
    }

    void Reset() noexcept
    {
        MCount = 0;
    }

    // Drops commands appended after `InCount`. PrefabSystem uses this as a
    void Truncate(std::uint32_t InCount) noexcept
    {
        if (InCount < MCount)
        {
            MCount = InCount;
        }
    }

    template <typename T> [[nodiscard]] bool Emplace(Entity InEntity, const T& InComponent = T{})
    {
        static_assert(std::is_trivially_copyable_v<T>, "a command payload must be POD");
        return Push(CommandKind::Emplace, InEntity, &ApplyEmplace<T>, InComponent);
    }

    template <typename T> [[nodiscard]] bool Replace(Entity InEntity, const T& InComponent)
    {
        static_assert(std::is_trivially_copyable_v<T>, "a command payload must be POD");
        return Push(CommandKind::Replace, InEntity, &ApplyReplace<T>, InComponent);
    }

    template <typename T> [[nodiscard]] bool Remove(Entity InEntity)
    {
        return Push(CommandKind::Remove, InEntity, &ApplyRemove<T>, T{});
    }

    [[nodiscard]] bool Destroy(Entity InEntity)
    {
        return Push(CommandKind::Destroy, InEntity, &ApplyDestroy, std::uint32_t{0});
    }

    // Applies every command in order. The arena is not reset here: the caller
    void Play(Registry& InRegistry)
    {
        for (std::uint32_t ScopedI = 0; ScopedI < MCount; ++ScopedI)
        {
            const Command& ScopedCommand = MCommands[ScopedI];
            if (ScopedCommand.entity != entt::null && ScopedCommand.apply != nullptr)
            {
                ScopedCommand.apply(InRegistry, ScopedCommand.entity, ScopedCommand.payload);
            }
        }
        MCount = 0;
    }

    [[nodiscard]] std::uint32_t Count() const noexcept
    {
        return MCount;
    }

private:
    template <typename T>
    [[nodiscard]] bool Push(CommandKind InKind, Entity InEntity, CommandApply InApply, const T& InValue)
    {
        if (MArena == nullptr || MCount >= kCommandBufferCapacity || InEntity == entt::null)
        {
            return false;
        }
        void* ScopedStorage = MArena->Allocate(sizeof(T), alignof(T));
        if (ScopedStorage == nullptr)
        {
            return false;
        }
        std::memcpy(ScopedStorage, &InValue, sizeof(T));
        MCommands[MCount++] = Command{InKind, InEntity, InApply, ScopedStorage};
        return true;
    }

    template <typename T> static void ApplyEmplace(Registry& InRegistry, Entity InEntity, const void* InPayload)
    {
        if (InRegistry.valid(InEntity))
        {
            InRegistry.emplace_or_replace<T>(InEntity, *static_cast<const T*>(InPayload));
        }
    }

    template <typename T> static void ApplyReplace(Registry& InRegistry, Entity InEntity, const void* InPayload)
    {
        if (InRegistry.valid(InEntity) && InRegistry.all_of<T>(InEntity))
        {
            InRegistry.emplace_or_replace<T>(InEntity, *static_cast<const T*>(InPayload));
        }
    }

    template <typename T> static void ApplyRemove(Registry& InRegistry, Entity InEntity, const void*)
    {
        if (InRegistry.valid(InEntity))
        {
            InRegistry.remove<T>(InEntity);
        }
    }

    static void ApplyDestroy(Registry& InRegistry, Entity InEntity, const void*)
    {
        if (InRegistry.valid(InEntity))
        {
            InRegistry.destroy(InEntity);
        }
    }

    FrameArena* MArena = nullptr;
    std::array<Command, kCommandBufferCapacity> MCommands{};
    std::uint32_t MCount = 0;
};
}
