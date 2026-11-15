// FemboyEngine2D - Windows UI Automation provider for the accessibility tree.
#include "Engine/UI/AccessibilityBridge.hpp"

#if defined(_WIN32)

#include "Engine/Core/Log.hpp"

#include <SDL3/SDL.h>

#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <oleauto.h>
#include <UIAutomationClient.h>
#include <uiautomationcore.h>
#include <uiautomationcoreapi.h>

#include <cstdio>
#include <cstring>

namespace Fby
{
namespace
{

HWND MHwnd = nullptr;
WNDPROC MPreviousProc = nullptr;
Ui::AccessibilityTree* MTree = nullptr;
AccessibilityBridgeSettings MSettings{};
bool MActive = false;
IRawElementProviderSimple* MRoot = nullptr;

[[nodiscard]] std::uint32_t NodeIndexOf(std::uint32_t InId) noexcept
{
    if (MTree == nullptr || InId == 0)
    {
        return Ui::kNoAccessibilityNode;
    }
    return MTree->FindIndexById(InId);
}

[[nodiscard]] RECT ScreenRectOf(const Vector4D& InBounds) noexcept
{
    POINT ScopedMin{static_cast<LONG>(InBounds.X), static_cast<LONG>(InBounds.Y)};
    POINT ScopedMax{static_cast<LONG>(InBounds.Z), static_cast<LONG>(InBounds.W)};
    if (MHwnd != nullptr)
    {
        ClientToScreen(MHwnd, &ScopedMin);
        ClientToScreen(MHwnd, &ScopedMax);
    }
    RECT ScopedRect{ScopedMin.x, ScopedMin.y, ScopedMax.x, ScopedMax.y};
    return ScopedRect;
}

[[nodiscard]] int ControlTypeOf(Ui::AccessibilityRole InRole) noexcept
{
    switch (InRole)
    {
        case Ui::AccessibilityRole::Button:
            return UIA_ButtonControlTypeId;
        case Ui::AccessibilityRole::CheckBox:
            return UIA_CheckBoxControlTypeId;
        case Ui::AccessibilityRole::RadioButton:
            return UIA_RadioButtonControlTypeId;
        case Ui::AccessibilityRole::ToggleButton:
            return UIA_ButtonControlTypeId;
        case Ui::AccessibilityRole::Slider:
            return UIA_SliderControlTypeId;
        case Ui::AccessibilityRole::ScrollBar:
            return UIA_ScrollBarControlTypeId;
        case Ui::AccessibilityRole::ProgressBar:
            return UIA_ProgressBarControlTypeId;
        case Ui::AccessibilityRole::TextInput:
            return UIA_EditControlTypeId;
        case Ui::AccessibilityRole::Text:
            return UIA_TextControlTypeId;
        case Ui::AccessibilityRole::List:
            return UIA_ListControlTypeId;
        case Ui::AccessibilityRole::ListItem:
            return UIA_ListItemControlTypeId;
        case Ui::AccessibilityRole::Tree:
            return UIA_TreeControlTypeId;
        case Ui::AccessibilityRole::TreeItem:
            return UIA_TreeItemControlTypeId;
        case Ui::AccessibilityRole::Tab:
            return UIA_TabControlTypeId;
        case Ui::AccessibilityRole::Menu:
            return UIA_MenuControlTypeId;
        case Ui::AccessibilityRole::MenuItem:
            return UIA_MenuItemControlTypeId;
        case Ui::AccessibilityRole::ComboBox:
            return UIA_ComboBoxControlTypeId;
        case Ui::AccessibilityRole::Separator:
            return UIA_SeparatorControlTypeId;
        case Ui::AccessibilityRole::Image:
            return UIA_ImageControlTypeId;
        case Ui::AccessibilityRole::Dialog:
        case Ui::AccessibilityRole::Window:
            return UIA_WindowControlTypeId;
        case Ui::AccessibilityRole::Pane:
            return UIA_PaneControlTypeId;
        case Ui::AccessibilityRole::Group:
            return UIA_GroupControlTypeId;
        case Ui::AccessibilityRole::Application:
            return UIA_PaneControlTypeId;
        case Ui::AccessibilityRole::Unknown:
        case Ui::AccessibilityRole::Count:
            break;
    }
    return UIA_CustomControlTypeId;
}

[[nodiscard]] bool IsInvokable(Ui::AccessibilityRole InRole) noexcept
{
    switch (InRole)
    {
        case Ui::AccessibilityRole::Button:
        case Ui::AccessibilityRole::MenuItem:
        case Ui::AccessibilityRole::ListItem:
        case Ui::AccessibilityRole::TreeItem:
        case Ui::AccessibilityRole::Tab:
            return true;
        default:
            return false;
    }
}

[[nodiscard]] bool IsTogglable(Ui::AccessibilityRole InRole) noexcept
{
    return InRole == Ui::AccessibilityRole::CheckBox || InRole == Ui::AccessibilityRole::RadioButton ||
           InRole == Ui::AccessibilityRole::ToggleButton;
}

[[nodiscard]] bool IsKeyboardFocusable(Ui::AccessibilityRole InRole) noexcept
{
    return IsInvokable(InRole) || IsTogglable(InRole) || InRole == Ui::AccessibilityRole::Slider ||
           InRole == Ui::AccessibilityRole::TextInput || InRole == Ui::AccessibilityRole::ComboBox;
}

void SetBstr(VARIANT* Out, const char* InText) noexcept
{
    Out->vt = VT_BSTR;
    const int ScopedWide = MultiByteToWideChar(CP_UTF8, 0, InText, -1, nullptr, 0);
    if (ScopedWide <= 0)
    {
        Out->bstrVal = SysAllocString(L"");
        return;
    }
    wchar_t* ScopedBuffer = new wchar_t[static_cast<std::size_t>(ScopedWide)];
    MultiByteToWideChar(CP_UTF8, 0, InText, -1, ScopedBuffer, ScopedWide);
    Out->bstrVal = SysAllocString(ScopedBuffer);
    delete[] ScopedBuffer;
}

void SetRectArray(VARIANT* Out, const RECT& InRect) noexcept
{
    SAFEARRAY* ScopedArray = SafeArrayCreateVector(VT_R8, 0, 4);
    if (ScopedArray == nullptr)
    {
        return;
    }
    double* ScopedData = nullptr;
    SafeArrayAccessData(ScopedArray, reinterpret_cast<void**>(&ScopedData));
    ScopedData[0] = static_cast<double>(InRect.left);
    ScopedData[1] = static_cast<double>(InRect.top);
    ScopedData[2] = static_cast<double>(InRect.right - InRect.left);
    ScopedData[3] = static_cast<double>(InRect.bottom - InRect.top);
    SafeArrayUnaccessData(ScopedArray);
    Out->vt = VT_ARRAY | VT_R8;
    Out->parray = ScopedArray;
}

[[nodiscard]] HRESULT MakeRuntimeId(std::uint32_t InId, SAFEARRAY** OutArray) noexcept
{
    SAFEARRAY* ScopedArray = SafeArrayCreateVector(VT_I4, 0, 2);
    if (ScopedArray == nullptr)
    {
        return E_OUTOFMEMORY;
    }
    int* ScopedData = nullptr;
    SafeArrayAccessData(ScopedArray, reinterpret_cast<void**>(&ScopedData));
    ScopedData[0] = UiaAppendRuntimeId;
    ScopedData[1] = static_cast<int>(InId);
    SafeArrayUnaccessData(ScopedArray);
    *OutArray = ScopedArray;
    return S_OK;
}

HRESULT NodePropertyValue(std::uint32_t InId, PROPERTYID InProperty, VARIANT* Out) noexcept
{
    const std::uint32_t ScopedIndex = NodeIndexOf(InId);
    if (ScopedIndex == Ui::kNoAccessibilityNode || MTree == nullptr)
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    const Ui::AccessibilityNodeView ScopedNode = MTree->At(ScopedIndex);
    VariantInit(Out);
    switch (InProperty)
    {
        case UIA_NamePropertyId:
        {
            char ScopedName[256]{};
            (void)MTree->NameOf(ScopedNode, ScopedName, sizeof(ScopedName));
            SetBstr(Out, ScopedName);
            return S_OK;
        }
        case UIA_ControlTypePropertyId:
            Out->vt = VT_I4;
            Out->lVal = ControlTypeOf(ScopedNode.Role);
            return S_OK;
        case UIA_BoundingRectanglePropertyId:
            SetRectArray(Out, ScreenRectOf(ScopedNode.Bounds));
            return S_OK;
        case UIA_IsEnabledPropertyId:
            Out->vt = VT_BOOL;
            Out->boolVal = (ScopedNode.States & Ui::AccessibilityStateEnabled) != 0 ? VARIANT_TRUE : VARIANT_FALSE;
            return S_OK;
        case UIA_IsOffscreenPropertyId:
            Out->vt = VT_BOOL;
            Out->boolVal = (ScopedNode.States & Ui::AccessibilityStateOffscreen) != 0 ? VARIANT_TRUE : VARIANT_FALSE;
            return S_OK;
        case UIA_HasKeyboardFocusPropertyId:
            Out->vt = VT_BOOL;
            Out->boolVal = (ScopedNode.States & Ui::AccessibilityStateFocused) != 0 ? VARIANT_TRUE : VARIANT_FALSE;
            return S_OK;
        case UIA_IsKeyboardFocusablePropertyId:
            Out->vt = VT_BOOL;
            Out->boolVal = IsKeyboardFocusable(ScopedNode.Role) ? VARIANT_TRUE : VARIANT_FALSE;
            return S_OK;
        case UIA_IsControlElementPropertyId:
        case UIA_IsContentElementPropertyId:
            Out->vt = VT_BOOL;
            Out->boolVal = VARIANT_TRUE;
            return S_OK;
        case UIA_AutomationIdPropertyId:
        {
            char ScopedId[16]{};
            std::snprintf(ScopedId, sizeof(ScopedId), "%u", static_cast<unsigned>(InId));
            SetBstr(Out, ScopedId);
            return S_OK;
        }
        case UIA_FrameworkIdPropertyId:
            SetBstr(Out, "FemboyEngine2D");
            return S_OK;
        case UIA_ToggleToggleStatePropertyId:
        {
            if (!IsTogglable(ScopedNode.Role))
            {
                return S_FALSE;
            }
            Out->vt = VT_I4;
            Out->lVal = (ScopedNode.States & Ui::AccessibilityStateChecked) != 0 ? ToggleState_On : ToggleState_Off;
            return S_OK;
        }
        default:
            break;
    }
    return S_FALSE;
}

class NodeProvider;

[[nodiscard]] NodeProvider* CreateNodeProvider(std::uint32_t InId) noexcept;

class NodeProvider final : public IRawElementProviderFragment,
                           public IRawElementProviderSimple,
                           public IInvokeProvider,
                           public IToggleProvider
{
public:
    explicit NodeProvider(std::uint32_t InId) noexcept : MId(InId)
    {
    }

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID InIid, void** Out) override
    {
        if (Out == nullptr)
        {
            return E_INVALIDARG;
        }
        *Out = nullptr;
        // Every interface must hand back its own subobject: UI Automation calls
        // GetPropertyValue through the simple pointer and Navigate through the
        // fragment pointer, and the two vtables are not interchangeable.
        if (InIid == __uuidof(IRawElementProviderSimple) || InIid == __uuidof(IUnknown))
        {
            *Out = static_cast<IRawElementProviderSimple*>(this);
            AddRef();
            return S_OK;
        }
        if (InIid == __uuidof(IRawElementProviderFragment))
        {
            *Out = static_cast<IRawElementProviderFragment*>(this);
            AddRef();
            return S_OK;
        }
        if (InIid == __uuidof(IInvokeProvider))
        {
            *Out = static_cast<IInvokeProvider*>(this);
            AddRef();
            return S_OK;
        }
        if (InIid == __uuidof(IToggleProvider))
        {
            *Out = static_cast<IToggleProvider*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return static_cast<ULONG>(InterlockedIncrement(&MRefs));
    }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const LONG ScopedRefs = InterlockedDecrement(&MRefs);
        if (ScopedRefs == 0)
        {
            delete this;
        }
        return static_cast<ULONG>(ScopedRefs);
    }

