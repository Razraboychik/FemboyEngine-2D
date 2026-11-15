// FemboyEngine2D - Linux AT-SPI2 provider (best-effort).
//
// The bridge speaks the AT-SPI2 D-Bus protocol directly through libdbus:
//   * connects to the accessibility bus announced by org.a11y.Bus;
//   * registers one object path per semantic node;
//   * embeds the application root in the registry (org.a11y.atspi.Socket);
//   * implements Accessible, Component, Action and Application properties;
//   * forwards focus and structure events.
// The mapping data (role ids, interfaces, state words) lives in AtspiModel and
// is covered by a portable test, so this file stays a thin protocol adapter.
#include "Engine/UI/AccessibilityBridge.hpp"

#if defined(__linux__) || defined(FBY_ATSPI_COMPILE_CHECK)

#include "Engine/Core/Log.hpp"
#include "Engine/UI/AtspiModel.hpp"

#include <SDL3/SDL.h>
#include <dbus/dbus.h>

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace Fby
{
namespace
{
constexpr const char* kRegistryName = "org.a11y.atspi.Registry";
constexpr const char* kRegistryPath = "/org/a11y/atspi/accessible/root";
constexpr const char* kRootPath = "/org/a11y/atspi/accessible/root";
constexpr const char* kNullPath = "/org/a11y/atspi/null";
constexpr const char* kAccessible = "org.a11y.atspi.Accessible";
constexpr const char* kComponent = "org.a11y.atspi.Component";
constexpr const char* kAction = "org.a11y.atspi.Action";
constexpr const char* kApplication = "org.a11y.atspi.Application";
constexpr const char* kProperties = "org.freedesktop.DBus.Properties";
constexpr const char* kIntrospectable = "org.freedesktop.DBus.Introspectable";

DBusConnection* MBus = nullptr;
Ui::AccessibilityTree* MTree = nullptr;
SDL_Window* MWindow = nullptr;
AccessibilityBridgeSettings MSettings{};
bool MActive = false;
char MUniqueName[128]{};
std::array<std::uint32_t, Ui::kMaxAccessibilityNodes> MRegisteredIds{};
std::uint32_t MRegisteredCount = 0;
std::int32_t MApplicationId = -1;

[[nodiscard]] std::uint32_t NodeIdOf(void* InUser) noexcept
{
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(InUser));
}

void ObjectPathFor(std::uint32_t InId, char* OutPath, std::size_t InCapacity) noexcept
{
    if (InId == 0)
    {
        std::snprintf(OutPath, InCapacity, "%s", kRootPath);
        return;
    }
    std::snprintf(OutPath, InCapacity, "/org/a11y/atspi/accessible/%u", InId);
}

[[nodiscard]] bool NodeById(std::uint32_t InId, Ui::AccessibilityNodeView& OutNode) noexcept
{
    if (MTree == nullptr || InId == 0)
    {
        return false;
    }
    const std::uint32_t ScopedIndex = MTree->FindIndexById(InId);
    if (ScopedIndex == Ui::kNoAccessibilityNode)
    {
        return false;
    }
    OutNode = MTree->At(ScopedIndex);
    return true;
}

[[nodiscard]] std::uint32_t RootNodeId() noexcept
{
    if (MTree == nullptr)
    {
        return 0;
    }
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MTree->Count(); ++ScopedIndex)
    {
        const Ui::AccessibilityNodeView ScopedNode = MTree->At(ScopedIndex);
        if (ScopedNode.ParentIndex == Ui::kNoAccessibilityNode)
        {
            return ScopedNode.Id;
        }
    }
    return 0;
}

void AppendReference(DBusMessageIter* InIter, std::uint32_t InId) noexcept
{
    char ScopedPath[128]{};
    ObjectPathFor(InId, ScopedPath, sizeof(ScopedPath));
    const char* ScopedName = MUniqueName;
    const char* ScopedObjectPath = ScopedPath;
    DBusMessageIter ScopedStruct;
    if (!dbus_message_iter_open_container(InIter, DBUS_TYPE_STRUCT, nullptr, &ScopedStruct))
    {
        return;
    }
    (void)dbus_message_iter_append_basic(&ScopedStruct, DBUS_TYPE_STRING, &ScopedName);
    (void)dbus_message_iter_append_basic(&ScopedStruct, DBUS_TYPE_OBJECT_PATH, &ScopedObjectPath);
    (void)dbus_message_iter_close_container(InIter, &ScopedStruct);
}

