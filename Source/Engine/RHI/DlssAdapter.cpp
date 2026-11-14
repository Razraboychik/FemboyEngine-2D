// FemboyEngine2D - optional DLSS Super Resolution adapter.
#include "Engine/RHI/DlssAdapter.hpp"

#include "Engine/Core/Log.hpp"

#if defined(FBY_WITH_DLSS)
#include <vulkan/vulkan.h>
#include <nvsdk_ngx.h>
#include <nvsdk_ngx_vk.h>
#include <nvsdk_ngx_defs_dlssg.h>

#include <windows.h>
#include <string>

namespace
{
struct DlssNgxApi
{
    HMODULE Module = nullptr;
    decltype(&NVSDK_NGX_VULKAN_Init) Init = nullptr;
    decltype(&NVSDK_NGX_VULKAN_Init_with_ProjectID) InitProject = nullptr;
    decltype(&NVSDK_NGX_VULKAN_Shutdown1) Shutdown = nullptr;
    decltype(&NVSDK_NGX_VULKAN_GetCapabilityParameters) GetCapabilityParameters = nullptr;
    decltype(&NVSDK_NGX_VULKAN_CreateFeature1) CreateFeature = nullptr;
    decltype(&NVSDK_NGX_VULKAN_EvaluateFeature) EvaluateFeature = nullptr;
    decltype(&NVSDK_NGX_VULKAN_ReleaseFeature) ReleaseFeature = nullptr;

