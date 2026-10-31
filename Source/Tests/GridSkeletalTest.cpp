// FemboyEngine2D - grid projections, tilemap, skeletal IK/retarget and clip
// compression tests.
#include "Engine/Core/Grid.hpp"
#include "Engine/Assets/AnimationClipAsset.hpp"
#include "Engine/Assets/VFS.hpp"
#include "Engine/ECS/AnimationCompression.hpp"
#include "Engine/ECS/ComponentRegistry.hpp"
#include "Engine/ECS/Scene.hpp"
#include "Engine/ECS/SkeletonIk.hpp"
#include "Engine/ECS/Tilemap.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
std::uint32_t GFailures = 0;

#define FBY_CHECK(condition)                                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                  \
            ++GFailures;                                                                                              \
        }                                                                                                              \
    } while (false)

constexpr float kTolerance = 1.0e-3f;

[[nodiscard]] bool Close(float InActual, float InExpected, float InTolerance = kTolerance)
{
    return std::fabs(InActual - InExpected) <= InTolerance;
}

[[nodiscard]] bool VecClose(Fby::Vector2D InValue, Fby::Vector2D InExpected, float InTolerance = kTolerance)
{
    return Close(InValue.X, InExpected.X, InTolerance) && Close(InValue.Y, InExpected.Y, InTolerance);
}

void TestGridConversions()
{
    using Fby::Grid::Cell;
    using Fby::Grid::GridDesc;
    using Fby::Grid::ProjectionMode;

    const ProjectionMode ScopedModes[4] = {ProjectionMode::Side, ProjectionMode::TopDown, ProjectionMode::IsoRect,
                                           ProjectionMode::IsoHex};
    for (const ProjectionMode ScopedMode : ScopedModes)
    {
        GridDesc ScopedGrid;
        ScopedGrid.Mode = ScopedMode;
        ScopedGrid.CellSize = Fby::Vector2D{64.0f, 32.0f};
        ScopedGrid.Origin = Fby::Vector2D{100.0f, -50.0f};
        for (std::int32_t ScopedY = -4; ScopedY <= 4; ++ScopedY)
        {
            for (std::int32_t ScopedX = -4; ScopedX <= 4; ++ScopedX)
            {
                const Cell ScopedCell{ScopedX, ScopedY};
                const Fby::Vector2D ScopedWorld = Fby::Grid::CellToWorld(ScopedGrid, ScopedCell);
                const Cell ScopedRoundTrip = Fby::Grid::WorldToCell(ScopedGrid, ScopedWorld);
                FBY_CHECK(ScopedRoundTrip == ScopedCell);
            }
        }
    }

    Fby::Grid::GridDesc ScopedRect;
    ScopedRect.Mode = ProjectionMode::IsoRect;
    ScopedRect.CellSize = Fby::Vector2D{64.0f, 32.0f};
    FBY_CHECK(VecClose(Fby::Grid::CellToWorld(ScopedRect, Cell{3, 5}), Fby::Vector2D{-64.0f, 128.0f}));

    FBY_CHECK(Fby::Grid::NeighborCount(ProjectionMode::IsoHex) == 6);
    FBY_CHECK(Fby::Grid::NeighborCount(ProjectionMode::TopDown) == 4);

    const Cell ScopedCenter{5, 7};
    float ScopedNeighbourDistance = 1.0e9f;
    for (std::uint32_t ScopedI = 0; ScopedI < 6; ++ScopedI)
    {
        const Cell ScopedNeighbour = Fby::Grid::Neighbor(ProjectionMode::IsoHex, ScopedCenter, ScopedI);
        const float ScopedDistance = Fby::Grid::CellDistance(ProjectionMode::IsoHex, ScopedCenter, ScopedNeighbour);
        ScopedNeighbourDistance = std::min(ScopedNeighbourDistance, ScopedDistance);
    }
    FBY_CHECK(Close(ScopedNeighbourDistance, 1.0f));

    const ProjectionMode ScopedParse = ProjectionMode::Side;
    Fby::Grid::ProjectionMode ScopedParsed = ScopedParse;
    FBY_CHECK(Fby::Grid::ParseProjectionMode("iso-hex", &ScopedParsed));
    FBY_CHECK(ScopedParsed == ProjectionMode::IsoHex);
    FBY_CHECK(!Fby::Grid::ParseProjectionMode("perspective", &ScopedParsed));

    Fby::Grid::GridDesc ScopedIso;
    ScopedIso.Mode = ProjectionMode::IsoRect;
    ScopedIso.CellSize = Fby::Vector2D{64.0f, 32.0f};
    const float ScopedBackRow = Fby::Grid::SortDepth(ScopedIso, Cell{0, 0});
    const float ScopedFrontRow = Fby::Grid::SortDepth(ScopedIso, Cell{1, 1});
    FBY_CHECK(ScopedFrontRow > ScopedBackRow);
    const float ScopedLeft = Fby::Grid::SortDepth(ScopedIso, Cell{0, 2});
    const float ScopedRight = Fby::Grid::SortDepth(ScopedIso, Cell{2, 0});
    FBY_CHECK(ScopedRight > ScopedLeft);
}

