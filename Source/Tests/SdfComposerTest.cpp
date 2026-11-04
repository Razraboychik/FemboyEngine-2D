// FemboyEngine2D - SDF composer tests.
#include "Engine/Physics/SdfComposer.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

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

[[nodiscard]] Fby::SdfShape MakeCircle(float InRadius)
{
    Fby::SdfShape ScopedShape;
    ScopedShape.SetType(Fby::SdfShapeType::Circle);
    ScopedShape.Radius = InRadius;
    return ScopedShape;
}

void TestUnion()
{
    Fby::SdfComposer ScopedComposer;
    Check(ScopedComposer.Add(Fby::SdfComposeOp::Union, MakeBox(10.0f, 10.0f), Fby::Vector2D{0.0f, 0.0f}),
          "add the union box");
    Check(ScopedComposer.Add(Fby::SdfComposeOp::Union, MakeCircle(5.0f), Fby::Vector2D{30.0f, 0.0f}),
          "add the union circle");
    Check(ScopedComposer.LayerCount() == 2, "both union layers are recorded");
    Check(ScopedComposer.Evaluate(Fby::Vector2D{0.0f, 0.0f}) < 0.0f, "the box interior is solid");
    Check(ScopedComposer.Evaluate(Fby::Vector2D{30.0f, 0.0f}) < 0.0f, "the circle interior is solid");
    Check(ScopedComposer.Evaluate(Fby::Vector2D{15.0f, 0.0f}) > 0.0f, "the gap between them stays empty");
}

void TestSubtract()
{
    Fby::SdfComposer ScopedComposer;
    Check(ScopedComposer.Add(Fby::SdfComposeOp::Union, MakeBox(10.0f, 10.0f)), "add the subtract base");
    Check(ScopedComposer.Add(Fby::SdfComposeOp::Subtract, MakeCircle(4.0f)),
          "add the subtract circle");
    Check(ScopedComposer.Evaluate(Fby::Vector2D{0.0f, 0.0f}) > 0.0f, "the carved centre is empty");
    Check(ScopedComposer.Evaluate(Fby::Vector2D{8.0f, 0.0f}) < 0.0f, "the rim keeps its material");

    // Re-adding the box after the carve fills the hole: later layers win.
    Check(ScopedComposer.Add(Fby::SdfComposeOp::Union, MakeBox(10.0f, 10.0f)), "re-add the base");
    Check(ScopedComposer.Evaluate(Fby::Vector2D{0.0f, 0.0f}) < 0.0f, "a later union fills the hole back");
}

void TestIntersect()
{
    Fby::SdfComposer ScopedComposer;
    Check(ScopedComposer.Add(Fby::SdfComposeOp::Union, MakeBox(10.0f, 10.0f)), "add the intersect base");
    Check(ScopedComposer.Add(Fby::SdfComposeOp::Intersect, MakeBox(10.0f, 10.0f), Fby::Vector2D{5.0f, 0.0f}),
          "add the intersect shape");
    Check(ScopedComposer.Evaluate(Fby::Vector2D{5.0f, 0.0f}) < 0.0f, "the overlap is solid");
    Check(ScopedComposer.Evaluate(Fby::Vector2D{-8.0f, 0.0f}) > 0.0f, "the left-only region is cut away");
    Check(ScopedComposer.Evaluate(Fby::Vector2D{14.0f, 0.0f}) > 0.0f, "the right-only region is cut away");
}

void TestBake()
{
    Fby::SdfComposer ScopedComposer;
    Check(ScopedComposer.Add(Fby::SdfComposeOp::Union, MakeBox(12.0f, 8.0f)), "add the baked box");
    Check(ScopedComposer.Add(Fby::SdfComposeOp::Subtract, MakeCircle(3.0f)), "add the baked hole");
    std::vector<float> ScopedSamples;
    Fby::SdfField ScopedField;
    Check(ScopedComposer.Bake(Fby::Vector2D{-16.0f, -16.0f}, Fby::Vector2D{1.0f, 1.0f}, 32, 32, &ScopedSamples,
                              &ScopedField),
          "bake the composition");
    Check(ScopedField.Valid(), "the baked field is valid");
    bool ScopedMatches = true;
    for (std::uint32_t ScopedY = 0; ScopedY < 32; ++ScopedY)
    {
        for (std::uint32_t ScopedX = 0; ScopedX < 32; ++ScopedX)
        {
            const Fby::Vector2D ScopedPoint{-16.0f + static_cast<float>(ScopedX), -16.0f + static_cast<float>(ScopedY)};
            const float ScopedBaked = ScopedSamples[static_cast<std::size_t>(ScopedY) * 32 + ScopedX];
            if (ScopedBaked != ScopedComposer.Evaluate(ScopedPoint))
            {
                ScopedMatches = false;
            }
        }
    }
    Check(ScopedMatches, "the bake matches the evaluation");
    Check(ScopedField.Sample(Fby::Vector2D{0.0f, 0.0f}) > 0.0f, "the baked hole samples empty");
}

void TestCapacity()
{
    Fby::SdfComposer ScopedComposer;
    const Fby::SdfShape ScopedShape = MakeCircle(1.0f);
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < Fby::SdfComposer::kMaxLayers; ++ScopedIndex)
    {
        Check(ScopedComposer.Add(Fby::SdfComposeOp::Union, ScopedShape, Fby::Vector2D{0.0f, 0.0f}),
              "layers fill to capacity");
    }
    Check(!ScopedComposer.Add(Fby::SdfComposeOp::Union, ScopedShape), "the layer cap is enforced");
    Fby::SdfComposer ScopedInvalid;
    Fby::SdfShape ScopedBad;
    ScopedBad.Type = 999;
    Check(!ScopedInvalid.Add(Fby::SdfComposeOp::Union, ScopedBad), "an invalid shape is rejected");
    Check(ScopedInvalid.IsEmpty(), "the rejected shape leaves the composer empty");
    Check(Fby::SdfComposer{}.Evaluate(Fby::Vector2D{0.0f, 0.0f}) > 0.0f, "an empty composer is empty space");
}
}

int main()
{
    TestUnion();
    TestSubtract();
    TestIntersect();
    TestBake();
    TestCapacity();
    if (GFailures != 0)
    {
        std::fprintf(stderr, "%d sdf composer test(s) failed\n", GFailures);
        return 1;
    }
    std::printf("sdf_composer: OK\n");
    return 0;
}
