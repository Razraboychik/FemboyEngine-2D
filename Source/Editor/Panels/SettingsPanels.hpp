// FemboyEngine2D - Project and Editor Settings panels.
#pragma once

#include "Editor/Infrastructure/EditTransactions.hpp"
#include "Editor/Infrastructure/ProjectConfig.hpp"
#include "Engine/RHI/BackendCapabilities.hpp"
#include "Engine/RHI/RHI.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string>

namespace Fby
{
namespace Ui
{
struct WidgetCanvas;
}
}

namespace Fby::Ecs
{
struct SystemContext;
}

namespace Fby::Editor
{

class CommandStack;
class ProjectConfigStore;

class ProjectSettingsPanel
{
public:
    ProjectSettingsPanel() = default;

    void Initialize(CommandStack& InCommands, ProjectConfigStore& InStore, std::uint16_t InCodec) noexcept;
    void SetUpscalerCaps(const Rhi::UpscalerCaps& InCaps) noexcept
    {
        MUpscalerCaps = InCaps;
    }
    void SetBackendCapabilities(const Rhi::BackendCapabilities& InCaps) noexcept
    {
        MBackendCaps = InCaps;
    }
    // Draws the panel and applies/persists after a commit.
    void Draw(Ecs::SystemContext& InContext, bool* InOpen);
    // Pushes the current POD to AudioSystem/AccessibilityKernel; called after a
    void Apply(Ecs::SystemContext& InContext);
    // EditorSystem calls this after Ctrl+Z/Ctrl+Y. It re-applies and persists
    void SyncFromHistory(Ecs::SystemContext& InContext);

    // The OLED shifter is a per-canvas policy in Pink UI (a fullscreen video
    static void ApplyOledShift(std::span<Ui::WidgetCanvas> InCanvases, bool InEnabled) noexcept;

    [[nodiscard]] bool Dirty() const noexcept
    {
        return MDirty;
    }
    [[nodiscard]] std::uint16_t Codec() const noexcept
    {
        return MCodec;
    }
    [[nodiscard]] ProjectSettings& Values() noexcept;
    [[nodiscard]] const ProjectSettings& Values() const noexcept;

private:
    void CommitIfFinished(Ecs::SystemContext& InContext);
    void BeginOrRecord(Ecs::SystemContext& InContext,
                       const std::array<std::byte, sizeof(ProjectSettings)>& InBefore) noexcept;

    CommandStack* MCommands = nullptr;
    ProjectConfigStore* MStore = nullptr;
    std::uint16_t MCodec = 0xFFFFu;
    InteractionTransaction MTransaction{};
    ProjectSettings MApplied{};
    Rhi::UpscalerCaps MUpscalerCaps{};
    Rhi::BackendCapabilities MBackendCaps{};
    // Gameplay tag editor state. The database itself is inside ProjectSettings,
    // so tag edits share the same undo transaction as the rest of the project.
    std::array<char, 64> MNewTagPath{};
    std::array<char, 64> MRenameTagPath{};
    std::array<char, 64> MTagSearch{};
    int MSelectedTag = -1;
    int MLastSelectedTag = -1;
    bool MAppliedValid = false;
    bool MDirty = false;
};

class EditorSettingsPanel
{
public:
    EditorSettingsPanel() = default;

    void Initialize(CommandStack& InCommands, ProjectConfigStore& InStore, std::uint16_t InCodec) noexcept;
    void Draw(Ecs::SystemContext& InContext, bool* InOpen);
    void Apply(Ecs::SystemContext& InContext);
    void SyncFromHistory(Ecs::SystemContext& InContext);

    [[nodiscard]] bool Dirty() const noexcept
    {
        return MDirty;
    }
    [[nodiscard]] bool WatchAssetsEnabled() const noexcept;
    [[nodiscard]] std::uint16_t Codec() const noexcept
    {
        return MCodec;
    }
    [[nodiscard]] EditorSettings& Values() noexcept;
    [[nodiscard]] const EditorSettings& Values() const noexcept;

private:
    void CommitIfFinished(Ecs::SystemContext& InContext);
    void BeginOrRecord(Ecs::SystemContext& InContext,
                       const std::array<std::byte, sizeof(EditorSettings)>& InBefore) noexcept;

    CommandStack* MCommands = nullptr;
    ProjectConfigStore* MStore = nullptr;
    std::uint16_t MCodec = 0xFFFFu;
    InteractionTransaction MTransaction{};
    EditorSettings MApplied{};
    bool MAppliedValid = false;
    bool MDirty = false;
};
}
