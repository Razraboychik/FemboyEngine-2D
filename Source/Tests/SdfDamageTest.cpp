// FemboyEngine2D - SDF impact, material and local damage tests.
#include "Engine/Physics/SdfDamage.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace
{
std::uint32_t GFailures = 0;

#define FBY_CHECK(condition)                                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                  \
            ++GFailures;                                                                                               \
        }                                                                                                              \
    } while (false)

[[nodiscard]] float CustomHalfDamage(void*, const Fby::SdfImpact&, const Fby::SdfPart&,
                                     const Fby::SdfMaterialProfile&) noexcept
{
    return 0.5f;
}

void TestMaterialAndParts()
{
    using namespace Fby;

    SdfMaterialProfile ScopedMaterial{};
    ScopedMaterial.Toughness = 0.0f;
    ScopedMaterial.Hardness = 5000.0f;
    ScopedMaterial.MaximumDamageFraction = 4.0f;
    ScopedMaterial.Clamp();
    FBY_CHECK(ScopedMaterial.Toughness >= 0.01f);
    FBY_CHECK(ScopedMaterial.Hardness <= 1000.0f);
    FBY_CHECK(ScopedMaterial.MaximumDamageFraction <= 1.0f);

    SdfDamageModel ScopedModel;
    FBY_CHECK(ScopedModel.PartCount() == 0);
    SdfPart ScopedPartA{};
    ScopedPartA.Center = Vector2D{0.0f, 0.0f};
    ScopedPartA.Radius = 2.0f;
    FBY_CHECK(ScopedModel.AddPart(ScopedPartA));
    SdfPart ScopedPartB{};
    ScopedPartB.Center = Vector2D{10.0f, 0.0f};
    ScopedPartB.Radius = 2.0f;
    ScopedPartB.Integrity = 0.5f;
    FBY_CHECK(ScopedModel.AddPart(ScopedPartB));
    FBY_CHECK(ScopedModel.PartCount() == 2);

    SdfConnection ScopedConnection{};
    ScopedConnection.PartA = 0;
    ScopedConnection.PartB = 1;
    FBY_CHECK(ScopedModel.AddConnection(ScopedConnection));
    FBY_CHECK(ScopedModel.ConnectionCount() == 1);
    SdfConnection ScopedInvalid = ScopedConnection;
    ScopedInvalid.PartB = 5;
    FBY_CHECK(!ScopedModel.AddConnection(ScopedInvalid));

    FBY_CHECK(ScopedModel.FindPartAtPoint(Vector2D{0.5f, 0.0f}) == 0);
    FBY_CHECK(ScopedModel.FindPartAtPoint(Vector2D{10.0f, 0.0f}) == 1);
    FBY_CHECK(ScopedModel.FindPartAtPoint(Vector2D{5.0f, 0.0f}) == kSdfInvalidDamagePart);

    FBY_CHECK(ScopedModel.Connection(0) != nullptr);
    ScopedModel.Connection(0)->Strength = 1.0f;
    FBY_CHECK(!ScopedModel.ApplyConnectionLoad(0, 0.6f));
    FBY_CHECK(ScopedModel.ApplyConnectionLoad(0, 0.6f));
    FBY_CHECK(ScopedModel.Connection(0)->Broken());
}

void TestDamageAndBreak()
{
    using namespace Fby;

    SdfMaterialProfile ScopedMaterial{};
    SdfDamageModel ScopedModel;
    SdfPart ScopedPart{};
    ScopedPart.Center = Vector2D{0.0f, 0.0f};
    ScopedPart.Radius = 3.0f;
    FBY_CHECK(ScopedModel.AddPart(ScopedPart));
    SdfPart ScopedNeighbor{};
    ScopedNeighbor.Center = Vector2D{5.0f, 0.0f};
    ScopedNeighbor.Radius = 2.0f;
    FBY_CHECK(ScopedModel.AddPart(ScopedNeighbor));
    SdfConnection ScopedLink{};
    ScopedLink.PartA = 0;
    ScopedLink.PartB = 1;
    FBY_CHECK(ScopedModel.AddConnection(ScopedLink));

    SdfImpact ScopedImpact{};
    ScopedImpact.Point = Vector2D{0.0f, 0.0f};
    ScopedImpact.KineticEnergy = 0.2f;
    ScopedImpact.LocalThickness = 1.0f;
    SdfImpactResult ScopedResult = ScopedModel.ApplyImpactAtPoint(ScopedImpact, ScopedMaterial);
    FBY_CHECK(ScopedResult.Applied);
    FBY_CHECK(ScopedResult.PartIndex == 0);
    FBY_CHECK(std::fabs(ScopedResult.Damage - 0.2f) < 1e-5f);
    FBY_CHECK(std::fabs(ScopedResult.RemainingIntegrity - 0.8f) < 1e-5f);
    FBY_CHECK(!ScopedResult.PartBroken);

    ScopedImpact.KineticEnergy = 10.0f;
    ScopedResult = ScopedModel.ApplyImpactAtPoint(ScopedImpact, ScopedMaterial);
    FBY_CHECK(ScopedResult.Applied);
    FBY_CHECK(ScopedResult.PartBroken);
    FBY_CHECK(ScopedResult.BrokenConnections == 1);
    FBY_CHECK(ScopedModel.Part(0) != nullptr && ScopedModel.Part(0)->Broken());
    FBY_CHECK(ScopedModel.Connection(0) != nullptr && ScopedModel.Connection(0)->Broken());

    ScopedResult = ScopedModel.ApplyImpactAtPoint(ScopedImpact, ScopedMaterial);
    FBY_CHECK(!ScopedResult.Applied);
}

