// FemboyEngine2D - startup project dashboard.
#pragma once

#include "Editor/Infrastructure/ProjectConfig.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace Fby
{
class VirtualFileSystem;
class WindowManager;
}

namespace Fby::Editor
{

class EditorFonts;

class ProjectDashboard
{
public:
    enum class Action : std::uint8_t
    {
        None = 0,
        OpenProject,
        NewProject,
        Quit,
    };

    static constexpr std::uint32_t kPathCapacity = 512;
    static constexpr std::uint32_t kNameCapacity = 128;

    ProjectDashboard() = default;

    // The extra arguments are optional so headless tests can keep calling the
    void Initialize(const ProjectPaths& InCurrentProject, const ProjectConfig& InCurrentConfig, bool InVisible,
                    WindowManager* InWindow = nullptr, VirtualFileSystem* InVfs = nullptr,
                    EditorFonts* InFonts = nullptr, float InUiScale = 1.0f);
    // Re-installs typography after a DPI or UI-scale change. Also safe to call
    void RefreshTypography(float InUiScale);
    void SetVisible(bool InVisible) noexcept
    {
        MVisible = InVisible;
    }
    [[nodiscard]] bool Visible() const noexcept
    {
        return MVisible;
    }
    void ShowOpenPanel() noexcept
    {
        MMode = Mode::Open;
        MVisible = true;
        MError.clear();
    }
    void ShowNewPanel() noexcept
    {
        MMode = Mode::New;
        MVisible = true;
        MError.clear();
    }
    // Automation hook: fills the path field and queues exactly the same open
    // action as pressing "Open", so a smoke run exercises the dashboard path.
    void RequestOpen(std::string_view InPath);

    // Builds the window. `window` is optional; when supplied, Open/Browse use
    void Draw(WindowManager* InWindow);

    // Consumes the action produced by the last Draw(). Copies the resolved
    [[nodiscard]] Action ConsumeResult(ProjectPaths* OutPaths, ProjectConfig* OutConfig, std::string* OutError);

    [[nodiscard]] const ProjectPaths& CurrentProject() const noexcept
    {
        return MCurrentProject;
    }
    [[nodiscard]] const std::string& Error() const noexcept
    {
        return MError;
    }

private:
    enum class Mode : std::uint8_t
    {
        Welcome = 0,
        Open,
        New,
    };

    void DrawWelcome(WindowManager* InWindow);
    void DrawOpen(WindowManager* InWindow);
    void DrawNew(WindowManager* InWindow);
    void DrawFontStatus();
    void OpenFromField();
    void CreateFromFields();
    void UseDefaultLocation();

    ProjectPaths MCurrentProject{};
    ProjectConfig MCurrentConfig{};
    ProjectPaths MPendingProject{};
    ProjectConfig MPendingConfig{};
    std::string MPendingError;
    std::string MError;
    std::string MStatus;
    std::filesystem::path MDefaultProjectsRoot{};
    char MOpenPath[kPathCapacity]{};
    char MLocation[kPathCapacity]{};
    char MProjectName[kNameCapacity] = "MyProject";
    Mode MMode = Mode::Welcome;
    Action MPendingAction = Action::None;
    WindowManager* MWindow = nullptr;
    VirtualFileSystem* MVfs = nullptr;
    EditorFonts* MFonts = nullptr;
    float MLastUiScale = 1.0f;
    bool MVisible = true;
    bool MGeometryLogged = false;
};
}