void AppendString(DBusMessageIter* InIter, const char* InText) noexcept
{
    const char* ScopedText = InText != nullptr ? InText : "";
    (void)dbus_message_iter_append_basic(InIter, DBUS_TYPE_STRING, &ScopedText);
}

void AppendState(DBusMessageIter* InIter, std::uint64_t InBits) noexcept
{
    DBusMessageIter ScopedArray;
    if (!dbus_message_iter_open_container(InIter, DBUS_TYPE_ARRAY, "u", &ScopedArray))
    {
        return;
    }
    const dbus_uint32_t ScopedLow = static_cast<dbus_uint32_t>(InBits & 0xFFFFFFFFu);
    const dbus_uint32_t ScopedHigh = static_cast<dbus_uint32_t>(InBits >> 32);
    (void)dbus_message_iter_append_basic(&ScopedArray, DBUS_TYPE_UINT32, &ScopedLow);
    (void)dbus_message_iter_append_basic(&ScopedArray, DBUS_TYPE_UINT32, &ScopedHigh);
    (void)dbus_message_iter_close_container(InIter, &ScopedArray);
}

[[nodiscard]] std::uint32_t WindowOffsetX() noexcept
{
    int ScopedX = 0;
    int ScopedY = 0;
    if (MWindow != nullptr)
    {
        (void)SDL_GetWindowPosition(MWindow, &ScopedX, &ScopedY);
    }
    return static_cast<std::uint32_t>(ScopedX < 0 ? 0 : ScopedX);
}

[[nodiscard]] std::uint32_t WindowOffsetY() noexcept
{
    int ScopedX = 0;
    int ScopedY = 0;
    if (MWindow != nullptr)
    {
        (void)SDL_GetWindowPosition(MWindow, &ScopedX, &ScopedY);
    }
    return static_cast<std::uint32_t>(ScopedY < 0 ? 0 : ScopedY);
}

void AppendExtents(DBusMessageIter* InIter, const Ui::AccessibilityNodeView& InNode) noexcept
{
    DBusMessageIter ScopedStruct;
    if (!dbus_message_iter_open_container(InIter, DBUS_TYPE_STRUCT, nullptr, &ScopedStruct))
    {
        return;
    }
    const dbus_int32_t ScopedX =
        static_cast<dbus_int32_t>(InNode.Bounds.X) + static_cast<dbus_int32_t>(WindowOffsetX());
    const dbus_int32_t ScopedY =
        static_cast<dbus_int32_t>(InNode.Bounds.Y) + static_cast<dbus_int32_t>(WindowOffsetY());
    const dbus_int32_t ScopedW = static_cast<dbus_int32_t>(InNode.Bounds.Z - InNode.Bounds.X);
    const dbus_int32_t ScopedH = static_cast<dbus_int32_t>(InNode.Bounds.W - InNode.Bounds.Y);
    (void)dbus_message_iter_append_basic(&ScopedStruct, DBUS_TYPE_INT32, &ScopedX);
    (void)dbus_message_iter_append_basic(&ScopedStruct, DBUS_TYPE_INT32, &ScopedY);
    (void)dbus_message_iter_append_basic(&ScopedStruct, DBUS_TYPE_INT32, &ScopedW);
    (void)dbus_message_iter_append_basic(&ScopedStruct, DBUS_TYPE_INT32, &ScopedH);
    (void)dbus_message_iter_close_container(InIter, &ScopedStruct);
}

