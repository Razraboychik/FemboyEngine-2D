// FemboyEngine2D - editor typography implementation.
#include "Engine/Core/Time.hpp"
#include "Editor/Infrastructure/EditorFonts.hpp"

#include "Engine/Assets/VFS.hpp"
#include "Engine/Core/Log.hpp"
#include "Engine/Core/WindowManager.hpp"
#include "Engine/UI/FontAtlas.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <span>
#include <utility>

namespace Fby::Editor
{
namespace
{
constexpr float kMinUiScale = 0.75f;
constexpr float kMaxUiScale = 3.0f;

[[nodiscard]] const char* RoleName(EditorFontRole InRole) noexcept
{
    switch (InRole)
    {
        case EditorFontRole::Regular:
            return "regular";
        case EditorFontRole::Small:
            return "small";
        case EditorFontRole::Mono:
            return "mono";
        case EditorFontRole::Bold:
            return "bold";
        case EditorFontRole::PanelTitle:
            return "panel title";
        case EditorFontRole::Tooltip:
            return "tooltip";
        case EditorFontRole::ScriptNodeTitle:
            return "script node title";
        case EditorFontRole::ScriptPinInput:
            return "script input pin";
        case EditorFontRole::ScriptPinOutput:
            return "script output pin";
        case EditorFontRole::SceneText:
            return "scene text";
        case EditorFontRole::Count:
            break;
    }
    return "unknown";
}

[[nodiscard]] float ClampUiScale(float InScale) noexcept
{
    if (!std::isfinite(InScale))
    {
        return 1.0f;
    }
    return std::clamp(InScale, kMinUiScale, kMaxUiScale);
}

// Cheap change detector for a font file: header and trailer plus the size.
[[nodiscard]] std::uint64_t HashFontBytes(std::span<const std::byte> InBytes) noexcept
{
    std::uint64_t ScopedHash = 14695981039346656037ull;
    const auto ScopedFeed = [&ScopedHash](std::byte InByte)
    {
        ScopedHash ^= static_cast<std::uint64_t>(InByte);
        ScopedHash *= 1099511628211ull;
    };
    const std::size_t ScopedHead = std::min<std::size_t>(InBytes.size(), 64);
    for (std::size_t ScopedIndex = 0; ScopedIndex < ScopedHead; ++ScopedIndex)
    {
        ScopedFeed(InBytes[ScopedIndex]);
    }
    for (std::size_t ScopedIndex = 0; ScopedIndex < 8; ++ScopedIndex)
    {
        ScopedFeed(static_cast<std::byte>((InBytes.size() >> (ScopedIndex * 8u)) & 0xFFu));
    }
    const std::size_t ScopedTail = std::min<std::size_t>(InBytes.size(), 64);
    for (std::size_t ScopedIndex = 0; ScopedIndex < ScopedTail; ++ScopedIndex)
    {
        ScopedFeed(InBytes[InBytes.size() - 1 - ScopedIndex]);
    }
    return ScopedHash;
}
}

ImFont* EditorFonts::Font(EditorFontRole InRole) const noexcept
{
    const auto ScopedIndex = static_cast<std::size_t>(InRole);
    if (ScopedIndex >= MFonts.size())
    {
        return MFonts[0];
    }
    return MFonts[ScopedIndex] != nullptr ? MFonts[ScopedIndex] : MFonts[0];
}

ImFont* EditorFonts::Regular() const noexcept
{
    return Font(EditorFontRole::Regular);
}

ImFont* EditorFonts::Small() const noexcept
{
    return Font(EditorFontRole::Small);
}

ImFont* EditorFonts::Mono() const noexcept
{
    return Font(EditorFontRole::Mono);
}

ImFont* EditorFonts::Bold() const noexcept
{
    return Font(EditorFontRole::Bold);
}

ImFont* EditorFonts::PanelTitle() const noexcept
{
    return Font(EditorFontRole::PanelTitle);
}

ImFont* EditorFonts::Tooltip() const noexcept
{
    return Font(EditorFontRole::Tooltip);
}

ImFont* EditorFonts::ScriptNodeTitle() const noexcept
{
    return Font(EditorFontRole::ScriptNodeTitle);
}

ImFont* EditorFonts::ScriptPinInput() const noexcept
{
    return Font(EditorFontRole::ScriptPinInput);
}

ImFont* EditorFonts::ScriptPinOutput() const noexcept
{
    return Font(EditorFontRole::ScriptPinOutput);
}

ImFont* EditorFonts::SceneText() const noexcept
{
    return Font(EditorFontRole::SceneText);
}

bool EditorFonts::Initialize(ImGuiIO& InIo, VirtualFileSystem* InVfs, WindowManager* InWindow,
                             const EditorFontSpec& InSpec)
{
    MIO = &InIo;
    MVfs = InVfs;
    MWindow = InWindow;
    MSpec = InSpec;
    MSpecUiScale = ClampUiScale(InSpec.uiScale);
    MStatus.displayScale = InWindow != nullptr ? InWindow->DisplayScale() : 1.0f;
    if (!(MStatus.displayScale > 0.0f))
    {
        MStatus.displayScale = 1.0f;
    }
    if (MInitialized)
    {
        SetUiScale(MSpecUiScale);
        return MFonts[0] != nullptr;
    }

    MDynamicTextures = (InIo.BackendFlags & ImGuiBackendFlags_RendererHasTextures) != 0;

    const auto ScopedLoad = [this, &InIo, InVfs](EditorFontRole InRole, std::string_view InPath, float InPixels)
    {
        const auto ScopedIndex = static_cast<std::size_t>(InRole);
        LoadedSource& ScopedSource = MSources[ScopedIndex];
        MFonts[ScopedIndex] = LoadOne(InIo, InVfs, InPath, InPixels, &ScopedSource);
        const bool ScopedOk = MFonts[ScopedIndex] != nullptr;
        if (ScopedOk)
        {
            if (InRole == EditorFontRole::Regular)
            {
                MStatus.regularBytes = ScopedSource.bytes;
            }
            else if (InRole == EditorFontRole::Mono)
            {
                MStatus.monoBytes = ScopedSource.bytes;
            }
            LogInfo("fonts", "{}: {:.1f} logical px from '{}' ({} bytes, mmap, {:.2f}x density)", RoleName(InRole),
                    static_cast<double>(InPixels), InPath, ScopedSource.bytes,
                    static_cast<double>(MDynamicTextures ? 1.0f : MStatus.displayScale));
        }
        else if (!InPath.empty() && InRole != EditorFontRole::Bold)
        {
            LogWarn("fonts", "{}: '{}' is not in the VFS mount", RoleName(InRole), InPath);
        }
        return ScopedOk;
    };

    (void)ScopedLoad(EditorFontRole::Regular, InSpec.regularPath, InSpec.regularPx);
    (void)ScopedLoad(EditorFontRole::Small, InSpec.regularPath, InSpec.smallPx);
    (void)ScopedLoad(EditorFontRole::Mono, InSpec.monoPath, InSpec.monoPx);
    (void)ScopedLoad(EditorFontRole::Bold, InSpec.boldPath, InSpec.boldPx);
    (void)ScopedLoad(EditorFontRole::PanelTitle, InSpec.regularPath, InSpec.panelTitlePx);
    (void)ScopedLoad(EditorFontRole::Tooltip, InSpec.regularPath, InSpec.tooltipPx);
    (void)ScopedLoad(EditorFontRole::ScriptNodeTitle, InSpec.regularPath, InSpec.scriptNodeTitlePx);
    (void)ScopedLoad(EditorFontRole::ScriptPinInput, InSpec.regularPath, InSpec.scriptPinInputPx);
    (void)ScopedLoad(EditorFontRole::ScriptPinOutput, InSpec.regularPath, InSpec.scriptPinOutputPx);
    (void)ScopedLoad(EditorFontRole::SceneText, InSpec.regularPath, InSpec.sceneTextPx);

    if (MStatus.regularBytes > 0)
    {
        MStatus.loadedFromVfs = true;
    }

    if (MFonts[0] == nullptr)
    {
        // The dashboard must never fall back to the 13 px bitmap font: on a
        if (!InSpec.allowEmbeddedVectorFallback)
        {
            LogError("fonts", "no usable UI font in the VFS and the embedded fallback is disabled");
            return false;
        }
        MFonts[0] = InIo.Fonts->AddFontDefaultVector();
        MStatus.embeddedVectorFallback = true;
        for (std::size_t ScopedRole = 0; ScopedRole < MFonts.size(); ++ScopedRole)
        {
            if (MFonts[ScopedRole] == nullptr)
            {
                MFonts[ScopedRole] = MFonts[0];
            }
        }
        LogError("fonts", "Inter is missing from the VFS; using the embedded vector font (UI text will not match "
                          "the project typography)");
    }

    InIo.FontDefault = MFonts[0];
    ApplyUiScale();
    ProbeBakedAtlas(InVfs, InSpec.bakedInterAtlas);
    MInitialized = true;

    LogInfo("fonts", "editor typography ready: {:.2f}x display scale, {:.2f}x UI scale, {} role(s), dynamic atlas {}",
            static_cast<double>(MStatus.displayScale), static_cast<double>(MSpecUiScale),
            static_cast<unsigned>(MFonts.size()), MDynamicTextures ? "yes" : "no (legacy density)");
    return MFonts[0] != nullptr;
}

const std::byte* EditorFonts::StableFontBytes(VirtualFileSystem* InVfs, std::string_view InPath,
                                              std::size_t* OutSize)
{
    if (OutSize != nullptr)
    {
        *OutSize = 0;
    }
    if (InVfs == nullptr || InPath.empty())
    {
        return nullptr;
    }
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MStableCount; ++ScopedIndex)
    {
        if (MStableSources[ScopedIndex].path == InPath)
        {
            if (OutSize != nullptr)
            {
                *OutSize = MStableSources[ScopedIndex].bytes.size();
            }
            return MStableSources[ScopedIndex].bytes.data();
        }
    }
    const auto ScopedBytes = InVfs->Read(InPath);
    if (!ScopedBytes || ScopedBytes->empty() || MStableCount >= MStableSources.size())
    {
        return nullptr;
    }
    StableSource& ScopedSource = MStableSources[MStableCount];
    ScopedSource.path.assign(InPath);
    ScopedSource.bytes.assign(ScopedBytes->begin(), ScopedBytes->end());
    ScopedSource.sourceSize = ScopedSource.bytes.size();
    ScopedSource.sourceHash = HashFontBytes(
        std::span<const std::byte>(ScopedSource.bytes.data(), ScopedSource.bytes.size()));
    if (const auto ScopedWritable = InVfs->ResolveWritablePath(InPath); ScopedWritable.has_value())
    {
        std::error_code ScopedCode;
        const std::filesystem::path ScopedPath(*ScopedWritable);
        const auto ScopedWriteTime = std::filesystem::last_write_time(ScopedPath, ScopedCode);
        if (!ScopedCode)
        {
            ScopedSource.sourceWriteTicks =
                static_cast<std::int64_t>(ScopedWriteTime.time_since_epoch().count());
            ScopedSource.hasOsStat = true;
        }
    }
    ++MStableCount;
    LogInfo("fonts", "font '{}' copied into a stable buffer ({} bytes); VFS hot reload cannot unmap it", InPath,
            ScopedSource.bytes.size());
    if (OutSize != nullptr)
    {
        *OutSize = ScopedSource.bytes.size();
    }
    return ScopedSource.bytes.data();
}

