#include "Engine/Core/WindowManager.hpp"

#include "Engine/Core/Log.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>

// SDL_vulkan.h only forwards the Vulkan handles; the engine needs the real
#if defined(FBY_HAS_VULKAN_HEADERS)
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#endif
#include <SDL3/SDL_vulkan.h>

#if defined(__APPLE__)
#include <SDL3/SDL_metal.h>
#endif

#include <memory>
#include <string>
#include <utility>

namespace Fby
{
namespace
{
// The dialog is asynchronous, so the strings the filters point at must outlive
struct DialogRequest
{
    WindowManager::DialogResult callback;
    std::vector<std::string> strings;
    std::vector<SDL_DialogFileFilter> filters;
    // SDL keeps the default-location pointer until the dialog closes, so the
    // string must outlive the call.
    std::string defaultLocation;
};

void SDLCALL DialogCallback(void* InUserdata, const char* const* InFiles, int InFilter)
{
    (void)InFilter;
    const std::unique_ptr<DialogRequest> ScopedRequest(static_cast<DialogRequest*>(InUserdata));
    if (!ScopedRequest)
    {
        return;
    }

    std::vector<std::string> ScopedPaths;
    if (InFiles != nullptr)
    {
        for (const char* const* ScopedIt = InFiles; *ScopedIt != nullptr; ++ScopedIt)
        {
            ScopedPaths.emplace_back(*ScopedIt);
        }
    }
    if (ScopedRequest->callback)
    {
        ScopedRequest->callback(std::move(ScopedPaths));
    }
}

[[nodiscard]] DialogRequest* MakeRequest(std::span<const FileFilter> InFilters, WindowManager::DialogResult InCallback)
{
    auto ScopedRequest = std::make_unique<DialogRequest>();
    ScopedRequest->callback = std::move(InCallback);

    // Reserve up front: the filter entries point into `strings`, so that vector
    ScopedRequest->strings.reserve(InFilters.size() * 2);
    ScopedRequest->filters.reserve(InFilters.size());
    for (const FileFilter& ScopedFilter : InFilters)
    {
        ScopedRequest->strings.emplace_back(ScopedFilter.name);
        ScopedRequest->strings.emplace_back(ScopedFilter.pattern);
    }
    for (std::size_t ScopedI = 0; ScopedI < InFilters.size(); ++ScopedI)
    {
        ScopedRequest->filters.push_back(SDL_DialogFileFilter{ScopedRequest->strings[ScopedI * 2].c_str(),
                                                              ScopedRequest->strings[ScopedI * 2 + 1].c_str()});
    }
    return ScopedRequest.release();
}
}

WindowManager::WindowManager() = default;

WindowManager::~WindowManager()
{
    Destroy();
}

bool WindowManager::Initialize()
{
    if (MInitialized)
    {
        return true;
    }

    // SDL3 handles DPI awareness itself when the window is created with
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
    {
        LogError("platform", "SDL_Init failed: {}", SDL_GetError());
        return false;
    }

    MInitialized = true;
    LogInfo("platform", "SDL {} initialised ({})", SDL_GetRevision(), SDL_GetCurrentVideoDriver());
    return true;
}

const char* WindowModeName(WindowMode InMode) noexcept
{
    switch (InMode)
    {
    case WindowMode::Windowed:
        return "windowed";
    case WindowMode::BorderlessFullscreen:
        return "borderless fullscreen";
    case WindowMode::ExclusiveFullscreen:
        return "exclusive fullscreen";
    }
    return "windowed";
}

bool WindowManager::Create(const WindowDesc& InDesc)
{
    if (!MInitialized && !Initialize())
    {
        return false;
    }
    if (MWindow != nullptr)
    {
        Destroy();
    }

    SDL_WindowFlags ScopedFlags = SDL_WINDOW_VULKAN;
    if (InDesc.resizable)
    {
        ScopedFlags |= SDL_WINDOW_RESIZABLE;
    }
    if (InDesc.highDpi)
    {
        ScopedFlags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
    }
    if (InDesc.startMaximized)
    {
        ScopedFlags |= SDL_WINDOW_MAXIMIZED;
    }

    MWindow = SDL_CreateWindow(InDesc.title.c_str(), static_cast<int>(InDesc.width), static_cast<int>(InDesc.height),
                               ScopedFlags);
    if (MWindow == nullptr)
    {
        LogError("platform", "SDL_CreateWindow failed: {}", SDL_GetError());
        return false;
    }

    int ScopedWidth = 0;
    int ScopedHeight = 0;
    SDL_GetWindowSizeInPixels(static_cast<SDL_Window*>(MWindow), &ScopedWidth, &ScopedHeight);
    MSize = WindowExtent{static_cast<std::uint32_t>(ScopedWidth), static_cast<std::uint32_t>(ScopedHeight)};
    MShouldClose = false;
    MMode = WindowMode::Windowed;

    // Borderless fullscreen is the default presentation: create the window at the
    // requested size first (so the swapchain has a valid extent) and then apply
    // the mode.
    if (InDesc.mode != WindowMode::Windowed)
    {
        if (!SetWindowMode(InDesc.mode))
        {
            LogWarn("platform", "could not enter {}: {}", WindowModeName(InDesc.mode), SDL_GetError());
        }
    }

    LogInfo("platform", "window created: {}x{} ({} driver, {})", MSize.width, MSize.height,
            SDL_GetCurrentVideoDriver(), WindowModeName(MMode));
    return true;
}

void WindowManager::Destroy()
{
    if (MWindow != nullptr)
    {
        SDL_DestroyWindow(static_cast<SDL_Window*>(MWindow));
        MWindow = nullptr;
    }
    if (MInitialized)
    {
        SDL_Quit();
        MInitialized = false;
    }
}

bool WindowManager::PumpEvents()
{
    if (MWindow == nullptr)
    {
        return false;
    }

    MInput.BeginFrame();
    SDL_Window* ScopedWindow = static_cast<SDL_Window*>(MWindow);

    SDL_Event ScopedEvent;
    while (SDL_PollEvent(&ScopedEvent))
    {
        // The UI backend sees every event before the engine consumes it. The
        if (MEventObserver)
        {
            MEventObserver(MEventObserverUser, &ScopedEvent);
        }

        switch (ScopedEvent.type)
        {
            case SDL_EVENT_QUIT:
                MShouldClose = true;
                break;

            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                if (ScopedEvent.window.windowID == SDL_GetWindowID(ScopedWindow))
                {
                    MShouldClose = true;
                }
                break;

            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            {
                int ScopedWidth = 0;
                int ScopedHeight = 0;
                SDL_GetWindowSizeInPixels(ScopedWindow, &ScopedWidth, &ScopedHeight);
                MSize = WindowExtent{static_cast<std::uint32_t>(ScopedWidth), static_cast<std::uint32_t>(ScopedHeight)};
                MMinimized = MSize.width == 0 || MSize.height == 0;
                break;
            }

            case SDL_EVENT_WINDOW_MINIMIZED:
                MMinimized = true;
                break;
            case SDL_EVENT_WINDOW_RESTORED:
                MMinimized = false;
                break;

            case SDL_EVENT_KEY_DOWN:
                if (!ScopedEvent.key.repeat && ScopedEvent.key.scancode < kMaxScancodes)
                {
                    MInput.held[ScopedEvent.key.scancode] = true;
                    MInput.pressed[ScopedEvent.key.scancode] = true;
                }
                break;
            case SDL_EVENT_KEY_UP:
                if (ScopedEvent.key.scancode < kMaxScancodes)
                {
                    MInput.held[ScopedEvent.key.scancode] = false;
                    MInput.released[ScopedEvent.key.scancode] = true;
                }
                break;

            case SDL_EVENT_TEXT_INPUT:
            {
                // Committed UTF-8 (typed or IME-selected). Append for this frame;
                // a truncated event is clamped instead of overflowing.
                const std::size_t ScopedLength = std::strlen(ScopedEvent.text.text);
                const std::size_t ScopedRoom = MInput.textInput.size() - MInput.textInputLength;
                const std::size_t ScopedCopy = std::min(ScopedLength, ScopedRoom);
                if (ScopedCopy != 0)
                {
                    std::memcpy(MInput.textInput.data() + MInput.textInputLength, ScopedEvent.text.text, ScopedCopy);
                    MInput.textInputLength = static_cast<std::uint8_t>(MInput.textInputLength + ScopedCopy);
                }
                break;
            }
            case SDL_EVENT_TEXT_EDITING:
            {
                // A negative start means the platform cleared the composition.
                MInput.compositionActive = ScopedEvent.edit.start >= 0;
                if (!MInput.compositionActive)
                {
                    MInput.compositionLength = 0;
                    MInput.compositionCursor = 0;
                    break;
                }
                const std::size_t ScopedLength = std::strlen(ScopedEvent.edit.text);
                MInput.compositionLength = static_cast<std::uint32_t>(
                    std::min<std::size_t>(ScopedLength, MInput.composition.size() - 1u));
                if (MInput.compositionLength != 0)
                {
                    std::memcpy(MInput.composition.data(), ScopedEvent.edit.text, MInput.compositionLength);
                }
                MInput.composition[MInput.compositionLength] = '\0';
                const std::int64_t ScopedCursor =
                    static_cast<std::int64_t>(ScopedEvent.edit.start) + ScopedEvent.edit.length;
                MInput.compositionCursor =
                    ScopedCursor < 0 ? 0u : static_cast<std::uint32_t>(ScopedCursor);
                break;
            }

            case SDL_EVENT_MOUSE_MOTION:
                MInput.mouse = Vector2D{ScopedEvent.motion.x, ScopedEvent.motion.y};
                MInput.mouseDelta = Vector2D{ScopedEvent.motion.xrel, ScopedEvent.motion.yrel};
                break;

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (ScopedEvent.button.button < kMaxMouseButtons)
                {
                    MInput.mouseHeld[ScopedEvent.button.button] = true;
                    MInput.mousePressed[ScopedEvent.button.button] = true;
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (ScopedEvent.button.button < kMaxMouseButtons)
                {
                    MInput.mouseHeld[ScopedEvent.button.button] = false;
                }
                break;

            case SDL_EVENT_MOUSE_WHEEL:
                MInput.wheel += ScopedEvent.wheel.y;
                break;

            default:
                break;
        }
    }

    if (MInput.quitRequested)
    {
        MShouldClose = true;
    }
    return !MShouldClose;
}

void WindowManager::SetEventObserver(EventObserver InObserver, void* InUser)
{
    MEventObserver = InObserver;
    MEventObserverUser = InUser;
}

void WindowManager::ClearEventObserver()
{
    MEventObserver = nullptr;
    MEventObserverUser = nullptr;
}

bool WindowManager::ShouldClose() const noexcept
{
    return MShouldClose;
}

bool WindowManager::IsMinimized() const noexcept
{
    return MMinimized;
}

WindowExtent WindowManager::Size() const noexcept
{
    return MSize;
}

float WindowManager::DisplayScale() const noexcept
{
    if (MWindow == nullptr)
    {
        return 1.0f;
    }
    // SDL3 returns the content scale of the display the window is on. A 0 is a
    const float ScopedScale = SDL_GetWindowDisplayScale(static_cast<SDL_Window*>(MWindow));
    return ScopedScale > 0.0f ? ScopedScale : 1.0f;
}

void* WindowManager::NativeWindow() const noexcept
{
    return MWindow;
}

std::span<const char* const> WindowManager::VulkanInstanceExtensions() const
{
    Uint32 ScopedCount = 0;
    const char* const* ScopedNames = SDL_Vulkan_GetInstanceExtensions(&ScopedCount);
    if (ScopedNames == nullptr || ScopedCount == 0)
    {
        LogError("platform", "SDL_Vulkan_GetInstanceExtensions failed: {}", SDL_GetError());
        return {};
    }
    return {ScopedNames, static_cast<std::size_t>(ScopedCount)};
}

bool WindowManager::CreateVulkanSurface(const void* InInstance, std::uint64_t* InSurface) const
{
#if defined(FBY_HAS_VULKAN_HEADERS)
    if (MWindow == nullptr || InInstance == nullptr || InSurface == nullptr)
    {
        return false;
    }

    const VkInstance ScopedVkInstance = static_cast<VkInstance>(const_cast<void*>(InInstance));
    VkSurfaceKHR ScopedCreated = VK_NULL_HANDLE;
    if (!SDL_Vulkan_CreateSurface(static_cast<SDL_Window*>(MWindow), ScopedVkInstance, nullptr, &ScopedCreated))
    {
        LogError("platform", "SDL_Vulkan_CreateSurface failed: {}", SDL_GetError());
        return false;
    }

    *InSurface = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(ScopedCreated));
    return true;
#else
    // A null/headless build has no Vulkan headers and must not reference
    (void)InInstance;
    (void)InSurface;
    LogWarn("platform", "Vulkan surface requested, but this build has no Vulkan headers");
    return false;
#endif
}

void* WindowManager::MetalLayer() const noexcept
{
#if defined(__APPLE__)
    if (MWindow == nullptr)
    {
        return nullptr;
    }
    SDL_MetalView view = SDL_Metal_CreateView(static_cast<SDL_Window*>(MWindow));
    return view != nullptr ? SDL_Metal_GetLayer(view) : nullptr;
#else
    return nullptr;
#endif
}

void WindowManager::SetTitle(std::string_view InTitle)
{
    if (MWindow != nullptr)
    {
        SDL_SetWindowTitle(static_cast<SDL_Window*>(MWindow), std::string(InTitle).c_str());
    }
}

void WindowManager::SetFullscreen(bool InFullscreen)
{
    (void)SetWindowMode(InFullscreen ? WindowMode::BorderlessFullscreen : WindowMode::Windowed);
}

bool WindowManager::ApplyWindowMode(WindowMode InMode) noexcept
{
    auto* ScopedWindow = static_cast<SDL_Window*>(MWindow);
    switch (InMode)
    {
    case WindowMode::Windowed:
        return SDL_SetWindowFullscreen(ScopedWindow, false);
    case WindowMode::BorderlessFullscreen:
        // NULL fullscreen mode == desktop-sized borderless fullscreen.
        return SDL_SetWindowFullscreenMode(ScopedWindow, nullptr) && SDL_SetWindowFullscreen(ScopedWindow, true);
    case WindowMode::ExclusiveFullscreen:
    {
        // Exclusive mode locks the display; use its current desktop mode so the
        // window never asks for an unsupported resolution.
        const SDL_DisplayID ScopedDisplay = SDL_GetDisplayForWindow(ScopedWindow);
        const SDL_DisplayMode* ScopedMode = SDL_GetDesktopDisplayMode(ScopedDisplay);
        return ScopedMode != nullptr && SDL_SetWindowFullscreenMode(ScopedWindow, ScopedMode) &&
               SDL_SetWindowFullscreen(ScopedWindow, true);
    }
    }
    return false;
}

bool WindowManager::SetWindowMode(WindowMode InMode) noexcept
{
    if (MWindow == nullptr)
    {
        return false;
    }
    if (InMode == MMode)
    {
        return true;
    }
    auto* ScopedWindow = static_cast<SDL_Window*>(MWindow);
    const WindowMode ScopedPrevious = MMode;

    // A failed transition (for example exclusive fullscreen on a remote desktop)
    // can leave the window windowed at its authored size, so restore the
    // previous mode instead of keeping that broken intermediate state.
    if (!ApplyWindowMode(InMode))
    {
        (void)ApplyWindowMode(ScopedPrevious);
        MMode = ScopedPrevious;
        LogWarn("platform", "could not switch to {}: {}", WindowModeName(InMode), SDL_GetError());
        return false;
    }
    MMode = InMode;
    // Block until the mode is applied so the swapchain is created at the real
    // size instead of the pre-fullscreen one.
    (void)SDL_SyncWindow(ScopedWindow);
    int ScopedWidth = 0;
    int ScopedHeight = 0;
    SDL_GetWindowSizeInPixels(ScopedWindow, &ScopedWidth, &ScopedHeight);
    if (ScopedWidth > 0 && ScopedHeight > 0)
    {
        MSize = WindowExtent{static_cast<std::uint32_t>(ScopedWidth), static_cast<std::uint32_t>(ScopedHeight)};
    }
    LogInfo("platform", "window mode: {} ({}x{})", WindowModeName(InMode), MSize.width, MSize.height);
    return true;
}

void WindowManager::SetVsyncHint(bool InEnabled)
{
    MVsync = InEnabled;
}

void WindowManager::ShowOpenFile(std::string_view InTitle, std::span<const FileFilter> InFilters,
                                 DialogResult InOnResult, std::string_view InDefaultLocation, bool InAllowMultiple)
{
    if (MWindow == nullptr)
    {
        return;
    }
    DialogRequest* ScopedRequest = MakeRequest(InFilters, std::move(InOnResult));
    ScopedRequest->defaultLocation = std::string(InDefaultLocation);
    SDL_ShowOpenFileDialog(DialogCallback, ScopedRequest, static_cast<SDL_Window*>(MWindow),
                           ScopedRequest->filters.empty() ? nullptr : ScopedRequest->filters.data(),
                           static_cast<int>(ScopedRequest->filters.size()),
                           ScopedRequest->defaultLocation.empty() ? nullptr
                                                                  : ScopedRequest->defaultLocation.c_str(),
                           InAllowMultiple);
}

void WindowManager::ShowSaveFile(std::string_view InTitle, std::span<const FileFilter> InFilters,
                                 DialogResult InOnResult, std::string_view InDefaultLocation,
                                 std::string_view InDefaultName)
{
    if (MWindow == nullptr)
    {
        return;
    }
    DialogRequest* ScopedRequest = MakeRequest(InFilters, std::move(InOnResult));
    ScopedRequest->defaultLocation = std::string(InDefaultLocation);
    SDL_ShowSaveFileDialog(DialogCallback, ScopedRequest, static_cast<SDL_Window*>(MWindow),
                           ScopedRequest->filters.empty() ? nullptr : ScopedRequest->filters.data(),
                           static_cast<int>(ScopedRequest->filters.size()),
                           ScopedRequest->defaultLocation.empty() ? nullptr
                                                                  : ScopedRequest->defaultLocation.c_str());
    // A save dialog's default name is supplied through the default location on
    (void)InDefaultName;
    (void)InTitle;
}

void WindowManager::ShowOpenFolder(std::string_view InTitle, DialogResult InOnResult,
                                   std::string_view InDefaultLocation)
{
    if (MWindow == nullptr)
    {
        return;
    }
    DialogRequest* ScopedRequest = MakeRequest({}, std::move(InOnResult));
    ScopedRequest->defaultLocation = std::string(InDefaultLocation);
    SDL_ShowOpenFolderDialog(DialogCallback, ScopedRequest, static_cast<SDL_Window*>(MWindow),
                             ScopedRequest->defaultLocation.empty() ? nullptr
                                                                   : ScopedRequest->defaultLocation.c_str(),
                             false);
    (void)InTitle;
}
}
