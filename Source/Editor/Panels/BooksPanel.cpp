// FemboyEngine2D - editor reader for cooked .fbybook assets.
#include "Editor/Panels/BooksPanel.hpp"

#include "Editor/Infrastructure/EditorTypography.hpp"

#include "Engine/Assets/VFS.hpp"
#include "Engine/ECS/SystemContext.hpp"

#include <imgui.h>

#include <cstddef>
#include <fstream>
#include <cstring>
#include <iterator>
#include <string>
#include <string_view>

namespace Fby::Editor
{

void BooksPanel::Draw(Ecs::SystemContext& InContext, bool* InOpen)
{
    if (InOpen != nullptr && !*InOpen)
    {
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(720.0f, 620.0f), ImGuiCond_FirstUseEver);
    if (!BeginEditorPanel("Books", InOpen))
    {
        EndEditorPanel();
        return;
    }
    (void)InContext;

    ImGui::TextDisabled("Reads a cooked .fbybook file from disk and paginates it with BookSession.");
    ImGui::SetNextItemWidth(420.0f);
    (void)ImGui::InputText("Book path", MPath.data(), MPath.size());
    ImGui::SameLine();
    if (ImGui::Button("Open"))
    {
        Load();
    }
    if (!MStatus.empty())
    {
        ImGui::TextWrapped("%s", MStatus.c_str());
    }
    if (!MBook.IsOpen())
    {
        EndEditorPanel();
        return;
    }

    ImGui::Separator();
    ImGui::Text("page %u / %u", MBook.CurrentPage() + 1u, MBook.PageCount());
    ImGui::SameLine();
    if (ImGui::Button("Previous"))
    {
        (void)MBook.PreviousPage();
    }
    ImGui::SameLine();
    if (ImGui::Button("Next"))
    {
        (void)MBook.NextPage();
    }
    Text::BookPage ScopedPage;
    if (!MBook.Page(MBook.CurrentPage(), &ScopedPage))
    {
        ImGui::TextDisabled("The current page is unavailable.");
        EndEditorPanel();
        return;
    }
    ImGui::Separator();
    if (ImGui::BeginChild("##book_page", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders))
    {
        for (std::uint32_t ScopedBlock = 0; ScopedBlock < ScopedPage.BlockCount; ++ScopedBlock)
        {
            Text::BookBlockView ScopedView;
            if (!MBook.Reader().Block(ScopedPage.FirstBlock + ScopedBlock, &ScopedView))
            {
                continue;
            }
            ImGui::PushTextWrapPos();
            ImGui::TextWrapped("%.*s", static_cast<int>(ScopedView.Text.size()), ScopedView.Text.data());
            ImGui::PopTextWrapPos();
        }
    }
    ImGui::EndChild();
    EndEditorPanel();
}

void BooksPanel::Load()
{
    MStatus.clear();
    MBook.Close();
    MBytes.clear();
    if (MPath[0] == '\0')
    {
        MStatus = "Enter a .fbybook path first.";
        return;
    }
    std::ifstream ScopedFile(MPath.data(), std::ios::binary);
    if (!ScopedFile)
    {
        MStatus = "Cannot open the book file.";
        return;
    }
    const std::vector<char> ScopedRaw((std::istreambuf_iterator<char>(ScopedFile)), std::istreambuf_iterator<char>());
    MBytes.resize(ScopedRaw.size());
    std::memcpy(MBytes.data(), ScopedRaw.data(), ScopedRaw.size());
    if (MBytes.empty())
    {
        MStatus = "The book file is empty.";
        return;
    }
    const std::span<const std::byte> ScopedBytes(MBytes.data(), MBytes.size());
    std::string ScopedError;
    if (!MBook.Open(ScopedBytes, Text::BookViewport{620.0f, 480.0f}, MLayout, &ScopedError))
    {
        MStatus = "Book failed: " + ScopedError;
        return;
    }
    MStatus = "loaded " + std::string(MPath.data());
}

}
