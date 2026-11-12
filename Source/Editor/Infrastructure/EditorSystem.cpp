#include "Editor/Infrastructure/EditorSystem.hpp"

#include "Engine/RHI/CameraStack.hpp"

#include "Engine/Audio/Audio.hpp"
#include "Engine/Audio/VoicePreset.hpp"
#include "Engine/Audio/VoiceReactiveSfx.hpp"
#include "Engine/Audio/VoiceSfxSynth.hpp"
#include "Engine/Audio/VoiceSystem.hpp"
#include "Engine/Core/PluginHost.hpp"
#include "Engine/UI/Accessibility.hpp"
#include "Engine/Text/TextService.hpp"
#include "Engine/Assets/VFS.hpp"
#include "Engine/Core/Log.hpp"
#include "Engine/Core/Profiler.hpp"
#include "Engine/Core/ProfilerPanel.hpp"
#include "Engine/ECS/Skeleton.hpp"
#include "Engine/ECS/Lighting.hpp"
#include "Engine/ECS/LightSource.hpp"
#include "Engine/ECS/Presentation.hpp"
#include "Engine/Text/TextFormat.hpp"
#include "Engine/ECS/Tilemap.hpp"
#include "Engine/Navigation/Navigation.hpp"
#include "Engine/Physics/SdfComposerSession.hpp"
#include "Editor/Panels/AIStateTreeCanvasPanel.hpp"
#include "Editor/Panels/LevelSettingsPanel.hpp"
#include "Engine/Gameplay/GameplayAttributes.hpp"
#include "Editor/Panels/AnimationGraphCanvasPanel.hpp"
#include "Editor/Panels/AnimationTimelinePanel.hpp"
#include "Editor/Panels/DirectionalBlendSpacePanel.hpp"
#include "Editor/Infrastructure/AssetDatabaseWatchSystem.hpp"
#include "Editor/Infrastructure/AssetImportService.hpp"
#include "Editor/Infrastructure/CommandStack.hpp"
#include "Editor/Panels/ContentBrowserPanel.hpp"
#include "Editor/Infrastructure/EditTransactions.hpp"
#include "Editor/Infrastructure/EditorFonts.hpp"
#include "Editor/Infrastructure/EditorTypography.hpp"
#include "Editor/Infrastructure/EditorTheme.hpp"
#include "Editor/Infrastructure/EditorWidgets.hpp"
#include "Editor/Panels/LogBoyPanel.hpp"
#include "Editor/Panels/LicensesPanel.hpp"
#include "Editor/Panels/NetworkDiagnosticsPanel.hpp"
#include "Editor/Panels/PieClientViewport.hpp"
#include "Editor/Panels/ParticleFxPanel.hpp"
#include "Editor/Panels/VoiceDevicePanel.hpp"
#include "Editor/Panels/MaterialGraphPanel.hpp"
#include "Editor/Infrastructure/PlayInEditor.hpp"
#include "Editor/Infrastructure/PhysicsDebugDraw.hpp"
#include "Editor/Infrastructure/ProjectConfig.hpp"
#include "Editor/Infrastructure/UserSettings.hpp"
#include "Editor/Infrastructure/ResolvedRenderSettings.hpp"
#include "Editor/Panels/UserSettingsPanel.hpp"
#include "Editor/Infrastructure/ReleaseCooker.hpp"
#include "Editor/Infrastructure/TextureThumbnails.hpp"
#include "Editor/Panels/ProjectDashboard.hpp"
#include "Editor/Panels/SdfCharacterPanel.hpp"
#include "Editor/Panels/SceneDiagnosticsPanel.hpp"
#include "Editor/Panels/ScriptDebuggerPanel.hpp"
#include "Editor/Panels/SettingsPanels.hpp"
#include "Editor/Panels/TilemapEditorPanel.hpp"
#include "Editor/Panels/TransportGroupPanel.hpp"
#include "Editor/Panels/VirtualTexturePanel.hpp"

#include "Engine/RHI/VirtualTextureSystem.hpp"
#include "Engine/RHI/ViewportModes.hpp"
#include "Editor/Panels/TextPreviewPanel.hpp"
#include "Editor/Panels/VisualScriptPanel.hpp"
#include "Editor/Panels/SkeletonAssetPanel.hpp"
#include "Editor/Panels/SpriteAnimationPanel.hpp"
#include "Engine/Core/WindowManager.hpp"