ImFont* EditorFonts::LoadOne(ImGuiIO& InIo, VirtualFileSystem* InVfs, std::string_view InPath, float InLogicalPx,
                             LoadedSource* OutSource)
{
    if (InVfs == nullptr || InPath.empty())
    {
        return nullptr;
    }
    std::size_t ScopedByteCount = 0;
    const std::byte* ScopedBytes = StableFontBytes(InVfs, InPath, &ScopedByteCount);
    if (ScopedBytes == nullptr || ScopedByteCount == 0)
    {
        return nullptr;
    }
    if (ScopedByteCount > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    {
        LogError("fonts", "'{}' is too large for the ImGui atlas ({} bytes)", InPath, ScopedByteCount);
        return nullptr;
    }

    ImFontConfig ScopedConfig;
    // The stable copy is owned by EditorFonts; the atlas must not free it.
    ScopedConfig.FontDataOwnedByAtlas = false;
    // Subpixel: fractional advances are kept, so text is positioned with the
    ScopedConfig.PixelSnapH = false;
    // 0 means "auto" (2-3 horizontally, 1 vertically), which is what ImGui
    ScopedConfig.OversampleH = 0;
    ScopedConfig.OversampleV = 1;
    ScopedConfig.RasterizerMultiply = 1.0f;
    // On a dynamic atlas ImGui bakes at the viewport pixel density itself;
    ScopedConfig.RasterizerDensity = MDynamicTextures ? 1.0f : MStatus.displayScale;

    const ImWchar* ScopedRanges = InIo.Fonts->GetGlyphRangesCyrillic();
    ImFont* ScopedFont = InIo.Fonts->AddFontFromMemoryTTF(const_cast<std::byte*>(ScopedBytes),
                                                          static_cast<int>(ScopedByteCount), InLogicalPx,
                                                          &ScopedConfig, ScopedRanges);
    if (ScopedFont == nullptr)
    {
        LogError("fonts", "ImGui rejected '{}' ({} bytes)", InPath, ScopedByteCount);
        return nullptr;
    }
    if (OutSource != nullptr)
    {
        OutSource->bytes = static_cast<std::uint32_t>(ScopedByteCount);
        OutSource->ok = true;
    }
    return ScopedFont;
}

void EditorFonts::ApplyUiScale() const
{
    if (MIO == nullptr)
    {
        return;
    }
    ImGuiStyle& ScopedStyle = ImGui::GetStyle();
    if (MDynamicTextures)
    {
        // 1.92+ global scale factors. io.FontGlobalScale is the deprecated path
        ScopedStyle.FontScaleMain = MSpecUiScale;
        MIO->FontGlobalScale = 1.0f;
        return;
    }
    // Legacy density path: glyphs were baked at `displayScale` px and must be
    ScopedStyle.FontScaleMain = 1.0f;
    const float ScopedDensity = MStatus.displayScale > 0.0f ? MStatus.displayScale : 1.0f;
    MIO->FontGlobalScale = MSpecUiScale / ScopedDensity;
}

void EditorFonts::SetSpec(const EditorFontSpec& InSpec) noexcept
{
    MSpec = InSpec;
    MSpecUiScale = ClampUiScale(InSpec.uiScale);
}

void EditorFonts::SetUiScale(float InScale) noexcept
{
    MSpecUiScale = ClampUiScale(InScale);
    if (MIO != nullptr)
    {
        ApplyUiScale();
    }
}

bool EditorFonts::ReloadFonts(ImGuiIO& InIo)
{
    if (!MInitialized || MIO != &InIo)
    {
        return false;
    }
    // Drop the stable copies first: they are the old bytes. LoadOne() will copy
    for (StableSource& ScopedSource : MStableSources)
    {
        ScopedSource.bytes.clear();
        ScopedSource.path.clear();
        ScopedSource.sourceHash = 0;
        ScopedSource.sourceSize = 0;
    }
    MStableCount = 0;
    MFonts.fill(nullptr);
    MSources.fill(LoadedSource{});
    MStatus = EditorFontStatus{};
    MInitialized = false;

    // Clear everything (fonts + dynamic textures). The Vulkan backend implements
    InIo.Fonts->Clear();
    ++MGeneration;
    const bool ScopedOk = Initialize(InIo, MVfs, MWindow, MSpec);
    LogInfo("fonts", "atlas reloaded (generation {}): {} role(s), up to {:.1f} px",
            static_cast<unsigned>(MGeneration), MFonts.size(),
            static_cast<double>(MSpec.regularPx));
    return ScopedOk;
}

bool EditorFonts::DetectFontChanges() noexcept
{
    if (!MInitialized || MVfs == nullptr)
    {
        return false;
    }
    const double ScopedSeconds = Fby::Core::Clock::NowSeconds();
    if (ScopedSeconds - MLastPollSeconds < 1.0)
    {
        return false;
    }
    MLastPollSeconds = ScopedSeconds;

    bool ScopedChanged = false;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MStableCount; ++ScopedIndex)
    {
        StableSource& ScopedSource = MStableSources[ScopedIndex];
        bool ScopedOsChanged = false;
        if (const auto ScopedWritable = MVfs->ResolveWritablePath(ScopedSource.path); ScopedWritable.has_value())
        {
            const std::filesystem::path ScopedPath(*ScopedWritable);
            std::error_code ScopedCode;
            const auto ScopedWriteTime = std::filesystem::last_write_time(ScopedPath, ScopedCode);
            if (!ScopedCode)
            {
                const std::int64_t ScopedTicks =
                    static_cast<std::int64_t>(ScopedWriteTime.time_since_epoch().count());
                if (ScopedSource.hasOsStat && ScopedTicks != ScopedSource.sourceWriteTicks)
                {
                    ScopedOsChanged = true;
                }
                ScopedSource.sourceWriteTicks = ScopedTicks;
                ScopedSource.hasOsStat = true;
            }
        }
        const auto ScopedBytes = MVfs->Read(ScopedSource.path);
        const bool ScopedHashChanged =
            ScopedBytes.has_value() && !ScopedBytes->empty() &&
            (HashFontBytes(*ScopedBytes) != ScopedSource.sourceHash ||
             ScopedBytes->size() != ScopedSource.sourceSize);
        if (!ScopedOsChanged && !ScopedHashChanged)
        {
            continue;
        }
        // The override is documented as replacing the mapping; it is the safe
        std::string ScopedError;
        if (!MVfs->ReloadFile(ScopedSource.path, &ScopedError) && !ScopedHashChanged)
        {
            continue;
        }
        ScopedChanged = true;
        break;
    }
    return ScopedChanged;
}

