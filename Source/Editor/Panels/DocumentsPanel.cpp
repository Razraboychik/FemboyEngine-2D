// FemboyEngine2D - editor viewer for VFS text documents.
#include "Editor/Panels/DocumentsPanel.hpp"

#include "Editor/Infrastructure/EditorTypography.hpp"

#include "Engine/Core/Log.hpp"
#include "Engine/ECS/SystemContext.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <span>
#include <string>
#include <string_view>

namespace Fby::Editor
{

void DocumentsPanel::Draw(Ecs::SystemContext& InContext, bool* InOpen)
{
    if (InOpen != nullptr && !*InOpen)
    {
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(760.0f, 620.0f), ImGuiCond_FirstUseEver);
    if (!BeginEditorPanel("Documents", InOpen))
    {
        EndEditorPanel();
        return;
    }

    ImGui::TextDisabled("Opens .txt/.text through the VFS and paginates by complete lines.");
    ImGui::SetNextItemWidth(520.0f);
    (void)ImGui::InputText("Path", MPath.data(), MPath.size());
    ImGui::SameLine();
    if (ImGui::Button("Open"))
    {
        OpenDocument(InContext);
    }
    ImGui::SameLine();
    if (ImGui::Button("Close"))
    {
        CloseDocument();
    }

    if (!MStatus.empty())
    {
        ImGui::TextWrapped("%s", MStatus.c_str());
    }

    if (!MHandle.IsValid())
    {
        ImGui::Separator();
        ImGui::TextDisabled("No document is open.");
        EndEditorPanel();
        return;
    }

    Fby::Documents::DocumentInfo ScopedInfo;
    if (MDocuments.GetInfo(MHandle, &ScopedInfo))
    {
        ImGui::Separator();
        ImGui::Text("%.*s", static_cast<int>(std::strlen(ScopedInfo.Title.data())), ScopedInfo.Title.data());
        ImGui::TextDisabled("%u page(s), %u line(s), %llu byte(s)", ScopedInfo.PageCount, ScopedInfo.LineCount,
                            static_cast<unsigned long long>(ScopedInfo.ByteSize));
    }
    ImGui::SameLine();
    if (ImGui::Button("Prev") && MPage > 0)
    {
        --MPage;
    }
    ImGui::SameLine();
    if (ImGui::Button("Next") && MPage + 1u < MPageCount)
    {
        ++MPage;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("page %u / %u", MPage + 1u, MPageCount);

    if (MPage >= MPageCount)
    {
        MPage = MPageCount == 0 ? 0 : MPageCount - 1u;
    }
    std::uint32_t ScopedBytes = 0;
    if (!MDocuments.PageText(MHandle, MPage, std::span<char>(MPageBuffer.data(), MPageBuffer.size() - 1u),
                             &ScopedBytes))
    {
        ImGui::TextDisabled("The page could not be read.");
        EndEditorPanel();
        return;
    }
    MPageBuffer[std::min<std::size_t>(ScopedBytes, MPageBuffer.size() - 1u)] = '\0';
    ImGui::Separator();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(MPageBuffer.data(),
                           MPageBuffer.data() + std::min<std::size_t>(ScopedBytes, MPageBuffer.size() - 1u));
    ImGui::PopTextWrapPos();
    EndEditorPanel();
}

void DocumentsPanel::OpenDocument(Ecs::SystemContext& InContext)
{
    CloseDocument();
    if (MPath[0] == '\0')
    {
        MStatus = "Enter a document path first.";
        return;
    }
    MDocuments.SetVfs(InContext.assets);
    Fby::Documents::DocumentError ScopedError = Fby::Documents::DocumentError::None;
    const Fby::Documents::DocumentHandle ScopedHandle = MDocuments.Open(MPath.data(), &ScopedError);
    if (!ScopedHandle.IsValid())
    {
        MStatus = std::string("Could not open the document (") +
                  std::to_string(static_cast<unsigned>(ScopedError)) + ").";
        return;
    }
    MHandle = ScopedHandle;
    MPage = 0;
    MPageCount = MDocuments.PageCount(MHandle);
    MStatus.clear();
    Fby::LogInfo("documents", "opened '{}' ({} pages)", MPath.data(), MPageCount);
}

void DocumentsPanel::CloseDocument()
{
    if (MHandle.IsValid())
    {
        (void)MDocuments.Close(MHandle);
        MHandle = Fby::Documents::DocumentHandle{};
    }
    MPage = 0;
    MPageCount = 0;
}

}