void TestTilemap()
{
    Fby::Ecs::Registry ScopedRegistry;
    const Fby::Entity ScopedEntity = ScopedRegistry.create();
    Fby::TilemapComponent& ScopedMap = ScopedRegistry.emplace<Fby::TilemapComponent>(ScopedEntity);
    ScopedMap.Grid.Mode = Fby::Grid::ProjectionMode::IsoRect;
    ScopedMap.Grid.CellSize = Fby::Vector2D{64.0f, 32.0f};
    ScopedMap.Grid.Origin = Fby::Vector2D{0.0f, 0.0f};
    ScopedMap.Grid.HeightStep = 8.0f;
    ScopedMap.TextureLayer = 3;
    Fby::Tilemap::Resize(ScopedMap, 4, 4);
    FBY_CHECK(ScopedMap.Width == 4 && ScopedMap.Height == 4);

    FBY_CHECK(Fby::Tilemap::Set(ScopedMap, Fby::Grid::Cell{1, 2}, Fby::MakeTile(3, Fby::kTileFlagSolid, 1)));
    FBY_CHECK(!Fby::Tilemap::Set(ScopedMap, Fby::Grid::Cell{9, 9}, Fby::MakeTile(1)));
    FBY_CHECK(Fby::Tilemap::At(ScopedMap, Fby::Grid::Cell{1, 2}) == Fby::MakeTile(3, Fby::kTileFlagSolid, 1));
    FBY_CHECK(Fby::Tilemap::IsSolid(ScopedMap, Fby::Grid::Cell{1, 2}));
    FBY_CHECK(!Fby::Tilemap::IsSolid(ScopedMap, Fby::Grid::Cell{0, 0}));
    FBY_CHECK(Fby::TileHeightOf(Fby::Tilemap::At(ScopedMap, Fby::Grid::Cell{1, 2})) == 1);

    Fby::Vector2D ScopedUvMin{};
    Fby::Vector2D ScopedUvMax{};
    ScopedMap.AtlasColumns = 8;
    ScopedMap.AtlasRows = 8;
    FBY_CHECK(Fby::Tilemap::TileUv(ScopedMap, 3, &ScopedUvMin, &ScopedUvMax));
    FBY_CHECK(Close(ScopedUvMin.X, 0.25f));
    FBY_CHECK(Close(ScopedUvMax.X, 0.375f));

    Fby::Vector2D ScopedBoundsMin{};
    Fby::Vector2D ScopedBoundsMax{};
    FBY_CHECK(Fby::Tilemap::CellBounds(ScopedMap, Fby::Grid::Cell{1, 2}, &ScopedBoundsMin, &ScopedBoundsMax));
    FBY_CHECK(VecClose(ScopedBoundsMin, Fby::Vector2D{-64.0f, 24.0f}));
    FBY_CHECK(VecClose(ScopedBoundsMax, Fby::Vector2D{0.0f, 56.0f}));

    Fby::TilemapRenderSystem ScopedRender;
    ScopedRender.Collect(ScopedRegistry, Fby::Vector2D{-500.0f, -500.0f}, Fby::Vector2D{500.0f, 500.0f});
    FBY_CHECK(ScopedRender.QuadCount() == 1);
    FBY_CHECK(!ScopedRender.Truncated());
    if (ScopedRender.QuadCount() == 1)
    {
        const Fby::TileQuad& ScopedQuad = ScopedRender.Quads()[0];
        FBY_CHECK(ScopedQuad.Item.layer == 3);
        FBY_CHECK(VecClose(ScopedQuad.Item.size, Fby::Vector2D{64.0f, 32.0f}));
        FBY_CHECK(VecClose(ScopedQuad.Item.position, Fby::Vector2D{-32.0f, 40.0f}));
        FBY_CHECK(ScopedQuad.Key != 0);
    }

    ScopedRender.Collect(ScopedRegistry, Fby::Vector2D{1000.0f, 1000.0f}, Fby::Vector2D{1200.0f, 1200.0f});
    FBY_CHECK(ScopedRender.QuadCount() == 0);

    Fby::Tilemap::Clear(ScopedMap);
    FBY_CHECK(Fby::Tilemap::At(ScopedMap, Fby::Grid::Cell{1, 2}) == 0);
}

