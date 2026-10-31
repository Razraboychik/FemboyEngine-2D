// FemboyEngine2D - window, input and native dialogs.
#pragma once

#include "Engine/Core/Types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Fby
{

inline constexpr std::size_t kMaxScancodes = 512;
inline constexpr std::size_t kMaxMouseButtons = 8;

struct WindowExtent
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    [[nodiscard]] constexpr bool operator==(const WindowExtent&) const = default;
};

// Key codes follow SDL's scancode numbering (physical key positions), so WASD
enum class Key : std::uint16_t
{
    Unknown = 0,
    A = 4,
    B,
    C,
    D,
    E,
    F,
    G,
    H,
    I,
    J,
    K,
    L,
    M,
    N,
    O,
    P,
    Q,
    R,
    S,
    T,
    U,
    V,
    W,
    X,
    Y,
    Z,
    Num1 = 30,
    Num2,
    Num3,
    Num4,
    Num5,
    Num6,
    Num7,
    Num8,
    Num9,
    Num0,
    Return = 40,
    Escape,
    Backspace,
    Tab,
    Space,
    F1 = 58,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12,
    Left = 80,
    Right,
    Down,
    Up,
    LeftShift = 225,
    RightShift = 229,
    LeftCtrl = 224,
    RightCtrl = 228,
    LeftAlt = 226,
    RightAlt = 230,
};

struct InputState
{
    std::array<bool, kMaxScancodes> held{};
    std::array<bool, kMaxScancodes> pressed{};
    std::array<bool, kMaxScancodes> released{};
    std::array<bool, kMaxMouseButtons> mouseHeld{};
    std::array<bool, kMaxMouseButtons> mousePressed{};

    // Client-space pixels
    Vector2D mouse{};
    // Pixels since the previous frame
    Vector2D mouseDelta{};
    // Vertical scroll this frame
    float wheel = 0.0f;
    bool quitRequested = false;

    // Committed UTF-8 from SDL_TEXT_INPUT this frame. The app feeds it to the
    // focused text field; composition stays visible through the fields below.
    std::array<char, 64> textInput{};
    std::uint8_t textInputLength = 0;
    // IME pre-edit string and its cursor. `compositionActive` is false when the
    // platform has no active composition.
    std::array<char, 256> composition{};
    std::uint32_t compositionLength = 0;
    std::uint32_t compositionCursor = 0;
    bool compositionActive = false;

    [[nodiscard]] bool IsDown(Key InKey) const noexcept
    {
        return held[static_cast<std::size_t>(InKey)];
    }
    [[nodiscard]] bool WasPressed(Key InKey) const noexcept
    {
        return pressed[static_cast<std::size_t>(InKey)];
    }
    [[nodiscard]] bool WasReleased(Key InKey) const noexcept
    {
        return released[static_cast<std::size_t>(InKey)];
    }
    [[nodiscard]] bool IsMouseDown(std::uint8_t InButton) const noexcept
    {
        return InButton < mouseHeld.size() && mouseHeld[InButton];
    }

    // Called by WindowManager once per PumpEvents(): pressed/released and the
    void BeginFrame() noexcept
    {
        pressed.fill(false);
        released.fill(false);
        mousePressed.fill(false);
        mouseDelta = {};
        wheel = 0.0f;
        textInputLength = 0;
    }
};

struct FileFilter
{
    // "Scene"
    std::string_view name;
    // SDL takes a semicolon-separated list of extensions without dots or globs,
    // for example "fescene;toml"; "*" is the "all files" filter.
    std::string_view pattern;
};

// How the OS presents the window. Borderless fullscreen is the editor default:
// it covers the whole display like exclusive fullscreen but switches instantly,
// keeps alt-tab/overlays working and needs no display-mode change.
enum class WindowMode : std::uint8_t
{
    Windowed = 0,
    BorderlessFullscreen = 1,
    ExclusiveFullscreen = 2,
};

[[nodiscard]] const char* WindowModeName(WindowMode InMode) noexcept;

