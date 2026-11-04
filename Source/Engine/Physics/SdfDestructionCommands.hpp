// FemboyEngine2D - destruction commands for the deterministic command log.
#pragma once

#include "Engine/ECS/CommandLog.hpp"
#include "Engine/Physics/SdfDestructible.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace Fby
{

// The carve command payload is the op itself. Both sides are PODs with the same
// layout, so the log stays a byte stream and a replay rebuilds the same hole.
[[nodiscard]] inline bool PushCarveCommand(Ecs::CommandLog& InLog, Entity InEntity, const SdfCarveOp& InOp,
                                           std::uint64_t InTick)
{
    return InLog.PushWorldCommand(
        Ecs::CommandType::Carve, InEntity,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(&InOp), sizeof(SdfCarveOp)), InTick);
}

[[nodiscard]] inline bool DecodeCarveCommand(std::span<const std::byte> InPayload, SdfCarveOp* OutOp) noexcept
{
    if (OutOp == nullptr || InPayload.size() != sizeof(SdfCarveOp))
    {
        return false;
    }
    std::memcpy(OutOp, InPayload.data(), sizeof(SdfCarveOp));
    return true;
}

// Applies the decoded carve to the object and copies the payload through. The
// callback is installed into CommandWorldCallbacks by the game layer.
struct SdfCarveReplayContext
{
    SdfDestructible* Object = nullptr;
    std::uint32_t Applied = 0;
};

[[nodiscard]] inline bool ReplayCarveCommand(void* InUser, Entity InEntity, std::span<const std::byte> InPayload)
{
    (void)InEntity;
    if (InUser == nullptr)
    {
        return false;
    }
    auto* ScopedContext = static_cast<SdfCarveReplayContext*>(InUser);
    if (ScopedContext->Object == nullptr)
    {
        return false;
    }
    SdfCarveOp ScopedOp;
    if (!DecodeCarveCommand(InPayload, &ScopedOp))
    {
        return false;
    }
    if (!ScopedContext->Object->Subtract(ScopedOp))
    {
        return false;
    }
    ++ScopedContext->Applied;
    return true;
}

}
