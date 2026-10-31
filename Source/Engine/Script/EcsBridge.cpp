// FemboyEngine2D - ECS bridge resolution.
#include "Engine/Script/EcsBridge.hpp"

#include <cstdio>

namespace Fby::Script
{
namespace
{
void SetError(std::string* InError, const std::string& InMessage)
{
    if (InError != nullptr)
    {
        *InError = InMessage;
    }
}
}

bool BindProgram(const CompiledProgram& InProgram, const EcsBridge& InBridge, BoundProgram* Out, std::string* InError)
{
    if (Out == nullptr || !InProgram.valid)
    {
        SetError(InError, "BindProgram: invalid program");
        return false;
    }
    *Out = {};
    Out->program = InProgram;
    Out->componentCount = static_cast<std::uint32_t>(InProgram.components.size());
    for (std::uint32_t ScopedI = 0; ScopedI < Out->componentCount; ++ScopedI)
    {
        const ComponentBinding& ScopedBinding = InProgram.components[ScopedI];
        const ComponentAccessor* ScopedAccessor = InBridge.Find(ScopedBinding.layoutHash);
        if (ScopedAccessor == nullptr)
        {
            char ScopedBuffer[256] = {};
            std::snprintf(ScopedBuffer, sizeof(ScopedBuffer),
                          "program expects a component with LayoutHash 0x%016llX (size %u) that is not registered",
                          static_cast<unsigned long long>(ScopedBinding.layoutHash), ScopedBinding.size);
            SetError(InError, ScopedBuffer);
            return false;
        }
        if (ScopedAccessor->size != ScopedBinding.size)
        {
            SetError(InError, "program component size does not match the registered layout");
            return false;
        }
        Out->components[ScopedI].binding = &ScopedBinding;
        Out->components[ScopedI].get = ScopedAccessor->get;
        Out->components[ScopedI].valid = true;
    }
    Out->valid = true;
    return true;
}
}
