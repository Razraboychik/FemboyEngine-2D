// FemboyEngine2D - registry operations shared by scene, snapshot and PIE code.
#pragma once

#include "Engine/Core/Assert.hpp"
#include "Engine/ECS/ComponentRegistry.hpp"

#include <array>
#include <cstddef>

namespace Fby::Scene
{

// A subsystem that owns native resources keyed by entity (physics bodies,
// navigation agents) registers a cleanup hook in the registry context. Clearing
// the registry then releases those resources before the components disappear.
struct NativeCleanupHook
{
    void (*cleanup)(void* InUser, Registry& InRegistry) noexcept = nullptr;
    void* user = nullptr;
};

inline constexpr std::size_t kMaxNativeCleanupHooks = 8;

struct NativeCleanupHooks
{
    std::array<NativeCleanupHook, kMaxNativeCleanupHooks> hooks{};
    std::size_t count = 0;
};

inline void SetNativeCleanupHook(Registry& InRegistry, void (*InCleanup)(void*, Registry&) noexcept,
                                 void* InUser) noexcept
{
    auto* ScopedHooks = InRegistry.ctx().find<NativeCleanupHooks>();
    if (ScopedHooks == nullptr)
    {
        InRegistry.ctx().insert_or_assign<NativeCleanupHooks>(NativeCleanupHooks{});
        ScopedHooks = InRegistry.ctx().find<NativeCleanupHooks>();
    }
    for (std::size_t ScopedIndex = 0; ScopedIndex < ScopedHooks->count; ++ScopedIndex)
    {
        if (ScopedHooks->hooks[ScopedIndex].user == InUser)
        {
            ScopedHooks->hooks[ScopedIndex] = NativeCleanupHook{InCleanup, InUser};
            return;
        }
    }
    FBY_ASSERT_MSG(ScopedHooks->count < kMaxNativeCleanupHooks, "too many native cleanup hooks");
    if (ScopedHooks->count < kMaxNativeCleanupHooks)
    {
        ScopedHooks->hooks[ScopedHooks->count++] = NativeCleanupHook{InCleanup, InUser};
    }
}

inline void ClearNativeCleanupHook(Registry& InRegistry, void* InUser) noexcept
{
    auto* ScopedHooks = InRegistry.ctx().find<NativeCleanupHooks>();
    if (ScopedHooks == nullptr)
    {
        return;
    }
    for (std::size_t ScopedIndex = 0; ScopedIndex < ScopedHooks->count; ++ScopedIndex)
    {
        if (ScopedHooks->hooks[ScopedIndex].user != InUser)
        {
            continue;
        }
        for (std::size_t ScopedShift = ScopedIndex + 1; ScopedShift < ScopedHooks->count; ++ScopedShift)
        {
            ScopedHooks->hooks[ScopedShift - 1] = ScopedHooks->hooks[ScopedShift];
        }
        --ScopedHooks->count;
        ScopedHooks->hooks[ScopedHooks->count] = NativeCleanupHook{};
        return;
    }
}

// Destroys every entity in the registry.
//
// Registry::clear() clears the FCS records and invalidates the entities; the
// cleanup hook releases native resources (physics bodies, navigation agents)
// before the components disappear.
inline void DestroyAllEntities(Registry& InRegistry)
{
    if (auto* ScopedHooks = InRegistry.ctx().find<NativeCleanupHooks>(); ScopedHooks != nullptr)
    {
        for (std::size_t ScopedIndex = 0; ScopedIndex < ScopedHooks->count; ++ScopedIndex)
        {
            if (ScopedHooks->hooks[ScopedIndex].cleanup != nullptr)
            {
                ScopedHooks->hooks[ScopedIndex].cleanup(ScopedHooks->hooks[ScopedIndex].user, InRegistry);
            }
        }
    }
    InRegistry.clear();
}
}
