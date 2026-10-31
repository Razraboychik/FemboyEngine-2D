// FemboyEngine2D - startup project dashboard implementation.
#include "Editor/Panels/ProjectDashboard.hpp"

#include "Editor/Infrastructure/EditorFonts.hpp"
#include "Editor/Infrastructure/EditorTypography.hpp"
#include "Engine/Core/Log.hpp"
#include "Engine/Core/WindowManager.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace Fby::Editor
{
namespace
{
void CopyFixed(char* InDestination, std::size_t InCapacity, std::string_view InValue) noexcept
{
    if (InDestination == nullptr || InCapacity == 0)
    {
        return;
    }
    const std::size_t ScopedLength = std::min<std::size_t>(InValue.size(), InCapacity - 1);
    if (ScopedLength > 0)
    {
        std::memcpy(InDestination, InValue.data(), ScopedLength);
    }
    InDestination[ScopedLength] = '\0';
}

[[nodiscard]] std::string PathText(const std::filesystem::path& InPath)
{
    return InPath.empty() ? std::string{} : InPath.generic_string();
}

[[nodiscard]] bool ValidProjectName(std::string_view InName) noexcept
{
    if (InName.empty() || InName == "." || InName == "..")
    {
        return false;
    }
    // Keep the scaffold generators honest: the name is embedded in TOML, C++
    for (const char ScopedCharacter : InName)
    {
        const auto ScopedByte = static_cast<unsigned char>(ScopedCharacter);
        if (ScopedByte < 0x20u || ScopedCharacter == '"' || ScopedCharacter == '\'' || ScopedCharacter == '`')
        {
            return false;
        }
    }
    return InName.find('/') == std::string_view::npos && InName.find('\\') == std::string_view::npos &&
           InName.find(':') == std::string_view::npos;
}
}

void ProjectDashboard::Initialize(const ProjectPaths& InCurrentProject, const ProjectConfig& InCurrentConfig,
                                  bool InVisible, WindowManager* InWindow, VirtualFileSystem* InVfs,
                                  EditorFonts* InFonts, float InUiScale)
{
    MWindow = InWindow;
    MVfs = InVfs;
    MFonts = InFonts;
    MLastUiScale = InUiScale;
    MCurrentProject = InCurrentProject;
    MCurrentConfig = InCurrentConfig;
    MVisible = InVisible;
    MMode = Mode::Welcome;
    MError.clear();
    MStatus.clear();

    // Typography first: this window is the first surface the user sees, and it
    if (MFonts != nullptr && ImGui::GetCurrentContext() != nullptr)
    {
        EditorFontSpec ScopedSpec;
        ScopedSpec.uiScale = InUiScale;
        if (!MFonts->Initialize(ImGui::GetIO(), MVfs, MWindow, ScopedSpec))
        {
            MError = "The dashboard could not install Inter from the VFS font mount. "
                     "The editor is running with the embedded vector fallback.";
        }
    }

    CopyFixed(MOpenPath, sizeof(MOpenPath),
              MCurrentProject.projectFile.empty() ? std::string{} : PathText(MCurrentProject.projectFile));

    // The default working directory is engine-owned, never the process CWD and
    std::string ScopedLocationError;
    MDefaultProjectsRoot = DefaultProjectsRoot(true, &ScopedLocationError);
    if (!ScopedLocationError.empty())
    {
        MStatus = ScopedLocationError;
    }

    // A new project always starts in Projects/. An already open project keeps
    if (MCurrentProject.IsValid() && !MCurrentProject.root.parent_path().empty())
    {
        CopyFixed(MLocation, sizeof(MLocation), PathText(MCurrentProject.root.parent_path()));
    }
    else
    {
        UseDefaultLocation();
    }

    const std::string_view ScopedProjectName =
        ProjectString(MCurrentConfig.project.name, sizeof(MCurrentConfig.project.name));
    CopyFixed(MProjectName, sizeof(MProjectName),
              ScopedProjectName.empty() || ScopedProjectName == "Untitled Project" ? "MyProject" : ScopedProjectName);

    LogInfo("dashboard", "default project location: '{}'", PathText(MDefaultProjectsRoot));
}

void ProjectDashboard::RefreshTypography(float InUiScale)
{
    MLastUiScale = InUiScale;
    if (MFonts == nullptr || ImGui::GetCurrentContext() == nullptr)
    {
        return;
    }
    ImGuiIO& ScopedIo = ImGui::GetIO();
    MFonts->RefreshDisplayScale(ScopedIo, MWindow);
    MFonts->SetUiScale(InUiScale);
}

void ProjectDashboard::UseDefaultLocation()
{
    CopyFixed(MLocation, sizeof(MLocation), PathText(MDefaultProjectsRoot));
}

void ProjectDashboard::RequestOpen(std::string_view InPath)
{
    CopyFixed(MOpenPath, sizeof(MOpenPath), InPath);
    MMode = Mode::Open;
    MVisible = true;
    OpenFromField();
}

void ProjectDashboard::OpenFromField()
{
    MPendingError.clear();
    if (MOpenPath[0] == '\0')
    {
        MError = "Choose a project.femproj or a project folder.";
        return;
    }
    if (!LoadProject(MOpenPath, &MPendingProject, &MPendingConfig, &MPendingError))
    {
        MError = MPendingError;
        return;
    }
    MPendingAction = Action::OpenProject;
    MError.clear();
    LogInfo("dashboard", "opening project '{}'", MPendingProject.root.string());
}

void ProjectDashboard::CreateFromFields()
{
    MPendingError.clear();
    if (MLocation[0] == '\0')
    {
        UseDefaultLocation();
    }
    if (MLocation[0] == '\0')
    {
        MError = "Choose where the project folder should be created.";
        return;
    }
    if (!ValidProjectName(MProjectName))
    {
        MError = "Project name must be a plain folder name: no separators, no ':', no '..'.";
        return;
    }
    if (!CreateProject(MLocation, MProjectName, &MPendingProject, &MPendingConfig, &MPendingError))
    {
        MError = MPendingError;
        return;
    }
    MPendingAction = Action::NewProject;
    MError.clear();
    MStatus = "Project created at " + PathText(MPendingProject.root);
    LogInfo("dashboard", "created project '{}'", MPendingProject.root.string());
}

void ProjectDashboard::DrawFontStatus()
{
    if (MFonts == nullptr)
    {
        return;
    }
    const EditorFontStatus& ScopedStatus = MFonts->Status();
    if (ScopedStatus.embeddedVectorFallback)
    {
        ImGui::TextColored(ImVec4(0.92f, 0.35f, 0.45f, 1.0f),
                           "Typography: embedded vector fallback - fonts/IBMPlexSans-Regular.ttf is missing from the VFS.");
        return;
    }
    ImGui::TextDisabled("Typography: IBM Plex from fonts/IBMPlexSans-Regular.ttf, %.0f%% display scale, ui scale %.2f%s",
                        static_cast<double>(ScopedStatus.displayScale * 100.0f), static_cast<double>(MLastUiScale),
                        ScopedStatus.bakedAtlasPresent && !ScopedStatus.bakedAtlasValid
                            ? " - fonts/Inter.fbyfont header is invalid"
                            : "");
}

void ProjectDashboard::Draw(WindowManager* InWindow)
{
    if (!MVisible)
    {
        return;
    }

    // The DPI can change while the dashboard is open (dragging the window to a
    RefreshTypography(MLastUiScale);

    ImFont* ScopedFont = MFonts != nullptr ? MFonts->Regular() : nullptr;
    if (ScopedFont != nullptr)
    {
        // 0.0f keeps the current size and only swaps the face.
        ImGui::PushFont(ScopedFont, 0.0f);
    }

    const ImGuiViewport* ScopedViewport = ImGui::GetMainViewport();
    if (!MGeometryLogged)
    {
        const ImGuiIO& ScopedIo = ImGui::GetIO();
        LogInfo("dashboard",
                "viewport {:.0f}x{:.0f} at ({:.0f},{:.0f}), work {:.0f}x{:.0f} at ({:.0f},{:.0f}), fb scale {:.2f}",
                ScopedIo.DisplaySize.x, ScopedIo.DisplaySize.y, ScopedViewport->Pos.x, ScopedViewport->Pos.y,
                ScopedViewport->WorkSize.x, ScopedViewport->WorkSize.y, ScopedViewport->WorkPos.x,
                ScopedViewport->WorkPos.y, ScopedIo.DisplayFramebufferScale.x);
        MGeometryLogged = true;
    }
    // Centre on the actual work area with a pivot instead of recomputing the
    // offset by hand; the dashboard then stays centred on any monitor layout.
    const ImVec2 ScopedSize(860.0f, 560.0f);
    ImGui::SetNextWindowPos(ScopedViewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ScopedSize, ImGuiCond_Always);
    const ImGuiWindowFlags ScopedFlags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                                         ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings |
                                         ImGuiWindowFlags_NoMove;
    if (!BeginEditorPanel("Project Dashboard", nullptr, ScopedFlags))
    {
        EndEditorPanel();
        if (ScopedFont != nullptr)
        {
            ImGui::PopFont();
        }
        return;
    }

    ImGui::TextDisabled("FemboyEngine2D");
    ImGui::SameLine();
    ImGui::Text("| Project Dashboard");
    ImGui::SameLine();
    ImGui::TextDisabled("| English");
    ImGui::Separator();

    switch (MMode)
    {
        case Mode::Welcome:
            DrawWelcome(InWindow);
            break;
        case Mode::Open:
            DrawOpen(InWindow);
            break;
        case Mode::New:
            DrawNew(InWindow);
            break;
    }

    if (!MError.empty())
    {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.92f, 0.35f, 0.45f, 1.0f), "%s", MError.c_str());
    }
    if (!MStatus.empty())
    {
        ImGui::Separator();
        ImGui::TextDisabled("%s", MStatus.c_str());
    }
    EndEditorPanel();
    if (ScopedFont != nullptr)
    {
        ImGui::PopFont();
    }
}