[[nodiscard]] Fby::SkeletonComponent MakeChain(std::uint32_t InBoneCount, float InLength)
{
    Fby::SkeletonComponent ScopedSkeleton;
    Fby::SkeletonReset(ScopedSkeleton);
    (void)Fby::SkeletonAddBone(ScopedSkeleton, "root", -1, Fby::Vector2D{0.0f, 0.0f}, 0.0f);
    for (std::uint32_t ScopedI = 1; ScopedI < InBoneCount; ++ScopedI)
    {
        char ScopedName[16] = {};
        std::snprintf(ScopedName, sizeof(ScopedName), "bone%u", ScopedI);
        (void)Fby::SkeletonAddBone(ScopedSkeleton, ScopedName, static_cast<std::int16_t>(ScopedI - 1),
                                   Fby::Vector2D{InLength, 0.0f}, 0.0f);
    }
    Fby::SkeletonBindPose(ScopedSkeleton);
    Fby::SkeletalAnimation::Evaluate(ScopedSkeleton);
    return ScopedSkeleton;
}

void TestTwoBoneIk()
{
    Fby::SkeletonComponent ScopedSkeleton = MakeChain(3, 10.0f);
    Fby::Skeletal::IkChain ScopedChain;
    FBY_CHECK(Fby::Skeletal::MakeTwoBoneChain(ScopedSkeleton, 0, 1, 2, Fby::Vector2D{12.0f, 6.0f}, &ScopedChain));
    FBY_CHECK(ScopedChain.BoneCount == 3);
    FBY_CHECK(Fby::Skeletal::SolveIk(ScopedSkeleton, ScopedChain));

    const Fby::Mat3& ScopedTip = ScopedSkeleton.GlobalMatrices[2];
    const Fby::Vector2D ScopedTipPosition{ScopedTip.m[6], ScopedTip.m[7]};
    FBY_CHECK(VecClose(ScopedTipPosition, Fby::Vector2D{12.0f, 6.0f}, 0.05f));

    FBY_CHECK(!Fby::Skeletal::MakeTwoBoneChain(ScopedSkeleton, 0, 2, 1, Fby::Vector2D{}, &ScopedChain));

    Fby::Skeletal::IkChain ScopedOutOfReach;
    FBY_CHECK(Fby::Skeletal::MakeTwoBoneChain(ScopedSkeleton, 0, 1, 2, Fby::Vector2D{500.0f, 0.0f}, &ScopedOutOfReach));
    FBY_CHECK(Fby::Skeletal::SolveIk(ScopedSkeleton, ScopedOutOfReach));
    const Fby::Vector2D ScopedStraight = Fby::Vector2D{ScopedSkeleton.GlobalMatrices[2].m[6],
                                                       ScopedSkeleton.GlobalMatrices[2].m[7]};
    FBY_CHECK(Close(ScopedStraight.X, 20.0f, 0.1f));
    FBY_CHECK(Close(ScopedStraight.Y, 0.0f, 0.1f));

    const std::uint16_t ScopedBones[4] = {0, 1, 2, 2};
    Fby::Skeletal::IkChain ScopedFabrikInvalid;
    FBY_CHECK(!Fby::Skeletal::MakeFabrikChain(ScopedSkeleton, ScopedBones, 4, Fby::Vector2D{1.0f, 1.0f},
                                              &ScopedFabrikInvalid));
}

void TestFabrikIk()
{
    Fby::SkeletonComponent ScopedSkeleton = MakeChain(4, 10.0f);
    const std::uint16_t ScopedBones[4] = {0, 1, 2, 3};
    Fby::Skeletal::IkChain ScopedChain;
    FBY_CHECK(Fby::Skeletal::MakeFabrikChain(ScopedSkeleton, ScopedBones, 4, Fby::Vector2D{18.0f, 12.0f}, &ScopedChain));
    ScopedChain.Iterations = 16;
    FBY_CHECK(Fby::Skeletal::SolveIk(ScopedSkeleton, ScopedChain));

    const Fby::Mat3& ScopedTip = ScopedSkeleton.GlobalMatrices[3];
    const Fby::Vector2D ScopedTipPosition{ScopedTip.m[6], ScopedTip.m[7]};
    const float ScopedError = std::sqrt((ScopedTipPosition.X - 18.0f) * (ScopedTipPosition.X - 18.0f) +
                                        (ScopedTipPosition.Y - 12.0f) * (ScopedTipPosition.Y - 12.0f));
    FBY_CHECK(ScopedError < 0.5f);

    Fby::SkeletonComponent ScopedOutOfReach = MakeChain(4, 10.0f);
    FBY_CHECK(Fby::Skeletal::MakeFabrikChain(ScopedOutOfReach, ScopedBones, 4, Fby::Vector2D{100.0f, 0.0f},
                                             &ScopedChain));
    FBY_CHECK(Fby::Skeletal::SolveIk(ScopedOutOfReach, ScopedChain));
    FBY_CHECK(Close(ScopedOutOfReach.GlobalMatrices[3].m[6], 30.0f, 0.2f));
}

