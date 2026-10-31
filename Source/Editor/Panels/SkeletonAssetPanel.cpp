#include "Editor/Panels/SkeletonAssetPanel.hpp"

#include "Engine/Assets/AssetDatabase.hpp"
#include "Engine/Assets/VFS.hpp"
#include "Engine/Core/Log.hpp"
#include "Editor/Infrastructure/EditorTheme.hpp"
#include "Engine/ECS/Schedule.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

namespace Fby::Editor
{
namespace
{
// The same FieldDesc vocabulary Mirror generates for ECS components, but
struct BoneColumn
{
    const char* label;
    Reflect::FieldDesc field;
    std::uint32_t stride;
    float speed;
};

[[nodiscard]] consteval std::array<BoneColumn, 3> MakeBoneColumns()
{
    return {{
        {"local position", FBY_FIELD(SkeletonComponent, LocalPositions, Vector2D), sizeof(Vector2D), 0.1f},
        {"local rotation", FBY_FIELD(SkeletonComponent, LocalRotations, Float), sizeof(float), 0.01f},
        {"local scale", FBY_FIELD(SkeletonComponent, LocalScales, Float), sizeof(float), 0.01f},
    }};
}

inline constexpr std::array<BoneColumn, 3> kBoneColumns = MakeBoneColumns();

[[nodiscard]] std::string BoneLabel(const SkeletonComponent& InSkeleton, const Skeleton::SkeletonNameTable& InNames,
                                    std::uint16_t InBone)
{
    const std::string_view ScopedName = SkeletonNameTableGet(InNames, InBone);
    if (!ScopedName.empty())
    {
        return std::string(ScopedName);
    }
    char ScopedBuffer[64]{};
    std::snprintf(ScopedBuffer, sizeof(ScopedBuffer), "Bone %u (0x%016llX)", static_cast<unsigned>(InBone),
                  static_cast<unsigned long long>(InSkeleton.BoneNameHashes[InBone]));
    return ScopedBuffer;
}

[[nodiscard]] bool DrawBoneNode(Ecs::Registry& InRegistry, Entity InEntity, SkeletonComponent& InSkeleton,
                                SkeletonInspectorState& InState, std::uint16_t InBone)
{
    if (InBone >= InSkeleton.BoneCount)
    {
        return false;
    }

    bool ScopedHasChildren = false;
    for (std::uint16_t ScopedChild = 0; ScopedChild < InSkeleton.BoneCount; ++ScopedChild)
    {
        if (InSkeleton.ParentIndices[ScopedChild] == static_cast<std::int16_t>(InBone))
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

    const std::string ScopedLabel = BoneLabel(InSkeleton, InState.names, InBone);
    const bool ScopedOpen = ImGui::TreeNodeEx("##bone", ScopedFlags, "%s", ScopedLabel.c_str());
    if (ImGui::IsItemClicked())
    {
        // Keep the existing editor selection model: clicking a bone selects its
        InRegistry.clear<EditorSelected>();
        InRegistry.emplace<EditorSelected>(InEntity);
    }

    bool ScopedEdited = false;
    if (ScopedOpen)
    {
        char ScopedNameBuffer[Skeleton::kFesklMaxBoneNameLength + 1]{};
        const std::string_view ScopedCurrentName = SkeletonNameTableGet(InState.names, InBone);
        std::snprintf(ScopedNameBuffer, sizeof(ScopedNameBuffer), "%.*s", static_cast<int>(ScopedCurrentName.size()),
                      ScopedCurrentName.empty() ? "" : ScopedCurrentName.data());
        if (ImGui::InputText("bone name", ScopedNameBuffer, sizeof(ScopedNameBuffer)))
        {
            if (SkeletonNameTableSet(InState.names, InBone, ScopedNameBuffer))
            {
                InSkeleton.BoneNameHashes[InBone] = BoneNameHash(ScopedNameBuffer);
                InState.dirty = true;
            }
        }
        ImGui::TextDisabled("index %u   hash 0x%016llX", static_cast<unsigned>(InBone),
                            static_cast<unsigned long long>(InSkeleton.BoneNameHashes[InBone]));

        // The consteval column table points at the first element of each SoA
        for (const BoneColumn& ScopedColumn : kBoneColumns)
        {
            void* ScopedAddress = reinterpret_cast<std::byte*>(&InSkeleton) + ScopedColumn.field.offset +
                                  static_cast<std::size_t>(InBone) * ScopedColumn.stride;
            ImGui::PushID(ScopedColumn.label);
            switch (ScopedColumn.field.kind)
            {
                case Reflect::FieldKind::Vector2D:
                {
                    auto* ScopedValue = static_cast<Vector2D*>(ScopedAddress);
                    Vector2D ScopedCopy = *ScopedValue;
                    if (ImGui::DragFloat2(ScopedColumn.label, &ScopedCopy.X, ScopedColumn.speed))
                    {
                        *ScopedValue = ScopedCopy;
                        ScopedEdited = true;
                    }
                    break;
                }
                case Reflect::FieldKind::Float:
                {
                    auto* ScopedValue = static_cast<float*>(ScopedAddress);
                    const bool ScopedIsScale = std::string_view(ScopedColumn.label) == "local scale";
                    if (ScopedIsScale)
                    {
                        if (ImGui::DragFloat(ScopedColumn.label, ScopedValue, ScopedColumn.speed, 0.0001f, 16.0f))
                        {
                            *ScopedValue = std::max(*ScopedValue, 0.0001f);
                            ScopedEdited = true;
                        }
                    }
                    else if (ImGui::DragFloat(ScopedColumn.label, ScopedValue, ScopedColumn.speed))
                    {
                        ScopedEdited = true;
                    }
                    break;
                }
                default:
                    break;
            }
            ImGui::PopID();
        }

        int ScopedParent = static_cast<int>(InSkeleton.ParentIndices[InBone]);
        // BoneLabel returns a temporary: keep a stable string alive for the
        std::string ScopedPreviewStorage =
            ScopedParent < 0 ? std::string("root")
                             : BoneLabel(InSkeleton, InState.names, static_cast<std::uint16_t>(ScopedParent));
        const char* ScopedPreview = ScopedPreviewStorage.c_str();
        if (ImGui::BeginCombo("parent", ScopedPreview))
        {
            if (ImGui::Selectable("root", ScopedParent < 0))
            {
                InSkeleton.ParentIndices[InBone] = -1;
                ScopedEdited = true;
            }
            for (std::uint16_t ScopedCandidate = 0; ScopedCandidate < InBone; ++ScopedCandidate)
            {
                const std::string ScopedCandidateLabel = BoneLabel(InSkeleton, InState.names, ScopedCandidate);
                if (ImGui::Selectable(ScopedCandidateLabel.c_str(), ScopedParent == static_cast<int>(ScopedCandidate)))
                {
                    InSkeleton.ParentIndices[InBone] = static_cast<std::int16_t>(ScopedCandidate);
                    ScopedEdited = true;
                }
            }
            ImGui::EndCombo();
        }

        if (ImGui::SmallButton("Rebind rest pose"))
        {
            SkeletonBindPose(InSkeleton);
            InState.dirty = true;
        }
        if (ScopedHasChildren)
        {
            ImGui::TreePop();
        }
    }
    ImGui::PopID();

    if (ScopedHasChildren)
    {
        for (std::uint16_t ScopedChild = 0; ScopedChild < InSkeleton.BoneCount; ++ScopedChild)
        {
            if (InSkeleton.ParentIndices[ScopedChild] == static_cast<std::int16_t>(InBone))
            {
                ScopedEdited |= DrawBoneNode(InRegistry, InEntity, InSkeleton, InState, ScopedChild);
            }
        }
    }
    return ScopedEdited;
}

void RefreshStateForEntity(Ecs::Registry& InRegistry, Assets::AssetDatabase* InAssetDb, Entity InEntity,
                           SkeletonInspectorState& InState)
{
    if (InState.entity == InEntity)
    {
        return;
    }
    InState.entity = InEntity;
    InState.loaded = false;
    InState.dirty = false;
    InState.contentHash = 0;
    InState.status.clear();
    InState.names = Skeleton::SkeletonNameTable{};
    InState.sprite = Skeleton::FesklSprite{};
    InState.path.clear();
    if (const auto* ScopedRef = InRegistry.try_get<SkeletonAssetRef>(InEntity); ScopedRef != nullptr)
    {
        if (ScopedRef->Asset.IsValid() && InAssetDb != nullptr)
        {
            InState.path.assign(InAssetDb->SourcePath(ScopedRef->Asset));
        }
        InState.contentHash = ScopedRef->ContentHash;
    }
    if (InState.path.empty())
    {
        InState.path = "skeletons/untitled.feskl";
    }
}

void SetStatus(SkeletonInspectorState& InState, std::string InMessage)
{
    InState.status = std::move(InMessage);
    LogInfo("editor", "skeleton: {}", InState.status);
}
}

void DrawSkeletonAssetInspector(Ecs::SystemContext& InContext, Entity InEntity, SkeletonInspectorState& InState)
{
    auto* ScopedSkeleton = InContext.registry.try_get<SkeletonComponent>(InEntity);
    if (ScopedSkeleton == nullptr)
    {
        return;
    }
    RefreshStateForEntity(InContext.registry, InContext.assetDb, InEntity, InState);
    SkinComponent* ScopedSkin = InContext.registry.try_get<SkinComponent>(InEntity);

    ImGui::PushID("skeleton_asset");
    InState.open = ImGui::CollapsingHeader("Skeleton asset (.feskl)", ImGuiTreeNodeFlags_DefaultOpen);
    if (!InState.open)
    {
        ImGui::PopID();
        return;
    }

    ImGui::Text("Bones %u / %u", static_cast<unsigned>(ScopedSkeleton->BoneCount), static_cast<unsigned>(kMaxBones));
    ImGui::SameLine();
    ImGui::TextDisabled(ScopedSkeleton->BindPoseValid ? "bind pose ok" : "bind pose dirty");
    if (InState.dirty)
    {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, HexColor(kThemeAccent));
        ImGui::TextUnformatted("modified");
        ImGui::PopStyleColor();
    }