[[nodiscard]] DBusMessage* HandleAccessible(DBusMessage* InMessage, std::uint32_t InId) noexcept
{
    const char* ScopedMember = dbus_message_get_member(InMessage);
    if (ScopedMember == nullptr)
    {
        return nullptr;
    }
    const std::uint32_t ScopedId = InId == 0 ? RootNodeId() : InId;
    Ui::AccessibilityNodeView ScopedNode{};
    (void)NodeById(ScopedId, ScopedNode);
    DBusMessage* ScopedReply = dbus_message_new_method_return(InMessage);
    if (ScopedReply == nullptr)
    {
        return nullptr;
    }
    DBusMessageIter ScopedIter;
    dbus_message_iter_init_append(ScopedReply, &ScopedIter);

    if (std::strcmp(ScopedMember, "GetName") == 0)
    {
        char ScopedName[256]{};
        if (ScopedNode.Id != 0)
        {
            (void)MTree->NameOf(ScopedNode, ScopedName, sizeof(ScopedName));
        }
        AppendString(&ScopedIter, ScopedName);
        return ScopedReply;
    }
    if (std::strcmp(ScopedMember, "GetDescription") == 0)
    {
        AppendString(&ScopedIter, "");
        return ScopedReply;
    }
    if (std::strcmp(ScopedMember, "GetRole") == 0)
    {
        const dbus_uint32_t ScopedRole = AtspiRoleId(ScopedNode.Role);
        (void)dbus_message_iter_append_basic(&ScopedIter, DBUS_TYPE_UINT32, &ScopedRole);
        return ScopedReply;
    }
    if (std::strcmp(ScopedMember, "GetRoleName") == 0)
    {
        AppendString(&ScopedIter, AtspiRoleName(ScopedNode.Role));
        return ScopedReply;
    }
    if (std::strcmp(ScopedMember, "GetInterfaces") == 0)
    {
        DBusMessageIter ScopedArray;
        (void)dbus_message_iter_open_container(&ScopedIter, DBUS_TYPE_ARRAY, "s", &ScopedArray);
        const std::uint32_t ScopedMask = AtspiInterfacesFor(ScopedNode.Role, ScopedNode.States, ScopedNode.Bounds);
        AppendString(&ScopedArray, "org.a11y.atspi.Accessible");
        if ((ScopedMask & static_cast<std::uint32_t>(AtspiInterface::Component)) != 0)
        {
            AppendString(&ScopedArray, "org.a11y.atspi.Component");
        }
        if ((ScopedMask & static_cast<std::uint32_t>(AtspiInterface::Action)) != 0)
        {
            AppendString(&ScopedArray, "org.a11y.atspi.Action");
        }
        (void)dbus_message_iter_close_container(&ScopedIter, &ScopedArray);
        return ScopedReply;
    }
    if (std::strcmp(ScopedMember, "GetState") == 0)
    {
        AppendState(&ScopedIter, AtspiStateBitsFor(ScopedNode.States));
        return ScopedReply;
    }
    if (std::strcmp(ScopedMember, "GetChildren") == 0)
    {
        DBusMessageIter ScopedArray;
        (void)dbus_message_iter_open_container(&ScopedIter, DBUS_TYPE_ARRAY, "(so)", &ScopedArray);
        if (ScopedNode.Id != 0)
        {
            const std::uint32_t ScopedIndex = MTree->FindIndexById(ScopedNode.Id);
            std::uint32_t ScopedChild = MTree->FirstChildOf(ScopedIndex);
            while (ScopedChild != Ui::kNoAccessibilityNode)
            {
                AppendReference(&ScopedArray, MTree->At(ScopedChild).Id);
                ScopedChild = MTree->NextSiblingOf(ScopedChild);
            }
        }
        (void)dbus_message_iter_close_container(&ScopedIter, &ScopedArray);
        return ScopedReply;
    }
    if (std::strcmp(ScopedMember, "GetChildAtIndex") == 0)
    {
        dbus_int32_t ScopedWanted = 0;
        if (!dbus_message_get_args(InMessage, nullptr, DBUS_TYPE_INT32, &ScopedWanted, DBUS_TYPE_INVALID))
        {
            dbus_message_unref(ScopedReply);
            return dbus_message_new_error(InMessage, DBUS_ERROR_INVALID_ARGS, "Invalid arguments");
        }
        std::uint32_t ScopedChild = Ui::kNoAccessibilityNode;
        if (ScopedNode.Id != 0)
        {
            const std::uint32_t ScopedIndex = MTree->FindIndexById(ScopedNode.Id);
            ScopedChild = MTree->FirstChildOf(ScopedIndex);
            for (dbus_int32_t ScopedStep = 0; ScopedStep < ScopedWanted && ScopedChild != Ui::kNoAccessibilityNode;
                 ++ScopedStep)
            {
                ScopedChild = MTree->NextSiblingOf(ScopedChild);
            }
        }
        AppendReference(&ScopedIter, ScopedChild == Ui::kNoAccessibilityNode ? 0 : MTree->At(ScopedChild).Id);
        return ScopedReply;
    }
    if (std::strcmp(ScopedMember, "GetIndexInParent") == 0)
    {
        dbus_int32_t ScopedIndexInParent = -1;
        if (ScopedNode.Id != 0 && ScopedNode.ParentIndex != Ui::kNoAccessibilityNode)
        {
            const std::uint32_t ScopedParentIndex = ScopedNode.ParentIndex;
            std::uint32_t ScopedChild = MTree->FirstChildOf(ScopedParentIndex);
            dbus_int32_t ScopedCounter = 0;
            while (ScopedChild != Ui::kNoAccessibilityNode)
            {
                if (MTree->At(ScopedChild).Id == ScopedNode.Id)
                {
                    ScopedIndexInParent = ScopedCounter;
                    break;
                }
                ++ScopedCounter;
                ScopedChild = MTree->NextSiblingOf(ScopedChild);
            }
        }
        (void)dbus_message_iter_append_basic(&ScopedIter, DBUS_TYPE_INT32, &ScopedIndexInParent);
        return ScopedReply;
    }
    if (std::strcmp(ScopedMember, "GetParent") == 0)
    {
        if (ScopedNode.Id != 0 && ScopedNode.ParentIndex != Ui::kNoAccessibilityNode)
        {
            AppendReference(&ScopedIter, MTree->At(ScopedNode.ParentIndex).Id);
        }
        else
        {
            AppendReference(&ScopedIter, 0);
        }
        return ScopedReply;
    }
    if (std::strcmp(ScopedMember, "GetApplication") == 0)
    {
        AppendReference(&ScopedIter, RootNodeId());
        return ScopedReply;
    }
    if (std::strcmp(ScopedMember, "GetAttributes") == 0)
    {
        DBusMessageIter ScopedDict;
        (void)dbus_message_iter_open_container(&ScopedIter, DBUS_TYPE_ARRAY, "{ss}", &ScopedDict);
        (void)dbus_message_iter_close_container(&ScopedIter, &ScopedDict);
        return ScopedReply;
    }
    dbus_message_unref(ScopedReply);
    return nullptr;
}

