// FemboyEngine2D - directional blend space editor panel tests.
#include "Editor/Panels/DirectionalBlendSpacePanel.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

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

using Fby::Animation::DirectionalBlendTopology;
using Fby::Editor::DirectionalBlendSpacePanel;
using Fby::Editor::kNoBlendAnchor;

bool SameDocuments(const Fby::Animation::DirectionalBlendSpaceDocument& InA,
                   const Fby::Animation::DirectionalBlendSpaceDocument& InB)
{
    if (InA.AnchorCount != InB.AnchorCount || InA.Topology != InB.Topology ||
        InA.NormalizeWeights != InB.NormalizeWeights)
    {
        return false;
    }
    for (std::uint16_t ScopedIndex = 0; ScopedIndex < InA.AnchorCount; ++ScopedIndex)
    {
        const Fby::Animation::DirectionalBlendAnchor& ScopedA = InA.Anchors[ScopedIndex];
        const Fby::Animation::DirectionalBlendAnchor& ScopedB = InB.Anchors[ScopedIndex];
        if (std::fabs(ScopedA.Position.X - ScopedB.Position.X) > 1.0e-5f ||
            std::fabs(ScopedA.Position.Y - ScopedB.Position.Y) > 1.0e-5f || ScopedA.ClipId != ScopedB.ClipId ||
            std::fabs(ScopedA.WeightScale - ScopedB.WeightScale) > 1.0e-5f || ScopedA.Enabled != ScopedB.Enabled)
        {
            return false;
        }
    }
    return true;
}

std::filesystem::path TempPath(const char* InName)
{
    // The test runner may forbid writes outside the workspace, so the panel
    // codec round trip uses a scratch file next to the build output.
    return std::filesystem::current_path() / InName;
}

void TestDefaultDocument()
{
    DirectionalBlendSpacePanel ScopedPanel;
    Check(ScopedPanel.Document().AnchorCount == 8, "default panel starts with eight directions");
    Check(ScopedPanel.Document().Topology == DirectionalBlendTopology::Ring, "default topology is a ring");
    Check(ScopedPanel.Table().AnchorCount == 8, "default table bakes eight anchors");
    Check(ScopedPanel.SelectedAnchor() == 0, "default selection is the first anchor");

    ScopedPanel.SetPreviewAngleDegrees(0.0f);
    Check(ScopedPanel.PreviewSample().Count == 1, "zero heading resolves one source");
    Check(ScopedPanel.DominantClip() == 0, "zero heading resolves the east clip");
    CheckClose(ScopedPanel.PreviewSample().Weights[0].Weight, 1.0f, 1.0e-5f, "zero heading weight");
}

void TestPreviewBlend()
{
    DirectionalBlendSpacePanel ScopedPanel;
    ScopedPanel.SetPreviewAngleDegrees(22.5f);
    const Fby::Animation::DirectionalBlendSample& ScopedSample = ScopedPanel.PreviewSample();
    Check(ScopedSample.Count == 2, "midway heading blends two sources");
    float ScopedLeft = 0.0f;
    float ScopedRight = 0.0f;
    for (std::uint8_t ScopedIndex = 0; ScopedIndex < ScopedSample.Count; ++ScopedIndex)
    {
        if (ScopedSample.Weights[ScopedIndex].ClipId == 0)
        {
            ScopedLeft = ScopedSample.Weights[ScopedIndex].Weight;
        }
        if (ScopedSample.Weights[ScopedIndex].ClipId == 1)
        {
            ScopedRight = ScopedSample.Weights[ScopedIndex].Weight;
        }
    }
    CheckClose(ScopedLeft, 0.5f, 1.0e-4f, "east source weight at 22.5 degrees");
    CheckClose(ScopedRight, 0.5f, 1.0e-4f, "north-east source weight at 22.5 degrees");

    ScopedPanel.SetPreviewAngleDegrees(-90.0f);
    Check(ScopedPanel.PreviewSample().Count == 1, "negative heading normalizes");
    Check(ScopedPanel.DominantClip() == 6, "negative heading resolves the south clip");
}

