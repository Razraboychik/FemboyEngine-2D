// FemboyEngine2D - Content Browser implementation.
#include "Editor/Panels/ContentBrowserPanel.hpp"

#include "Engine/Audio/Audio.hpp"
#include "Engine/Assets/VFS.hpp"
#include "Engine/Core/Log.hpp"
#include "Editor/Infrastructure/AssetImport.hpp"
#include "Editor/Infrastructure/AssetImportService.hpp"
#include "Editor/Infrastructure/CommandStack.hpp"
#include "Editor/Infrastructure/EditTransactions.hpp"
#include "Editor/Infrastructure/EditorTypography.hpp"
#include "Engine/ECS/Scene.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <format>
#include <string>
#include <system_error>
#include <vector>

namespace Fby::Editor
{
namespace
{
void SetError(std::string* InError, std::string InText)
{
    if (InError != nullptr)
    {
        *InError = std::move(InText);
    }
}

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

[[nodiscard]] std::string_view FileNameOf(std::string_view InPath) noexcept
{
    const std::size_t ScopedSlash = InPath.find_last_of('/');
    return ScopedSlash == std::string_view::npos ? InPath : InPath.substr(ScopedSlash + 1);
}

[[nodiscard]] std::string_view ParentOf(std::string_view InPath) noexcept
{
    const std::size_t ScopedSlash = InPath.find_last_of('/');
    return ScopedSlash == std::string_view::npos ? std::string_view{} : InPath.substr(0, ScopedSlash);
}

[[nodiscard]] bool IsHiddenArtifact(std::string_view InPath) noexcept
{
    return InPath.ends_with(".femeta") || InPath.ends_with(".ftex") || InPath.ends_with(".fpcm") ||
           InPath.ends_with(".fbin") || InPath.ends_with(".fempak") || InPath.ends_with(".fbtmp") ||
           InPath.ends_with(".pdb") || InPath.ends_with(".femproj");
}

[[nodiscard]] const char* KindLabel(AssetKind InKind) noexcept
{
    switch (InKind)
    {
        case AssetKind::Texture:
            return "texture";
        case AssetKind::TextureArray:
            return "texture-array";
        case AssetKind::AudioClip:
            return "audio";
        case AssetKind::Video:
            return "video";
        case AssetKind::Scene:
            return "prefab";
        case AssetKind::Skeleton:
            return "skeleton";
        case AssetKind::Animation:
            return "animation";
        case AssetKind::Material:
            return "material";
        case AssetKind::Script:
            return "script";
        case AssetKind::Text:
            return "text";
        case AssetKind::Font:
            return "font";
        case AssetKind::FontFamily:
            return "font-family";
        case AssetKind::Mesh:
            return "mesh";
        case AssetKind::SpriteSheet:
            return "sprite";
        case AssetKind::Unknown:
            break;
    }
    return "unimported";
}

void OffsetEntity(Ecs::Registry& InRegistry, Entity InEntity, Vector2D InDelta)
{
    if (auto* ScopedTransform = InRegistry.try_get<Transform2D>(InEntity); ScopedTransform != nullptr)
    {
        ScopedTransform->Position.X += InDelta.X;
        ScopedTransform->Position.Y += InDelta.Y;
    }
}
}

void ContentBrowserPanel::Initialize(Assets::AssetDatabase* InDatabase, const Config& InConfig,
                                     AssetImportService* InImporter)
{
    MDatabase = InDatabase;
    MImporter = InImporter;
    MConfig = InConfig;
    MRefreshRequested = true;
}

void ContentBrowserPanel::SetProjectRoot(const std::filesystem::path& InAssetsRoot)
{
    MConfig.assetsRoot = InAssetsRoot;
    MSelected = -1;
    MRefreshRequested = true;
}

void ContentBrowserPanel::Refresh()
{
    MEntryCount = 0;
    MSelected = -1;
    MRefreshRequested = false;
    if (MConfig.assetsRoot.empty())
    {
        return;
    }
    std::error_code ScopedCode;
    if (!std::filesystem::is_directory(MConfig.assetsRoot, ScopedCode))
    {
        return;
    }

    for (const std::filesystem::directory_entry& ScopedItem : std::filesystem::recursive_directory_iterator(
             MConfig.assetsRoot, std::filesystem::directory_options::skip_permission_denied, ScopedCode))
    {
        if (ScopedCode || !ScopedItem.is_regular_file(ScopedCode))
        {
            continue;
        }
        const std::string ScopedRelative = ScopedItem.path().lexically_relative(MConfig.assetsRoot).generic_string();
        if (ScopedRelative.empty() || IsHiddenArtifact(ScopedRelative) ||
            ScopedRelative.find("/.") != std::string::npos || ScopedRelative.starts_with("."))
        {
            continue;
        }
        const std::string_view ScopedParentPath = ParentOf(ScopedRelative);
        const std::int16_t ScopedParent = EnsureDirectory(ScopedParentPath);
        AddEntry(ScopedRelative, FileNameOf(ScopedRelative), ScopedParent, false);
        if (const std::int16_t ScopedAdded = FindEntry(ScopedRelative); ScopedAdded >= 0)
        {
            const auto ScopedFileSize = ScopedItem.file_size(ScopedCode);
            MEntries[static_cast<std::uint32_t>(ScopedAdded)].size =
                ScopedCode ? 0u : static_cast<std::uint32_t>(std::min<std::uintmax_t>(ScopedFileSize, 0xFFFFFFFFu));
        }
        if (MEntryCount >= kMaxEntries)
        {
            break;
        }
    }
}

std::int16_t ContentBrowserPanel::FindEntry(std::string_view InPath) const noexcept
{
    for (std::uint32_t ScopedI = 0; ScopedI < MEntryCount; ++ScopedI)
    {
        if (InPath == MEntries[ScopedI].path)
        {
            return static_cast<std::int16_t>(ScopedI);
        }
    }
    return -1;
}

std::int16_t ContentBrowserPanel::AppendChild(std::int16_t InParent, std::int16_t InChild) noexcept
{
    if (InParent < 0 || InChild < 0)
    {
        return InChild;
    }
    Entry& ScopedParentEntry = MEntries[static_cast<std::uint32_t>(InParent)];
    if (ScopedParentEntry.firstChild < 0)
    {
        ScopedParentEntry.firstChild = InChild;
        return InChild;
    }
    std::int16_t ScopedCursor = ScopedParentEntry.firstChild;
    while (MEntries[static_cast<std::uint32_t>(ScopedCursor)].nextSibling >= 0)
    {
        ScopedCursor = MEntries[static_cast<std::uint32_t>(ScopedCursor)].nextSibling;
    }
    MEntries[static_cast<std::uint32_t>(ScopedCursor)].nextSibling = InChild;
    return InChild;
}

void ContentBrowserPanel::AddEntry(std::string_view InPath, std::string_view InName, std::int16_t InParent,
                                   bool InDirectory)
{
    if (MEntryCount >= kMaxEntries || InPath.size() >= kMaxEntryPath || InName.size() >= kMaxEntryName)
    {
        return;
    }
    if (FindEntry(InPath) >= 0)
    {
        return;
    }
    Entry& ScopedEntry = MEntries[MEntryCount];
    ScopedEntry = Entry{};
    CopyFixed(ScopedEntry.name, sizeof(ScopedEntry.name), InName);
    CopyFixed(ScopedEntry.path, sizeof(ScopedEntry.path), InPath);
    ScopedEntry.parent = InParent;
    ScopedEntry.directory = InDirectory;
    if (!InDirectory && MDatabase != nullptr)
    {
        if (const Assets::AssetHandle ScopedHandle = MDatabase->FindHandleBySourcePath(InPath); ScopedHandle.IsValid())
        {
            if (const Assets::AssetMeta* ScopedMeta = MDatabase->Meta(ScopedHandle); ScopedMeta != nullptr)
            {
                ScopedEntry.hasMeta = true;
                ScopedEntry.id = ScopedMeta->id;
                ScopedEntry.guid = ScopedMeta->guid;
                ScopedEntry.kind = ScopedMeta->kind;
                ScopedEntry.size = 0;
                ScopedEntry.generation = ScopedMeta->contentGeneration;
            }
        }
    }
    if (!InDirectory && !ScopedEntry.hasMeta)
    {
        (void)InferAssetKindFromPath(InPath, &ScopedEntry.kind);
    }
    const auto ScopedIndex = static_cast<std::int16_t>(MEntryCount);
    ++MEntryCount;
    (void)AppendChild(InParent, ScopedIndex);
}

std::int16_t ContentBrowserPanel::EnsureDirectory(std::string_view InRelativeDirectory)
{
    if (InRelativeDirectory.empty())
    {
        return -1;
    }
    if (const std::int16_t ScopedExisting = FindEntry(InRelativeDirectory); ScopedExisting >= 0)
    {
        return ScopedExisting;
    }
    const std::string_view ScopedParentPath = ParentOf(InRelativeDirectory);
    const std::int16_t ScopedParent = EnsureDirectory(ScopedParentPath);
    AddEntry(InRelativeDirectory, FileNameOf(InRelativeDirectory), ScopedParent, true);
    return FindEntry(InRelativeDirectory);
}

const ContentBrowserPanel::Entry* ContentBrowserPanel::Selected() const noexcept
{
    return MSelected >= 0 ? &MEntries[static_cast<std::uint32_t>(MSelected)] : nullptr;
}

void ContentBrowserPanel::Draw(Ecs::Registry& InRegistry, const Scene::ComponentRegistry& InComponents,
                               CommandStack& InCommands, VirtualFileSystem* InAssets, bool* InOpen,
                               std::string* InStatus)
{
    if (!BeginEditorPanel("Content Browser", InOpen))
    {
        EndEditorPanel();
        return;
    }

    const float ScopedNow = static_cast<float>(ImGui::GetTime());
    if (MRefreshRequested || (MConfig.autoRefresh && !ImGui::IsAnyItemActive() &&
                              ScopedNow - MLastRefreshTime >= MConfig.refreshIntervalSeconds))
    {
        Refresh();
        MLastRefreshTime = ScopedNow;
    }

    if (ImGui::Button("Refresh"))
    {
        Refresh();
        MLastRefreshTime = ScopedNow;
    }
    ImGui::SameLine();
    DrawFileMenu(InAssets, InStatus);
    ImGui::SameLine();
    ImGui::Checkbox("Auto", &MConfig.autoRefresh);
    ImGui::SameLine();
    ImGui::TextDisabled("%u entries | %s", MEntryCount,
                        MConfig.assetsRoot.empty() ? "(no project)" : MConfig.assetsRoot.string().c_str());
    ImGui::Separator();

    if (MFileDialogOpen)
    {
        ImGui::OpenPopup("Asset name");
        MFileDialogOpen = false;
    }
    if (ImGui::BeginPopupModal("Asset name", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        const char* ScopedTitle = MFileDialog == FileDialog::NewFolder
                                      ? "New folder name"
                                      : (MFileDialog == FileDialog::NewScene ? "New scene name" : "New name");
        ImGui::TextUnformatted(ScopedTitle);
        ImGui::SetNextItemWidth(280.0f);
        if (MFileDialogFocus)
        {
            ImGui::SetKeyboardFocusHere();
            MFileDialogFocus = false;
        }
        const bool ScopedEntered =
            ImGui::InputText("##cbName", MFileName, sizeof(MFileName), ImGuiInputTextFlags_EnterReturnsTrue);
        if (ImGui::Button("OK", ImVec2(90.0f, 0.0f)) || ScopedEntered)
        {
            ApplyFileDialog(InAssets, InStatus);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(90.0f, 0.0f)))
        {
            MFileDialog = FileDialog::None;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (MDeleteOpen)
    {
        ImGui::OpenPopup("Delete asset");
        MDeleteOpen = false;
    }
    if (ImGui::BeginPopupModal("Delete asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Delete '%s'?", MDeleteTarget.c_str());
        ImGui::TextDisabled("This cannot be undone.");
        if (ImGui::Button("Delete", ImVec2(90.0f, 0.0f)))
        {
            DeleteTargetFile(InAssets, InStatus);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(90.0f, 0.0f)))
        {
            MDeleteTarget.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    const ImVec2 ScopedAvailable = ImGui::GetContentRegionAvail();
    if (ImGui::BeginChild("##content_tree", ImVec2(0.0f, std::max(160.0f, ScopedAvailable.y * 0.55f)),
                          ImGuiChildFlags_Borders))
    {
        for (std::uint32_t ScopedI = 0; ScopedI < MEntryCount; ++ScopedI)
        {
            if (MEntries[ScopedI].parent < 0)
            {
                DrawEntry(static_cast<std::int16_t>(ScopedI), InRegistry, InComponents, InCommands, InAssets, InStatus);
            }
        }
    }
    ImGui::EndChild();

    DrawDetails(InRegistry, InComponents, InCommands, InAssets, InStatus);
    EndEditorPanel();
}

void ContentBrowserPanel::DrawEntry(std::int16_t InIndex, Ecs::Registry& InRegistry,
                                    const Scene::ComponentRegistry& InComponents, CommandStack& InCommands,
                                    VirtualFileSystem* InAssets, std::string* InStatus)
{
    if (InIndex < 0 || static_cast<std::uint32_t>(InIndex) >= MEntryCount)
    {
        return;
    }
    Entry& ScopedEntry = MEntries[static_cast<std::uint32_t>(InIndex)];
    ImGui::PushID(static_cast<int>(InIndex));

    if (ScopedEntry.directory)
    {
        ImGuiTreeNodeFlags ScopedFlags =
            ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen;
        if (ScopedEntry.firstChild < 0)
        {
            ScopedFlags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        }
        const bool ScopedOpen = ImGui::TreeNodeEx("##dir", ScopedFlags, "%s/", ScopedEntry.name);
        if (ImGui::IsItemHovered())
        {
            EditorTooltip("%s", ScopedEntry.path);
        }
        if (ScopedOpen && ScopedEntry.firstChild >= 0)
        {
            std::int16_t ScopedChild = ScopedEntry.firstChild;
            while (ScopedChild >= 0)
            {
                DrawEntry(ScopedChild, InRegistry, InComponents, InCommands, InAssets, InStatus);
                ScopedChild = MEntries[static_cast<std::uint32_t>(ScopedChild)].nextSibling;
            }
            ImGui::TreePop();
        }
    }
    else
    {
        const bool ScopedSelected = MSelected == InIndex;
        if (ImGui::Selectable(ScopedEntry.name, ScopedSelected, ImGuiSelectableFlags_AllowDoubleClick))
        {
            MSelected = InIndex;
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                const std::string_view ScopedPath(ScopedEntry.path);
                if (ScopedPath.ends_with(".toml"))
                {
                    MOpenRequest = ScopedEntry.path;
                    SetError(InStatus, std::format("opening {}", ScopedEntry.path));
                }
                else if (MImporter != nullptr && !ScopedEntry.hasMeta)
                {
                    std::string ScopedError;
                    if (MImporter->EnsureMeta(ScopedEntry.path, &ScopedError))
                    {
                        if (InStatus != nullptr)
                        {
                            *InStatus = std::format("imported {}", ScopedEntry.path);
                        }
                        RequestRefresh();
                    }
                    else if (InStatus != nullptr)
                    {
                        *InStatus = std::format("import failed: {}", ScopedError);
                    }
                }
            }
        }
        if (ImGui::IsItemHovered())
        {
            EditorTooltip("%s\n%s", ScopedEntry.path, KindLabel(ScopedEntry.kind));
        }
        DrawDragSource(InIndex);
    }

    ImGui::PopID();
}

void ContentBrowserPanel::DrawDragSource(std::int16_t InIndex)
{
    if (InIndex < 0 || static_cast<std::uint32_t>(InIndex) >= MEntryCount)
    {
        return;
    }
    const Entry& ScopedEntry = MEntries[static_cast<std::uint32_t>(InIndex)];
    if (ScopedEntry.directory)
    {
        return;
    }
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
    {
        DragPayload ScopedPayload;
        ScopedPayload.id = ScopedEntry.id;
        ScopedPayload.guid = ScopedEntry.guid;
        ScopedPayload.kind = ScopedEntry.kind;
        CopyFixed(ScopedPayload.path, sizeof(ScopedPayload.path), ScopedEntry.path);
        ImGui::SetDragDropPayload(kDragPayloadType, &ScopedPayload, sizeof(ScopedPayload));
        ImGui::Text("%s", ScopedEntry.name);
        ImGui::TextDisabled("%s", ScopedEntry.hasMeta ? KindLabel(ScopedEntry.kind) : "unimported");
        ImGui::EndDragDropSource();
    }
}

std::string ContentBrowserPanel::TakeOpenRequest()
{
    std::string ScopedRequest;
    ScopedRequest.swap(MOpenRequest);
    return ScopedRequest;
}

std::filesystem::path ContentBrowserPanel::PhysicalPath(std::string_view InRelative) const
{
    return MConfig.assetsRoot / std::filesystem::path(InRelative);
}

bool ContentBrowserPanel::IsSafeFileName(std::string_view InName)
{
    if (InName.empty() || InName.size() > 96 || InName == "." || InName == "..")
    {
        return false;
    }
    return InName.find('/') == std::string_view::npos && InName.find('\\') == std::string_view::npos &&
           InName.find("..") == std::string_view::npos;
}

void ContentBrowserPanel::DrawFileMenu(VirtualFileSystem* InAssets, std::string* InStatus)
{
    if (!ImGui::BeginMenu("File"))
    {
        return;
    }
    const Entry* ScopedSelected = Selected();
    const bool ScopedHasFile = ScopedSelected != nullptr && !ScopedSelected->directory;
    const bool ScopedHasAssetsRoot = !MConfig.assetsRoot.empty();
    ImGui::BeginDisabled(!ScopedHasAssetsRoot);
    if (ImGui::MenuItem("New Folder"))
    {
        MFileDialog = FileDialog::NewFolder;
        std::snprintf(MFileName, sizeof(MFileName), "NewFolder");
        MFileDialogOpen = true;
        MFileDialogFocus = true;
    }
    if (ImGui::MenuItem("New Scene"))
    {
        MFileDialog = FileDialog::NewScene;
        std::snprintf(MFileName, sizeof(MFileName), "untitled");
        MFileDialogOpen = true;
        MFileDialogFocus = true;
    }
    ImGui::Separator();
    ImGui::BeginDisabled(!ScopedHasFile);
    if (ImGui::MenuItem("Rename"))
    {
        MFileDialog = FileDialog::Rename;
        std::snprintf(MFileName, sizeof(MFileName), "%s", ScopedSelected->name);
        MFileDialogOpen = true;
        MFileDialogFocus = true;
    }
    if (ImGui::MenuItem("Duplicate"))
    {
        DuplicateSelectedFile(InAssets, InStatus);
    }
    if (ImGui::MenuItem("Delete"))
    {
        MDeleteTarget = ScopedSelected->path;
        MDeleteOpen = true;
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    ImGui::EndMenu();
}

void ContentBrowserPanel::ApplyFileDialog(VirtualFileSystem* InAssets, std::string* InStatus)
{
    const std::string ScopedName(MFileName);
    if (!IsSafeFileName(ScopedName))
    {
        SetError(InStatus, "invalid asset name");
        MFileDialog = FileDialog::None;
        return;
    }
    const std::filesystem::path ScopedRoot = MConfig.assetsRoot;
    std::error_code ScopedCode;
    const Entry* ScopedSelected = Selected();
    if (MFileDialog == FileDialog::NewFolder)
    {
        std::filesystem::create_directories(ScopedRoot / ScopedName, ScopedCode);
        SetError(InStatus, ScopedCode ? "cannot create folder: " + ScopedCode.message()
                                      : "folder created: " + ScopedName);
    }
    else if (MFileDialog == FileDialog::NewScene)
    {
        std::filesystem::path ScopedFile = ScopedRoot / ScopedName;
        if (ScopedFile.extension() != ".toml")
        {
            ScopedFile += ".toml";
        }
        Scene::SceneMeta ScopedMeta;
        ScopedMeta.name = ScopedFile.stem().string();
        ScopedMeta.author = "FemboyEngine2D editor";
        Ecs::Registry ScopedEmpty;
        Scene::ComponentRegistry ScopedComponents;
        Scene::RegisterBuiltInComponents(ScopedComponents);
        std::string ScopedError;
        if (Scene::SaveSceneFileToml(ScopedFile, ScopedEmpty, ScopedComponents, ScopedMeta, &ScopedError))
        {
            SetError(InStatus, "scene created: " + ScopedFile.filename().string());
        }
        else
        {
            SetError(InStatus, "cannot create scene: " + ScopedError);
        }
    }
    else if (MFileDialog == FileDialog::Rename && ScopedSelected != nullptr && !ScopedSelected->directory)
    {
        const std::filesystem::path ScopedOld = ScopedRoot / ScopedSelected->path;
        std::filesystem::path ScopedNew = ScopedOld.parent_path() / ScopedName;
        if (ScopedNew.extension().empty())
        {
            ScopedNew += ScopedOld.extension();
        }
        std::filesystem::rename(ScopedOld, ScopedNew, ScopedCode);
        SetError(InStatus, ScopedCode ? "cannot rename: " + ScopedCode.message()
                                      : "renamed to " + ScopedNew.filename().string());
    }
    MFileDialog = FileDialog::None;
    if (InAssets != nullptr)
    {
        InAssets->RefreshDirectoryMounts();
    }
    RequestRefresh();
}

void ContentBrowserPanel::DuplicateSelectedFile(VirtualFileSystem* InAssets, std::string* InStatus)
{
    const Entry* ScopedSelected = Selected();
    if (ScopedSelected == nullptr || ScopedSelected->directory)
    {
        return;
    }
    const std::filesystem::path ScopedSource = MConfig.assetsRoot / ScopedSelected->path;
    std::error_code ScopedCode;
    std::filesystem::path ScopedCopy = ScopedSource.parent_path() /
                                       (ScopedSource.stem().string() + "_copy" + ScopedSource.extension().string());
    for (int ScopedIndex = 2; std::filesystem::exists(ScopedCopy, ScopedCode) && ScopedIndex < 1000; ++ScopedIndex)
    {
        ScopedCopy = ScopedSource.parent_path() / (ScopedSource.stem().string() + "_copy" +
                                                   std::to_string(ScopedIndex) + ScopedSource.extension().string());
    }
    std::filesystem::copy_file(ScopedSource, ScopedCopy, ScopedCode);
    SetError(InStatus, ScopedCode ? "cannot duplicate: " + ScopedCode.message()
                                  : "duplicated: " + ScopedCopy.filename().string());
    if (InAssets != nullptr)
    {
        InAssets->RefreshDirectoryMounts();
    }
    RequestRefresh();
}

void ContentBrowserPanel::DeleteTargetFile(VirtualFileSystem* InAssets, std::string* InStatus)
{
    if (MDeleteTarget.empty())
    {
        return;
    }
    std::error_code ScopedCode;
    const bool ScopedRemoved = std::filesystem::remove(MConfig.assetsRoot / MDeleteTarget, ScopedCode);
    SetError(InStatus, ScopedRemoved && !ScopedCode ? "deleted: " + MDeleteTarget
                                                    : "cannot delete: " + ScopedCode.message());
    MDeleteTarget.clear();
    if (InAssets != nullptr)
    {
        InAssets->RefreshDirectoryMounts();
    }
    RequestRefresh();
}

void ContentBrowserPanel::DrawDetails(Ecs::Registry& InRegistry, const Scene::ComponentRegistry& InComponents,
                                      CommandStack& InCommands, VirtualFileSystem* InAssets, std::string* InStatus)
{
    ImGui::Separator();
    const Entry* ScopedEntry = Selected();
    if (ScopedEntry == nullptr)
    {
        ImGui::TextDisabled("Select an asset to inspect its .femeta row.");
        return;
    }

    ImGui::TextUnformatted(ScopedEntry->path);
    ImGui::SameLine();
    ImGui::TextDisabled("[%s]", KindLabel(ScopedEntry->kind));
    if (ScopedEntry->directory)
    {
        ImGui::TextDisabled("folder");
        return;
    }

    if (!ScopedEntry->hasMeta)
    {
        ImGui::TextDisabled("No .femeta row. The watcher imports new sources automatically.");
        if (MImporter != nullptr && ImGui::Button("Import / Generate .femeta"))
        {
            std::string ScopedError;
            if (MImporter->EnsureMeta(ScopedEntry->path, &ScopedError))
            {
                if (InStatus != nullptr)
                {
                    *InStatus = std::format("imported {}", ScopedEntry->path);
                }
                RequestRefresh();
            }
            else if (InStatus != nullptr)
            {
                *InStatus = std::format("import failed: {}", ScopedError);
            }
        }
        return;
    }

    if (MDatabase != nullptr)
    {
        if (const Assets::AssetHandle ScopedHandle = MDatabase->FindHandleBySourcePath(ScopedEntry->path);
            ScopedHandle.IsValid())
        {
            if (const Assets::AssetMeta* ScopedMeta = MDatabase->Meta(ScopedHandle); ScopedMeta != nullptr)
            {
                ImGui::Text("AssetId 0x%016llX", static_cast<unsigned long long>(ScopedMeta->id.value));
                ImGui::Text("GUID    0x%016llX", static_cast<unsigned long long>(ScopedMeta->guid));
                ImGui::Text("content generation %u", ScopedMeta->contentGeneration);
                ImGui::Text("cooked  %s", ScopedMeta->cookedPath[0] != '\0' ? ScopedMeta->cookedPath : "(none)");
                ImGui::Text(
                    "import  %s  max %u  %s  mips %s",
                    Assets::TextureFormatName(static_cast<Assets::TextureFormat>(ScopedMeta->import.textureFormat)),
                    ScopedMeta->import.maxSize, ScopedMeta->import.srgb ? "sRGB" : "linear",
                    ScopedMeta->import.generateMips ? "on" : "off");
                if (ScopedMeta->kind == AssetKind::Font)
                {
                    ImGui::Text("font    style %u  size %u  oversample %u  SDF %s",
                                ScopedMeta->import.fontStyle, ScopedMeta->import.fontPixels,
                                ScopedMeta->import.fontOversample, ScopedMeta->import.fontSdf ? "on" : "off");
                }
            }
        }
    }

    ImGui::Text("source size %u bytes", ScopedEntry->size);

    if (ScopedEntry->kind == AssetKind::Texture || ScopedEntry->kind == AssetKind::Scene)
    {
        ImGui::TextDisabled("Drag to the Viewport to spawn an entity (one undo step).");
    }
    (void)InRegistry;
    (void)InComponents;
    (void)InCommands;
    (void)InAssets;
}

bool ContentBrowserPanel::AcceptViewportDrop(SpawnContext& InContext, std::string* InError)
{
    if (!ImGui::BeginDragDropTarget())
    {
        return false;
    }
    DragPayload ScopedPayload{};
    bool ScopedHasPayload = false;
    if (const ImGuiPayload* ScopedAccepted = ImGui::AcceptDragDropPayload(kDragPayloadType); ScopedAccepted != nullptr)
    {
        if (ScopedAccepted->DataSize == static_cast<int>(sizeof(DragPayload)) && ScopedAccepted->Data != nullptr)
        {
            std::memcpy(&ScopedPayload, ScopedAccepted->Data, sizeof(ScopedPayload));
            ScopedPayload.path[sizeof(ScopedPayload.path) - 1] = '\0';
            ScopedHasPayload = true;
        }
    }
    ImGui::EndDragDropTarget();
    return ScopedHasPayload && SpawnAsset(ScopedPayload, InContext, InError);
}

bool ContentBrowserPanel::SpawnAsset(const DragPayload& InPayload, SpawnContext& InContext, std::string* InError)
{
    if (InContext.commands == nullptr || InContext.registry == nullptr || InContext.components == nullptr)
    {
        SetError(InError, "spawn context is incomplete");
        return false;
    }
    CommandStack& ScopedCommands = *InContext.commands;
    Ecs::Registry& ScopedRegistry = *InContext.registry;

    const bool ScopedIsTexture = InPayload.kind == AssetKind::Texture;
    const bool ScopedIsPrefab =
        InPayload.kind == AssetKind::Scene || std::string_view(InPayload.path).ends_with(".toml");
    if (!ScopedIsTexture && !ScopedIsPrefab)
    {
        SetError(InError, "only texture and .toml prefab drops spawn entities");
        return false;
    }

    SpawnTransaction ScopedTransaction;
    const char* ScopedLabel = ScopedIsTexture ? "Spawn texture entity" : "Spawn prefab";
    if (ScopedTransaction.Begin(ScopedCommands, ScopedLabel) != CommandStack::Result::Ok)
    {
        SetError(InError, "another edit transaction is already open");
        return false;
    }

    if (ScopedIsTexture)
    {
        const Entity ScopedEntity = ScopedRegistry.create();
        Transform2D ScopedTransform;
        ScopedTransform.Position = InContext.dropPosition;
        ScopedRegistry.emplace<Transform2D>(ScopedEntity, ScopedTransform);

        Sprite2D ScopedSprite;
        ScopedSprite.Size = {64.0f, 64.0f};
        ScopedSprite.Layer = InContext.fallbackTextureLayer;
        if (InContext.importer != nullptr)
        {
            const std::uint32_t ScopedLayer = InContext.importer->LayerFor(InPayload.id);
            if (ScopedLayer != Rhi::TextureArrayHandle::kInvalid32)
            {
                ScopedSprite.Layer = ScopedLayer;
            }
        }
        ScopedSprite.Color = Color32::White();
        ScopedRegistry.emplace<Sprite2D>(ScopedEntity, ScopedSprite);

        DrawOrder ScopedOrder;
        ScopedOrder.Layer = 10;
        ScopedOrder.Depth = 0.0f;
        ScopedRegistry.emplace<DrawOrder>(ScopedEntity, ScopedOrder);

        if (ScopedTransaction.Record(ScopedEntity) != CommandStack::Result::Ok)
        {
            ScopedRegistry.destroy(ScopedEntity);
            ScopedTransaction.Cancel();
            SetError(InError, "cannot record the spawned texture entity");
            return false;
        }
    }
    else
    {
        const std::filesystem::path ScopedPhysical = MConfig.assetsRoot / std::filesystem::path(InPayload.path);
        Scene::SceneLoadOptions ScopedOptions;
        ScopedOptions.clearExisting = false;
        ScopedOptions.assets = InContext.assets;
        ScopedOptions.assetDb = InContext.database;
        auto ScopedLoaded =
            Scene::LoadSceneFileToml(ScopedRegistry, *InContext.components, ScopedPhysical, ScopedOptions);
        if (!ScopedLoaded)
        {
            ScopedTransaction.Cancel();
            SetError(InError, ScopedLoaded.error());
            return false;
        }
        const Scene::EntityRemap& ScopedRemap = *ScopedLoaded;
        if (ScopedRemap.entities.empty())
        {
            ScopedTransaction.Cancel();
            SetError(InError, "prefab contains no entities");
            return false;
        }
        // The prefab's authored coordinates are preserved; the drop point moves
        Vector2D ScopedAnchor{};
        bool ScopedHasAnchor = false;
        for (const Entity ScopedEntity : ScopedRemap.entities)
        {
            if (const auto* ScopedTransform = ScopedRegistry.try_get<Transform2D>(ScopedEntity);
                ScopedTransform != nullptr)
            {
                ScopedAnchor = ScopedTransform->Position;
                ScopedHasAnchor = true;
                break;
            }
        }
        const Vector2D ScopedDelta = ScopedHasAnchor ? Vector2D{InContext.dropPosition.X - ScopedAnchor.X,
                                                                InContext.dropPosition.Y - ScopedAnchor.Y}
                                                     : Vector2D{};
        for (const Entity ScopedEntity : ScopedRemap.entities)
        {
            OffsetEntity(ScopedRegistry, ScopedEntity, ScopedDelta);
            if (ScopedTransaction.Record(ScopedEntity) != CommandStack::Result::Ok)
            {
                ScopedTransaction.Cancel();
                SetError(InError, "cannot record the prefab entity set");
                return false;
            }
        }
    }

    const CommandStack::Result ScopedCommitted = ScopedTransaction.Commit();
    if (ScopedCommitted != CommandStack::Result::Ok)
    {
        SetError(InError, std::format("spawn transaction failed: {}", CommandStack::ResultName(ScopedCommitted)));
        return false;
    }
    return true;
}
}