[[nodiscard]] DBusMessage* HandleComponent(DBusMessage* InMessage, std::uint32_t InId) noexcept
{
    const char* ScopedMember = dbus_message_get_member(InMessage);
    if (ScopedMember == nullptr)
    {
        return nullptr;
    }
    const std::uint32_t ScopedId = InId == 0 ? RootNodeId() : InId;
    Ui::AccessibilityNodeView ScopedNode{};
    (void)NodeById(ScopedId, ScopedNode);
    DBusMessage* ScopedReply = dbus_message_new_method_return(InMessage);
    if (ScopedReply == nullptr)
    {
        return nullptr;
    }
    DBusMessageIter ScopedIter;
    dbus_message_iter_init_append(ScopedReply, &ScopedIter);
    if (std::strcmp(ScopedMember, "GetExtents") == 0)
    {
        AppendExtents(&ScopedIter, ScopedNode);
        return ScopedReply;
    }
    if (std::strcmp(ScopedMember, "GetPosition") == 0)
    {
        DBusMessageIter ScopedStruct;
        (void)dbus_message_iter_open_container(&ScopedIter, DBUS_TYPE_STRUCT, nullptr, &ScopedStruct);
        const dbus_int32_t ScopedX =
            static_cast<dbus_int32_t>(ScopedNode.Bounds.X) + static_cast<dbus_int32_t>(WindowOffsetX());
        const dbus_int32_t ScopedY =
            static_cast<dbus_int32_t>(ScopedNode.Bounds.Y) + static_cast<dbus_int32_t>(WindowOffsetY());
        (void)dbus_message_iter_append_basic(&ScopedStruct, DBUS_TYPE_INT32, &ScopedX);
        (void)dbus_message_iter_append_basic(&ScopedStruct, DBUS_TYPE_INT32, &ScopedY);
        (void)dbus_message_iter_close_container(&ScopedIter, &ScopedStruct);
        return ScopedReply;
    }
    if (std::strcmp(ScopedMember, "GetSize") == 0)
    {
        DBusMessageIter ScopedStruct;
        (void)dbus_message_iter_open_container(&ScopedIter, DBUS_TYPE_STRUCT, nullptr, &ScopedStruct);
        const dbus_int32_t ScopedW = static_cast<dbus_int32_t>(ScopedNode.Bounds.Z - ScopedNode.Bounds.X);
        const dbus_int32_t ScopedH = static_cast<dbus_int32_t>(ScopedNode.Bounds.W - ScopedNode.Bounds.Y);
        (void)dbus_message_iter_append_basic(&ScopedStruct, DBUS_TYPE_INT32, &ScopedW);
        (void)dbus_message_iter_append_basic(&ScopedStruct, DBUS_TYPE_INT32, &ScopedH);
        (void)dbus_message_iter_close_container(&ScopedIter, &ScopedStruct);
        return ScopedReply;
    }
    if (std::strcmp(ScopedMember, "GrabFocus") == 0)
    {
        const dbus_bool_t ScopedResult = TRUE;
        (void)dbus_message_iter_append_basic(&ScopedIter, DBUS_TYPE_BOOLEAN, &ScopedResult);
        return ScopedReply;
    }
    if (std::strcmp(ScopedMember, "Contains") == 0)
    {
        const dbus_bool_t ScopedResult =
            ScopedNode.Id != 0 && (ScopedNode.States & Ui::AccessibilityStateOffscreen) == 0 ? TRUE : FALSE;
        (void)dbus_message_iter_append_basic(&ScopedIter, DBUS_TYPE_BOOLEAN, &ScopedResult);
        return ScopedReply;
    }
    if (std::strcmp(ScopedMember, "GetAccessibleAtPoint") == 0)
    {
        AppendReference(&ScopedIter, InId);
        return ScopedReply;
    }
    if (std::strcmp(ScopedMember, "GetLayer") == 0)
    {
        const dbus_uint32_t ScopedLayer = 3;
        (void)dbus_message_iter_append_basic(&ScopedIter, DBUS_TYPE_UINT32, &ScopedLayer);
        return ScopedReply;
    }
    if (std::strcmp(ScopedMember, "GetMDIZOrder") == 0)
    {
        const dbus_int32_t ScopedOrder = -1;
        (void)dbus_message_iter_append_basic(&ScopedIter, DBUS_TYPE_INT32, &ScopedOrder);
        return ScopedReply;
    }
    if (std::strcmp(ScopedMember, "GetWindow") == 0)
    {
        const dbus_uint32_t ScopedWindow = 0;
        (void)dbus_message_iter_append_basic(&ScopedIter, DBUS_TYPE_UINT32, &ScopedWindow);
        return ScopedReply;
    }
    dbus_message_unref(ScopedReply);
    return nullptr;
}

