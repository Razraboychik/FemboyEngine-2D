// FemboyEngine2D - fixed-capacity heap for FemboyScript values.
#include "Engine/Script/ScriptHeap.hpp"

#include <algorithm>
#include <cstring>

namespace Fby::Script
{
namespace
{
constexpr std::uint32_t kInitialArrayCapacity = 4;
constexpr std::uint32_t kInitialMapCapacity = 4;
}

void ScriptHeap::Reset() noexcept
{
    MObjectCount = 0;
    MStringUsed = 0;
    MElementUsed = 0;
    MMapEntryUsed = 0;
}

const ScriptHeap::Object* ScriptHeap::Find(std::uint32_t InHandle, ObjectKind InKind) const noexcept
{
    if (InHandle >= MObjectCount || MObjects[InHandle].Kind != InKind)
    {
        return nullptr;
    }
    return &MObjects[InHandle];
}

ScriptHeap::Object* ScriptHeap::FindMutable(std::uint32_t InHandle, ObjectKind InKind) noexcept
{
    if (InHandle >= MObjectCount || MObjects[InHandle].Kind != InKind)
    {
        return nullptr;
    }
    return &MObjects[InHandle];
}

std::uint32_t ScriptHeap::InternString(std::string_view InText) noexcept
{
    if (InText.size() > kMaxStringBytes - MStringUsed || MObjectCount >= kMaxObjects)
    {
        return kInvalidHandle;
    }
    Object ScopedObject;
    ScopedObject.Kind = ObjectKind::String;
    ScopedObject.First = MStringUsed;
    ScopedObject.Length = static_cast<std::uint32_t>(InText.size());
    ScopedObject.Capacity = static_cast<std::uint32_t>(InText.size());
    if (!InText.empty())
    {
        std::memcpy(MStringBytes.data() + MStringUsed, InText.data(), InText.size());
    }
    MStringUsed += static_cast<std::uint32_t>(InText.size());
    MObjects[MObjectCount] = ScopedObject;
    return MObjectCount++;
}

bool ScriptHeap::StringView(std::uint32_t InHandle, std::string_view* OutText) const noexcept
{
    const Object* ScopedObject = Find(InHandle, ObjectKind::String);
    if (ScopedObject == nullptr)
    {
        return false;
    }
    if (OutText != nullptr)
    {
        *OutText = std::string_view(MStringBytes.data() + ScopedObject->First, ScopedObject->Length);
    }
    return true;
}

bool ScriptHeap::ConcatStrings(std::uint32_t InLeft, std::uint32_t InRight, std::uint32_t* OutHandle) noexcept
{
    std::string_view ScopedLeft;
    std::string_view ScopedRight;
    if (!StringView(InLeft, &ScopedLeft) || !StringView(InRight, &ScopedRight))
    {
        return false;
    }
    if (ScopedLeft.size() + ScopedRight.size() > kMaxStringBytes - MStringUsed)
    {
        return false;
    }
    if (MObjectCount >= kMaxObjects)
    {
        return false;
    }
    Object ScopedObject;
    ScopedObject.Kind = ObjectKind::String;
    ScopedObject.First = MStringUsed;
    ScopedObject.Length = static_cast<std::uint32_t>(ScopedLeft.size() + ScopedRight.size());
    ScopedObject.Capacity = ScopedObject.Length;
    if (!ScopedLeft.empty())
    {
        std::memcpy(MStringBytes.data() + MStringUsed, ScopedLeft.data(), ScopedLeft.size());
    }
    std::memcpy(MStringBytes.data() + MStringUsed + ScopedLeft.size(), ScopedRight.data(), ScopedRight.size());
    MStringUsed += ScopedObject.Length;
    MObjects[MObjectCount] = ScopedObject;
    if (OutHandle != nullptr)
    {
        *OutHandle = MObjectCount;
    }
    ++MObjectCount;
    return true;
}

std::uint32_t ScriptHeap::NewArray() noexcept
{
    if (MObjectCount >= kMaxObjects || MElements.size() - MElementUsed < kInitialArrayCapacity)
    {
        return kInvalidHandle;
    }
    Object ScopedObject;
    ScopedObject.Kind = ObjectKind::Array;
    ScopedObject.First = MElementUsed;
    ScopedObject.Length = 0;
    ScopedObject.Capacity = kInitialArrayCapacity;
    for (std::uint32_t ScopedI = 0; ScopedI < kInitialArrayCapacity; ++ScopedI)
    {
        MElements[MElementUsed + ScopedI] = {};
    }
    MElementUsed += kInitialArrayCapacity;
    MObjects[MObjectCount] = ScopedObject;
    return MObjectCount++;
}

bool ScriptHeap::GrowArray(Object& InObject) noexcept
{
    if (InObject.Capacity >= kArrayGrowthLimit)
    {
        return false;
    }
    const std::uint32_t ScopedCapacity = std::min(InObject.Capacity * 2u, kArrayGrowthLimit);
    if (ScopedCapacity > kMaxElements - MElementUsed)
    {
        return false;
    }
    const std::uint32_t ScopedFirst = MElementUsed;
    for (std::uint32_t ScopedI = 0; ScopedI < ScopedCapacity; ++ScopedI)
    {
        MElements[ScopedFirst + ScopedI] = ScopedI < InObject.Length ? MElements[InObject.First + ScopedI]
                                                                     : ScriptValue{};
    }
    MElementUsed += ScopedCapacity;
    InObject.First = ScopedFirst;
    InObject.Capacity = ScopedCapacity;
    return true;
}

bool ScriptHeap::ArrayPush(std::uint32_t InArray, ScriptValue InValue) noexcept
{
    Object* ScopedObject = FindMutable(InArray, ObjectKind::Array);
    if (ScopedObject == nullptr)
    {
        return false;
    }
    if (ScopedObject->Length >= ScopedObject->Capacity && !GrowArray(*ScopedObject))
    {
        return false;
    }
    MElements[ScopedObject->First + ScopedObject->Length] = InValue;
    ++ScopedObject->Length;
    return true;
}

bool ScriptHeap::ArrayGet(std::uint32_t InArray, std::uint32_t InIndex, ScriptValue* OutValue) const noexcept
{
    const Object* ScopedObject = Find(InArray, ObjectKind::Array);
    if (ScopedObject == nullptr || InIndex >= ScopedObject->Length)
    {
        return false;
    }
    if (OutValue != nullptr)
    {
        *OutValue = MElements[ScopedObject->First + InIndex];
    }
    return true;
}

bool ScriptHeap::ArraySet(std::uint32_t InArray, std::uint32_t InIndex, ScriptValue InValue) noexcept
{
    Object* ScopedObject = FindMutable(InArray, ObjectKind::Array);
    if (ScopedObject == nullptr || InIndex >= ScopedObject->Length)
    {
        return false;
    }
    MElements[ScopedObject->First + InIndex] = InValue;
    return true;
}

bool ScriptHeap::ArrayLength(std::uint32_t InArray, std::uint32_t* OutLength) const noexcept
{
    const Object* ScopedObject = Find(InArray, ObjectKind::Array);
    if (ScopedObject == nullptr)
    {
        return false;
    }
    if (OutLength != nullptr)
    {
        *OutLength = ScopedObject->Length;
    }
    return true;
}

std::uint32_t ScriptHeap::NewMap() noexcept
{
    if (MObjectCount >= kMaxObjects || MMapEntries.size() - MMapEntryUsed < kInitialMapCapacity)
    {
        return kInvalidHandle;
    }
    Object ScopedObject;
    ScopedObject.Kind = ObjectKind::Map;
    ScopedObject.First = MMapEntryUsed;
    ScopedObject.Length = 0;
    ScopedObject.Capacity = kInitialMapCapacity;
    for (std::uint32_t ScopedI = 0; ScopedI < kInitialMapCapacity; ++ScopedI)
    {
        MMapEntries[MMapEntryUsed + ScopedI] = {};
    }
    MMapEntryUsed += kInitialMapCapacity;
    MObjects[MObjectCount] = ScopedObject;
    return MObjectCount++;
}

bool ScriptHeap::GrowMap(Object& InObject) noexcept
{
    if (InObject.Capacity >= kMapGrowthLimit)
    {
        return false;
    }
    const std::uint32_t ScopedCapacity = std::min(InObject.Capacity * 2u, kMapGrowthLimit);
    if (ScopedCapacity > kMaxMapEntries - MMapEntryUsed)
    {
        return false;
    }
    const std::uint32_t ScopedFirst = MMapEntryUsed;
    for (std::uint32_t ScopedI = 0; ScopedI < ScopedCapacity; ++ScopedI)
    {
        MMapEntries[ScopedFirst + ScopedI] =
            ScopedI < InObject.Length ? MMapEntries[InObject.First + ScopedI] : MapEntry{};
    }
    MMapEntryUsed += ScopedCapacity;
    InObject.First = ScopedFirst;
    InObject.Capacity = ScopedCapacity;
    return true;
}

bool ScriptHeap::Equal(const ScriptValue& InLeft, const ScriptValue& InRight) const noexcept
{
    const auto ScopedIsObject = [](ValueTag InTag)
    {
        return InTag == ValueTag::String || InTag == ValueTag::Array || InTag == ValueTag::Map;
    };
    const bool ScopedLeftObject = ScopedIsObject(InLeft.Tag);
    const bool ScopedRightObject = ScopedIsObject(InRight.Tag);
    if (ScopedLeftObject != ScopedRightObject)
    {
        return false;
    }
    if (!ScopedLeftObject)
    {
        // Numeric tags (float, int, bool, entity) compare by payload bits, so a
        // value keeps its identity when a move or an arithmetic result retags it.
        return InLeft.Bits == InRight.Bits;
    }
    if (InLeft.Tag != InRight.Tag)
    {
        return false;
    }
    if (InLeft.Tag == ValueTag::String)
    {
        std::string_view ScopedLeft;
        std::string_view ScopedRight;
        return StringView(InLeft.Bits, &ScopedLeft) && StringView(InRight.Bits, &ScopedRight) &&
               ScopedLeft == ScopedRight;
    }
    return InLeft.Bits == InRight.Bits;
}

bool ScriptHeap::MapSet(std::uint32_t InMap, ScriptValue InKey, ScriptValue InValue) noexcept
{
    Object* ScopedObject = FindMutable(InMap, ObjectKind::Map);
    if (ScopedObject == nullptr)
    {
        return false;
    }
    for (std::uint32_t ScopedI = 0; ScopedI < ScopedObject->Length; ++ScopedI)
    {
        MapEntry& ScopedEntry = MMapEntries[ScopedObject->First + ScopedI];
        if (Equal(ScopedEntry.Key, InKey))
        {
            ScopedEntry.Value = InValue;
            return true;
        }
    }
    if (ScopedObject->Length >= ScopedObject->Capacity && !GrowMap(*ScopedObject))
    {
        return false;
    }
    MapEntry& ScopedEntry = MMapEntries[ScopedObject->First + ScopedObject->Length];
    ScopedEntry.Key = InKey;
    ScopedEntry.Value = InValue;
    ++ScopedObject->Length;
    return true;
}

bool ScriptHeap::MapGet(std::uint32_t InMap, ScriptValue InKey, ScriptValue* OutValue) const noexcept
{
    const Object* ScopedObject = Find(InMap, ObjectKind::Map);
    if (ScopedObject == nullptr)
    {
        return false;
    }
    for (std::uint32_t ScopedI = 0; ScopedI < ScopedObject->Length; ++ScopedI)
    {
        const MapEntry& ScopedEntry = MMapEntries[ScopedObject->First + ScopedI];
        if (Equal(ScopedEntry.Key, InKey))
        {
            if (OutValue != nullptr)
            {
                *OutValue = ScopedEntry.Value;
            }
            return true;
        }
    }
    return false;
}

bool ScriptHeap::MapHas(std::uint32_t InMap, ScriptValue InKey) const noexcept
{
    return MapGet(InMap, InKey, nullptr);
}

bool ScriptHeap::MapLength(std::uint32_t InMap, std::uint32_t* OutLength) const noexcept
{
    const Object* ScopedObject = Find(InMap, ObjectKind::Map);
    if (ScopedObject == nullptr)
    {
        return false;
    }
    if (OutLength != nullptr)
    {
        *OutLength = ScopedObject->Length;
    }
    return true;
}
}