    // IRawElementProviderSimple
    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* OutOptions) override
    {
        if (OutOptions == nullptr)
        {
            return E_INVALIDARG;
        }
        *OutOptions = static_cast<ProviderOptions>(ProviderOptions_ServerSideProvider);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID InPattern, IUnknown** Out) override
    {
        if (Out == nullptr)
        {
            return E_INVALIDARG;
        }
        *Out = nullptr;
        const std::uint32_t ScopedIndex = NodeIndexOf(MId);
        if (ScopedIndex == Ui::kNoAccessibilityNode || MTree == nullptr)
        {
            return UIA_E_ELEMENTNOTAVAILABLE;
        }
        const Ui::AccessibilityRole ScopedRole = MTree->At(ScopedIndex).Role;
        if (InPattern == UIA_InvokePatternId && IsInvokable(ScopedRole))
        {
            return QueryInterface(__uuidof(IInvokeProvider), reinterpret_cast<void**>(Out));
        }
        if (InPattern == UIA_TogglePatternId && IsTogglable(ScopedRole))
        {
            return QueryInterface(__uuidof(IToggleProvider), reinterpret_cast<void**>(Out));
        }
        return S_FALSE;
    }

    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID InProperty, VARIANT* Out) override
    {
        if (Out == nullptr)
        {
            return E_INVALIDARG;
        }
        return NodePropertyValue(MId, InProperty, Out);
    }

    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** Out) override
    {
        if (Out == nullptr)
        {
            return E_INVALIDARG;
        }
        return UiaHostProviderFromHwnd(MHwnd, Out);
    }

    // IRawElementProviderFragment
    HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection InDirection, IRawElementProviderFragment** Out) override
    {
        if (Out == nullptr)
        {
            return E_INVALIDARG;
        }
        *Out = nullptr;
        const std::uint32_t ScopedIndex = NodeIndexOf(MId);
        if (ScopedIndex == Ui::kNoAccessibilityNode || MTree == nullptr)
        {
            return UIA_E_ELEMENTNOTAVAILABLE;
        }
        const Ui::AccessibilityNodeView ScopedNode = MTree->At(ScopedIndex);
        switch (InDirection)
        {
            case NavigateDirection_Parent:
            {
                if (ScopedNode.ParentIndex != Ui::kNoAccessibilityNode)
                {
                    *Out = CreateNodeProvider(MTree->At(ScopedNode.ParentIndex).Id);
                }
                else if (MRoot != nullptr)
                {
                    return MRoot->QueryInterface(__uuidof(IRawElementProviderFragment), reinterpret_cast<void**>(Out));
                }
                break;
            }
            case NavigateDirection_FirstChild:
            {
                const std::uint32_t ScopedChild = MTree->FirstChildOf(ScopedIndex);
                if (ScopedChild != Ui::kNoAccessibilityNode)
                {
                    *Out = CreateNodeProvider(MTree->At(ScopedChild).Id);
                }
                break;
            }
            case NavigateDirection_LastChild:
            {
                std::uint32_t ScopedChild = MTree->FirstChildOf(ScopedIndex);
                std::uint32_t ScopedNext = ScopedChild;
                while (ScopedNext != Ui::kNoAccessibilityNode)
                {
                    ScopedChild = ScopedNext;
                    ScopedNext = MTree->NextSiblingOf(ScopedNext);
                }
                if (ScopedChild != Ui::kNoAccessibilityNode)
                {
                    *Out = CreateNodeProvider(MTree->At(ScopedChild).Id);
                }
                break;
            }
            case NavigateDirection_NextSibling:
            {
                const std::uint32_t ScopedSibling = MTree->NextSiblingOf(ScopedIndex);
                if (ScopedSibling != Ui::kNoAccessibilityNode)
                {
                    *Out = CreateNodeProvider(MTree->At(ScopedSibling).Id);
                }
                break;
            }
            case NavigateDirection_PreviousSibling:
            {
                const std::uint32_t ScopedParent = ScopedNode.ParentIndex;
                std::uint32_t ScopedPrevious = Ui::kNoAccessibilityNode;
                const std::uint32_t ScopedFirst =
                    ScopedParent == Ui::kNoAccessibilityNode ? 0u : MTree->FirstChildOf(ScopedParent);
                std::uint32_t ScopedCurrent = ScopedFirst;
                while (ScopedCurrent != Ui::kNoAccessibilityNode && ScopedCurrent != ScopedIndex)
                {
                    ScopedPrevious = ScopedCurrent;
                    ScopedCurrent = MTree->NextSiblingOf(ScopedCurrent);
                }
                if (ScopedPrevious != Ui::kNoAccessibilityNode)
                {
                    *Out = CreateNodeProvider(MTree->At(ScopedPrevious).Id);
                }
                break;
            }
            default:
                break;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** OutArray) override
    {
        if (OutArray == nullptr)
        {
            return E_INVALIDARG;
        }
        return MakeRuntimeId(MId, OutArray);
    }

    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* OutRect) override
    {
        if (OutRect == nullptr)
        {
            return E_INVALIDARG;
        }
        const std::uint32_t ScopedIndex = NodeIndexOf(MId);
        if (ScopedIndex == Ui::kNoAccessibilityNode || MTree == nullptr)
        {
            return UIA_E_ELEMENTNOTAVAILABLE;
        }
        const RECT ScopedRect = ScreenRectOf(MTree->At(ScopedIndex).Bounds);
        OutRect->left = static_cast<double>(ScopedRect.left);
        OutRect->top = static_cast<double>(ScopedRect.top);
        OutRect->width = static_cast<double>(ScopedRect.right - ScopedRect.left);
        OutRect->height = static_cast<double>(ScopedRect.bottom - ScopedRect.top);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** OutArray) override
    {
        if (OutArray == nullptr)
        {
            return E_INVALIDARG;
        }
        *OutArray = nullptr;
        return S_FALSE;
    }

    HRESULT STDMETHODCALLTYPE SetFocus() override
    {
        return UIA_E_NOTSUPPORTED;
    }

    HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** OutRoot) override
    {
        if (OutRoot == nullptr)
        {
            return E_INVALIDARG;
        }
        *OutRoot = nullptr;
        if (MRoot == nullptr)
        {
            return UIA_E_ELEMENTNOTAVAILABLE;
        }
        return MRoot->QueryInterface(__uuidof(IRawElementProviderFragmentRoot), reinterpret_cast<void**>(OutRoot));
    }

    // IInvokeProvider
    HRESULT STDMETHODCALLTYPE Invoke() override
    {
        if (MTree == nullptr)
        {
            return UIA_E_ELEMENTNOTAVAILABLE;
        }
        return MTree->Activate(MId) ? S_OK : UIA_E_NOTSUPPORTED;
    }

    // IToggleProvider
    HRESULT STDMETHODCALLTYPE Toggle() override
    {
        return Invoke();
    }

    HRESULT STDMETHODCALLTYPE get_ToggleState(ToggleState* OutState) override
    {
        if (OutState == nullptr)
        {
            return E_INVALIDARG;
        }
        const std::uint32_t ScopedIndex = NodeIndexOf(MId);
        if (ScopedIndex == Ui::kNoAccessibilityNode || MTree == nullptr)
        {
            return UIA_E_ELEMENTNOTAVAILABLE;
        }
        *OutState =
            (MTree->At(ScopedIndex).States & Ui::AccessibilityStateChecked) != 0 ? ToggleState_On : ToggleState_Off;
        return S_OK;
    }

