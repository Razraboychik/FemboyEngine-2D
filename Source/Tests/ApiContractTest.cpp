// FemboyEngine2D - public API version and layout contract.
#include "Engine/Core/ApiVersion.hpp"
#include "Engine/ECS/Scene.hpp"
#include "Engine/ECS/Snapshot.hpp"
#include "Engine/Modding/ModAPI.h"

#include <cstddef>
#include <cstdio>
#include <type_traits>

namespace
{
template <typename T> void CheckStandardLayout(const char* InName)
{
    static_assert(std::is_standard_layout_v<T>, "stable ABI types must be standard-layout");
    std::printf("api_contract: %s size=%zu\n", InName, sizeof(T));
}
}

int main()
{
    static_assert(Fby::kModAbiVersion == FBY_MOD_API_VERSION);
    static_assert(Fby::kSceneFormatVersion == Fby::Scene::kSceneVersion);
    static_assert(Fby::kSnapshotFormatVersion == Fby::Ecs::kSnapshotVersion);

    CheckStandardLayout<FbyModQuery>("FbyModQuery");
    CheckStandardLayout<FbyModEffectDef>("FbyModEffectDef");
    CheckStandardLayout<FbyModSkillDef>("FbyModSkillDef");
    CheckStandardLayout<FbyModApi>("FbyModApi");

    FbyModApi ScopedApi{};
    ScopedApi.struct_size = static_cast<std::uint32_t>(offsetof(FbyModApi, log) + sizeof(ScopedApi.log));
    ScopedApi.abi_version = Fby::kModAbiVersion;
    if (!FbyModApiHas(&ScopedApi, ScopedApi.struct_size))
    {
        std::fprintf(stderr, "api_contract: a complete FbyModApi did not pass its size probe\n");
        return 1;
    }
    if (FbyModApiHas(&ScopedApi, ScopedApi.struct_size + 1u))
    {
        std::fprintf(stderr, "api_contract: a probe past struct_size unexpectedly succeeded\n");
        return 1;
    }
    if (FbyModApiHas(nullptr, 0))
    {
        std::fprintf(stderr, "api_contract: a null API passed the capability probe\n");
        return 1;
    }
    std::printf("api_contract: versions api=%u mod=%u scene=%u snapshot=%u\n", Fby::kEngineApiVersion,
                Fby::kModAbiVersion, Fby::kSceneFormatVersion, Fby::kSnapshotFormatVersion);
    return 0;
}