void TestAddRemove()
{
    DirectionalBlendSpacePanel ScopedPanel;
    ScopedPanel.SetPreviewAngleDegrees(90.0f);
    const std::uint16_t ScopedAdded = ScopedPanel.AddAnchor();
    Check(ScopedAdded == 8, "added anchor returns its index");
    Check(ScopedPanel.Document().AnchorCount == 9, "added anchor extends the document");
    Check(ScopedPanel.SelectedAnchor() == 8, "added anchor becomes selected");
    CheckClose(ScopedPanel.Document().Anchors[8].Position.Y, 1.0f, 1.0e-4f, "new anchor sits at the preview heading");

    Check(ScopedPanel.SetClipPath(1, "assets/clips/keep.fanim"), "clip path binds");
    Check(ScopedPanel.RemoveAnchor(0), "anchor removal succeeds");
    Check(ScopedPanel.Document().AnchorCount == 8, "removal shrinks the document");
    Check(ScopedPanel.ClipPath(0) == "assets/clips/keep.fanim", "clip paths shift with the anchors");
    Check(ScopedPanel.SelectedAnchor() == 7, "selection follows the shifted anchor");

    while (ScopedPanel.Document().AnchorCount < Fby::Animation::kMaxDirectionalAnchors)
    {
        Check(ScopedPanel.AddAnchor() != kNoBlendAnchor, "anchor added up to the capacity");
    }
    Check(ScopedPanel.AddAnchor() == kNoBlendAnchor, "add past the capacity is rejected");
}

void TestTopologySwitch()
{
    DirectionalBlendSpacePanel ScopedPanel;
    ScopedPanel.SetTopology(DirectionalBlendTopology::Plane);
    Check(ScopedPanel.Document().Topology == DirectionalBlendTopology::Plane, "topology switches to plane");
    Check(ScopedPanel.Table().HullCount == 8, "plane hull keeps the eight points");
    Check(ScopedPanel.Table().TriangleCount == 6, "eight hull points fan into six triangles");

    ScopedPanel.Document().Anchors[0].Position = {0.0f, 0.0f};
    ScopedPanel.SetTopology(DirectionalBlendTopology::Ring);
    Check(ScopedPanel.Document().Topology == DirectionalBlendTopology::Ring, "topology switches back to ring");
    Check(ScopedPanel.Table().AnchorCount == 8, "ring table rebuilds after the switch");
    Check(std::fabs(ScopedPanel.Document().Anchors[0].Position.X) +
                  std::fabs(ScopedPanel.Document().Anchors[0].Position.Y) >
              0.1f,
          "a zero plane anchor gets a ring heading");
}

void TestSaveLoadRoundTrip()
{
    DirectionalBlendSpacePanel ScopedSource;
    ScopedSource.SetTopology(DirectionalBlendTopology::Plane);
    ScopedSource.Document().NormalizeWeights = false;
    ScopedSource.Document().Anchors[0].Position = {-2.0f, 1.5f};
    ScopedSource.Document().Anchors[0].ClipId = 100;
    ScopedSource.Document().Anchors[0].WeightScale = 0.25f;
    ScopedSource.Document().Anchors[0].Enabled = false;
    ScopedSource.Document().Anchors[1].Position = {3.0f, -0.5f};
    ScopedSource.Document().Anchors[1].ClipId = 101;
    (void)ScopedSource.SetClipPath(0, "assets/clips/run_east.fanim");
    (void)ScopedSource.SetClipPath(1, "assets/clips/run north.fanim");

    const std::filesystem::path ScopedPath = TempPath("fby_directional_blend_panel.fblend");
    const bool ScopedSaved = ScopedSource.SaveToFile(ScopedPath);
    Check(ScopedSaved, "panel document saves");
    Check(std::filesystem::exists(ScopedPath), "panel file exists");

    DirectionalBlendSpacePanel ScopedLoaded;
    Check(ScopedLoaded.LoadFromFile(ScopedPath), "panel document loads");
    Check(SameDocuments(ScopedSource.Document(), ScopedLoaded.Document()), "round trip keeps the document");
    Check(ScopedLoaded.ClipPath(0) == "assets/clips/run_east.fanim", "round trip keeps the first clip path");
    Check(ScopedLoaded.ClipPath(1) == "assets/clips/run north.fanim", "round trip keeps a path with spaces");
    Check(ScopedLoaded.Document().Topology == DirectionalBlendTopology::Plane, "round trip keeps the topology");
    Check(!ScopedLoaded.Document().NormalizeWeights, "round trip keeps the normalization flag");

    std::error_code ScopedError;
    std::filesystem::remove(ScopedPath, ScopedError);
}