void TestRetarget()
{
    Fby::SkeletonComponent ScopedSource = MakeChain(3, 10.0f);
    ScopedSource.LocalRotations[1] = 0.5f;
    ScopedSource.LocalRotations[2] = -0.25f;
    Fby::SkeletalAnimation::Evaluate(ScopedSource);

    Fby::SkeletonComponent ScopedTarget;
    Fby::SkeletonReset(ScopedTarget);
    (void)Fby::SkeletonAddBone(ScopedTarget, "root", -1, Fby::Vector2D{0.0f, 0.0f}, 0.0f);
    (void)Fby::SkeletonAddBone(ScopedTarget, "bone1", 0, Fby::Vector2D{20.0f, 0.0f}, 0.0f);
    (void)Fby::SkeletonAddBone(ScopedTarget, "bone2", 1, Fby::Vector2D{20.0f, 0.0f}, 0.0f);
    Fby::SkeletonBindPose(ScopedTarget);

    Fby::Skeletal::RetargetMap ScopedMap;
    const std::uint32_t ScopedMatched = Fby::Skeletal::BuildRetargetMap(ScopedSource, ScopedTarget, 1.0f, &ScopedMap);
    FBY_CHECK(ScopedMatched == 3);
    FBY_CHECK(ScopedMap.PairCount == 3);

    const std::uint32_t ScopedApplied = Fby::Skeletal::ApplyRetarget(ScopedSource, ScopedMap, ScopedTarget);
    FBY_CHECK(ScopedApplied == 3);
    FBY_CHECK(Close(ScopedTarget.LocalPositions[1].X, 20.0f, 0.01f));
    FBY_CHECK(Close(ScopedTarget.LocalRotations[1], 0.5f, 0.001f));
    FBY_CHECK(Close(ScopedTarget.LocalRotations[2], -0.25f, 0.001f));

    Fby::Skeletal::RetargetMap ScopedScaled;
    (void)Fby::Skeletal::BuildRetargetMap(ScopedSource, ScopedTarget, 2.0f, &ScopedScaled);
    FBY_CHECK(Close(ScopedScaled.Pairs[1].LengthScale, 4.0f, 0.01f));
}

void TestPoseCompression()
{
    Fby::SkeletonComponent ScopedSkeleton = MakeChain(3, 10.0f);
    ScopedSkeleton.LocalRotations[1] = 0.7f;
    ScopedSkeleton.LocalRotations[2] = -1.2f;
    ScopedSkeleton.LocalPositions[1] = Fby::Vector2D{12.34f, -5.6f};
    ScopedSkeleton.LocalScales[2] = 1.25f;
    Fby::SkeletonBindPose(ScopedSkeleton);

    Fby::Skeletal::QuantizedPose ScopedPose;
    Fby::Skeletal::CompressPose(ScopedSkeleton, &ScopedPose);
    FBY_CHECK(ScopedPose.BoneCount == 3);
    FBY_CHECK(sizeof(Fby::Skeletal::QuantizedPose) <= 1032);

    Fby::SkeletonComponent ScopedRestored = MakeChain(3, 10.0f);
    FBY_CHECK(Fby::Skeletal::DecompressPose(ScopedPose, &ScopedRestored));
    FBY_CHECK(Close(ScopedRestored.LocalRotations[1], 0.7f, Fby::Skeletal::PoseRotationTolerance()));
    FBY_CHECK(Close(ScopedRestored.LocalRotations[2], -1.2f, Fby::Skeletal::PoseRotationTolerance()));
    FBY_CHECK(Close(ScopedRestored.LocalPositions[1].X, 12.34f, Fby::Skeletal::PosePositionTolerance()));
    FBY_CHECK(Close(ScopedRestored.LocalPositions[1].Y, -5.6f, Fby::Skeletal::PosePositionTolerance()));
    FBY_CHECK(Close(ScopedRestored.LocalScales[2], 1.25f, 1.0e-4f));

    Fby::Skeletal::QuantizedPose ScopedBroken;
    ScopedBroken.BoneCount = static_cast<std::uint16_t>(Fby::kMaxBones + 1);
    FBY_CHECK(!Fby::Skeletal::DecompressPose(ScopedBroken, &ScopedRestored));
}