[[nodiscard]] DBusMessage* HandleAction(DBusMessage* InMessage, std::uint32_t InId) noexcept
{
    const char* ScopedMember = dbus_message_get_member(InMessage);
    if (ScopedMember == nullptr)
    {
        return nullptr;
    }
    const std::uint32_t ScopedId = InId == 0 ? RootNodeId() : InId;
    Ui::AccessibilityNodeView ScopedNode{};
    (void)NodeById(ScopedId, ScopedNode);
    DBusMessage* ScopedReply = dbus_message_new_method_return(InMessage);
    if (ScopedReply == nullptr)
    {
        return nullptr;
    }
    DBusMessageIter ScopedIter;
    dbus_message_iter_init_append(ScopedReply, &ScopedIter);
    if (std::strcmp(ScopedMember, "GetActions") == 0)
    {
        DBusMessageIter ScopedArray;
        (void)dbus_message_iter_open_container(&ScopedIter, DBUS_TYPE_ARRAY, "(sss)", &ScopedArray);
        char ScopedName[128]{};
        if (ScopedNode.Id != 0)
        {
            (void)MTree->NameOf(ScopedNode, ScopedName, sizeof(ScopedName));
        }
        DBusMessageIter ScopedStruct;
        (void)dbus_message_iter_open_container(&ScopedArray, DBUS_TYPE_STRUCT, nullptr, &ScopedStruct);
        AppendString(&ScopedStruct, "activate");
        AppendString(&ScopedStruct, ScopedName);
        AppendString(&ScopedStruct, "");
        (void)dbus_message_iter_close_container(&ScopedArray, &ScopedStruct);
        (void)dbus_message_iter_close_container(&ScopedIter, &ScopedArray);
        return ScopedReply;
    }
    if (std::strcmp(ScopedMember, "DoAction") == 0)
    {
        dbus_uint32_t ScopedIndex = 0;
        (void)dbus_message_get_args(InMessage, nullptr, DBUS_TYPE_INT32, &ScopedIndex, DBUS_TYPE_INVALID);
        const dbus_bool_t ScopedResult = ScopedNode.Id != 0 && MTree->Activate(ScopedNode.Id) ? TRUE : FALSE;
        (void)dbus_message_iter_append_basic(&ScopedIter, DBUS_TYPE_BOOLEAN, &ScopedResult);
        return ScopedReply;
    }
    if (std::strcmp(ScopedMember, "GetName") == 0 || std::strcmp(ScopedMember, "GetDescription") == 0)
    {
        AppendString(&ScopedIter, "activate");
        return ScopedReply;
    }
    if (std::strcmp(ScopedMember, "GetKeyBinding") == 0)
    {
        AppendString(&ScopedIter, "");
        return ScopedReply;
    }
    dbus_message_unref(ScopedReply);
    return nullptr;
}

