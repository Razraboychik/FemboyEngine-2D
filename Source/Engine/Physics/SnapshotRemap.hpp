// FemboyEngine2D - Snapshot v4 physics remap table.
#pragma once

#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/Schedule.hpp"
#include "Engine/Physics/Physics.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace Fby
{

inline constexpr std::uint32_t kMaxPhysicsRemapEntries = 16384;

struct PhysicsBodyRemapEntry
{
    Entity entity = entt::null;
    std::uint64_t serializedBody = 0;
    std::uint64_t liveBody = 0;
};

class PhysicsBodyRemapTable
{
public:
    void Clear() noexcept
    {
        MCount = 0;
    }
    [[nodiscard]] bool Add(Entity InEntity, std::uint64_t InSerializedBody, std::uint64_t InLiveBody) noexcept
    {
        if (MCount >= MEntries.size())
        {
            return false;
        }
        MEntries[MCount++] = PhysicsBodyRemapEntry{InEntity, InSerializedBody, InLiveBody};
        return true;
    }
    [[nodiscard]] const PhysicsBodyRemapEntry* FindByEntity(Entity InEntity) const noexcept
    {
        for (std::uint32_t ScopedI = 0; ScopedI < MCount; ++ScopedI)
        {
            if (MEntries[ScopedI].entity == InEntity)
            {
                return &MEntries[ScopedI];
            }
        }
        return nullptr;
    }
    [[nodiscard]] const PhysicsBodyRemapEntry* FindBySerialized(std::uint64_t InSerializedBody) const noexcept
    {
        // 0 means "no serialized identity". After a snapshot restore every row
        // can carry it, and matching them to the first row would be wrong.
        if (InSerializedBody == 0)
        {
            return nullptr;
        }
        for (std::uint32_t ScopedI = 0; ScopedI < MCount; ++ScopedI)
        {
            if (MEntries[ScopedI].serializedBody == InSerializedBody)
            {
                return &MEntries[ScopedI];
            }
        }
        return nullptr;
    }
    [[nodiscard]] std::uint64_t RemapBody(std::uint64_t InSerializedBody) const noexcept
    {
        const PhysicsBodyRemapEntry* ScopedEntry = FindBySerialized(InSerializedBody);
        return ScopedEntry != nullptr ? ScopedEntry->liveBody : 0;
    }
    // Restore-time lookup: after a snapshot restore the entity is the only
    // stable key, because the serialized body handles are gone.
    [[nodiscard]] std::uint64_t RemapBodyForEntity(Entity InEntity) const noexcept
    {
        const PhysicsBodyRemapEntry* ScopedEntry = FindByEntity(InEntity);
        return ScopedEntry != nullptr ? ScopedEntry->liveBody : 0;
    }
    bool SetLive(Entity InEntity, std::uint64_t InLiveBody) noexcept
    {
        for (std::uint32_t ScopedI = 0; ScopedI < MCount; ++ScopedI)
        {
            if (MEntries[ScopedI].entity == InEntity)
            {
                MEntries[ScopedI].liveBody = InLiveBody;
                return true;
            }
        }
        return false;
    }
    [[nodiscard]] std::uint32_t Count() const noexcept
    {
        return MCount;
    }
    [[nodiscard]] std::span<const PhysicsBodyRemapEntry> Entries() const noexcept
    {
        return {MEntries.data(), MCount};
    }

private:
    std::array<PhysicsBodyRemapEntry, kMaxPhysicsRemapEntries> MEntries{};
    std::uint32_t MCount = 0;
};

// Captures the current packed body ids, clears every native handle in the
[[nodiscard]] bool RebuildPhysicsBodies(Ecs::Registry& InRegistry, PhysicsSystem& InPhysics,
                                        PhysicsBodyRemapTable& InRemap) noexcept;
}