void TestClipCompression()
{
    Fby::Animation::AnimationTimeline ScopedTimeline;
    ScopedTimeline.Clear();
    ScopedTimeline.duration = 2.0f;
    const std::uint16_t ScopedTrack =
        ScopedTimeline.AddTrack(Fby::Animation::TrackKind::BonePosition, 0x1234u, 0xFFFFFFFFu);
    for (std::uint32_t ScopedI = 0; ScopedI <= 20; ++ScopedI)
    {
        const float ScopedTime = static_cast<float>(ScopedI) * 0.1f;
        (void)ScopedTimeline.AddKey(ScopedTrack, ScopedTime, Fby::Vector2D{ScopedTime * 10.0f, 0.0f},
                                    Fby::Animation::Interpolation::Linear);
    }
    FBY_CHECK(ScopedTimeline.keyCount == 21);

    Fby::Animation::CompressedClip ScopedClip;
    Fby::Animation::CompressionReport ScopedReport;
    FBY_CHECK(Fby::Animation::CompressTimeline(ScopedTimeline, 0.05f, &ScopedClip, &ScopedReport));
    FBY_CHECK(ScopedClip.TrackCount == 1);
    FBY_CHECK(ScopedClip.KeyCount <= 3);
    FBY_CHECK(ScopedReport.SourceKeys == 21);
    FBY_CHECK(!ScopedReport.Truncated);

    const Fby::Vector2D ScopedMid = Fby::Animation::EvaluateCompressedTrack(ScopedClip, 0, 1.05f);
    FBY_CHECK(Close(ScopedMid.X, 10.5f, 0.01f));

    Fby::Animation::AnimationTimeline ScopedDecompressed;
    FBY_CHECK(Fby::Animation::DecompressTimeline(ScopedClip, &ScopedDecompressed));
    FBY_CHECK(ScopedDecompressed.trackCount == 1);
    FBY_CHECK(ScopedDecompressed.keyCount > 0);
    FBY_CHECK(ScopedDecompressed.keyCount <= Fby::Animation::kMaxTimelineKeys);
    const Fby::Vector2D ScopedRestored = ScopedDecompressed.EvaluateTrack(0, 1.55f);
    FBY_CHECK(Close(ScopedRestored.X, 15.5f, 0.02f));

    Fby::Animation::AnimationTimeline ScopedCurved;
    ScopedCurved.Clear();
    ScopedCurved.duration = 1.0f;
    const std::uint16_t ScopedCurvedTrack =
        ScopedCurved.AddTrack(Fby::Animation::TrackKind::BoneRotation, 0x99u, 0xFFFFFFFFu);
    for (std::uint32_t ScopedI = 0; ScopedI <= 32; ++ScopedI)
    {
        const float ScopedTime = static_cast<float>(ScopedI) / 32.0f;
        (void)ScopedCurved.AddKey(ScopedCurvedTrack, ScopedTime,
                                  Fby::Vector2D{std::sin(ScopedTime * 6.28318f), 0.0f},
                                  Fby::Animation::Interpolation::Linear);
    }
    Fby::Animation::CompressedClip ScopedCurvedClip;
    FBY_CHECK(Fby::Animation::CompressTimeline(ScopedCurved, 0.001f, &ScopedCurvedClip, nullptr));
    FBY_CHECK(ScopedCurvedClip.KeyCount > 3);

    Fby::Animation::CompressedClip ScopedInvalid;
    ScopedInvalid.Duration = 0.0f;
    Fby::Animation::AnimationTimeline ScopedZeroDuration;
    ScopedZeroDuration.duration = 0.0f;
    FBY_CHECK(!Fby::Animation::CompressTimeline(ScopedZeroDuration, 0.1f, &ScopedInvalid, nullptr));
    FBY_CHECK(!Fby::Animation::DecompressTimeline(ScopedInvalid, &ScopedDecompressed));
}

void TestTilemapTextRoundTrip()
{
    Fby::TilemapComponent ScopedMap;
    ScopedMap.Grid.Mode = Fby::Grid::ProjectionMode::IsoHex;
    ScopedMap.Grid.CellSize = Fby::Vector2D{56.0f, 64.0f};
    ScopedMap.Grid.Origin = Fby::Vector2D{12.5f, -3.0f};
    Fby::Tilemap::Resize(ScopedMap, 5, 3);
    ScopedMap.AtlasColumns = 8;
    ScopedMap.AtlasRows = 4;
    ScopedMap.TextureLayer = 2;
    ScopedMap.SortLayer = 7;
    ScopedMap.Tint = 0x80FFFFFFu;
    FBY_CHECK(Fby::Tilemap::Set(ScopedMap, Fby::Grid::Cell{1, 1}, Fby::MakeTile(17, Fby::kTileFlagSolid, 2)));
    FBY_CHECK(Fby::Tilemap::Set(ScopedMap, Fby::Grid::Cell{4, 2}, Fby::MakeTile(3, Fby::kTileFlagTrigger, 0)));

    std::string ScopedText;
    FBY_CHECK(Fby::Tilemap::SaveText(ScopedMap, &ScopedText));
    FBY_CHECK(ScopedText.find("mode iso-hex") != std::string::npos);
    FBY_CHECK(ScopedText.find("size 5 3") != std::string::npos);

    Fby::TilemapComponent ScopedLoaded;
    FBY_CHECK(Fby::Tilemap::LoadText(ScopedText, &ScopedLoaded, nullptr));
    FBY_CHECK(ScopedLoaded.Grid.Mode == ScopedMap.Grid.Mode);
    FBY_CHECK(Close(ScopedLoaded.Grid.CellSize.X, 56.0f));
    FBY_CHECK(Close(ScopedLoaded.Grid.Origin.Y, -3.0f));
    FBY_CHECK(ScopedLoaded.Width == 5 && ScopedLoaded.Height == 3);
    FBY_CHECK(ScopedLoaded.AtlasColumns == 8 && ScopedLoaded.AtlasRows == 4 && ScopedLoaded.TextureLayer == 2);
    FBY_CHECK(ScopedLoaded.SortLayer == 7);
    FBY_CHECK(ScopedLoaded.Tint == ScopedMap.Tint);
    for (std::uint32_t ScopedY = 0; ScopedY < 3; ++ScopedY)
    {
        for (std::uint32_t ScopedX = 0; ScopedX < 5; ++ScopedX)
        {
            const Fby::Grid::Cell ScopedCell{static_cast<std::int32_t>(ScopedX), static_cast<std::int32_t>(ScopedY)};
            FBY_CHECK(Fby::Tilemap::At(ScopedLoaded, ScopedCell) == Fby::Tilemap::At(ScopedMap, ScopedCell));
        }
    }

    Fby::TilemapComponent ScopedBroken;
    std::string ScopedError;
    FBY_CHECK(!Fby::Tilemap::LoadText("tilemap 1\nsize 2 2\ntiles 00000001,00000002\n", &ScopedBroken, &ScopedError));
    FBY_CHECK(ScopedError.find("tile count") != std::string::npos);
    FBY_CHECK(!Fby::Tilemap::LoadText("tilemap 1\nbogus 1\n", &ScopedBroken, &ScopedError));
}

