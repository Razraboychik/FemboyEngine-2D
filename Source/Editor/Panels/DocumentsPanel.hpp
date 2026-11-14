// FemboyEngine2D - editor viewer for VFS text documents.
#pragma once

#include "Engine/Documents/Document.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace Fby::Ecs
{
struct SystemContext;
}

namespace Fby::Editor
{

class DocumentsPanel
{
public:
    void Draw(Ecs::SystemContext& InContext, bool* InOpen);

private:
    void OpenDocument(Ecs::SystemContext& InContext);
    void CloseDocument();

    Fby::Documents::DocumentSystem MDocuments;
    Fby::Documents::DocumentHandle MHandle{};
    std::array<char, 512> MPath{};
    std::array<char, 32768> MPageBuffer{};
    std::uint32_t MPage = 0;
    std::uint32_t MPageCount = 0;
    std::string MStatus;
};

}