private:
    LONG MRefs = 1;
    std::uint32_t MId = 0;
};

class RootProvider final : public IRawElementProviderFragmentRoot,
                           public IRawElementProviderFragment,
                           public IRawElementProviderSimple
{
public:
    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID InIid, void** Out) override
    {
        if (Out == nullptr)
        {
            return E_INVALIDARG;
        }
        *Out = nullptr;
        if (InIid == __uuidof(IRawElementProviderSimple) || InIid == __uuidof(IUnknown))
        {
            *Out = static_cast<IRawElementProviderSimple*>(this);
            AddRef();
            return S_OK;
        }
        if (InIid == __uuidof(IRawElementProviderFragment))
        {
            *Out = static_cast<IRawElementProviderFragment*>(this);
            AddRef();
            return S_OK;
        }
        if (InIid == __uuidof(IRawElementProviderFragmentRoot))
        {
            *Out = static_cast<IRawElementProviderFragmentRoot*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return static_cast<ULONG>(InterlockedIncrement(&MRefs));
    }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const LONG ScopedRefs = InterlockedDecrement(&MRefs);
        if (ScopedRefs == 0)
        {
            delete this;
        }
        return static_cast<ULONG>(ScopedRefs);
    }

    // IRawElementProviderSimple
    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* OutOptions) override
    {
        if (OutOptions == nullptr)
        {
            return E_INVALIDARG;
        }
        *OutOptions = static_cast<ProviderOptions>(ProviderOptions_ServerSideProvider);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID InPattern, IUnknown** Out) override
    {
        (void)InPattern;
        if (Out == nullptr)
        {
            return E_INVALIDARG;
        }
        *Out = nullptr;
        return S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID InProperty, VARIANT* Out) override
    {
        if (Out == nullptr)
        {
            return E_INVALIDARG;
        }
        VariantInit(Out);
        switch (InProperty)
        {
            case UIA_NamePropertyId:
                SetBstr(Out, "FemboyEngine2D");
                return S_OK;
            case UIA_ControlTypePropertyId:
                Out->vt = VT_I4;
                Out->lVal = UIA_WindowControlTypeId;
                return S_OK;
            case UIA_BoundingRectanglePropertyId:
            {
                RECT ScopedRect{0, 0, 0, 0};
                if (MHwnd != nullptr)
                {
                    GetClientRect(MHwnd, &ScopedRect);
                    POINT ScopedOrigin{0, 0};
                    ClientToScreen(MHwnd, &ScopedOrigin);
                    OffsetRect(&ScopedRect, ScopedOrigin.x, ScopedOrigin.y);
                }
                SetRectArray(Out, ScopedRect);
                return S_OK;
            }
            case UIA_FrameworkIdPropertyId:
                SetBstr(Out, "FemboyEngine2D");
                return S_OK;
            case UIA_IsControlElementPropertyId:
            case UIA_IsContentElementPropertyId:
                Out->vt = VT_BOOL;
                Out->boolVal = VARIANT_TRUE;
                return S_OK;
            default:
                break;
        }
        return S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** Out) override
    {
        if (Out == nullptr)
        {
            return E_INVALIDARG;
        }
        return UiaHostProviderFromHwnd(MHwnd, Out);
    }

    // IRawElementProviderFragment
    HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection InDirection, IRawElementProviderFragment** Out) override
    {
        if (Out == nullptr)
        {
            return E_INVALIDARG;
        }
        *Out = nullptr;
        if (MTree == nullptr)
        {
            return S_OK;
        }
        if (InDirection == NavigateDirection_FirstChild || InDirection == NavigateDirection_LastChild)
        {
            std::uint32_t ScopedIndex = Ui::kNoAccessibilityNode;
            std::uint32_t ScopedFallback = Ui::kNoAccessibilityNode;
            const std::uint32_t ScopedCount = MTree->Count();
            for (std::uint32_t ScopedNodeIndex = 0; ScopedNodeIndex < ScopedCount; ++ScopedNodeIndex)
            {
                if (MTree->At(ScopedNodeIndex).ParentIndex != Ui::kNoAccessibilityNode)
                {
                    continue;
                }
                if (ScopedIndex == Ui::kNoAccessibilityNode)
                {
                    ScopedIndex = ScopedNodeIndex;
                }
                ScopedFallback = ScopedNodeIndex;
            }
            const std::uint32_t ScopedChosen =
                InDirection == NavigateDirection_FirstChild ? ScopedIndex : ScopedFallback;
            if (ScopedChosen != Ui::kNoAccessibilityNode)
            {
                *Out = CreateNodeProvider(MTree->At(ScopedChosen).Id);
            }
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** OutArray) override
    {
        if (OutArray == nullptr)
        {
            return E_INVALIDARG;
        }
        return MakeRuntimeId(0, OutArray);
    }
    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* OutRect) override
    {
        if (OutRect == nullptr)
        {
            return E_INVALIDARG;
        }
        RECT ScopedRect{0, 0, 0, 0};
        if (MHwnd != nullptr)
        {
            GetClientRect(MHwnd, &ScopedRect);
            POINT ScopedOrigin{0, 0};
            ClientToScreen(MHwnd, &ScopedOrigin);
            OffsetRect(&ScopedRect, ScopedOrigin.x, ScopedOrigin.y);
        }
        OutRect->left = static_cast<double>(ScopedRect.left);
        OutRect->top = static_cast<double>(ScopedRect.top);
        OutRect->width = static_cast<double>(ScopedRect.right - ScopedRect.left);
        OutRect->height = static_cast<double>(ScopedRect.bottom - ScopedRect.top);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** OutArray) override
    {
        if (OutArray == nullptr)
        {
            return E_INVALIDARG;
        }
        *OutArray = nullptr;
        return S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE SetFocus() override
    {
        return UIA_E_NOTSUPPORTED;
    }
    HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** OutRoot) override
    {
        if (OutRoot == nullptr)
        {
            return E_INVALIDARG;
        }
        *OutRoot = static_cast<IRawElementProviderFragmentRoot*>(this);
        AddRef();
        return S_OK;
    }

    // IRawElementProviderFragmentRoot
    HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(double InX, double InY,
                                                       IRawElementProviderFragment** Out) override
    {
        if (Out == nullptr)
        {
            return E_INVALIDARG;
        }
        *Out = nullptr;
        if (MTree == nullptr)
        {
            return S_OK;
        }
        // Topmost node wins: the projection appends children in draw order.
        for (std::uint32_t ScopedNodeIndex = MTree->Count(); ScopedNodeIndex > 0; --ScopedNodeIndex)
        {
            const Ui::AccessibilityNodeView ScopedNode = MTree->At(ScopedNodeIndex - 1);
            if ((ScopedNode.States & Ui::AccessibilityStateOffscreen) != 0)
            {
                continue;
            }
            const RECT ScopedRect = ScreenRectOf(ScopedNode.Bounds);
            if (InX < static_cast<double>(ScopedRect.left) || InX >= static_cast<double>(ScopedRect.right) ||
                InY < static_cast<double>(ScopedRect.top) || InY >= static_cast<double>(ScopedRect.bottom))
            {
                continue;
            }
            *Out = CreateNodeProvider(ScopedNode.Id);
            return S_OK;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetFocus(IRawElementProviderFragment** Out) override
    {
        if (Out == nullptr)
        {
            return E_INVALIDARG;
        }
        *Out = nullptr;
        if (MTree == nullptr || MTree->FocusedId() == 0)
        {
            return S_OK;
        }
        *Out = CreateNodeProvider(MTree->FocusedId());
        return S_OK;
    }

private:
    LONG MRefs = 1;
};

NodeProvider* CreateNodeProvider(std::uint32_t InId) noexcept
{
    if (NodeIndexOf(InId) == Ui::kNoAccessibilityNode)
    {
        return nullptr;
    }
    return new NodeProvider(InId);
}

LRESULT CALLBACK BridgeWndProc(HWND InHwnd, UINT InMessage, WPARAM InWParam, LPARAM InLParam)
{
    if (InMessage == WM_GETOBJECT && static_cast<LONG>(InLParam) == UiaRootObjectId && MRoot != nullptr)
    {
        return UiaReturnRawElementProvider(InHwnd, InWParam, InLParam, MRoot);
    }
    if (MPreviousProc != nullptr)
    {
        return CallWindowProcW(MPreviousProc, InHwnd, InMessage, InWParam, InLParam);
    }
    return DefWindowProcW(InHwnd, InMessage, InWParam, InLParam);
}
}