    char ScopedPathBuffer[Skeleton::kFesklMaxAssetPath]{};
    std::snprintf(ScopedPathBuffer, sizeof(ScopedPathBuffer), "%s", InState.path.c_str());
    if (ImGui::InputText("asset path", ScopedPathBuffer, sizeof(ScopedPathBuffer)))
    {
        InState.path = ScopedPathBuffer;
    }

    const bool ScopedWritable = InContext.assets != nullptr && InContext.assetDb != nullptr;
    if (!ScopedWritable)
    {
        ImGui::TextDisabled("No VFS mounted: load/save disabled.");
    }

    ImGui::BeginDisabled(!ScopedWritable || InState.path.empty());
    if (ImGui::Button("Load .feskl"))
    {
        SkinComponent ScopedLoadedSkin;
        Skeleton::SkeletonNameTable ScopedNames;
        Skeleton::FesklSprite ScopedSprite;
        std::uint64_t ScopedHash = 0;
        std::string ScopedError;
        if (Skeleton::LoadSkeletonAsset(*InContext.assets, InState.path, *ScopedSkeleton, ScopedLoadedSkin, ScopedNames,
                                        ScopedSprite, &ScopedHash, &ScopedError))
        {
            InState.names = ScopedNames;
            InState.sprite = ScopedSprite;
            InState.loaded = true;
            InState.dirty = false;
            InState.contentHash = ScopedHash;
            if (ScopedLoadedSkin.VertexCount > 0)
            {
                InContext.registry.emplace_or_replace<SkinComponent>(InEntity, ScopedLoadedSkin);
                ScopedSkin = InContext.registry.try_get<SkinComponent>(InEntity);
            }
            auto& ScopedRef = InContext.registry.get_or_emplace<SkeletonAssetRef>(InEntity);
            const AssetId ScopedSkeletonId = InContext.assetDb->RegisterPath(InState.path, AssetKind::Skeleton);
            ScopedRef.Asset = InContext.assetDb->EnsureMeta(ScopedSkeletonId, AssetKind::Skeleton, InState.path);
            ScopedRef.ContentHash = static_cast<std::uint32_t>(ScopedHash);
            SetStatus(InState, "loaded '" + InState.path + "'");
        }
        else
        {
            InState.status = "load failed: " + ScopedError;
            LogWarn("editor", "skeleton load failed: {}", ScopedError);
        }
    }
    ImGui::SameLine();
    const bool ScopedSave = ImGui::Button(InState.dirty ? "Save .feskl *" : "Save .feskl");
    ImGui::EndDisabled();
    if (ScopedSave)
    {
        if (InState.names.count < ScopedSkeleton->BoneCount)
        {
            SkeletonNameTablePopulateFromIndices(InState.names, ScopedSkeleton->BoneCount);
            // Keep the runtime identity in sync with the names that are about
            for (std::uint16_t ScopedBone = 0; ScopedBone < ScopedSkeleton->BoneCount; ++ScopedBone)
            {
                ScopedSkeleton->BoneNameHashes[ScopedBone] =
                    BoneNameHash(SkeletonNameTableGet(InState.names, ScopedBone));
            }
        }
        Skeleton::SkeletonAssetSource ScopedSource;
        ScopedSource.skeleton = ScopedSkeleton;
        ScopedSource.skin = ScopedSkin;
        ScopedSource.names = &InState.names;
        ScopedSource.sprite = InState.sprite;
        ScopedSource.includeSkin = ScopedSkin != nullptr && ScopedSkin->VertexCount > 0;
        ScopedSource.includeSprite = ScopedSource.includeSkin;
        std::uint64_t ScopedHash = 0;
        std::string ScopedError;
        if (Skeleton::SaveSkeletonAsset(*InContext.assets, InState.path, ScopedSource, &ScopedHash, &ScopedError))
        {
            InState.loaded = true;
            InState.dirty = false;
            InState.contentHash = ScopedHash;
            auto& ScopedRef = InContext.registry.get_or_emplace<SkeletonAssetRef>(InEntity);
            const AssetId ScopedSkeletonId = InContext.assetDb->RegisterPath(InState.path, AssetKind::Skeleton);
            ScopedRef.Asset = InContext.assetDb->EnsureMeta(ScopedSkeletonId, AssetKind::Skeleton, InState.path);
            ScopedRef.ContentHash = static_cast<std::uint32_t>(ScopedHash);
            SetStatus(InState, "saved '" + InState.path + "'");
        }
        else
        {
            InState.status = "save failed: " + ScopedError;
            LogWarn("editor", "skeleton save failed: {}", ScopedError);
        }
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Rebind"))
    {
        SkeletonBindPose(*ScopedSkeleton);
        InState.dirty = true;
    }

    if (!InState.status.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, InState.dirty ? HexColor(kThemeAccent) : HexColor(kThemeMono));
        ImGui::TextWrapped("%s", InState.status.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::TextDisabled("content hash 0x%016llX", static_cast<unsigned long long>(InState.contentHash));

    ImGui::Separator();
    if (ImGui::SmallButton("Add bone"))
    {
        std::int16_t ScopedParent = -1;
        if (ScopedSkeleton->BoneCount > 0)
        {
            ScopedParent = static_cast<std::int16_t>(ScopedSkeleton->BoneCount - 1u);
        }
        char ScopedName[64]{};
        std::snprintf(ScopedName, sizeof(ScopedName), "bone_%u", static_cast<unsigned>(ScopedSkeleton->BoneCount));
        const std::int16_t ScopedCreated =
            SkeletonAddBone(*ScopedSkeleton, ScopedName, ScopedParent, Vector2D{0.0f, 0.0f}, 0.0f);
        if (ScopedCreated >= 0)
        {
            (void)SkeletonNameTableSet(InState.names, static_cast<std::uint32_t>(ScopedCreated), ScopedName);
            ScopedSkeleton->BoneNameHashes[ScopedCreated] = BoneNameHash(ScopedName);
            SkeletonBindPose(*ScopedSkeleton);
            InState.dirty = true;
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("parents must precede children");

    bool ScopedEdited = false;
    for (std::uint16_t ScopedBone = 0; ScopedBone < ScopedSkeleton->BoneCount; ++ScopedBone)
    {
        if (ScopedSkeleton->ParentIndices[ScopedBone] < 0)
        {
            ScopedEdited |= DrawBoneNode(InContext.registry, InEntity, *ScopedSkeleton, InState, ScopedBone);
        }
    }
    if (ScopedEdited)
    {
        // The local pose is the rest pose in this panel: rebinding once after
        SkeletonBindPose(*ScopedSkeleton);
        InState.dirty = true;
    }

    if (ScopedSkin != nullptr)
    {
        ImGui::Separator();
        ImGui::Text("Skin: %u / %u vertices, %u influences", static_cast<unsigned>(ScopedSkin->VertexCount),
                    static_cast<unsigned>(kMaxSkinVertices), static_cast<unsigned>(kMaxBoneInfluences));
    }

    ImGui::PopID();
}
}