void EditorFonts::RefreshDisplayScale(ImGuiIO& InIo, WindowManager* InWindow)
{
    (void)InIo;
    if (InWindow == nullptr)
    {
        return;
    }
    const float ScopedScale = InWindow->DisplayScale();
    if (!(ScopedScale > 0.0f) || std::abs(ScopedScale - MStatus.displayScale) < 0.01f)
    {
        return;
    }
    MStatus.displayScale = ScopedScale;
    ApplyUiScale();
    LogInfo("fonts", "display scale changed to {:.2f}x; ImGui re-bakes glyphs at the new pixel density",
            static_cast<double>(ScopedScale));
}

void EditorFonts::ProbeBakedAtlas(VirtualFileSystem* InVfs, std::string_view InPath)
{
    if (InVfs == nullptr || InPath.empty())
    {
        return;
    }
    const auto ScopedBytes = InVfs->Read(InPath);
    if (!ScopedBytes || ScopedBytes->empty())
    {
        // The game UI may still use the vector path; absence is not an error.
        return;
    }
    MStatus.bakedAtlasPresent = true;
    Ui::FontAtlas ScopedAtlas;
    if (!ScopedAtlas.Open(*ScopedBytes))
    {
        LogWarn("fonts", "'{}' is present but its FB YFNT header is invalid; the game UI will reject it", InPath);
        return;
    }
    MStatus.bakedAtlasValid = true;
    MStatus.bakedGlyphs = ScopedAtlas.GlyphCount();
    MStatus.bakedLineHeight = ScopedAtlas.LineHeight();
    LogInfo("fonts", "PinkUI atlas '{}': {} glyphs, {}x{}, line height {:.2f} px (ImGui still uses the vector TTF)",
            InPath, ScopedAtlas.GlyphCount(), ScopedAtlas.AtlasWidth(), ScopedAtlas.AtlasHeight(),
            static_cast<double>(ScopedAtlas.LineHeight()));
}
}