void TestInvalidLoad()
{
    DirectionalBlendSpacePanel ScopedPanel;
    const std::filesystem::path ScopedPath = TempPath("fby_directional_blend_panel_bad.fblend");
    {
        std::ofstream ScopedFile(ScopedPath);
        ScopedFile << "FBYBLEND 1\n";
        ScopedFile << "topology spiral\n";
    }
    Check(!ScopedPanel.LoadFromFile(ScopedPath), "unknown topology is rejected");
    Check(ScopedPanel.Document().AnchorCount == 8, "rejected load keeps the previous document");
    std::error_code ScopedError;
    std::filesystem::remove(ScopedPath, ScopedError);

    const std::filesystem::path ScopedBadHeader = TempPath("fby_directional_blend_panel_header.fblend");
    {
        std::ofstream ScopedFile(ScopedBadHeader);
        ScopedFile << "NOTBLEND 1\n";
    }
    Check(!ScopedPanel.LoadFromFile(ScopedBadHeader), "missing header is rejected");
    std::filesystem::remove(ScopedBadHeader, ScopedError);
}

void TestSetDocument()
{
    DirectionalBlendSpacePanel ScopedPanel;
    Fby::Animation::DirectionalBlendSpaceDocument ScopedDocument;
    ScopedDocument.AnchorCount = 3;
    ScopedDocument.Anchors[0].Position = {1.0f, 0.0f};
    ScopedDocument.Anchors[0].ClipId = 7;
    ScopedDocument.Anchors[1].Position = {0.0f, 1.0f};
    ScopedDocument.Anchors[1].ClipId = 8;
    ScopedDocument.Anchors[2].Position = {-1.0f, 0.0f};
    ScopedDocument.Anchors[2].ClipId = 9;
    ScopedPanel.SetDocument(ScopedDocument);
    Check(ScopedPanel.Document().AnchorCount == 3, "set document replaces the anchors");
    Check(ScopedPanel.Table().AnchorCount == 3, "set document rebakes the table");
    ScopedPanel.SetPreviewAngleDegrees(45.0f);
    Check(ScopedPanel.PreviewSample().Count == 2, "set document preview blends neighbours");
    Check(!ScopedPanel.SetClipPath(9, "ignored.fanim"), "clip path past the anchor count is rejected");
}

void TestClipPathFallbackLabel()
{
    DirectionalBlendSpacePanel ScopedPanel;
    Check(ScopedPanel.ClipPath(0).empty(), "clip path starts empty");
    Check(ScopedPanel.SetClipPath(0, "assets/run.fanim"), "clip path accepts a file");
    Check(ScopedPanel.ClipPath(0) == "assets/run.fanim", "clip path returns the bound file");
}
}

int main()
{
    TestDefaultDocument();
    TestPreviewBlend();
    TestAddRemove();
    TestTopologySwitch();
    TestSaveLoadRoundTrip();
    TestInvalidLoad();
    TestSetDocument();
    TestClipPathFallbackLabel();
    if (GFailures != 0)
    {
        std::printf("%d directional blend panel test(s) failed\n", GFailures);
        return 1;
    }
    std::printf("directional_blend_panel_test: all tests passed\n");
    return 0;
}
