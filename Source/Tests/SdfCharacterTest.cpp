// FemboyEngine2D - bone-bound SDF character evaluation tests.
#include "Engine/Physics/SdfAtlas.hpp"
#include "Engine/Physics/SdfCharacter.hpp"

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

bool Near(float InValue, float InExpected, float InTolerance = 1.0e-4f)
{
    return std::fabs(InValue - InExpected) <= InTolerance;
}

Fby::SdfCharacterComponent MakeCharacter()
{
    Fby::SdfCharacterComponent ScopedCharacter;
    ScopedCharacter.PartCount = 2;
    ScopedCharacter.Parts[0].Shape = Fby::MakeSdfShape(Fby::SdfShapeType::Circle);
    ScopedCharacter.Parts[0].Shape.Radius = 8.0f;
    ScopedCharacter.Parts[0].BoneIndex = 0;
    ScopedCharacter.Parts[0].BlendRadius = 4.0f;
    ScopedCharacter.Parts[1].Shape = Fby::MakeSdfShape(Fby::SdfShapeType::Capsule);
    ScopedCharacter.Parts[1].Shape.HalfExtents = Fby::Vector2D{10.0f, 0.0f};
    ScopedCharacter.Parts[1].Shape.Radius = 3.0f;
    ScopedCharacter.Parts[1].BoneIndex = 1;
    ScopedCharacter.Parts[1].LocalOffset = Fby::Vector2D{10.0f, 0.0f};
    ScopedCharacter.Parts[1].BlendRadius = 4.0f;
    return ScopedCharacter;
}
}