[[nodiscard]] DBusMessage* HandleProperties(DBusMessage* InMessage) noexcept
{
    const char* ScopedMember = dbus_message_get_member(InMessage);
    if (ScopedMember == nullptr)
    {
        return nullptr;
    }
    if (std::strcmp(ScopedMember, "Set") == 0)
    {
        const char* ScopedInterface = nullptr;
        const char* ScopedProperty = nullptr;
        DBusMessageIter ScopedIter;
        if (!dbus_message_iter_init(InMessage, &ScopedIter))
        {
            return dbus_message_new_error(InMessage, DBUS_ERROR_INVALID_ARGS, "Invalid arguments");
        }
        if (dbus_message_iter_get_arg_type(&ScopedIter) != DBUS_TYPE_STRING)
        {
            return dbus_message_new_error(InMessage, DBUS_ERROR_INVALID_ARGS, "Invalid arguments");
        }
        dbus_message_iter_get_basic(&ScopedIter, &ScopedInterface);
        (void)dbus_message_iter_next(&ScopedIter);
        if (dbus_message_iter_get_arg_type(&ScopedIter) != DBUS_TYPE_STRING)
        {
            return dbus_message_new_error(InMessage, DBUS_ERROR_INVALID_ARGS, "Invalid arguments");
        }
        dbus_message_iter_get_basic(&ScopedIter, &ScopedProperty);
        return dbus_message_new_method_return(InMessage);
    }
    if (std::strcmp(ScopedMember, "Get") == 0)
    {
        const char* ScopedInterface = nullptr;
        const char* ScopedProperty = nullptr;
        DBusMessageIter ScopedIter;
        if (!dbus_message_iter_init(InMessage, &ScopedIter))
        {
            return dbus_message_new_error(InMessage, DBUS_ERROR_INVALID_ARGS, "Invalid arguments");
        }
        dbus_message_iter_get_basic(&ScopedIter, &ScopedInterface);
        (void)dbus_message_iter_next(&ScopedIter);
        dbus_message_iter_get_basic(&ScopedIter, &ScopedProperty);
        DBusMessage* ScopedReply = dbus_message_new_method_return(InMessage);
        if (ScopedReply == nullptr)
        {
            return nullptr;
        }
        DBusMessageIter ScopedOut;
        dbus_message_iter_init_append(ScopedReply, &ScopedOut);
        const char* ScopedValue = "";
        if (std::strcmp(ScopedProperty, "ToolkitName") == 0)
        {
            ScopedValue = "FemboyEngine2D";
        }
        else if (std::strcmp(ScopedProperty, "Version") == 0)
        {
            ScopedValue = "0.5.0";
        }
        else if (std::strcmp(ScopedProperty, "Locale") == 0)
        {
            ScopedValue = "en";
        }
        else if (std::strcmp(ScopedProperty, "Name") == 0)
        {
            ScopedValue = "FemboyEngine2D";
        }
        DBusMessageIter ScopedVariant;
        (void)dbus_message_iter_open_container(&ScopedOut, DBUS_TYPE_VARIANT, "s", &ScopedVariant);
        AppendString(&ScopedVariant, ScopedValue);
        (void)dbus_message_iter_close_container(&ScopedOut, &ScopedVariant);
        return ScopedReply;
    }
    return nullptr;
}