void TestClipAssetRoundTrip()
{
    Fby::Animation::AnimationTimeline ScopedTimeline;
    ScopedTimeline.Clear();
    ScopedTimeline.duration = 1.5f;
    const std::uint16_t ScopedTrack =
        ScopedTimeline.AddTrack(Fby::Animation::TrackKind::BoneRotation, 0xABCDu, 0xFF00FF00u);
    for (std::uint32_t ScopedI = 0; ScopedI <= 10; ++ScopedI)
    {
        const float ScopedTime = static_cast<float>(ScopedI) * 0.15f;
        (void)ScopedTimeline.AddKey(ScopedTrack, ScopedTime, Fby::Vector2D{ScopedTime, -ScopedTime},
                                    Fby::Animation::Interpolation::Linear);
    }
    Fby::Animation::CompressedClip ScopedClip;
    FBY_CHECK(Fby::Animation::CompressTimeline(ScopedTimeline, 0.001f, &ScopedClip, nullptr));
    std::memcpy(ScopedClip.Name, "walk", 4);

    std::vector<std::byte> ScopedBytes;
    std::string ScopedError;
    FBY_CHECK(Fby::Animation::Asset::SaveClip(ScopedClip, &ScopedBytes, &ScopedError));
    FBY_CHECK(ScopedBytes.size() > Fby::Animation::Asset::kFanmHeaderSize);

    Fby::Animation::CompressedClip ScopedLoaded;
    FBY_CHECK(Fby::Animation::Asset::LoadClip(ScopedBytes, &ScopedLoaded, &ScopedError));
    FBY_CHECK(ScopedLoaded.TrackCount == ScopedClip.TrackCount);
    FBY_CHECK(ScopedLoaded.KeyCount == ScopedClip.KeyCount);
    FBY_CHECK(Close(ScopedLoaded.Duration, 1.5f));
    FBY_CHECK(std::memcmp(ScopedLoaded.Name, "walk", 4) == 0);
    FBY_CHECK(ScopedLoaded.Tracks[0].BoneHash == 0xABCDu);

    Fby::Animation::AnimationTimeline ScopedPlayback;
    FBY_CHECK(Fby::Animation::DecompressTimeline(ScopedLoaded, &ScopedPlayback));
    const Fby::Vector2D ScopedSample = ScopedPlayback.EvaluateTrack(0, 0.75f);
    FBY_CHECK(Close(ScopedSample.X, 0.75f, 0.02f));

    // A flipped byte must fail the content hash instead of playing garbage.
    std::vector<std::byte> ScopedCorrupt = ScopedBytes;
    ScopedCorrupt[Fby::Animation::Asset::kFanmHeaderSize + 4] ^= std::byte{0x5A};
    FBY_CHECK(!Fby::Animation::Asset::LoadClip(ScopedCorrupt, &ScopedLoaded, &ScopedError));
    FBY_CHECK(ScopedError.find("hash") != std::string::npos);
    FBY_CHECK(!Fby::Animation::Asset::LoadClip(std::span<const std::byte>(), &ScopedLoaded, &ScopedError));
}