bool PlatformAccessibilityBridgeStart(void* InNativeWindow, const AccessibilityBridgeSettings& InSettings) noexcept
{
    if (MActive)
    {
        return true;
    }
    auto* ScopedWindow = static_cast<SDL_Window*>(InNativeWindow);
    if (ScopedWindow == nullptr)
    {
        return false;
    }
    const SDL_PropertiesID ScopedProperties = SDL_GetWindowProperties(ScopedWindow);
    MHwnd = static_cast<HWND>(SDL_GetPointerProperty(ScopedProperties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    if (MHwnd == nullptr)
    {
        return false;
    }
    MSettings = InSettings;
    MRoot = new RootProvider();
    MPreviousProc =
        reinterpret_cast<WNDPROC>(SetWindowLongPtrW(MHwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&BridgeWndProc)));
    MActive = true;
    LogInfo("a11y", "UIA provider attached to the game window");
    return true;
}

void PlatformAccessibilityBridgeStop() noexcept
{
    if (!MActive)
    {
        return;
    }
    if (MHwnd != nullptr && MPreviousProc != nullptr)
    {
        (void)SetWindowLongPtrW(MHwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(MPreviousProc));
    }
    MPreviousProc = nullptr;
    if (MRoot != nullptr)
    {
        MRoot->Release();
        MRoot = nullptr;
    }
    MTree = nullptr;
    MHwnd = nullptr;
    MActive = false;
}

void PlatformAccessibilityBridgeUpdate(Ui::AccessibilityTree& InTree) noexcept
{
    if (!MActive)
    {
        return;
    }
    MTree = &InTree;
    Ui::AccessibilityEvent ScopedEvent{};
    while (InTree.PollEvent(ScopedEvent))
    {
        switch (ScopedEvent.Kind)
        {
            case Ui::AccessibilityEventKind::Structure:
                if (MSettings.RaiseStructureEvents && MRoot != nullptr)
                {
                    (void)UiaRaiseStructureChangedEvent(MRoot, StructureChangeType_ChildrenInvalidated, nullptr, 0);
                }
                break;
            case Ui::AccessibilityEventKind::Focus:
                if (MSettings.RaiseFocusEvents && ScopedEvent.Id != 0)
                {
                    NodeProvider* ScopedProvider = CreateNodeProvider(ScopedEvent.Id);
                    if (ScopedProvider != nullptr)
                    {
                        (void)UiaRaiseAutomationEvent(static_cast<IRawElementProviderSimple*>(ScopedProvider),
                                                      UIA_AutomationFocusChangedEventId);
                        ScopedProvider->Release();
                    }
                }
                break;
            case Ui::AccessibilityEventKind::State:
                if (MSettings.RaiseStateEvents && ScopedEvent.Id != 0)
                {
                    NodeProvider* ScopedProvider = CreateNodeProvider(ScopedEvent.Id);
                    if (ScopedProvider != nullptr)
                    {
                        VARIANT ScopedOld;
                        VARIANT ScopedNew;
                        VariantInit(&ScopedOld);
                        VariantInit(&ScopedNew);
                        ScopedOld.vt = VT_BOOL;
                        ScopedOld.boolVal = VARIANT_FALSE;
                        ScopedNew.vt = VT_BOOL;
                        ScopedNew.boolVal =
                            (ScopedEvent.States & Ui::AccessibilityStateFocused) != 0 ? VARIANT_TRUE : VARIANT_FALSE;
                        (void)UiaRaiseAutomationPropertyChangedEvent(
                            static_cast<IRawElementProviderSimple*>(ScopedProvider), UIA_HasKeyboardFocusPropertyId,
                            ScopedOld, ScopedNew);
                        ScopedProvider->Release();
                    }
                }
                break;
            case Ui::AccessibilityEventKind::Value:
            case Ui::AccessibilityEventKind::Invoke:
            default:
                break;
        }
    }
}

bool PlatformAccessibilityBridgeActive() noexcept
{
    return MActive;
}

const char* PlatformAccessibilityBridgeName() noexcept
{
    return "Windows";
}
}
#endif
