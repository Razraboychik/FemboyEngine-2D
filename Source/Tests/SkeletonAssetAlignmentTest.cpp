// FemboyEngine2D - skeleton asset alignment rejection.
#include "Engine/ECS/SkeletonAsset.hpp"

#include <cstdio>
#include <span>
#include <string>
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
}

int main()
{
    std::vector<std::byte> ScopedStorage(512);
    const std::span<const std::byte> ScopedMisaligned = std::span<const std::byte>(ScopedStorage).subspan(1, 128);
    const auto ScopedResult = Fby::Skeleton::DecodeSkeletonAsset(ScopedMisaligned);
    Check(!ScopedResult.has_value(), "a misaligned skeleton buffer is rejected");
    Check(ScopedResult.error().find("aligned") != std::string::npos, "the rejection names the alignment requirement");

    if (GFailures != 0)
    {
        std::fprintf(stderr, "%d skeleton alignment failure(s)\n", GFailures);
        return 1;
    }
    std::printf("skeleton_alignment: OK\n");
    return 0;
}
