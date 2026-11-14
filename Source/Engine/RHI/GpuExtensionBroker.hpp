// FemboyEngine2D - backend extension requirements and capability filtering.
#pragma once

#include <algorithm>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Fby::Rhi
{

// Extensions requested by independent subsystems (presentation, NGX, FidelityFX,
// virtual texturing). The broker deduplicates them and filters against what the
// driver actually exposes, so a missing optional extension is reported instead
// of failing vkCreateInstance/vkCreateDevice.
class GpuExtensionBroker
{
public:
    void AddInstance(std::string_view InName)
    {
        AddUnique(MInstance, InName);
    }
    void AddDevice(std::string_view InName)
    {
        AddUnique(MDevice, InName);
    }
    void Add(const GpuExtensionBroker& InOther)
    {
        for (const std::string& ScopedName : InOther.MInstance)
        {
            AddUnique(MInstance, ScopedName);
        }
        for (const std::string& ScopedName : InOther.MDevice)
        {
            AddUnique(MDevice, ScopedName);
        }
    }

    [[nodiscard]] const std::vector<std::string>& Instance() const noexcept
    {
        return MInstance;
    }
    [[nodiscard]] const std::vector<std::string>& Device() const noexcept
    {
        return MDevice;
    }

    // Returns the subset present in the driver lists. Names absent from the
    // driver are appended to OutMissing when it is non-null.
    [[nodiscard]] GpuExtensionBroker Filter(std::span<const std::string> InAvailableInstance,
                                            std::span<const std::string> InAvailableDevice,
                                            std::vector<std::string>* OutMissing = nullptr) const
    {
        GpuExtensionBroker ScopedResult;
        for (const std::string& ScopedName : MInstance)
        {
            if (Contains(InAvailableInstance, ScopedName))
            {
                ScopedResult.AddInstance(ScopedName);
            }
            else if (OutMissing != nullptr)
            {
                OutMissing->push_back(ScopedName);
            }
        }
        for (const std::string& ScopedName : MDevice)
        {
            if (Contains(InAvailableDevice, ScopedName))
            {
                ScopedResult.AddDevice(ScopedName);
            }
            else if (OutMissing != nullptr)
            {
                OutMissing->push_back(ScopedName);
            }
        }
        return ScopedResult;
    }

private:
    static void AddUnique(std::vector<std::string>& InList, std::string_view InName)
    {
        if (InName.empty())
        {
            return;
        }
        if (std::find(InList.begin(), InList.end(), InName) == InList.end())
        {
            InList.emplace_back(InName);
        }
    }
    [[nodiscard]] static bool Contains(std::span<const std::string> InList, std::string_view InName)
    {
        return std::find(InList.begin(), InList.end(), InName) != InList.end();
    }

    std::vector<std::string> MInstance;
    std::vector<std::string> MDevice;
};

}