DBusHandlerResult HandleMessage(DBusConnection*, DBusMessage* InMessage, void* InUser) noexcept
{
    if (!MActive)
    {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
    const char* ScopedInterface = dbus_message_get_interface(InMessage);
    if (ScopedInterface == nullptr)
    {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
    const std::uint32_t ScopedId = NodeIdOf(InUser);
    DBusMessage* ScopedReply = nullptr;
    if (std::strcmp(ScopedInterface, kAccessible) == 0)
    {
        ScopedReply = HandleAccessible(InMessage, ScopedId);
    }
    else if (std::strcmp(ScopedInterface, kComponent) == 0)
    {
        ScopedReply = HandleComponent(InMessage, ScopedId);
    }
    else if (std::strcmp(ScopedInterface, kAction) == 0)
    {
        ScopedReply = HandleAction(InMessage, ScopedId);
    }
    else if (std::strcmp(ScopedInterface, kProperties) == 0)
    {
        ScopedReply = HandleProperties(InMessage);
    }
    if (ScopedReply == nullptr)
    {
        ScopedReply = dbus_message_new_error(InMessage, DBUS_ERROR_UNKNOWN_METHOD, "Unsupported method");
    }
    if (ScopedReply != nullptr)
    {
        (void)dbus_connection_send(MBus, ScopedReply, nullptr);
        dbus_message_unref(ScopedReply);
    }
    return DBUS_HANDLER_RESULT_HANDLED;
}

DBusObjectPathVTable MNodeVTable = {nullptr, &HandleMessage, nullptr, nullptr, nullptr, nullptr};

void RegisterNode(std::uint32_t InId) noexcept
{
    if (InId == 0 || MBus == nullptr)
    {
        return;
    }
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MRegisteredCount; ++ScopedIndex)
    {
        if (MRegisteredIds[ScopedIndex] == InId)
        {
            return;
        }
    }
    char ScopedPath[128]{};
    ObjectPathFor(InId, ScopedPath, sizeof(ScopedPath));
    if (MRegisteredCount < MRegisteredIds.size() &&
        dbus_connection_register_object_path(MBus, ScopedPath, &MNodeVTable,
                                             reinterpret_cast<void*>(static_cast<std::uintptr_t>(InId))))
    {
        MRegisteredIds[MRegisteredCount++] = InId;
    }
}

void UnregisterAll() noexcept
{
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MRegisteredCount; ++ScopedIndex)
    {
        char ScopedPath[128]{};
        ObjectPathFor(MRegisteredIds[ScopedIndex], ScopedPath, sizeof(ScopedPath));
        (void)dbus_connection_unregister_object_path(MBus, ScopedPath);
    }
    MRegisteredCount = 0;
}

void RegisterTree() noexcept
{
    if (MTree == nullptr)
    {
        return;
    }
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MTree->Count(); ++ScopedIndex)
    {
        const Ui::AccessibilityNodeView ScopedNode = MTree->At(ScopedIndex);
        if (ScopedNode.ParentIndex != Ui::kNoAccessibilityNode)
        {
            RegisterNode(ScopedNode.Id);
        }
    }
}

void EmbedInRegistry() noexcept
{
    DBusMessage* ScopedMessage =
        dbus_message_new_method_call(kRegistryName, kRegistryPath, "org.a11y.atspi.Socket", "Embed");
    if (ScopedMessage == nullptr)
    {
        return;
    }
    DBusMessageIter ScopedIter;
    dbus_message_iter_init_append(ScopedMessage, &ScopedIter);
    // The registry expects the application root object path, which is the path
    // registered with node id 0; every other id lives under it.
    AppendReference(&ScopedIter, 0);
    DBusMessage* ScopedReply = dbus_connection_send_with_reply_and_block(MBus, ScopedMessage, 2000, nullptr);
    if (ScopedReply != nullptr)
    {
        dbus_message_unref(ScopedReply);
    }
    dbus_message_unref(ScopedMessage);
}

[[nodiscard]] DBusConnection* ConnectAccessibilityBus() noexcept
{
    DBusError ScopedError;
    dbus_error_init(&ScopedError);
    DBusConnection* ScopedSession = dbus_bus_get(DBUS_BUS_SESSION, &ScopedError);
    if (ScopedSession == nullptr)
    {
        dbus_error_free(&ScopedError);
        return nullptr;
    }
    DBusMessage* ScopedCall =
        dbus_message_new_method_call("org.a11y.Bus", "/org/a11y/bus", "org.a11y.Bus", "GetAddress");
    if (ScopedCall == nullptr)
    {
        dbus_connection_unref(ScopedSession);
        return nullptr;
    }
    DBusMessage* ScopedReply = dbus_connection_send_with_reply_and_block(ScopedSession, ScopedCall, 2000, &ScopedError);
    dbus_message_unref(ScopedCall);
    dbus_connection_unref(ScopedSession);
    if (ScopedReply == nullptr)
    {
        dbus_error_free(&ScopedError);
        return nullptr;
    }
    char* ScopedAddress = nullptr;
    if (!dbus_message_get_args(ScopedReply, &ScopedError, DBUS_TYPE_STRING, &ScopedAddress, DBUS_TYPE_INVALID))
    {
        dbus_message_unref(ScopedReply);
        dbus_error_free(&ScopedError);
        return nullptr;
    }
    DBusConnection* ScopedBus =
        ScopedAddress != nullptr ? dbus_connection_open_private(ScopedAddress, &ScopedError) : nullptr;
    dbus_message_unref(ScopedReply);
    if (ScopedBus == nullptr)
    {
        dbus_error_free(&ScopedError);
        return nullptr;
    }
    dbus_connection_set_exit_on_disconnect(ScopedBus, FALSE);
    if (!dbus_bus_register(ScopedBus, &ScopedError))
    {
        dbus_connection_unref(ScopedBus);
        dbus_error_free(&ScopedError);
        return nullptr;
    }
    const char* ScopedUnique = dbus_bus_get_unique_name(ScopedBus);
    std::snprintf(MUniqueName, sizeof(MUniqueName), "%s", ScopedUnique != nullptr ? ScopedUnique : "");
    return ScopedBus;
}