struct WindowDesc
{
    std::string title = "FemboyEngine";
    std::uint32_t width = 1600;
    std::uint32_t height = 900;
    bool resizable = true;
    bool highDpi = true;
    bool startMaximized = false;
    WindowMode mode = WindowMode::BorderlessFullscreen;
};

class WindowManager
{
public:
    WindowManager();
    ~WindowManager();

    WindowManager(const WindowManager&) = delete;
    WindowManager& operator=(const WindowManager&) = delete;

    // Initialises SDL's video subsystem. Safe to call once per process.
    [[nodiscard]] bool Initialize();
    [[nodiscard]] bool Create(const WindowDesc& InDesc);
    void Destroy();

    // Drain the OS event queue into the input snapshot. Returns false when the
    bool PumpEvents();

    // Optional observer for a backend that needs the raw event stream (ImGui's
    // own message hook). A plain function pointer plus context avoids a heap
    // allocation on the per-event path; the observer runs on the main thread.
    using EventObserver = void (*)(void* InUser, const void* InSdlEvent);
    void SetEventObserver(EventObserver InObserver, void* InUser = nullptr);
    void ClearEventObserver();

    [[nodiscard]] bool ShouldClose() const noexcept;
    [[nodiscard]] bool IsMinimized() const noexcept;
    // Client size in pixels (not points): this is what the swapchain uses.
    [[nodiscard]] WindowExtent Size() const noexcept;
    // SDL_GetWindowDisplayScale() for the window's current monitor: 1.0 is a
    [[nodiscard]] float DisplayScale() const noexcept;
    [[nodiscard]] const InputState& Input() const noexcept
    {
        return MInput;
    }

    // Opaque bridge for the RHI. `NativeWindow()` returns SDL_Window*.
    [[nodiscard]] void* NativeWindow() const noexcept;
    // Vulkan instance extensions required by the window system. Empty when the
    [[nodiscard]] std::span<const char* const> VulkanInstanceExtensions() const;
    // Creates the platform surface for `instance` (VkInstance). The result is
    [[nodiscard]] bool CreateVulkanSurface(const void* InInstance, std::uint64_t* InSurface) const;
    // macOS/iOS: returns the CAMetalLayer* the Metal backend must draw into.
    [[nodiscard]] void* MetalLayer() const noexcept;

    void SetTitle(std::string_view InTitle);
    // Legacy helper: keeps the old borderless semantics.
    void SetFullscreen(bool InFullscreen);
    // Applies a presentation mode now. Returns false when SDL rejects it; the
    // previous mode stays active in that case.
    [[nodiscard]] bool SetWindowMode(WindowMode InMode) noexcept;
    [[nodiscard]] WindowMode Mode() const noexcept
    {
        return MMode;
    }
    void SetVsyncHint(bool InEnabled);

    // Both are asynchronous: SDL runs the dialog on its own thread and delivers
    using DialogResult = std::function<void(std::vector<std::string> InPaths)>;

    void ShowOpenFile(std::string_view InTitle, std::span<const FileFilter> InFilters, DialogResult InOnResult,
                      std::string_view InDefaultLocation = {}, bool InAllowMultiple = false);
    void ShowSaveFile(std::string_view InTitle, std::span<const FileFilter> InFilters, DialogResult InOnResult,
                      std::string_view InDefaultLocation = {}, std::string_view InDefaultName = {});
    // Native folder picker. `InDefaultLocation` starts the dialog in that folder;
    // the result receives the chosen directory path.
    void ShowOpenFolder(std::string_view InTitle, DialogResult InOnResult, std::string_view InDefaultLocation = {});

private:
    // One raw SDL transition. SetWindowMode wraps this with the commit/rollback
    // of the presentation mode.
    [[nodiscard]] bool ApplyWindowMode(WindowMode InMode) noexcept;

    void* MWindow = nullptr;
    InputState MInput{};
    EventObserver MEventObserver = nullptr;
    void* MEventObserverUser = nullptr;
    bool MInitialized = false;
    bool MShouldClose = false;
    bool MMinimized = false;
    bool MVsync = true;
    WindowMode MMode = WindowMode::Windowed;
    WindowExtent MSize{};
};
}
