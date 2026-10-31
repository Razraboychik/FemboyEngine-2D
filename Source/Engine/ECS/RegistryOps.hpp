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
// EnTT's registry::clear() clears the component pools and invalidates the
// entities in this version, but it leaves their slots in the entity storage, so
// the hierarchy keeps counting the previous world after a scene reload. The
// entity pool has to be cleared explicitly to drop those dead slots.
inline void DestroyAllEntities(Registry& InRegistry)
{
    if (auto* ScopedHook = InRegistry.ctx().find<NativeCleanupHook>();
        ScopedHook != nullptr && ScopedHook->cleanup != nullptr)
    {
        ScopedHook->cleanup(ScopedHook->user, InRegistry);
    }
    InRegistry.clear();
    InRegistry.storage<Entity>().clear();
}
}
