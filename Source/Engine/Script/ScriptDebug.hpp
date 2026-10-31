// FemboyEngine2D - FemboyScript debug helpers: source spans, locals, fields.
#pragma once

#include "Engine/ECS/ComponentRegistry.hpp"
#include "Engine/Script/EcsBridge.hpp"

#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>

namespace Fby::Script
{

inline constexpr std::uint32_t kInvalidPc = 0xFFFFFFFFu;

struct SourceLocation
{
    std::uint32_t Line = 0;
    std::uint32_t Column = 0;
    std::uint16_t Flags = 0;
    bool Valid = false;

    [[nodiscard]] bool IsGraphNode() const noexcept
    {
        return Valid && (Flags & DebugSpanGraphNode) != 0;
    }
};

// Instruction address to source location. Returns an invalid location when the
// program was compiled without debug information.
[[nodiscard]] inline SourceLocation LocatePc(const CompiledProgram& InProgram, std::uint32_t InPc) noexcept
{
    DebugSpan ScopedSpan;
    if (!InProgram.debug.present || !InProgram.debug.Locate(InPc, &ScopedSpan))
    {
        return SourceLocation{};
    }
    SourceLocation ScopedLocation;
    ScopedLocation.Line = ScopedSpan.Line;
    ScopedLocation.Column = ScopedSpan.Column;
    ScopedLocation.Flags = ScopedSpan.Flags;
    ScopedLocation.Valid = true;
    return ScopedLocation;
}

// First instruction belonging to a text line (or a graph node id).
[[nodiscard]] inline std::uint32_t FirstPcForLine(const CompiledProgram& InProgram, std::uint32_t InLine) noexcept
{
    for (const DebugSpan& ScopedSpan : InProgram.debug.spans)
    {
        if (ScopedSpan.Line == InLine)
        {
            return ScopedSpan.Pc;
        }
    }
    return kInvalidPc;
}

// Every instruction that belongs to `InLine`; used to set line breakpoints.
template <typename OutRange> std::uint32_t CollectPcsForLine(const CompiledProgram& InProgram, std::uint32_t InLine,
                                                             OutRange& OutPcs) noexcept
{
    std::uint32_t ScopedCount = 0;
    for (const DebugSpan& ScopedSpan : InProgram.debug.spans)
    {
        if (ScopedSpan.Line != InLine)
        {
            continue;
        }
        OutPcs[ScopedCount++] = ScopedSpan.Pc;
    }
    return ScopedCount;
}

[[nodiscard]] inline std::string_view SourceName(const CompiledProgram& InProgram) noexcept
{
    return InProgram.debug.present ? InProgram.debug.sourceName : std::string_view{};
}

[[nodiscard]] inline std::string_view VariableName(const CompiledProgram& InProgram, std::uint8_t InRegister) noexcept
{
    for (const DebugVariable& ScopedVariable : InProgram.debug.variables)
    {
        if (ScopedVariable.Register != InRegister)
        {
            continue;
        }
        if (ScopedVariable.NameOffset >= InProgram.debug.names.size())
        {
            return {};
        }
        const std::string_view ScopedRest = InProgram.debug.names.substr(ScopedVariable.NameOffset);
        const std::size_t ScopedTerminator = ScopedRest.find('\0');
        return ScopedTerminator == std::string_view::npos ? ScopedRest : ScopedRest.substr(0, ScopedTerminator);
    }
    return {};
}

// The debugger's "call stack". The VM is a flat event machine without user
// functions, so a paused program has exactly one frame: the event, the entity
// it runs for, and the source location inside it.
struct DebugFrame
{
    ScriptEvent Event = ScriptEvent::Update;
    Entity Self = entt::null;
    std::uint32_t Pc = 0;
    SourceLocation Location{};
};

[[nodiscard]] inline DebugFrame MakeFrame(const CompiledProgram& InProgram, ScriptEvent InEvent, Entity InSelf,
                                          std::uint32_t InPc) noexcept
{
    DebugFrame ScopedFrame;
    ScopedFrame.Event = InEvent;
    ScopedFrame.Self = InSelf;
    ScopedFrame.Pc = InPc;
    ScopedFrame.Location = LocatePc(InProgram, InPc);
    return ScopedFrame;
}

// One component field a program can touch, resolved to its authored name.
struct DebugField
{
    std::string_view Component{};
    std::string_view Field{};
    std::uint8_t Kind = 0;
    std::uint8_t Size = 0;
    std::uint32_t Offset = 0;
    std::uint32_t ComponentIndex = 0;
};

// Resolves the program's field bindings against the scene schema. Writes at
// most `OutFields.size()` entries and returns how many were written.
inline std::uint32_t DescribeFields(const CompiledProgram& InProgram, const Scene::ComponentRegistry& InRegistry,
                                    std::span<DebugField> OutFields) noexcept
{
    std::uint32_t ScopedCount = 0;
    for (std::uint32_t ScopedComponent = 0; ScopedComponent < InProgram.header.componentCount; ++ScopedComponent)
    {
        const ComponentBinding& ScopedBinding = InProgram.components[ScopedComponent];
        const Scene::ComponentOps* ScopedOps = nullptr;
        for (const Scene::ComponentOps& ScopedCandidate : InRegistry.All())
        {
            if (ScopedCandidate.layoutHash == ScopedBinding.layoutHash)
            {
                ScopedOps = &ScopedCandidate;
                break;
            }
        }
        for (std::uint16_t ScopedField = 0; ScopedField < ScopedBinding.fieldCount; ++ScopedField)
        {
            const std::uint32_t ScopedIndex = static_cast<std::uint32_t>(ScopedBinding.firstField) + ScopedField;
            if (ScopedIndex >= InProgram.header.fieldCount || ScopedCount >= OutFields.size())
            {
                return ScopedCount;
            }
            const FieldBinding& ScopedFieldBinding = InProgram.fields[ScopedIndex];
            DebugField ScopedOut;
            ScopedOut.Component = ScopedOps != nullptr ? ScopedOps->name : std::string_view{"<component>"};
            ScopedOut.Field = std::string_view{"<field>"};
            ScopedOut.Kind = ScopedFieldBinding.kind;
            ScopedOut.Size = ScopedFieldBinding.size;
            ScopedOut.Offset = ScopedFieldBinding.offset;
            ScopedOut.ComponentIndex = ScopedComponent;
            if (ScopedOps != nullptr)
            {
                for (const Reflect::FieldDesc& ScopedDesc : ScopedOps->fields)
                {
                    if (ScopedDesc.offset == ScopedFieldBinding.offset)
                    {
                        ScopedOut.Field = ScopedDesc.name;
                        break;
                    }
                }
            }
            OutFields[ScopedCount++] = ScopedOut;
        }
    }
    return ScopedCount;
}

// Reads a field value out of the live world as a float. Returns false when the
// entity does not carry the component or the kind is not numeric.
[[nodiscard]] inline bool ReadFieldValue(const BoundProgram& InProgram, Ecs::Registry& InRegistry, Entity InEntity,
                                         std::uint32_t InFieldIndex, float* OutValue) noexcept
{
    if (OutValue == nullptr || InFieldIndex >= InProgram.program.header.fieldCount)
    {
        return false;
    }
    const FieldBinding& ScopedField = InProgram.program.fields[InFieldIndex];
    if (ScopedField.component >= InProgram.componentCount)
    {
        return false;
    }
    const ResolvedComponent& ScopedComponent = InProgram.components[ScopedField.component];
    if (!ScopedComponent.valid || ScopedComponent.get == nullptr)
    {
        return false;
    }
    const void* ScopedBase = ScopedComponent.get(InRegistry, InEntity);
    if (ScopedBase == nullptr)
    {
        return false;
    }
    const std::byte* ScopedAddress = static_cast<const std::byte*>(ScopedBase) + ScopedField.offset;
    switch (static_cast<Reflect::FieldKind>(ScopedField.kind))
    {
        case Reflect::FieldKind::Float:
        {
            float ScopedValue = 0.0f;
            std::memcpy(&ScopedValue, ScopedAddress, sizeof(float));
            *OutValue = ScopedValue;
            return true;
        }
        case Reflect::FieldKind::Int32:
        {
            std::int32_t ScopedValue = 0;
            std::memcpy(&ScopedValue, ScopedAddress, sizeof(ScopedValue));
            *OutValue = static_cast<float>(ScopedValue);
            return true;
        }
        case Reflect::FieldKind::UInt32:
        {
            std::uint32_t ScopedValue = 0;
            std::memcpy(&ScopedValue, ScopedAddress, sizeof(ScopedValue));
            *OutValue = static_cast<float>(ScopedValue);
            return true;
        }
        case Reflect::FieldKind::Bool:
        {
            bool ScopedValue = false;
            std::memcpy(&ScopedValue, ScopedAddress, sizeof(ScopedValue));
            *OutValue = ScopedValue ? 1.0f : 0.0f;
            return true;
        }
        default:
            break;
    }
    return false;
}

// Writes a float into a live component field. Returns false when the binding is
// not a float or the component is missing.
[[nodiscard]] inline bool WriteFieldValue(const BoundProgram& InProgram, Ecs::Registry& InRegistry, Entity InEntity,
                                          std::uint32_t InFieldIndex, float InValue) noexcept
{
    if (InFieldIndex >= InProgram.program.header.fieldCount)
    {
        return false;
    }
    const FieldBinding& ScopedField = InProgram.program.fields[InFieldIndex];
    if (ScopedField.component >= InProgram.componentCount ||
        ScopedField.kind != static_cast<std::uint8_t>(Reflect::FieldKind::Float) || ScopedField.size != sizeof(float))
    {
        return false;
    }
    const ResolvedComponent& ScopedComponent = InProgram.components[ScopedField.component];
    if (!ScopedComponent.valid || ScopedComponent.get == nullptr)
    {
        return false;
    }
    void* ScopedBase = ScopedComponent.get(InRegistry, InEntity);
    if (ScopedBase == nullptr)
    {
        return false;
    }
    std::memcpy(static_cast<std::byte*>(ScopedBase) + ScopedField.offset, &InValue, sizeof(float));
    return true;
}
}
