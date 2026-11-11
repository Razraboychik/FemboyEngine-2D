// FemboyEngine2D - registry operations shared by scene, snapshot and PIE code.
#pragma once

#include "Engine/ECS/ComponentRegistry.hpp"

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

inline void SetNativeCleanupHook(Registry& InRegistry, void (*InCleanup)(void*, Registry&) noexcept,
                                 void* InUser) noexcept
{
    InRegistry.ctx().insert_or_assign<NativeCleanupHook>(NativeCleanupHook{InCleanup, InUser});
}

// Destroys every entity in the registry.
//
// Registry::clear() clears the FCS records and invalidates the entities; the
// cleanup hook releases native resources (physics bodies, navigation agents)
// before the components disappear.
inline void DestroyAllEntities(Registry& InRegistry)
{
    if (auto* ScopedHook = InRegistry.ctx().find<NativeCleanupHook>();
        ScopedHook != nullptr && ScopedHook->cleanup != nullptr)
    {
        ScopedHook->cleanup(ScopedHook->user, InRegistry);
    }
    InRegistry.clear();
}
}