int main()
{
    Fby::SkeletonComponent ScopedSkeleton;
    Fby::SkeletonReset(ScopedSkeleton);
    const std::int16_t ScopedRoot = Fby::SkeletonAddBone(ScopedSkeleton, "root", -1,
                                                         Fby::Vector2D{0.0f, 0.0f}, 0.0f);
    const std::int16_t ScopedArm = Fby::SkeletonAddBone(ScopedSkeleton, "arm", ScopedRoot,
                                                        Fby::Vector2D{10.0f, 0.0f}, 0.0f);
    Check(ScopedRoot == 0 && ScopedArm == 1, "skeleton bones are created");
    Fby::SkeletonBindPose(ScopedSkeleton);
    Fby::SkeletalAnimation::Evaluate(ScopedSkeleton);

    const Fby::SdfCharacterComponent ScopedCharacter = MakeCharacter();
    Check(Fby::SdfCharacterCoversPoint(ScopedSkeleton, ScopedCharacter, Fby::Vector2D{0.0f, 0.0f}),
          "the root circle is solid");
    Check(Fby::SdfCharacterCoversPoint(ScopedSkeleton, ScopedCharacter, Fby::Vector2D{20.0f, 0.0f}),
          "the arm capsule is solid");
    Check(!Fby::SdfCharacterCoversPoint(ScopedSkeleton, ScopedCharacter, Fby::Vector2D{80.0f, 0.0f}),
          "points far outside are empty");

    const float ScopedInside = Fby::SdfCharacterDistance(ScopedSkeleton, ScopedCharacter, Fby::Vector2D{0.0f, 0.0f});
    const float ScopedOutside =
        Fby::SdfCharacterDistance(ScopedSkeleton, ScopedCharacter, Fby::Vector2D{80.0f, 0.0f});
    Check(ScopedInside < 0.0f, "the signed distance is negative inside");
    Check(ScopedOutside > 0.0f, "the signed distance is positive outside");
    const Fby::Vector2D ScopedGradient =
        Fby::SdfCharacterGradient(ScopedSkeleton, ScopedCharacter, Fby::Vector2D{7.5f, 0.0f});
    Check(std::fabs(ScopedGradient.X) > 0.5f, "the gradient points out of the surface");

    // Binding follows the bone pose, not a cached mesh.
    const float ScopedBefore = Fby::SdfCharacterDistance(ScopedSkeleton, ScopedCharacter, Fby::Vector2D{20.0f, 12.0f});
    ScopedSkeleton.LocalRotations[ScopedArm] = 1.2f;
    Fby::SkeletalAnimation::Evaluate(ScopedSkeleton);
    const float ScopedAfter = Fby::SdfCharacterDistance(ScopedSkeleton, ScopedCharacter, Fby::Vector2D{20.0f, 12.0f});
    Check(!Near(ScopedBefore, ScopedAfter, 1.0e-3f), "the field deforms with the bone pose");

    Fby::Vector2D ScopedBoundsMin;
    Fby::Vector2D ScopedBoundsMax;
    Check(Fby::SdfCharacterBounds(ScopedSkeleton, ScopedCharacter, &ScopedBoundsMin, &ScopedBoundsMax),
          "the posed character has bounds");
    Check(ScopedBoundsMin.X < ScopedBoundsMax.X && ScopedBoundsMin.Y < ScopedBoundsMax.Y,
          "the bounds are ordered");

    // The baked field agrees with direct evaluation at the sample points.
    Fby::SdfCharacterField ScopedField;
    Check(Fby::BakeSdfCharacterField(ScopedSkeleton, ScopedCharacter, Fby::Vector2D{-64.0f, -64.0f},
                                     Fby::Vector2D{64.0f, 64.0f}, 65, 65, &ScopedField),
          "the character field bakes");
    Check(ScopedField.Valid(), "the baked field has samples");
    Check(ScopedField.Sample(Fby::Vector2D{0.0f, 0.0f}) < 0.0f, "the baked field is solid inside");
    Check(ScopedField.Sample(Fby::Vector2D{63.0f, 63.0f}) > 0.0f, "the baked field is empty outside");
    Check(Fby::BakeSdfCharacterField(ScopedSkeleton, ScopedCharacter, Fby::Vector2D{-64.0f, -64.0f},
                                     Fby::Vector2D{64.0f, 64.0f}, 1024, 1024, &ScopedField) == false,
          "the field refuses an unbounded resolution");

    Fby::SdfCharacterMeshData ScopedMesh;
    Check(Fby::BuildSdfCharacterMesh(ScopedField, 0.0f, &ScopedMesh), "the character field builds a render mesh");
    Check(!ScopedMesh.Positions.empty() && ScopedMesh.Indices.size() % 6u == 0,
          "the render mesh has triangle quads");

    Fby::SdfBrickMap ScopedBrickMap;
    Check(Fby::BuildSdfCharacterBrickMap(ScopedField, 0.0f, &ScopedBrickMap), "the character field builds a brick map");
    Fby::SdfAtlasImage ScopedAtlas;
    Check(Fby::PackSdfAtlas(ScopedBrickMap, &ScopedAtlas), "the character brick map packs into an atlas");
    Check(ScopedAtlas.Width > 0 && ScopedAtlas.Height > 0, "the atlas has a GPU-ready image");

    Check(Fby::SdfSmoothMin(1.0f, 2.0f, 0.0f) == 1.0f, "a zero blend is a hard minimum");
    Check(Fby::SdfSmoothMin(1.0f, 2.0f, 2.0f) <= 1.0f, "a smooth minimum rounds the union");

    // No distance LOD: the far and near evaluations use the same path.
    const float ScopedFar = Fby::SdfCharacterDistance(ScopedSkeleton, ScopedCharacter, Fby::Vector2D{1000.0f, 0.0f});
    const float ScopedFarAgain =
        Fby::SdfCharacterDistance(ScopedSkeleton, ScopedCharacter, Fby::Vector2D{2000.0f, 0.0f});
    Check(ScopedFar > 0.0f && ScopedFarAgain > ScopedFar, "distance keeps increasing without an LOD switch");

    if (GFailures == 0)
    {
        std::printf("sdf_character: OK\n");
        return 0;
    }
    std::fprintf(stderr, "sdf_character: %d failure(s)\n", GFailures);
    return 1;
}