void TestCustomPolicy()
{
    using namespace Fby;

    SdfMaterialProfile ScopedMaterial{};
    SdfDamageModel ScopedModel;
    SdfPart ScopedPart{};
    ScopedPart.Center = Vector2D{0.0f, 0.0f};
    ScopedPart.Radius = 1.0f;
    FBY_CHECK(ScopedModel.AddPart(ScopedPart));

    SdfImpact ScopedImpact{};
    ScopedImpact.Point = Vector2D{0.0f, 0.0f};
    ScopedImpact.KineticEnergy = 1000.0f;
    const SdfImpactResult ScopedResult =
        ScopedModel.ApplyImpactAtPoint(ScopedImpact, ScopedMaterial, &CustomHalfDamage, nullptr);
    FBY_CHECK(ScopedResult.Applied);
    FBY_CHECK(std::fabs(ScopedResult.Damage - 0.5f) < 1e-5f);
    FBY_CHECK(std::fabs(ScopedModel.Part(0)->Integrity - 0.5f) < 1e-5f);
}

void TestContactAndThickness()
{
    using namespace Fby;

    constexpr std::uint32_t kSize = 32;
    std::vector<float> ScopedSamples(kSize * kSize);
    for (std::uint32_t ScopedY = 0; ScopedY < kSize; ++ScopedY)
    {
        for (std::uint32_t ScopedX = 0; ScopedX < kSize; ++ScopedX)
        {
            ScopedSamples[ScopedY * kSize + ScopedX] = static_cast<float>(ScopedX) * 0.5f - 4.0f;
        }
    }
    SdfField ScopedField{};
    ScopedField.samples = ScopedSamples.data();
    ScopedField.width = kSize;
    ScopedField.height = kSize;
    ScopedField.origin = Vector2D{0.0f, 0.0f};
    ScopedField.cellSize = Vector2D{0.5f, 0.5f};
    FBY_CHECK(ScopedField.Valid());

    const float ScopedThickness = SdfThicknessInward(ScopedField, Vector2D{4.0f, 8.0f}, Vector2D{1.0f, 0.0f}, 8.0f);
    FBY_CHECK(ScopedThickness > 0.0f);
    FBY_CHECK(ScopedThickness <= 8.0f);
    FBY_CHECK(SdfThicknessInward(ScopedField, Vector2D{30.0f, 8.0f}, Vector2D{1.0f, 0.0f}, 8.0f) == 0.0f);

    SdfContact ScopedContact{};
    ScopedContact.Point = Vector2D{4.0f, 8.0f};
    ScopedContact.Normal = Vector2D{1.0f, 0.0f};
    ScopedContact.RelativeSpeed = 20.0f;
    ScopedContact.KineticEnergy = 200.0f;
    ScopedContact.Mass = 1.0f;
    const SdfImpact ScopedImpact = MakeSdfImpact(ScopedContact, ScopedField, 8.0f);
    FBY_CHECK(std::fabs(ScopedImpact.RelativeSpeed - 20.0f) < 1e-5f);
    FBY_CHECK(std::fabs(ScopedImpact.KineticEnergy - 200.0f) < 1e-5f);
    FBY_CHECK(std::fabs(ScopedImpact.Mass - 1.0f) < 1e-5f);
    FBY_CHECK(ScopedImpact.LocalThickness > 0.0f);
}
}

int main()
{
    TestMaterialAndParts();
    TestDamageAndBreak();
    TestCustomPolicy();
    TestContactAndThickness();

    if (GFailures == 0)
    {
        std::printf("sdf_damage_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "sdf_damage_test: %u failure(s)\n", GFailures);
    return 1;
}
