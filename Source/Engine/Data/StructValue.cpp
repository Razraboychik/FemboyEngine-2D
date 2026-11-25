#include "Engine/Data/StructValue.hpp"

namespace Fby::Data
{
StructTypeRegistry& GlobalStructTypes() noexcept
{
    static StructTypeRegistry ScopedRegistry;
    return ScopedRegistry;
}
}
