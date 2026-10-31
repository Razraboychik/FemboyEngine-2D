// FemboyEngine2D - editor typography from the VFS mmap.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

struct ImFont;
struct ImGuiIO;

namespace Fby
{
class VirtualFileSystem;
class WindowManager;
}

namespace Fby::Editor
{

enum class EditorFontRole : std::uint8_t
{
    Regular = 0,
    Small,
    Mono,
    Bold,
    PanelTitle,
    Tooltip,
    ScriptNodeTitle,
    ScriptPinInput,
    ScriptPinOutput,
    SceneText,
    Count,
};

// Logical (layout) pixel sizes. The rasterizer multiplies these by the physical
struct EditorFontSpec
{
    float regularPx = 18.0f;
    float smallPx = 17.0f;
    float monoPx = 17.0f;
    float boldPx = 18.0f;
    float panelTitlePx = 24.0f;
    float tooltipPx = 18.0f;
    float scriptNodeTitlePx = 21.0f;
    float scriptPinInputPx = 18.0f;
    float scriptPinOutputPx = 18.0f;
    float sceneTextPx = 18.0f;

    // One family for the whole UI: IBM Plex Sans for text and IBM Plex Mono
    // for code, both under the same SIL OFL licence as the text pipeline family.
    std::string regularPath = "fonts/IBMPlexSans-Regular.ttf";
    std::string boldPath = "fonts/IBMPlexSans-Bold.ttf";
    std::string monoPath = "fonts/IBMPlexMono-Regular.ttf";
    std::string bakedInterAtlas = "fonts/Inter.fbyfont";

    // UI scale from EditorSettings. Applied through ImGuiStyle::FontScaleMain,
    float uiScale = 1.0f;
    // True when the editor is allowed to draw the embedded vector font if the
    bool allowEmbeddedVectorFallback = true;

    [[nodiscard]] friend bool operator==(const EditorFontSpec&, const EditorFontSpec&) noexcept = default;
};

struct EditorFontStatus
{
    std::uint32_t regularBytes = 0;
    std::uint32_t monoBytes = 0;
    std::uint32_t bakedGlyphs = 0;
    float displayScale = 1.0f;
    float bakedLineHeight = 0.0f;
    bool loadedFromVfs = false;
    bool embeddedVectorFallback = false;
    bool bakedAtlasPresent = false;
    bool bakedAtlasValid = false;
};

class EditorFonts
{
public:
    static constexpr std::uint32_t kRoleCount = static_cast<std::uint32_t>(EditorFontRole::Count);

    EditorFonts() = default;

    // Idempotent. Safe to call from both the dashboard and EditorSystem.
    bool Initialize(ImGuiIO& InIo, VirtualFileSystem* InVfs, WindowManager* InWindow, const EditorFontSpec& InSpec);

    // Re-reads SDL_GetWindowDisplayScale() and logs a DPI transition. Glyphs are
    void RefreshDisplayScale(ImGuiIO& InIo, WindowManager* InWindow);

    // Applies the UI scale through the supported 1.92+ path.
    void SetUiScale(float InScale) noexcept;
    // Replaces the logical pixel sizes used by the next ReloadFonts().
    void SetSpec(const EditorFontSpec& InSpec) noexcept;

    // Rebuilds the ImGui atlas in place. The stable TTF copies are dropped, the
    [[nodiscard]] bool ReloadFonts(ImGuiIO& InIo);

    // Detection pass only: stats/reloads the VFS mapping but does NOT rebuild the
    [[nodiscard]] bool DetectFontChanges() noexcept;

    [[nodiscard]] std::uint32_t Generation() const noexcept
    {
        return MGeneration;
    }

    [[nodiscard]] bool Initialized() const noexcept
    {
        return MInitialized;
    }
    [[nodiscard]] float DisplayScale() const noexcept
    {
        return MStatus.displayScale;
    }
    [[nodiscard]] ImFont* Font(EditorFontRole InRole) const noexcept;
    [[nodiscard]] ImFont* Regular() const noexcept;
    [[nodiscard]] ImFont* Small() const noexcept;
    [[nodiscard]] ImFont* Mono() const noexcept;
    [[nodiscard]] ImFont* Bold() const noexcept;
    [[nodiscard]] ImFont* PanelTitle() const noexcept;
    [[nodiscard]] ImFont* Tooltip() const noexcept;
    [[nodiscard]] ImFont* ScriptNodeTitle() const noexcept;
    [[nodiscard]] ImFont* ScriptPinInput() const noexcept;
    [[nodiscard]] ImFont* ScriptPinOutput() const noexcept;
    [[nodiscard]] ImFont* SceneText() const noexcept;
    [[nodiscard]] const EditorFontStatus& Status() const noexcept
    {
        return MStatus;
    }

private:
    struct LoadedSource
    {
        std::uint32_t bytes = 0;
        bool ok = false;
    };

    // A stable copy of one font file. ImGui keeps the pointer for the lifetime
    struct StableSource
    {
        std::string path;
        std::vector<std::byte> bytes;
        std::uint64_t sourceHash = 0;
        std::size_t sourceSize = 0;
        std::int64_t sourceWriteTicks = 0;
        bool hasOsStat = false;
    };

    ImFont* LoadOne(ImGuiIO& InIo, VirtualFileSystem* InVfs, std::string_view InPath, float InLogicalPx,
                    LoadedSource* OutSource);
    [[nodiscard]] const std::byte* StableFontBytes(VirtualFileSystem* InVfs, std::string_view InPath,
                                                   std::size_t* OutSize);
    void ApplyUiScale() const;
    void ProbeBakedAtlas(VirtualFileSystem* InVfs, std::string_view InPath);

    std::array<ImFont*, kRoleCount> MFonts{};
    std::array<LoadedSource, kRoleCount> MSources{};
    std::array<StableSource, 4> MStableSources{};
    std::uint32_t MStableCount = 0;
    VirtualFileSystem* MVfs = nullptr;
    WindowManager* MWindow = nullptr;
    EditorFontSpec MSpec{};
    EditorFontStatus MStatus{};
    std::uint64_t MGeneration = 0;
    double MLastPollSeconds = -1.0e9;
    ImGuiIO* MIO = nullptr;
    float MSpecUiScale = 1.0f;
    bool MDynamicTextures = true;
    bool MInitialized = false;
};
}