void EmitFocus(std::uint32_t InId) noexcept
{
    if (MBus == nullptr || InId == 0)
    {
        return;
    }
    char ScopedPath[128]{};
    ObjectPathFor(InId, ScopedPath, sizeof(ScopedPath));
    DBusMessage* ScopedSignal = dbus_message_new_signal(ScopedPath, "org.a11y.atspi.Event.Focus", "Focus");
    if (ScopedSignal == nullptr)
    {
        return;
    }
    DBusMessageIter ScopedIter;
    dbus_message_iter_init_append(ScopedSignal, &ScopedIter);
    AppendString(&ScopedIter, "focus");
    const dbus_int32_t ScopedZero = 0;
    (void)dbus_message_iter_append_basic(&ScopedIter, DBUS_TYPE_INT32, &ScopedZero);
    (void)dbus_message_iter_append_basic(&ScopedIter, DBUS_TYPE_INT32, &ScopedZero);
    DBusMessageIter ScopedVariant;
    (void)dbus_message_iter_open_container(&ScopedIter, DBUS_TYPE_VARIANT, "b", &ScopedVariant);
    const dbus_bool_t ScopedTrue = TRUE;
    (void)dbus_message_iter_append_basic(&ScopedVariant, DBUS_TYPE_BOOLEAN, &ScopedTrue);
    (void)dbus_message_iter_close_container(&ScopedIter, &ScopedVariant);
    DBusMessageIter ScopedDict;
    (void)dbus_message_iter_open_container(&ScopedIter, DBUS_TYPE_ARRAY, "{sv}", &ScopedDict);
    (void)dbus_message_iter_close_container(&ScopedIter, &ScopedDict);
    (void)dbus_connection_send(MBus, ScopedSignal, nullptr);
    dbus_message_unref(ScopedSignal);
}
}

bool PlatformAccessibilityBridgeStart(void* InNativeWindow, const AccessibilityBridgeSettings& InSettings) noexcept
{
    if (InNativeWindow == nullptr)
    {
        return false;
    }
    MBus = ConnectAccessibilityBus();
    if (MBus == nullptr)
    {
        LogWarn("a11y", "AT-SPI2 bridge unavailable: could not connect to the accessibility bus");
        return false;
    }
    MWindow = static_cast<SDL_Window*>(InNativeWindow);
    MSettings = InSettings;
    MActive = true;
    if (!dbus_connection_register_object_path(MBus, kRootPath, &MNodeVTable,
                                              reinterpret_cast<void*>(static_cast<std::uintptr_t>(0))))
    {
        MActive = false;
        dbus_connection_unref(MBus);
        MBus = nullptr;
        return false;
    }
    LogInfo("a11y", "AT-SPI2 bridge started on {}", MUniqueName);
    return true;
}

void PlatformAccessibilityBridgeStop() noexcept
{
    if (MBus != nullptr)
    {
        UnregisterAll();
        dbus_connection_unref(MBus);
        MBus = nullptr;
    }
    MTree = nullptr;
    MWindow = nullptr;
    MActive = false;
}

void PlatformAccessibilityBridgeUpdate(Ui::AccessibilityTree& InTree) noexcept
{
    if (!MActive || MBus == nullptr)
    {
        return;
    }
    MTree = &InTree;
    RegisterTree();
    if (MApplicationId < 0)
    {
        EmbedInRegistry();
        MApplicationId = 0;
    }
    Ui::AccessibilityEvent ScopedEvent{};
    while (InTree.PollEvent(ScopedEvent))
    {
        if (ScopedEvent.Kind == Ui::AccessibilityEventKind::Focus && MSettings.RaiseFocusEvents)
        {
            EmitFocus(ScopedEvent.Id);
        }
    }
    (void)dbus_connection_read_write_dispatch(MBus, 0);
}

bool PlatformAccessibilityBridgeActive() noexcept
{
    return MActive;
}

const char* PlatformAccessibilityBridgeName() noexcept
{
    return "Linux";
}
}
#endif
