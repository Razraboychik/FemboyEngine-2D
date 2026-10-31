// FemboyEngine2D - runtime component registry.
#pragma once

#include "Engine/Core/FlatMap.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/Core/Mirror.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace Fby::Scene
{

using Registry = entt::registry;

struct ComponentOps
{
    // Static storage: points into Reflection<T>
    std::string_view name;
    std::uint32_t size = 0;
    std::uint32_t alignment = 1;
    std::uint64_t layoutHash = 0;
    std::span<const Reflect::FieldDesc> fields;
    // Editor-only markers (selection, hover, ...) must not be written to disk.
    bool serializable = true;
    // A marker component (Hidden, ...) has no storage EnTT can hand out a
    bool empty = false;

    void (*emplaceDefault)(Registry&, Entity) = nullptr;
    void (*emplaceBytes)(Registry&, Entity, const void*) = nullptr;
    [[nodiscard]] const void* (*get)(const Registry&, Entity) = nullptr;
    [[nodiscard]] void* (*getMutable)(Registry&, Entity) = nullptr;
    [[nodiscard]] bool (*has)(const Registry&, Entity) = nullptr;
    void (*remove)(Registry&, Entity) = nullptr;
};

namespace Detail
{
template <typename T> struct ComponentOpsFor
{
    static void EmplaceDefault(Registry& InRegistry, Entity InEntity)
    {
        InRegistry.emplace<T>(InEntity);
    }

    static void EmplaceBytes(Registry& InRegistry, Entity InEntity, const void* InBytes)
    {
        if constexpr (std::is_empty_v<T>)
        {
            (void)InBytes;
            InRegistry.emplace<T>(InEntity);
        }
        else
        {
            auto& ScopedComponent = InRegistry.emplace<T>(InEntity);
            std::memcpy(&ScopedComponent, InBytes, sizeof(T));
        }
    }

    [[nodiscard]] static const void* Get(const Registry& InRegistry, Entity InEntity)
    {
        if constexpr (std::is_empty_v<T>)
        {
            (void)InRegistry;
            (void)InEntity;
            return nullptr;
        }
        else
        {
            return InRegistry.try_get<T>(InEntity);
        }
    }

    [[nodiscard]] static void* GetMutable(Registry& InRegistry, Entity InEntity)
    {
        if constexpr (std::is_empty_v<T>)
        {
            (void)InRegistry;
            (void)InEntity;
            return nullptr;
        }
        else
        {
            return InRegistry.try_get<T>(InEntity);
        }
    }

    [[nodiscard]] static bool Has(const Registry& InRegistry, Entity InEntity)
    {
        return InRegistry.all_of<T>(InEntity);
    }

    static void Remove(Registry& InRegistry, Entity InEntity)
    {
        InRegistry.remove<T>(InEntity);
    }
};
}

class ComponentRegistry
{
public:
    template <typename Component> void Register(bool InSerializable = true)
    {
        static_assert(std::is_trivially_copyable_v<Component>,
                      "a serialized component must be trivially copyable; use a POD, not std::string");
        static_assert(std::is_standard_layout_v<Component>, "a serialized component must be standard layout");
        static_assert(Reflect::IsRegistered<Component>(), "register the component with FBY_REFLECT_COMPONENT first");

        ComponentOps ScopedOps;
        ScopedOps.name = Reflect::Reflection<Component>::kName;
        ScopedOps.size = static_cast<std::uint32_t>(sizeof(Component));
        ScopedOps.alignment = static_cast<std::uint32_t>(alignof(Component));
        ScopedOps.layoutHash = Reflect::LayoutHash<Component>();
        ScopedOps.fields = Reflect::Fields<Component>();
        ScopedOps.serializable = InSerializable;
        ScopedOps.empty = std::is_empty_v<Component>;
        ScopedOps.emplaceDefault = &Detail::ComponentOpsFor<Component>::EmplaceDefault;
        ScopedOps.emplaceBytes = &Detail::ComponentOpsFor<Component>::EmplaceBytes;
        ScopedOps.get = &Detail::ComponentOpsFor<Component>::Get;
        ScopedOps.getMutable = &Detail::ComponentOpsFor<Component>::GetMutable;
        ScopedOps.has = &Detail::ComponentOpsFor<Component>::Has;
        ScopedOps.remove = &Detail::ComponentOpsFor<Component>::Remove;

        const std::string ScopedKey(ScopedOps.name);
        if (const auto ScopedExisting = MIndex.find(ScopedKey); ScopedExisting != MIndex.end())
        {
            MOps[ScopedExisting->second] = ScopedOps;
            return;
        }
        MIndex.insert_or_assign(ScopedKey, MOps.size());
        MOps.push_back(ScopedOps);
    }

    [[nodiscard]] const ComponentOps* Find(std::string_view InName) const;
    [[nodiscard]] bool Contains(std::string_view InName) const
    {
        return Find(InName) != nullptr;
    }
    [[nodiscard]] std::uint64_t LayoutHashOf(std::string_view InName) const;

    [[nodiscard]] std::size_t Size() const noexcept
    {
        return MOps.size();
    }
    [[nodiscard]] const std::deque<ComponentOps>& All() const noexcept
    {
        return MOps;
    }

private:
    // deque, not vector: SceneData and the plugin table hold ComponentOps* for
    std::deque<ComponentOps> MOps;
    Fby::FlatMap<std::string, std::size_t> MIndex;
};

// Transform2D, Velocity, Sprite2D, DrawOrder, Camera2D, Lifetime and the Hidden
void RegisterBuiltInComponents(ComponentRegistry& InRegistry);
}
