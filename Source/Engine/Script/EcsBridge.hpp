// FemboyEngine2D - the ECS bridge between bytecode and EnTT storage.
#pragma once

#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/Schedule.hpp"
#include "Engine/Core/Mirror.hpp"
#include "Engine/Script/Bytecode.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <type_traits>

namespace Fby::Script
{

struct ComponentAccessor
{
    std::uint64_t layoutHash = 0;
    std::uint32_t size = 0;
    const char* name = nullptr;
    // Type-erased component lookup. Returns nullptr when the entity does not
    void* (*get)(Ecs::Registry& InRegistry, Entity InEntity) noexcept = nullptr;
};

class EcsBridge
{
public:
    static constexpr std::uint32_t kMaxAccessors = 64;

    template <typename T> bool RegisterComponent(const char* InName = nullptr) noexcept
    {
        static_assert(std::is_standard_layout_v<T>, "Mirror components are standard layout");
        static_assert(std::is_trivially_copyable_v<T>, "Mirror components are trivially copyable");
        static_assert(Reflect::IsRegistered<T>(), "component is not registered with FBY_REFLECT_COMPONENT");

        constexpr std::uint64_t ScopedHash = Reflect::LayoutHash<T>();
        if (Find(ScopedHash) != nullptr)
        {
            // Already registered; registration is idempotent
            return true;
        }
        if (MCount >= kMaxAccessors)
        {
            return false;
        }
        ComponentAccessor ScopedAccessor;
        ScopedAccessor.layoutHash = ScopedHash;
        ScopedAccessor.size = static_cast<std::uint32_t>(sizeof(T));
        ScopedAccessor.name = InName != nullptr ? InName : Reflect::Reflection<T>::kName.data();
        ScopedAccessor.get = [](Ecs::Registry& InRegistry, Entity InEntity) noexcept -> void*
        {
            T* ScopedComponent = InRegistry.try_get<T>(InEntity);
            return ScopedComponent;
        };
        MAccessors[MCount++] = ScopedAccessor;
        return true;
    }

    [[nodiscard]] const ComponentAccessor* Find(std::uint64_t InLayoutHash) const noexcept
    {
        for (std::uint32_t ScopedI = 0; ScopedI < MCount; ++ScopedI)
        {
            if (MAccessors[ScopedI].layoutHash == InLayoutHash)
            {
                return &MAccessors[ScopedI];
            }
        }
        return nullptr;
    }

    [[nodiscard]] const ComponentAccessor& Accessor(std::uint16_t InSlot) const noexcept
    {
        return MAccessors[InSlot];
    }

    [[nodiscard]] std::uint32_t Count() const noexcept
    {
        return MCount;
    }

    void Clear() noexcept
    {
        MCount = 0;
    }

private:
    std::array<ComponentAccessor, kMaxAccessors> MAccessors{};
    std::uint32_t MCount = 0;
};

// One resolved component of a loaded program. `binding` points into the cooked
struct ResolvedComponent
{
    const ComponentBinding* binding = nullptr;
    void* (*get)(Ecs::Registry& InRegistry, Entity InEntity) noexcept = nullptr;
    bool valid = false;
};

// A CompiledProgram plus the pointers it resolved to. Copied by value into the
struct BoundProgram
{
    CompiledProgram program{};
    std::array<ResolvedComponent, kMaxComponentBindings> components{};
    std::uint32_t componentCount = 0;
    bool valid = false;
};

// Validates every component LayoutHash against the bridge and fills the getter
[[nodiscard]] bool BindProgram(const CompiledProgram& InProgram, const EcsBridge& InBridge, BoundProgram* Out,
                               std::string* InError = nullptr);
}
