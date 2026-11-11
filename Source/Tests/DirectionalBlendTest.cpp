// FemboyEngine2D - directional blend space tests (no registry, no ImGui).
#include "Engine/ECS/DirectionalBlend.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace
{

int GFailures = 0;

void Check(bool InCondition, const char* InMessage)
{
    if (!InCondition)
    {
        std::printf("FAIL: %s\n", InMessage);
        ++GFailures;
    }
}

void CheckClose(float InActual, float InExpected, float InTolerance, const char* InMessage)
{
    if (std::fabs(InActual - InExpected) > InTolerance)
    {
        std::printf("FAIL: %s (expected %f, got %f)\n", InMessage, static_cast<double>(InExpected),
                    static_cast<double>(InActual));
        ++GFailures;
    }
}

using Fby::Animation::BakeDirectionalBlendSpace;
using Fby::Animation::DirectionalBlendSample;
using Fby::Animation::DirectionalBlendSpaceDocument;
using Fby::Animation::DirectionalBlendTable;
using Fby::Animation::DirectionalBlendTopology;
using Fby::Animation::EvaluateDirectionalBlend;
using Fby::Animation::ValidateDirectionalBlendSpace;

float WeightOf(const DirectionalBlendSample& InSample, std::uint32_t InClipId)
{
    for (std::uint8_t ScopedIndex = 0; ScopedIndex < InSample.Count; ++ScopedIndex)
    {
        if (InSample.Weights[ScopedIndex].ClipId == InClipId)
        {
            return InSample.Weights[ScopedIndex].Weight;
        }
    }
    return 0.0f;
}

float TotalWeight(const DirectionalBlendSample& InSample)
{
    float ScopedTotal = 0.0f;
    for (std::uint8_t ScopedIndex = 0; ScopedIndex < InSample.Count; ++ScopedIndex)
    {
        ScopedTotal += InSample.Weights[ScopedIndex].Weight;
    }
    return ScopedTotal;
}

DirectionalBlendSpaceDocument MakeEightWayRing(std::uint32_t InClipBase)
{
    DirectionalBlendSpaceDocument ScopedDocument;
    ScopedDocument.AnchorCount = 8;
    ScopedDocument.Topology = DirectionalBlendTopology::Ring;
    for (std::uint16_t ScopedIndex = 0; ScopedIndex < 8; ++ScopedIndex)
    {
        const float ScopedAngle = static_cast<float>(ScopedIndex) * 0.7853981633974483f;
        ScopedDocument.Anchors[ScopedIndex].Position = {std::cos(ScopedAngle), std::sin(ScopedAngle)};
        ScopedDocument.Anchors[ScopedIndex].ClipId = InClipBase + ScopedIndex;
    }
    return ScopedDocument;
}

DirectionalBlendSample SampleAt(const DirectionalBlendTable& InTable, float InAngle)
{
    DirectionalBlendSample ScopedSample;
    EvaluateDirectionalBlend(InTable, {std::cos(InAngle), std::sin(InAngle)}, &ScopedSample);
    return ScopedSample;
}

void TestEightWayRing()
{
    const DirectionalBlendSpaceDocument ScopedDocument = MakeEightWayRing(10);
    Check(ValidateDirectionalBlendSpace(ScopedDocument), "eight-way ring validates");
    DirectionalBlendTable ScopedTable;
    Check(BakeDirectionalBlendSpace(ScopedDocument, &ScopedTable), "eight-way ring bakes");
    Check(ScopedTable.AnchorCount == 8, "eight anchors baked");

    const DirectionalBlendSample ScopedEast = SampleAt(ScopedTable, 0.0f);
    Check(ScopedEast.Count == 1, "exact east heading resolves one source");
    CheckClose(WeightOf(ScopedEast, 10), 1.0f, 1.0e-4f, "east source weight");

    const DirectionalBlendSample ScopedNorth = SampleAt(ScopedTable, 1.5707963267948966f);
    Check(ScopedNorth.Count == 1 && ScopedNorth.Weights[0].ClipId == 12, "north heading resolves clip 12");

    const DirectionalBlendSample ScopedSouth = SampleAt(ScopedTable, 4.71238898038469f);
    Check(ScopedSouth.Count == 1 && ScopedSouth.Weights[0].ClipId == 16, "south heading resolves clip 16");

    const DirectionalBlendSample ScopedBetween = SampleAt(ScopedTable, 0.39269908169872414f);
    Check(ScopedBetween.Count == 2, "midway heading blends two sources");
    CheckClose(WeightOf(ScopedBetween, 10), 0.5f, 1.0e-4f, "east weight at 22.5 degrees");
    CheckClose(WeightOf(ScopedBetween, 11), 0.5f, 1.0e-4f, "north-east weight at 22.5 degrees");
    CheckClose(TotalWeight(ScopedBetween), 1.0f, 1.0e-4f, "ring weights sum to one");

    const DirectionalBlendSample ScopedWrapped = SampleAt(ScopedTable, 6.1086523819801535f);
    Check(ScopedWrapped.Count == 2, "heading past the last anchor wraps to the first");
    CheckClose(WeightOf(ScopedWrapped, 17), 0.2222222f, 1.0e-4f, "north-west weight near the wrap");
    CheckClose(WeightOf(ScopedWrapped, 10), 0.7777778f, 1.0e-4f, "east weight near the wrap");
    CheckClose(TotalWeight(ScopedWrapped), 1.0f, 1.0e-4f, "wrapped weights sum to one");
}

void TestRingNonUniform()
{
    DirectionalBlendSpaceDocument ScopedDocument;
    ScopedDocument.AnchorCount = 3;
    ScopedDocument.Anchors[0].Position = {1.0f, 0.0f};
    ScopedDocument.Anchors[0].ClipId = 1;
    ScopedDocument.Anchors[1].Position = {0.0f, 1.0f};
    ScopedDocument.Anchors[1].ClipId = 2;
    ScopedDocument.Anchors[2].Position = {-1.0f, 0.0f};
    ScopedDocument.Anchors[2].ClipId = 3;
    DirectionalBlendTable ScopedTable;
    Check(BakeDirectionalBlendSpace(ScopedDocument, &ScopedTable), "non-uniform ring bakes");

    const DirectionalBlendSample ScopedQuarter = SampleAt(ScopedTable, 0.7853981633974483f);
    CheckClose(WeightOf(ScopedQuarter, 1), 0.5f, 1.0e-4f, "non-uniform ring first neighbour");
    CheckClose(WeightOf(ScopedQuarter, 2), 0.5f, 1.0e-4f, "non-uniform ring second neighbour");

    const DirectionalBlendSample ScopedWrap = SampleAt(ScopedTable, 4.71238898038469f);
    CheckClose(WeightOf(ScopedWrap, 3), 0.5f, 1.0e-4f, "non-uniform ring wrap source");
    CheckClose(WeightOf(ScopedWrap, 1), 0.5f, 1.0e-4f, "non-uniform ring wrap target");
}

void TestRingDegenerateAndValidation()
{
    DirectionalBlendSpaceDocument ScopedEmpty;
    Check(!ValidateDirectionalBlendSpace(ScopedEmpty), "empty document is rejected");
    DirectionalBlendTable ScopedTable;
    Check(!BakeDirectionalBlendSpace(ScopedEmpty, &ScopedTable), "empty document does not bake");
    Check(ScopedTable.AnchorCount == 0, "failed bake leaves an empty table");

    DirectionalBlendSpaceDocument ScopedTooMany;
    ScopedTooMany.AnchorCount = static_cast<std::uint16_t>(Fby::Animation::kMaxDirectionalAnchors + 1);
    Check(!ValidateDirectionalBlendSpace(ScopedTooMany), "anchor count past the capacity is rejected");

    DirectionalBlendSpaceDocument ScopedZeroHeading;
    ScopedZeroHeading.AnchorCount = 1;
    ScopedZeroHeading.Anchors[0].Position = {0.0f, 0.0f};
    Check(!ValidateDirectionalBlendSpace(ScopedZeroHeading), "ring anchor without a heading is rejected");

    DirectionalBlendSpaceDocument ScopedNegativeScale;
    ScopedNegativeScale.AnchorCount = 1;
    ScopedNegativeScale.Anchors[0].WeightScale = -1.0f;
    Check(!ValidateDirectionalBlendSpace(ScopedNegativeScale), "negative weight scale is rejected");

    DirectionalBlendSpaceDocument ScopedSingle;
    ScopedSingle.AnchorCount = 1;
    ScopedSingle.Anchors[0].Position = {0.0f, 1.0f};
    ScopedSingle.Anchors[0].ClipId = 7;
    Check(BakeDirectionalBlendSpace(ScopedSingle, &ScopedTable), "single anchor bakes");
    const DirectionalBlendSample ScopedAny = SampleAt(ScopedTable, 2.3f);
    Check(ScopedAny.Count == 1 && ScopedAny.Weights[0].ClipId == 7, "single anchor answers every heading");
    CheckClose(ScopedAny.Weights[0].Weight, 1.0f, 1.0e-6f, "single anchor weight");

    DirectionalBlendSpaceDocument ScopedDuplicate;
    ScopedDuplicate.AnchorCount = 2;
    ScopedDuplicate.Anchors[0].Position = {1.0f, 0.0f};
    ScopedDuplicate.Anchors[0].ClipId = 1;
    ScopedDuplicate.Anchors[1].Position = {1.0f, 0.0f};
    ScopedDuplicate.Anchors[1].ClipId = 2;
    Check(BakeDirectionalBlendSpace(ScopedDuplicate, &ScopedTable), "duplicate headings bake");
    const DirectionalBlendSample ScopedTie = SampleAt(ScopedTable, 0.0f);
    Check(ScopedTie.Count == 1, "duplicate headings do not divide by zero");
    CheckClose(ScopedTie.Weights[0].Weight, 1.0f, 1.0e-6f, "duplicate heading weight");
}

DirectionalBlendSpaceDocument MakePlaneSquare()
{
    DirectionalBlendSpaceDocument ScopedDocument;
    ScopedDocument.AnchorCount = 4;
    ScopedDocument.Topology = DirectionalBlendTopology::Plane;
    ScopedDocument.Anchors[0].Position = {-1.0f, -1.0f};
    ScopedDocument.Anchors[0].ClipId = 1;
    ScopedDocument.Anchors[1].Position = {1.0f, -1.0f};
    ScopedDocument.Anchors[1].ClipId = 2;
    ScopedDocument.Anchors[2].Position = {1.0f, 1.0f};
    ScopedDocument.Anchors[2].ClipId = 3;
    ScopedDocument.Anchors[3].Position = {-1.0f, 1.0f};
    ScopedDocument.Anchors[3].ClipId = 4;
    return ScopedDocument;
}

void TestPlaneSquare()
{
    DirectionalBlendTable ScopedTable;
    Check(BakeDirectionalBlendSpace(MakePlaneSquare(), &ScopedTable), "plane square bakes");
    Check(ScopedTable.HullCount == 4, "plane hull keeps the square corners");
    Check(ScopedTable.TriangleCount == 2, "plane square fans into two triangles");

    DirectionalBlendSample ScopedCenter;
    EvaluateDirectionalBlend(ScopedTable, {0.0f, 0.0f}, &ScopedCenter);
    Check(ScopedCenter.Count == 2, "square center lands on a shared triangle edge");
    CheckClose(WeightOf(ScopedCenter, 1), 0.5f, 1.0e-4f, "square center first corner");
    CheckClose(WeightOf(ScopedCenter, 3), 0.5f, 1.0e-4f, "square center opposite corner");
    CheckClose(TotalWeight(ScopedCenter), 1.0f, 1.0e-4f, "square center weights sum to one");

    DirectionalBlendSample ScopedInside;
    EvaluateDirectionalBlend(ScopedTable, {0.25f, -0.75f}, &ScopedInside);
    Check(ScopedInside.Count == 3, "point inside a triangle keeps three barycentric sources");
    CheckClose(WeightOf(ScopedInside, 1), 0.375f, 1.0e-4f, "barycentric first corner");
    CheckClose(WeightOf(ScopedInside, 2), 0.5f, 1.0e-4f, "barycentric second corner");
    CheckClose(WeightOf(ScopedInside, 3), 0.125f, 1.0e-4f, "barycentric third corner");

    DirectionalBlendSample ScopedOutside;
    EvaluateDirectionalBlend(ScopedTable, {2.0f, 0.0f}, &ScopedOutside);
    CheckClose(WeightOf(ScopedOutside, 2), 0.5f, 1.0e-4f, "outside sample projects to the right edge");
    CheckClose(WeightOf(ScopedOutside, 3), 0.5f, 1.0e-4f, "outside sample projection weight");
    CheckClose(TotalWeight(ScopedOutside), 1.0f, 1.0e-4f, "outside sample weights sum to one");

    DirectionalBlendSample ScopedCorner;
    EvaluateDirectionalBlend(ScopedTable, {-2.0f, 2.0f}, &ScopedCorner);
    Check(ScopedCorner.Count == 1 && ScopedCorner.Weights[0].ClipId == 4, "outside corner clamps to one anchor");
}

void TestPlaneDegenerate()
{
    DirectionalBlendSpaceDocument ScopedSegment;
    ScopedSegment.AnchorCount = 2;
    ScopedSegment.Topology = DirectionalBlendTopology::Plane;
    ScopedSegment.Anchors[0].Position = {0.0f, 0.0f};
    ScopedSegment.Anchors[0].ClipId = 5;
    ScopedSegment.Anchors[1].Position = {10.0f, 0.0f};
    ScopedSegment.Anchors[1].ClipId = 6;
    DirectionalBlendTable ScopedTable;
    Check(BakeDirectionalBlendSpace(ScopedSegment, &ScopedTable), "two-point plane bakes");
    DirectionalBlendSample ScopedMiddle;
    EvaluateDirectionalBlend(ScopedTable, {5.0f, 3.0f}, &ScopedMiddle);
    CheckClose(WeightOf(ScopedMiddle, 5), 0.5f, 1.0e-4f, "segment projection first source");
    CheckClose(WeightOf(ScopedMiddle, 6), 0.5f, 1.0e-4f, "segment projection second source");

    DirectionalBlendSample ScopedBeyond;
    EvaluateDirectionalBlend(ScopedTable, {20.0f, 0.0f}, &ScopedBeyond);
    Check(ScopedBeyond.Count == 1 && ScopedBeyond.Weights[0].ClipId == 6, "segment projection clamps to the end");

    DirectionalBlendSpaceDocument ScopedCollinear;
    ScopedCollinear.AnchorCount = 3;
    ScopedCollinear.Topology = DirectionalBlendTopology::Plane;
    ScopedCollinear.Anchors[0].Position = {0.0f, 0.0f};
    ScopedCollinear.Anchors[0].ClipId = 1;
    ScopedCollinear.Anchors[1].Position = {5.0f, 0.0f};
    ScopedCollinear.Anchors[1].ClipId = 2;
    ScopedCollinear.Anchors[2].Position = {10.0f, 0.0f};
    ScopedCollinear.Anchors[2].ClipId = 3;
    Check(BakeDirectionalBlendSpace(ScopedCollinear, &ScopedTable), "collinear plane bakes");
    Check(ScopedTable.HullCount == 2, "collinear plane collapses to a segment");
    DirectionalBlendSample ScopedCollinearSample;
    EvaluateDirectionalBlend(ScopedTable, {2.5f, 0.0f}, &ScopedCollinearSample);
    CheckClose(WeightOf(ScopedCollinearSample, 1), 0.75f, 1.0e-4f, "collinear projection first endpoint");
    CheckClose(WeightOf(ScopedCollinearSample, 3), 0.25f, 1.0e-4f, "collinear projection second endpoint");
}

void TestWeightScales()
{
    DirectionalBlendSpaceDocument ScopedDocument;
    ScopedDocument.AnchorCount = 2;
    ScopedDocument.NormalizeWeights = false;
    ScopedDocument.Anchors[0].Position = {1.0f, 0.0f};
    ScopedDocument.Anchors[0].ClipId = 1;
    ScopedDocument.Anchors[0].WeightScale = 1.0f;
    ScopedDocument.Anchors[1].Position = {0.0f, 1.0f};
    ScopedDocument.Anchors[1].ClipId = 2;
    ScopedDocument.Anchors[1].WeightScale = 3.0f;
    DirectionalBlendTable ScopedTable;
    Check(BakeDirectionalBlendSpace(ScopedDocument, &ScopedTable), "scaled ring bakes");
    const DirectionalBlendSample ScopedRaw = SampleAt(ScopedTable, 0.7853981633974483f);
    CheckClose(WeightOf(ScopedRaw, 1), 0.5f, 1.0e-4f, "raw weight keeps the first scale");
    CheckClose(WeightOf(ScopedRaw, 2), 1.5f, 1.0e-4f, "raw weight keeps the second scale");

    ScopedDocument.NormalizeWeights = true;
    Check(BakeDirectionalBlendSpace(ScopedDocument, &ScopedTable), "normalized scaled ring bakes");
    const DirectionalBlendSample ScopedNormalized = SampleAt(ScopedTable, 0.7853981633974483f);
    CheckClose(WeightOf(ScopedNormalized, 1), 0.25f, 1.0e-4f, "normalized first scale");
    CheckClose(WeightOf(ScopedNormalized, 2), 0.75f, 1.0e-4f, "normalized second scale");
}

void TestDeterminismAndSelfContainedTable()
{
    DirectionalBlendSpaceDocument ScopedDocument = MakeEightWayRing(10);
    DirectionalBlendTable ScopedFirst;
    DirectionalBlendTable ScopedSecond;
    Check(BakeDirectionalBlendSpace(ScopedDocument, &ScopedFirst), "first bake succeeds");
    Check(BakeDirectionalBlendSpace(ScopedDocument, &ScopedSecond), "second bake succeeds");
    Check(std::memcmp(&ScopedFirst, &ScopedSecond, sizeof(DirectionalBlendTable)) == 0, "bake is deterministic");

    DirectionalBlendSample ScopedA;
    DirectionalBlendSample ScopedB;
    EvaluateDirectionalBlend(ScopedFirst, {0.3f, 0.9f}, &ScopedA);
    EvaluateDirectionalBlend(ScopedFirst, {0.3f, 0.9f}, &ScopedB);
    Check(std::memcmp(&ScopedA, &ScopedB, sizeof(DirectionalBlendSample)) == 0, "evaluation is repeatable");

    // The table copies its anchors: editing the document afterwards cannot
    // change an already baked runtime image.
    ScopedDocument.Anchors[0].Position = {0.0f, -1.0f};
    ScopedDocument.Anchors[0].ClipId = 99;
    DirectionalBlendSample ScopedAfterEdit;
    EvaluateDirectionalBlend(ScopedFirst, {1.0f, 0.0f}, &ScopedAfterEdit);
    CheckClose(WeightOf(ScopedAfterEdit, 10), 1.0f, 1.0e-4f, "baked table does not read the document");
    CheckClose(WeightOf(ScopedAfterEdit, 99), 0.0f, 1.0e-6f, "edited clip id is not visible to the table");
}

void TestAngleHelpersAndFacing()
{
    CheckClose(Fby::Animation::DirectionalBlendNormalizeAngle(-1.5707963267948966f), 4.71238898038469f, 1.0e-5f,
               "negative angle wraps into the positive range");
    CheckClose(Fby::Animation::DirectionalBlendNormalizeAngle(6.283185307179586f), 0.0f, 1.0e-6f,
               "full turn wraps to zero");
    CheckClose(Fby::Animation::DirectionalBlendNormalizeAngle(std::nanf("")), 0.0f, 0.0f,
               "non-finite angle maps to zero");
    CheckClose(Fby::Animation::DirectionalBlendAngleOf({1.0f, 0.0f}), 0.0f, 1.0e-6f, "east heading is zero radians");
    CheckClose(Fby::Animation::DirectionalBlendAngleOf({0.0f, 1.0f}), 1.5707963267948966f, 1.0e-5f,
               "north heading is a quarter turn");
    CheckClose(Fby::Animation::DirectionalBlendAngleOf({0.0f, 0.0f}), 0.0f, 0.0f, "zero heading maps to zero");

    Fby::Facing2D ScopedFacing;
    Fby::Facing2DFromVector(ScopedFacing, {3.0f, 4.0f});
    CheckClose(ScopedFacing.Direction.X, 0.6f, 1.0e-5f, "facing direction is normalized");
    CheckClose(ScopedFacing.Direction.Y, 0.8f, 1.0e-5f, "facing direction keeps the ratio");
    CheckClose(ScopedFacing.Angle, 0.9272952f, 1.0e-4f, "facing angle follows the vector");

    Fby::Facing2DFromVector(ScopedFacing, {0.0f, 0.0f});
    CheckClose(ScopedFacing.Direction.X, 0.6f, 1.0e-5f, "zero vector keeps the previous direction");
}

void TestPlaneOutsideHullStaysContinuous()
{
    // Two nearby samples outside the hull must not jump between unrelated
    // anchors: the projection uses the nearest boundary point.
    DirectionalBlendTable ScopedTable;
    Check(BakeDirectionalBlendSpace(MakePlaneSquare(), &ScopedTable), "plane square bakes for continuity");
    DirectionalBlendSample ScopedFirst;
    DirectionalBlendSample ScopedSecond;
    EvaluateDirectionalBlend(ScopedTable, {1.05f, 0.4f}, &ScopedFirst);
    EvaluateDirectionalBlend(ScopedTable, {1.05f, 0.4001f}, &ScopedSecond);
    CheckClose(WeightOf(ScopedFirst, 2) + WeightOf(ScopedFirst, 3), 1.0f, 1.0e-4f,
               "outside sample uses the hull boundary");
    CheckClose(WeightOf(ScopedSecond, 2) + WeightOf(ScopedSecond, 3), 1.0f, 1.0e-4f,
               "nearby outside sample uses the same boundary");
}
}

int main()
{
    TestEightWayRing();
    TestRingNonUniform();
    TestRingDegenerateAndValidation();
    TestPlaneSquare();
    TestPlaneDegenerate();
    TestWeightScales();
    TestDeterminismAndSelfContainedTable();
    TestAngleHelpersAndFacing();
    TestPlaneOutsideHullStaysContinuous();
    if (GFailures != 0)
    {
        std::printf("%d directional blend test(s) failed\n", GFailures);
        return 1;
    }
    std::printf("directional_blend_test: all tests passed\n");
    return 0;
}
