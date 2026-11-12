// FemboyEngine2D - particle preset registry and .fbyparticle pack tests.
#include "Engine/Effects/ParticlePreset.hpp"

#include <cstdint>
#include <cstdio>
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
            ++GFailures;                                                                                               \
        }                                                                                                              \
    } while (false)

constexpr std::string_view kTestToml = R"toml(
[[presets]]
id = "Test.Pack.Spark"
name = "Test Spark"
category = "Test"
shape = "Spark"
emission_shape = "Point"
max_particles = 120
seed = 42
direction = [0.0, -1.0]
gravity = [0.0, 250.0]
speed = [100.0, 220.0]
lifetime = [0.3, 0.7]
size = [3.0, 0.0]
rate = 90.0
burst_count = 4
burst_interval = 0.2
duration = 1.5
loop = true
additive = true
spread = 0.8
drag = 1.0
turbulence = 12.0
swirl = 0.0
colors = ["#FFE060FF", "#FF602000"]
)toml";
}

int main()
{
    using namespace Fby;

    ParticlePresetRegistry ScopedRegistry;
    ScopedRegistry.RegisterBuiltIns();
    FBY_CHECK(ScopedRegistry.Count() == 16);
    FBY_CHECK(ScopedRegistry.Find("Particle.Fire") != nullptr);
    ScopedRegistry.RegisterBuiltIns();
    FBY_CHECK(ScopedRegistry.Count() == 16);

    std::string ScopedError;
    FBY_CHECK(LoadParticlePresetToml(kTestToml, ScopedRegistry, nullptr, &ScopedError));
    FBY_CHECK(ScopedError.empty());
    FBY_CHECK(ScopedRegistry.Count() == 17);
    const ParticlePresetDesc* ScopedPreset = ScopedRegistry.Find("Test.Pack.Spark");
    FBY_CHECK(ScopedPreset != nullptr);
    if (ScopedPreset != nullptr)
    {
        FBY_CHECK(ScopedPreset->Settings.Shape == ParticleShape::Spark);
        FBY_CHECK(ScopedPreset->Settings.MaxParticles == 120);
        FBY_CHECK(ScopedPreset->Settings.BurstCount == 4);
        FBY_CHECK(ScopedPreset->Settings.Additive);
        FBY_CHECK(ScopedPreset->Settings.SpeedMin > 99.9f);
        FBY_CHECK(ScopedPreset->Settings.EndColor.A() == 0);
    }

    ParticlePresetDesc ScopedDuplicate{};
    ScopedDuplicate.Id[0] = 'X';
    ScopedDuplicate.DisplayName[0] = 'X';
    FBY_CHECK(ScopedRegistry.Add(ScopedDuplicate, nullptr, &ScopedError));
    FBY_CHECK(!ScopedRegistry.Add(ScopedDuplicate, nullptr, &ScopedError));
    FBY_CHECK(!ScopedError.empty());

    const int ScopedOwner = 7;
    ParticlePresetDesc ScopedOwned{};
    std::snprintf(ScopedOwned.Id, sizeof(ScopedOwned.Id), "%s", "Test.Owned");
    std::snprintf(ScopedOwned.DisplayName, sizeof(ScopedOwned.DisplayName), "%s", "Owned");
    FBY_CHECK(ScopedRegistry.Add(ScopedOwned, &ScopedOwner));
    FBY_CHECK(ScopedRegistry.Find("Test.Owned") != nullptr);
    ScopedRegistry.RemoveOwner(&ScopedOwner);
    FBY_CHECK(ScopedRegistry.Find("Test.Owned") == nullptr);

    std::string ScopedInvalid = "[[presets]]\nid = \"Test.Bad\"\nshape = \"Nope\"\n";
    FBY_CHECK(!LoadParticlePresetToml(ScopedInvalid, ScopedRegistry, nullptr, &ScopedError));
    FBY_CHECK(!ScopedError.empty());

#if defined(FBY_TEST_PARTICLE_PACKS_DIR)
    ParticlePresetRegistry ScopedPacks;
    ScopedPacks.RegisterBuiltIns();
    const auto ScopedLoad = [&ScopedPacks](const char* InName)
    {
        const std::string ScopedPath = std::string(FBY_TEST_PARTICLE_PACKS_DIR) + "/" + InName;
        std::string ScopedPackError;
        const bool ScopedOk = LoadParticlePresetFile(ScopedPath, ScopedPacks, nullptr, &ScopedPackError);
        if (!ScopedOk)
        {
            std::fprintf(stderr, "pack load failed: %s\n", ScopedPackError.c_str());
        }
        return ScopedOk;
    };
    FBY_CHECK(ScopedLoad("pixel_fx_combat.fbyparticle"));
    FBY_CHECK(ScopedLoad("pixel_fx_ambient.fbyparticle"));
    FBY_CHECK(ScopedLoad("pixel_fx_status.fbyparticle"));
    FBY_CHECK(ScopedPacks.Count() == 16 + 18);
    FBY_CHECK(ScopedPacks.Find("PixelFX.Combat.Sparks") != nullptr);
    FBY_CHECK(ScopedPacks.Find("PixelFX.Ambient.Fireflies") != nullptr);
    FBY_CHECK(ScopedPacks.Find("PixelFX.Status.Poison") != nullptr);
#endif

    if (GFailures != 0)
    {
        std::fprintf(stderr, "%u particle preset failure(s)\n", GFailures);
        return 1;
    }
    std::printf("particle presets: all checks passed\n");
    return 0;
}
