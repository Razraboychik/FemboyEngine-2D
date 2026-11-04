// FemboyEngine2D - composer edit session: undo/redo over layer edits.
#pragma once

#include "Engine/Physics/SdfComposer.hpp"

#include <cstdint>
#include <vector>

namespace Fby
{

// Edit session for the composer panel. Every mutation is recorded as a full layer
// list, which is cheap (a layer is a 40-byte POD) and guarantees that undo returns
// the exact previous field instead of an approximated one.
class SdfComposerSession
{
public:
    inline static constexpr std::uint32_t kMaxUndoSteps = 128;

    [[nodiscard]] SdfComposer& Composer() noexcept
    {
        return MComposer;
    }
    [[nodiscard]] const SdfComposer& Composer() const noexcept
    {
        return MComposer;
    }

    [[nodiscard]] bool AddLayer(SdfComposeOp InOp, const SdfShape& InShape, Vector2D InCenter = {}) noexcept
    {
        if (!MComposer.Add(InOp, InShape, InCenter))
        {
            return false;
        }
        PushUndo();
        return true;
    }

    [[nodiscard]] bool RemoveLayer(std::uint32_t InIndex) noexcept
    {
        if (InIndex >= MComposer.LayerCount())
        {
            return false;
        }
        std::vector<SdfComposeLayer> ScopedLayers(MComposer.Layers().begin(), MComposer.Layers().end());
        ScopedLayers.erase(ScopedLayers.begin() + InIndex);
        MComposer.Clear();
        for (const SdfComposeLayer& ScopedLayer : ScopedLayers)
        {
            (void)MComposer.Add(ScopedLayer.Op, ScopedLayer.Shape, ScopedLayer.Center);
        }
        PushUndo();
        return true;
    }

    [[nodiscard]] bool CanUndo() const noexcept
    {
        return !MUndo.empty();
    }
    [[nodiscard]] bool CanRedo() const noexcept
    {
        return !MRedo.empty();
    }

    [[nodiscard]] bool Undo() noexcept
    {
        if (MUndo.empty())
        {
            return false;
        }
        MRedo.push_back(Snapshot());
        Restore(MUndo.back());
        MUndo.pop_back();
        // The session snapshot must follow the restored state, or the next edit
        // would push a stale pre-state.
        MNextUndoSnapshot = Snapshot();
        return true;
    }

    [[nodiscard]] bool Redo() noexcept
    {
        if (MRedo.empty())
        {
            return false;
        }
        MUndo.push_back(Snapshot());
        Restore(MRedo.back());
        MRedo.pop_back();
        MNextUndoSnapshot = Snapshot();
        return true;
    }

    [[nodiscard]] std::uint32_t UndoDepth() const noexcept
    {
        return static_cast<std::uint32_t>(MUndo.size());
    }
    [[nodiscard]] std::uint32_t RedoDepth() const noexcept
    {
        return static_cast<std::uint32_t>(MRedo.size());
    }

private:
    [[nodiscard]] std::vector<SdfComposeLayer> Snapshot() const
    {
        return std::vector<SdfComposeLayer>(MComposer.Layers().begin(), MComposer.Layers().end());
    }

    void Restore(const std::vector<SdfComposeLayer>& InLayers) noexcept
    {
        MComposer.Clear();
        for (const SdfComposeLayer& ScopedLayer : InLayers)
        {
            (void)MComposer.Add(ScopedLayer.Op, ScopedLayer.Shape, ScopedLayer.Center);
        }
    }

    void PushUndo()
    {
        // The snapshot is the state *before* the mutation, which is what Undo
        // restores; the redo branch is invalidated by every new edit.
        MRedo.clear();
        if (MUndo.size() >= kMaxUndoSteps)
        {
            MUndo.erase(MUndo.begin());
        }
        MUndo.push_back(MNextUndoSnapshot);
        MNextUndoSnapshot = Snapshot();
    }

    SdfComposer MComposer;
    std::vector<std::vector<SdfComposeLayer>> MUndo;
    std::vector<std::vector<SdfComposeLayer>> MRedo;
    std::vector<SdfComposeLayer> MNextUndoSnapshot;
};

}
