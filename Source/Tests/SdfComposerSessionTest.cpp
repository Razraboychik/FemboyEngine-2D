// FemboyEngine2D - composer undo/redo session tests.
#include "Engine/Physics/SdfComposerSession.hpp"

#include <cmath>
#include <cstdio>

namespace
{
int GFailures = 0;

void Check(bool InCondition, const char* InMessage)
{
    if (!InCondition)
    {
        std::fprintf(stderr, "FAIL: %s\n", InMessage);
        ++GFailures;
    }
}

[[nodiscard]] Fby::SdfShape MakeBox(float InHalfX, float InHalfY)
{
    Fby::SdfShape ScopedShape;
    ScopedShape.SetType(Fby::SdfShapeType::Box);
    ScopedShape.HalfExtents = Fby::Vector2D{InHalfX, InHalfY};
    return ScopedShape;
}

void TestUndoRedo()
{
    Fby::SdfComposerSession ScopedSession;
    Check(!ScopedSession.CanUndo(), "a new session has nothing to undo");
    Check(!ScopedSession.Undo(), "undo on an empty history fails");
    Check(!ScopedSession.Redo(), "redo on an empty history fails");

    Check(ScopedSession.AddLayer(Fby::SdfComposeOp::Union, MakeBox(10.0f, 10.0f)), "add the first layer");
    Check(ScopedSession.AddLayer(Fby::SdfComposeOp::Subtract, MakeBox(4.0f, 4.0f)), "add the carve layer");
    Check(ScopedSession.Composer().LayerCount() == 2, "two layers are live");
    Check(ScopedSession.Composer().Evaluate(Fby::Vector2D{0.0f, 0.0f}) > 0.0f, "the carve opened the centre");
    Check(ScopedSession.UndoDepth() == 2, "two undo steps are recorded");
    Check(!ScopedSession.CanRedo(), "no redo before an undo");

    Check(ScopedSession.Undo(), "undo the carve");
    Check(ScopedSession.Composer().LayerCount() == 1, "one layer after undo");
    Check(ScopedSession.Composer().Evaluate(Fby::Vector2D{0.0f, 0.0f}) < 0.0f, "undo restored the solid centre");
    Check(ScopedSession.CanRedo(), "redo is available after undo");

    Check(ScopedSession.Redo(), "redo the carve");
    Check(ScopedSession.Composer().LayerCount() == 2, "two layers after redo");
    Check(ScopedSession.Composer().Evaluate(Fby::Vector2D{0.0f, 0.0f}) > 0.0f, "redo restored the carve");
    Check(!ScopedSession.CanRedo(), "redo is consumed");
}

void TestRemoveAndBranching()
{
    Fby::SdfComposerSession ScopedSession;
    Check(ScopedSession.AddLayer(Fby::SdfComposeOp::Union, MakeBox(20.0f, 20.0f)), "add the base");
    Check(ScopedSession.AddLayer(Fby::SdfComposeOp::Union, MakeBox(4.0f, 4.0f), Fby::Vector2D{15.0f, 0.0f}),
          "add the second shape");
    Check(ScopedSession.RemoveLayer(1), "remove the second shape");
    Check(ScopedSession.Composer().LayerCount() == 1, "one layer after remove");
    Check(ScopedSession.Composer().Evaluate(Fby::Vector2D{15.0f, 0.0f}) < 0.0f,
          "the wide base still covers the removed spot");
    Check(!ScopedSession.RemoveLayer(7), "removing an out-of-range layer fails");

    // A new edit after undo discards the redo branch.
    Check(ScopedSession.Undo(), "undo the remove");
    Check(ScopedSession.Composer().LayerCount() == 2, "the remove is undone");
    Check(ScopedSession.CanRedo(), "the redo branch exists");
    Check(ScopedSession.AddLayer(Fby::SdfComposeOp::Subtract, MakeBox(2.0f, 2.0f)), "add after undo");
    Check(!ScopedSession.CanRedo(), "the new edit discards the redo branch");
}

void TestCapacity()
{
    Fby::SdfComposerSession ScopedSession;
    const Fby::SdfShape ScopedShape = MakeBox(4.0f, 4.0f);
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < Fby::SdfComposerSession::kMaxUndoSteps + 8; ++ScopedIndex)
    {
        if (!ScopedSession.AddLayer(Fby::SdfComposeOp::Union, ScopedShape))
        {
            Check(false, "the composer layer cap must outlive the undo cap");
            break;
        }
    }
    Check(ScopedSession.UndoDepth() == Fby::SdfComposerSession::kMaxUndoSteps, "the undo stack is capped");
}
}

int main()
{
    TestUndoRedo();
    TestRemoveAndBranching();
    TestCapacity();
    if (GFailures != 0)
    {
        std::fprintf(stderr, "%d composer session test(s) failed\n", GFailures);
        return 1;
    }
    std::printf("sdf_composer_session: OK\n");
    return 0;
}
