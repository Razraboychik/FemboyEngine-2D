// FemboyEngine2D - transactional ImNodes canvas.
#pragma once

#include "Editor/Infrastructure/CommandStack.hpp"

#include "imgui.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>

namespace Fby::Editor
{

// The maximum node/link count the canvas itself can select in one deletion.
inline constexpr std::uint32_t kMaxCanvasNodes = 192;
inline constexpr std::uint32_t kMaxCanvasLinks = 384;
inline constexpr std::uint32_t kCanvasStatusBytes = 256;

struct CanvasModelOps
{
    void* user = nullptr;

    [[nodiscard]] std::uint32_t (*nodeCount)(void* InUser) noexcept = nullptr;
    [[nodiscard]] const char* (*nodeTitle)(void* InUser, std::uint16_t InNode) noexcept = nullptr;
    [[nodiscard]] ImVec2 (*nodePosition)(void* InUser, std::uint16_t InNode) noexcept = nullptr;
    void (*setNodePosition)(void* InUser, std::uint16_t InNode, ImVec2 InPosition) noexcept = nullptr;
    [[nodiscard]] std::uint8_t (*inputCount)(void* InUser, std::uint16_t InNode) noexcept = nullptr;
    [[nodiscard]] const char* (*inputName)(void* InUser, std::uint16_t InNode, std::uint8_t InPin) noexcept = nullptr;
    [[nodiscard]] std::uint8_t (*outputCount)(void* InUser, std::uint16_t InNode) noexcept = nullptr;
    [[nodiscard]] const char* (*outputName)(void* InUser, std::uint16_t InNode, std::uint8_t InPin) noexcept = nullptr;

    [[nodiscard]] std::uint32_t (*linkCount)(void* InUser) noexcept = nullptr;
    void (*linkEndpoints)(void* InUser, std::uint32_t InLink, std::uint16_t* InFromNode, std::uint8_t* InFromPin,
                          std::uint16_t* InToNode, std::uint8_t* InToPin) noexcept = nullptr;
    [[nodiscard]] bool (*connect)(void* InUser, std::uint16_t InToNode, std::uint8_t InToPin, std::uint16_t InFromNode,
                                  std::uint8_t InFromPin, std::string* InError) = nullptr;
    void (*disconnect)(void* InUser, std::uint16_t InToNode, std::uint8_t InToPin) noexcept = nullptr;
    [[nodiscard]] bool (*deleteNodes)(void* InUser, std::span<const std::uint16_t> InNodes,
                                      std::string* InError) = nullptr;

    [[nodiscard]] std::uint32_t (*addableCount)(void* InUser) noexcept = nullptr;
    [[nodiscard]] const char* (*addableName)(void* InUser, std::uint32_t InAddable) noexcept = nullptr;
    [[nodiscard]] bool (*addNode)(void* InUser, std::uint32_t InAddable, ImVec2 InPosition, std::uint16_t* OutNode,
                                  std::string* InError) = nullptr;

    // Optional: draw compact constant/field readouts inside the node body.
    void (*drawNodeBody)(void* InUser, std::uint16_t InNode) noexcept = nullptr;
    // Optional: panel-specific toolbar controls (Apply Shader, Compile, AOT).
    void (*drawPanelTools)(void* InUser) noexcept = nullptr;
};

class NodeCanvasPanel
{
public:
    NodeCanvasPanel() = default;
    ~NodeCanvasPanel();

    NodeCanvasPanel(const NodeCanvasPanel&) = delete;
    NodeCanvasPanel& operator=(const NodeCanvasPanel&) = delete;

    // `codec` must be a CommandStack external buffer covering the whole graph.
    void Initialize(CommandStack& InCommands, std::uint16_t InCodec) noexcept;
    void Shutdown() noexcept;
    void SetModel(const CanvasModelOps& InModel) noexcept;
    void SetModelFonts(ImFont* InTitleFont, ImFont* InPinFontInput, ImFont* InPinFontOutput,
                       ImFont* InPanelTitleFont) noexcept;
    [[nodiscard]] bool Ready() const noexcept;

    // Draws the window and processes one ImNodes frame. `open` is the usual
    void Draw(const char* InWindowTitle, bool* InOpen);

    // Public document-edit hooks for side inspectors (material constants, field
    [[nodiscard]] bool BeginDocumentEdit(const char* InLabel) noexcept;
    void RecordDocumentEdit() noexcept;
    void CommitDocumentEdit() noexcept;
    [[nodiscard]] bool Editing() const noexcept
    {
        return MEditing;
    }

    // Valid after Draw() until the next one. 0xFFFF when nothing is selected.
    [[nodiscard]] std::uint16_t SelectedNode() const noexcept
    {
        return MSelectedNode;
    }
    [[nodiscard]] const char* Status() const noexcept
    {
        return MStatus;
    }

    // Draws a highlight ring around one node on the next frame. Used by the
    // script debugger to show which graph node owns the paused instruction.
    static constexpr std::uint32_t kNoHighlightNode = 0xFFFFFFFFu;
    void SetHighlightNode(std::uint32_t InNode) noexcept
    {
        MHighlightNode = InNode;
    }

private:
    void DrawToolbar();
    void ProcessEvents();
    void SetStatus(const char* InFormat, ...) noexcept;

    CommandStack* MCommands = nullptr;
    CanvasModelOps MModel{};
    ImFont* MNodeTitleFont = nullptr;
    ImFont* MInputPinFont = nullptr;
    ImFont* MOutputPinFont = nullptr;
    ImFont* MPanelTitleFont = nullptr;
    std::uint16_t MCodec = 0xFFFFu;
    std::uint32_t MCodecSize = 0;
    bool MInitialized = false;
    bool MEditing = false;
    bool MCanvasDrag = false;
    std::uint16_t MSelectedNode = 0xFFFFu;
    std::uint32_t MHighlightNode = kNoHighlightNode;

    std::array<std::byte, kMaxTrackedComponentBytes> MBefore{};
    std::array<std::byte, kMaxTrackedComponentBytes> MAfter{};
    char MStatus[kCanvasStatusBytes] = {};
    void* MImNodesContext = nullptr;
};
}