    [[nodiscard]] bool Load()
    {
        if (Module != nullptr)
        {
            return true;
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
        for (const wchar_t* ScopedName : {L"nvngx.dll", L"_nvngx.dll"})
        {
            Module = LoadLibraryW((ScopedBase + ScopedName).c_str());
            if (Module != nullptr)
            {
                break;
            }
        }
        if (Module == nullptr)
        {
            Fby::LogWarn("dlss", "nvngx.dll load failed: {}", static_cast<int>(GetLastError()));
            return false;
        }
        const auto ScopedResolve = [this](const char* InName)
        {
            return GetProcAddress(Module, InName);
        };
        Init = reinterpret_cast<decltype(Init)>(ScopedResolve("NVSDK_NGX_VULKAN_Init"));
        InitProject = reinterpret_cast<decltype(InitProject)>(ScopedResolve("NVSDK_NGX_VULKAN_Init_ProjectID"));
        Shutdown = reinterpret_cast<decltype(Shutdown)>(ScopedResolve("NVSDK_NGX_VULKAN_Shutdown1"));
        if (Shutdown == nullptr)
        {
            Shutdown = reinterpret_cast<decltype(Shutdown)>(ScopedResolve("NVSDK_NGX_VULKAN_Shutdown"));
        }
        GetCapabilityParameters = reinterpret_cast<decltype(GetCapabilityParameters)>(
            ScopedResolve("NVSDK_NGX_VULKAN_GetCapabilityParameters"));
        CreateFeature = reinterpret_cast<decltype(CreateFeature)>(ScopedResolve("NVSDK_NGX_VULKAN_CreateFeature1"));
        EvaluateFeature = reinterpret_cast<decltype(EvaluateFeature)>(
            ScopedResolve("NVSDK_NGX_VULKAN_EvaluateFeature"));
        ReleaseFeature = reinterpret_cast<decltype(ReleaseFeature)>(ScopedResolve("NVSDK_NGX_VULKAN_ReleaseFeature"));
        return Init != nullptr && Shutdown != nullptr && GetCapabilityParameters != nullptr &&
               CreateFeature != nullptr && EvaluateFeature != nullptr && ReleaseFeature != nullptr;
    }

    // Parameter access goes through the NGX C++ interface, not exported
    // free functions: modern nvngx.dll runtimes expose only the vtable.
    static void SetVoidPointer(NVSDK_NGX_Parameter* InParameter, const char* InName, void* InValue)
    {
        InParameter->Set(InName, InValue);
    }
    static void SetUI(NVSDK_NGX_Parameter* InParameter, const char* InName, unsigned int InValue)
    {
        InParameter->Set(InName, InValue);
    }
    static void SetF(NVSDK_NGX_Parameter* InParameter, const char* InName, float InValue)
    {
        InParameter->Set(InName, InValue);
    }
    static NVSDK_NGX_Result GetI(NVSDK_NGX_Parameter* InParameter, const char* InName, int* OutValue)
    {
        return InParameter->Get(InName, OutValue);
    }
};

DlssNgxApi GDlssNgx;
}
#endif

namespace Fby::Rhi
{

bool DlssAdapter::Initialize(RHI& InGpu)
{
#if defined(FBY_WITH_DLSS)
    if (MInitialized)
    {
        return MAvailable;
    }
    RHI::NativeDeviceHandles ScopedHandles;
    if (!InGpu.GetNativeDeviceHandles(ScopedHandles) || ScopedHandles.Instance == nullptr ||
        ScopedHandles.PhysicalDevice == nullptr || ScopedHandles.Device == nullptr)
    {
        return false;
    }
    if (!GDlssNgx.Load())
    {
        Fby::LogWarn("dlss", "nvngx runtime is not loadable");
        return false;
    }
    const NVSDK_NGX_Result ScopedInit =
        GDlssNgx.InitProject != nullptr
            ? GDlssNgx.InitProject(
                  "femboyengine", NVSDK_NGX_ENGINE_TYPE_CUSTOM, "1.0", L"./",
                  static_cast<VkInstance>(ScopedHandles.Instance),
                  static_cast<VkPhysicalDevice>(ScopedHandles.PhysicalDevice),
                  static_cast<VkDevice>(ScopedHandles.Device), nullptr, nullptr, nullptr,
                  NVSDK_NGX_Version_API)
            : GDlssNgx.Init(0, L"./", static_cast<VkInstance>(ScopedHandles.Instance),
                            static_cast<VkPhysicalDevice>(ScopedHandles.PhysicalDevice),
                            static_cast<VkDevice>(ScopedHandles.Device), nullptr, nullptr, nullptr,
                            NVSDK_NGX_Version_API);
    if (NVSDK_NGX_FAILED(ScopedInit))
    {
        Fby::LogWarn("dlss", "NVSDK_NGX_VULKAN_Init failed: {}", static_cast<int>(ScopedInit));
        return false;
    }
    MInitialized = true;
    MDevice = ScopedHandles.Device;
    NVSDK_NGX_Parameter* ScopedParameters = nullptr;
    if (NVSDK_NGX_FAILED(GDlssNgx.GetCapabilityParameters(&ScopedParameters)) ||
        ScopedParameters == nullptr)
    {
        return false;
    }
    MParameters = ScopedParameters;
    int ScopedAvailable = 0;
    MAvailable = !NVSDK_NGX_FAILED(GDlssNgx.GetI(
                     ScopedParameters, NVSDK_NGX_Parameter_SuperSampling_Available, &ScopedAvailable)) &&
                 ScopedAvailable != 0;
    int ScopedFgAvailable = 0;
    MFgAvailable = !NVSDK_NGX_FAILED(GDlssNgx.GetI(
                       ScopedParameters, NVSDK_NGX_Parameter_FrameGeneration_Available, &ScopedFgAvailable)) &&
                   ScopedFgAvailable != 0;
    return MAvailable;
#else
    (void)InGpu;
    return false;
#endif
}

bool DlssAdapter::CreateFeature(void* InCommandBuffer, void* InColor, void* InDepth, void* InMotion,
                                void* InOutput, std::uint32_t InWidth, std::uint32_t InHeight,
                                DlssQualityMode InQuality)
{
#if defined(FBY_WITH_DLSS)
    if (!MAvailable || MParameters == nullptr || InCommandBuffer == nullptr || InColor == nullptr ||
        InDepth == nullptr || InMotion == nullptr || InOutput == nullptr)
    {
        return false;
    }
    if (MFeature != nullptr)
    {
        GDlssNgx.ReleaseFeature(static_cast<NVSDK_NGX_Handle*>(MFeature));
        MFeature = nullptr;
    }
    auto* ScopedParameters = static_cast<NVSDK_NGX_Parameter*>(MParameters);
    GDlssNgx.SetVoidPointer(ScopedParameters, NVSDK_NGX_Parameter_Color, InColor);
    GDlssNgx.SetVoidPointer(ScopedParameters, NVSDK_NGX_Parameter_Depth, InDepth);
    GDlssNgx.SetVoidPointer(ScopedParameters, NVSDK_NGX_Parameter_MotionVectors, InMotion);
    GDlssNgx.SetVoidPointer(ScopedParameters, NVSDK_NGX_Parameter_Output, InOutput);
    GDlssNgx.SetUI(ScopedParameters, NVSDK_NGX_Parameter_Width, InWidth);
    GDlssNgx.SetUI(ScopedParameters, NVSDK_NGX_Parameter_Height, InHeight);
    GDlssNgx.SetF(ScopedParameters, NVSDK_NGX_Parameter_SuperSampling_ScaleFactor, DlssRenderScale(InQuality));
    NVSDK_NGX_Handle* ScopedFeature = nullptr;
    const NVSDK_NGX_Result ScopedResult = GDlssNgx.CreateFeature(
        static_cast<VkDevice>(MDevice), static_cast<VkCommandBuffer>(InCommandBuffer),
        NVSDK_NGX_Feature_SuperSampling, ScopedParameters, &ScopedFeature);
    if (NVSDK_NGX_FAILED(ScopedResult) || ScopedFeature == nullptr)
    {
        return false;
    }
    int ScopedFeatureInit = 0;
    if (!NVSDK_NGX_FAILED(GDlssNgx.GetI(ScopedParameters, NVSDK_NGX_Parameter_SuperSampling_FeatureInitResult,
                                        &ScopedFeatureInit)) &&
        ScopedFeatureInit == 0)
    {
        GDlssNgx.ReleaseFeature(ScopedFeature);
        return false;
    }
    MFeature = ScopedFeature;
    return true;
#else
    (void)InCommandBuffer;
    (void)InColor;
    (void)InDepth;
    (void)InMotion;
    (void)InOutput;
    (void)InWidth;
    (void)InHeight;
    (void)InQuality;
    return false;
#endif
}

void DlssAdapter::SetJitter(float InX, float InY) noexcept
{
#if defined(FBY_WITH_DLSS)
    if (MParameters != nullptr)
    {
        auto* ScopedParameters = static_cast<NVSDK_NGX_Parameter*>(MParameters);
        GDlssNgx.SetF(ScopedParameters, NVSDK_NGX_Parameter_Jitter_Offset_X, InX);
        GDlssNgx.SetF(ScopedParameters, NVSDK_NGX_Parameter_Jitter_Offset_Y, InY);
    }
#else
    (void)InX;
    (void)InY;
#endif
}

bool DlssAdapter::CreateFrameGeneration(void* InCommandBuffer, void* InBackbuffer, void* InDepth, void* InMotion,
                                        void* InHudless, void* InUi, void* InOutput, std::uint32_t InWidth,
                                        std::uint32_t InHeight)
{
#if defined(FBY_WITH_DLSS)
    if (!MAvailable || !MFgAvailable || MParameters == nullptr || InCommandBuffer == nullptr ||
        InBackbuffer == nullptr ||
        InDepth == nullptr || InMotion == nullptr || InOutput == nullptr)
    {
        return false;
    }
    if (MFgFeature != nullptr)
    {
        GDlssNgx.ReleaseFeature(static_cast<NVSDK_NGX_Handle*>(MFgFeature));
        MFgFeature = nullptr;
    }
    auto* ScopedParameters = static_cast<NVSDK_NGX_Parameter*>(MParameters);
    GDlssNgx.SetVoidPointer(ScopedParameters, NVSDK_NGX_DLSSG_Parameter_Backbuffer, InBackbuffer);
    GDlssNgx.SetVoidPointer(ScopedParameters, NVSDK_NGX_DLSSG_Parameter_Depth, InDepth);
    GDlssNgx.SetVoidPointer(ScopedParameters, NVSDK_NGX_DLSSG_Parameter_MVecs, InMotion);
    GDlssNgx.SetVoidPointer(ScopedParameters, NVSDK_NGX_DLSSG_Parameter_OutputInterpolated, InOutput);
    if (InHudless != nullptr)
    {
        GDlssNgx.SetVoidPointer(ScopedParameters, NVSDK_NGX_DLSSG_Parameter_HUDLess, InHudless);
    }
    if (InUi != nullptr)
    {
        GDlssNgx.SetVoidPointer(ScopedParameters, NVSDK_NGX_DLSSG_Parameter_UI, InUi);
    }
    GDlssNgx.SetUI(ScopedParameters, NVSDK_NGX_Parameter_Width, InWidth);
    GDlssNgx.SetUI(ScopedParameters, NVSDK_NGX_Parameter_Height, InHeight);
    NVSDK_NGX_Handle* ScopedFeature = nullptr;
    const NVSDK_NGX_Result ScopedResult = GDlssNgx.CreateFeature(
        static_cast<VkDevice>(MDevice), static_cast<VkCommandBuffer>(InCommandBuffer),
        NVSDK_NGX_Feature_FrameGeneration, ScopedParameters, &ScopedFeature);
    if (NVSDK_NGX_FAILED(ScopedResult) || ScopedFeature == nullptr)
    {
        return false;
    }
    MFgFeature = ScopedFeature;
    return true;
#else
    (void)InCommandBuffer;
    (void)InBackbuffer;
    (void)InDepth;
    (void)InMotion;
    (void)InHudless;
    (void)InUi;
    (void)InOutput;
    (void)InWidth;
    (void)InHeight;
    return false;
#endif
}

bool DlssAdapter::EvaluateFrameGeneration(void* InCommandBuffer)
{
#if defined(FBY_WITH_DLSS)
    if (MFgFeature == nullptr || InCommandBuffer == nullptr || MParameters == nullptr)
    {
        return false;
    }
    const NVSDK_NGX_Result ScopedResult = GDlssNgx.EvaluateFeature(
        static_cast<VkCommandBuffer>(InCommandBuffer), static_cast<NVSDK_NGX_Handle*>(MFgFeature),
        static_cast<NVSDK_NGX_Parameter*>(MParameters), nullptr);
    return !NVSDK_NGX_FAILED(ScopedResult);
#else
    (void)InCommandBuffer;
    return false;
#endif
}

bool DlssAdapter::Evaluate(void* InCommandBuffer)
{
#if defined(FBY_WITH_DLSS)
    if (MFeature == nullptr || InCommandBuffer == nullptr || MParameters == nullptr)
    {
        return false;
    }
    const NVSDK_NGX_Result ScopedResult = GDlssNgx.EvaluateFeature(
        static_cast<VkCommandBuffer>(InCommandBuffer), static_cast<NVSDK_NGX_Handle*>(MFeature),
        static_cast<NVSDK_NGX_Parameter*>(MParameters), nullptr);
    return !NVSDK_NGX_FAILED(ScopedResult);
#else
    (void)InCommandBuffer;
    return false;
#endif
}

void DlssAdapter::Shutdown(RHI& InGpu) noexcept
{
    (void)InGpu;
#if defined(FBY_WITH_DLSS)
    if (MFgFeature != nullptr)
    {
        GDlssNgx.ReleaseFeature(static_cast<NVSDK_NGX_Handle*>(MFgFeature));
    }
    if (MFeature != nullptr)
    {
        GDlssNgx.ReleaseFeature(static_cast<NVSDK_NGX_Handle*>(MFeature));
    }
    if (MInitialized)
    {
        GDlssNgx.Shutdown(nullptr);
    }
#endif
    MInitialized = false;
    MAvailable = false;
    MFgAvailable = false;
    MParameters = nullptr;
    MFeature = nullptr;
    MDevice = nullptr;
}

}