void TestCameraScreenMapping()
{
    Fby::Grid::CameraView ScopedCamera;
    ScopedCamera.Center = Fby::Vector2D{320.0f, 240.0f};
    ScopedCamera.ViewportSize = Fby::Vector2D{1280.0f, 720.0f};
    ScopedCamera.Zoom = 2.0f;

    const Fby::Vector2D ScopedCentre = Fby::Grid::WorldToScreen(ScopedCamera, ScopedCamera.Center);
    FBY_CHECK(VecClose(ScopedCentre, Fby::Vector2D{640.0f, 360.0f}));

    const Fby::Vector2D ScopedWorld{100.0f, -50.0f};
    const Fby::Vector2D ScopedScreen = Fby::Grid::WorldToScreen(ScopedCamera, ScopedWorld);
    FBY_CHECK(VecClose(Fby::Grid::ScreenToWorld(ScopedCamera, ScopedScreen), ScopedWorld));

    Fby::Grid::GridDesc ScopedGrid;
    ScopedGrid.Mode = Fby::Grid::ProjectionMode::IsoRect;
    ScopedGrid.CellSize = Fby::Vector2D{64.0f, 32.0f};
    const Fby::Vector2D ScopedCellCentre = Fby::Grid::CellToWorld(ScopedGrid, Fby::Grid::Cell{3, 5});
    const Fby::Vector2D ScopedCellScreen = Fby::Grid::WorldToScreen(ScopedCamera, ScopedCellCentre);
    FBY_CHECK(Fby::Grid::ScreenToCell(ScopedGrid, ScopedCamera, ScopedCellScreen) == (Fby::Grid::Cell{3, 5}));

    // A zero zoom must not divide by zero.
    Fby::Grid::CameraView ScopedFlat = ScopedCamera;
    ScopedFlat.Zoom = 0.0f;
    const Fby::Vector2D ScopedSafe = Fby::Grid::ScreenToWorld(ScopedFlat, Fby::Vector2D{0.0f, 0.0f});
    FBY_CHECK(std::isfinite(ScopedSafe.X) && std::isfinite(ScopedSafe.Y));
}

void TestIkGoalComponent()
{
    Fby::SkeletonComponent ScopedSkeleton = MakeChain(3, 10.0f);
    Fby::Skeletal::IkGoalComponent ScopedGoal;
    ScopedGoal.Root = 0;
    ScopedGoal.Mid = 1;
    ScopedGoal.Tip = 2;
    ScopedGoal.Target = Fby::Vector2D{9.0f, 7.0f};
    ScopedGoal.Weight = 1.0f;
    FBY_CHECK(Fby::Skeletal::SolveIkGoal(ScopedSkeleton, ScopedGoal));
    const Fby::Vector2D ScopedTip{ScopedSkeleton.GlobalMatrices[2].m[6], ScopedSkeleton.GlobalMatrices[2].m[7]};
    FBY_CHECK(VecClose(ScopedTip, ScopedGoal.Target, 0.05f));

    // A FABRIK goal on the same chain reaches the target too.
    Fby::SkeletonComponent ScopedFabrikSkeleton = MakeChain(3, 10.0f);
    Fby::Skeletal::IkGoalComponent ScopedFabrikGoal = ScopedGoal;
    ScopedFabrikGoal.Solver = 1;
    ScopedFabrikGoal.Target = Fby::Vector2D{6.0f, 8.0f};
    FBY_CHECK(Fby::Skeletal::SolveIkGoal(ScopedFabrikSkeleton, ScopedFabrikGoal));
    const Fby::Vector2D ScopedFabrikTip{ScopedFabrikSkeleton.GlobalMatrices[2].m[6],
                                        ScopedFabrikSkeleton.GlobalMatrices[2].m[7]};
    FBY_CHECK(VecClose(ScopedFabrikTip, ScopedFabrikGoal.Target, 0.1f));

    // Invalid indices and a zero weight are refused instead of solving garbage.
    Fby::Skeletal::IkGoalComponent ScopedInvalid = ScopedGoal;
    ScopedInvalid.Mid = 200;
    FBY_CHECK(!Fby::Skeletal::SolveIkGoal(ScopedSkeleton, ScopedInvalid));
    ScopedInvalid = ScopedGoal;
    ScopedInvalid.Weight = 0.0f;
    FBY_CHECK(!Fby::Skeletal::SolveIkGoal(ScopedSkeleton, ScopedInvalid));
    ScopedInvalid = ScopedGoal;
    ScopedInvalid.Root = -1;
    FBY_CHECK(!Fby::Skeletal::SolveIkGoal(ScopedSkeleton, ScopedInvalid));
}