#include <SDL3/SDL.h>

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>
#include <ImGuizmo.h>
#include <implot.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <limits>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace Fby::Editor
{
namespace
{
// Fixed PIE snapshot arena.
inline constexpr std::size_t kPieArenaBytes = 32u * 1024u * 1024u;
// Bump when the default dock layout changes materially (new panel, new split).
// The marker next to the ImGui ini then forces one rebuild, so an old saved
// layout cannot hide the Viewport behind stale tabs.
inline constexpr std::uint32_t kEditorLayoutVersion = 2;
[[nodiscard]] std::string EntityLabel(Entity InEntity)
{
    return std::format("Entity {}", Fby::ToIntegral(InEntity));
}

[[nodiscard]] std::string EntityLabel(const Ecs::Registry& InRegistry, Entity InEntity)
{
    if (const auto* ScopedName = InRegistry.try_get<NameComponent>(InEntity);
        ScopedName != nullptr && ScopedName->Value[0] != '\0')
    {
        return std::string(ScopedName->Value);
    }
    return EntityLabel(InEntity);
}

[[nodiscard]] Entity SelectedEntity(Ecs::Registry& InRegistry)
{
    const auto ScopedView = InRegistry.view<EditorSelected>();
    const auto ScopedIt = ScopedView.begin();
    return ScopedIt != ScopedView.end() ? *ScopedIt : Fby::kNullEntity;
}

void SelectOnly(Ecs::Registry& InRegistry, Entity InEntity)
{
    InRegistry.clear<EditorSelected>();
    if (InEntity != Fby::kNullEntity)
    {
        InRegistry.emplace<EditorSelected>(InEntity);
    }
}

void AddToSelection(Ecs::Registry& InRegistry, Entity InEntity)
{
    if (InEntity != Fby::kNullEntity && !InRegistry.all_of<EditorSelected>(InEntity))
    {
        InRegistry.emplace<EditorSelected>(InEntity);
    }
}

void ToggleSelection(Ecs::Registry& InRegistry, Entity InEntity)
{
    if (InEntity == Fby::kNullEntity)
    {
        return;
    }
    if (InRegistry.all_of<EditorSelected>(InEntity))
    {
        InRegistry.remove<EditorSelected>(InEntity);
    }
    else
    {
        InRegistry.emplace<EditorSelected>(InEntity);
    }
}

[[nodiscard]] std::vector<Entity> SelectedEntities(Ecs::Registry& InRegistry)
{
    std::vector<Entity> ScopedResult;
    const auto ScopedView = InRegistry.view<EditorSelected>();
    ScopedResult.reserve(ScopedView.size());
    for (const Entity ScopedEntity : ScopedView)
    {
        ScopedResult.push_back(ScopedEntity);
    }
    return ScopedResult;
}

// Topmost entity under a world point in the viewport. Sprites are picked by
// their authored rectangle, other entities (a ground collider without a sprite)
// by their collider extents, and the winner is the last one the renderer would
// draw so a click selects what the user sees.
[[nodiscard]] Entity PickEntityAt(const Ecs::Registry& InRegistry, Vector2D InWorld)
{
    Entity ScopedBest = Fby::kNullEntity;
    float ScopedBestKey = -1.0e30f;
    auto ScopedSprites = InRegistry.view<Sprite2D, Transform2D>();
    for (auto [ScopedEntity, ScopedSprite, ScopedTransform] : ScopedSprites.each())
    {
        if (InRegistry.all_of<Hidden>(ScopedEntity))
        {
            continue;
        }
        const float ScopedHalfX = std::abs(ScopedSprite.Size.X * ScopedTransform.Scale.X) * 0.5f;
        const float ScopedHalfY = std::abs(ScopedSprite.Size.Y * ScopedTransform.Scale.Y) * 0.5f;
        if (std::abs(InWorld.X - ScopedTransform.Position.X) > ScopedHalfX ||
            std::abs(InWorld.Y - ScopedTransform.Position.Y) > ScopedHalfY)
        {
            continue;
        }
        float ScopedKey = 0.0f;
        if (const DrawOrder* ScopedOrder = InRegistry.try_get<DrawOrder>(ScopedEntity); ScopedOrder != nullptr)
        {
            ScopedKey = static_cast<float>(ScopedOrder->Layer) * 4096.0f + ScopedOrder->Depth;
        }
        if (ScopedKey >= ScopedBestKey)
        {
            ScopedBestKey = ScopedKey;
            ScopedBest = ScopedEntity;
        }
    }
    if (ScopedBest != Fby::kNullEntity)
    {
        return ScopedBest;
    }
    auto ScopedColliders = InRegistry.view<Collider2D, Transform2D>();
    for (auto [ScopedEntity, ScopedCollider, ScopedTransform] : ScopedColliders.each())
    {
        if (InRegistry.all_of<Hidden>(ScopedEntity))
        {
            continue;
        }
        const float ScopedHalfX = std::abs(ScopedCollider.HalfExtents.X * ScopedTransform.Scale.X);
        const float ScopedHalfY = std::abs(ScopedCollider.HalfExtents.Y * ScopedTransform.Scale.Y);
        if (std::abs(InWorld.X - ScopedTransform.Position.X) <= ScopedHalfX &&
            std::abs(InWorld.Y - ScopedTransform.Position.Y) <= ScopedHalfY)
        {
            return ScopedEntity;
        }
    }
    return Fby::kNullEntity;
}

[[nodiscard]] float* FloatAt(void* InComponent, const Reflect::FieldDesc& InField)
{
    return reinterpret_cast<float*>(static_cast<std::byte*>(InComponent) + InField.offset);
}

[[nodiscard]] Vector2D* Vec2At(void* InComponent, const Reflect::FieldDesc& InField)
{
    return reinterpret_cast<Vector2D*>(static_cast<std::byte*>(InComponent) + InField.offset);
}

[[nodiscard]] bool* BoolAt(void* InComponent, const Reflect::FieldDesc& InField)
{
    return reinterpret_cast<bool*>(static_cast<std::byte*>(InComponent) + InField.offset);
}

[[nodiscard]] std::uint32_t ReadU32(const void* InComponent, const Reflect::FieldDesc& InField)
{
    if (InField.size == 1)
    {
        std::uint8_t ScopedValue = 0;
        std::memcpy(&ScopedValue, static_cast<const std::byte*>(InComponent) + InField.offset, sizeof(ScopedValue));
        return ScopedValue;
    }
    if (InField.size == 2)
    {
        std::uint16_t ScopedValue = 0;
        std::memcpy(&ScopedValue, static_cast<const std::byte*>(InComponent) + InField.offset, sizeof(ScopedValue));
        return ScopedValue;
    }
    std::uint32_t ScopedValue = 0;
    std::memcpy(&ScopedValue, static_cast<const std::byte*>(InComponent) + InField.offset, sizeof(ScopedValue));
    return ScopedValue;
}

void WriteU32(void* InComponent, const Reflect::FieldDesc& InField, std::uint32_t InValue)
{
    if (InField.size == 1)
    {
        const auto ScopedNarrowed = static_cast<std::uint8_t>(InValue);
        std::memcpy(static_cast<std::byte*>(InComponent) + InField.offset, &ScopedNarrowed, sizeof(ScopedNarrowed));
        return;
    }
    if (InField.size == 2)
    {
        const auto ScopedNarrowed = static_cast<std::uint16_t>(InValue);
        std::memcpy(static_cast<std::byte*>(InComponent) + InField.offset, &ScopedNarrowed, sizeof(ScopedNarrowed));
        return;
    }
    std::memcpy(static_cast<std::byte*>(InComponent) + InField.offset, &InValue, sizeof(InValue));
}

[[nodiscard]] std::int32_t ReadI32(const void* InComponent, const Reflect::FieldDesc& InField)
{
    if (InField.size == 1)
    {
        std::int8_t ScopedValue = 0;
        std::memcpy(&ScopedValue, static_cast<const std::byte*>(InComponent) + InField.offset, sizeof(ScopedValue));
        return ScopedValue;
    }
    if (InField.size == 2)
    {
        std::int16_t ScopedValue = 0;
        std::memcpy(&ScopedValue, static_cast<const std::byte*>(InComponent) + InField.offset, sizeof(ScopedValue));
        return ScopedValue;
    }
    std::int32_t ScopedValue = 0;
    std::memcpy(&ScopedValue, static_cast<const std::byte*>(InComponent) + InField.offset, sizeof(ScopedValue));
    return ScopedValue;
}

void WriteI32(void* InComponent, const Reflect::FieldDesc& InField, std::int32_t InValue)
{
    if (InField.size == 1)
    {
        const auto ScopedNarrowed = static_cast<std::int8_t>(InValue);
        std::memcpy(static_cast<std::byte*>(InComponent) + InField.offset, &ScopedNarrowed, sizeof(ScopedNarrowed));
        return;
    }
    if (InField.size == 2)
    {
        const auto ScopedNarrowed = static_cast<std::int16_t>(InValue);
        std::memcpy(static_cast<std::byte*>(InComponent) + InField.offset, &ScopedNarrowed, sizeof(ScopedNarrowed));
        return;
    }
    std::memcpy(static_cast<std::byte*>(InComponent) + InField.offset, &InValue, sizeof(InValue));
}

[[nodiscard]] std::string ReadStringField(const void* InComponent, const Reflect::FieldDesc& InField)
{
    const char* ScopedText = static_cast<const char*>(InComponent) + InField.offset;
    std::size_t ScopedLength = 0;
    while (ScopedLength < InField.size && ScopedText[ScopedLength] != '\0')
    {
        ++ScopedLength;
    }
    return std::string(ScopedText, ScopedLength);
}

[[nodiscard]] bool WriteStringField(void* InComponent, const Reflect::FieldDesc& InField, std::string_view InValue)
{
    if (InValue.size() >= InField.size || InValue.find('\0') != std::string_view::npos)
    {
        return false;
    }
    char* ScopedText = static_cast<char*>(InComponent) + InField.offset;
    std::memset(ScopedText, 0, InField.size);
    std::memcpy(ScopedText, InValue.data(), InValue.size());
    return true;
}

[[nodiscard]] std::uint64_t ReadU64Field(const void* InComponent, const Reflect::FieldDesc& InField)
{
    std::uint64_t ScopedValue = 0;
    if (InField.size == sizeof(ScopedValue))
    {
        std::memcpy(&ScopedValue, static_cast<const std::byte*>(InComponent) + InField.offset, sizeof(ScopedValue));
    }
    return ScopedValue;
}

[[nodiscard]] Fby::BigNum::Int128 ReadInt128Field(const void* InComponent, const Reflect::FieldDesc& InField)
{
    Fby::BigNum::Int128 ScopedValue;
    if (InField.size == sizeof(ScopedValue))
    {
        std::memcpy(&ScopedValue, static_cast<const std::byte*>(InComponent) + InField.offset, sizeof(ScopedValue));
    }
    return ScopedValue;
}

void WriteInt128Field(void* InComponent, const Reflect::FieldDesc& InField, const Fby::BigNum::Int128& InValue)
{
    if (InField.size == sizeof(InValue))
    {
        std::memcpy(static_cast<std::byte*>(InComponent) + InField.offset, &InValue, sizeof(InValue));
    }
}

[[nodiscard]] Fby::BigNum::Decimal128 ReadDecimalField(const void* InComponent, const Reflect::FieldDesc& InField)
{
    Fby::BigNum::Decimal128 ScopedValue;
    if (InField.size == sizeof(ScopedValue))
    {
        std::memcpy(&ScopedValue, static_cast<const std::byte*>(InComponent) + InField.offset, sizeof(ScopedValue));
    }
    return ScopedValue;
}

void WriteDecimalField(void* InComponent, const Reflect::FieldDesc& InField, const Fby::BigNum::Decimal128& InValue)
{
    if (InField.size == sizeof(InValue))
    {
        std::memcpy(static_cast<std::byte*>(InComponent) + InField.offset, &InValue, sizeof(InValue));
    }
}

[[nodiscard]] bool WriteU64Field(void* InComponent, const Reflect::FieldDesc& InField, std::uint64_t InValue)
{
    if (InField.size != sizeof(InValue))
    {
        return false;
    }
    std::memcpy(static_cast<std::byte*>(InComponent) + InField.offset, &InValue, sizeof(InValue));
    return true;
}

void MakeViewProjection(const EditorCamera& InCamera, float InWidth, float InHeight, float* InView, float* InProjection)
{
    std::fill(InView, InView + 16, 0.0f);
    InView[0] = 1.0f;
    InView[5] = 1.0f;
    InView[10] = 1.0f;
    InView[15] = 1.0f;

    std::fill(InProjection, InProjection + 16, 0.0f);
    const float ScopedSx = 2.0f * InCamera.zoom / std::max(InWidth, 1.0f);
    const float ScopedSy = 2.0f * InCamera.zoom / std::max(InHeight, 1.0f);
    InProjection[0] = ScopedSx;
    InProjection[5] = -ScopedSy;
    InProjection[10] = 1.0f;
    InProjection[12] = -InCamera.center.X * ScopedSx;
    InProjection[13] = InCamera.center.Y * ScopedSy;
    InProjection[15] = 1.0f;
}

void MakeModelMatrix(const Transform2D& InTransform, float* InModel)
{
    const float ScopedCosine = std::cos(InTransform.Rotation);
    const float ScopedSine = std::sin(InTransform.Rotation);
    std::fill(InModel, InModel + 16, 0.0f);
    InModel[0] = ScopedCosine * InTransform.Scale.X;
    InModel[1] = ScopedSine * InTransform.Scale.X;
    InModel[4] = -ScopedSine * InTransform.Scale.Y;
    InModel[5] = ScopedCosine * InTransform.Scale.Y;
    InModel[10] = 1.0f;
    InModel[12] = InTransform.Position.X;
    InModel[13] = InTransform.Position.Y;
    InModel[15] = 1.0f;
}

void ApplyModelMatrix(const float* InModel, Transform2D& InTransform)
{
    InTransform.Position = Vector2D{InModel[12], InModel[13]};
    InTransform.Rotation = std::atan2(InModel[1], InModel[0]);
    InTransform.Scale = Vector2D{std::sqrt(InModel[0] * InModel[0] + InModel[1] * InModel[1]),
                                 std::sqrt(InModel[4] * InModel[4] + InModel[5] * InModel[5])};
}

// Viewport camera + image rectangle, handed to the physics debug visualizer.
struct PhysicsDebugView
{
    Vector2D center{};
    float zoom = 1.0f;
    ImVec2 origin{};
    float width = 1.0f;
    float height = 1.0f;
};

[[nodiscard]] Vector2D MapPhysicsDebugToScreen(void* InUser, Vector2D InWorld)
{
    const auto* ScopedView = static_cast<const PhysicsDebugView*>(InUser);
    return Vector2D{
        ScopedView->origin.x + ScopedView->width * 0.5f + (InWorld.X - ScopedView->center.X) * ScopedView->zoom,
        ScopedView->origin.y + ScopedView->height * 0.5f + (InWorld.Y - ScopedView->center.Y) * ScopedView->zoom};
}
}

// Pimpl
struct EditorSystem::Impl
{
    EditorSystem* owner = nullptr;
    ImGuiContext* imguiContext = nullptr;
    ImPlotContext* plotContext = nullptr;
    Rhi::RHI* gpu = nullptr;
    WindowManager* window = nullptr;
    VirtualFileSystem* vfs = nullptr;
    EditorDesc desc;
    std::string iniPath;
    bool initialized = false;
    bool sdlInitialized = false;
    bool vulkanInitialized = false;
    bool visible = true;
    bool needsLayout = false;

    ImFont* fontRegular = nullptr;
    ImFont* fontMono = nullptr;

    // Style geometry is scaled relative to the last applied factor, never to the raw factor.
    float styleUiScale = 1.0f;
    float appliedStyleScale = 0.0f;

    void ApplyStyleScale()
    {
        // ContentScale is the part of the display scale the UI still has to
        // apply: Windows and X11 report window coordinates in physical pixels,
        // so a 150% display returns 1.5, while macOS and Wayland already fold
        // the scale into the framebuffer ratio and return 1.0.
        const float ScopedContentScale = window != nullptr ? window->ContentScale() : 1.0f;
        const float ScopedTarget = ScopedContentScale * std::max(0.75f, styleUiScale);
        const bool ScopedFirst = appliedStyleScale <= 0.0f;
        if (!ScopedFirst && std::abs(ScopedTarget - appliedStyleScale) <= 0.001f)
        {
            return;
        }
        ImGui::GetStyle().ScaleAllSizes(ScopedFirst ? ScopedTarget : ScopedTarget / appliedStyleScale);
        appliedStyleScale = ScopedTarget;
        const float ScopedDisplayScale = window != nullptr ? window->DisplayScale() : 1.0f;
        LogInfo("editor", "UI style scale {:.2f} (content {:.2f} x ui {:.2f}, display {:.2f})",
                static_cast<double>(ScopedTarget), static_cast<double>(ScopedContentScale),
                static_cast<double>(styleUiScale), static_cast<double>(ScopedDisplayScale));
    }

    Rhi::RenderTargetHandle target{};
    Rhi::RenderTargetHandle displayTarget{};
    Rhi::RenderTargetHandle hudTarget{};
    Rhi::RenderTargetHandle motionTarget{};
    Rhi::RenderTargetHandle fsr1Intermediate{};
    Rhi::RenderTargetHandle pieDisplayTarget{};
    Rhi::RenderTargetHandle pieFsr1Intermediate{};
    Rhi::Extent2D targetSize{};
    Rhi::Extent2D displaySize{};
    ImTextureID targetTexture = ImTextureID_Invalid;
    bool presentationFsr1 = false;
    bool presentationHudOffscreen = false;
    // A resized target is retired, not destroyed while the previous frames may
    // still sample its descriptor. Entries are released after four frames.
    struct RetiredTarget
    {
        Rhi::RenderTargetHandle target{};
        ImTextureID texture = ImTextureID_Invalid;
        std::uint64_t destroyAt = 0;
    };
    std::vector<RetiredTarget> retiredTargets;
    std::uint64_t targetFrame = 0;
    Rhi::TextureArrayHandle textureArray{};
    TextureThumbnailCache thumbnails;

    Scene::ComponentRegistry components;
    EditorCamera camera;
    std::filesystem::path currentScene;
    std::string status;
    double statusTime = 0.0;
    // The native save dialog answers on the SDL dialog thread, so a new scene
    // path is handed over through this lock and consumed at the top of Tick.
    std::mutex sceneDialogMutex;
    std::string pendingNewScenePath;
    std::string pendingPrefabPath;
    // Dirty tracking is derived from the command stack each frame, so undo back
    // to the saved state clears the marker again.
    std::uint32_t savedCommandCount = 0;
    std::uint32_t savedCursor = 0;
    bool sceneDirty = false;
    bool quitPrompt = false;
    bool autoSaveEnabled = true;
    float autoSaveSeconds = 60.0f;
    float autoSaveTimer = 0.0f;
    bool titleDirty = false;

    // Panels keep their transient state here: the path being edited,
    SkeletonInspectorState skeletonPanel;
    SpriteAnimationInspectorState spriteAnimationPanel;
    AnimationTimelinePanel timelinePanel;
    CommandStack commands;
    InteractionTransaction interaction;
    std::uint16_t transformCodec = 0xFFFFu;
    char hierarchySearch[64] = {};
    Entity renamingEntity = Fby::kNullEntity;
    bool renameFocusTaken = false;
    // Clipboard and snapping live in the editor session; they are not saved.
    struct ClipboardComponent
    {
        std::uint64_t layoutHash = 0;
        std::vector<std::byte> bytes;
    };
    struct ClipboardEntity
    {
        std::vector<ClipboardComponent> components;
    };
    std::vector<ClipboardEntity> clipboard;
    bool snapEnabled = false;
    // Viewport HUD and slider appearance, toggled from the viewport itself.
    bool showOriginNavigator = true;
    bool sliderGrabOverflow = false;
    float snapGrid = 16.0f;
    static constexpr float kSnapAngleDegrees = 15.0f;
    static constexpr float kSnapScale = 0.1f;
    // Editor authoring panels; they hold no globals.
    ContentBrowserPanel contentBrowser;
    MaterialGraphPanel materialGraph;
    VisualScriptPanel visualScript;
    TextPreviewPanel textPreview;
    bool showTextPreview = false;
    ScriptDebuggerPanel scriptDebugger;
    TilemapEditorPanel tilemap;
    VirtualTexturePanel virtualTexture;
    Rhi::VirtualTextureSystem* virtualTextureSource = nullptr;
    AIStateTreeCanvasPanel aiStateTree;
    AnimationGraphCanvasPanel animationGraph;
    DirectionalBlendSpacePanel directionalBlendSpace;
    const Ecs::Registry* aiPlayWorld = nullptr;
    bool aiPlayActive = false;

    // Play-In-Editor state. The toolbar only records an intent; the state
    PlayInEditor pie;
    std::vector<std::byte> pieArena;
    Ecs::SystemGraph* graph = nullptr;
    bool piePaused = false;
    bool piePauseSfx = true;
    bool piePauseMusic = false;
    bool piePauseHud = true;
    bool pieInitialized = false;
    bool pieVoicePushToTalk = false;
    enum class PieCommand : std::uint8_t
    {
        None = 0,
        Play,
        PlayMultiplayer,
        TogglePause,
        Step,
        Stop,
    };
    PieCommand pieCommand = PieCommand::None;
    // Second viewport of a PIE multiplayer session: the panel owns the client
    // camera and the target, the render loop fills the target every frame.
    PieClientViewport pieClientViewport;
    ImTextureID pieClientTexture = ImTextureID_Invalid;
    bool showPieClient = false;
    ProjectDashboard dashboard;
    ProjectConfigStore projectConfig;
    // Project-wide gameplay registries. Project settings rebuild the tag
    // registry whenever the authored database changes; the runtime and PIE use
    // the same ids.
    Fby::GameplayTagRegistry gameplayTags;
    Fby::GameplayAttributeRegistry gameplayAttributes;
    ProjectSettingsPanel projectSettings;
    LevelSettingsPanel levelSettings;
    EditorSettingsPanel editorSettings;
    UserSettings userSettings{};
    Rhi::BackendCapabilities backendCaps{};
    ReleaseCooker releaseCooker;
    AssetImportService importer;
    AssetDatabaseWatchSystem watcher;
    // Fallback kernel for hosts that do not create one. The editor's settings
    AccessibilityKernel accessibility;
    EditorFonts fonts;
    EditorFontSpec fontSpec{};
    std::unique_ptr<Text::TextService> sceneText;
    bool showSceneText = true;
    LogBoyPanel logBoy;
    LicensesPanel licenses;
    NetworkDiagnosticsPanel networkDiagnostics;
    Fby::SdfComposerSession sdfComposer;
    std::vector<float> composerPreviewSamples;
    Fby::SdfField composerPreviewField;
    bool composerPreviewValid = false;
    Fby::Core::Profiler* profiler = nullptr;
    Fby::Core::ProfilerPanel profilerPanel;
    const std::array<std::string_view, 5> profilerNames{"PreUpdate", "FixedUpdate", "PostUpdate", "Render", "Frame"};
    VoiceDevicePanel voiceDevices;
    ParticleFxPanel particleFx;
    TransportGroupPanel transportGroups;
    std::unique_ptr<VoiceSystem> voiceRuntime;
    VoiceSession voiceSession;
    Fby::VoicePresetRegistry voicePresets;
    Fby::VoicePresetSelection voiceSelection;
    Fby::VoiceEffectPolicy voicePolicy;
    Fby::VoicePresetApplier voiceApplier;
    Fby::VoiceReactiveSfxSettings voiceSfxSettings{};
    Fby::VoiceReactiveSfxSettings voiceSfxApplied{};
    Fby::VoiceReactiveSfx voiceSfx;
    std::array<Fby::AssetId, static_cast<std::size_t>(Fby::VoiceSfxKind::Count)> voiceSfxClips{};
    bool voiceSfxClipsReady = false;
    AudioDeviceEnumerator voiceDevicesEnumerator;
    bool voiceDevicesReady = false;
    bool voiceAttempted = false;
    SceneDiagnosticsPanel sceneDiagnostics;
    SdfCharacterPanel sdfCharacters;
    PhysicsDebugVisualizer physicsDebug;
    std::uint16_t projectSettingsCodec = 0xFFFFu;
    std::uint16_t editorSettingsCodec = 0xFFFFu;
    bool projectOpen = false;
    bool watcherStarted = false;
    bool quitRequested = false;
    bool showProjectSettings = false;
    bool showLevelSettings = true;
    bool showEditorSettings = false;
    bool showPluginManager = false;
    Plugin::PluginHost* pluginHost = nullptr;
    Ui::AccessibilityEffectRegistry* effectRegistry = nullptr;
    std::array<char, 192> pluginPath{};
    std::string pluginStatus;
    bool showUserSettings = false;

    bool showHierarchy = true;
    bool showInspector = true;
    bool showViewport = true;
    bool showProfiler = false;
    bool showSdfComposer = false;
    bool showStats = false;
    bool showLogBoy = true;
    bool showSceneDiagnostics = true;
    bool showSdfCharacters = false;
    bool showPhysicsDebug = false;
    bool showNavigationDebug = false;
    bool showLightDebug = false;
    bool showAssets = true;
    bool showConsole = true;
    bool showLicenses = false;
    bool showNetworkDiagnostics = false;
    bool showVoiceDevices = false;
    bool showParticleFx = false;
    bool showTransportGroups = false;
    bool showMaterialGraph = true;
    bool showVisualScript = true;
    bool showScriptDebugger = true;
    bool showTilemap = true;
    bool showVirtualTexture = false;
    bool showAIStateTree = true;
    bool showAnimationGraph = true;
    bool showDirectionalBlendSpace = true;
    bool showDemo = false;

    std::string assetDir;
    std::string selectedAsset;
    // Asset file operations. Names are edited in a modal popup and applied on the
    // project assets directory, then the VFS index is refreshed.
    enum class AssetDialog : std::uint8_t
    {
        None = 0,
        NewFolder,
        NewScene,
        Rename,
    };
    AssetDialog assetDialog = AssetDialog::None;
    char assetDialogName[128] = {};
    bool assetDialogOpen = false;
    bool assetDialogFocus = false;
    bool assetDeleteOpen = false;
    std::string assetDeleteTarget;
    std::vector<std::string> consoleLines;
    bool consoleScrollToBottom = false;

    ImGuizmo::OPERATION gizmoOperation = ImGuizmo::TRANSLATE;
    ImGuizmo::MODE gizmoMode = ImGuizmo::LOCAL;

    std::vector<float> frameMs;
    std::uint64_t tickCount = 0;
    ImGuiID dockspaceId = 0;
    std::filesystem::path layoutMarker;

    // Migration for layouts saved before a panel existed: dock the new window
    [[nodiscard]] ImGuiID DockHostFor(const char* InAnchorName) const
    {
        if (InAnchorName != nullptr)
        {
            const ImGuiWindow* ScopedAnchor = ImGui::FindWindowByName(InAnchorName);
            if (ScopedAnchor != nullptr && ScopedAnchor->DockId != 0)
            {
                return ScopedAnchor->DockId;
            }
        }
        return dockspaceId;
    }

    static void RenderThunk(void* InUser, void* InCommandBuffer)
    {
        auto* ScopedSelf = static_cast<EditorSystem*>(InUser);
        if (!ScopedSelf->MImpl->visible || ScopedSelf->MImpl->imguiContext == nullptr)
        {
            return;
        }
        ImGui::SetCurrentContext(ScopedSelf->MImpl->imguiContext);
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), static_cast<VkCommandBuffer>(InCommandBuffer));
    }

    static PFN_vkVoidFunction LoadVulkanFunction(const char* InName, void* InUser)
    {
        auto* ScopedGpu = static_cast<Rhi::RHI*>(InUser);
        return reinterpret_cast<PFN_vkVoidFunction>(ScopedGpu->GetNativeFunction(InName));
    }

    static void CheckVulkanResult(VkResult InResult)
    {
        if (InResult != VK_SUCCESS)
        {
            LogError("editor", "ImGui Vulkan backend returned VkResult {}", static_cast<int>(InResult));
        }
    }

    void SetStatus(std::string InMessage)
    {
        status = std::move(InMessage);
        statusTime = ImGui::GetTime();
        LogInfo("editor", "{}", status);
    }

    void StartReleaseCook()
    {
        if (!projectOpen || !projectConfig.Paths().IsValid())
        {
            SetStatus("release cook needs an open project");
            return;
        }
        if (releaseCooker.Running())
        {
            SetStatus("release cook is already running");
            return;
        }
        const std::filesystem::path ScopedEngineRoot = ResolveEngineRoot();
        std::filesystem::path ScopedPacker;
        for (const char* ScopedBuild : {"build", "build-dev", "build-linux-release", "build-macos-release",
                                        "build-windows-release", "build-clangcl", "build-ci"})
        {
            for (const char* ScopedSubdir : {"tools", ""})
            {
                std::filesystem::path ScopedCandidate =
                    ScopedEngineRoot / ScopedBuild / "bin" / ScopedSubdir / "fby_fempak";
#if defined(_WIN32)
                ScopedCandidate += ".exe";
#endif
                if (std::filesystem::is_regular_file(ScopedCandidate))
                {
                    ScopedPacker = ScopedCandidate;
                    break;
                }
            }
            if (!ScopedPacker.empty())
            {
                break;
            }
        }
        if (ScopedPacker.empty())
        {
#if defined(_WIN32)
            ScopedPacker = ScopedEngineRoot / "build" / "bin" / "tools" / "fby_fempak.exe";
#else
            ScopedPacker = ScopedEngineRoot / "build" / "bin" / "tools" / "fby_fempak";
#endif
        }
        ReleaseCookConfig ScopedConfig =
            ReleaseCooker::FromProject(projectConfig.Paths(), projectConfig.Project(),
                                       ScopedEngineRoot.generic_string(), ScopedPacker.generic_string());
        ScopedConfig.compileAot = visualScript.Initialized() && visualScript.Graph().nodeCount != 0;
        char ScopedError[kReleaseMessageBytes]{};
        Script::LogicGraph* ScopedGraph = ScopedConfig.compileAot ? &visualScript.Graph() : nullptr;
        if (!releaseCooker.Start(ScopedConfig, ScopedGraph, ScopedError, sizeof(ScopedError)))
        {
            SetStatus(std::string("release cook failed to start: ") + ScopedError);
            return;
        }
        showSceneDiagnostics = true;
        SetStatus("Build & Pack Game started");
    }

    void PrintConsole(std::string InLine)
    {
        consoleLines.push_back(std::move(InLine));
        if (consoleLines.size() > 512)
        {
            consoleLines.erase(consoleLines.begin(), consoleLines.begin() + 128);
        }
        consoleScrollToBottom = true;
    }

    // Fonts live in the .fempak. The VFS returns spans into an mmap.
    [[nodiscard]] EditorFontSpec MakeFontSpec(float InUiScale) const
    {
        EditorFontSpec ScopedSpec;
        ScopedSpec.uiScale = InUiScale;
        const auto& ScopedTypography = projectConfig.Editor();
        ScopedSpec.regularPx = ScopedTypography.fontPanelBody;
        ScopedSpec.smallPx = ScopedTypography.fontPanelBody;
        ScopedSpec.monoPx = ScopedTypography.fontPanelBody;
        ScopedSpec.boldPx = ScopedTypography.fontPanelBody;
        ScopedSpec.panelTitlePx = ScopedTypography.fontPanelTitle;
        ScopedSpec.tooltipPx = ScopedTypography.fontTooltip;
        ScopedSpec.scriptNodeTitlePx = ScopedTypography.fontScriptNodeTitle;
        ScopedSpec.scriptPinInputPx = ScopedTypography.fontScriptPinInput;
        ScopedSpec.scriptPinOutputPx = ScopedTypography.fontScriptPinOutput;
        ScopedSpec.sceneTextPx = ScopedTypography.fontSceneText;
        return ScopedSpec;
    }

    void InitializeFonts(ImGuiIO& InIo, float InUiScale)
    {
        const EditorFontSpec ScopedSpec = MakeFontSpec(InUiScale);
        fontSpec = ScopedSpec;
        if (!fonts.Initialize(InIo, vfs, window, ScopedSpec))
        {
            LogError("editor", "no usable editor font: the UI will render with the embedded vector font");
        }
        fontRegular = fonts.Regular();
        fontMono = fonts.Mono();
        visualScript.SetFonts(&fonts);
        materialGraph.Canvas().SetModelFonts(fonts.ScriptNodeTitle(), fonts.ScriptPinInput(), fonts.ScriptPinOutput(),
                                             fonts.PanelTitle());
        SetEditorFonts(&fonts);
        InIo.FontDefault = fontRegular;
    }

    void StopProjectSystems()
    {
        // Stop the producer first.
        watcher.Shutdown();
        importer.Shutdown();
        watcherStarted = false;
    }

    bool StartProjectSystems(Ecs::SystemContext& InContext, std::string* InError)
    {
        StopProjectSystems();
        if (!projectConfig.Loaded() || !projectConfig.Paths().IsValid())
        {
            return false;
        }
        if (InContext.assetDb == nullptr || InContext.assets == nullptr)
        {
            if (InError != nullptr)
            {
                *InError = "the SystemContext has no AssetDatabase/VFS; project watching is disabled";
            }
            return false;
        }

        AssetImportService::Config ScopedImportConfig;
        ScopedImportConfig.assetsRoot = projectConfig.Paths().assets;
        ScopedImportConfig.textureFormat = projectConfig.Project().textureFormat;
        ScopedImportConfig.srgb = true;
        ScopedImportConfig.maxSize = projectConfig.Project().maxTextureSize;
        ScopedImportConfig.background = true;
        ScopedImportConfig.textureArray = desc.textureArray;
        ScopedImportConfig.textureArrayFormat = desc.textureArrayFormat;
        std::string ScopedImportError;
        if (!importer.Initialize(*InContext.assetDb, *InContext.assets, InContext.renderer, ScopedImportConfig,
                                 &ScopedImportError))
        {
            if (InError != nullptr)
            {
                *InError = ScopedImportError;
            }
            return false;
        }
        (void)importer.BindAtlasManifest("atlas/atlas.toml", nullptr);

        if (!desc.watchAssets)
        {
            if (InError != nullptr)
            {
                *InError = "asset watcher disabled by EditorDesc";
            }
            return true;
        }

        AssetDatabaseWatchSystem::Config ScopedWatchConfig;
        ScopedWatchConfig.projectRoot = projectConfig.Paths().assets;
        ScopedWatchConfig.recursive = true;
        ScopedWatchConfig.autoImport = true;
        ScopedWatchConfig.defaultTextureFormat = projectConfig.Project().textureFormat;
        AssetDatabaseWatchSystem::Callbacks ScopedCallbacks;
        ScopedCallbacks.user = &importer;
        ScopedCallbacks.scheduleCook = &AssetImportService::ScheduleCookThunk;
        std::string ScopedWatchError;
        if (!watcher.Initialize(*InContext.assetDb, ScopedWatchConfig, ScopedCallbacks, &ScopedWatchError))
        {
            if (InError != nullptr)
            {
                *InError = ScopedWatchError;
            }
            return false;
        }
        watcherStarted = true;
        watcher.ScanForNewSources(nullptr);
        // The startup scan may have just created the .femeta rows the atlas
        (void)importer.BindAtlasManifest("atlas/atlas.toml", nullptr);
        (void)watcher.Watch("", nullptr);
        return true;
    }

    // Bind both ImNodes canvases to the same runtime world.
    void SyncCanvasWorlds(Ecs::SystemContext& InContext)
    {
        const Ecs::Registry* ScopedRuntime = pie.Active() ? &pie.World() : aiPlayWorld;
        const bool ScopedActive = pie.Active() || aiPlayActive;
        aiStateTree.BindRuntimeWorld(ScopedRuntime, &InContext.registry, ScopedActive);
        animationGraph.BindRuntimeWorld(ScopedRuntime, &InContext.registry, ScopedActive);
    }

    void StartPIE(Ecs::SystemContext& InContext, bool InMultiplayer)
    {
        if (pie.Active())
        {
            return;
        }
        if (graph == nullptr || !pieInitialized)
        {
            SetStatus("PIE start failed: the SystemGraph or the PIE arena is not initialized");
            return;
        }
        // BuiltIns() is the same schema the rollback session uses. The snapshot
        const Ecs::SnapshotSchema ScopedSchema = Ecs::SnapshotSchema::BuiltIns();
        // The replication protocol expects the first baseline at tick 1; the
        // sandbox second copy applies that baseline before any delta.
        ExternalClientDesc ScopedExternal;
        ScopedExternal.executable = desc.pieClientExecutable.string();
        if (InContext.physics != nullptr)
        {
            const std::uint8_t ScopedBackend = projectConfig.Project().physicsBackend;
            const Fby::PhysicsBackendKind ScopedKind =
                ScopedBackend == 1
                    ? Fby::PhysicsBackendKind::Box2D
                    : (ScopedBackend == 2 ? Fby::PhysicsBackendKind::Dod : Fby::PhysicsBackendKind::Auto);
            InContext.physics->SetProjectPolicy(ScopedKind, projectConfig.Project().physicsBodyBudget);
        }
        const PlayInEditor::Result ScopedResult =
            InMultiplayer
                ? pie.StartMultiplayer(InContext.registry, ScopedSchema, InContext, 1, 1, nullptr, &ScopedExternal)
                : pie.Start(InContext.registry, ScopedSchema, InContext);
        if (ScopedResult != PlayInEditor::Result::Ok)
        {
            SetStatus(std::format("PIE start failed: {}", PlayInEditor::ResultName(ScopedResult)));
            return;
        }
        piePaused = false;
        if (InContext.audio != nullptr)
        {
            InContext.audio->SetBusPaused(Fby::AudioBus::GameSfx, false);
            InContext.audio->SetBusPaused(Fby::AudioBus::Music, false);
        }
        if (graph != nullptr)
        {
            graph->SetPaused(false);
        }
        // The PIE clone only carries registered (snapshot) components, so the
        // runtime tilemap grids are rebuilt from their authored references.
        owner->ResolveTilemapRefs(pie.World(), InContext.assets != nullptr ? InContext.assets : vfs);
        if (InMultiplayer)
        {
            // The client world starts on the host camera so both viewports agree
            // until the client camera is panned independently.
            owner->ResolveTilemapRefs(pie.ClientWorld(), InContext.assets != nullptr ? InContext.assets : vfs);
            pieClientViewport.Camera() = camera;
            showPieClient = true;
        }
        SyncCanvasWorlds(InContext);
        (void)animationGraph.PublishPIEStateFromWorlds();
        if (InMultiplayer)
        {
            if (pie.ExternalClient())
            {
                SetStatus(
                    std::format("PIE multiplayer playing: second sandbox on UDP {}, local client world, {} bytes, "
                                "voice {}",
                                pie.ExternalClientPort(), pie.SnapshotBytes(), pie.VoiceActive() ? "on" : "off"));
            }
            else
            {
                SetStatus(std::format("PIE multiplayer playing: local client world only (no second process), {} bytes, "
                                      "voice {}",
                                      pie.SnapshotBytes(), pie.VoiceActive() ? "on" : "off"));
            }
        }
        else
        {
            SetStatus(std::format("PIE playing: {} snapshot bytes, isolated physics world", pie.SnapshotBytes()));
        }
    }

    void StopPIE(Ecs::SystemContext& InContext)
    {
        if (!pie.Active())
        {
            return;
        }
        const std::uint64_t ScopedTicks = pie.TickCount();
        // Stop() destroys the PIE bodies, shuts the isolated Box2D world down and
        // clears both the host and the replicated client world.
        pie.Stop();
        ReleasePieClientTarget();
        piePaused = false;
        if (InContext.audio != nullptr)
        {
            InContext.audio->SetBusPaused(Fby::AudioBus::GameSfx, false);
            InContext.audio->SetBusPaused(Fby::AudioBus::Music, false);
        }
        if (graph != nullptr)
        {
            graph->SetPaused(false);
        }
        pieVoicePushToTalk = false;
        animationGraph.InvalidatePIEState();
        SyncCanvasWorlds(InContext);
        SetStatus(std::format("PIE stopped after {} ticks; editor scene restored", ScopedTicks));
    }

    void ProcessPIE(Ecs::SystemContext& InContext)
    {
        bool ScopedStepOnce = false;
        switch (pieCommand)
        {
            case PieCommand::Play:
                StartPIE(InContext, false);
                break;
            case PieCommand::PlayMultiplayer:
                StartPIE(InContext, true);
                break;
            case PieCommand::TogglePause:
                if (pie.Active())
                {
                    piePaused = !piePaused;
                    if (InContext.audio != nullptr)
                    {
                        InContext.audio->SetBusPaused(Fby::AudioBus::GameSfx, piePaused && piePauseSfx);
                        InContext.audio->SetBusPaused(Fby::AudioBus::Music, piePaused && piePauseMusic);
                    }
                    if (graph != nullptr)
                    {
                        graph->SetPaused(piePaused, !piePauseHud);
                    }
                    SetStatus(piePaused ? "PIE paused" : "PIE resumed");
                }
                break;
            case PieCommand::Step:
                ScopedStepOnce = pie.Active() && piePaused;
                break;
            case PieCommand::Stop:
                StopPIE(InContext);
                break;
            case PieCommand::None:
                break;
        }
        pieCommand = PieCommand::None;

        if (!pie.Active())
        {
            return;
        }
        // Bind before the step so PublishPIEStateFromWorlds() resolves.
        SyncCanvasWorlds(InContext);
        if (piePaused)
        {
            if (ScopedStepOnce)
            {
                // A single fixed tick while the session stays paused, so a game
                // can be inspected frame by frame.
                const float ScopedFixedHz = std::clamp(projectConfig.Project().fixedUpdateHz, 15.0f, 240.0f);
                const float ScopedFixedStep = 1.0f / std::max(ScopedFixedHz, 1.0f);
                if (graph != nullptr)
                {
                    graph->SetPaused(false);
                }
                const bool ScopedStepped = pie.Multiplayer()
                                               ? pie.StepMultiplayer(*graph, ScopedFixedStep, ScopedFixedStep)
                                               : pie.Step(*graph, ScopedFixedStep, ScopedFixedStep);
                if (graph != nullptr)
                {
                    graph->SetPaused(piePaused, !piePauseHud);
                }
                if (ScopedStepped)
                {
                    SyncCanvasWorlds(InContext);
                    (void)animationGraph.PublishPIEStateFromWorlds();
                    SetStatus("PIE stepped one tick");
                }
            }
            // A paused session still answers its second process so the sandbox
            // window does not treat the pause as a stopped host.
            pie.KeepExternalClientAlive(InContext.dt);
            return;
        }
        const float ScopedFixedHz = std::clamp(projectConfig.Project().fixedUpdateHz, 15.0f, 240.0f);
        const float ScopedFixedStep = 1.0f / std::max(ScopedFixedHz, 1.0f);
        // A multiplayer session also replicates the host delta into the client
        // world; the plain play session only steps the host clone.
        const bool ScopedStepped = pie.Multiplayer() ? pie.StepMultiplayer(*graph, InContext.dt, ScopedFixedStep)
                                                     : pie.Step(*graph, InContext.dt, ScopedFixedStep);
        if (ScopedStepped)
        {
            // The step joined every system-graph worker before returning.
            SyncCanvasWorlds(InContext);
            (void)animationGraph.PublishPIEStateFromWorlds();
        }
    }

    // Pushes the editor's saved log limits into the engine sink. The sink then
    // prunes older files and compacts the open file immediately.
    void ApplyLogLimits()
    {
        LogRetention ScopedRetention;
        ScopedRetention.MaxFiles = projectConfig.Editor().logMaxFiles;
        ScopedRetention.MaxLinesPerFile = projectConfig.Editor().logMaxLinesPerFile;
        SetLogRetention(ScopedRetention);
    }

    void ApplySettings(Ecs::SystemContext& InContext)
    {
        projectSettings.Apply(InContext);
        editorSettings.Apply(InContext);
        ApplyLogLimits();
        // Window presentation comes from the project config, so a project can
        // start borderless on one machine and windowed on another.
        if (window != nullptr)
        {
            const auto ScopedMode =
                static_cast<WindowMode>(std::min<std::uint8_t>(projectConfig.Editor().windowMode, 2));
            if (window->Mode() != ScopedMode)
            {
                (void)window->SetWindowMode(ScopedMode);
            }
        }
        // The font set owns the UI scale: it applies FontScaleMain (dynamic
        // atlas) or FontGlobalScale (legacy density) on the next atlas apply.
        fonts.SetUiScale(projectConfig.Editor().uiScale);
        // Geometry follows the same UI scale. The platform's pixel density is
        // applied by the ImGui framebuffer scale, not by this multiplier.
        styleUiScale = projectConfig.Editor().uiScale;
        ApplyStyleScale();
    }

    void PushMono() const
    {
        if (fontMono != nullptr)
        {
            ImGui::PushFont(fontMono);
        }
    }

    void PopMono() const
    {
        if (fontMono != nullptr)
        {
            ImGui::PopFont();
        }
    }

    static void RemoveTexture(ImTextureID InTexture)
    {
        if (InTexture == ImTextureID_Invalid)
        {
            return;
        }
        const auto ScopedBits = static_cast<std::uintptr_t>(static_cast<std::uint64_t>(InTexture));
        ImGui_ImplVulkan_RemoveTexture(reinterpret_cast<VkDescriptorSet>(ScopedBits));
    }

    void ReleaseTargetTexture()
    {
        if (targetTexture != ImTextureID_Invalid)
        {
            RemoveTexture(targetTexture);
            targetTexture = ImTextureID_Invalid;
        }
    }

    void CollectRetiredTargets()
    {
        ++targetFrame;
        for (std::size_t ScopedI = 0; ScopedI < retiredTargets.size();)
        {
            if (retiredTargets[ScopedI].destroyAt > targetFrame)
            {
                ++ScopedI;
                continue;
            }
            RemoveTexture(retiredTargets[ScopedI].texture);
            if (retiredTargets[ScopedI].target.IsValid() && gpu != nullptr)
            {
                gpu->DestroyRenderTarget(retiredTargets[ScopedI].target);
            }
            retiredTargets[ScopedI] = retiredTargets.back();
            retiredTargets.pop_back();
        }
    }

    void EnsureViewportTarget(float InWidth, float InHeight)
    {
        if (gpu == nullptr)
        {
            return;
        }
        CollectRetiredTargets();
        const std::uint32_t ScopedDisplayWidth =
            std::clamp(static_cast<std::uint32_t>(std::lround(InWidth)), 1u, 4096u);
        const std::uint32_t ScopedDisplayHeight =
            std::clamp(static_cast<std::uint32_t>(std::lround(InHeight)), 1u, 4096u);
        const float ScopedScale = std::clamp(userSettings.renderScale, 0.25f, 1.0f);
        const std::uint32_t ScopedPixelWidth =
            std::max(1u, static_cast<std::uint32_t>(std::lround(static_cast<float>(ScopedDisplayWidth) * ScopedScale)));
        const std::uint32_t ScopedPixelHeight = std::max(
            1u, static_cast<std::uint32_t>(std::lround(static_cast<float>(ScopedDisplayHeight) * ScopedScale)));
        const bool ScopedVendorUpscaler = userSettings.upscalerBackend == UpscalerBackendSetting::Dlss ||
                                          userSettings.upscalerBackend == UpscalerBackendSetting::MetalFx;
        const bool ScopedFsr1 =
            ((userSettings.upscaleMode == UpscaleMode::Fsr1 || userSettings.upscaleMode == UpscaleMode::Fsr2) &&
             (ScopedPixelWidth != ScopedDisplayWidth || ScopedPixelHeight != ScopedDisplayHeight)) ||
            ScopedVendorUpscaler || userSettings.upscalePolicy != UpscalePolicy::Off;
        const bool ScopedFrameGenerationRequested =
            userSettings.frameGenerationPolicy != FrameGenerationPolicySetting::Off &&
            (backendCaps.CustomFrameGeneration || backendCaps.Fsr3FrameGeneration || backendCaps.DlssFrameGeneration);
        const bool ScopedHudOffscreen =
            ResolveHudTarget(userSettings.hudTarget, ScopedFrameGenerationRequested) == HudTargetMode::Offscreen;
        if (target.IsValid() && targetSize.width == ScopedPixelWidth && targetSize.height == ScopedPixelHeight &&
            displaySize.width == ScopedDisplayWidth && displaySize.height == ScopedDisplayHeight &&
            presentationFsr1 == ScopedFsr1 && presentationHudOffscreen == ScopedHudOffscreen)
        {
            return;
        }

        if (displayTarget.IsValid())
        {
            retiredTargets.push_back(RetiredTarget{displayTarget, ImTextureID_Invalid, targetFrame + 4});
            displayTarget = {};
        }
        if (hudTarget.IsValid())
        {
            retiredTargets.push_back(RetiredTarget{hudTarget, ImTextureID_Invalid, targetFrame + 4});
            hudTarget = {};
        }
        if (motionTarget.IsValid())
        {
            retiredTargets.push_back(RetiredTarget{motionTarget, ImTextureID_Invalid, targetFrame + 4});
            motionTarget = {};
        }
        if (fsr1Intermediate.IsValid())
        {
            retiredTargets.push_back(RetiredTarget{fsr1Intermediate, ImTextureID_Invalid, targetFrame + 4});
            fsr1Intermediate = {};
        }
        if (target.IsValid())
        {
            // The previous frame may still sample the old descriptor. Retire it
            // for a few frames instead of stalling the device on every resize.
            if (targetTexture != ImTextureID_Invalid)
            {
                retiredTargets.push_back(RetiredTarget{target, targetTexture, targetFrame + 4});
                targetTexture = ImTextureID_Invalid;
            }
            else
            {
                retiredTargets.push_back(RetiredTarget{target, ImTextureID_Invalid, targetFrame + 4});
            }
            target = {};
        }
        else
        {
            ReleaseTargetTexture();
        }

        Rhi::RenderTargetDesc ScopedTargetDesc;
        ScopedTargetDesc.size = {ScopedPixelWidth, ScopedPixelHeight};
        ScopedTargetDesc.srgb = false;
        target = gpu->CreateRenderTarget(ScopedTargetDesc);
        if (!target.IsValid())
        {
            return;
        }
        targetSize = ScopedTargetDesc.size;
        displaySize = {ScopedDisplayWidth, ScopedDisplayHeight};
        motionTarget = gpu->CreateMotionVectorTarget(targetSize);
        presentationFsr1 = ScopedFsr1;
        presentationHudOffscreen = ScopedHudOffscreen;
        if (ScopedFsr1)
        {
            ScopedTargetDesc.size = {ScopedDisplayWidth, ScopedDisplayHeight};
            displayTarget = gpu->CreateRenderTarget(ScopedTargetDesc);
            if (userSettings.upscaleMode == UpscaleMode::Fsr1 || userSettings.upscaleMode == UpscaleMode::Fsr2)
            {
                fsr1Intermediate = gpu->CreateRenderTarget(ScopedTargetDesc);
            }
        }
        if (ScopedHudOffscreen)
        {
            const float ScopedHudScale = std::clamp(userSettings.hudRenderScale, 0.25f, 1.0f);
            ScopedTargetDesc.size = {
                std::max(1u, static_cast<std::uint32_t>(std::lround(ScopedDisplayWidth * ScopedHudScale))),
                std::max(1u, static_cast<std::uint32_t>(std::lround(ScopedDisplayHeight * ScopedHudScale)))};
            hudTarget = gpu->CreateHudTarget(ScopedTargetDesc.size);
        }
        gpu->SetHudTarget(hudTarget);

        Rhi::NativeTextureView ScopedView;
        const Rhi::RenderTargetHandle ScopedPresentedTarget = displayTarget.IsValid() ? displayTarget : target;
        if (gpu->RenderTargetTexture(ScopedPresentedTarget, ScopedView) && ScopedView.view != nullptr)
        {
            const VkDescriptorSet ScopedDescriptor = ImGui_ImplVulkan_AddTexture(
                static_cast<VkImageView>(ScopedView.view), static_cast<VkImageLayout>(ScopedView.layout));
            targetTexture = static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(ScopedDescriptor));
        }
    }

    void ReleasePieClientTexture()
    {
        if (pieClientTexture != ImTextureID_Invalid)
        {
            const auto ScopedBits = static_cast<std::uintptr_t>(static_cast<std::uint64_t>(pieClientTexture));
            ImGui_ImplVulkan_RemoveTexture(reinterpret_cast<VkDescriptorSet>(ScopedBits));
            pieClientTexture = ImTextureID_Invalid;
        }
    }

    void ReleasePieClientTarget()
    {
        if (gpu == nullptr)
        {
            return;
        }
        if (pieClientViewport.Active())
        {
            // The previous frame can still sample the descriptor; idle before
            // the descriptor set and the image view are freed.
            gpu->WaitIdle();
        }
        ReleasePieClientTexture();
        pieClientViewport.SyncSession(*gpu, false);
    }

    void EnsurePieClientTarget()
    {
        if (gpu == nullptr)
        {
            return;
        }
        // The client target mirrors the main viewport resolution; the panel
        // letterboxes it when the dock area has a different aspect ratio.
        Rhi::Extent2D ScopedSize = targetSize;
        if (ScopedSize.width == 0 || ScopedSize.height == 0)
        {
            ScopedSize = {1280, 720};
        }
        const bool ScopedPieVendorUpscaler = userSettings.upscalerBackend == UpscalerBackendSetting::Dlss ||
                                             userSettings.upscalerBackend == UpscalerBackendSetting::MetalFx;
        const bool ScopedPieFsr1 =
            ((userSettings.upscaleMode == UpscaleMode::Fsr1 || userSettings.upscaleMode == UpscaleMode::Fsr2) &&
             (displaySize.width != ScopedSize.width || displaySize.height != ScopedSize.height)) ||
            ScopedPieVendorUpscaler || userSettings.upscalePolicy != UpscalePolicy::Off;
        if (pieClientViewport.Target().IsValid() && pieClientViewport.Size() == ScopedSize &&
            (!ScopedPieFsr1 || (pieDisplayTarget.IsValid() && pieFsr1Intermediate.IsValid())))
        {
            return;
        }
        if (pieClientViewport.Active())
        {
            // The old descriptor may still be referenced by the last frame.
            gpu->WaitIdle();
        }
        ReleasePieClientTexture();
        if (pieDisplayTarget.IsValid())
        {
            gpu->DestroyRenderTarget(pieDisplayTarget);
            pieDisplayTarget = {};
        }
        if (pieFsr1Intermediate.IsValid())
        {
            gpu->DestroyRenderTarget(pieFsr1Intermediate);
            pieFsr1Intermediate = {};
        }
        if (!pieClientViewport.EnsureTarget(*gpu, ScopedSize))
        {
            return;
        }
        if (ScopedPieFsr1)
        {
            Rhi::RenderTargetDesc ScopedDisplayDesc;
            ScopedDisplayDesc.size = displaySize;
            ScopedDisplayDesc.srgb = false;
            pieDisplayTarget = gpu->CreateRenderTarget(ScopedDisplayDesc);
            if (userSettings.upscaleMode == UpscaleMode::Fsr1 || userSettings.upscaleMode == UpscaleMode::Fsr2)
            {
                pieFsr1Intermediate = gpu->CreateRenderTarget(ScopedDisplayDesc);
            }
        }
        const Rhi::RenderTargetHandle ScopedPresentedPie =
            pieDisplayTarget.IsValid() ? pieDisplayTarget : pieClientViewport.Target();
        Rhi::NativeTextureView ScopedView;
        if (gpu->RenderTargetTexture(ScopedPresentedPie, ScopedView) && ScopedView.view != nullptr)
        {
            const VkDescriptorSet ScopedDescriptor = ImGui_ImplVulkan_AddTexture(
                static_cast<VkImageView>(ScopedView.view), static_cast<VkImageLayout>(ScopedView.layout));
            pieClientTexture = static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(ScopedDescriptor));
        }
    }

    void DrawPieClientViewport()
    {
        if (!showPieClient)
        {
            // A hidden panel needs no offscreen image; release it so a long
            // session does not keep the second target resident.
            ReleasePieClientTarget();
            return;
        }
        const bool ScopedMultiplayer = pie.Multiplayer();
        if (ScopedMultiplayer)
        {
            EnsurePieClientTarget();
        }
        else
        {
            ReleasePieClientTarget();
        }
        ImGui::SetNextWindowDockID(DockHostFor("Viewport"), ImGuiCond_FirstUseEver);
        const std::uint64_t ScopedTexture =
            pieClientTexture != ImTextureID_Invalid ? static_cast<std::uint64_t>(pieClientTexture) : 0u;
        pieClientViewport.Draw(ScopedTexture, &showPieClient, ScopedMultiplayer);
    }

    void BuildLayout(ImGuiID InDockspace)
    {
        ImGui::DockBuilderRemoveNode(InDockspace);
        ImGui::DockBuilderAddNode(InDockspace, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(InDockspace, ImGui::GetMainViewport()->WorkSize);
        ImGuiID ScopedLeft = 0;
        ImGuiID ScopedAssets = 0;
        ImGuiID ScopedRight = 0;
        ImGuiID ScopedProfiles = 0;
        ImGuiID ScopedStats = 0;
        ImGuiID ScopedGraphs = 0;
        ImGuiID ScopedCenter = InDockspace;
        ImGui::DockBuilderSplitNode(ScopedCenter, ImGuiDir_Left, 0.22f, &ScopedLeft, &ScopedCenter);
        ImGui::DockBuilderSplitNode(ScopedCenter, ImGuiDir_Right, 0.28f, &ScopedRight, &ScopedCenter);
        ImGui::DockBuilderSplitNode(ScopedCenter, ImGuiDir_Down, 0.26f, &ScopedProfiles, &ScopedCenter);
        ImGui::DockBuilderSplitNode(ScopedCenter, ImGuiDir_Down, 0.34f, &ScopedGraphs, &ScopedCenter);
        ImGui::DockBuilderSplitNode(ScopedLeft, ImGuiDir_Down, 0.48f, &ScopedAssets, &ScopedLeft);
        ImGui::DockBuilderSplitNode(ScopedRight, ImGuiDir_Down, 0.45f, &ScopedStats, &ScopedRight);

        // The Viewport is docked last so it is the selected tab in the center;
        // the timeline and the debugger live with the graph editors instead of
        // stealing the viewport's node.
        ImGui::DockBuilderDockWindow("PIE Client", ScopedCenter);
        ImGui::DockBuilderDockWindow("Viewport", ScopedCenter);
        ImGui::DockBuilderDockWindow("Hierarchy", ScopedLeft);
        ImGui::DockBuilderDockWindow("Content Browser", ScopedAssets);
        ImGui::DockBuilderDockWindow("Inspector", ScopedRight);
        ImGui::DockBuilderDockWindow("Material Graph", ScopedGraphs);
        ImGui::DockBuilderDockWindow("VisualScript", ScopedGraphs);
        ImGui::DockBuilderDockWindow("AI State Tree", ScopedGraphs);
        ImGui::DockBuilderDockWindow("Animation Graph", ScopedGraphs);
        ImGui::DockBuilderDockWindow("Directional Blend Space", ScopedGraphs);
        ImGui::DockBuilderDockWindow("Timeline & Curves", ScopedGraphs);
        ImGui::DockBuilderDockWindow("Script Debugger", ScopedGraphs);
        ImGui::DockBuilderDockWindow("Tilemap", ScopedGraphs);
        ImGui::DockBuilderDockWindow("Profiler", ScopedProfiles);
        ImGui::DockBuilderDockWindow("Console", ScopedProfiles);
        ImGui::DockBuilderDockWindow("LogBoy", ScopedProfiles);
        ImGui::DockBuilderDockWindow("Stats", ScopedStats);
        ImGui::DockBuilderDockWindow("Scene Diagnostics", ScopedStats);
        ImGui::DockBuilderDockWindow("Text Preview", ScopedStats);
        ImGui::DockBuilderFinish(InDockspace);
        // Remember which default layout this ini now reflects.
        std::error_code ScopedMarkerError;
        std::filesystem::create_directories(layoutMarker.parent_path(), ScopedMarkerError);
        std::ofstream ScopedLayoutFile(layoutMarker, std::ios::trunc);
        ScopedLayoutFile << kEditorLayoutVersion << "\n";
        LogInfo("editor",
                "docking layout: work {:.0f}x{:.0f}, hierarchy={} assets={} center={} right={} graphs={} "
                "profiler={} stats={}",
                ImGui::GetMainViewport()->WorkSize.x, ImGui::GetMainViewport()->WorkSize.y, ScopedLeft, ScopedAssets,
                ScopedCenter, ScopedRight, ScopedGraphs, ScopedProfiles, ScopedStats);
    }

    void CommitInteraction() noexcept
    {
        if (interaction.Active())
        {
            (void)interaction.Commit();
        }
    }

    // Called after a Mirror widget ran. Only the widget's own field is compared:
    void TrackFieldChange(Entity InEntity, std::uint16_t InCodec, const Reflect::FieldDesc& InField,
                          const std::array<std::byte, kMaxTrackedComponentBytes>& InBefore) noexcept
    {
        const std::uint32_t ScopedSize = commands.CodecSize(InCodec);
        if (ScopedSize == 0 || ScopedSize > InBefore.size() || !Reflect::FieldFits(InField, ScopedSize))
        {
            return;
        }
        std::array<std::byte, kMaxTrackedComponentBytes> ScopedCurrent{};
        if (commands.CaptureComponent(InEntity, InCodec, ScopedCurrent) != CommandStack::Result::Ok)
        {
            return;
        }
        if (Reflect::FieldBytesEqual(InField, InBefore.data() + InField.offset, ScopedCurrent.data() + InField.offset))
        {
            // Hover / click without a value change, or a change in another field
            return;
        }
        if (interaction.Active() &&
            !interaction.IsFor(InEntity, InCodec, InteractionTransaction::Source::InspectorField))
        {
            // A gizmo drag or a different component cannot share the one open
            CommitInteraction();
        }
        if (!interaction.Active())
        {
            std::array<char, kMaxCommandLabel> ScopedLabel{};
            std::snprintf(ScopedLabel.data(), ScopedLabel.size(), "Edit %.*s", static_cast<int>(InField.name.size()),
                          InField.name.data());
            if (interaction.BeginWithSnapshot(
                    commands, InEntity, InCodec, ScopedLabel.data(), InteractionTransaction::Source::InspectorField,
                    std::span<const std::byte>(InBefore.data(), ScopedSize)) != CommandStack::Result::Ok)
            {
                return;
            }
        }
        (void)interaction.Record();
    }

    // Inspector rows put the property name on the left and the editor on the
    // right. ImGui's widgets draw their label after the value, which made a row
    // read "640.000 360.000 Position"; a fixed left column keeps the inspector
    // scannable and matches every other property grid.
    void BeginPropertyRow(const char* InName)
    {
        const float ScopedColumn = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x * 0.42f;
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(InName);
        ImGui::SameLine(ScopedColumn);
        ImGui::SetNextItemWidth(-1.0f);
    }

    void DrawField(Ecs::Registry& InRegistry, Entity InEntity, const Scene::ComponentOps& InOps,
                   const Reflect::FieldDesc& InField, std::uint16_t InCodec)
    {
        void* ScopedComponent = InOps.getMutable(InRegistry, InEntity);
        if (ScopedComponent == nullptr)
        {
            return;
        }
        std::array<std::byte, kMaxTrackedComponentBytes> ScopedBefore{};
        const bool ScopedTrackable = InCodec != 0xFFFFu && commands.CaptureComponent(InEntity, InCodec, ScopedBefore) ==
                                                               CommandStack::Result::Ok;
        ImGui::PushID(InField.name.data());
        const std::string ScopedLabel(InField.name);
        switch (InField.kind)
        {
            case Reflect::FieldKind::Float:
            {
                float ScopedValue = *FloatAt(ScopedComponent, InField);
                BeginPropertyRow(ScopedLabel.c_str());
                if (ImGui::DragFloat("##value", &ScopedValue, 0.01f))
                {
                    *FloatAt(ScopedComponent, InField) = ScopedValue;
                }
                break;
            }
            case Reflect::FieldKind::Int32:
            {
                std::int32_t ScopedValue = ReadI32(ScopedComponent, InField);
                BeginPropertyRow(ScopedLabel.c_str());
                if (ImGui::DragInt("##value", &ScopedValue))
                {
                    WriteI32(ScopedComponent, InField, ScopedValue);
                }
                break;
            }
            case Reflect::FieldKind::UInt32:
            {
                std::uint32_t ScopedValue = ReadU32(ScopedComponent, InField);
                if (InOps.name == "TextFormatComponent" && InField.name.starts_with("Spec"))
                {
                    Fby::BigNum::FormatSpec ScopedSpec = Text::UnpackFormatSpec(ScopedValue);
                    int ScopedStyle = static_cast<int>(ScopedSpec.Style);
                    const char* ScopedStyles[] = {"Plain", "Scientific"};
                    BeginPropertyRow("Style");
                    if (ImGui::Combo("##style", &ScopedStyle, ScopedStyles, 2))
                    {
                        ScopedSpec.Style = static_cast<Fby::BigNum::Notation>(ScopedStyle);
                    }
                    int ScopedSignificant = static_cast<int>(ScopedSpec.SignificantDigits);
                    BeginPropertyRow("Significant");
                    if (ImGui::DragInt("##significant", &ScopedSignificant, 1.0f, 0, 32))
                    {
                        ScopedSpec.SignificantDigits = static_cast<std::uint8_t>(std::clamp(ScopedSignificant, 0, 32));
                    }
                    char ScopedMarker[2] = {ScopedSpec.ExponentMarker, '\0'};
                    BeginPropertyRow("Exponent marker");
                    if (ImGui::InputText("##marker", ScopedMarker, sizeof(ScopedMarker)))
                    {
                        ScopedSpec.ExponentMarker = ScopedMarker[0];
                    }
                    WriteU32(ScopedComponent, InField, Text::PackFormatSpec(ScopedSpec));
                    break;
                }
                if (InOps.name == "TextComponent" && InField.name == "Flags")
                {
                    const std::array<std::pair<const char*, std::uint32_t>, 5> ScopedTextFlags{{
                        {"Shadow", kTextFlagShadow},
                        {"Outline", kTextFlagOutline},
                        {"Center X", kTextFlagCenterX},
                        {"Right X", kTextFlagRightX},
                        {"Hidden", kTextFlagHidden},
                    }};
                    for (const auto& [ScopedName, ScopedMask] : ScopedTextFlags)
                    {
                        bool ScopedEnabled = (ScopedValue & ScopedMask) != 0;
                        if (ImGui::Checkbox(ScopedName, &ScopedEnabled))
                        {
                            ScopedValue = ScopedEnabled ? (ScopedValue | ScopedMask) : (ScopedValue & ~ScopedMask);
                        }
                    }
                    WriteU32(ScopedComponent, InField, ScopedValue);
                    break;
                }
                if (InOps.name == "CollisionPolicyComponent" && InField.name == "Mode")
                {
                    const std::array<const char*, 7> ScopedModeNames{CollisionModeName(CollisionMode::NoCollision),
                                                                     CollisionModeName(CollisionMode::QueryOnly),
                                                                     CollisionModeName(CollisionMode::TriggerOnly),
                                                                     CollisionModeName(CollisionMode::SimpleStatic),
                                                                     CollisionModeName(CollisionMode::SimpleDynamic),
                                                                     CollisionModeName(CollisionMode::ComplexStatic),
                                                                     CollisionModeName(CollisionMode::ComplexDynamic)};
                    int ScopedMode = static_cast<int>(ScopedValue < kCollisionModeCount ? ScopedValue : 0u);
                    BeginPropertyRow(ScopedLabel.c_str());
                    if (ImGui::Combo("##value", &ScopedMode, ScopedModeNames.data(),
                                     static_cast<int>(ScopedModeNames.size())))
                    {
                        WriteU32(ScopedComponent, InField, static_cast<std::uint32_t>(ScopedMode));
                    }
                    break;
                }
                if (InOps.name == "LightSourceComponent" && InField.name == "Type")
                {
                    const std::array<const char*, 3> ScopedTypeNames{"Global", "Omnidirectional", "Spot / Arc"};
                    int ScopedType = static_cast<int>(ScopedValue < kLightSourceTypeCount ? ScopedValue : 0u);
                    BeginPropertyRow(ScopedLabel.c_str());
                    if (ImGui::Combo("##value", &ScopedType, ScopedTypeNames.data(),
                                     static_cast<int>(ScopedTypeNames.size())))
                    {
                        WriteU32(ScopedComponent, InField, static_cast<std::uint32_t>(ScopedType));
                    }
                    break;
                }
                int ScopedDisplay = static_cast<int>(ScopedValue);
                BeginPropertyRow(ScopedLabel.c_str());
                if (ImGui::DragInt("##value", &ScopedDisplay, 1.0f, 0, std::numeric_limits<int>::max()))
                {
                    WriteU32(ScopedComponent, InField, static_cast<std::uint32_t>(std::max(ScopedDisplay, 0)));
                }
                break;
            }
            case Reflect::FieldKind::Bool:
            {
                bool ScopedValue = *BoolAt(ScopedComponent, InField);
                BeginPropertyRow(ScopedLabel.c_str());
                if (ImGui::Checkbox("##value", &ScopedValue))
                {
                    *BoolAt(ScopedComponent, InField) = ScopedValue;
                }
                break;
            }
            case Reflect::FieldKind::Vector2D:
            {
                Vector2D ScopedValue = *Vec2At(ScopedComponent, InField);
                float ScopedValues[2] = {ScopedValue.X, ScopedValue.Y};
                BeginPropertyRow(ScopedLabel.c_str());
                if (ImGui::DragFloat2("##value", ScopedValues, 0.1f))
                {
                    *Vec2At(ScopedComponent, InField) = Vector2D{ScopedValues[0], ScopedValues[1]};
                }
                break;
            }
            case Reflect::FieldKind::Color32:
            {
                const Color32 ScopedColor = Color32{ReadU32(ScopedComponent, InField)};
                float ScopedChannels[4] = {
                    static_cast<float>(ScopedColor.R()) / 255.0f, static_cast<float>(ScopedColor.G()) / 255.0f,
                    static_cast<float>(ScopedColor.B()) / 255.0f, static_cast<float>(ScopedColor.A()) / 255.0f};
                BeginPropertyRow(ScopedLabel.c_str());
                if (ImGui::ColorEdit4("##value", ScopedChannels, ImGuiColorEditFlags_AlphaBar))
                {
                    WriteU32(
                        ScopedComponent, InField,
                        Color32::FromFloats(ScopedChannels[0], ScopedChannels[1], ScopedChannels[2], ScopedChannels[3])
                            .rgba);
                }
                break;
            }
            case Reflect::FieldKind::Entity:
            {
                const std::uint32_t ScopedValue = ReadU32(ScopedComponent, InField);
                BeginPropertyRow(ScopedLabel.c_str());
                if (ScopedValue == Scene::kInvalidEntityIndex)
                {
                    ImGui::TextDisabled("null");
                }
                else
                {
                    ImGui::Text("entity %u", ScopedValue);
                }
                break;
            }
            case Reflect::FieldKind::String:
            {
                // Asset paths and other fixed-capacity strings.
                std::array<char, 256> ScopedBuffer{};
                const std::string ScopedCurrent = ReadStringField(ScopedComponent, InField);
                std::snprintf(ScopedBuffer.data(), ScopedBuffer.size(), "%s", ScopedCurrent.c_str());
                BeginPropertyRow(ScopedLabel.c_str());
                if (ImGui::InputText("##value", ScopedBuffer.data(), ScopedBuffer.size()))
                {
                    (void)WriteStringField(ScopedComponent, InField, ScopedBuffer.data());
                }
                break;
            }
            case Reflect::FieldKind::Int128:
            {
                const std::string ScopedCurrent = ReadInt128Field(ScopedComponent, InField).ToString();
                std::array<char, 64> ScopedBuffer{};
                std::snprintf(ScopedBuffer.data(), ScopedBuffer.size(), "%s", ScopedCurrent.c_str());
                BeginPropertyRow(ScopedLabel.c_str());
                if (ImGui::InputText("##value", ScopedBuffer.data(), ScopedBuffer.size()))
                {
                    Fby::BigNum::Int128 ScopedValue;
                    if (Fby::BigNum::Int128::FromString(ScopedBuffer.data(), &ScopedValue) == Fby::BigNum::Status::Ok)
                    {
                        WriteInt128Field(ScopedComponent, InField, ScopedValue);
                    }
                }
                break;
            }
            case Reflect::FieldKind::Decimal:
            {
                const std::string ScopedCurrent = ReadDecimalField(ScopedComponent, InField).ToString();
                std::array<char, 128> ScopedBuffer{};
                std::snprintf(ScopedBuffer.data(), ScopedBuffer.size(), "%s", ScopedCurrent.c_str());
                BeginPropertyRow(ScopedLabel.c_str());
                if (ImGui::InputText("##value", ScopedBuffer.data(), ScopedBuffer.size()))
                {
                    Fby::BigNum::Decimal128 ScopedValue;
                    if (Fby::BigNum::Decimal128::FromString(ScopedBuffer.data(), &ScopedValue) ==
                        Fby::BigNum::Status::Ok)
                    {
                        WriteDecimalField(ScopedComponent, InField, ScopedValue);
                    }
                }
                break;
            }
            case Reflect::FieldKind::UInt64:
            {
                std::array<char, 24> ScopedBuffer{};
                std::snprintf(ScopedBuffer.data(), ScopedBuffer.size(), "0x%016llX",
                              static_cast<unsigned long long>(ReadU64Field(ScopedComponent, InField)));
                BeginPropertyRow(ScopedLabel.c_str());
                if (ImGui::InputText("##value", ScopedBuffer.data(), ScopedBuffer.size()))
                {
                    std::string_view ScopedText(ScopedBuffer.data());
                    if (ScopedText.starts_with("0x") || ScopedText.starts_with("0X"))
                    {
                        ScopedText.remove_prefix(2);
                    }
                    std::uint64_t ScopedValue = 0;
                    const auto ScopedResult =
                        std::from_chars(ScopedText.data(), ScopedText.data() + ScopedText.size(), ScopedValue, 16);
                    if (ScopedResult.ec == std::errc{} && ScopedResult.ptr == ScopedText.data() + ScopedText.size())
                    {
                        (void)WriteU64Field(ScopedComponent, InField, ScopedValue);
                    }
                }
                break;
            }
            case Reflect::FieldKind::Vector2DArray:
            {
                const std::size_t ScopedPointCount = InField.size / sizeof(Vector2D);
                BeginPropertyRow(ScopedLabel.c_str());
                ImGui::TextDisabled("%zu control point(s)", ScopedPointCount);
                break;
            }
        }
        ImGui::PopID();
        if (ScopedTrackable)
        {
            TrackFieldChange(InEntity, InCodec, InField, ScopedBefore);
        }
    }

    void DrawBoneTree(Ecs::Registry& InRegistry, Entity InEntity, const SkeletonComponent& InSkeleton,
                      std::uint16_t InBone)
    {
        if (InBone >= InSkeleton.BoneCount)
        {
            return;
        }
        bool ScopedHasChildren = false;
        for (std::uint16_t ScopedI = 0; ScopedI < InSkeleton.BoneCount; ++ScopedI)
        {
            if (InSkeleton.ParentIndices[ScopedI] == static_cast<std::int16_t>(InBone))
            {
                ScopedHasChildren = true;
                break;
            }
        }

        ImGui::PushID(static_cast<int>(InBone));
        ImGuiTreeNodeFlags ScopedFlags =
            ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen;
        if (!ScopedHasChildren)
        {
            ScopedFlags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        }
        const bool ScopedOpen =
            ImGui::TreeNodeEx("##bone", ScopedFlags, "Bone %u  0x%016llX", static_cast<unsigned>(InBone),
                              static_cast<unsigned long long>(InSkeleton.BoneNameHashes[InBone]));
        if (ImGui::IsItemClicked())
        {
            SelectOnly(InRegistry, InEntity);
        }
        if (ImGui::IsItemHovered())
        {
            const Vector2D ScopedPosition = InSkeleton.LocalPositions[InBone];
            EditorTooltip("parent: %d\nlocal: (%.2f, %.2f)  rot %.3f rad  scale %.3f",
                          static_cast<int>(InSkeleton.ParentIndices[InBone]), ScopedPosition.X, ScopedPosition.Y,
                          InSkeleton.LocalRotations[InBone], InSkeleton.LocalScales[InBone]);
        }
        if (ScopedOpen && ScopedHasChildren)
        {
            for (std::uint16_t ScopedI = 0; ScopedI < InSkeleton.BoneCount; ++ScopedI)
            {
                if (InSkeleton.ParentIndices[ScopedI] == static_cast<std::int16_t>(InBone))
                {
                    DrawBoneTree(InRegistry, InEntity, InSkeleton, ScopedI);
                }
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    void DrawHierarchy(Ecs::SystemContext& InContext)
    {
        if (!showHierarchy)
        {
            return;
        }
        if (!BeginEditorPanel("Hierarchy", &showHierarchy))
        {
            EndEditorPanel();
            return;
        }

        Ecs::Registry& ScopedRegistry = InContext.registry;
        const Entity ScopedSelected = SelectedEntity(ScopedRegistry);
        const std::size_t ScopedSelectedCount = ScopedRegistry.view<EditorSelected>().size();
        ImGui::TextDisabled("%u entities | %u selected", static_cast<unsigned>(ScopedRegistry.storage<Entity>().size()),
                            static_cast<unsigned>(ScopedSelectedCount));
        if (ImGui::SmallButton("+ Entity"))
        {
            SpawnEntity(InContext, camera.center);
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(ScopedSelected == Fby::kNullEntity);
        if (ImGui::SmallButton("Duplicate"))
        {
            DuplicateSelected(InContext);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Delete"))
        {
            DeleteSelectedEntities(InContext);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        (void)ImGui::InputTextWithHint("##hierarchySearch", "Search...", hierarchySearch, sizeof(hierarchySearch));
        ImGui::Separator();

        const auto ScopedContains = [](std::string_view InText, std::string_view InNeedle)
        {
            if (InNeedle.empty())
            {
                return true;
            }
            const auto ScopedLower = [](char InChar)
            {
                return static_cast<char>(std::tolower(static_cast<unsigned char>(InChar)));
            };
            for (std::size_t ScopedStart = 0; ScopedStart + InNeedle.size() <= InText.size(); ++ScopedStart)
            {
                bool ScopedMatch = true;
                for (std::size_t ScopedI = 0; ScopedI < InNeedle.size(); ++ScopedI)
                {
                    if (ScopedLower(InText[ScopedStart + ScopedI]) != ScopedLower(InNeedle[ScopedI]))
                    {
                        ScopedMatch = false;
                        break;
                    }
                }
                if (ScopedMatch)
                {
                    return true;
                }
            }
            return false;
        };

        const auto& ScopedStorage = ScopedRegistry.storage<Entity>();
        for (const Entity ScopedEntity : ScopedStorage)
        {
            if (!ScopedRegistry.valid(ScopedEntity))
            {
                continue;
            }
            const bool ScopedIsSelected = ScopedRegistry.all_of<EditorSelected>(ScopedEntity);
            std::string ScopedLabel = EntityLabel(ScopedRegistry, ScopedEntity);
            if (!ScopedContains(ScopedLabel, hierarchySearch))
            {
                continue;
            }
            if (ScopedRegistry.all_of<Hidden>(ScopedEntity))
            {
                ScopedLabel += "  [hidden]";
            }
            if (renamingEntity == ScopedEntity)
            {
                char ScopedBuffer[64] = {};
                const auto* ScopedName = ScopedRegistry.try_get<NameComponent>(ScopedEntity);
                if (ScopedName != nullptr)
                {
                    std::snprintf(ScopedBuffer, sizeof(ScopedBuffer), "%s", ScopedName->Value);
                }
                ImGui::SetNextItemWidth(-1.0f);
                if (!renameFocusTaken)
                {
                    ImGui::SetKeyboardFocusHere();
                    renameFocusTaken = true;
                }
                const bool ScopedCommitted = ImGui::InputText("##rename", ScopedBuffer, sizeof(ScopedBuffer),
                                                              ImGuiInputTextFlags_EnterReturnsTrue);
                const bool ScopedCancelled = ImGui::IsKeyPressed(ImGuiKey_Escape);
                const bool ScopedDeactivated = ImGui::IsItemDeactivated();
                if (ScopedCommitted || ScopedCancelled || ScopedDeactivated)
                {
                    if (ScopedCommitted || (ScopedDeactivated && !ScopedCancelled))
                    {
                        SetEntityName(InContext, ScopedEntity, ScopedBuffer);
                    }
                    renamingEntity = Fby::kNullEntity;
                    renameFocusTaken = false;
                }
                continue;
            }
            if (ImGui::Selectable(ScopedLabel.c_str(), ScopedIsSelected, ImGuiSelectableFlags_AllowDoubleClick))
            {
                if (ImGui::GetIO().KeyCtrl)
                {
                    ToggleSelection(ScopedRegistry, ScopedEntity);
                }
                else if (ImGui::GetIO().KeyShift)
                {
                    AddToSelection(ScopedRegistry, ScopedEntity);
                }
                else
                {
                    SelectOnly(ScopedRegistry, ScopedEntity);
                }
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                {
                    renamingEntity = ScopedEntity;
                    renameFocusTaken = false;
                }
            }
            if (ScopedIsSelected)
            {
                ImGui::SetItemDefaultFocus();
            }

            if (const auto* ScopedSkeleton = ScopedRegistry.try_get<SkeletonComponent>(ScopedEntity))
            {
                ImGui::SameLine();
                ImGui::TextDisabled("  [skeleton: %u bones]", static_cast<unsigned>(ScopedSkeleton->BoneCount));
                if (ScopedIsSelected)
                {
                    ImGui::Indent(14.0f);
                    for (std::uint16_t ScopedBone = 0; ScopedBone < ScopedSkeleton->BoneCount; ++ScopedBone)
                    {
                        if (ScopedSkeleton->ParentIndices[ScopedBone] < 0)
                        {
                            DrawBoneTree(ScopedRegistry, ScopedEntity, *ScopedSkeleton, ScopedBone);
                        }
                    }
                    ImGui::Unindent(14.0f);
                }
            }
        }
        EndEditorPanel();
    }

    void DrawInspector(Ecs::SystemContext& InContext)
    {
        if (!showInspector)
        {
            return;
        }
        if (!BeginEditorPanel("Inspector", &showInspector))
        {
            EndEditorPanel();
            return;
        }

        Ecs::Registry& ScopedRegistry = InContext.registry;
        const Entity ScopedEntity = SelectedEntity(ScopedRegistry);
        const std::size_t ScopedSelectedCount = ScopedRegistry.view<EditorSelected>().size();
        if (ScopedSelectedCount > 1)
        {
            if (interaction.Active())
            {
                CommitInteraction();
            }
            ImGui::Text("%u entities selected", static_cast<unsigned>(ScopedSelectedCount));
            if (ImGui::SmallButton("Delete selected"))
            {
                DeleteSelectedEntities(InContext);
            }
            ImGui::Separator();
            ImGui::TextDisabled("Select one entity to edit its components.");
            ImGui::TextDisabled("Ctrl+click toggles, Shift+click adds, Ctrl+C/V copies and pastes.");
            EndEditorPanel();
            return;
        }
        if (ScopedEntity == Fby::kNullEntity || !ScopedRegistry.valid(ScopedEntity))
        {
            if (interaction.Active())
            {
                CommitInteraction();
            }
            ImGui::TextDisabled("No entity selected.");
            EndEditorPanel();
            return;
        }
        if (interaction.Active() && interaction.Target() != ScopedEntity)
        {
            // Selection changed mid-gesture: finish the old edit before showing
            CommitInteraction();
        }

        ImGui::Text("Entity %u", static_cast<unsigned>(Fby::ToIntegral(ScopedEntity)));
        if (const auto* ScopedName = ScopedRegistry.try_get<NameComponent>(ScopedEntity);
            ScopedName != nullptr && ScopedName->Value[0] != '\0')
        {
            ImGui::SameLine();
            ImGui::TextDisabled("(%s)", ScopedName->Value);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Delete"))
        {
            DeleteSelectedEntities(InContext);
            EndEditorPanel();
            return;
        }
        ImGui::Separator();

        std::vector<const Scene::ComponentOps*> ScopedPresent;
        std::vector<const Scene::ComponentOps*> ScopedMissing;
        for (const Scene::ComponentOps& ScopedOps : components.All())
        {
            if (ScopedOps.has(ScopedRegistry, ScopedEntity))
            {
                ScopedPresent.push_back(&ScopedOps);
            }
            else
            {
                ScopedMissing.push_back(&ScopedOps);
            }
        }

        for (const Scene::ComponentOps* ScopedOps : ScopedPresent)
        {
            ImGui::PushID(ScopedOps->name.data());
            const bool ScopedOpen =
                ImGui::CollapsingHeader(std::string(ScopedOps->name).c_str(), ImGuiTreeNodeFlags_DefaultOpen);
            if (ScopedOpen)
            {
                if (ScopedOps->empty)
                {
                    ImGui::TextDisabled("marker component");
                }
                const std::uint16_t ScopedCodec = commands.CodecForLayoutHash(ScopedOps->layoutHash);
                for (const Reflect::FieldDesc& ScopedField : ScopedOps->fields)
                {
                    DrawField(ScopedRegistry, ScopedEntity, *ScopedOps, ScopedField, ScopedCodec);
                }
                if (ImGui::SmallButton("Remove"))
                {
                    CommitInteraction();
                    ScopedOps->remove(ScopedRegistry, ScopedEntity);
                    ImGui::PopID();
                    break;
                }
            }
            ImGui::PopID();
        }

        // The skeleton is a large SoA block whose persistent form is the asset, not the component.
        DrawSkeletonAssetInspector(InContext, ScopedEntity, skeletonPanel);
        spriteAnimationPanel.textureArray = textureArray;
        spriteAnimationPanel.thumbnails = &thumbnails;
        DrawSpriteAnimationInspector(InContext, ScopedEntity, spriteAnimationPanel);

        if (!ScopedMissing.empty())
        {
            ImGui::Separator();
            if (ImGui::BeginCombo("Add component", "select..."))
            {
                for (const Scene::ComponentOps* ScopedOps : ScopedMissing)
                {
                    if (ImGui::Selectable(std::string(ScopedOps->name).c_str()))
                    {
                        CommitInteraction();
                        ScopedOps->emplaceDefault(ScopedRegistry, ScopedEntity);
                    }
                }
                ImGui::EndCombo();
            }
        }
        // A drag ends when ImGui no longer has an active item.
        if (interaction.Active() && interaction.GetSource() == InteractionTransaction::Source::InspectorField &&
            !ImGui::IsAnyItemActive())
        {
            CommitInteraction();
        }
        EndEditorPanel();
    }

    // Physical path of a VFS-relative asset inside the project.
    [[nodiscard]] std::filesystem::path AssetPhysicalPath(std::string_view InRelative) const
    {
        return projectConfig.Paths().assets / std::filesystem::path(InRelative);
    }

    // The asset file manager only accepts plain leaf names: no separators, no
    // traversal and a sane length.
    [[nodiscard]] static bool IsSafeAssetName(std::string_view InName)
    {
        if (InName.empty() || InName.size() > 96 || InName == "." || InName == "..")
        {
            return false;
        }
        return InName.find('/') == std::string_view::npos && InName.find('\\') == std::string_view::npos &&
               InName.find("..") == std::string_view::npos;
    }

    void RefreshAssetIndex(Ecs::SystemContext& InContext)
    {
        if (InContext.assets != nullptr)
        {
            InContext.assets->RefreshDirectoryMounts();
        }
    }

    void OpenAssetDialog(AssetDialog InDialog, std::string_view InInitialName)
    {
        assetDialog = InDialog;
        std::snprintf(assetDialogName, sizeof(assetDialogName), "%.*s", static_cast<int>(InInitialName.size()),
                      InInitialName.data());
        assetDialogOpen = true;
        assetDialogFocus = true;
    }

    void ApplyAssetDialog(Ecs::SystemContext& InContext)
    {
        const std::string ScopedName(assetDialogName);
        if (!IsSafeAssetName(ScopedName))
        {
            SetStatus("invalid asset name");
            assetDialog = AssetDialog::None;
            return;
        }
        const std::filesystem::path ScopedRoot = projectConfig.Paths().assets;
        const std::filesystem::path ScopedDirectory = assetDir.empty() ? ScopedRoot : ScopedRoot / assetDir;
        std::error_code ScopedCode;
        if (assetDialog == AssetDialog::NewFolder)
        {
            std::filesystem::create_directories(ScopedDirectory / ScopedName, ScopedCode);
            if (ScopedCode)
            {
                SetStatus("cannot create folder: " + ScopedCode.message());
            }
            else
            {
                SetStatus("folder created: " + ScopedName);
            }
        }
        else if (assetDialog == AssetDialog::NewScene)
        {
            std::filesystem::path ScopedFile = ScopedDirectory / ScopedName;
            if (ScopedFile.extension() != ".toml")
            {
                ScopedFile += ".toml";
            }
            Scene::SceneMeta ScopedMeta;
            ScopedMeta.name = ScopedFile.stem().string();
            ScopedMeta.author = "FemboyEngine2D editor";
            Ecs::Registry ScopedEmpty;
            std::string ScopedError;
            if (!Scene::SaveSceneFileToml(ScopedFile, ScopedEmpty, components, ScopedMeta, &ScopedError))
            {
                SetStatus("cannot create scene: " + ScopedError);
            }
            else
            {
                SetStatus("scene created: " + ScopedFile.filename().string());
            }
        }
        else if (assetDialog == AssetDialog::Rename && !selectedAsset.empty())
        {
            const std::filesystem::path ScopedOld = AssetPhysicalPath(selectedAsset);
            std::filesystem::path ScopedNew = ScopedOld.parent_path() / ScopedName;
            if (ScopedNew.extension().empty())
            {
                ScopedNew += ScopedOld.extension();
            }
            std::filesystem::rename(ScopedOld, ScopedNew, ScopedCode);
            if (ScopedCode)
            {
                SetStatus("cannot rename: " + ScopedCode.message());
            }
            else
            {
                selectedAsset = std::filesystem::relative(ScopedNew, ScopedRoot, ScopedCode).generic_string();
                SetStatus("renamed to " + ScopedNew.filename().string());
            }
        }
        RefreshAssetIndex(InContext);
        assetDialog = AssetDialog::None;
    }

    void DuplicateSelectedAsset(Ecs::SystemContext& InContext)
    {
        if (selectedAsset.empty())
        {
            return;
        }
        const std::filesystem::path ScopedSource = AssetPhysicalPath(selectedAsset);
        std::error_code ScopedCode;
        std::filesystem::path ScopedCopy =
            ScopedSource.parent_path() / (ScopedSource.stem().string() + "_copy" + ScopedSource.extension().string());
        for (int ScopedIndex = 2; std::filesystem::exists(ScopedCopy, ScopedCode) && ScopedIndex < 1000; ++ScopedIndex)
        {
            ScopedCopy = ScopedSource.parent_path() / (ScopedSource.stem().string() + "_copy" +
                                                       std::to_string(ScopedIndex) + ScopedSource.extension().string());
        }
        std::filesystem::copy_file(ScopedSource, ScopedCopy, ScopedCode);
        if (ScopedCode)
        {
            SetStatus("cannot duplicate: " + ScopedCode.message());
            return;
        }
        RefreshAssetIndex(InContext);
        selectedAsset =
            std::filesystem::relative(ScopedCopy, projectConfig.Paths().assets, ScopedCode).generic_string();
        SetStatus("duplicated: " + ScopedCopy.filename().string());
    }

    void DeleteSelectedAsset(Ecs::SystemContext& InContext)
    {
        if (assetDeleteTarget.empty())
        {
            return;
        }
        std::error_code ScopedCode;
        const bool ScopedRemoved = std::filesystem::remove(AssetPhysicalPath(assetDeleteTarget), ScopedCode);
        if (!ScopedRemoved || ScopedCode)
        {
            SetStatus("cannot delete: " + ScopedCode.message());
        }
        else
        {
            SetStatus("deleted: " + assetDeleteTarget);
            selectedAsset.clear();
        }
        assetDeleteTarget.clear();
        RefreshAssetIndex(InContext);
    }

    void DrawAssets(Ecs::SystemContext& InContext)
    {
        if (!showAssets)
        {
            return;
        }
        if (!BeginEditorPanel("Assets", &showAssets))
        {
            EndEditorPanel();
            return;
        }
        VirtualFileSystem* ScopedAssetVfs = InContext.assets;
        if (ScopedAssetVfs == nullptr)
        {
            ImGui::TextDisabled("No VFS mounted.");
            EndEditorPanel();
            return;
        }

        if (ImGui::SmallButton("vfs:/"))
        {
            assetDir.clear();
        }
        ImGui::SameLine();
        if (!assetDir.empty() && ImGui::SmallButton(".."))
        {
            const std::size_t ScopedSlash = assetDir.find_last_of('/');
            assetDir = ScopedSlash == std::string::npos ? std::string{} : assetDir.substr(0, ScopedSlash);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", assetDir.empty() ? "/" : assetDir.c_str());
        ImGui::Separator();

        // File operations for the project assets. Paths are confined to the
        // project assets root and the VFS index is refreshed after each change.
        ImGui::BeginDisabled(!projectOpen);
        if (ImGui::SmallButton("New Folder"))
        {
            OpenAssetDialog(AssetDialog::NewFolder, "NewFolder");
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("New Scene"))
        {
            OpenAssetDialog(AssetDialog::NewScene, "untitled");
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(selectedAsset.empty());
        if (ImGui::SmallButton("Rename"))
        {
            OpenAssetDialog(AssetDialog::Rename, std::filesystem::path(selectedAsset).stem().string());
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Duplicate"))
        {
            DuplicateSelectedAsset(InContext);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Delete"))
        {
            assetDeleteTarget = selectedAsset;
            assetDeleteOpen = true;
        }
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        ImGui::Separator();

        if (assetDialogOpen)
        {
            ImGui::OpenPopup("Asset name");
            assetDialogOpen = false;
        }
        if (ImGui::BeginPopupModal("Asset name", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            const char* ScopedTitle = assetDialog == AssetDialog::NewFolder
                                          ? "New folder name"
                                          : (assetDialog == AssetDialog::NewScene ? "New scene name" : "New name");
            ImGui::TextUnformatted(ScopedTitle);
            ImGui::SetNextItemWidth(300.0f);
            if (assetDialogFocus)
            {
                ImGui::SetKeyboardFocusHere();
                assetDialogFocus = false;
            }
            const bool ScopedEntered = ImGui::InputText("##assetName", assetDialogName, sizeof(assetDialogName),
                                                        ImGuiInputTextFlags_EnterReturnsTrue);
            if (ImGui::Button("OK", ImVec2(90.0f, 0.0f)) || ScopedEntered)
            {
                ApplyAssetDialog(InContext);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(90.0f, 0.0f)))
            {
                assetDialog = AssetDialog::None;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (assetDeleteOpen)
        {
            ImGui::OpenPopup("Delete asset");
            assetDeleteOpen = false;
        }
        if (ImGui::BeginPopupModal("Delete asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Delete '%s'?", assetDeleteTarget.c_str());
            ImGui::TextDisabled("This cannot be undone.");
            if (ImGui::Button("Delete", ImVec2(90.0f, 0.0f)))
            {
                DeleteSelectedAsset(InContext);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(90.0f, 0.0f)))
            {
                assetDeleteTarget.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        std::set<std::string> ScopedDirectories;
        std::set<std::string> ScopedFiles;
        const std::string ScopedPrefix = assetDir.empty() ? std::string{} : assetDir + "/";
        for (const std::string_view ScopedPathView : ScopedAssetVfs->List(assetDir))
        {
            const std::string ScopedPath(ScopedPathView);
            if (ScopedPath.size() <= ScopedPrefix.size() ||
                ScopedPath.compare(0, ScopedPrefix.size(), ScopedPrefix) != 0)
            {
                continue;
            }
            const std::string ScopedRelative = ScopedPath.substr(ScopedPrefix.size());
            const std::size_t ScopedSlash = ScopedRelative.find('/');
            if (ScopedSlash == std::string::npos)
            {
                ScopedFiles.insert(ScopedRelative);
            }
            else
            {
                ScopedDirectories.insert(ScopedRelative.substr(0, ScopedSlash));
            }
        }

        for (const std::string& ScopedDirectory : ScopedDirectories)
        {
            const std::string ScopedLabel = "[" + ScopedDirectory + "]";
            if (ImGui::Selectable(ScopedLabel.c_str()))
            {
                assetDir = assetDir.empty() ? ScopedDirectory : assetDir + "/" + ScopedDirectory;
                selectedAsset.clear();
            }
        }
        for (const std::string& ScopedFile : ScopedFiles)
        {
            const std::string ScopedPath = assetDir.empty() ? ScopedFile : assetDir + "/" + ScopedFile;
            if (ImGui::Selectable(ScopedFile.c_str(), selectedAsset == ScopedPath,
                                  ImGuiSelectableFlags_AllowDoubleClick))
            {
                selectedAsset = ScopedPath;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && ScopedFile.ends_with(".toml"))
                {
                    std::string ScopedError;
                    if (owner->LoadScene(InContext, AssetPhysicalPath(ScopedPath), &ScopedError))
                    {
                        desc.scenePath = AssetPhysicalPath(ScopedPath);
                        SetStatus("opened scene " + ScopedPath);
                    }
                    else
                    {
                        SetStatus("open failed: " + ScopedError);
                    }
                }
            }
            if (ImGui::BeginPopupContextItem())
            {
                if (ImGui::MenuItem("Open in editor", nullptr, false, ScopedFile.ends_with(".toml")))
                {
                    std::string ScopedError;
                    if (owner->LoadScene(InContext, AssetPhysicalPath(ScopedPath), &ScopedError))
                    {
                        desc.scenePath = AssetPhysicalPath(ScopedPath);
                        SetStatus("opened scene " + ScopedPath);
                    }
                    else
                    {
                        SetStatus("open failed: " + ScopedError);
                    }
                }
                if (ImGui::MenuItem("Rename"))
                {
                    selectedAsset = ScopedPath;
                    OpenAssetDialog(AssetDialog::Rename, std::filesystem::path(ScopedFile).stem().string());
                }
                if (ImGui::MenuItem("Duplicate"))
                {
                    selectedAsset = ScopedPath;
                    DuplicateSelectedAsset(InContext);
                }
                if (ImGui::MenuItem("Delete"))
                {
                    selectedAsset = ScopedPath;
                    assetDeleteTarget = ScopedPath;
                    assetDeleteOpen = true;
                }
                ImGui::EndPopup();
            }
        }

        if (!selectedAsset.empty())
        {
            ImGui::Separator();
            const auto ScopedEntry = ScopedAssetVfs->Find(selectedAsset);
            const auto ScopedBytes = ScopedAssetVfs->Read(selectedAsset);
            if (ScopedEntry && ScopedBytes)
            {
                PushMono();
                ImGui::Text("vfs:/%s", selectedAsset.c_str());
                ImGui::Text("size: %zu bytes", ScopedBytes->size());
                PopMono();

                std::string ScopedPreview;
                std::size_t ScopedPrintable = 0;
                const std::size_t ScopedInspect = std::min<std::size_t>(ScopedBytes->size(), 512);
                for (std::size_t ScopedI = 0; ScopedI < ScopedInspect; ++ScopedI)
                {
                    const auto ScopedCharacter = static_cast<unsigned char>((*ScopedBytes)[ScopedI]);
                    if (ScopedCharacter == '\n' || ScopedCharacter == '\r' || ScopedCharacter == '\t' ||
                        (ScopedCharacter >= 0x20 && ScopedCharacter < 0x7F) || ScopedCharacter >= 0xC0)
                    {
                        ++ScopedPrintable;
                    }
                }
                if (ScopedInspect > 0 && ScopedPrintable + 8 >= ScopedInspect)
                {
                    ScopedPreview.assign(reinterpret_cast<const char*>(ScopedBytes->data()),
                                         std::min<std::size_t>(ScopedBytes->size(), 1024));
                }
                else
                {
                    const std::size_t ScopedDump = std::min<std::size_t>(ScopedBytes->size(), 64);
                    for (std::size_t ScopedI = 0; ScopedI < ScopedDump; ++ScopedI)
                    {
                        ScopedPreview += std::format("{:02X} ", static_cast<unsigned>((*ScopedBytes)[ScopedI]));
                        if (ScopedI % 16 == 15)
                        {
                            ScopedPreview += '\n';
                        }
                    }
                }
                PushMono();
                ImGui::BeginChild("##asset_preview", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
                ImGui::TextUnformatted(ScopedPreview.c_str());
                ImGui::EndChild();
                PopMono();
            }
        }
        EndEditorPanel();
    }

    void DrawConsole()
    {
        if (!showConsole)
        {
            return;
        }
        if (!BeginEditorPanel("Console", &showConsole))
        {
            EndEditorPanel();
            return;
        }
        PushMono();
        ImGui::BeginChild("##console_log", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders,
                          ImGuiWindowFlags_HorizontalScrollbar);
        for (const std::string& ScopedLine : consoleLines)
        {
            ImGui::TextUnformatted(ScopedLine.c_str());
        }
        if (consoleScrollToBottom)
        {
            ImGui::SetScrollHereY(1.0f);
            consoleScrollToBottom = false;
        }
        ImGui::EndChild();
        PopMono();
        EndEditorPanel();
    }

    void DrawViewport(Ecs::SystemContext& InContext)
    {
        if (!showViewport)
        {
            return;
        }
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        if (!BeginEditorPanel("Viewport", &showViewport, ImGuiWindowFlags_NoScrollbar))
        {
            EndEditorPanel();
            ImGui::PopStyleVar();
            return;
        }
        ImGui::PopStyleVar();

        const ImVec2 ScopedAvailable = ImGui::GetContentRegionAvail();
        const ImGuiIO& ScopedIo = ImGui::GetIO();
        const float ScopedPixelWidth = std::max(1.0f, ScopedAvailable.x * ScopedIo.DisplayFramebufferScale.x);
        const float ScopedPixelHeight = std::max(1.0f, ScopedAvailable.y * ScopedIo.DisplayFramebufferScale.y);
        EnsureViewportTarget(ScopedPixelWidth, ScopedPixelHeight);

        if (targetTexture != ImTextureID_Invalid)
        {
            ImGui::Image(static_cast<ImTextureRef>(targetTexture), ScopedAvailable);
        }
        else
        {
            ImGui::Dummy(ScopedAvailable);
            ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                                                      IM_COL32(30, 32, 40, 255));
        }
        const ImVec2 ScopedImageMin = ImGui::GetItemRectMin();
        const ImVec2 ScopedImageMax = ImGui::GetItemRectMax();
        const bool ScopedImageHovered = ImGui::IsItemHovered();

        // World grid overlay. The grid follows the projection of the selected
        // tilemap (axis-aligned, iso diamonds or hex lattice) and highlights the
        // world axes.
        if (projectConfig.Editor().showGrid)
        {
            ImDrawList* ScopedDraw = ImGui::GetWindowDrawList();
            ScopedDraw->PushClipRect(ScopedImageMin, ScopedImageMax, true);
            const float ScopedZoom = std::max(camera.zoom, 0.01f);
            const ImVec2 ScopedCenterScreen{(ScopedImageMin.x + ScopedImageMax.x) * 0.5f,
                                            (ScopedImageMin.y + ScopedImageMax.y) * 0.5f};
            const float ScopedWorldLeft = camera.center.X - (ScopedImageMax.x - ScopedImageMin.x) * 0.5f / ScopedZoom;
            const float ScopedWorldRight = camera.center.X + (ScopedImageMax.x - ScopedImageMin.x) * 0.5f / ScopedZoom;
            const float ScopedWorldTop = camera.center.Y - (ScopedImageMax.y - ScopedImageMin.y) * 0.5f / ScopedZoom;
            const float ScopedWorldBottom = camera.center.Y + (ScopedImageMax.y - ScopedImageMin.y) * 0.5f / ScopedZoom;
            const ImU32 ScopedGridColor = SymbolColorU32(EditorSymbolRole::Grid, 0.24f);
            const ImU32 ScopedAxisColor = SymbolColorU32(EditorSymbolRole::AxisX, 0.65f);
            const ImU32 ScopedAxisYColor = SymbolColorU32(EditorSymbolRole::AxisY, 0.65f);

            const auto ScopedToScreen = [&](Vector2D InWorld)
            {
                return ImVec2(ScopedCenterScreen.x + (InWorld.X - camera.center.X) * ScopedZoom,
                              ScopedCenterScreen.y + (InWorld.Y - camera.center.Y) * ScopedZoom);
            };

            // Projection and cell size come from the selected tilemap; with no
            // tilemap selected the overlay stays a plain 64-unit square grid.
            Grid::GridDesc ScopedGrid;
            ScopedGrid.Mode = Grid::ProjectionMode::TopDown;
            ScopedGrid.CellSize = Vector2D{64.0f, 64.0f};
            Grid::Cell ScopedMapSize{64, 64};
            const Entity ScopedGridEntity = SelectedEntity(InContext.registry);
            if (const TilemapComponent* ScopedMap = InContext.registry.try_get<TilemapComponent>(ScopedGridEntity))
            {
                ScopedGrid = ScopedMap->Grid;
                ScopedMapSize = Grid::Cell{static_cast<std::int32_t>(ScopedMap->Width),
                                           static_cast<std::int32_t>(ScopedMap->Height)};
            }

            if (ScopedGrid.Mode == Grid::ProjectionMode::IsoRect || ScopedGrid.Mode == Grid::ProjectionMode::IsoHex)
            {
                const Grid::Cell ScopedMin = Grid::WorldToCell(ScopedGrid, Vector2D{ScopedWorldLeft, ScopedWorldTop});
                const Grid::Cell ScopedMax =
                    Grid::WorldToCell(ScopedGrid, Vector2D{ScopedWorldRight, ScopedWorldBottom});
                const std::int32_t ScopedLowX = std::max(ScopedMin.X, -256);
                const std::int32_t ScopedLowY = std::max(ScopedMin.Y, -256);
                const std::int32_t ScopedHighX = std::min(ScopedMax.X + 2, ScopedMapSize.X + 256);
                const std::int32_t ScopedHighY = std::min(ScopedMax.Y + 2, ScopedMapSize.Y + 256);
                const Vector2D ScopedExtent = Grid::CellExtent(ScopedGrid);
                const float ScopedHalfWidth = ScopedExtent.X * 0.5f;
                const float ScopedHalfHeight = ScopedExtent.Y * 0.5f;
                std::uint32_t ScopedDrawn = 0;
                for (std::int32_t ScopedY = ScopedLowY; ScopedY <= ScopedHighY && ScopedDrawn < 2048; ++ScopedY)
                {
                    for (std::int32_t ScopedX = ScopedLowX; ScopedX <= ScopedHighX && ScopedDrawn < 2048; ++ScopedX)
                    {
                        const Vector2D ScopedCenter = Grid::CellToWorld(ScopedGrid, Grid::Cell{ScopedX, ScopedY});
                        ImVec2 ScopedPoints[6] = {};
                        int ScopedCount = 0;
                        if (ScopedGrid.Mode == Grid::ProjectionMode::IsoHex)
                        {
                            const float ScopedRadius = ScopedHalfHeight;
                            for (int ScopedVertex = 0; ScopedVertex < 6; ++ScopedVertex)
                            {
                                const float ScopedAngle = 1.5707963f + static_cast<float>(ScopedVertex) * 1.0471976f;
                                ScopedPoints[ScopedVertex] =
                                    ScopedToScreen(Vector2D{ScopedCenter.X + std::cos(ScopedAngle) * ScopedRadius,
                                                            ScopedCenter.Y + std::sin(ScopedAngle) * ScopedRadius});
                            }
                            ScopedCount = 6;
                        }
                        else
                        {
                            ScopedPoints[0] =
                                ScopedToScreen(Vector2D{ScopedCenter.X, ScopedCenter.Y - ScopedHalfHeight});
                            ScopedPoints[1] =
                                ScopedToScreen(Vector2D{ScopedCenter.X + ScopedHalfWidth, ScopedCenter.Y});
                            ScopedPoints[2] =
                                ScopedToScreen(Vector2D{ScopedCenter.X, ScopedCenter.Y + ScopedHalfHeight});
                            ScopedPoints[3] =
                                ScopedToScreen(Vector2D{ScopedCenter.X - ScopedHalfWidth, ScopedCenter.Y});
                            ScopedCount = 4;
                        }
                        const ImU32 ScopedColor = (ScopedX == 0 || ScopedY == 0) ? ScopedAxisColor : ScopedGridColor;
                        ScopedDraw->AddPolyline(ScopedPoints, ScopedCount, ScopedColor, ImDrawFlags_Closed, 1.0f);
                        ++ScopedDrawn;
                    }
                }
            }
            else
            {
                const float ScopedStep = std::max(ScopedGrid.CellSize.X, 4.0f);
                const float ScopedFirstX = std::floor(ScopedWorldLeft / ScopedStep) * ScopedStep;
                const int ScopedLineCountX =
                    std::max(0, static_cast<int>(std::floor((ScopedWorldRight - ScopedFirstX) / ScopedStep)) + 1);
                for (int ScopedIndex = 0; ScopedIndex < ScopedLineCountX; ++ScopedIndex)
                {
                    const float ScopedX = ScopedFirstX + static_cast<float>(ScopedIndex) * ScopedStep;
                    const float ScopedScreenX = ScopedCenterScreen.x + (ScopedX - camera.center.X) * ScopedZoom;
                    const ImU32 ScopedColor = std::fabs(ScopedX) < 0.001f ? ScopedAxisYColor : ScopedGridColor;
                    ScopedDraw->AddLine(ImVec2(ScopedScreenX, ScopedImageMin.y),
                                        ImVec2(ScopedScreenX, ScopedImageMax.y), ScopedColor, 1.0f);
                }
                const float ScopedStepY = std::max(ScopedGrid.CellSize.Y, 4.0f);
                const float ScopedFirstY = std::floor(ScopedWorldTop / ScopedStepY) * ScopedStepY;
                const int ScopedLineCountY =
                    std::max(0, static_cast<int>(std::floor((ScopedWorldBottom - ScopedFirstY) / ScopedStepY)) + 1);
                for (int ScopedIndex = 0; ScopedIndex < ScopedLineCountY; ++ScopedIndex)
                {
                    const float ScopedY = ScopedFirstY + static_cast<float>(ScopedIndex) * ScopedStepY;
                    const float ScopedScreenY = ScopedCenterScreen.y + (ScopedY - camera.center.Y) * ScopedZoom;
                    const ImU32 ScopedColor = std::fabs(ScopedY) < 0.001f ? ScopedAxisColor : ScopedGridColor;
                    ScopedDraw->AddLine(ImVec2(ScopedImageMin.x, ScopedScreenY),
                                        ImVec2(ScopedImageMax.x, ScopedScreenY), ScopedColor, 1.0f);
                }
            }
            ScopedDraw->PopClipRect();
        }

        // Physics debug overlay: Box2D outlines, rope links, Verlet cloth.
        if (showPhysicsDebug)
        {
            PhysicsDebugView ScopedDebugView;
            ScopedDebugView.center = camera.center;
            ScopedDebugView.zoom = std::max(camera.zoom, 0.01f);
            ScopedDebugView.origin = ScopedImageMin;
            ScopedDebugView.width = std::max(ScopedImageMax.x - ScopedImageMin.x, 1.0f);
            ScopedDebugView.height = std::max(ScopedImageMax.y - ScopedImageMin.y, 1.0f);
            physicsDebug.Build(InContext, PhysicsDebugAll, nullptr);
            physicsDebug.DrawImGui(ImGui::GetWindowDrawList(), &ScopedDebugView, &MapPhysicsDebugToScreen);
        }

        // Scene text overlay: the editor draws TextComponent content with the
        // SceneText typography role. It is intentionally outside the game render
        // target, so it remains readable while the viewport is zoomed.
        if (showSceneText)
        {
            Text::TextService* ScopedTextService = InContext.text;
            if (ScopedTextService == nullptr)
            {
                if (!sceneText)
                {
                    sceneText = std::make_unique<Text::TextService>();
                }
                ScopedTextService = sceneText.get();
            }
            ImFont* ScopedSceneFont = fonts.SceneText();
            if (ScopedTextService != nullptr && ScopedSceneFont != nullptr)
            {
                ImDrawList* ScopedDraw = ImGui::GetWindowDrawList();
                ScopedDraw->PushClipRect(ScopedImageMin, ScopedImageMax, true);
                const float ScopedZoom = std::max(camera.zoom, 0.01f);
                const ImVec2 ScopedCenterScreen{(ScopedImageMin.x + ScopedImageMax.x) * 0.5f,
                                                (ScopedImageMin.y + ScopedImageMax.y) * 0.5f};
                auto ScopedTextView = InContext.registry.view<TextComponent, Transform2D>();
                for (auto [ScopedEntity, ScopedTextComponent, ScopedTransform] : ScopedTextView.each())
                {
                    (void)ScopedEntity;
                    const AssetId ScopedAsset{ScopedTextComponent.Asset};
                    if (!ScopedTextService->HasAsset(ScopedAsset) && InContext.assetDb != nullptr &&
                        InContext.assets != nullptr)
                    {
                        if (const Assets::AssetHandle ScopedHandle =
                                InContext.assetDb->FindHandleByAssetId(ScopedAsset);
                            ScopedHandle.IsValid())
                        {
                            const std::string_view ScopedSource = InContext.assetDb->SourcePath(ScopedHandle);
                            if (const auto ScopedBytes = InContext.assets->Read(ScopedSource); ScopedBytes)
                            {
                                if (ScopedSource.ends_with(".fbytext"))
                                {
                                    (void)ScopedTextService->AddCookedAsset(ScopedAsset, *ScopedBytes);
                                }
                                else
                                {
                                    (void)ScopedTextService->AddMarkupAsset(
                                        ScopedAsset,
                                        std::string_view(reinterpret_cast<const char*>(ScopedBytes->data()),
                                                         ScopedBytes->size()));
                                }
                            }
                        }
                    }
                    const Text::TextAssetRef ScopedRef =
                        ScopedTextService->MakeRef(ScopedAsset, ScopedTextComponent.Block);
                    const std::string_view ScopedBlock = ScopedTextService->Block(ScopedRef);
                    if (ScopedBlock.empty())
                    {
                        continue;
                    }
                    const ImVec2 ScopedPosition{
                        ScopedCenterScreen.x + (ScopedTransform.Position.X - camera.center.X) * ScopedZoom,
                        ScopedCenterScreen.y + (ScopedTransform.Position.Y - camera.center.Y) * ScopedZoom};
                    ScopedDraw->AddText(ScopedSceneFont, ScopedSceneFont->LegacySize, ScopedPosition,
                                        IM_COL32(235, 235, 245, 255), ScopedBlock.data(),
                                        ScopedBlock.data() + ScopedBlock.size());
                }
                ScopedDraw->PopClipRect();
            }
        }

        // Navigation and light debug overlays. They live in the viewport so the
        // authored data can be checked against what the systems actually use.
        if (showNavigationDebug || showLightDebug)
        {
            ImDrawList* ScopedDraw = ImGui::GetWindowDrawList();
            ScopedDraw->PushClipRect(ScopedImageMin, ScopedImageMax, true);
            const float ScopedZoom = std::max(camera.zoom, 0.01f);
            const ImVec2 ScopedCenterScreen{(ScopedImageMin.x + ScopedImageMax.x) * 0.5f,
                                            (ScopedImageMin.y + ScopedImageMax.y) * 0.5f};
            const auto ScopedToScreen = [&](Vector2D InWorld)
            {
                return ImVec2(ScopedCenterScreen.x + (InWorld.X - camera.center.X) * ScopedZoom,
                              ScopedCenterScreen.y + (InWorld.Y - camera.center.Y) * ScopedZoom);
            };
            Grid::GridDesc ScopedGrid;
            ScopedGrid.Mode = Grid::ProjectionMode::TopDown;
            ScopedGrid.CellSize = Vector2D{64.0f, 64.0f};
            if (const TilemapComponent* ScopedMap =
                    InContext.registry.try_get<TilemapComponent>(SelectedEntity(InContext.registry)))
            {
                ScopedGrid = ScopedMap->Grid;
            }

            if (showLightDebug)
            {
                auto ScopedLightView = InContext.registry.view<Transform2D, Light2DComponent>();
                for (auto [ScopedEntity, ScopedTransform, ScopedLight] : ScopedLightView.each())
                {
                    (void)ScopedEntity;
                    if ((ScopedLight.Flags & kLight2DFlagEnabled) == 0)
                    {
                        continue;
                    }
                    const ImVec2 ScopedCenter = ScopedToScreen(ScopedTransform.Position + ScopedLight.Offset);
                    const float ScopedRadius = std::max(ScopedLight.Radius, 1.0f) * ScopedZoom;
                    const ImU32 ScopedColor = IM_COL32(255, 200, 120, 130);
                    ScopedDraw->AddCircle(ScopedCenter, ScopedRadius, ScopedColor, 64, 1.5f);
                    ScopedDraw->AddCircleFilled(ScopedCenter, 4.0f, ScopedColor);
                }
            }

            if (showNavigationDebug)
            {
                const ImU32 ScopedPathColor = IM_COL32(120, 220, 255, 200);
                auto ScopedAgentView =
                    InContext.registry.view<Transform2D, Navigation::NavAgentSettings, Navigation::NavAgentComponent>();
                for (auto [ScopedEntity, ScopedTransform, ScopedSettings, ScopedAgent] : ScopedAgentView.each())
                {
                    (void)ScopedEntity;
                    const ImVec2 ScopedAgentPosition = ScopedToScreen(ScopedTransform.Position);
                    ScopedDraw->AddCircle(ScopedAgentPosition, 5.0f, ScopedPathColor, 12, 1.5f);
                    ScopedDraw->AddCircle(ScopedToScreen(ScopedSettings.Target),
                                          std::max(ScopedSettings.ArrivalRadius, 1.0f) * ScopedZoom,
                                          IM_COL32(200, 255, 160, 200), 24, 1.5f);
                    if (ScopedAgent.Path.Count < 2)
                    {
                        continue;
                    }
                    for (std::uint16_t ScopedI = 1; ScopedI < ScopedAgent.Path.Count; ++ScopedI)
                    {
                        const Vector2D ScopedFrom =
                            ScopedAgent.Path.HasPoints
                                ? ScopedAgent.Path.Points[ScopedI - 1]
                                : Grid::CellToWorld(ScopedGrid, ScopedAgent.Path.Cells[ScopedI - 1]);
                        const Vector2D ScopedTo = ScopedAgent.Path.HasPoints
                                                      ? ScopedAgent.Path.Points[ScopedI]
                                                      : Grid::CellToWorld(ScopedGrid, ScopedAgent.Path.Cells[ScopedI]);
                        ScopedDraw->AddLine(ScopedToScreen(ScopedFrom), ScopedToScreen(ScopedTo), ScopedPathColor,
                                            2.0f);
                    }
                }
            }
            ScopedDraw->PopClipRect();
        }

        // Viewport HUD: a compact axis compass, the zoom factor and, when the
        // world origin is outside the visible area, an arrow towards it with the
        // bearing in degrees and the distance in world units. The viewport is a
        // top-down 2D camera, so +X points right and +Y points down. The whole
        // navigator can be switched off from the viewport's Display popup.
        if (showOriginNavigator)
        {
            ImDrawList* ScopedDraw = ImGui::GetWindowDrawList();
            ScopedDraw->PushClipRect(ScopedImageMin, ScopedImageMax, true);
            const float ScopedWidth = std::max(ScopedImageMax.x - ScopedImageMin.x, 1.0f);
            const float ScopedHeight = std::max(ScopedImageMax.y - ScopedImageMin.y, 1.0f);
            const ImVec2 ScopedCenter{(ScopedImageMin.x + ScopedImageMax.x) * 0.5f,
                                      (ScopedImageMin.y + ScopedImageMax.y) * 0.5f};
            const float ScopedZoom = std::max(camera.zoom, 0.0001f);
            const ImVec2 ScopedOrigin{ScopedCenter.x - camera.center.X * ScopedZoom,
                                      ScopedCenter.y - camera.center.Y * ScopedZoom};
            const ImU32 ScopedInk = IM_COL32(230, 232, 240, 220);
            const ImU32 ScopedInkDim = IM_COL32(230, 232, 240, 130);
            const ImU32 ScopedBack = IM_COL32(16, 17, 24, 170);
            const auto ScopedHudLabel = [&](ImVec2 InPosition, const char* InText)
            {
                const ImVec2 ScopedTextSize = ImGui::CalcTextSize(InText);
                ScopedDraw->AddRectFilled(
                    ImVec2(InPosition.x - 4.0f, InPosition.y - 2.0f),
                    ImVec2(InPosition.x + ScopedTextSize.x + 4.0f, InPosition.y + ScopedTextSize.y + 2.0f), ScopedBack,
                    3.0f);
                ScopedDraw->AddText(InPosition, ScopedInk, InText);
            };

            const float ScopedInset = 18.0f;
            const bool ScopedOriginVisible =
                ScopedOrigin.x >= ScopedImageMin.x + ScopedInset && ScopedOrigin.x <= ScopedImageMax.x - ScopedInset &&
                ScopedOrigin.y >= ScopedImageMin.y + ScopedInset && ScopedOrigin.y <= ScopedImageMax.y - ScopedInset;
            if (ScopedOriginVisible)
            {
                ScopedDraw->AddCircle(ScopedOrigin, 5.0f, ScopedInkDim, 12, 1.5f);
                ScopedDraw->AddLine(ImVec2(ScopedOrigin.x - 9.0f, ScopedOrigin.y),
                                    ImVec2(ScopedOrigin.x + 9.0f, ScopedOrigin.y), ScopedInkDim, 1.0f);
                ScopedDraw->AddLine(ImVec2(ScopedOrigin.x, ScopedOrigin.y - 9.0f),
                                    ImVec2(ScopedOrigin.x, ScopedOrigin.y + 9.0f), ScopedInkDim, 1.0f);
                ScopedHudLabel(ImVec2(ScopedOrigin.x + 11.0f, ScopedOrigin.y - 19.0f), "0,0");
            }
            else
            {
                float ScopedDirectionX = ScopedOrigin.x - ScopedCenter.x;
                float ScopedDirectionY = ScopedOrigin.y - ScopedCenter.y;
                const float ScopedRayLength =
                    std::sqrt(ScopedDirectionX * ScopedDirectionX + ScopedDirectionY * ScopedDirectionY);
                if (ScopedRayLength > 0.001f)
                {
                    ScopedDirectionX /= ScopedRayLength;
                    ScopedDirectionY /= ScopedRayLength;
                    const float ScopedHalfWidth = ScopedWidth * 0.5f - ScopedInset;
                    const float ScopedHalfHeight = ScopedHeight * 0.5f - ScopedInset;
                    const float ScopedReach =
                        std::min(ScopedHalfWidth / std::max(std::abs(ScopedDirectionX), 1.0e-4f),
                                 ScopedHalfHeight / std::max(std::abs(ScopedDirectionY), 1.0e-4f));
                    const ImVec2 ScopedEdge{ScopedCenter.x + ScopedDirectionX * ScopedReach,
                                            ScopedCenter.y + ScopedDirectionY * ScopedReach};
                    // A short edge arrow, not a full centre-to-origin line: the
                    // viewport keeps its content clean and the label stays clear
                    // of the transform bar drawn over the top of the image.
                    const ImVec2 ScopedArrowStart{ScopedEdge.x - ScopedDirectionX * 46.0f,
                                                  ScopedEdge.y - ScopedDirectionY * 46.0f};
                    ScopedDraw->AddLine(ScopedArrowStart, ScopedEdge, ScopedInk, 1.5f);
                    const ImVec2 ScopedTip{ScopedEdge.x + ScopedDirectionX * 6.0f,
                                           ScopedEdge.y + ScopedDirectionY * 6.0f};
                    ScopedDraw->AddTriangleFilled(
                        ScopedTip,
                        ImVec2(ScopedEdge.x - ScopedDirectionX * 8.0f - ScopedDirectionY * 5.0f,
                               ScopedEdge.y - ScopedDirectionY * 8.0f + ScopedDirectionX * 5.0f),
                        ImVec2(ScopedEdge.x - ScopedDirectionX * 8.0f + ScopedDirectionY * 5.0f,
                               ScopedEdge.y - ScopedDirectionY * 8.0f - ScopedDirectionX * 5.0f),
                        ScopedInk);
                    float ScopedAngle = std::atan2(ScopedDirectionY, ScopedDirectionX) * 57.2957795f;
                    if (ScopedAngle < 0.0f)
                    {
                        ScopedAngle += 360.0f;
                    }
                    const float ScopedDistance =
                        std::sqrt(camera.center.X * camera.center.X + camera.center.Y * camera.center.Y);
                    char ScopedText[96] = {};
                    std::snprintf(ScopedText, sizeof(ScopedText), "0,0  %.0f deg  %.0f u",
                                  static_cast<double>(ScopedAngle), static_cast<double>(ScopedDistance));
                    ImVec2 ScopedTextPosition{ScopedEdge.x - ScopedDirectionX * 74.0f,
                                              ScopedEdge.y - ScopedDirectionY * 74.0f};
                    // Keep the label inside the image and below the transform bar
                    // (about 120 px of overlays) so it is never painted over.
                    ScopedTextPosition.x = std::clamp(ScopedTextPosition.x, ScopedImageMin.x + 8.0f,
                                                      std::max(ScopedImageMin.x + 8.0f, ScopedImageMax.x - 200.0f));
                    ScopedTextPosition.y = std::clamp(ScopedTextPosition.y, ScopedImageMin.y + 120.0f,
                                                      std::max(ScopedImageMin.y + 120.0f, ScopedImageMax.y - 26.0f));
                    ScopedHudLabel(ScopedTextPosition, ScopedText);
                }
            }

            const ImVec2 ScopedCompass{ScopedImageMax.x - 76.0f, ScopedImageMin.y + 78.0f};
            ScopedDraw->AddCircleFilled(ScopedCompass, 30.0f, ScopedBack, 24);
            ScopedDraw->AddCircle(ScopedCompass, 30.0f, ScopedInkDim, 24, 1.0f);
            ScopedDraw->AddLine(ScopedCompass, ImVec2(ScopedCompass.x + 22.0f, ScopedCompass.y),
                                IM_COL32(225, 90, 100, 230), 2.0f);
            ScopedDraw->AddLine(ScopedCompass, ImVec2(ScopedCompass.x, ScopedCompass.y + 22.0f),
                                IM_COL32(120, 200, 120, 230), 2.0f);
            ScopedDraw->AddText(ImVec2(ScopedCompass.x + 24.0f, ScopedCompass.y - 15.0f), IM_COL32(235, 150, 155, 235),
                                "X");
            ScopedDraw->AddText(ImVec2(ScopedCompass.x + 4.0f, ScopedCompass.y + 21.0f), IM_COL32(150, 215, 150, 235),
                                "Y");
            char ScopedZoomText[32] = {};
            std::snprintf(ScopedZoomText, sizeof(ScopedZoomText), "%.0f%%", static_cast<double>(camera.zoom * 100.0f));
            ScopedHudLabel(ImVec2(ScopedCompass.x - 16.0f, ScopedCompass.y + 36.0f), ScopedZoomText);
            ScopedDraw->PopClipRect();
        }

        // Content Browser -> Viewport drop.
        {
            ContentBrowserPanel::SpawnContext ScopedSpawn;
            ScopedSpawn.commands = &commands;
            ScopedSpawn.registry = &InContext.registry;
            ScopedSpawn.components = &components;
            ScopedSpawn.assets = InContext.assets;
            ScopedSpawn.database = InContext.assetDb;
            ScopedSpawn.importer = &importer;
            const ImVec2 ScopedMouse = ImGui::GetIO().MousePos;
            const float ScopedWidth = std::max(ScopedImageMax.x - ScopedImageMin.x, 1.0f);
            const float ScopedHeight = std::max(ScopedImageMax.y - ScopedImageMin.y, 1.0f);
            const float ScopedX = std::clamp(ScopedMouse.x - ScopedImageMin.x, 0.0f, ScopedWidth);
            const float ScopedY = std::clamp(ScopedMouse.y - ScopedImageMin.y, 0.0f, ScopedHeight);
            Grid::CameraView ScopedCameraView;
            ScopedCameraView.Center = camera.center;
            ScopedCameraView.ViewportSize = Vector2D{ScopedWidth, ScopedHeight};
            ScopedCameraView.Zoom = camera.zoom;
            ScopedSpawn.dropPosition = Grid::ScreenToWorld(ScopedCameraView, Vector2D{ScopedX, ScopedY});
            std::string ScopedSpawnError;
            if (contentBrowser.AcceptViewportDrop(ScopedSpawn, &ScopedSpawnError))
            {
                SetStatus("spawned asset in the Viewport");
            }
            else if (!ScopedSpawnError.empty())
            {
                SetStatus("spawn failed: " + ScopedSpawnError);
            }
        }

        // Toolbar overlay.
        ImGui::SetCursorPos(ImVec2(8.0f, 30.0f));
        ImGui::BeginGroup();
        if (ImGui::Button("+ Entity"))
        {
            SpawnEntity(InContext, camera.center);
        }
        ImGui::SameLine();
        if (ImGui::Button("Duplicate"))
        {
            DuplicateSelected(InContext);
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(pie.Active());
        if (ImGui::Button("Delete"))
        {
            DeleteSelectedEntities(InContext);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        // Play/Pause/Step/Stop drive the real PIE loop. The buttons are square
        // icons; the one matching the current runtime state is drawn translucent,
        // like a pressed toggle, while the others stay dark with white glyphs.
        if (PlaybackIconButton("play", PlaybackIcon::Play, pie.Active() && !piePaused, pie.Active() || !pieInitialized,
                               "Play"))
        {
            pieCommand = PieCommand::Play;
        }
        ImGui::SameLine();
        if (PlaybackIconButton("play_mp", PlaybackIcon::Multiplayer, false, pie.Active() || !pieInitialized,
                               "Play multiplayer"))
        {
            pieCommand = PieCommand::PlayMultiplayer;
        }
        ImGui::SameLine();
        if (PlaybackIconButton("pause", PlaybackIcon::Pause, pie.Active() && piePaused, !pie.Active(),
                               piePaused ? "Resume" : "Pause"))
        {
            pieCommand = PieCommand::TogglePause;
        }
        ImGui::SameLine();
        if (ImGui::Button("Pause audio"))
        {
            ImGui::OpenPopup("pie_pause_options");
        }
        if (ImGui::BeginPopup("pie_pause_options"))
        {
            bool ScopedChanged = false;
            ScopedChanged |= ImGui::Checkbox("Pause SFX", &piePauseSfx);
            ScopedChanged |= ImGui::Checkbox("Pause music", &piePauseMusic);
            ScopedChanged |= ImGui::Checkbox("Pause HUD animations", &piePauseHud);
            if (ScopedChanged)
            {
                if (InContext.audio != nullptr)
                {
                    InContext.audio->SetBusPaused(Fby::AudioBus::GameSfx, piePaused && piePauseSfx);
                    InContext.audio->SetBusPaused(Fby::AudioBus::Music, piePaused && piePauseMusic);
                }
                if (graph != nullptr)
                {
                    graph->SetPaused(piePaused, !piePauseHud);
                }
            }
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        if (PlaybackIconButton("step", PlaybackIcon::Step, false, !piePaused, "Step"))
        {
            pieCommand = PieCommand::Step;
        }
        ImGui::SameLine();
        if (PlaybackIconButton("stop", PlaybackIcon::Stop, false, !pie.Active(), "Stop"))
        {
            pieCommand = PieCommand::Stop;
        }
        ImGui::SameLine();
        if (pie.Active())
        {
            ImGui::TextColored(ImVec4(0.816f, 0.322f, 0.502f, 1.0f), "PIE %s%s  tick %llu",
                               pie.Multiplayer() ? "multiplayer " : "",
                               piePaused ? (piePauseMusic ? "paused (music muted)" : "paused") : "playing",
                               static_cast<unsigned long long>(pie.TickCount()));
        }
        else
        {
            ImGui::TextDisabled("edit mode");
        }
        if (pie.Active() && pie.VoiceActive())
        {
            ImGui::SameLine();
            bool ScopedVoiceMuted = pie.VoiceHostSession().LocalMuted();
            if (ImGui::Checkbox("Voice mute", &ScopedVoiceMuted))
            {
                pie.VoiceHostSession().SetLocalMuted(ScopedVoiceMuted);
            }
            ImGui::SameLine();
            if (ImGui::Checkbox("PTT (V)", &pieVoicePushToTalk))
            {
                pie.VoiceHostSession().SetPushToTalkEnabled(pieVoicePushToTalk);
            }
            pie.VoiceHostSession().SetPushToTalkHeld(pieVoicePushToTalk && ImGui::IsKeyDown(ImGuiKey_V));
            ImGui::SameLine();
            if (pie.VoiceHostSession().CanTransmit())
            {
                ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.55f, 1.0f), "voice tx");
            }
            else
            {
                ImGui::TextDisabled("voice idle");
            }
        }
        ImGui::SameLine();
        ImGui::Checkbox("Scene text", &showSceneText);
        ImGui::SameLine();
        ImGui::BeginDisabled(releaseCooker.Running() || !projectOpen);
        if (ImGui::Button("Build & Pack Game"))
        {
            StartReleaseCook();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (releaseCooker.Running())
        {
            if (ImGui::Button("Cancel Cook"))
            {
                releaseCooker.Cancel();
                SetStatus("release cook cancellation requested");
            }
        }
        ImGui::EndGroup();

        // Transform and camera bar. The mode buttons mirror the UE viewport bar:
        // the three gizmo modes plus world/local space, and one-shot arrow steps
        // for the selected object. Camera speed and zoom are plain left-dragged
        // sliders, so no mouse wheel is needed for either.
        const float ScopedToolbarHeight = ImGui::GetItemRectSize().y;
        ImGui::SetCursorPos(ImVec2(8.0f, 30.0f + ScopedToolbarHeight + 6.0f));
        ImGui::BeginGroup();
        const auto ScopedModeButton = [this](const char* InLabel, ImGuizmo::OPERATION InOperation)
        {
            const bool ScopedActive = gizmoOperation == InOperation;
            const bool ScopedRotate = InOperation == ImGuizmo::ROTATE;
            if (ScopedActive)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            }
            if (ScopedRotate)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, SymbolColor(EditorSymbolRole::Rotate));
            }
            const bool ScopedPressed = ImGui::Button(InLabel);
            if (ScopedRotate)
            {
                ImGui::PopStyleColor();
            }
            if (ScopedActive)
            {
                ImGui::PopStyleColor();
            }
            if (ScopedPressed)
            {
                gizmoOperation = InOperation;
            }
            ImGui::SameLine();
        };
        const std::span<const Rhi::ViewportModeDescriptor> ScopedViewModes = Rhi::AllViewportModes();
        const std::uint32_t ScopedStoredMode = ClampViewportViewMode(projectConfig.Editor().viewportViewMode);
        const Rhi::ViewportModeDescriptor& ScopedActiveMode =
            Rhi::DescribeViewportMode(static_cast<Rhi::ViewportMode>(ScopedStoredMode));
        ImGui::SetNextItemWidth(150.0f);
        if (ImGui::BeginCombo("##viewMode", ScopedActiveMode.Label))
        {
            for (const Rhi::ViewportModeDescriptor& ScopedDescriptor : ScopedViewModes)
            {
                const bool ScopedSelected = ScopedDescriptor.Mode == ScopedActiveMode.Mode;
                if (ImGui::Selectable(ScopedDescriptor.Label, ScopedSelected))
                {
                    projectConfig.Editor().viewportViewMode = static_cast<std::uint32_t>(ScopedDescriptor.Mode);
                    SetStatus(std::string("view mode: ") + ScopedDescriptor.ShortLabel);
                }
                if (ScopedSelected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        // Hover-only help for the closed selector. While the popup is open the
        // last-item hover state belongs to its rows, so the card is suppressed.
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort) && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId))
        {
            BeginEditorTooltip(480.0f);
            ImGui::TextUnformatted(ScopedActiveMode.Label);
            ImGui::Separator();
            EditorTooltipParagraph(ScopedActiveMode.Summary);
            EditorTooltipParagraph(ScopedActiveMode.Paragraph1);
            EditorTooltipParagraph(ScopedActiveMode.Paragraph2);
            EditorTooltipParagraph(ScopedActiveMode.Paragraph3);
            if (ScopedActiveMode.LegendCount > 0 && ScopedActiveMode.Legend != nullptr)
            {
                ImGui::Separator();
                EditorTooltipParagraph("Colour legend");
                for (std::uint32_t ScopedLegend = 0; ScopedLegend < ScopedActiveMode.LegendCount; ++ScopedLegend)
                {
                    const Rhi::ViewportModeLegend& ScopedEntry = ScopedActiveMode.Legend[ScopedLegend];
                    EditorTooltipLegendRow(ScopedEntry.Label, ScopedEntry.Color.rgba);
                }
            }
            EndEditorTooltip();
        }
        ImGui::SameLine();
        ScopedModeButton("Move", ImGuizmo::TRANSLATE);
        ScopedModeButton("Rotate", ImGuizmo::ROTATE);
        ScopedModeButton("Scale", ImGuizmo::SCALE);
        if (ImGui::Button(gizmoMode == ImGuizmo::WORLD ? "World" : "Local"))
        {
            gizmoMode = gizmoMode == ImGuizmo::WORLD ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
            SetStatus(gizmoMode == ImGuizmo::WORLD ? "gizmo space: world" : "gizmo space: local");
        }
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        const Entity ScopedSelectedEntity = SelectedEntity(InContext.registry);
        ImGui::BeginDisabled(ScopedSelectedEntity == Fby::kNullEntity);
        const float ScopedStep = snapEnabled ? snapGrid : (ImGui::GetIO().KeyShift ? 10.0f : 1.0f);
        const float ScopedTurn =
            snapEnabled ? kSnapAngleDegrees * 0.0174532925f : 0.0872665f * (ImGui::GetIO().KeyShift ? 3.0f : 1.0f);
        const float ScopedScaleStep = snapEnabled ? kSnapScale : (ImGui::GetIO().KeyShift ? 0.5f : 0.05f);
        if (gizmoOperation == ImGuizmo::TRANSLATE)
        {
            if (ImGui::Button("X -"))
            {
                NudgeSelectedTransform(InContext, "Nudge X", Vector2D{-ScopedStep, 0.0f}, 0.0f, 0.0f);
            }
            ImGui::SameLine();
            if (ImGui::Button("X +"))
            {
                NudgeSelectedTransform(InContext, "Nudge X", Vector2D{ScopedStep, 0.0f}, 0.0f, 0.0f);
            }
            ImGui::SameLine();
            if (ImGui::Button("Y -"))
            {
                NudgeSelectedTransform(InContext, "Nudge Y", Vector2D{0.0f, -ScopedStep}, 0.0f, 0.0f);
            }
            ImGui::SameLine();
            if (ImGui::Button("Y +"))
            {
                NudgeSelectedTransform(InContext, "Nudge Y", Vector2D{0.0f, ScopedStep}, 0.0f, 0.0f);
            }
        }
        else if (gizmoOperation == ImGuizmo::ROTATE)
        {
            if (ImGui::Button("-5 deg"))
            {
                NudgeSelectedTransform(InContext, "Rotate -5", Vector2D{}, -ScopedTurn, 0.0f);
            }
            ImGui::SameLine();
            if (ImGui::Button("+5 deg"))
            {
                NudgeSelectedTransform(InContext, "Rotate +5", Vector2D{}, ScopedTurn, 0.0f);
            }
        }
        else
        {
            if (ImGui::Button("Scale -"))
            {
                NudgeSelectedTransform(InContext, "Scale -", Vector2D{}, 0.0f, -ScopedScaleStep);
            }
            ImGui::SameLine();
            if (ImGui::Button("Scale +"))
            {
                NudgeSelectedTransform(InContext, "Scale +", Vector2D{}, 0.0f, ScopedScaleStep);
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::Text("Zoom %.0f%%", static_cast<double>(camera.zoom * 100.0f));
        float ScopedZoomPercent = camera.zoom * 100.0f;
        if (EditorSlider("##zoom", &ScopedZoomPercent, 25.0f, 800.0f, "%.0f%%", 120.0f, sliderGrabOverflow, true, 3.0f,
                         false))
        {
            camera.zoom = std::clamp(ScopedZoomPercent / 100.0f, 0.05f, 8.0f);
        }
        ImGui::EndGroup();
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::Text("Camera speed %.2fx", static_cast<double>(camera.panSpeed));
        (void)EditorSlider("##panSpeed", &camera.panSpeed, 0.25f, 4.0f, "%.2fx", 96.0f, sliderGrabOverflow, false, 3.0f,
                           false);
        ImGui::EndGroup();
        ImGui::SameLine();
        if (ImGui::Button("Display"))
        {
            ImGui::OpenPopup("viewport_display");
        }
        if (ImGui::BeginPopup("viewport_display"))
        {
            (void)ImGui::Checkbox("Origin compass", &showOriginNavigator);
            (void)ImGui::Checkbox("Grab spills over track", &sliderGrabOverflow);
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Fit"))
        {
            if (ScopedSelectedEntity != Fby::kNullEntity)
            {
                FrameSelected(InContext);
            }
            else
            {
                camera.center = Vector2D{640.0f, 360.0f};
                camera.zoom = 1.0f;
            }
        }
        ImGui::SameLine();
        (void)ImGui::Checkbox("Snap", &snapEnabled);
        if (snapEnabled)
        {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(60.0f);
            const char* ScopedGrids[] = {"8", "16", "32"};
            int ScopedGridIndex = snapGrid <= 8.0f ? 0 : (snapGrid <= 16.0f ? 1 : 2);
            if (ImGui::Combo("##snapGrid", &ScopedGridIndex, ScopedGrids, 3))
            {
                snapGrid = ScopedGridIndex == 0 ? 8.0f : (ScopedGridIndex == 1 ? 16.0f : 32.0f);
            }
            ImGui::SameLine();
            ImGui::TextDisabled("15 deg / 0.1x");
        }
        ImGui::EndGroup();

        const Entity ScopedEntity = SelectedEntity(InContext.registry);
        auto* ScopedTransform =
            ScopedEntity != Fby::kNullEntity ? InContext.registry.try_get<Transform2D>(ScopedEntity) : nullptr;
        if (ScopedTransform != nullptr && targetTexture != ImTextureID_Invalid)
        {
            const float ScopedWidth = std::max(ScopedImageMax.x - ScopedImageMin.x, 1.0f);
            const float ScopedHeight = std::max(ScopedImageMax.y - ScopedImageMin.y, 1.0f);

            float ScopedView[16]{};
            float ScopedProjection[16]{};
            MakeViewProjection(camera, ScopedWidth, ScopedHeight, ScopedView, ScopedProjection);

            float ScopedModel[16]{};
            MakeModelMatrix(*ScopedTransform, ScopedModel);

            ImGuizmo::SetOrthographic(true);
            ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
            ImGuizmo::SetRect(ScopedImageMin.x, ScopedImageMin.y, ScopedWidth, ScopedHeight);
            ImGuizmo::AllowAxisFlip(false);
            // ImGuizmo takes degrees for rotation and world units for translate
            // and scale; the toolbar's Snap toggle feeds the same values here.
            float ScopedSnapValues[3] = {snapGrid, snapGrid, snapGrid};
            const float* ScopedSnap = nullptr;
            if (snapEnabled)
            {
                if (gizmoOperation == ImGuizmo::ROTATE)
                {
                    ScopedSnapValues[0] = kSnapAngleDegrees;
                }
                ScopedSnap = ScopedSnapValues;
            }
            const bool ScopedManipulated = ImGuizmo::Manipulate(ScopedView, ScopedProjection, gizmoOperation, gizmoMode,
                                                                ScopedModel, nullptr, ScopedSnap);
            const bool ScopedGizmoUsing = ImGuizmo::IsUsing();
            if (transformCodec != 0xFFFFu && (ScopedGizmoUsing || ScopedManipulated))
            {
                if (interaction.Active() &&
                    !interaction.IsFor(ScopedEntity, transformCodec, InteractionTransaction::Source::Gizmo))
                {
                    // An inspector drag on the same entity or a different gizmo
                    CommitInteraction();
                }
                if (!interaction.Active())
                {
                    (void)interaction.Begin(commands, ScopedEntity, transformCodec, "ImGuizmo transform",
                                            InteractionTransaction::Source::Gizmo);
                }
                if (ScopedManipulated)
                {
                    // Manipulate() only produced a new model matrix; the component
                    ApplyModelMatrix(ScopedModel, *ScopedTransform);
                    (void)interaction.Record();
                }
            }
            if (interaction.Active() &&
                interaction.IsFor(ScopedEntity, transformCodec, InteractionTransaction::Source::Gizmo) &&
                !ImGuizmo::IsUsing())
            {
                // Mouse release ends the drag: one transaction, one undo step.
                (void)interaction.Commit();
            }
        }
        HandleViewportInput(InContext, ScopedImageMin, ScopedImageMax, ScopedImageHovered);
        EndEditorPanel();
    }

    // Viewport navigation and selection. ImGui owns the mouse over every panel,
    // so the host loop's "not captured" checks could never see a wheel event or a
    // click inside the viewport; the gestures are handled where the image lives.
    void HandleViewportInput(Ecs::SystemContext& InContext, ImVec2 InImageMin, ImVec2 InImageMax, bool InHovered)
    {
        const ImGuiIO& ScopedIo = ImGui::GetIO();
        const float ScopedWidth = std::max(InImageMax.x - InImageMin.x, 1.0f);
        const float ScopedHeight = std::max(InImageMax.y - InImageMin.y, 1.0f);
        const Vector2D ScopedCursorLocal{ScopedIo.MousePos.x - InImageMin.x, ScopedIo.MousePos.y - InImageMin.y};
        Grid::CameraView ScopedView;
        ScopedView.Center = camera.center;
        ScopedView.ViewportSize = Vector2D{ScopedWidth, ScopedHeight};
        ScopedView.Zoom = camera.zoom;

        if (InHovered)
        {
            // Wheel zoom keeps the world point under the cursor pinned, which is
            // what every other 2D editor does.
            if (ScopedIo.MouseWheel != 0.0f && !ScopedIo.KeyCtrl)
            {
                const Vector2D ScopedAnchor = Grid::ScreenToWorld(ScopedView, ScopedCursorLocal);
                camera.zoom = std::clamp(camera.zoom * std::pow(1.15f, ScopedIo.MouseWheel), 0.05f, 8.0f);
                ScopedView.Zoom = camera.zoom;
                const Vector2D ScopedAfter = Grid::ScreenToWorld(ScopedView, ScopedCursorLocal);
                camera.center += ScopedAnchor - ScopedAfter;
            }
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) || ImGui::IsMouseDragging(ImGuiMouseButton_Right))
            {
                const float ScopedSpeed = std::max(camera.panSpeed, 0.01f);
                camera.center.X -= ScopedIo.MouseDelta.x / std::max(camera.zoom, 0.0001f) * ScopedSpeed;
                camera.center.Y -= ScopedIo.MouseDelta.y / std::max(camera.zoom, 0.0001f) * ScopedSpeed;
            }
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGuizmo::IsOver() && !ImGuizmo::IsUsing() &&
                !ImGui::IsAnyItemHovered() && !ImGui::IsAnyItemActive())
            {
                ScopedView.Center = camera.center;
                const Vector2D ScopedWorld = Grid::ScreenToWorld(ScopedView, ScopedCursorLocal);
                const Entity ScopedPicked = PickEntityAt(InContext.registry, ScopedWorld);
                if (ScopedIo.KeyCtrl)
                {
                    ToggleSelection(InContext.registry, ScopedPicked);
                }
                else if (ScopedIo.KeyShift)
                {
                    AddToSelection(InContext.registry, ScopedPicked);
                }
                else
                {
                    SelectOnly(InContext.registry, ScopedPicked);
                }
                SetStatus(ScopedPicked == Fby::kNullEntity ? "selection cleared"
                                                           : EntityLabel(InContext.registry, ScopedPicked));
            }
        }
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows))
        {
            if (ImGui::IsKeyPressed(ImGuiKey_Delete))
            {
                DeleteSelectedEntities(InContext);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_F))
            {
                FrameSelected(InContext);
            }
            // W/E/R pick the gizmo mode, the same keys UE uses.
            if (ImGui::IsKeyPressed(ImGuiKey_W) && !ImGui::GetIO().KeyCtrl)
            {
                gizmoOperation = ImGuizmo::TRANSLATE;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_E) && !ImGui::GetIO().KeyCtrl)
            {
                gizmoOperation = ImGuizmo::ROTATE;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_R) && !ImGui::GetIO().KeyCtrl)
            {
                gizmoOperation = ImGuizmo::SCALE;
            }
        }
    }

    // Adds an authored entity at the viewport centre. The spawn is recorded as a
    // structural command, so Ctrl+Z removes it again.
    Entity SpawnEntity(Ecs::SystemContext& InContext, Vector2D InPosition)
    {
        if (!projectOpen)
        {
            SetStatus("open a project before adding entities");
            return Fby::kNullEntity;
        }
        SpawnTransaction ScopedTransaction;
        if (ScopedTransaction.Begin(commands, "Add entity") != CommandStack::Result::Ok)
        {
            SetStatus("another edit is already in progress");
            return Fby::kNullEntity;
        }
        const Entity ScopedEntity = InContext.registry.create();
        Transform2D ScopedTransform;
        ScopedTransform.Position = InPosition;
        InContext.registry.emplace<Transform2D>(ScopedEntity, ScopedTransform);
        Sprite2D ScopedSprite;
        ScopedSprite.Size = {64.0f, 64.0f};
        ScopedSprite.Color = Color32::FromBytes(230, 150, 190);
        InContext.registry.emplace<Sprite2D>(ScopedEntity, ScopedSprite);
        DrawOrder ScopedOrder;
        ScopedOrder.Layer = 10;
        InContext.registry.emplace<DrawOrder>(ScopedEntity, ScopedOrder);
        if (ScopedTransaction.Record(ScopedEntity) != CommandStack::Result::Ok)
        {
            InContext.registry.destroy(ScopedEntity);
            ScopedTransaction.Cancel();
            SetStatus("cannot record the new entity");
            return Fby::kNullEntity;
        }
        (void)ScopedTransaction.Commit();
        SelectOnly(InContext.registry, ScopedEntity);
        SetStatus("entity added");
        LogInfo("editor", "added entity {} at ({:.1f}, {:.1f})", Fby::ToIntegral(ScopedEntity),
                static_cast<double>(InPosition.X), static_cast<double>(InPosition.Y));
        return ScopedEntity;
    }

    // Copies every component of the selected entity. Entity references stored
    // inside components still point at the original; that is the same authoring
    // result as a duplicate in the hierarchy and the inspector can fix links.
    // Copies every serializable component of one entity into a fresh one.
    Entity CopyEntityInto(Ecs::SystemContext& InContext, Entity InSource, Vector2D InOffset)
    {
        const Entity ScopedCopy = InContext.registry.create();
        for (const Scene::ComponentOps& ScopedOps : components.All())
        {
            if (!ScopedOps.serializable || !ScopedOps.has(InContext.registry, InSource))
            {
                continue;
            }
            ScopedOps.emplaceBytes(InContext.registry, ScopedCopy, ScopedOps.get(InContext.registry, InSource));
        }
        if (auto* ScopedTransform = InContext.registry.try_get<Transform2D>(ScopedCopy); ScopedTransform != nullptr)
        {
            ScopedTransform->Position += InOffset;
        }
        return ScopedCopy;
    }

    void DuplicateSelected(Ecs::SystemContext& InContext)
    {
        const std::vector<Entity> ScopedSelected = SelectedEntities(InContext.registry);
        if (ScopedSelected.empty())
        {
            SetStatus("select an entity to duplicate");
            return;
        }
        if (commands.InTransaction())
        {
            SetStatus("another edit is already in progress");
            return;
        }
        if (commands.BeginTransaction("Duplicate entities") != CommandStack::Result::Ok)
        {
            SetStatus("cannot start the duplicate transaction");
            return;
        }
        std::vector<Entity> ScopedCopies;
        ScopedCopies.reserve(ScopedSelected.size());
        for (const Entity ScopedSource : ScopedSelected)
        {
            const Entity ScopedCopy = CopyEntityInto(InContext, ScopedSource, Vector2D{32.0f, 32.0f});
            if (commands.RecordSpawn(ScopedCopy) != CommandStack::Result::Ok)
            {
                for (const Entity ScopedCreated : ScopedCopies)
                {
                    InContext.registry.destroy(ScopedCreated);
                }
                InContext.registry.destroy(ScopedCopy);
                commands.CancelTransaction();
                SetStatus("cannot record the duplicated entities");
                return;
            }
            ScopedCopies.push_back(ScopedCopy);
        }
        (void)commands.CommitTransaction();
        SelectOnly(InContext.registry, ScopedCopies.front());
        for (std::size_t ScopedI = 1; ScopedI < ScopedCopies.size(); ++ScopedI)
        {
            AddToSelection(InContext.registry, ScopedCopies[ScopedI]);
        }
        SetStatus(std::format("{} entities duplicated", ScopedCopies.size()));
        LogInfo("editor", "duplicated {} entities", ScopedCopies.size());
    }

    // Deleting is recorded as a structural command first, so Ctrl+Z recreates
    // every entity with its component image.
    void DeleteSelectedEntities(Ecs::SystemContext& InContext)
    {
        const std::vector<Entity> ScopedSelected = SelectedEntities(InContext.registry);
        if (ScopedSelected.empty())
        {
            return;
        }
        if (pie.Active())
        {
            SetStatus("stop the PIE session before deleting entities");
            return;
        }
        CommitInteraction();
        if (commands.InTransaction())
        {
            SetStatus("another edit is already in progress");
            return;
        }
        if (commands.BeginTransaction("Delete entities") != CommandStack::Result::Ok)
        {
            SetStatus("cannot start the delete transaction");
            return;
        }
        for (const Entity ScopedEntity : ScopedSelected)
        {
            if (commands.RecordDestroy(ScopedEntity) != CommandStack::Result::Ok)
            {
                commands.CancelTransaction();
                SetStatus("cannot record the deleted entities");
                return;
            }
        }
        for (const Entity ScopedEntity : ScopedSelected)
        {
            InContext.registry.destroy(ScopedEntity);
        }
        (void)commands.CommitTransaction();
        SetStatus(std::format("{} entities deleted", ScopedSelected.size()));
        LogInfo("editor", "deleted {} entities", ScopedSelected.size());
    }

    // Clipboard: the component images are copied by layout hash so the engine's
    // editor-only markers never leak into a paste.
    void CopySelection(Ecs::SystemContext& InContext)
    {
        const std::vector<Entity> ScopedSelected = SelectedEntities(InContext.registry);
        if (ScopedSelected.empty())
        {
            SetStatus("nothing is selected to copy");
            return;
        }
        clipboard.clear();
        for (const Entity ScopedSource : ScopedSelected)
        {
            ClipboardEntity ScopedEntry;
            for (const Scene::ComponentOps& ScopedOps : components.All())
            {
                if (!ScopedOps.serializable || ScopedOps.size > kMaxTrackedComponentBytes ||
                    !ScopedOps.has(InContext.registry, ScopedSource))
                {
                    continue;
                }
                ClipboardComponent ScopedComponent;
                ScopedComponent.layoutHash = ScopedOps.layoutHash;
                ScopedComponent.bytes.resize(ScopedOps.size);
                if (ScopedOps.size > 0)
                {
                    const void* ScopedData = ScopedOps.get(InContext.registry, ScopedSource);
                    if (ScopedData == nullptr)
                    {
                        continue;
                    }
                    std::memcpy(ScopedComponent.bytes.data(), ScopedData, ScopedOps.size);
                }
                ScopedEntry.components.push_back(std::move(ScopedComponent));
            }
            clipboard.push_back(std::move(ScopedEntry));
        }
        SetStatus(std::format("{} entities copied", clipboard.size()));
    }

    void PasteClipboard(Ecs::SystemContext& InContext)
    {
        if (clipboard.empty())
        {
            SetStatus("the clipboard is empty");
            return;
        }
        if (commands.InTransaction())
        {
            SetStatus("another edit is already in progress");
            return;
        }
        if (commands.BeginTransaction("Paste entities") != CommandStack::Result::Ok)
        {
            SetStatus("cannot start the paste transaction");
            return;
        }
        std::vector<Entity> ScopedPasted;
        ScopedPasted.reserve(clipboard.size());
        for (const ClipboardEntity& ScopedEntry : clipboard)
        {
            const Entity ScopedEntity = InContext.registry.create();
            for (const ClipboardComponent& ScopedComponent : ScopedEntry.components)
            {
                const Scene::ComponentOps* ScopedOps = nullptr;
                for (const Scene::ComponentOps& ScopedCandidate : components.All())
                {
                    if (ScopedCandidate.layoutHash == ScopedComponent.layoutHash)
                    {
                        ScopedOps = &ScopedCandidate;
                        break;
                    }
                }
                if (ScopedOps == nullptr || ScopedOps->emplaceBytes == nullptr)
                {
                    continue;
                }
                ScopedOps->emplaceBytes(InContext.registry, ScopedEntity,
                                        ScopedComponent.bytes.empty() ? nullptr : ScopedComponent.bytes.data());
            }
            if (auto* ScopedTransform = InContext.registry.try_get<Transform2D>(ScopedEntity);
                ScopedTransform != nullptr)
            {
                ScopedTransform->Position += Vector2D{32.0f, 32.0f};
            }
            if (commands.RecordSpawn(ScopedEntity) != CommandStack::Result::Ok)
            {
                for (const Entity ScopedCreated : ScopedPasted)
                {
                    InContext.registry.destroy(ScopedCreated);
                }
                InContext.registry.destroy(ScopedEntity);
                commands.CancelTransaction();
                SetStatus("cannot record the pasted entities");
                return;
            }
            ScopedPasted.push_back(ScopedEntity);
        }
        (void)commands.CommitTransaction();
        SelectOnly(InContext.registry, ScopedPasted.front());
        for (std::size_t ScopedI = 1; ScopedI < ScopedPasted.size(); ++ScopedI)
        {
            AddToSelection(InContext.registry, ScopedPasted[ScopedI]);
        }
        SetStatus(std::format("{} entities pasted", ScopedPasted.size()));
    }

    // Writes the NameComponent. The edit goes through the interaction snapshot so
    // a rename is a single undo step.
    void SetEntityName(Ecs::SystemContext& InContext, Entity InEntity, std::string_view InName)
    {
        if (!InContext.registry.valid(InEntity))
        {
            return;
        }
        const Scene::ComponentOps* ScopedOps = nullptr;
        for (const Scene::ComponentOps& ScopedCandidate : components.All())
        {
            if (ScopedCandidate.name == "NameComponent")
            {
                ScopedOps = &ScopedCandidate;
                break;
            }
        }
        if (ScopedOps == nullptr)
        {
            return;
        }
        if (!ScopedOps->has(InContext.registry, InEntity) && ScopedOps->emplaceDefault != nullptr)
        {
            ScopedOps->emplaceDefault(InContext.registry, InEntity);
        }
        auto* ScopedName = InContext.registry.try_get<NameComponent>(InEntity);
        if (ScopedName == nullptr)
        {
            return;
        }
        const std::uint16_t ScopedCodec = commands.CodecForLayoutHash(ScopedOps->layoutHash);
        const std::uint32_t ScopedSize = commands.CodecSize(ScopedCodec);
        std::array<std::byte, kMaxTrackedComponentBytes> ScopedBefore{};
        if (ScopedCodec == 0xFFFFu || ScopedSize == 0 || ScopedSize > ScopedBefore.size() ||
            commands.CaptureComponent(InEntity, ScopedCodec, ScopedBefore) != CommandStack::Result::Ok)
        {
            return;
        }
        if (interaction.Active())
        {
            CommitInteraction();
        }
        if (interaction.BeginWithSnapshot(
                commands, InEntity, ScopedCodec, "Rename entity", InteractionTransaction::Source::InspectorField,
                std::span<const std::byte>(ScopedBefore.data(), ScopedSize)) != CommandStack::Result::Ok)
        {
            return;
        }
        std::memset(ScopedName->Value, 0, sizeof(ScopedName->Value));
        const std::size_t ScopedLength = std::min<std::size_t>(InName.size(), sizeof(ScopedName->Value) - 1);
        std::memcpy(ScopedName->Value, InName.data(), ScopedLength);
        (void)interaction.Record();
        (void)interaction.Commit();
        SetStatus("entity renamed");
    }

    // Saves the open scene and moves the dirty baseline to the current command
    // stack position. Returns false when there is no path or the write failed.
    bool SaveCurrentScene(Ecs::SystemContext& InContext, bool InSilent)
    {
        if (desc.scenePath.empty())
        {
            if (!InSilent)
            {
                SetStatus("no scene path: open or create a scene first");
            }
            return false;
        }
        std::string ScopedError;
        if (!owner->SaveScene(InContext, desc.scenePath, &ScopedError))
        {
            SetStatus("save failed: " + ScopedError);
            return false;
        }
        const CommandStack::Stats ScopedStats = commands.GetStats();
        savedCommandCount = ScopedStats.commands;
        savedCursor = ScopedStats.undoDepth;
        sceneDirty = false;
        if (!InSilent)
        {
            SetStatus("scene saved to " + desc.scenePath.string());
        }
        return true;
    }

    // Called after a scene or project loads: everything currently in the world is
    // the new saved baseline.
    void ResetDirtyBaseline()
    {
        const CommandStack::Stats ScopedStats = commands.GetStats();
        savedCommandCount = ScopedStats.commands;
        savedCursor = ScopedStats.undoDepth;
        sceneDirty = false;
        autoSaveTimer = 0.0f;
    }

    // Saves the selected entities as a scene TOML that the Content Browser can
    // drag back into the viewport as a prefab.
    bool SaveSelectionAsPrefab(Ecs::SystemContext& InContext, const std::filesystem::path& InPath, std::string* InError)
    {
        const std::vector<Entity> ScopedSelected = SelectedEntities(InContext.registry);
        if (ScopedSelected.empty())
        {
            if (InError != nullptr)
            {
                *InError = "select at least one entity";
            }
            return false;
        }
        Ecs::Registry ScopedPrefab;
        for (const Entity ScopedSource : ScopedSelected)
        {
            const Entity ScopedCopy = ScopedPrefab.create();
            for (const Scene::ComponentOps& ScopedOps : components.All())
            {
                if (!ScopedOps.serializable || ScopedOps.size > kMaxTrackedComponentBytes ||
                    !ScopedOps.has(InContext.registry, ScopedSource))
                {
                    continue;
                }
                ScopedOps.emplaceBytes(ScopedPrefab, ScopedCopy, ScopedOps.get(InContext.registry, ScopedSource));
            }
        }
        std::error_code ScopedCode;
        std::filesystem::create_directories(InPath.parent_path(), ScopedCode);
        Scene::SceneMeta ScopedMeta;
        ScopedMeta.name = InPath.stem().string();
        ScopedMeta.author = "FemboyEngine2D editor";
        return Scene::SaveSceneFileToml(InPath, ScopedPrefab, components, ScopedMeta, InError);
    }

    void FrameSelected(Ecs::SystemContext& InContext)
    {
        const Entity ScopedEntity = SelectedEntity(InContext.registry);
        if (ScopedEntity == Fby::kNullEntity)
        {
            return;
        }
        if (const Transform2D* ScopedTransform = InContext.registry.try_get<Transform2D>(ScopedEntity);
            ScopedTransform != nullptr)
        {
            camera.center = ScopedTransform->Position;
            SetStatus("view centred on the selection");
        }
    }

    // One undoable transform step for the viewport arrow buttons. The snapshot
    // is captured before the edit, so the whole nudge is a single Ctrl+Z step.
    void NudgeSelectedTransform(Ecs::SystemContext& InContext, const char* InLabel, Vector2D InDelta,
                                float InDeltaRotation, float InDeltaScale)
    {
        if (transformCodec == 0xFFFFu)
        {
            return;
        }
        const Entity ScopedEntity = SelectedEntity(InContext.registry);
        if (ScopedEntity == Fby::kNullEntity)
        {
            return;
        }
        auto* ScopedTransform = InContext.registry.try_get<Transform2D>(ScopedEntity);
        if (ScopedTransform == nullptr)
        {
            return;
        }
        const std::uint32_t ScopedSize = commands.CodecSize(transformCodec);
        if (ScopedSize == 0 || ScopedSize > kMaxTrackedComponentBytes)
        {
            return;
        }
        if (interaction.Active())
        {
            CommitInteraction();
        }
        std::array<std::byte, kMaxTrackedComponentBytes> ScopedBefore{};
        if (commands.CaptureComponent(ScopedEntity, transformCodec, ScopedBefore) != CommandStack::Result::Ok)
        {
            return;
        }
        if (interaction.BeginWithSnapshot(
                commands, ScopedEntity, transformCodec, InLabel, InteractionTransaction::Source::InspectorField,
                std::span<const std::byte>(ScopedBefore.data(), ScopedSize)) != CommandStack::Result::Ok)
        {
            return;
        }
        ScopedTransform->Position += InDelta;
        ScopedTransform->Rotation += InDeltaRotation;
        ScopedTransform->Scale.X = std::clamp(ScopedTransform->Scale.X + InDeltaScale, 0.01f, 100.0f);
        ScopedTransform->Scale.Y = std::clamp(ScopedTransform->Scale.Y + InDeltaScale, 0.01f, 100.0f);
        (void)interaction.Record();
        (void)interaction.Commit();
        SetStatus(InLabel);
    }

    // Writes an empty scene at the chosen path and opens it, so a project can
    // grow beyond the default scene it was created with.
    void CreateSceneAndLoad(Ecs::SystemContext& InContext, std::string_view InPath)
    {
        std::filesystem::path ScopedPath(InPath);
        if (ScopedPath.extension() != ".toml")
        {
            ScopedPath += ".toml";
        }
        Scene::SceneMeta ScopedMeta;
        ScopedMeta.name = ScopedPath.stem().string();
        ScopedMeta.author = "FemboyEngine2D editor";
        Ecs::Registry ScopedEmpty;
        std::string ScopedError;
        if (!Scene::SaveSceneFileToml(ScopedPath, ScopedEmpty, components, ScopedMeta, &ScopedError))
        {
            SetStatus("new scene failed: " + ScopedError);
            return;
        }
        if (!owner->LoadScene(InContext, ScopedPath, &ScopedError))
        {
            SetStatus("new scene load failed: " + ScopedError);
            return;
        }
        desc.scenePath = ScopedPath;
        SetStatus("new scene: " + ScopedPath.string());
    }

    void DrawProfiler(Ecs::SystemContext& InContext)
    {
        if (!showProfiler)
        {
            return;
        }
        if (!BeginEditorPanel("Profiler", &showProfiler))
        {
            EndEditorPanel();
            return;
        }
        frameMs.push_back(InContext.dt * 1000.0f);
        if (frameMs.size() > 240)
        {
            frameMs.erase(frameMs.begin());
        }
        PushMono();
        if (ImPlot::BeginPlot("Frame time", ImVec2(-1.0f, 150.0f)))
        {
            ImPlot::SetupAxes("frame", "ms", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
            ImPlotSpec ScopedPlotSpec;
            ScopedPlotSpec.LineColor = PlotColor(0);
            ImPlot::PlotLine("frame", frameMs.data(), static_cast<int>(frameMs.size()), 1.0, 0.0, ScopedPlotSpec);
            ImPlot::EndPlot();
        }
        ImGui::Text("%.2f ms  |  %.0f fps", InContext.dt * 1000.0f, ImGui::GetIO().Framerate);
        if (profiler != nullptr)
        {
            // Rows from the shared profiler: phase scopes and the frame total.
            profilerPanel.Update(*profiler, profilerNames);
            int ScopedSort = static_cast<int>(profilerPanel.SortMode());
            if (ImGui::Combo("Sort", &ScopedSort, "Total\0Average\0P95\0Max\0"))
            {
                profilerPanel.SetSortMode(static_cast<Fby::Core::ProfileSortMode>(ScopedSort));
                profilerPanel.Update(*profiler, profilerNames);
            }
            if (ImGui::BeginTable("profiler_rows", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
            {
                ImGui::TableSetupColumn("Category");
                ImGui::TableSetupColumn("Count");
                ImGui::TableSetupColumn("Avg");
                ImGui::TableSetupColumn("P95");
                ImGui::TableHeadersRow();
                char ScopedBuffer[32];
                for (const Fby::Core::ProfileRow& ScopedRow : profilerPanel.Rows())
                {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(ScopedRow.Name.c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("%llu", static_cast<unsigned long long>(ScopedRow.Count));
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(
                        Fby::Core::FormatMilliseconds(ScopedRow.AverageMs, ScopedBuffer, sizeof(ScopedBuffer)).data());
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(
                        Fby::Core::FormatMilliseconds(ScopedRow.P95Ms, ScopedBuffer, sizeof(ScopedBuffer)).data());
                }
                ImGui::EndTable();
            }
            if (profilerPanel.HiddenCount() > 0)
            {
                ImGui::Text("%u more categories", profilerPanel.HiddenCount());
            }
        }
        PopMono();
        EndEditorPanel();
    }

    void DrawSdfComposer()
    {
        if (!showSdfComposer)
        {
            return;
        }
        if (!BeginEditorPanel("SDF Composer", &showSdfComposer))
        {
            EndEditorPanel();
            return;
        }
        PushMono();
        if (ImGui::Button("Add Box"))
        {
            Fby::SdfShape ScopedShape;
            ScopedShape.SetType(Fby::SdfShapeType::Box);
            ScopedShape.HalfExtents = Fby::Vector2D{8.0f, 8.0f};
            (void)sdfComposer.AddLayer(Fby::SdfComposeOp::Union, ScopedShape);
        }
        ImGui::SameLine();
        if (ImGui::Button("Add Circle"))
        {
            Fby::SdfShape ScopedShape;
            ScopedShape.SetType(Fby::SdfShapeType::Circle);
            ScopedShape.Radius = 8.0f;
            (void)sdfComposer.AddLayer(Fby::SdfComposeOp::Union, ScopedShape);
        }
        ImGui::SameLine();
        if (ImGui::Button("Subtract"))
        {
            Fby::SdfShape ScopedShape;
            ScopedShape.SetType(Fby::SdfShapeType::Circle);
            ScopedShape.Radius = 4.0f;
            (void)sdfComposer.AddLayer(Fby::SdfComposeOp::Subtract, ScopedShape);
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!sdfComposer.CanUndo());
        if (ImGui::Button("Undo"))
        {
            (void)sdfComposer.Undo();
            composerPreviewValid = false;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!sdfComposer.CanRedo());
        if (ImGui::Button("Redo"))
        {
            (void)sdfComposer.Redo();
            composerPreviewValid = false;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::Text("%u layers", sdfComposer.Composer().LayerCount());

        ImGui::Separator();
        const std::span<const Fby::SdfComposeLayer> ScopedLayers = sdfComposer.Composer().Layers();
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedLayers.size(); ++ScopedIndex)
        {
            const Fby::SdfComposeLayer& ScopedLayer = ScopedLayers[ScopedIndex];
            ImGui::PushID(static_cast<int>(ScopedIndex));
            const char* ScopedOpName = ScopedLayer.Op == Fby::SdfComposeOp::Union
                                           ? "union"
                                           : (ScopedLayer.Op == Fby::SdfComposeOp::Subtract ? "subtract" : "intersect");
            ImGui::Text("%u: %s %s at (%.1f, %.1f)", ScopedIndex, ScopedOpName,
                        ScopedLayer.Shape.GetType() == Fby::SdfShapeType::Circle ? "circle" : "box",
                        ScopedLayer.Center.X, ScopedLayer.Center.Y);
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove"))
            {
                (void)sdfComposer.RemoveLayer(ScopedIndex);
                composerPreviewValid = false;
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
        if (ScopedLayers.empty())
        {
            ImGui::TextUnformatted("No layers yet: add a box or a circle.");
        }

        ImGui::Separator();
        if (ImGui::Button("Bake preview"))
        {
            composerPreviewValid = sdfComposer.Composer().Bake(Fby::Vector2D{-32.0f, -32.0f}, Fby::Vector2D{1.0f, 1.0f},
                                                               64, 64, &composerPreviewSamples, &composerPreviewField);
        }
        if (composerPreviewValid)
        {
            std::uint32_t ScopedSolid = 0;
            for (const float ScopedSample : composerPreviewSamples)
            {
                if (ScopedSample < 0.0f)
                {
                    ++ScopedSolid;
                }
            }
            ImGui::Text("Preview: %u of %zu cells solid", ScopedSolid, composerPreviewSamples.size());
        }
        PopMono();
        EndEditorPanel();
    }

    void DrawStats(Ecs::SystemContext& InContext)
    {
        if (!showStats)
        {
            return;
        }
        if (!BeginEditorPanel("Stats", &showStats))
        {
            EndEditorPanel();
            return;
        }
        const Rhi::FrameStats ScopedStats = gpu->Stats();
        const Rhi::DeviceInfo& ScopedInfo = gpu->Info();
        PushMono();
        ImGui::Text("Frame %llu", static_cast<unsigned long long>(InContext.frameIndex));
        ImGui::Text("GPU: %s", ScopedInfo.name);
        ImGui::Text("Items %u  Draws %u  Batches %u", ScopedStats.items, ScopedStats.drawCalls, ScopedStats.batches);
        ImGui::Text("Texture binds %u", ScopedStats.textureBinds);
        ImGui::Text("Uploaded %.1f KiB", static_cast<double>(ScopedStats.uploadedBytes) / 1024.0);
        ImGui::Separator();
        ImGui::Text("Entities %u", static_cast<unsigned>(InContext.registry.storage<Entity>().size()));
        ImGui::Text("Selection: %s", status.empty() ? "-" : status.c_str());
        if (!currentScene.empty())
        {
            ImGui::Text("Scene: %s", currentScene.string().c_str());
        }
        PopMono();
        EndEditorPanel();
    }

    void Menu(Ecs::SystemContext& InContext)
    {
        bool ScopedDirtyShortcut = false;
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::Button("Quit"))
            {
                ImGui::OpenPopup("Quit editor");
            }
            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Project Dashboard..."))
                {
                    dashboard.ShowOpenPanel();
                }
                if (ImGui::MenuItem("New Project..."))
                {
                    dashboard.ShowNewPanel();
                }
                if (ImGui::MenuItem("Open Project..."))
                {
                    dashboard.ShowOpenPanel();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("New Scene...", nullptr, false, projectOpen && window != nullptr))
                {
                    FileFilter ScopedFilter{"FemboyEngine scene", "toml"};
                    const std::string ScopedScenes = projectConfig.Paths().scenes.string();
                    window->ShowSaveFile(
                        "New Scene", std::span<const FileFilter>(&ScopedFilter, 1),
                        [this](std::vector<std::string> InPaths)
                        {
                            if (InPaths.empty())
                            {
                                return;
                            }
                            std::lock_guard<std::mutex> ScopedLock(sceneDialogMutex);
                            pendingNewScenePath = InPaths.front();
                        },
                        ScopedScenes, "untitled.toml");
                }
                if (ImGui::MenuItem("Save Scene", "Ctrl+S", false, !desc.scenePath.empty()))
                {
                    ScopedDirtyShortcut = true;
                }
                if (ImGui::MenuItem("Reload Scene", "F5", false, !currentScene.empty()))
                {
                    std::string ScopedError;
                    if (owner->LoadScene(InContext, currentScene, &ScopedError))
                    {
                        SetStatus("scene reloaded");
                    }
                    else
                    {
                        SetStatus("reload failed: " + ScopedError);
                    }
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Quit", "Ctrl+Q"))
                {
                    owner->RequestQuit();
                }
                ImGui::Separator();
                ImGui::MenuItem("Autosave scene (60 s)", nullptr, &autoSaveEnabled);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Entity"))
            {
                if (ImGui::MenuItem("Add Entity"))
                {
                    SpawnEntity(InContext, camera.center);
                }
                const Entity ScopedSelected = SelectedEntity(InContext.registry);
                const bool ScopedHasSelection = ScopedSelected != Fby::kNullEntity;
                if (ImGui::MenuItem("Duplicate Selected", nullptr, false, ScopedHasSelection))
                {
                    DuplicateSelected(InContext);
                }
                if (ImGui::MenuItem("Delete Selected", nullptr, false, ScopedHasSelection && !pie.Active()))
                {
                    DeleteSelectedEntities(InContext);
                }
                if (ImGui::MenuItem("Focus Selected", "F", false, ScopedHasSelection))
                {
                    FrameSelected(InContext);
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Create Prefab from Selection...", nullptr, false,
                                    ScopedHasSelection && projectOpen && window != nullptr))
                {
                    const std::filesystem::path ScopedPrefabs = projectConfig.Paths().assets / "prefabs";
                    std::error_code ScopedCode;
                    std::filesystem::create_directories(ScopedPrefabs, ScopedCode);
                    std::string ScopedName = "prefab";
                    if (const auto* ScopedEntityName = InContext.registry.try_get<NameComponent>(ScopedSelected);
                        ScopedEntityName != nullptr && ScopedEntityName->Value[0] != '\0')
                    {
                        ScopedName = ScopedEntityName->Value;
                    }
                    ScopedName += ".toml";
                    FileFilter ScopedFilter{"FemboyEngine prefab", "toml"};
                    window->ShowSaveFile(
                        "Create Prefab from Selection", std::span<const FileFilter>(&ScopedFilter, 1),
                        [this](std::vector<std::string> InPaths)
                        {
                            if (InPaths.empty())
                            {
                                return;
                            }
                            std::lock_guard<std::mutex> ScopedLock(sceneDialogMutex);
                            pendingPrefabPath = InPaths.front();
                        },
                        ScopedPrefabs.string(), ScopedName);
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Build"))
            {
                const bool ScopedCanBuild = projectOpen && !releaseCooker.Running();
                if (ImGui::MenuItem("Build & Pack Game", "Ctrl+B", false, ScopedCanBuild))
                {
                    StartReleaseCook();
                }
                if (ImGui::MenuItem("Cancel Release Cook", nullptr, false, releaseCooker.Running()))
                {
                    releaseCooker.Cancel();
                    SetStatus("release cook cancellation requested");
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Show Release Progress"))
                {
                    showSceneDiagnostics = true;
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("View"))
            {
                const bool ScopedFullscreen = window != nullptr && window->Mode() == WindowMode::BorderlessFullscreen;
                if (ImGui::MenuItem("Fullscreen (Borderless)", "F11", ScopedFullscreen) && window != nullptr)
                {
                    (void)window->SetWindowMode(ScopedFullscreen ? WindowMode::Windowed
                                                                 : WindowMode::BorderlessFullscreen);
                }
                ImGui::Separator();
                ImGui::MenuItem("Hierarchy", nullptr, &showHierarchy);
                ImGui::MenuItem("Content Browser", nullptr, &showAssets);
                ImGui::MenuItem("Inspector", nullptr, &showInspector);
                ImGui::MenuItem("Viewport", nullptr, &showViewport);
                ImGui::MenuItem("PIE Client", nullptr, &showPieClient);
                ImGui::MenuItem("Profiler", "F7", &showProfiler);
                ImGui::MenuItem("SDF Composer", "F8", &showSdfComposer);
                ImGui::MenuItem("Scene Diagnostics", nullptr, &showSceneDiagnostics);
                ImGui::MenuItem("SDF Character", nullptr, &showSdfCharacters);
                ImGui::MenuItem("Plugin Manager", nullptr, &showPluginManager);
                ImGui::MenuItem("LogBoy", nullptr, &showLogBoy);
                ImGui::MenuItem("Physics Debug Draw", nullptr, &showPhysicsDebug);
                ImGui::MenuItem("Navigation Debug Draw", nullptr, &showNavigationDebug);
                ImGui::MenuItem("Light Debug Draw", nullptr, &showLightDebug);
                if (ImGui::BeginMenu("Viewport Mode"))
                {
                    const std::uint32_t ScopedCurrentMode =
                        ClampViewportViewMode(projectConfig.Editor().viewportViewMode);
                    for (const Rhi::ViewportModeDescriptor& ScopedDescriptor : Rhi::AllViewportModes())
                    {
                        const bool ScopedSelected =
                            static_cast<std::uint32_t>(ScopedDescriptor.Mode) == ScopedCurrentMode;
                        if (ImGui::MenuItem(ScopedDescriptor.Label, nullptr, ScopedSelected))
                        {
                            projectConfig.Editor().viewportViewMode = static_cast<std::uint32_t>(ScopedDescriptor.Mode);
                            SetStatus(std::string("view mode: ") + ScopedDescriptor.ShortLabel);
                        }
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Next Mode", "F6"))
                    {
                        projectConfig.Editor().viewportViewMode = static_cast<std::uint32_t>(
                            Rhi::CycleViewportMode(static_cast<Rhi::ViewportMode>(ScopedCurrentMode), 1));
                        SetStatus(std::string("view mode: ") + Rhi::ViewportModeName(static_cast<Rhi::ViewportMode>(
                                                                   projectConfig.Editor().viewportViewMode)));
                    }
                    ImGui::EndMenu();
                }
                ImGui::MenuItem("Console", nullptr, &showConsole);
                ImGui::MenuItem("Stats", nullptr, &showStats);
                ImGui::Separator();
                ImGui::MenuItem("Material Graph", nullptr, &showMaterialGraph);
                ImGui::MenuItem("VisualScript", nullptr, &showVisualScript);
                ImGui::MenuItem("Text Preview", nullptr, &showTextPreview);
                ImGui::MenuItem("Script Debugger", nullptr, &showScriptDebugger);
                ImGui::MenuItem("Tilemap", nullptr, &showTilemap);
                ImGui::MenuItem("Virtual Texture", nullptr, &showVirtualTexture);
                ImGui::MenuItem("AI State Tree", nullptr, &showAIStateTree);
                ImGui::MenuItem("Animation Graph", nullptr, &showAnimationGraph);
                ImGui::MenuItem("Directional Blend Space", nullptr, &showDirectionalBlendSpace);
                ImGui::Separator();
                ImGui::MenuItem("Level Settings", nullptr, &showLevelSettings);
                ImGui::MenuItem("Project Settings", nullptr, &showProjectSettings);
                ImGui::MenuItem("Editor Settings", nullptr, &showEditorSettings);
                ImGui::MenuItem("Player Settings", nullptr, &showUserSettings);
                ImGui::MenuItem("Licenses", nullptr, &showLicenses);
                ImGui::MenuItem("Network Diagnostics", nullptr, &showNetworkDiagnostics);
                ImGui::MenuItem("Voice Devices", nullptr, &showVoiceDevices);
                ImGui::MenuItem("Particle FX", nullptr, &showParticleFx);
                ImGui::MenuItem("Transport Groups", nullptr, &showTransportGroups);
                ImGui::MenuItem("ImGui demo", nullptr, &showDemo);
                ImGui::EndMenu();
            }
            ImGui::TextDisabled("  %s", desc.title.c_str());
            if (ImGui::BeginPopupModal("Quit editor", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::TextUnformatted("Quit FemboyEngine editor?");
                ImGui::Separator();
                if (ImGui::Button("Quit", ImVec2(120.0f, 0.0f)))
                {
                    ImGui::CloseCurrentPopup();
                    owner->RequestQuit();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(90.0f, 0.0f)))
                {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
            ImGui::EndMainMenuBar();
        }

        const ImGuiIO& ScopedIo = ImGui::GetIO();
        if (ScopedIo.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Q, false))
        {
            owner->RequestQuit();
        }
        if (ScopedIo.KeyCtrl && !ScopedIo.WantTextInput)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_C, false))
            {
                CopySelection(InContext);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_V, false))
            {
                PasteClipboard(InContext);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_D, false))
            {
                DuplicateSelected(InContext);
            }
        }
        if (!ScopedIo.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
        {
            DeleteSelectedEntities(InContext);
        }
        if (!ScopedIo.WantTextInput && !ScopedIo.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F6, false))
        {
            const std::uint32_t ScopedCurrentMode = ClampViewportViewMode(projectConfig.Editor().viewportViewMode);
            projectConfig.Editor().viewportViewMode = static_cast<std::uint32_t>(
                Rhi::CycleViewportMode(static_cast<Rhi::ViewportMode>(ScopedCurrentMode), 1));
            SetStatus(std::string("view mode: ") +
                      Rhi::ViewportModeName(static_cast<Rhi::ViewportMode>(projectConfig.Editor().viewportViewMode)));
        }
        if (!ScopedIo.WantTextInput && !ScopedIo.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F7, false))
        {
            showProfiler = !showProfiler;
        }
        if (!ScopedIo.WantTextInput && !ScopedIo.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F8, false))
        {
            showSdfComposer = !showSdfComposer;
        }
        if (!ScopedIo.WantTextInput && pie.Active() && piePaused && ImGui::IsKeyPressed(ImGuiKey_F10, false))
        {
            pieCommand = PieCommand::Step;
        }
        if (ScopedDirtyShortcut || (ScopedIo.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)))
        {
            (void)SaveCurrentScene(InContext, false);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_F5, false) && !currentScene.empty())
        {
            std::string ScopedError;
            if (owner->LoadScene(InContext, currentScene, &ScopedError))
            {
                SetStatus("scene reloaded");
            }
            else
            {
                SetStatus("reload failed: " + ScopedError);
            }
        }
    }
};

// EditorSystem
EditorSystem::EditorSystem() : MImpl(std::make_unique<Impl>())
{
    MImpl->owner = this;
}

EditorSystem::~EditorSystem()
{
    // A destructor is implicitly noexcept; Shutdown performs filesystem and
    // ImGui teardown that may allocate. Contain any failure so the process logs
    // and exits instead of terminating during unwinding.
    try
    {
        Shutdown();
    }
    catch (...)
    {
    }
}

bool EditorSystem::Initialize(Ecs::SystemContext& InContext, const EditorDesc& InDesc)
{
    if (MImpl->initialized)
    {
        return true;
    }
    if (MImpl->voicePresets.Empty())
    {
        MImpl->voicePresets.RegisterBuiltIns();
    }
    MImpl->voiceSfx.Configure(MImpl->voiceSfxSettings);
    MImpl->voiceSfxApplied = MImpl->voiceSfxSettings;
    if (InContext.renderer == nullptr || InContext.window == nullptr)
    {
        LogError("editor", "EditorSystem::Initialize needs SystemContext::renderer and ::window");
        return false;
    }

    MImpl->gpu = InContext.renderer;
    MImpl->window = InContext.window;
    MImpl->vfs = InContext.assets;
    MImpl->projectSettings.SetUpscalerCaps(MImpl->gpu->Info().upscalers);
    MImpl->backendCaps = Fby::Rhi::CapabilitiesFromDevice(MImpl->gpu->Info());
    MImpl->projectSettings.SetBackendCapabilities(MImpl->backendCaps);
    if (InContext.accessibility == nullptr)
    {
        InContext.accessibility = &MImpl->accessibility;
    }
    if (InContext.gameplayTags == nullptr)
    {
        InContext.gameplayTags = &MImpl->gameplayTags;
    }
    if (InContext.gameplayAttributes == nullptr)
    {
        InContext.gameplayAttributes = &MImpl->gameplayAttributes;
    }
    MImpl->desc = InDesc;
    MImpl->visible = InDesc.startVisible;
    MImpl->showDemo = InDesc.showDemoWindow;
    MImpl->iniPath = InDesc.iniPath.string();
    {
        std::string ScopedUserError;
        if (!LoadUserSettings("Saved/Config/UserSettings.toml", &MImpl->userSettings, &ScopedUserError))
        {
            LogInfo("editor", "user settings not loaded: {}", ScopedUserError);
        }
        if (MImpl->userSettings.voicePresetId[0] != '\0')
        {
            MImpl->voiceSelection.Local.Set(MImpl->userSettings.voicePresetId,
                                            MImpl->userSettings.voicePresetIntensity);
        }
        MImpl->voicePolicy.MumbleWhenModerated = MImpl->userSettings.voiceModerationMumble;
        MImpl->voiceSfxSettings.Enabled = MImpl->userSettings.voiceReactiveSfx;
        MImpl->voiceSfxSettings.MasterGain = MImpl->userSettings.voiceReactiveSfxGain;
        MImpl->voiceSfx.Configure(MImpl->voiceSfxSettings);
        MImpl->voiceSfxApplied = MImpl->voiceSfxSettings;
    }
    Scene::RegisterBuiltInComponents(MImpl->components);
    MImpl->textureArray = InDesc.textureArray;
    MImpl->thumbnails.Initialize(InContext.renderer);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    MImpl->imguiContext = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(MImpl->imguiContext);

    ImGuiIO& ScopedIo = ImGui::GetIO();
    ScopedIo.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ScopedIo.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ScopedIo.ConfigWindowsMoveFromTitleBarOnly = true;
    ScopedIo.IniFilename = MImpl->iniPath.c_str();

    ApplyEditorTheme(MImpl->projectConfig.Editor());
    MImpl->styleUiScale = InDesc.uiScale;
    MImpl->ApplyStyleScale();

    if (!ImGui_ImplSDL3_InitForVulkan(static_cast<SDL_Window*>(MImpl->window->NativeWindow())))
    {
        LogError("editor", "ImGui_ImplSDL3_InitForVulkan failed");
        Shutdown();
        return false;
    }
    MImpl->sdlInitialized = true;

    Rhi::UiBackendInfo ScopedBackend;
    if (!MImpl->gpu->QueryUiBackend(ScopedBackend))
    {
        LogError("editor", "the RHI does not expose a UI backend (Vulkan only)");
        Shutdown();
        return false;
    }

    if (!ImGui_ImplVulkan_LoadFunctions(ScopedBackend.apiVersion, &Impl::LoadVulkanFunction, MImpl->gpu))
    {
        LogError("editor", "ImGui_ImplVulkan_LoadFunctions failed");
        Shutdown();
        return false;
    }

    ImGui_ImplVulkan_InitInfo ScopedInfo{};
    ScopedInfo.ApiVersion = ScopedBackend.apiVersion;
    ScopedInfo.Instance = static_cast<VkInstance>(ScopedBackend.instance);
    ScopedInfo.PhysicalDevice = static_cast<VkPhysicalDevice>(ScopedBackend.physicalDevice);
    ScopedInfo.Device = static_cast<VkDevice>(ScopedBackend.device);
    ScopedInfo.QueueFamily = ScopedBackend.queueFamily;
    ScopedInfo.Queue = static_cast<VkQueue>(ScopedBackend.queue);
    ScopedInfo.DescriptorPool = static_cast<VkDescriptorPool>(ScopedBackend.descriptorPool);
    ScopedInfo.DescriptorPoolSize = ScopedBackend.descriptorPoolSize;
    ScopedInfo.MinImageCount = ScopedBackend.minImageCount;
    ScopedInfo.ImageCount = ScopedBackend.imageCount;
    ScopedInfo.PipelineCache = static_cast<VkPipelineCache>(ScopedBackend.pipelineCache);
    ScopedInfo.PipelineInfoMain.RenderPass = static_cast<VkRenderPass>(ScopedBackend.renderPass);
    ScopedInfo.PipelineInfoMain.Subpass = 0;
    ScopedInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    ScopedInfo.UseDynamicRendering = ScopedBackend.dynamicRendering;
    ScopedInfo.CheckVkResultFn = &Impl::CheckVulkanResult;

    if (!ImGui_ImplVulkan_Init(&ScopedInfo))
    {
        LogError("editor", "ImGui_ImplVulkan_Init failed");
        Shutdown();
        return false;
    }
    MImpl->vulkanInitialized = true;

    // Fonts are installed after the Vulkan backend, because the dynamic-atlas
    MImpl->InitializeFonts(ScopedIo, InDesc.uiScale);
    if (MImpl->vfs != nullptr && MImpl->dashboard.Visible())
    {
        MImpl->dashboard.RefreshTypography(InDesc.uiScale);
    }

    MImpl->plotContext = ImPlot::CreateContext();
    MImpl->window->SetEventObserver(
        [](void* InUser, const void* InEvent)
        {
            static_cast<EditorSystem*>(InUser)->OnPlatformEvent(InEvent);
        },
        this);
    MImpl->gpu->SetUiRenderer(&Impl::RenderThunk, this);
    // The ImGui ini remembers the dock layout. A stale one (older panel set or
    // the pre-readability split) is rebuilt once and then versioned.
    const std::filesystem::path ScopedLayoutMarker = std::filesystem::path(InDesc.iniPath.string() + ".layout");
    std::uint32_t ScopedLayoutVersion = 0;
    {
        std::ifstream ScopedMarker(ScopedLayoutMarker);
        if (ScopedMarker)
        {
            ScopedMarker >> ScopedLayoutVersion;
        }
    }
    MImpl->needsLayout = !std::filesystem::exists(InDesc.iniPath) || ScopedLayoutVersion != kEditorLayoutVersion;
    MImpl->layoutMarker = ScopedLayoutMarker;
    MImpl->initialized = true;

    // Authoring services: the command stack is bound to the editor registry
    (void)MImpl->commands.Initialize();
    MImpl->commands.Bind(&InContext.registry, &MImpl->components);
    for (const Scene::ComponentOps& ScopedOps : MImpl->components.All())
    {
        if (ScopedOps.serializable && ScopedOps.size <= kMaxTrackedComponentBytes)
        {
            (void)MImpl->commands.RegisterComponentOps(ScopedOps);
        }
    }
    MImpl->transformCodec = MImpl->commands.CodecForLayoutHash(Reflect::LayoutHash<Transform2D>());

    // Settings are external CommandStack targets so a slider drag has the same
    MImpl->projectSettingsCodec =
        MImpl->commands.RegisterExternalBuffer("ProjectSettings", &MImpl->projectConfig.Project(),
                                               static_cast<std::uint32_t>(sizeof(Fby::Editor::ProjectSettings)));
    MImpl->editorSettingsCodec =
        MImpl->commands.RegisterExternalBuffer("EditorSettings", &MImpl->projectConfig.Editor(),
                                               static_cast<std::uint32_t>(sizeof(Fby::Editor::EditorSettings)));
    MImpl->projectSettings.Initialize(MImpl->commands, MImpl->projectConfig, MImpl->projectSettingsCodec);
    MImpl->editorSettings.Initialize(MImpl->commands, MImpl->projectConfig, MImpl->editorSettingsCodec);

    // Both visual editors share the CommandStack with the Inspector.
    (void)MImpl->materialGraph.Initialize(MImpl->commands, MImpl->gpu);
    Script::AotBuildOptions ScopedAotOptions;
    ScopedAotOptions.outputDirectory = std::filesystem::current_path() / "build" / "aot";
    ScopedAotOptions.includeDirectory = FBY_ENGINE_INCLUDE_DIR;
    ScopedAotOptions.moduleName = "logic_graph";
    (void)MImpl->visualScript.Initialize(MImpl->commands, &MImpl->components, ScopedAotOptions);
    // The debugger's second compile path cooks text scripts into their own module.
    Script::AotBuildOptions ScopedDebugAotOptions = ScopedAotOptions;
    ScopedDebugAotOptions.moduleName = "script_debug";
    MImpl->scriptDebugger.SetAotOptions(ScopedDebugAotOptions);
    (void)MImpl->aiStateTree.Initialize(MImpl->commands);
    MImpl->aiStateTree.SetGameplayTagRegistry(&MImpl->gameplayTags);
    (void)MImpl->animationGraph.Initialize(MImpl->commands);
    (void)MImpl->directionalBlendSpace.Initialize(MImpl->commands);
    MImpl->animationGraph.SetGameplayTagRegistry(&MImpl->gameplayTags);

    // Allocate the PIE arena once, at editor startup.
    MImpl->pieArena.resize(kPieArenaBytes);
    if (!MImpl->pie.Initialize(MImpl->pieArena, Ecs::SnapshotSchema::BuiltIns(), MImpl->pieArena.size()))
    {
        LogWarn("editor", "PIE arena initialization failed; the Play button will report the reason");
    }
    else
    {
        MImpl->pieInitialized = true;
    }

    // A project is only a project when its project.femproj exists. Accepting an
    // arbitrary working directory would offer "Continue with this project" for
    // a folder that Load() then rejects, which is the broken path users hit when
    // the editor starts outside a project.
    std::filesystem::path ScopedProjectPath = InDesc.initialProjectPath;
    if (ScopedProjectPath.empty())
    {
        ScopedProjectPath = InDesc.projectRoot;
    }
    if (ScopedProjectPath.empty())
    {
        std::error_code ScopedCode;
        ScopedProjectPath = std::filesystem::current_path(ScopedCode);
        if (ScopedCode)
        {
            ScopedProjectPath.clear();
        }
    }
    if (!ScopedProjectPath.empty())
    {
        std::error_code ScopedCode;
        std::filesystem::path ScopedProjectDirectory = ScopedProjectPath;
        if (ScopedProjectDirectory.extension() == ".femproj")
        {
            ScopedProjectDirectory = ScopedProjectDirectory.parent_path();
        }
        if (!std::filesystem::is_regular_file(ScopedProjectDirectory / "project.femproj", ScopedCode))
        {
            if (!InDesc.initialProjectPath.empty())
            {
                LogWarn("editor", "no project.femproj at '{}'; showing the project dashboard",
                        InDesc.initialProjectPath.string());
            }
            ScopedProjectPath.clear();
        }
    }

    ProjectPaths ScopedInitialPaths;
    const bool ScopedHasProjectPath =
        !ScopedProjectPath.empty() && ResolveProjectPaths(ScopedProjectPath, &ScopedInitialPaths, nullptr);
    MImpl->contentBrowser.Initialize(
        InContext.assetDb,
        ContentBrowserPanel::Config{ScopedHasProjectPath ? ScopedInitialPaths.assets : std::filesystem::path{}},
        &MImpl->importer);
    MImpl->dashboard.Initialize(ScopedInitialPaths, MImpl->projectConfig.Config(),
                                InDesc.showProjectDashboard && !ScopedHasProjectPath, MImpl->window, MImpl->vfs,
                                &MImpl->fonts, InDesc.uiScale);
    MImpl->desc.iniPath = InDesc.iniPath;

    if (ScopedHasProjectPath)
    {
        ProjectConfig ScopedInitialConfig;
        std::string ScopedProjectError;
        if (LoadProject(ScopedInitialPaths.root, nullptr, &ScopedInitialConfig, &ScopedProjectError))
        {
            // Opening at startup must behave exactly like the dashboard's Open:
            // the project's default scene is loaded, not the engine demo world.
            if (!OpenProject(InContext, ScopedInitialPaths, ScopedInitialConfig, &ScopedProjectError, true))
            {
                LogWarn("editor", "project '{}' could not be opened: {}", ScopedInitialPaths.root.string(),
                        ScopedProjectError);
                MImpl->dashboard.Initialize(ScopedInitialPaths, ScopedInitialConfig, InDesc.showProjectDashboard,
                                            MImpl->window, MImpl->vfs, &MImpl->fonts, InDesc.uiScale);
            }
        }
        else
        {
            LogWarn("editor", "project config rejected: {}", ScopedProjectError);
            MImpl->dashboard.Initialize(ScopedInitialPaths, MImpl->projectConfig.Config(), InDesc.showProjectDashboard,
                                        MImpl->window, MImpl->vfs, &MImpl->fonts, InDesc.uiScale);
        }
    }
    else if (InDesc.showProjectDashboard)
    {
        MImpl->dashboard.SetVisible(true);
    }

    LogInfo("editor", "Dear ImGui {} + ImGuizmo + ImPlot initialized (SDL3 + Vulkan)", IMGUI_VERSION);
    MImpl->PrintConsole(std::format("FemboyEngine2D editor - ImGui {}", IMGUI_VERSION));
    MImpl->logBoy.Initialize();
    const EditorFontStatus& ScopedFontStatus = MImpl->fonts.Status();
    MImpl->PrintConsole(std::format("fonts: Inter {:.1f}px / JetBrains Mono {:.1f}px, {:.2f}x display scale ({})",
                                    static_cast<double>(EditorFontSpec{}.regularPx),
                                    static_cast<double>(EditorFontSpec{}.monoPx),
                                    static_cast<double>(ScopedFontStatus.displayScale),
                                    MImpl->vfs != nullptr ? "VFS mmap, subpixel" : "embedded vector fallback"));
    MImpl->PrintConsole("panels: Hierarchy, Viewport, Inspector, Content Browser, Settings, LogBoy, Scene Diagnostics, "
                        "Profiler, Console, Stats, Material Graph, VisualScript, AI State Tree, Animation Graph");

    if (!InDesc.scenePath.empty() && std::filesystem::exists(InDesc.scenePath))
    {
        std::string ScopedError;
        if (!LoadScene(InContext, InDesc.scenePath, &ScopedError))
        {
            LogWarn("editor", "could not load '{}': {}", InDesc.scenePath.string(), ScopedError);
        }
    }
    return true;
}

void EditorSystem::Shutdown()
{
    if (MImpl == nullptr)
    {
        return;
    }
    if (!MImpl->initialized && MImpl->imguiContext == nullptr && MImpl->gpu == nullptr)
    {
        return;
    }
    MImpl->initialized = false;

    // Thumbnail descriptors reference ImGui's Vulkan backend; drop the cache
    // before the ImGui context and the renderer go away.
    MImpl->thumbnails.Clear();
    // Persist the final settings image before the background threads and the device shut down.
    if (MImpl->projectConfig.Loaded())
    {
        std::string ScopedPersistError;
        if (!MImpl->projectConfig.PersistIfChanged(&ScopedPersistError))
        {
            LogWarn("editor", "could not persist project settings on shutdown: {}", ScopedPersistError);
        }
    }
    MImpl->StopProjectSystems();
    LogInfo("editor", "shutdown: project systems stopped");
    // Stop() destroys the PIE bodies and its isolated Box2D world; Shutdown
    MImpl->pie.Shutdown();
    MImpl->pieInitialized = false;
    MImpl->graph = nullptr;
    MImpl->piePaused = false;

    if (MImpl->window != nullptr)
    {
        MImpl->window->ClearEventObserver();
    }
    if (MImpl->gpu != nullptr)
    {
        MImpl->gpu->WaitIdle();
        MImpl->materialGraph.Shutdown(*MImpl->gpu);
        MImpl->visualScript.Shutdown();
        MImpl->aiStateTree.Shutdown();
        MImpl->animationGraph.Shutdown();
        MImpl->gpu->SetUiRenderer(nullptr, nullptr);
        if (MImpl->imguiContext != nullptr)
        {
            ImGui::SetCurrentContext(MImpl->imguiContext);
        }
        if (MImpl->vulkanInitialized)
        {
            MImpl->ReleaseTargetTexture();
        }
        for (Impl::RetiredTarget& ScopedRetired : MImpl->retiredTargets)
        {
            Impl::RemoveTexture(ScopedRetired.texture);
            if (ScopedRetired.target.IsValid() && MImpl->gpu != nullptr)
            {
                MImpl->gpu->DestroyRenderTarget(ScopedRetired.target);
            }
        }
        MImpl->retiredTargets.clear();
        if (MImpl->target.IsValid())
        {
            MImpl->gpu->DestroyRenderTarget(MImpl->target);
            MImpl->target = {};
        }
        // The PIE client viewport owns a second target and descriptor; drop both
        // while the ImGui Vulkan backend and the renderer are still alive.
        if (MImpl->vulkanInitialized)
        {
            MImpl->ReleasePieClientTexture();
        }
        MImpl->pieClientViewport.Release(*MImpl->gpu);
    }
    if (MImpl->imguiContext != nullptr)
    {
        ImGui::SetCurrentContext(MImpl->imguiContext);
        if (MImpl->vulkanInitialized)
        {
            ImGui_ImplVulkan_Shutdown();
            MImpl->vulkanInitialized = false;
        }
        if (MImpl->sdlInitialized)
        {
            ImGui_ImplSDL3_Shutdown();
            MImpl->sdlInitialized = false;
            // The SDL3 backend publishes the window id on the main viewport but
            // its Shutdown only clears the platform handlers. Clear the handles
            // too, so DestroyContext does not report a forgotten platform
            // window on every exit.
            ImGuiViewport* ScopedMainViewport = ImGui::GetMainViewport();
            if (ScopedMainViewport != nullptr)
            {
                ScopedMainViewport->PlatformHandle = nullptr;
                ScopedMainViewport->PlatformHandleRaw = nullptr;
            }
        }
        // Detach the Log() observer before the ImGui context and the panel are destroyed.
        MImpl->logBoy.Shutdown();
        if (MImpl->plotContext != nullptr)
        {
            ImPlot::DestroyContext(MImpl->plotContext);
            MImpl->plotContext = nullptr;
        }
        ImGui::DestroyContext(MImpl->imguiContext);
        MImpl->imguiContext = nullptr;
    }
    MImpl->gpu = nullptr;
    MImpl->window = nullptr;
}

bool EditorSystem::IsInitialized() const noexcept
{
    return MImpl != nullptr && MImpl->initialized;
}

bool EditorSystem::Register(Ecs::SystemGraph& InGraph)
{
    // The PIE step runs the simulation phases through the same graph the game
    MImpl->graph = &InGraph;
    // Function pointer + object pointer: the graph never stores a std::function
    return InGraph.Add(
        Ecs::Phase::Render, "editor",
        +[](Ecs::SystemContext& InContext, void* InUser)
        {
            static_cast<EditorSystem*>(InUser)->Tick(InContext);
        },
        this);
}

namespace
{
bool DropVoicePacket(void*, std::uint8_t, std::span<const std::uint8_t>) noexcept
{
    return false;
}
}

void EditorSystem::Tick(Ecs::SystemContext& InContext)
{
    // A path chosen in the New Scene dialog is applied outside the dialog
    // thread, before any ImGui window is built for this frame.
    std::string ScopedPendingScene;
    std::string ScopedPendingPrefab;
    {
        std::lock_guard<std::mutex> ScopedLock(MImpl->sceneDialogMutex);
        ScopedPendingScene.swap(MImpl->pendingNewScenePath);
        ScopedPendingPrefab.swap(MImpl->pendingPrefabPath);
    }
    if (InContext.cameraStack != nullptr)
    {
        InContext.cameraStack->SetComfortShakeScale(MImpl->projectConfig.Project().motionComfort.cameraShakeScale);
    }
    if (!ScopedPendingScene.empty())
    {
        MImpl->CreateSceneAndLoad(InContext, ScopedPendingScene);
    }
    if (!ScopedPendingPrefab.empty())
    {
        std::string ScopedPrefabError;
        if (MImpl->SaveSelectionAsPrefab(InContext, ScopedPendingPrefab, &ScopedPrefabError))
        {
            MImpl->SetStatus("prefab saved: " + ScopedPendingPrefab);
            MImpl->PrintConsole("prefab saved: " + ScopedPendingPrefab);
        }
        else
        {
            MImpl->SetStatus("prefab failed: " + ScopedPrefabError);
        }
    }
    // Dirty marker and autosave. The marker is derived from the command stack, so
    // undoing back to the saved state clears it again.
    const CommandStack::Stats ScopedCommandStats = MImpl->commands.GetStats();
    MImpl->sceneDirty = MImpl->projectOpen && (ScopedCommandStats.commands != MImpl->savedCommandCount ||
                                               ScopedCommandStats.undoDepth != MImpl->savedCursor);
    if (MImpl->window != nullptr && MImpl->titleDirty != MImpl->sceneDirty)
    {
        MImpl->titleDirty = MImpl->sceneDirty;
        MImpl->window->SetTitle(MImpl->sceneDirty ? "FemboyEngine2D 1.0 - editor *" : "FemboyEngine2D 1.0 - editor");
    }
    if (MImpl->projectOpen && MImpl->sceneDirty && MImpl->autoSaveEnabled && !MImpl->pie.Active() &&
        !MImpl->desc.scenePath.empty())
    {
        MImpl->autoSaveTimer += InContext.dt;
        if (MImpl->autoSaveTimer >= MImpl->autoSaveSeconds)
        {
            MImpl->autoSaveTimer = 0.0f;
            if (MImpl->SaveCurrentScene(InContext, true))
            {
                MImpl->PrintConsole("autosaved the scene");
                LogInfo("editor", "autosaved '{}'", MImpl->desc.scenePath.string());
            }
        }
    }
    else
    {
        MImpl->autoSaveTimer = 0.0f;
    }
    if (MImpl->projectOpen && InContext.audio != nullptr && InContext.audio->IsValid())
    {
        if (!MImpl->voiceAttempted)
        {
            MImpl->voiceAttempted = true;
            std::string ScopedVoiceError;
            if (!MImpl->voiceDevicesReady)
            {
                MImpl->voiceDevicesReady = MImpl->voiceDevicesEnumerator.Initialize(&ScopedVoiceError);
            }
            auto ScopedRuntime = std::make_unique<VoiceSystem>();
            VoiceSystemConfig ScopedConfig{};
            ScopedConfig.LocalPlayer = 0;
            const auto& ScopedProject = MImpl->projectConfig.Config().project;
            ScopedConfig.Denoise =
                static_cast<VoiceDenoiseTier>(std::clamp<std::uint32_t>(ScopedProject.voiceDenoise, 0, 3));
            ScopedConfig.Aec.Enabled = ScopedProject.voiceAec;
            ScopedConfig.Aec.TailMs = ScopedProject.voiceAecTailMs;
            ScopedConfig.Aec.StepSize = ScopedProject.voiceAecStep;
            if (ScopedRuntime->Start(InContext.audio->Mixer(), InContext.audio->ReferenceRing(), ScopedConfig,
                                     &DropVoicePacket, nullptr))
            {
                if (MImpl->voiceDevicesReady)
                {
                    const AudioDeviceList& ScopedInputs = MImpl->voiceDevicesEnumerator.Devices(AudioDeviceKind::Input);
                    std::int32_t ScopedIndex =
                        MImpl->voiceDevicesEnumerator.Resolve(AudioDeviceKind::Input, ScopedProject.voiceInputDevice);
                    if (ScopedIndex < 0 && ScopedInputs.Count > 0)
                    {
                        ScopedIndex = 0;
                    }
                    if (ScopedIndex >= 0)
                    {
                        (void)ScopedRuntime->StartCapture(MImpl->voiceDevicesEnumerator,
                                                          ScopedInputs.Devices[static_cast<std::uint32_t>(ScopedIndex)],
                                                          &ScopedVoiceError);
                    }
                }
                MImpl->voiceRuntime = std::move(ScopedRuntime);
            }
        }
        if (MImpl->voiceRuntime != nullptr)
        {
            MImpl->voiceRuntime->Tick(InContext.dt, MImpl->voiceSession);
            if (MImpl->voicePresets.Empty())
            {
                MImpl->voicePresets.RegisterBuiltIns();
            }
            MImpl->voiceApplier.Apply(*MImpl->voiceRuntime, MImpl->voiceSession, MImpl->voicePresets,
                                      MImpl->voicePolicy, MImpl->voiceSelection);
            std::snprintf(MImpl->userSettings.voicePresetId, sizeof(MImpl->userSettings.voicePresetId), "%s",
                          MImpl->voiceSelection.Local.Enabled ? MImpl->voiceSelection.Local.Id : "");
            MImpl->userSettings.voicePresetIntensity = MImpl->voiceSelection.Local.Intensity;
            MImpl->userSettings.voiceModerationMumble = MImpl->voicePolicy.MumbleWhenModerated;
            MImpl->userSettings.voiceReactiveSfx = MImpl->voiceSfxSettings.Enabled;
            MImpl->userSettings.voiceReactiveSfxGain = MImpl->voiceSfxSettings.MasterGain;
            if (!(MImpl->voiceSfxSettings == MImpl->voiceSfxApplied))
            {
                MImpl->voiceSfx.Configure(MImpl->voiceSfxSettings);
                MImpl->voiceSfxApplied = MImpl->voiceSfxSettings;
            }
            if (InContext.audio != nullptr && InContext.audio->IsValid() && !MImpl->voiceSfxClipsReady)
            {
                std::array<float, Fby::kVoiceSfxMaxSamples> ScopedBuffer{};
                for (std::uint8_t ScopedKind = 1; ScopedKind < static_cast<std::uint8_t>(Fby::VoiceSfxKind::Count);
                     ++ScopedKind)
                {
                    const auto ScopedValue = static_cast<Fby::VoiceSfxKind>(ScopedKind);
                    const std::uint32_t ScopedSamples =
                        Fby::SynthesizeVoiceSfx(ScopedValue, 48000, std::span<float>(ScopedBuffer));
                    if (ScopedSamples == 0)
                    {
                        continue;
                    }
                    const std::string ScopedPath = std::string("voice_sfx/") + Fby::VoiceSfxKindName(ScopedValue);
                    const Fby::AssetId ScopedClip = Fby::AssetId::FromPath(ScopedPath, Fby::AssetKind::AudioClip);
                    if (InContext.audio->RegisterClip(
                            ScopedClip, std::span<const float>(ScopedBuffer.data(), ScopedSamples), 1, 48000))
                    {
                        MImpl->voiceSfxClips[ScopedKind] = ScopedClip;
                    }
                }
                MImpl->voiceSfxClipsReady = true;
            }

            Fby::VoiceReactiveSfxInput ScopedSfxInput{};
            for (std::uint8_t ScopedPlayer = 0; ScopedPlayer < static_cast<std::uint8_t>(Fby::kVoiceMaxPeers);
                 ++ScopedPlayer)
            {
                const Fby::VoiceParticipant* ScopedParticipant = MImpl->voiceSession.Participant(ScopedPlayer);
                if (ScopedParticipant == nullptr || !ScopedParticipant->Present)
                {
                    continue;
                }
                Fby::VoiceReactiveSfxPlayer& ScopedSource = ScopedSfxInput.Players[ScopedPlayer];
                ScopedSource.Present = true;
                ScopedSource.Talking = ScopedParticipant->Talking;
                ScopedSource.LocallyMuted = ScopedParticipant->LocalMuted;
                ScopedSource.Position = ScopedParticipant->SourcePosition;
                ScopedSource.Pan = ScopedParticipant->SpatialPan;
                const Fby::VoiceConditionOutput* ScopedCondition =
                    MImpl->voiceRuntime->ParticipantCondition(ScopedPlayer);
                ScopedSource.Submerged = ScopedCondition != nullptr && ScopedCondition->Submerged;
            }
            std::array<Fby::VoiceSfxEvent, 8> ScopedSfxEvents{};
            const std::uint32_t ScopedSfxCount = MImpl->voiceSfx.Update(InContext.dt, ScopedSfxInput, ScopedSfxEvents);
            if (InContext.audio != nullptr)
            {
                for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedSfxCount; ++ScopedIndex)
                {
                    const Fby::VoiceSfxEvent& ScopedEvent = ScopedSfxEvents[ScopedIndex];
                    const Fby::AssetId ScopedClip = MImpl->voiceSfxClips[static_cast<std::size_t>(ScopedEvent.Kind)];
                    if (ScopedClip.IsValid())
                    {
                        (void)Fby::PlayVoiceSfx(*InContext.audio, ScopedClip, ScopedEvent);
                    }
                }
            }
        }
    }

    // Main-thread speech pump: SAPI is apartment-threaded and AVSpeech owns a queue.
    if (InContext.accessibility != nullptr)
    {
        InContext.accessibility->FlushSpeech();
    }
    if (!MImpl->initialized || !MImpl->visible)
    {
        return;
    }
    ImGui::SetCurrentContext(MImpl->imguiContext);

    // Typography changes and font-file hot reload both mutate the ImGui atlas,
    // so both are applied before NewFrame.
    const EditorFontSpec ScopedFontSpec = MImpl->MakeFontSpec(MImpl->projectConfig.Editor().uiScale);
    if (!(ScopedFontSpec == MImpl->fontSpec))
    {
        if (MImpl->gpu != nullptr)
        {
            MImpl->gpu->WaitIdle();
        }
        MImpl->fontSpec = ScopedFontSpec;
        MImpl->fonts.SetSpec(ScopedFontSpec);
        const bool ScopedReloaded = MImpl->fonts.ReloadFonts(ImGui::GetIO());
        // The atlas was cleared before the reload; a failed reload leaves no
        // valid ImFont*, so the cached pointers must be refreshed either way.
        MImpl->fontRegular = MImpl->fonts.Regular();
        MImpl->fontMono = MImpl->fonts.Mono();
        if (ScopedReloaded)
        {
            MImpl->visualScript.SetFonts(&MImpl->fonts);
            MImpl->materialGraph.Canvas().SetModelFonts(MImpl->fonts.ScriptNodeTitle(), MImpl->fonts.ScriptPinInput(),
                                                        MImpl->fonts.ScriptPinOutput(), MImpl->fonts.PanelTitle());
            SetEditorFonts(&MImpl->fonts);
            MImpl->PrintConsole("fonts: typography settings reloaded");
        }
        // The UI scale is part of the same settings block, so the panel
        // geometry has to follow the new value in the same frame.
        MImpl->styleUiScale = ScopedFontSpec.uiScale;
        MImpl->ApplyStyleScale();
    }
    if (MImpl->fonts.DetectFontChanges())
    {
        // ImGui::FontAtlas::Clear() destroys the dynamic font texture.
        if (MImpl->gpu != nullptr)
        {
            MImpl->gpu->WaitIdle();
        }
        const bool ScopedReloaded = MImpl->fonts.ReloadFonts(ImGui::GetIO());
        // The atlas was cleared before the reload; a failed reload leaves no
        // valid ImFont*, so the cached pointers must be refreshed either way.
        MImpl->fontRegular = MImpl->fonts.Regular();
        MImpl->fontMono = MImpl->fonts.Mono();
        if (ScopedReloaded)
        {
            MImpl->visualScript.SetFonts(&MImpl->fonts);
            MImpl->materialGraph.Canvas().SetModelFonts(MImpl->fonts.ScriptNodeTitle(), MImpl->fonts.ScriptPinInput(),
                                                        MImpl->fonts.ScriptPinOutput(), MImpl->fonts.PanelTitle());
            SetEditorFonts(&MImpl->fonts);
            MImpl->PrintConsole("fonts: hot-reloaded from the VFS (GPU idle barrier)");
        }
    }

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();

    // Follow the monitor the window is on.
    MImpl->fonts.RefreshDisplayScale(ImGui::GetIO(), MImpl->window);

    // Watcher -> importer -> RHI. Poll() only enqueues the background cook.
    if (MImpl->watcherStarted)
    {
        (void)MImpl->watcher.Poll();
    }
    if (MImpl->importer.IsInitialized())
    {
        if (MImpl->projectConfig.Loaded() &&
            MImpl->importer.GetConfig().textureFormat != MImpl->projectConfig.Project().textureFormat)
        {
            MImpl->importer.SetTextureFormat(MImpl->projectConfig.Project().textureFormat);
            MImpl->watcher.SetDefaultTextureFormat(MImpl->projectConfig.Project().textureFormat);
        }
        std::string ScopedImportError;
        const std::uint32_t ScopedCommitted = MImpl->importer.Tick(&ScopedImportError);
        if (ScopedCommitted > 0)
        {
            if (InContext.audio != nullptr)
            {
                AudioDeviceEnumerator ScopedDevices;
                std::string ScopedDeviceError;
                if (ScopedDevices.Initialize(&ScopedDeviceError))
                {
                    const char* ScopedPreferred = MImpl->projectConfig.Config().project.voiceOutputDevice;
                    const std::int32_t ScopedIndex = ScopedDevices.Resolve(AudioDeviceKind::Output, ScopedPreferred);
                    if (ScopedIndex >= 0)
                    {
                        (void)InContext.audio->ReopenOutput(ScopedDevices, AudioDeviceKind::Output,
                                                            static_cast<std::uint32_t>(ScopedIndex));
                    }
                }
            }

            MImpl->contentBrowser.RequestRefresh();
        }
        if (!ScopedImportError.empty())
        {
            MImpl->PrintConsole("import: " + ScopedImportError);
        }
    }

    // The Editor Settings panel owns the watcher switch. Applying it here keeps
    if (MImpl->projectOpen && MImpl->projectConfig.Loaded() && MImpl->desc.watchAssets)
    {
        const bool ScopedWantWatcher = MImpl->editorSettings.WatchAssetsEnabled();
        if (!ScopedWantWatcher && MImpl->watcherStarted)
        {
            MImpl->watcher.Shutdown();
            MImpl->watcherStarted = false;
        }
        else if (ScopedWantWatcher && !MImpl->watcherStarted)
        {
            std::string ScopedError;
            if (!MImpl->StartProjectSystems(InContext, &ScopedError))
            {
                MImpl->PrintConsole("watcher restart failed: " + ScopedError);
            }
        }
    }

    // Startup dashboard. It blocks the dock layout until a project is chosen;
    MImpl->dashboard.Draw(InContext.window);
    ProjectPaths ScopedOpenedPaths;
    ProjectConfig ScopedOpenedConfig;
    std::string ScopedProjectError;
    const ProjectDashboard::Action ScopedProjectAction =
        MImpl->dashboard.ConsumeResult(&ScopedOpenedPaths, &ScopedOpenedConfig, &ScopedProjectError);
    if (ScopedProjectAction == ProjectDashboard::Action::OpenProject ||
        ScopedProjectAction == ProjectDashboard::Action::NewProject)
    {
        if (!OpenProject(InContext, ScopedOpenedPaths, ScopedOpenedConfig, &ScopedProjectError, true))
        {
            MImpl->SetStatus("project open failed: " + ScopedProjectError);
            MImpl->dashboard.SetVisible(true);
        }
    }
    else if (ScopedProjectAction == ProjectDashboard::Action::Quit)
    {
        MImpl->PrintConsole("dashboard requested quit");
        RequestQuit();
    }
    // A dirty scene asks before it is lost, even when the dashboard is still up.
    if (MImpl->quitPrompt)
    {
        ImGui::OpenPopup("Unsaved scene");
        MImpl->quitPrompt = false;
    }
    if (ImGui::BeginPopupModal("Unsaved scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("The scene has unsaved changes.");
        ImGui::TextUnformatted("Save it before quitting?");
        ImGui::Separator();
        if (ImGui::Button("Save and quit", ImVec2(150.0f, 0.0f)))
        {
            if (MImpl->SaveCurrentScene(InContext, false))
            {
                MImpl->quitRequested = true;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard and quit", ImVec2(150.0f, 0.0f)))
        {
            MImpl->quitRequested = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(90.0f, 0.0f)))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (MImpl->dashboard.Visible())
    {
        ImGui::Render();
        ++MImpl->tickCount;
        return;
    }

    // PIE state machine, once per editor frame, before any panel reads the world.
    MImpl->ProcessPIE(InContext);

    // Ctrl+Z / Ctrl+Y (and Ctrl+Shift+Z) drive the transactional command stack.
    if (!ImGui::GetIO().WantTextInput)
    {
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z))
        {
            MImpl->CommitInteraction();
            MImpl->materialGraph.Canvas().CommitDocumentEdit();
            MImpl->visualScript.Canvas().CommitDocumentEdit();
            MImpl->aiStateTree.CommitEdit();
            MImpl->animationGraph.CommitEdit();
            MImpl->directionalBlendSpace.CommitPendingEdit();
            (void)MImpl->commands.Undo();
            MImpl->aiStateTree.NotifyHistoryChanged();
            MImpl->animationGraph.NotifyHistoryChanged();
            MImpl->directionalBlendSpace.NotifyHistoryChanged();
            MImpl->projectSettings.SyncFromHistory(InContext);
            MImpl->editorSettings.SyncFromHistory(InContext);
        }
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y) ||
            ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z))
        {
            MImpl->CommitInteraction();
            MImpl->materialGraph.Canvas().CommitDocumentEdit();
            MImpl->visualScript.Canvas().CommitDocumentEdit();
            MImpl->aiStateTree.CommitEdit();
            MImpl->animationGraph.CommitEdit();
            MImpl->directionalBlendSpace.CommitPendingEdit();
            (void)MImpl->commands.Redo();
            MImpl->aiStateTree.NotifyHistoryChanged();
            MImpl->animationGraph.NotifyHistoryChanged();
            MImpl->directionalBlendSpace.NotifyHistoryChanged();
            MImpl->projectSettings.SyncFromHistory(InContext);
            MImpl->editorSettings.SyncFromHistory(InContext);
        }
    }
    if (MImpl->interaction.Active() && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
        // Cancel restores the pre-gesture bytes; Escape is the "I did not mean
        (void)MImpl->interaction.Abort();
    }
    MImpl->timelinePanel.Tick(InContext.dt);

    MImpl->Menu(InContext);

    ImGuiViewport* ScopedViewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ScopedViewport->WorkPos);
    ImGui::SetNextWindowSize(ScopedViewport->WorkSize);
    ImGui::SetNextWindowViewport(ScopedViewport->ID);
    // Strict passthrough: no host-window slab
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const ImGuiWindowFlags ScopedHostFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                             ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                             ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                                             ImGuiWindowFlags_NoDocking;
    ImGui::Begin("##DockHost", nullptr, ScopedHostFlags);
    ImGui::PopStyleVar(3);
    const ImGuiID ScopedDockspace = ImGui::GetID("FemboyDockspace");
    MImpl->dockspaceId = ScopedDockspace;
    if (MImpl->needsLayout)
    {
        // Build the node tree before submitting the DockSpace, so the frame
        MImpl->BuildLayout(ScopedDockspace);
        MImpl->needsLayout = false;
    }
    constexpr ImGuiDockNodeFlags ScopedKDockFlags =
        ImGuiDockNodeFlags_PassthruCentralNode | static_cast<ImGuiDockNodeFlags>(ImGuiDockNodeFlags_NoWindowMenuButton);
    ImGui::DockSpace(ScopedDockspace, ImVec2(0.0f, 0.0f), ScopedKDockFlags);
    ImGui::End();

    MImpl->DrawHierarchy(InContext);
    MImpl->contentBrowser.Draw(InContext.registry, MImpl->components, MImpl->commands, InContext.assets,
                               &MImpl->showAssets, &MImpl->status);
    // A scene double-clicked in the browser is opened after the panel is drawn:
    // loading clears the world, so the rest of this frame must not hold onto
    // entities from the previous scene.
    const std::string ScopedOpenScene = MImpl->contentBrowser.TakeOpenRequest();
    if (!ScopedOpenScene.empty() && MImpl->projectOpen)
    {
        MImpl->CommitInteraction();
        std::string ScopedOpenError;
        const std::filesystem::path ScopedOpenPath = MImpl->projectConfig.Paths().assets / ScopedOpenScene;
        if (LoadScene(InContext, ScopedOpenPath, &ScopedOpenError))
        {
            MImpl->desc.scenePath = ScopedOpenPath;
            MImpl->SetStatus("opened scene " + ScopedOpenScene);
        }
        else
        {
            MImpl->SetStatus("open failed: " + ScopedOpenError);
        }
    }
    MImpl->DrawInspector(InContext);
    ImGui::SetNextWindowDockID(MImpl->DockHostFor("Material Graph"), ImGuiCond_FirstUseEver);
    MImpl->timelinePanel.Draw();
    MImpl->materialGraph.Draw("Material Graph", &MImpl->showMaterialGraph);
    MImpl->visualScript.Draw("VisualScript", &MImpl->showVisualScript);
    if (MImpl->showScriptDebugger)
    {
        ImGui::SetNextWindowDockID(MImpl->DockHostFor("Material Graph"), ImGuiCond_FirstUseEver);
        MImpl->scriptDebugger.Draw(InContext.registry, MImpl->components, SelectedEntity(InContext.registry),
                                   &MImpl->showScriptDebugger, &MImpl->visualScript);
        // Graph programs pause on a node span; mark that node on the canvas.
        MImpl->visualScript.SetDebugHighlight(MImpl->scriptDebugger.CurrentGraphNode());
    }
    if (MImpl->showTilemap)
    {
        ImGui::SetNextWindowDockID(MImpl->DockHostFor("Material Graph"), ImGuiCond_FirstUseEver);
        MImpl->tilemap.Draw(InContext.registry, SelectedEntity(InContext.registry), &MImpl->showTilemap);
    }
    if (MImpl->showVirtualTexture)
    {
        MImpl->virtualTexture.Draw(MImpl->virtualTextureSource, &MImpl->showVirtualTexture);
    }
    MImpl->SyncCanvasWorlds(InContext);
    MImpl->aiStateTree.Draw("AI State Tree", &MImpl->showAIStateTree);
    MImpl->animationGraph.Draw("Animation Graph", &MImpl->showAnimationGraph);
    MImpl->directionalBlendSpace.Draw("Directional Blend Space", &MImpl->showDirectionalBlendSpace);
    MImpl->DrawViewport(InContext);
    MImpl->DrawPieClientViewport();
    if (MImpl->showLevelSettings)
    {
        MImpl->levelSettings.Draw(InContext, &MImpl->showLevelSettings);
    }
    if (MImpl->showProjectSettings)
    {
        MImpl->projectSettings.Draw(InContext, &MImpl->showProjectSettings);
    }
    if (MImpl->showEditorSettings)
    {
        MImpl->editorSettings.Draw(InContext, &MImpl->showEditorSettings);
    }
    if (MImpl->showPluginManager)
    {
        if (BeginEditorPanel("Plugin Manager", &MImpl->showPluginManager))
        {
            ImGui::TextWrapped("Plugins belong to the open project. Paths are relative to the project root; "
                               "the editor never loads a project plugin into another project.");
            ImGui::SeparatorText("Project plugins");
            Fby::Editor::ProjectSettings& ScopedProjectSettings = MImpl->projectConfig.Config().project;
            const bool ScopedProjectOpen = MImpl->projectConfig.Loaded() && !MImpl->projectConfig.Paths().root.empty();
            ImGui::BeginDisabled(!ScopedProjectOpen);
            ImGui::SetNextItemWidth(420.0f);
            ImGui::InputText("##plugin_path", MImpl->pluginPath.data(), MImpl->pluginPath.size());
            ImGui::SameLine();
            if (ImGui::Button("Add & load") && MImpl->pluginHost != nullptr && ScopedProjectOpen)
            {
                const std::string ScopedText(MImpl->pluginPath.data());
                const std::filesystem::path ScopedRelative(ScopedText);
                std::error_code ScopedErrorCode;
                const std::filesystem::path ScopedRoot =
                    std::filesystem::weakly_canonical(MImpl->projectConfig.Paths().root, ScopedErrorCode);
                const std::filesystem::path ScopedResolved =
                    std::filesystem::weakly_canonical(ScopedRoot / ScopedRelative, ScopedErrorCode);
                const std::filesystem::path ScopedRelativeToRoot =
                    std::filesystem::relative(ScopedResolved, ScopedRoot, ScopedErrorCode);
                const bool ScopedUnderProject = !ScopedRelative.is_absolute() &&
                                                ScopedText.find("..") == std::string::npos && !ScopedErrorCode &&
                                                !ScopedRelativeToRoot.empty() && *ScopedRelativeToRoot.begin() != "..";
                const bool ScopedKnownSuffix = ScopedRelative.extension() == ".dll" ||
                                               ScopedRelative.extension() == ".so" ||
                                               ScopedRelative.extension() == ".dylib";
                if (!ScopedUnderProject || !ScopedKnownSuffix)
                {
                    MImpl->pluginStatus = "rejected: path must be a plugin inside the project root";
                }
                else if (ScopedProjectSettings.pluginCount >= Fby::Editor::ProjectSettings::kMaxPlugins)
                {
                    MImpl->pluginStatus = "rejected: project plugin list is full";
                }
                else
                {
                    std::string ScopedLoadError;
                    if (!MImpl->pluginHost->Load(ScopedResolved.string(), &ScopedLoadError))
                    {
                        MImpl->pluginStatus = "load failed: " + ScopedLoadError;
                    }
                    else
                    {
                        Fby::Editor::ProjectPluginEntry& ScopedEntry =
                            ScopedProjectSettings.plugins[ScopedProjectSettings.pluginCount++];
                        std::snprintf(ScopedEntry.Path, sizeof(ScopedEntry.Path), "%s",
                                      ScopedRelative.generic_string().c_str());
                        ScopedEntry.Enabled = true;
                        std::string ScopedPersistError;
                        (void)MImpl->projectConfig.PersistIfChanged(&ScopedPersistError);
                        MImpl->pluginStatus = "loaded and saved to project: " + ScopedRelative.generic_string();
                    }
                }
            }
            for (std::uint32_t ScopedI = 0; ScopedI < ScopedProjectSettings.pluginCount; ++ScopedI)
            {
                Fby::Editor::ProjectPluginEntry& ScopedEntry = ScopedProjectSettings.plugins[ScopedI];
                ImGui::PushID(static_cast<int>(ScopedI));
                ImGui::TextUnformatted(ScopedEntry.Path);
                ImGui::SameLine();
                if (ImGui::SmallButton("Load") && MImpl->pluginHost != nullptr)
                {
                    std::string ScopedLoadError;
                    const std::string ScopedFull = (MImpl->projectConfig.Paths().root / ScopedEntry.Path).string();
                    MImpl->pluginStatus = MImpl->pluginHost->Load(ScopedFull, &ScopedLoadError)
                                              ? "loaded " + std::string(ScopedEntry.Path)
                                              : "load failed: " + ScopedLoadError;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Forget"))
                {
                    for (std::uint32_t ScopedMove = ScopedI + 1; ScopedMove < ScopedProjectSettings.pluginCount;
                         ++ScopedMove)
                    {
                        ScopedProjectSettings.plugins[ScopedMove - 1] = ScopedProjectSettings.plugins[ScopedMove];
                    }
                    --ScopedProjectSettings.pluginCount;
                    std::string ScopedPersistError;
                    (void)MImpl->projectConfig.PersistIfChanged(&ScopedPersistError);
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }
            ImGui::EndDisabled();
            ImGui::SeparatorText("Loaded in this project");
            if (MImpl->pluginHost == nullptr)
            {
                ImGui::TextDisabled("No plugin host bound.");
            }
            else
            {
                for (const auto& ScopedPlugin : MImpl->pluginHost->Plugins())
                {
                    ImGui::PushID(ScopedPlugin.get());
                    ImGui::Text("%s %s", ScopedPlugin->name.c_str(), ScopedPlugin->version.c_str());
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Reload"))
                    {
                        std::string ScopedError;
                        MImpl->pluginStatus = MImpl->pluginHost->Reload(ScopedPlugin->path, &ScopedError)
                                                  ? "reloaded " + ScopedPlugin->name
                                                  : "reload failed: " + ScopedError;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Unload"))
                    {
                        std::string ScopedError;
                        MImpl->pluginStatus = MImpl->pluginHost->Unload(ScopedPlugin->path, &ScopedError)
                                                  ? "unloaded " + ScopedPlugin->name
                                                  : "unload failed: " + ScopedError;
                    }
                    ImGui::PopID();
                }
            }
            if (MImpl->effectRegistry != nullptr)
            {
                ImGui::SeparatorText("Project effect");
                const Ui::AccessibilityEffectProfile* ScopedActiveEffect = MImpl->effectRegistry->Active();
                const char* ScopedPreview = ScopedActiveEffect != nullptr ? ScopedActiveEffect->DisplayName : "None";
                if (ImGui::BeginCombo("##active_effect", ScopedPreview))
                {
                    for (const Ui::AccessibilityEffectProfile& ScopedEffect : MImpl->effectRegistry->Effects())
                    {
                        const bool ScopedSelected = ScopedActiveEffect != nullptr &&
                                                    std::string_view(ScopedActiveEffect->Id) == ScopedEffect.Id;
                        if (ImGui::Selectable(ScopedEffect.DisplayName, ScopedSelected))
                        {
                            (void)MImpl->effectRegistry->SetActive(ScopedEffect.Id);
                            std::snprintf(ScopedProjectSettings.activeEffect,
                                          sizeof(ScopedProjectSettings.activeEffect), "%s", ScopedEffect.Id);
                            std::string ScopedPersistError;
                            (void)MImpl->projectConfig.PersistIfChanged(&ScopedPersistError);
                        }
                    }
                    ImGui::EndCombo();
                }
            }
            if (!MImpl->pluginStatus.empty())
            {
                ImGui::Separator();
                ImGui::TextWrapped("%s", MImpl->pluginStatus.c_str());
            }
            EndEditorPanel();
        }
    }
    if (MImpl->showUserSettings)
    {
        ImGui::SetNextWindowDockID(MImpl->DockHostFor("Inspector"), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Player Settings", &MImpl->showUserSettings))
        {
            if (UserSettingsPanel::Draw(MImpl->userSettings, &MImpl->backendCaps))
            {
                std::string ScopedError;
                if (!SaveUserSettings("Saved/Config/UserSettings.toml", MImpl->userSettings, &ScopedError))
                {
                    LogWarn("editor", "user settings save failed: {}", ScopedError);
                }
            }
        }
        ImGui::End();
    }
    if (MImpl->showLicenses)
    {
        ImGui::SetNextWindowDockID(MImpl->DockHostFor("Inspector"), ImGuiCond_FirstUseEver);
        MImpl->licenses.Draw(&MImpl->showLicenses);
    }
    if (MImpl->showNetworkDiagnostics)
    {
        ImGui::SetNextWindowDockID(MImpl->DockHostFor("Inspector"), ImGuiCond_FirstUseEver);
        MImpl->networkDiagnostics.Draw(&MImpl->showNetworkDiagnostics);
    }
    if (MImpl->showVoiceDevices)
    {
        ImGui::SetNextWindowDockID(MImpl->DockHostFor("Inspector"), ImGuiCond_FirstUseEver);
        MImpl->voiceDevices.Draw(&MImpl->showVoiceDevices, &MImpl->projectConfig, InContext.audio,
                                 MImpl->voiceRuntime.get(), &MImpl->voiceSession, &MImpl->voicePresets,
                                 &MImpl->voiceSelection, &MImpl->voicePolicy, &MImpl->voiceSfxSettings);
    }
    if (MImpl->showParticleFx)
    {
        ImGui::SetNextWindowDockID(MImpl->DockHostFor("Inspector"), ImGuiCond_FirstUseEver);
        MImpl->particleFx.Draw(&MImpl->showParticleFx);
    }
    if (MImpl->showTransportGroups)
    {
        ImGui::SetNextWindowDockID(MImpl->DockHostFor("Inspector"), ImGuiCond_FirstUseEver);
        MImpl->transportGroups.Draw(&MImpl->showTransportGroups, InContext.registry,
                                    SelectedEntity(InContext.registry));
    }
    MImpl->DrawProfiler(InContext);
    MImpl->DrawSdfComposer();
    MImpl->logBoy.SetFrame(static_cast<std::uint32_t>(InContext.frameIndex));
    if (MImpl->showLogBoy)
    {
        ImGui::SetNextWindowDockID(MImpl->DockHostFor("Console"), ImGuiCond_FirstUseEver);
        MImpl->logBoy.Draw(&MImpl->showLogBoy);
    }
    if (MImpl->showSceneDiagnostics)
    {
        MImpl->releaseCooker.Poll();
        MImpl->sceneDiagnostics.SetReleaseProgress(MImpl->releaseCooker.Progress());
        ImGui::SetNextWindowDockID(MImpl->DockHostFor("Stats"), ImGuiCond_FirstUseEver);
        MImpl->sceneDiagnostics.Draw(InContext, &MImpl->showSceneDiagnostics);
    }
    if (MImpl->showSdfCharacters)
    {
        ImGui::SetNextWindowDockID(MImpl->DockHostFor("SDF Character"), ImGuiCond_FirstUseEver);
        MImpl->sdfCharacters.Draw(InContext, &MImpl->showSdfCharacters);
    }
    MImpl->DrawConsole();
    MImpl->DrawStats(InContext);
    if (MImpl->showTextPreview)
    {
        MImpl->textPreview.Draw(MImpl->vfs, &MImpl->showTextPreview);
    }
    if (MImpl->showDemo)
    {
        ImGui::ShowDemoWindow(&MImpl->showDemo);
    }

    // Safety net for focus loss, window changes or a selection swap: a gesture
    if (MImpl->interaction.Active() && !ImGui::IsAnyItemActive() &&
        !(MImpl->interaction.GetSource() == InteractionTransaction::Source::Gizmo && ImGuizmo::IsUsing()))
    {
        MImpl->CommitInteraction();
    }

    ImGui::Render();
    ++MImpl->tickCount;
}

void EditorSystem::OnPlatformEvent(const void* InSdlEvent)
{
    if (!MImpl->initialized || InSdlEvent == nullptr)
    {
        return;
    }
    ImGui::SetCurrentContext(MImpl->imguiContext);
    const SDL_Event* ScopedEvent = static_cast<const SDL_Event*>(InSdlEvent);
    // A window can move between monitors or its content scale can change.
    if (ScopedEvent->type == SDL_EVENT_WINDOW_DISPLAY_CHANGED ||
        ScopedEvent->type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED)
    {
        // Both calls compare the new display, pixel density and content scale
        // against the last applied values, so an event that does not change the
        // effective scale is a no-op.
        MImpl->ApplyStyleScale();
        MImpl->fonts.RefreshDisplayScale(ImGui::GetIO(), MImpl->window);
    }
    ImGui_ImplSDL3_ProcessEvent(ScopedEvent);
}

bool EditorSystem::WantsMouse() const noexcept
{
    if (!MImpl->initialized)
    {
        return false;
    }
    ImGui::SetCurrentContext(MImpl->imguiContext);
    return ImGui::GetIO().WantCaptureMouse;
}

bool EditorSystem::WantsKeyboard() const noexcept
{
    if (!MImpl->initialized)
    {
        return false;
    }
    ImGui::SetCurrentContext(MImpl->imguiContext);
    return ImGui::GetIO().WantCaptureKeyboard;
}

bool EditorSystem::IsVisible() const noexcept
{
    return MImpl->visible;
}

void EditorSystem::SetVisible(bool InVisible)
{
    MImpl->visible = InVisible;
}

void EditorSystem::SetProfiler(Core::Profiler* InProfiler) noexcept
{
    MImpl->profiler = InProfiler;
}

void EditorSystem::SetEffectRegistry(Ui::AccessibilityEffectRegistry* InEffects) noexcept
{
    MImpl->effectRegistry = InEffects;
    MImpl->accessibility.SetEffectRegistry(InEffects);
    if (InEffects != nullptr && MImpl->projectConfig.Loaded())
    {
        const char* ScopedActive = MImpl->projectConfig.Project().activeEffect;
        if (ScopedActive[0] != '\0')
        {
            (void)InEffects->SetActive(ScopedActive);
        }
    }
}

void EditorSystem::SetPluginHost(Plugin::PluginHost* InHost) noexcept
{
    MImpl->pluginHost = InHost;
    if (MImpl->initialized && MImpl->projectConfig.Loaded())
    {
        try
        {
            SyncProjectPlugins();
        }
        catch (...)
        {
            LogError("editor", "project plugin synchronization failed");
        }
    }
}

void EditorSystem::SyncProjectPlugins()
{
    if (MImpl->pluginHost == nullptr || !MImpl->projectConfig.Loaded())
    {
        return;
    }
    MImpl->pluginHost->UnloadAll();
    const Fby::Editor::ProjectSettings& ScopedPluginProject = MImpl->projectConfig.Project();
    const std::filesystem::path& ScopedRoot = MImpl->projectConfig.Paths().root;
    for (std::uint32_t ScopedI = 0; ScopedI < ScopedPluginProject.pluginCount; ++ScopedI)
    {
        const Fby::Editor::ProjectPluginEntry& ScopedEntry = ScopedPluginProject.plugins[ScopedI];
        if (!ScopedEntry.Enabled || ScopedEntry.Path[0] == '\0')
        {
            continue;
        }
        std::string ScopedPluginError;
        const std::filesystem::path ScopedPluginPath = ScopedRoot / ScopedEntry.Path;
        if (!MImpl->pluginHost->Load(ScopedPluginPath.string(), &ScopedPluginError))
        {
            LogWarn("editor", "project plugin '{}' not loaded: {}", ScopedEntry.Path, ScopedPluginError);
        }
    }
    if (MImpl->effectRegistry != nullptr)
    {
        const char* ScopedActive = ScopedPluginProject.activeEffect;
        if (ScopedActive[0] != '\0')
        {
            (void)MImpl->effectRegistry->SetActive(ScopedActive);
        }
    }
}

Rhi::RenderTargetHandle EditorSystem::ViewportTarget() const noexcept
{
    return MImpl->target;
}

Rhi::RenderTargetHandle EditorSystem::ViewportDisplayTarget() const noexcept
{
    return MImpl->displayTarget.IsValid() ? MImpl->displayTarget : MImpl->target;
}

Rhi::RenderTargetHandle EditorSystem::ViewportHudTarget() const noexcept
{
    return MImpl->hudTarget;
}

Rhi::RenderTargetHandle EditorSystem::ViewportMotionTarget() const noexcept
{
    return MImpl->motionTarget;
}

Rhi::RenderTargetHandle EditorSystem::ViewportFsr1Intermediate() const noexcept
{
    return MImpl->fsr1Intermediate;
}

bool EditorSystem::ViewportUpscaleActive() const noexcept
{
    return MImpl->displayTarget.IsValid();
}

Rhi::Extent2D EditorSystem::ViewportSize() const noexcept
{
    return MImpl->displaySize.width > 0 ? MImpl->displaySize : MImpl->targetSize;
}

Rhi::Extent2D EditorSystem::ViewportRenderSize() const noexcept
{
    return MImpl->targetSize;
}

EditorCamera& EditorSystem::Camera() noexcept
{
    return MImpl->camera;
}

const EditorCamera& EditorSystem::Camera() const noexcept
{
    return MImpl->camera;
}

Scene::ComponentRegistry& EditorSystem::Components() noexcept
{
    return MImpl->components;
}

AnimationTimelinePanel& EditorSystem::TimelinePanel() noexcept
{
    return MImpl->timelinePanel;
}

CommandStack& EditorSystem::Commands() noexcept
{
    return MImpl->commands;
}

ContentBrowserPanel& EditorSystem::ContentBrowser() noexcept
{
    return MImpl->contentBrowser;
}

MaterialGraphPanel& EditorSystem::MaterialGraph() noexcept
{
    return MImpl->materialGraph;
}

VisualScriptPanel& EditorSystem::VisualScript() noexcept
{
    return MImpl->visualScript;
}

AIStateTreeCanvasPanel& EditorSystem::AIStateTree() noexcept
{
    return MImpl->aiStateTree;
}

AnimationGraphCanvasPanel& EditorSystem::AnimationGraph() noexcept
{
    return MImpl->animationGraph;
}

void EditorSystem::BindAIPlayInEditor(const Ecs::Registry* InPlayWorld, bool InActive) noexcept
{
    MImpl->aiPlayWorld = InPlayWorld;
    MImpl->aiPlayActive = InActive;
}

Ecs::Registry* EditorSystem::ActiveWorld() noexcept
{
    return MImpl->pie.Active() ? &MImpl->pie.World() : nullptr;
}

const Ecs::Registry* EditorSystem::ActiveWorld() const noexcept
{
    return MImpl->pie.Active() ? &MImpl->pie.World() : nullptr;
}

Rhi::RenderTargetHandle EditorSystem::PieClientTarget() const noexcept
{
    return MImpl->pieClientViewport.Target();
}

Rhi::RenderTargetHandle EditorSystem::PieClientDisplayTarget() const noexcept
{
    return MImpl->pieDisplayTarget;
}

Rhi::RenderTargetHandle EditorSystem::PieClientFsr1Intermediate() const noexcept
{
    return MImpl->pieFsr1Intermediate;
}

Rhi::Extent2D EditorSystem::PieClientSize() const noexcept
{
    return MImpl->displaySize.width > 0 ? MImpl->displaySize : MImpl->pieClientViewport.Size();
}

Rhi::Extent2D EditorSystem::PieClientRenderSize() const noexcept
{
    return MImpl->pieClientViewport.Size();
}

EditorCamera& EditorSystem::PieClientCamera() noexcept
{
    return MImpl->pieClientViewport.Camera();
}

Ecs::Registry* EditorSystem::PieClientWorld() noexcept
{
    return MImpl->pie.Multiplayer() ? &MImpl->pie.ClientWorld() : nullptr;
}

bool EditorSystem::IsPlaying() const noexcept
{
    return MImpl->pie.Active();
}

bool EditorSystem::IsPaused() const noexcept
{
    return MImpl->pie.Active() && MImpl->piePaused;
}

bool EditorSystem::PieHudAnimationsPaused() const noexcept
{
    return MImpl->pie.Active() && MImpl->piePaused && MImpl->piePauseHud;
}

ProjectDashboard& EditorSystem::Dashboard() noexcept
{
    return MImpl->dashboard;
}

ProjectConfigStore& EditorSystem::Config() noexcept
{
    return MImpl->projectConfig;
}

ProjectSettingsPanel& EditorSystem::ProjectSettings() noexcept
{
    return MImpl->projectSettings;
}

EditorSettingsPanel& EditorSystem::EditorSettings() noexcept
{
    return MImpl->editorSettings;
}

UserSettings& EditorSystem::UserConfig() noexcept
{
    return MImpl->userSettings;
}

const UserSettings& EditorSystem::UserConfig() const noexcept
{
    return MImpl->userSettings;
}

AssetImportService& EditorSystem::Importer() noexcept
{
    return MImpl->importer;
}

AssetDatabaseWatchSystem& EditorSystem::Watcher() noexcept
{
    return MImpl->watcher;
}

bool EditorSystem::ProjectOpen() const noexcept
{
    return MImpl->projectOpen;
}

bool EditorSystem::ShouldQuit() const noexcept
{
    return MImpl->quitRequested;
}

void EditorSystem::RequestQuit()
{
    if (MImpl == nullptr)
    {
        return;
    }
    if (MImpl->projectOpen && MImpl->sceneDirty && !MImpl->desc.scenePath.empty())
    {
        MImpl->quitPrompt = true;
        return;
    }
    MImpl->quitRequested = true;
}

bool EditorSystem::OpenProject(Ecs::SystemContext& InContext, const ProjectPaths& InPaths,
                               const ProjectConfig& InConfig, std::string* InError, bool InLoadDefaultScene)
{
    (void)InConfig;
    if (!MImpl->initialized)
    {
        if (InError != nullptr)
        {
            *InError = "the editor is not initialized";
        }
        return false;
    }
    if (!InPaths.IsValid())
    {
        if (InError != nullptr)
        {
            *InError = "invalid project paths";
        }
        return false;
    }
    if (!MImpl->projectConfig.SetProject(InPaths))
    {
        if (InError != nullptr)
        {
            *InError = "cannot adopt the project paths";
        }
        return false;
    }
    std::string ScopedLoadError;
    if (!MImpl->projectConfig.Load(&ScopedLoadError))
    {
        if (InError != nullptr)
        {
            *InError = ScopedLoadError;
        }
        return false;
    }

    SyncProjectPlugins();
    MImpl->desc.projectRoot = InPaths.root;
    MImpl->desc.showProjectDashboard = false;
    MImpl->projectConfig.Project().gameplayTags.ApplyToRegistry(MImpl->gameplayTags);
    MImpl->dashboard.Initialize(InPaths, MImpl->projectConfig.Config(), false);
    MImpl->contentBrowser.SetProjectRoot(InPaths.assets);
    MImpl->contentBrowser.Initialize(InContext.assetDb, ContentBrowserPanel::Config{InPaths.assets}, &MImpl->importer);

    // Mount the project's assets folder at a higher priority than the engine's
    if (InContext.assets != nullptr)
    {
        std::string ScopedMountError;
        if (!InContext.assets->MountDirectory(InPaths.assets.string(), 20, &ScopedMountError))
        {
            LogWarn("editor", "project assets not mounted: {}", ScopedMountError);
        }
    }
    if (InContext.assetDb != nullptr)
    {
        std::string ScopedMetaError;
        if (!InContext.assetDb->LoadMetaDirectory(InPaths.assets.string(), &ScopedMetaError))
        {
            LogWarn("editor", "some .femeta rows could not be loaded: {}", ScopedMetaError);
        }
    }

    std::string ScopedSystemsError;
    if (!MImpl->StartProjectSystems(InContext, &ScopedSystemsError))
    {
        LogWarn("editor", "project background systems not started: {}", ScopedSystemsError);
    }
    MImpl->projectOpen = true;
    MImpl->projectSettings.Apply(InContext);
    // EditorSettingsPanel::Apply pushes the loaded log limits into the sink.
    MImpl->editorSettings.Apply(InContext);

    MImpl->commands.ClearHistory();
    MImpl->ResetDirtyBaseline();
    if (InLoadDefaultScene)
    {
        MImpl->currentScene.clear();
        const std::string_view ScopedDefaultScene = ProjectString(MImpl->projectConfig.Project().defaultScene,
                                                                  sizeof(MImpl->projectConfig.Project().defaultScene));
        if (!ScopedDefaultScene.empty())
        {
            const std::filesystem::path ScopedScenePath =
                InPaths.assets / std::filesystem::path(std::string(ScopedDefaultScene));
            std::error_code ScopedCode;
            if (std::filesystem::is_regular_file(ScopedScenePath, ScopedCode))
            {
                std::string ScopedSceneError;
                if (LoadScene(InContext, ScopedScenePath, &ScopedSceneError))
                {
                    MImpl->desc.scenePath = ScopedScenePath;
                }
                else
                {
                    LogWarn("editor", "default scene '{}' was not loaded: {}", ScopedScenePath.string(),
                            ScopedSceneError);
                }
            }
            else
            {
                LogWarn("editor", "default scene '{}' is missing", ScopedScenePath.string());
            }
        }
    }

    MImpl->contentBrowser.RequestRefresh();
    MImpl->PrintConsole(std::format("project: {} (fixed {} Hz, {} assets root)", InPaths.root.string(),
                                    MImpl->projectConfig.Project().fixedUpdateHz, InPaths.assets.string()));
    MImpl->PrintConsole(std::format("settings: {} / watcher {} / importer {}", InPaths.projectFile.string(),
                                    MImpl->watcherStarted ? "on" : "off",
                                    MImpl->importer.IsInitialized() ? "on" : "off"));
    return true;
}

void EditorSystem::PrintLine(std::string_view InLine)
{
    if (MImpl != nullptr)
    {
        MImpl->PrintConsole(std::string(InLine));
    }
}

void EditorSystem::ResolveTilemapRefs(Ecs::Registry& InRegistry, VirtualFileSystem* InVfs)
{
    if (InVfs == nullptr)
    {
        return;
    }
    // Structural changes are batched: collect first, then add the runtime grids.
    std::vector<Entity> ScopedEntities;
    {
        auto ScopedView = InRegistry.view<TilemapAssetRefComponent>();
        for (auto [ScopedEntity, ScopedRef] : ScopedView.each())
        {
            (void)ScopedRef;
            ScopedEntities.push_back(ScopedEntity);
        }
    }
    for (const Entity ScopedEntity : ScopedEntities)
    {
        const auto* ScopedRef = InRegistry.try_get<TilemapAssetRefComponent>(ScopedEntity);
        if (ScopedRef == nullptr || ScopedRef->Path[0] == '\0')
        {
            continue;
        }
        TilemapComponent ScopedMap;
        std::string ScopedError;
        if (!Tilemap::LoadFromVfs(*InVfs, ScopedRef->Path, &ScopedMap, &ScopedError))
        {
            LogWarn("editor", "tilemap ref '{}': {}", ScopedRef->Path, ScopedError);
            continue;
        }
        ScopedMap.SortLayer = static_cast<std::uint16_t>(std::min(ScopedRef->SortLayer, 65535u));
        InRegistry.emplace_or_replace<TilemapComponent>(ScopedEntity, ScopedMap);
    }
}

void EditorSystem::SetNetworkSession(Net::NetSession* InSession) noexcept
{
    MImpl->networkDiagnostics.SetSession(InSession);
}

void EditorSystem::SetVirtualTextureSource(Rhi::VirtualTextureSystem* InSystem) noexcept
{
    MImpl->virtualTextureSource = InSystem;
    if (InSystem != nullptr)
    {
        MImpl->showVirtualTexture = true;
    }
}

void EditorSystem::SetVirtualTextureDebugMode(std::uint32_t InMode) noexcept
{
    MImpl->virtualTexture.SetDebugMode(InMode);
}

std::uint32_t EditorSystem::VirtualTextureDebugMode() const noexcept
{
    return MImpl->virtualTexture.DebugMode();
}

const VirtualTextureSettings& EditorSystem::VirtualTextureConfig() const noexcept
{
    return MImpl->projectSettings.Values().virtualTexture;
}

bool EditorSystem::LoadScene(Ecs::SystemContext& InContext, const std::filesystem::path& InPath, std::string* InError)
{
    Scene::SceneLoadOptions ScopedOptions;
    ScopedOptions.clearExisting = true;
    // SkeletonAssetRef resolves through the same VFS the editor already uses
    ScopedOptions.assets = InContext.assets != nullptr ? InContext.assets : MImpl->vfs;
    ScopedOptions.assetDb = InContext.assetDb;
    const auto ScopedResult = Scene::LoadSceneFileToml(InContext.registry, MImpl->components, InPath, ScopedOptions);
    if (!ScopedResult)
    {
        if (InError != nullptr)
        {
            *InError = ScopedResult.error();
        }
        return false;
    }
    // Loading replaces every entity in the world. Commands recorded against the
    // previous scene would target reused ids or stale entities, so the history
    // must not survive the load.
    MImpl->commands.CancelTransaction();
    MImpl->commands.ClearHistory();
    MImpl->currentScene = InPath;
    MImpl->aiStateTree.SetScenePath(InPath);
    (void)MImpl->aiStateTree.LoadCompiledFromSceneToml();
    MImpl->animationGraph.SetScenePath(InPath);
    (void)MImpl->animationGraph.LoadFromSceneToml();
    ResolveTilemapRefs(InContext.registry, InContext.assets != nullptr ? InContext.assets : MImpl->vfs);

    const auto ScopedCameras = InContext.registry.view<Camera2D>();
    const auto ScopedIt = ScopedCameras.begin();
    if (ScopedIt != ScopedCameras.end())
    {
        const Camera2D& ScopedCamera = InContext.registry.get<Camera2D>(*ScopedIt);
        MImpl->camera.center = ScopedCamera.Center;
        MImpl->camera.zoom = ScopedCamera.Zoom > 0.0f ? ScopedCamera.Zoom : 1.0f;
    }
    MImpl->frameMs.clear();
    MImpl->ResetDirtyBaseline();
    LogInfo("editor", "loaded scene '{}' ({} entities)", InPath.string(), ScopedResult->Size());
    return true;
}

bool EditorSystem::SaveScene(Ecs::SystemContext& InContext, const std::filesystem::path& InPath, std::string* InError)
{
    Scene::SceneMeta ScopedMeta;
    ScopedMeta.name = InPath.stem().string();
    ScopedMeta.author = "FemboyEngine2D editor";
    if (!Scene::SaveSceneFileToml(InPath, InContext.registry, MImpl->components, ScopedMeta, InError))
    {
        return false;
    }
    MImpl->currentScene = InPath;
    MImpl->aiStateTree.SetScenePath(InPath);
    if (MImpl->aiStateTree.HasCompiledImage())
    {
        // SaveSceneFileToml rewrites the whole file, so the marked AI block must be merged back first.
        (void)MImpl->aiStateTree.SaveCompiledToSceneToml();
    }
    MImpl->animationGraph.SetScenePath(InPath);
    if (MImpl->animationGraph.HasCompiledImage())
    {
        (void)MImpl->animationGraph.SaveToSceneToml();
    }
    return true;
}

const std::filesystem::path& EditorSystem::CurrentScenePath() const noexcept
{
    return MImpl->currentScene;
}
}