void ProjectDashboard::DrawWelcome(WindowManager* InWindow)
{
    (void)InWindow;
    ImGui::TextWrapped("Open an existing project or create a clean, typed project tree. "
                       "A project owns assets/, configs/ and one project.femproj document.");
    ImGui::Spacing();
    ImGui::Spacing();

    if (ImGui::Button("Open Project", ImVec2(220.0f, 44.0f)))
    {
        MMode = Mode::Open;
        MError.clear();
    }
    ImGui::SameLine();
    if (ImGui::Button("New Project", ImVec2(220.0f, 44.0f)))
    {
        MMode = Mode::New;
        UseDefaultLocation();
        MError.clear();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("New projects are created in Projects/ inside the engine root:");
    ImGui::Text("%s", PathText(MDefaultProjectsRoot).c_str());

    ImGui::Spacing();
    if (MCurrentProject.IsValid())
    {
        ImGui::TextDisabled("Current project");
        ImGui::Text("%s", MCurrentProject.root.string().c_str());
        if (ImGui::Button("Continue with this project", ImVec2(300.0f, 32.0f)))
        {
            MPendingProject = MCurrentProject;
            MPendingConfig = MCurrentConfig;
            MPendingAction = Action::OpenProject;
        }
    }
    else
    {
        ImGui::TextDisabled("No project is open yet.");
    }

    ImGui::Separator();
    if (ImGui::Button("Quit", ImVec2(100.0f, 26.0f)))
    {
        MPendingAction = Action::Quit;
    }
    ImGui::SameLine();
    DrawFontStatus();
}

void ProjectDashboard::DrawOpen(WindowManager* InWindow)
{
    ImGui::Text("Open project");
    ImGui::Spacing();
    ImGui::SetNextItemWidth(-120.0f);
    ImGui::InputTextWithHint("##open_path", "path/to/project.femproj or project folder", MOpenPath, sizeof(MOpenPath));
    ImGui::SameLine();
    if (ImGui::Button("Browse...", ImVec2(110.0f, 0.0f)) && InWindow != nullptr)
    {
        // SDL takes bare extensions, so "femproj" means *.femproj. Passing
        // "project.femproj" would ask the OS for *.project.femproj and hide
        // every real project file.
        static const FileFilter ScopedFilters[] = {{"FemboyEngine project", "femproj"}};
        InWindow->ShowOpenFile("Open FemboyEngine project", ScopedFilters,
                               [this](std::vector<std::string> InPaths)
                               {
                                   if (!InPaths.empty())
                                   {
                                       CopyFixed(MOpenPath, sizeof(MOpenPath), InPaths.front());
                                   }
                               });
    }

    ImGui::Spacing();
    if (ImGui::Button("Open", ImVec2(140.0f, 34.0f)))
    {
        OpenFromField();
    }
    ImGui::SameLine();
    if (ImGui::Button("Back", ImVec2(100.0f, 34.0f)))
    {
        MMode = Mode::Welcome;
    }
}

void ProjectDashboard::DrawNew(WindowManager* InWindow)
{
    ImGui::Text("New project");
    ImGui::Spacing();
    ImGui::SetNextItemWidth(-120.0f);
    ImGui::InputTextWithHint("##project_name", "project name", MProjectName, sizeof(MProjectName));
    ImGui::SetNextItemWidth(-120.0f);
    ImGui::InputTextWithHint("##project_location", "parent directory", MLocation, sizeof(MLocation));
    ImGui::SameLine();
    if (ImGui::Button("Browse...", ImVec2(110.0f, 0.0f)) && InWindow != nullptr)
    {
        InWindow->ShowOpenFolder("Choose the parent directory",
                                 [this](std::vector<std::string> InPaths)
                                 {
                                     if (!InPaths.empty())
                                     {
                                         CopyFixed(MLocation, sizeof(MLocation),
                                                   std::filesystem::path(InPaths.front()).generic_string());
                                     }
                                 },
                                 MLocation[0] != '\0' ? std::string_view(MLocation) : std::string_view{});
    }
    if (ImGui::Button("Use the default Projects/ location", ImVec2(300.0f, 0.0f)))
    {
        UseDefaultLocation();
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Creates:");
    ImGui::BulletText("assets/textures, assets/audio, assets/scenes");
    ImGui::BulletText("configs/");
    ImGui::BulletText("project.femproj (TOML, toml++)");
    ImGui::Spacing();
    if (MLocation[0] != '\0')
    {
        ImGui::TextDisabled("Target: %s", PathText(std::filesystem::path(MLocation) / MProjectName).c_str());
    }

    ImGui::Spacing();
    const bool ScopedCanCreate = ValidProjectName(MProjectName) && MLocation[0] != '\0';
    ImGui::BeginDisabled(!ScopedCanCreate);
    if (ImGui::Button("Create", ImVec2(140.0f, 34.0f)))
    {
        CreateFromFields();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Back", ImVec2(100.0f, 34.0f)))
    {
        MMode = Mode::Welcome;
    }
}

ProjectDashboard::Action ProjectDashboard::ConsumeResult(ProjectPaths* OutPaths, ProjectConfig* OutConfig,
                                                         std::string* OutError)
{
    if (MPendingAction == Action::None)
    {
        return Action::None;
    }
    const Action ScopedAction = MPendingAction;
    MPendingAction = Action::None;
    if (ScopedAction == Action::OpenProject || ScopedAction == Action::NewProject)
    {
        if (OutPaths != nullptr)
        {
            *OutPaths = MPendingProject;
        }
        if (OutConfig != nullptr)
        {
            *OutConfig = MPendingConfig;
        }
        MVisible = false;
        MCurrentProject = MPendingProject;
        MCurrentConfig = MPendingConfig;
    }
    else if (ScopedAction == Action::Quit)
    {
        MVisible = false;
    }
    if (OutError != nullptr)
    {
        *OutError = MPendingError;
    }
    return ScopedAction;
}
}