void TestTilemapVfsLoad()
{
    const std::filesystem::path ScopedRoot = std::filesystem::current_path() / "fby_tilemap_vfs_test";
    std::error_code ScopedError;
    std::filesystem::remove_all(ScopedRoot, ScopedError);
    std::filesystem::create_directories(ScopedRoot / "assets", ScopedError);

    Fby::TilemapComponent ScopedMap;
    ScopedMap.Grid.Mode = Fby::Grid::ProjectionMode::IsoRect;
    ScopedMap.Grid.CellSize = Fby::Vector2D{64.0f, 32.0f};
    Fby::Tilemap::Resize(ScopedMap, 6, 4);
    ScopedMap.AtlasColumns = 4;
    ScopedMap.AtlasRows = 4;
    FBY_CHECK(Fby::Tilemap::Set(ScopedMap, Fby::Grid::Cell{2, 3}, Fby::MakeTile(9, Fby::kTileFlagSolid)));
    std::string ScopedText;
    FBY_CHECK(Fby::Tilemap::SaveText(ScopedMap, &ScopedText));
    {
        std::ofstream ScopedFile(ScopedRoot / "assets" / "main.tilemap", std::ios::binary | std::ios::trunc);
        ScopedFile << ScopedText;
    }

    Fby::VirtualFileSystem ScopedVfs;
    std::string ScopedVfsError;
    FBY_CHECK(ScopedVfs.MountDirectory(ScopedRoot.string(), 0, &ScopedVfsError));
    Fby::TilemapComponent ScopedLoaded;
    std::string ScopedLoadError;
    FBY_CHECK(Fby::Tilemap::LoadFromVfs(ScopedVfs, "assets/main.tilemap", &ScopedLoaded, &ScopedLoadError));
    FBY_CHECK(ScopedLoaded.Width == 6 && ScopedLoaded.Height == 4);
    FBY_CHECK(Fby::Tilemap::At(ScopedLoaded, Fby::Grid::Cell{2, 3}) == Fby::Tilemap::At(ScopedMap, Fby::Grid::Cell{2, 3}));
    FBY_CHECK(!Fby::Tilemap::LoadFromVfs(ScopedVfs, "assets/missing.tilemap", &ScopedLoaded, &ScopedLoadError));
    FBY_CHECK(ScopedLoadError.find("cannot read") != std::string::npos);

    // The authored reference is a registered scene component.
    Fby::Scene::ComponentRegistry ScopedComponents;
    Fby::Scene::RegisterBuiltInComponents(ScopedComponents);
    FBY_CHECK(ScopedComponents.Contains("TilemapAssetRefComponent"));
    FBY_CHECK(ScopedComponents.Contains("NavAgentSettings"));
    FBY_CHECK(ScopedComponents.Contains("IkGoalComponent"));

    std::filesystem::remove_all(ScopedRoot, ScopedError);
}

void TestTilemapRefSerialization()
{
    Fby::Scene::ComponentRegistry ScopedComponents;
    Fby::Scene::RegisterBuiltInComponents(ScopedComponents);
    entt::registry ScopedRegistry;
    const Fby::Entity ScopedEntity = ScopedRegistry.create();
    Fby::TilemapAssetRefComponent ScopedRef;
    std::strncpy(ScopedRef.Path, "assets/main.tilemap", sizeof(ScopedRef.Path) - 1);
    ScopedRef.SortLayer = 3;
    ScopedRef.Flags = 1;
    ScopedRegistry.emplace<Fby::TilemapAssetRefComponent>(ScopedEntity, ScopedRef);

    const std::string ScopedToml = Fby::Scene::SaveSceneToml(ScopedRegistry, ScopedComponents);
    FBY_CHECK(ScopedToml.find("TilemapAssetRefComponent") != std::string::npos);
    FBY_CHECK(ScopedToml.find("assets/main.tilemap") != std::string::npos);

    entt::registry ScopedLoaded;
    Fby::Scene::SceneLoadOptions ScopedOptions;
    const auto ScopedResult = Fby::Scene::LoadSceneToml(ScopedLoaded, ScopedComponents, ScopedToml, ScopedOptions);
    FBY_CHECK(static_cast<bool>(ScopedResult));
    std::uint32_t ScopedRefCount = 0;
    auto ScopedLoadedView = ScopedLoaded.view<Fby::TilemapAssetRefComponent>();
    for (auto [ScopedLoadedEntity, ScopedLoadedRef] : ScopedLoadedView.each())
    {
        (void)ScopedLoadedEntity;
        ++ScopedRefCount;
        FBY_CHECK(std::strcmp(ScopedLoadedRef.Path, "assets/main.tilemap") == 0);
        FBY_CHECK(ScopedLoadedRef.SortLayer == 3);
    }
    FBY_CHECK(ScopedRefCount == 1);
}
}

int main()
{
    TestGridConversions();
    TestCameraScreenMapping();
    TestTilemap();
    TestTilemapTextRoundTrip();
    TestTilemapVfsLoad();
    TestTilemapRefSerialization();
    TestTwoBoneIk();
    TestFabrikIk();
    TestIkGoalComponent();
    TestRetarget();
    TestPoseCompression();
    TestClipCompression();
    TestClipAssetRoundTrip();

    if (GFailures != 0)
    {
        std::fprintf(stderr, "%u grid/skeletal test failure(s)\n", GFailures);
        return 1;
    }
    std::printf("grid/skeletal tests passed\n");
    return 0;
}
