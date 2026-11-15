// FemboyEngine2D - NGX/DLSS Vulkan extension discovery.
#include "Engine/RHI/DlssExtensions.hpp"

#include "Engine/Core/Log.hpp"

#if defined(FBY_WITH_DLSS) && defined(_WIN32)
#include <vulkan/vulkan.h>

#include <nvsdk_ngx.h>
#include <nvsdk_ngx_vk.h>
#include <windows.h>

#include <string>
#endif

namespace Fby::Rhi
{
#if defined(FBY_WITH_DLSS) && defined(_WIN32)
namespace
{
using InstanceRequirementsFn = NVSDK_NGX_Result(NVSDK_CONV*)(
    const NVSDK_NGX_FeatureDiscoveryInfo*, std::uint32_t*, VkExtensionProperties**);
using DeviceRequirementsFn = NVSDK_NGX_Result(NVSDK_CONV*)(
    VkInstance, VkPhysicalDevice, const NVSDK_NGX_FeatureDiscoveryInfo*, std::uint32_t*, VkExtensionProperties**);

struct NgxExtensionApi
{
    HMODULE Module = nullptr;
    InstanceRequirementsFn Instance = nullptr;
    DeviceRequirementsFn Device = nullptr;
};

[[nodiscard]] NgxExtensionApi& NgxApi()
{
    static NgxExtensionApi ScopedApi;
    return ScopedApi;
}

[[nodiscard]] bool LoadNgx()
{
    NgxExtensionApi& ScopedApi = NgxApi();
    if (ScopedApi.Module != nullptr)
    {
        return ScopedApi.Instance != nullptr || ScopedApi.Device != nullptr;
    }
    wchar_t ScopedPath[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, ScopedPath, MAX_PATH) == 0)
    {
        return false;
    }
    wchar_t* ScopedSlash = wcsrchr(ScopedPath, L'\\');
    if (ScopedSlash == nullptr)
    {
        return false;
    }
    *(ScopedSlash + 1) = L'\0';
    const std::wstring ScopedBase(ScopedPath);
    ScopedApi.Module = LoadLibraryW((ScopedBase + L"nvngx_dlss.dll").c_str());
    if (ScopedApi.Module == nullptr)
    {
        ScopedApi.Module = LoadLibraryW((ScopedBase + L"nvngx.dll").c_str());
    }
    if (ScopedApi.Module == nullptr)
    {
        return false;
    }
    ScopedApi.Instance = reinterpret_cast<InstanceRequirementsFn>(
        GetProcAddress(ScopedApi.Module, "NVSDK_NGX_VULKAN_GetFeatureInstanceExtensionRequirements"));
    ScopedApi.Device = reinterpret_cast<DeviceRequirementsFn>(
        GetProcAddress(ScopedApi.Module, "NVSDK_NGX_VULKAN_GetFeatureDeviceExtensionRequirements"));
    return ScopedApi.Instance != nullptr || ScopedApi.Device != nullptr;
}

void FillDiscoveryInfo(NVSDK_NGX_FeatureDiscoveryInfo& OutInfo)
{
    OutInfo.SDKVersion = NVSDK_NGX_Version_API;
    OutInfo.FeatureID = NVSDK_NGX_Feature_SuperSampling;
    OutInfo.Identifier.IdentifierType = NVSDK_NGX_Application_Identifier_Type_Project_Id;
    OutInfo.Identifier.v.ProjectDesc.ProjectId = "femboyengine";
    OutInfo.Identifier.v.ProjectDesc.EngineType = NVSDK_NGX_ENGINE_TYPE_CUSTOM;
    OutInfo.Identifier.v.ProjectDesc.EngineVersion = "1.0";
    OutInfo.ApplicationDataPath = L"./";
    OutInfo.FeatureInfo = nullptr;
}

[[nodiscard]] std::vector<std::string> ReadProperties(std::uint32_t InCount, VkExtensionProperties* InProperties)
{
    std::vector<std::string> ScopedNames;
    if (InProperties == nullptr || InCount == 0)
    {
        return ScopedNames;
    }
    ScopedNames.reserve(InCount);
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < InCount; ++ScopedIndex)
    {
        ScopedNames.emplace_back(InProperties[ScopedIndex].extensionName);
    }
    return ScopedNames;
}
}
#endif

bool DlssRuntimeFilePresent()
{
#if defined(FBY_WITH_DLSS) && defined(_WIN32)
    return LoadNgx();
#else
    return false;
#endif
}

std::vector<std::string> DlssRequiredInstanceExtensions()
{
#if defined(FBY_WITH_DLSS) && defined(_WIN32)
    if (!LoadNgx() || NgxApi().Instance == nullptr)
    {
        return {};
    }
    NVSDK_NGX_FeatureDiscoveryInfo ScopedInfo{};
    FillDiscoveryInfo(ScopedInfo);
    std::uint32_t ScopedCount = 0;
    VkExtensionProperties* ScopedProperties = nullptr;
    const NVSDK_NGX_Result ScopedResult =
        NgxApi().Instance(&ScopedInfo, &ScopedCount, &ScopedProperties);
    if (NVSDK_NGX_FAILED(ScopedResult))
    {
        LogWarn("dlss", "NGX instance extension query failed: {}", static_cast<int>(ScopedResult));
        return {};
    }
    return ReadProperties(ScopedCount, ScopedProperties);
#else
    return {};
#endif
}

std::vector<std::string> DlssRequiredDeviceExtensions(void* InInstance, void* InPhysicalDevice)
{
#if defined(FBY_WITH_DLSS) && defined(_WIN32)
    if (!LoadNgx() || NgxApi().Device == nullptr || InInstance == nullptr || InPhysicalDevice == nullptr)
    {
        return {};
    }
    NVSDK_NGX_FeatureDiscoveryInfo ScopedInfo{};
    FillDiscoveryInfo(ScopedInfo);
    std::uint32_t ScopedCount = 0;
    VkExtensionProperties* ScopedProperties = nullptr;
    const NVSDK_NGX_Result ScopedResult = NgxApi().Device(
        static_cast<VkInstance>(InInstance), static_cast<VkPhysicalDevice>(InPhysicalDevice), &ScopedInfo,
        &ScopedCount, &ScopedProperties);
    if (NVSDK_NGX_FAILED(ScopedResult))
    {
        LogWarn("dlss", "NGX device extension query failed: {}", static_cast<int>(ScopedResult));
        return {};
    }
    return ReadProperties(ScopedCount, ScopedProperties);
#else
    (void)InInstance;
    (void)InPhysicalDevice;
    return {};
#endif
}
}
